// session.cpp - M0 session layer.
//
// Responsibilities kept deliberately small for M0: choose a backend (player's choice,
// never inferred), handshake with a version check that REFUSES mismatched builds,
// own the uid->owner authority map, and pump inbound messages on the main thread.
//
// Architecture commitment 1 (PLAN section 3): ONE module answers "who owns entity X".
// That module is this one, and the map lives here so no subsystem can grow its own.
//
// Architecture commitment 5: divergence detection ships with M0, not after the first
// desync mystery. The probe scaffolding is below and is exercised from M2 onward.

#include "session.h"
#include "../handoff.h" // M-D: ApplyRemoteXfer / Ack
#include "../../common/holdwire.h"   /* P105 build 2 (protocol 126): HOLD / LAND, the 'HLD1' / 'LND1' / 'RQT1' trailers */
#include "../../common/squadlead.h"   /* T-1 B1 restructure: MSG_SQUAD_LEAD encode / decode */
/* T-1 B3 restructure (protocol 89): MSG_KEEPER and src/common/keeperwire.h retired - the squad's money rides MSG_SQUAD_LEAD */
#include "transport.h"
#include "steam_probe.h"   /* T-290 S0: the probe's counters ride the [net] REPORT line */
#include "../upnp.h"   /* T-53: the router port counters ride the [net] REPORT line */
#include "../ai_spike.h"
#include "../spawn.h"
#include "../medical.h"   /* par5 (parity P5): ApplyOwnerHunger */
#include "../store.h"   // the arrival queue (InQueueEnqueue) and the store link's notebook address
#include "../doors.h"   /* E45 (P8e): ApplyDoorState */
#include "../speech.h"  /* P3: ApplyRemoteSay, SayNoteMalformed, SayCountsString */
#include "../stats.h"   /* S1: StatsReadOwned, ApplyRemoteStats and the SPAWN-tail counters */
#include "../../common/ownedmirror.h"   /* O1 (recheck-c2b): the owned-uid mirror a worker thread may read */
#include "../../common/uidlayout.h"   /* M4 fold: SpawnUidDecide - a SPAWN naming a uid this game runs is refused */
#include "../../common/uidtable.h"    /* T-354: MSG_NOT_SHOWN's bytes and decisions */
#include "../../common/lostcopy.h"    /* MSG_RESEND's bytes and the lost-copy book */
#include "../zones.h"                 /* ZoneBuildingsInHereTri - a lost copy is asked for only where the engine's zone is loaded */
#include <deque>
#include "../../common/liveenvelope.h"   /* M5a: the notebook road's route and the relayed sender id */
#include "../../common/liverelay.h"   /* M7a: the character stream's road, route and AREA target */
#include "../../common/liveowner.h"   /* M7a A1 build 1 [a1b1-sp1i]: generations, receipts, ROSTER, the road to one game */
#include "../../common/ownerroute.h"   /* M7b slice 1: a request to its target's owner, an answer back to its asker */
#include "../../common/peergone.h"   /* M8: PLAYER_GONE and the per-player removal rule */
#include "../../common/saywire.h"   /* P3: the MSG_SAY payload - one pure pair the offline suite hits */
#include "../../common/crimewire.h"  /* crime3: the MSG_CRIME payload */
#include "../../common/bountywire.h" /* crime5: the MSG_BOUNTY payload */
#include "../../common/prisonwire.h" /* arrest2: the MSG_PRISON payload */
#include "../../common/treatwire.h"  /* heal1: the MSG_TREAT payload */
#include "../../common/insidewire.h"   /* P25 fold 2: the MSG_INSIDE payload */
#include "../../common/namewire.h"   /* names1: the MSG_NAME payload */
#include "../../common/slavewire.h"  /* slave1: the MSG_SLAVE payload */
#include "../../common/shotwire.h"   /* P104 fix: the MSG_SHOT payload */
#include "../../common/effectwire.h"   /* T-327: the MSG_EFFECT payload and its road */
#include "../effect.h"                 /* T-327: EffectOnNet */
#include "../hire.h"   /* recruit1: MSG_HIRE - HireNoteRecv / HireForgetPeer */
#include "../../common/buildwire.h"  /* build1-b: the MSG_BUILD payload */
#include "../build.h"                 /* build1-b: BuildNoteRecv, BuildNoteBad */
#include "../../common/farmwire.h"   /* par16: MSG_BUILD kinds 6 FARM / 7 FARM_OP */
#include "../farm.h"                  /* par16: FarmNoteRecv */
#include "../crime.h"                /* crime3: ApplyRemoteCrime */
#include "../../common/statewire.h"   /* K1 (read-carry): the MSG_STATE payload - SendState and OnState call these */
#include "../../common/spawnage.h"   /* P1 (animal age): the SPAWN payload is encoded and decoded by one pure pair the offline suite hits */
#include "../../common/doorsync.h"   /* P8n-b (review-p8n L-8): DoorParseOrigin - the trailing byte is parsed by ONE pure function, and that function is in the offline suite */
#include "../../common/storelink.h"   /* join1: NotebookAddrForJoiner - a loopback notebook address in the WELCOME means the host's machine */
#include "../../common/gamelink.h"   /* link1 (T416): the game link's ENet timeouts and the joiner's in-world re-dial */
#include "../soak.h"   /* link1: GameplayRunning - the re-dial never runs at the title screen */
#include "../relations.h"   // P3 piece 2: ApplyRemoteRelation
#include "../playerfaction.h"   /* stand1 fold (1d): NoteHeldForSlot */
#include "../../common/slotwire.h"   /* stand1 fold (1d): WireLacksSlot */
#include "../replicate.h"
#include "../combat.h"
#include "../appearance.h"
#include "../appearance_record.h"
#include "../clothing.h"
#include "../items.h"      /* E22a / decision 38: ItemMoveMsg + ApplyItemMove */
#include "../worldsync.h"   // F302: LocalViewDistance, the outbound half of the HELLO field
#include "../config.h"      /* E38 / decision 42: the host forwards the CONFIG'S notebook address, never its own g_linkAddr */
#include "../../common/worlddir.h"   /* mp4: DisplayNameOk - the name in a HELLO passes the rule playername= does */

#include "../coop_log.h"

#include <map>
#include <set>
#include <vector>
#include <sstream>
#include <locale>
#include <cstring>

#include <Windows.h>   /* E16: InterlockedIncrement64. Last of the includes, behind every engine header - the order the other plugin translation units use. */

namespace coop { int PeerPlayerSectorsTS(int* slots, int* xs, int* ys, int cap); }   /* M7a A1 build 1 [a1b1-sp1f]: zones.cpp - the fresh player-sector rows of this link (every slot, this game's too), -1 = no fresh table */
namespace coop { void WorldStateOnOwnershipReleased(unsigned int uid, const void* character); }   /* worldstate.cpp - a dual run's yield is a release */
namespace coop { void OnPlayerGone(unsigned int slot, const char* how, int standIn); }   /* below, in coop: the return check handles a leave it missed as PLAYER_GONE's (standIn 1: no take here) */
namespace coop { int CopyIsPlayerCharacter(unsigned int uid); }   /* spawn.cpp: 1 the player character / a player-faction person, 0 an NPC, -1 unreadable, -2 no body */
namespace coop {
namespace net {

// E16: defined here, at namespace scope and OUTSIDE the anonymous namespace below, because spawn.cpp's [M1] REPORT
// reads it - an anonymous-namespace global has internal linkage and cannot be reached from another file. Declared in
// session.h, so the increments below (which come first in this file) already have it in scope.
volatile __int64 g_remoteMayWriteRefused = 0;

namespace {

// Bumped whenever the wire format or message semantics change. Both peers must match
// exactly - a co-op session between mismatched builds desyncs in ways that look like
// engine bugs, which is the most expensive kind of confusion to debug.
// Bumped to 2 (F115): SPAWN now carries the faction name and the container flag. A
// mismatched pair must REFUSE rather than misparse - a silently misread payload is exactly
// the failure class this project keeps finding, and the handshake already disconnects on
// mismatch, so the version bump converts a subtle corruption into a loud refusal.
// 7 (2026-08-06): HIT carries the attacker's engine handle (parity P-2).
// 8 (2026-08-06): the per-part record grows to 8 floats - `age` and `healthScale` join it,
//                 because they are the other two factors in the ceiling the engine clamps
//                 `flesh` against (F131). The version is bumped for every payload-shape
//                 change so mismatched builds refuse at the handshake rather than misparse
//                 each other's bytes.
// 9 (2026-08-06): STATE carries the MedicalSystem latch block (F147) - the peer's own collapse
//                 decision reads `unconcious`, and it was holding 0 while the authority held 1.
// 15 (2026-08-07): HELLO carries the sender's viewDistance (F302). The adoption radius decides
//                  what the CLIENT needs sent to it, so it is the CLIENT's setting - and the host,
//                  which does all the adopting, cannot read it locally.
// 16 (2026-08-07): MSG_DESPAWN (F322). The peer was never told when a character died, so the two
//                  worlds diverged by attrition - the last known structural gap on this path.
// 17 (2026-08-08): MSG_SWING (F348). Combat mode told the peer a fight was on; nothing ever told
//                  it a blow was being thrown, and the user reported exactly that - people in the
//                  right places, not swinging.
LinkState g_lastLinkState = LINK_DOWN;   // the session edge memory (review-s6 H1)
/* ---- P7v (design-noworld-queue 5) - THE SESSION LINK'S OWN GENERATION AND THE DEFERRED REFUSAL ----
   g_sessionLinkGen is bumped on the UP and DOWN edges and stamped onto every queued session entry, so a
   session bounce discards that link's entries and the NOTEBOOK's bounce does not touch them (review-p7p H-2
   kept the wrong half; H-3 gave session entries no edge at all).
   g_connectGenSeen mirrors the transport's monotonic connect counter: ENet's Poll can drain a DISCONNECT and
   a CONNECT in one call, and a state comparison cannot see that pair (review-p7h M-5).
   g_sessionLeavePending is the latch a protocol refusal sets. The refusal used to call SessionLeave() ->
   OnPeerGone() straight out of the pump, walking and destroying ENGINE OBJECTS inside GameWorld::resetGame
   (review-p7h H-1, restated by review-p7q H-2). It queues a kActSessionLeave action instead, and while the
   latch is set the poll loop breaks: nothing further from a session being refused is worth queueing. */
volatile long g_sessionLinkGen = 0;
volatile long g_hostClosingEpisode = 0;   /* M11a S3 review fold (F2): +1 on a link UP and on a leave - the events that clear g_hostClosingSeen (SessionClosingEpisode) */
unsigned int g_connectGenSeen = 0;
int g_sessionLeavePending = 0;
long long g_helloViewDistanceUnknown = 0, g_helloResentWithViewDistance = 0;
/* THE ONE EARLY RETURN IN SessionPerformAction THAT WOULD OTHERWISE BOOK NOTHING (review-p7p M-3's
   shape): a queued HELLO resend reached the drain and did not go out, because the link had gone, or
   this game is the host, or the predicate was somehow blocked. A resend that silently does not happen
   leaves the host adopting against a view distance of 0 with nothing saying so. */
long long g_helloResendSkipped = 0;
long long g_welcomeProtocolRefused = 0, g_helloProtocolRefused = 0;
/* The three refusal sites - OnHello's malformed branch, OnHello's version branch and OnWelcome's version
   branch - all come through here. The design named only OnWelcome; OnHello is in the SAME inline table and
   reaches the SAME OnPeerGone, so closing one and not the other would leave the hole open under a new name. */
void SessionRefuseAndLeaveLater(const char* why, long long* counter)
{
    if (counter != 0) ++(*counter);
    if (g_sessionLeavePending != 0) return;
    g_sessionLeavePending = 1;
    std::vector<char> none;
    coop::InQueueEnqueue(coop::kActSessionLeave, coop::kOriginLocal, coop::kScopeSave, coop::kClassEdge, 0, 0, none);
    ErrorLog(std::string("[net] the session is REFUSED (") + why + ") and the TEARDOWN IS QUEUED, not run here"
             " (P7v, review-p7h H-1). SessionLeave calls OnPeerGone, which walks and DESTROYS engine objects,"
             " and this handler can be reached from the load gate's pump - i.e. from inside"
             " GameWorld::resetGame. The drain performs the leave on the first tick with no engine write"
             " blocked, which is by construction outside every gate. The gate's own !SessionLinked() break"
             " still ends its wait: the refusal never reached SetStoreServer, so the notebook link stays 0.");
}
const unsigned int kProtocolVersion = 148;   /* the game-to-game protocol: raise it when any message's meaning changes (an older game is then refused at HELLO / WELCOME); the reason goes in the commit message (owner 355 / 356, 2026-10-02) */

// A body has 7 parts in this build (T032/T033/T034, every character, both instances). The
// cap is a bound on a network-supplied count, not a belief about anatomy - a peer claiming
// more is malformed, and ApplyHealth refuses any count that disagrees with ours anyway.
const unsigned int kMaxHealthParts = 32;

ITransport* g_transport = 0;
bool        g_isHost    = false;
int         g_hostClosingSeen = 0;   /* mmo5: this joiner received SESSION_CLOSING (coopgl::HostClosingFlagStep; the fold moved it here - SessionOnLinkUp clears it) */
unsigned int g_myPeerId = 0;
/* M5b: PEER_SLOT (the session peer's player number, said by the peer itself - OnPeerSlot) and the proof verb's MOVE road. */
long g_slotReadyGen = -1, g_slotSentGen = -1; int g_slotSentValue = -1;
long long g_peerSlotSent = 0, g_peerSlotRecv = 0, g_peerSlotSelf = 0, g_peerSlotBad = 0, g_peerSlotChanged = 0, g_ownerRekeyed = 0;
long long g_relayOwnIn = 0, g_relayOwnRefused = 0, g_moveViaLive = 0, g_moveViaLiveFailed = 0; int g_moveViaLiveBudget = 0;
/* MOVESTOP: sent by the road taken (0 session link, 1 world server), received by the road it came by, too short, refused (not the uid's owner) */
long long g_moveStopSent[2] = { 0, 0 }, g_moveStopRecv[2] = { 0, 0 }, g_moveStopShort = 0, g_moveStopRefused = 0;
/* M5b fold 1 (item 6): UidOwnedByPeer's refusals. Read ONLY as a before/after difference around a relayed message's dispatch
   (SessionDispatchQueued -> relayOwn[refused]); never reported alone, and never folded into g_remoteMayWriteRefused. */
long long g_uidOwnedByPeerRefused = 0;
/* E38: WHAT THIS SESSION IS, so a repeated `host`/`join` for the address already in use is a no-op instead of a
   teardown. Written only by SessionHost/SessionJoin, beside g_isHost, and read only by their own guards. */
unsigned short g_hostPort = 0, g_joinPort = 0;
std::string    g_joinAddr;
/* link1 (T416): the joiner's in-world re-dial of its game link. One SERIES runs from the drop being seen to the next
   link UP; it is closed (abandoned) by SessionLeave. Counted on the [net] REPORT line as gameRedial[...]. */
static int          g_glSeriesOpen = 0;
static unsigned int g_glAttempts = 0;     /* in the open series */
static DWORD        g_glLastMs = 0;       /* the interval's start: the drop, or the end of the last CONNECTING */
static long long    g_glSeries = 0, g_glAttemptsTotal = 0, g_glRelinked = 0, g_glDialFailed = 0, g_glAbandoned = 0;
BackendId      g_hostBackend = BACKEND_NONE;

// Ping bookkeeping (RTT measured by us, independent of ENet's own estimate).
unsigned long long g_pingSeq      = 0;
unsigned long long g_pingSentTick = 0;
// In-game pump ticks. FRAME-LOCKED, ~118/s on this machine (F071) - NOT the ~900/s figure
// from F041, which is the title-screen pump. Ticks are an ordering aid, not a clock.
unsigned long long g_tick         = 0;

// P1 (animal age): [net] REPORT spawnAge[sent,unread,absent,clamped]. `sent` = SPAWNs that carried an age read
// from an ANIMAL's +0x700; `unread` = SPAWNs whose character was a human or could not be read (age 0.0 went);
// `absent` = inbound SPAWNs from a pre-45 sender (no age: 0.0); `clamped` = inbound ages that were NaN / out of [0,1].
long long g_spawnUidOwnRefused = 0;   // M4 fold (defence in depth): SPAWNs whose uid names a character THIS game runs - refused; must read 0
long long g_spawnAgeSent = 0, g_spawnAgeUnread = 0, g_spawnAgeAbsent = 0, g_spawnAgeClamped = 0;

// --- Authority map (commitment 1) -------------------------------------------------
// uid -> owning peer id. Host is peer 0. A uid absent from the map is UNOWNED, which
// is deliberately distinct from "owned by host": unowned means we have not been told,
// and the invariant (commitment 3) is that we never write state we do not own, so an
// unknown uid is refused rather than assumed.
std::map<unsigned int, unsigned int> g_owner;

// F278. Uids THIS PROCESS created or adopted, and it is deliberately SEPARATE from g_owner and
// deliberately NEVER cleared.
//
// g_owner answers "who told us they own this", and that is session-scoped: it is wiped on leave and
// on host/join, correctly, because a remote peer's claims die with the session that carried them.
// Authorship does not. T023/F092 settled this once already, in the opposite direction - ownership
// is recorded at creation rather than on a successful send, because "authorship is what confers
// authority, and whether a peer has been told is a different question."
//
// An adversarial review found the same principle broken at the other end: with ownership living
// only in g_owner, a leave->rejoin wiped it, and every character this instance had created or
// adopted became un-owned FOREVER. It would never stream again (replicate.cpp skips !IsUidMine),
// never be re-announced (the world sweep still saw it as adopted), take the PUPPET branch in the
// combat detour on the very machine that is its authority, and - with medgate on - have its
// medical simulation suppressed outright. A plain lifecycle mismatch, no threading required.
std::set<unsigned int> g_localOwned;
/* M7a A1 build 1 [a1b1-sp1] (design 2.1 [review F2]) - EVERY PERSON'S OWNERSHIP CARRIES A GENERATION, kept in two tables with the two
   lifetimes: g_mineGen (uid -> gen of a person THIS game runs or ran) lives exactly as long as g_localOwned - never cleared, erased only
   where g_localOwned erases (ReleaseLocalOwner, which moves the gen to the copy table); g_copyGen (uid -> gen of a copy of another
   game's person, 0 / absent = unknown) is cleared and erased together with g_owner. The higher generation wins everywhere
   (src/common/liveowner.h). SPAWN carries no gen: a copy's gen is stamped by OWNER_MOVED and the ROSTER. MAIN THREAD. */
std::map<unsigned int, unsigned int> g_mineGen;
std::map<unsigned int, unsigned int> g_copyGen;
/* the receipts this game owes: the sender's player key -> the seqs of its UNLOADs / DESPAWNs that listed this game's slot (design 2.6) */
std::map<unsigned int, std::vector<unsigned int> > g_receiptOut;
long long g_wdStale = 0, g_wdMalformed = 0;   /* withdrawals ignored as stale (the copy here carries a higher gen) / refused as malformed */
// O1 (recheck-c2b): the SAME set as a fixed lock-free table, written right beside every g_localOwned insert/erase
// (SetLocalOwner, TakeLocalOwner, ReleaseLocalOwner - there is no clear). g_localOwned is a std::set the MAIN thread
// mutates; a worker-thread find racing an erase can walk a freed node. IsUidMineAnyThread reads this instead.
// O1-b: two static 32768-slot tables, one published; SessionTick rebuilds the spare and swaps it in.
coopown::OwnedMirror g_ownedMirror;

/* M5b (T-197 piece 5) - OWNERSHIP BY PLAYER NUMBER. An owner is recorded and compared as a PLAYER KEY (cooplive::PlayerKeyOf):
   the permanent slot of the owning player whenever it is known - a relayed sender carries it, the session peer said it with
   PEER_SLOT - so the owner's message is a match on either road; the raw transport id only while no slot is known. */
unsigned int OwnerKeyOf(unsigned int sender) { return cooplive::PlayerKeyOf(sender, coop::LinkPeerSlot()); }
bool OwnerMatch(unsigned int recorded, unsigned int sender) { return cooplive::SamePlayer(recorded, sender, coop::LinkPeerSlot()); }

// SPLIT-BRAIN GUARD. IsUidMine answers from g_localOwned (F279) while the four inbound handlers
// answered from g_owner, so there was a reachable state in which we act as a uid's authority AND
// accept the remote peer as its authority for the same uid.
//
// It is reachable because OnSpawn writes g_owner[uid] = sender for ANY uid the peer names. Until protocol 97 uids
// were (pidPrefix << 24) | counter with an 8-bit prefix derived from the process id - two processes collided
// about 1 in 256, and then totally, because both counters start at 1. Symptom: the character teleports between
// the two instances' positions, because we stream our own MOVE for it while applying theirs.
// M4 (owner 203 / 205 A, 2026-09-29): a uid is now (the MAKING game's 10-bit SEAT << 22) | a 22-bit counter the
// notebook grants in blocks (src/common/uidlayout.h, uidblock.h, spawn.cpp AllocateUid). Two connected games never hold
// one seat and a seat's counters never repeat in the world's life, so two games no longer mint the same uid. The uid is ONLY A NAME: who may write a character is still decided here
// (g_localOwned / g_owner) and by the hand-over record - never by UidSlot(uid), because a character handed to
// another game keeps the uid its maker minted. This guard stays for any uid a peer names wrongly.
//
// Authorship wins. If we created or adopted it, no remote message may write it.
// E16 (F528): both refusals are counted. The first is authorship winning over a remote claim; the second is a uid whose
// owner is nobody we know, or somebody other than the sender. They are one number deliberately - the question the [M1]
// REPORT answers is "did this game silently drop remote writes at all", and until it reads non-zero there is nothing to
// split. Interlocked because the handlers this feeds run on the pump while nothing guarantees no other caller ever will.
bool RemoteMayWrite(unsigned int uid, unsigned int fromPeer)
{
    if (g_localOwned.find(uid) != g_localOwned.end()) { ::InterlockedIncrement64(&g_remoteMayWriteRefused); return false; }
    std::map<unsigned int, unsigned int>::const_iterator own = g_owner.find(uid);
    const bool may = (own != g_owner.end() && OwnerMatch(own->second, fromPeer));   /* M5b: by player number */
    if (!may) ::InterlockedIncrement64(&g_remoteMayWriteRefused);
    return may;
}


// Inbound STATE count. Its whole purpose is that receipt stops being an inference.
long long g_stateRecv = 0;

// review-session S6 - the link-drop cleanup, counted so a session that had one is not indistinguishable
// from a session that never lost its peer. Cumulative across reconnects within one game session.
long long g_peerGoneEvents = 0, g_peerGoneClaims = 0;
/* M8 (T-197 piece 8; src/common/peergone.h): the session peer's rows captured at the link's DOWN edge (uid -> the key it had then),
   removed by whichever road runs first; a later claim of the uid erases its mark. MAIN THREAD. */
std::map<unsigned int, unsigned int> g_departedPending;
long long g_peerGoneCaptured = 0, g_peerGoneAlreadyGone = 0, g_peerGoneDiscardSweeps = 0;   /* M8: rows captured at DOWN edges; captured rows found removed (by PLAYER_GONE) or re-claimed; sweeps of a discarded action */
long long g_pgRecv = 0, g_pgUidsDropped = 0, g_pgOwnersCleared = 0, g_pgLoadedBitArmed = 0, g_pgDestroyed = 0, g_pgWithdrawn = 0, g_pgAbsent = 0, g_pgApplied = 0, g_pgSelf = 0, g_pgMalformed = 0, g_pgSkippedSessionPeer = 0; int g_lastCapturedSessionSlot = -1;   /* M8: PLAYER_GONE (loadedBitArmed: the departed player's loaded bit cleared and armed - not the M4 uid seat; M8 review F1: skippedSessionPeer - it named the session peer while that link was up) */

std::string N(long long v)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

void PutU32(std::vector<char>* b, unsigned int v)
{
    size_t at = b->size();
    b->resize(at + 4);
    std::memcpy(&(*b)[at], &v, 4);
}

bool GetU32(const std::vector<char>& b, size_t at, unsigned int* out)
{
    if (b.size() < at + 4) return false;
    std::memcpy(out, &b[at], 4);
    return true;
}

/* Length-prefixed strings. These used to live beside the RECORD encoder six hundred lines below; E38 needs them
   in MSG_WELCOME, which is encoded near the top of this file, so the DEFINITIONS moved here rather than a
   forward declaration being added down there - one declaration of each, so a future signature change cannot
   leave a stale second one behind. GetStr advances `at` past the field it read and refuses a length the buffer
   cannot hold, so a truncated or malformed payload stops the decode instead of reading off the end. */
static void PutStr(std::vector<char>* b, const std::string& v) { PutU32(b, (unsigned int)v.size()); b->insert(b->end(), v.begin(), v.end()); }
static bool GetStr(const std::vector<char>& b, size_t* at, std::string* out)
{
    unsigned int n = 0; if (!GetU32(b, *at, &n) || b.size() < *at + 4 + n) return false;
    /* P7f (review-p6z L-1): an EMPTY trailing field passes the bound check with *at + 4 == b.size(), and the
       assign then takes &b[b.size()] - undefined, and a debug-CRT assert. E38 makes that the default case: a
       host with no `world=` line sends a 20-byte WELCOME whose last field lands exactly there. The relay's own
       GetStr (coop-store/store_main.cpp) already reads this way. */
    if (n == 0) out->clear(); else out->assign(&b[*at + 4], n);
    *at += 4 + n; return true;
}

BackendId ParseBackend(const std::string& name)
{
    if (name == "direct" || name == "enet" || name == "ip") return BACKEND_DIRECT;
    if (name == "steam")                                    return BACKEND_STEAM;
    return BACKEND_NONE;
}

// F302. The sender's own render distance, as raw IEEE-754 bits in a u32 - the same treatment
// every other float on this wire gets, and safe for the same reason (both peers are x64 Windows
// and the version handshake refuses mismatched builds).
//
// ONLY THE CLIENT SENDS HELLO (see the LINK_UP edge below: `if (now == LINK_UP && !g_isHost)`), so
// what the host receives here is unambiguously the CLIENT's setting. That is the whole point: the
// host does all the adopting, and the question the adoption radius answers is "what does the
// client need sent to it", which no amount of reading the host's own options can tell it.
float g_peerViewDistance = 0.0f;   // 0 = not received; never guess a default in its place
/* mp4: the joined player's name, from its HELLO - a fact about a peer, so it dies with the peer (ClearPeerViewDistance). */
static std::string g_peerName;

void SendHello()
{
    // The engine read lives in worldsync.cpp, which already owns the `options` include and the
    // guarded read. One implementation, so the two sides of this field cannot drift apart.
    /* P7v (review-p7h H-4) - coop::LocalViewDistance IS A GUARDED ENGINE READ, and this function is now
       reachable from the pump's LINK_UP edge INSIDE a load gate. While EngineWritesBlocked() the field goes
       out as 0 - "I cannot tell you", which the wire format already allows and which OnHello already handles
       without substituting a default - and a kActResendHello entry is queued so the drain re-sends the HELLO
       with the live value on the first unblocked tick. RECURRENCE-COVERED: the deferral does not consume the
       trigger (design principle 4). */
    float vd = 0.0f;
    if (coop::EngineWritesBlocked())
    {
        ++g_helloViewDistanceUnknown;
        std::vector<char> none;
        coop::InQueueEnqueue(coop::kActResendHello, coop::kOriginLocal, coop::kScopeSave, coop::kClassEdge, 0, 0, none);
    }
    else if (!coop::LocalViewDistance(&vd) || !(vd > 1.0f)) vd = 0.0f;   // 0 == "I cannot tell you"

    unsigned int vdBits = 0;
    std::memcpy(&vdBits, &vd, 4);

    std::vector<char> b;
    PutU32(&b, kProtocolVersion);
    PutU32(&b, g_isHost ? 1u : 0u);
    PutU32(&b, vdBits);
    /* mp4 (protocol 69): this player's name, for the host's players list. Read live from shared_wastelands.cfg (the landing
       screen writes it before Join arms), so a name typed this session is the one sent. */
    const std::string myName = coop::ConfigFilePlayerName();
    PutStr(&b, myName);
    DebugLog("[NET] name: this game's HELLO carries the player name '" + myName + "'"
             + (myName.empty() ? std::string(" (none - shared_wastelands.cfg has no playername= line)") : std::string()));
    g_transport->Send(0, MSG_HELLO, &b[0], b.size(), CH_RELIABLE);
    DebugLog("[net] -> HELLO (protocol " + N(kProtocolVersion) + ", myViewDistance="
             + (vd > 0.0f ? N((long long)vd)
                          : std::string("UNREADABLE - the host will use its fallback radius"))
             + ")");
}

void OnHello(const Message& m)
{
    unsigned int ver = 0;
    if (!GetU32(m.payload, 0, &ver))
    {
        SessionRefuseAndLeaveLater("malformed HELLO", &g_helloProtocolRefused);
        return;
    }
    if (ver != kProtocolVersion)
    {
        // Refuse loudly. A version handshake that warns and continues is worse than none:
        // it converts a clean failure into a subtle desync.
        ErrorLog("[net] PROTOCOL MISMATCH: peer speaks " + N(ver)
                 + ", we speak " + N(kProtocolVersion));
        SessionRefuseAndLeaveLater("HELLO protocol mismatch", &g_helloProtocolRefused);
        return;
    }

    // F302. Absent or zero means "the peer could not read its own setting" - recorded as such,
    // never replaced with a plausible number. The adoption radius logs which source it used, so a
    // fallback can never be read later as a derived value.
    unsigned int vdBits = 0;
    float peerVd = 0.0f;
    if (GetU32(m.payload, 8, &vdBits)) std::memcpy(&peerVd, &vdBits, 4);
    if (peerVd > 1.0f && peerVd < 1000000.0f) g_peerViewDistance = peerVd;

    /* mp4 (protocol 69): the joining player's name. Network input, so it is kept only when it passes the same rule the
       settings file applies to playername= (length, characters); anything else is shown as a friend with no name. */
    {
        size_t at = 12;
        std::string nm;
        const bool have = GetStr(m.payload, &at, &nm);
        const bool ok = have && coopworld::DisplayNameOk(nm, 0);
        g_peerName = ok ? nm : std::string();
        DebugLog("[NET] name: the other game's HELLO " + (!have ? std::string("carries no name field")
                 : ok ? "names its player '" + nm + "'" : "carries a name that fails the name rule (" + N((long long)nm.size())
                                                     + " bytes) - shown without a name"));
    }

    // F311 - print what THIS MESSAGE carried, not the global. The earlier version logged
    // `g_peerViewDistance`, so a HELLO whose value was absent or out of range printed the value
    // still held from before as though this peer had supplied it.
    DebugLog("[net] <- HELLO ok (protocol " + N(ver) + ", viewDistanceInThisHello="
             + (peerVd > 1.0f && peerVd < 1000000.0f ? N((long long)peerVd)
                                                     : std::string("NOT SUPPLIED / OUT OF RANGE"))
             + (g_isHost ? " - this is the CLIENT's setting and it is what the adoption radius"
                           " must use (F302)"
                         : " - we are the client; this value is not used for adoption")
             + ")");

    if (g_isHost)
    {
        /* E38 / decision 42 - THE WELCOME NOW CARRIES THE NOTEBOOK'S ADDRESS, and it is THE CONFIG FILE'S address,
           not this game's own `g_linkAddr`. A host that reached its notebook over loopback holds "127.0.0.1",
           which is a perfectly correct value here and a useless one on the client's machine: the same shape as
           F311, where a log line printed the value it happened to hold rather than the one that answered the
           question. The config's `store=` line is what the host publishes, and if it has none the field is empty
           and the client falls back to its own file. `storePort` is a port - it never exceeds 65535 - but it goes
           on the wire as a u32 like every other integer on this link (read-e38 Q4), and the reader range-checks
           it before using it. */
        const std::string storeAddr = coop::ConfigStoreAddr();
        const unsigned int storePort = (unsigned int)coop::ConfigStorePort();
        const std::string worldKey = coop::ConfigWorldKey();
        std::vector<char> b;
        PutU32(&b, kProtocolVersion);
        PutU32(&b, m.peer);            // the peer id we assigned them
        PutStr(&b, storeAddr);
        PutU32(&b, storePort);
        PutStr(&b, worldKey);
        g_transport->Send(m.peer, MSG_WELCOME, &b[0], b.size(), CH_RELIABLE);
        coop::ItemsSessionReady();   /* inv7c fold (review 7): the host accepted this HELLO - the session is ready */ g_slotReadyGen = SessionLinkGenAfterBatch(); g_slotSentGen = -1;   /* M5b: PEER_SLOT may go now (fold 1: stamped with the generation AFTER this Poll's edges - a CONNECT and this HELLO in one Poll no longer strand it) */
        DebugLog("[net] -> WELCOME (assigned peer id " + N(m.peer) + ", notebook "
                 + (storeAddr.empty() || storePort == 0 ? std::string("NOT ADVERTISED - this host's shared_wastelands.cfg has no `store=` line, so the client must supply its own")
                                                        : storeAddr + ":" + N(storePort))
                 + ", world='" + worldKey + "')");
    }
}

static long long g_welcomeNotebookSwapped = 0;   /* join1: WELCOMEs whose loopback notebook address was replaced by the dialled host address */

static long g_welcomeLinkGen = -1;   /* ui1 review LOW: the link generation a WELCOME last arrived on */
void OnWelcome(const Message& m)
{
    g_welcomeLinkGen = SessionLinkGen();   /* ui1 */
    unsigned int ver = 0, assigned = 0;
    GetU32(m.payload, 0, &ver);
    /* P7f (review-p6z M-5) - THE VERSION IS DECODED, SO IT IS CHECKED, BEFORE ANY FIELD IS USED. The refusal
       this build relies on lives on the HOST side (OnHello), which is enough for the ordinary case - a v39 host
       never answers a v40 client. What it does not cover is this side: the decoder below reads three fields
       added in v40, one of them an ADDRESS THIS GAME WILL DIAL, out of a payload whose version was read into a
       variable and then only logged. */
    if (ver != kProtocolVersion)
    {
        ErrorLog("[net] <- WELCOME from a peer speaking protocol " + N(ver) + ", we speak " + N(kProtocolVersion)
                 + " - REFUSED. Nothing in this payload is read: it ends with a notebook address this game"
                 " would otherwise open a link to.");
        SessionRefuseAndLeaveLater("WELCOME protocol mismatch", &g_welcomeProtocolRefused);
        return;
    }
    GetU32(m.payload, 4, &assigned);
    g_myPeerId = assigned;

    /* E38 / decision 42 - THE NOTEBOOK'S ADDRESS, READ AT THE TITLE SCREEN. This is the whole join sequence:
       the client reaches the host first, the host answers with the notebook's address, and the client opens
       that link BEFORE its world loads, so the load gate in store.cpp has something to wait on. A payload
       without the fields (an older peer, though the version handshake refuses those) leaves storeAddr empty
       and changes nothing - the client keeps whatever its own shared_wastelands.cfg gave it. */
    std::string storeAddr, worldKey;
    unsigned int storePort = 0;
    size_t at = 8;
    const bool haveAddr = GetStr(m.payload, &at, &storeAddr) && GetU32(m.payload, at, &storePort);
    if (haveAddr) { at += 4; if (!GetStr(m.payload, &at, &worldKey)) worldKey.clear(); }

    DebugLog("[net] <- WELCOME: session established, my peer id = " + N(assigned)
             + ", the host's notebook is "
             + (haveAddr && !storeAddr.empty() && storePort > 0 && storePort <= 65535 ? storeAddr + ":" + N(storePort)
                                                                                     : std::string("NOT NAMED (this game keeps whatever its own shared_wastelands.cfg gave it)"))
             + ", world='" + worldKey + "'");

    if (!worldKey.empty()) coop::ConfigNoteRemoteWorldKey(worldKey);
    coop::ItemsSessionReady();   /* inv7c fold (review 7): the joiner's WELCOME - the session is ready */ g_slotReadyGen = SessionLinkGenAfterBatch(); g_slotSentGen = -1;   /* M5b: PEER_SLOT may go now (fold 1: the generation AFTER this Poll's edges) */
    if (haveAddr && !storeAddr.empty() && storePort > 0 && storePort <= 65535)
    {
        /* join1: a notebook on the HOST's computer is named as 127.0.0.1 by the panel's Host button - on this computer
           that is this computer. Dial the address this game used to reach the host instead (unchanged when that too is
           a loopback, i.e. both games on one machine). */
        /* addr1: the host is whatever the session link actually reached (ENet's peer address), then the address the
           join was dialled at, then the config file's host= - see coopstore::NotebookDialFromWelcome. */
        int swapped = 0;
        std::string why;
        const std::string peerIp = (g_transport != 0) ? g_transport->PeerHostIp() : std::string();
        const std::string dialAddr = coopstore::NotebookDialFromWelcome(storeAddr, peerIp, g_joinAddr, coop::ConfigHostAddr(),
                                                                        &swapped, &why);
        DebugLog("[STORE] notebook address from WELCOME '" + storeAddr + ":" + N(storePort) + "' -> using '" + dialAddr + ":"
                 + N(storePort) + "' (" + why + ")");
        if (swapped)
        {
            ++g_welcomeNotebookSwapped;
            DebugLog("[net] WELCOME named the notebook at " + storeAddr + ":" + N(storePort) + " - a loopback address, which on"
                     " this computer is this computer; dialling the host's own address " + dialAddr + " instead (join1)");
        }
        coop::StoreLinkFromWelcome(dialAddr, (unsigned short)storePort);
    }
}

void OnPing(const Message& m)
{
    // Echo the sequence straight back; the sender times the round trip.
    g_transport->Send(m.peer, MSG_PONG,
                      m.payload.empty() ? 0 : &m.payload[0],
                      m.payload.size(), CH_RELIABLE);
}

void OnPong(const Message& m)
{
    unsigned int seq = 0;
    GetU32(m.payload, 0, &seq);
    // Frames elapsed, NOT milliseconds (F071): the in-game pump is frame-locked, so one
    // tick is a frame (~8 ms at 118 fps), and the round trip cannot resolve finer than
    // that. Neither this nor ENet's estimate is a trustworthy latency figure on loopback
    // - both are reported so a divergence between them is visible.
    unsigned long long frames = g_tick - g_pingSentTick;
    DebugLog("[net] <- PONG seq=" + N(seq)
             + " frames=" + N((long long)frames)
             + " enetRTT=" + N(g_transport->RoundTripMs()) + "ms");
}

// H010b APPEARANCE payload: uid(u32) | serialised appearance RECORD (see
// appearance_record.cpp for the field order). What travels is the record the engine DERIVES
// the character from, not the derived fields - F160 measured the latter being re-derived away
// inside the same function that wrote them.
//
// QUEUED, never applied here: the local copy may not have finished assembling its own
// appearance yet, and applying before that lets the engine's pending update re-derive over
// the top (F157). coop::AppearanceTick() applies it once the local copy settles.
void OnAppearance(const Message& m)
{
    unsigned int uid = 0;
    if (m.payload.size() < 8 || !GetU32(m.payload, 0, &uid))
    {
        ErrorLog("[net] malformed APPEARANCE (too short) - ignored");
        return;
    }
    coop::RecordCopy rec;
    if (!coop::DeserialiseRecord(m.payload, 4, &rec))
    {
        // REFUSED, not best-effort. A half-parsed appearance record would write a partial
        // roll onto the peer and produce a character that is neither one instance's nor the
        // other's - the exact failure class the protocol version bump exists to prevent.
        ErrorLog("[net] malformed APPEARANCE record for uid " + N(uid) + " - REFUSED");
        return;
    }
    DebugLog("[net] <- APPEARANCE uid=" + N(uid) + " bytes=" + N((long long)m.payload.size())
             + " " + coop::RecordSummary(rec));
    coop::QueueRemoteRecord(uid, rec);
}

// M1 SPAWN payload: uid(u32) | x,y,z(3 floats) | nameLen(u32) | name bytes.
// Floats travel as raw IEEE-754 - both peers are x64 Windows and the version handshake
// refuses mismatched builds, so no conversion is needed.
// The authority's worn items for a uid we do not own. QUEUED, like the appearance record and
// for the same reason (F157) - and applied AFTER it, because applying the appearance rebuilds
// attachments and would undo clothing written first.
void OnClothing(const Message& m)
{
    unsigned int uid = 0;
    if (m.payload.size() < 8 || !GetU32(m.payload, 0, &uid))
    {
        ErrorLog("[net] malformed CLOTHING (too short) - ignored");
        return;
    }
    coop::GarmentSet set;
    if (!coop::DeserialiseGarments(m.payload, 4, &set))
    {
        // REFUSED, not best-effort: a half-parsed garment list would dress the peer in some of
        // the authority's clothes and some of its own, which is neither instance's character.
        ErrorLog("[net] malformed CLOTHING record for uid " + N(uid) + " - REFUSED");
        return;
    }
    DebugLog("[net] <- CLOTHING uid=" + N(uid) + " bytes=" + N((long long)m.payload.size())
             + " " + coop::GarmentSummary(set));
    coop::QueueRemoteGarments(uid, set);
}

// P-15 / F220 - the authority entered or left COMBAT MODE, and against whom.
//
// Why this message exists, and why it is small: T066 counted the calls and proved the peer's
// combat controller IS being ticked (`ccOurTicks` climbed 0 -> 22,625 while the peer showed
// `mode=0 state=3:SWORD_STARTING` in all 106 samples). It is running and simply never told to
// fight. So nothing about animation, stance or technique needs to cross the wire - the peer's
// own engine produces all of that once it is in combat mode against a target it can resolve.
//
// That is the H010b shape again: send the CAUSE, let each machine derive the appearance.
void OnCombatMode(const Message& m)
{
    unsigned int uid = 0, on = 0, targetUid = 0;
    if (m.payload.size() < 12 || !GetU32(m.payload, 0, &uid)
        || !GetU32(m.payload, 4, &on) || !GetU32(m.payload, 8, &targetUid))
    {
        ErrorLog("[net] malformed COMBATMODE (too short) - ignored");
        return;
    }
    DebugLog("[net] <- COMBATMODE uid=" + N(uid) + " on=" + N(on)
             + " targetUid=" + N(targetUid));
    coop::ApplyRemoteCombatMode(uid, on != 0, targetUid);
}

// F348 - THE SWING. Combat mode (17) is a LEVEL: it says a fight is on, and T067 verified the peer
// adopts it. This is an EDGE: the authority's character has just begun a blow.
//
// It is the same H010b shape and for the same reason. Nothing about the animation, the technique
// or the weapon crosses the wire - only "uid U is now attacking uid V". The peer's own engine
// picks its own technique for its own gap and plays it. What makes that possible is F348: the
// engine authorizes a swing with three field writes and NO AI decision anywhere in the path, so
// the puppet's decision gate stays fully closed - which is what every previous attempt at this
// had to open, and what cost T071/T075/T076 their runs.
void OnSwing(const Message& m)
{
    unsigned int uid = 0, targetUid = 0;
    if (m.payload.size() < 8 || !GetU32(m.payload, 0, &uid) || !GetU32(m.payload, 4, &targetUid))
    {
        ErrorLog("[net] malformed SWING (too short) - ignored");
        return;
    }
    coop::ApplyRemoteSwing(uid, targetUid);
}

void OnContext(const Message& m)
{
    if (m.payload.size() < 96) { ErrorLog("[net] malformed CONTEXT (too short) - ignored"); return; }
    unsigned int uid = 0, st = 0, mt = 0, sl = 0, tl = 0;
    GetU32(m.payload, 0, &uid); GetU32(m.payload, 4, &st); GetU32(m.payload, 8, &mt);
    unsigned char hraw[32]; std::memcpy(hraw, &m.payload[12], 32);
    unsigned char craw[32]; std::memcpy(craw, &m.payload[44], 32);
    float bp[3]; std::memcpy(bp, &m.payload[76], 12);
    size_t off = 88;
    if (!GetU32(m.payload, off, &sl) || m.payload.size() < off + 4 + sl + 4) { ErrorLog("[net] malformed CONTEXT (squad sid) - ignored"); return; }
    std::string squadSid(&m.payload[off + 4], sl); off += 4 + sl;
    if (!GetU32(m.payload, off, &tl) || m.payload.size() < off + 4 + tl) { ErrorLog("[net] malformed CONTEXT (town sid) - ignored"); return; }
    std::string townSid(&m.payload[off + 4], tl); off += 4 + tl;
    std::string platoonId; unsigned int pl = 0;
    if (GetU32(m.payload, off, &pl) && m.payload.size() >= off + 4 + pl) platoonId.assign(&m.payload[off + 4], pl);   // P1 (older senders: empty)
    coop::ApplyRemoteContext(uid, squadSid, (int)st, townSid, hraw, craw, bp[0], bp[1], bp[2], (int)mt, platoonId);
}

void OnIntent(const Message& m)
{
    if (m.payload.size() < 28) { ErrorLog("[net] malformed INTENT (too short) - ignored"); return; }
    unsigned int uid = 0, subj = 0; int type = 0, prio = 0; float v[3] = { 0, 0, 0 };
    std::memcpy(&uid, &m.payload[0], 4); std::memcpy(&type, &m.payload[4], 4); std::memcpy(&subj, &m.payload[8], 4);
    std::memcpy(v, &m.payload[12], 12); std::memcpy(&prio, &m.payload[24], 4);
    coop::ApplyRemoteIntent(uid, type, subj, v[0], v[1], v[2], prio);   // refused there unless we hold a puppet for uid
}

/* P25 fold 2: the owner's word on which building its character is in - refused there unless we hold a puppet for uid; a
   relayed one passes the stream's relayed-owner gate first (liverelay.h CharStreamNeedsOwnerGate) */
void OnInside(const Message& m)
{
    unsigned int uid = 0; int inside = 0; std::string key;
    const int rc = p25inside::DecodeInside(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), &uid, &inside, &key);
    if (rc != p25inside::kInsideDecodeOk)
    {
        ErrorLog(std::string("[net] malformed INSIDE (") + (rc == p25inside::kInsideDecodeTooLong ? "key too long" : "too short") + ") - ignored");
        return;
    }
    coop::ApplyRemoteInside(uid, inside, key);
}

void OnSpawn(const Message& m)
{
    // P1: the whole payload is parsed by coopspawn::DecodeSpawn (src/common/spawnage.h), the same function the
    // offline suite round-trips. F115: faction and keepContainer travel with it; P1: so does the owner's age.
    coopspawn::SpawnMsg sm;
    const int rc = coopspawn::DecodeSpawn(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), &sm);
    if (rc != coopspawn::kSpawnDecodeOk)
    {
        ErrorLog(std::string("[net] malformed SPAWN (")
                 + (rc == coopspawn::kSpawnDecodeTooShort ? "too short"
                    : rc == coopspawn::kSpawnDecodeNameTrunc ? "name truncated"
                    : rc == coopspawn::kSpawnDecodeNoModifier ? "no modifier block"
                    : "faction truncated") + ") - ignored");
        return;
    }
    if (sm.ageAbsent) ++g_spawnAgeAbsent;     // a pre-45 sender: created at age 0.0, as every copy was before P1
    if (sm.ageClamped) ++g_spawnAgeClamped;
    if (sm.statsState == coopspawn::kSpawnStatsBad) coop::StatsNoteBadBlock();          // S1: truncated / NaN / infinite
    else if (sm.statsState != coopspawn::kSpawnStatsOk) coop::StatsNoteNoBlock();       // S1: an older payload, or has 0

    // M4 fold (review 2026-09-29 H1, defence in depth): a SPAWN whose uid names a character THIS game runs is refused and
    // counted. It can only be a repeated uid (a restarted peer re-minting); taking it would write g_owner over our own
    // character, hand it to the peer and suppress its AI. The notebook's uid blocks (src/common/uidblock.h) stop the repeat
    // at the source; this is the second wall.
    if (coopuid::SpawnUidDecide(g_localOwned.find(sm.uid) != g_localOwned.end()) == coopuid::kSpawnUidOwnRefused)
    {
        ++g_spawnUidOwnRefused;
        if (g_spawnUidOwnRefused <= 5 || g_spawnUidOwnRefused % 100 == 0)
            ErrorLog("[net] SPAWN REFUSED: uid=" + N(sm.uid) + " '" + sm.templateName + "' from peer " + N(m.peer)
                     + " names a character THIS game runs - a repeated uid; nothing created, the owner record untouched (spawnUidOwnRefused="
                     + N(g_spawnUidOwnRefused) + ")");
        return;
    }
    // Record ownership BEFORE creating: the sender owns what it told us to spawn, and
    // commitment 3 (never overwrite owner state from a non-authoritative copy) depends
    // on the map being right from the object's first frame, not from the next message.
    g_owner[sm.uid] = OwnerKeyOf(m.peer);   /* M5b: the owning PLAYER, by its slot when known */
    g_departedPending.erase(sm.uid);   /* M8: a claim made after a departure supersedes it - no road removes this row for that departure */

    std::ostringstream ageText; ageText.imbue(std::locale::classic()); ageText << sm.age;
    DebugLog("[net] <- SPAWN uid=" + N(sm.uid) + " '" + sm.templateName + "' (owner peer " + N(m.peer) + ")"
             + " faction='" + (sm.factionName.empty() ? std::string("<inherited>") : sm.factionName)
             + "' keepContainer=" + N(sm.keepContainer ? 1 : 0)
             + " age=" + (sm.ageAbsent ? std::string("<absent>") : ageText.str()) + (sm.ageClamped ? " (clamped)" : "")
             + " dead=" + (sm.flagsAbsent ? std::string("<absent>") : N((sm.ownerFlags & coopspawn::kSpawnFlagDead) != 0 ? 1 : 0))
             + " ko=" + (sm.flagsAbsent ? std::string("<absent>") : N((sm.ownerFlags & coopspawn::kSpawnFlagKo) != 0 ? 1 : 0)));   /* T-303 */
    /* T-303 (protocol 102): the owner's word is noted BEFORE the copy exists - its appearance and clothing wait on it (a
       knocked-out one: crash1b, until the owner's STATE lays it down; a dead one: until its death below lands). A dead one
       needs no KO word. T-303 fold 1: a present flags byte is written WHOLE - dead or alive, KO or not - so an alive SPAWN
       clears a stale entry of an earlier life (CopyNoteSpawnFlags: NoteOwnerDead + CopyNoteOwnerKo, and the recv counts). */
    if (!sm.flagsAbsent) coop::CopyNoteSpawnFlags(sm.uid, sm.ownerFlags);
    const long long nsBefore = coop::MirrorPeerRefusals();   /* T-354 */
    coop::CreateRefusalReset();   /* the creation core's lasting refusal, read after the SPAWN */
    coop::ApplyRemoteSpawn(sm.uid, sm.templateName, sm.x, sm.y, sm.z, sm.factionName, sm.keepContainer, sm.age,
                           sm.statsState == coopspawn::kSpawnStatsOk ? sm.stats : 0);   /* S1: the owner's 44 stat values */
    /* T-354: our character table refused this copy - the owner is told, so it is counted on both games. T-354 fold 1: only
       when the character really is not shown - a twin refused, then built fresh by CreateAt, is shown */
    if (coopuid::SpawnRefusedByTable(nsBefore, coop::MirrorPeerRefusals()) && coop::FindSpawned(sm.uid) == 0)
        SendNotShownToOwner(sm.uid, OwnerKeyOf(m.peer));
    /* no row for the uid after the SPAWN, for a reason that would repeat on a re-send (a template or faction not in this game's
       data, the TEST-ONLY table cap - lostcopy::CreateRefusalLasting): never booked as a lost copy. A passing refusal (no world, no
       reference character, the table really full, the engine's create answering nothing) or a kept (retired) row is not marked. */
    LostCopyRefusedHere(sm.uid, coop::SpawnedRawObject(sm.uid) == 0 && lostcopy::CreateRefusalLasting(coop::CreateLastRefusal()) != 0);
    LostCopyArrived(sm.uid);   /* a booked uid whose copy this SPAWN made (or found live) is BACK */
    /* T-303 (protocol 102): the owner says this character is DEAD - the copy dies HERE, in the same message, through the
       engine's path (ApplyOwnerDeath), before any APPEARANCE / CLOTHING (later messages) can reach it; the P022 dead-copy
       path dresses it. A death that cannot land now is retried every ~0.25 s (SpawnDeathRetryTick), the looks held. */
    if ((sm.ownerFlags & coopspawn::kSpawnFlagDead) != 0) coop::ApplyOwnerDeathAtSpawn(sm.uid);
}

// M4 STATE: the authority's full medical + downed state for one character it owns.
void OnState(const Message& m)
{
    unsigned int uid = 0, partCount = 0;
    if (m.payload.size() < 8) return;              // periodic and unreliable - do not spam
    std::memcpy(&uid,       &m.payload[0], 4);
    std::memcpy(&partCount, &m.payload[4], 4);
    if (partCount == 0 || partCount > kMaxHealthParts) return;
    if (m.payload.size() < 8 + partCount * coop::kPartFloats * 4 + 12) return;

    // Authority rule (commitment 3): only the uid's owner may report its state.
    std::map<unsigned int, unsigned int>::const_iterator own = g_owner.find(uid);
    if (!RemoteMayWrite(uid, m.peer)) return;

    // K1 (read-carry): one decoder (src/common/statewire.h), the one the offline suite tests. Absent latch bytes
    // still read as "no latch" (F147), absent koTimer as -1 (H029), absent carryingUid as 0.
    float parts[kMaxHealthParts * kPartFloats];
    coopstate::StateMsg sm;
    if (coopstate::DecodeState(&m.payload[0], m.payload.size(), kMaxHealthParts, (unsigned int)kPartFloats,
                               parts, &sm) != coopstate::kStateDecodeOk) return;
    const float blood = sm.blood;
    const int prone = sm.prone, dead = sm.dead;
    const unsigned int latchBits = sm.latchBits;
    const float nextKnockoutAt = sm.nextKnockoutAt, koTimer = sm.koTimer;
    const unsigned int carryingUid = sm.carryingUid;   // K1: the body this carrier carries, 0 = nothing
    // POSE (read-poses, 50): the owner's in-place pose; PoseApplyTick acts on it. An absent block changes nothing.
    // BED1 (51): a bed pose also carries the outer building's key (sm.pose.outerKey), empty when the bed is not layout furniture.
    if (!sm.poseAbsent) coop::NotePoseWant(uid, sm.pose, m.peer);

    // T040/T041, flagged by two different executors: there was NO receive line for STATE,
    // so "the peer got it" rested entirely on counter reconciliation. A counter can only say
    // that some number of packets arrived; it cannot say that THIS uid's state did. Bounded
    // in full, then thinned - an hour-long soak at ~1 Hz per character would otherwise be
    // mostly this line.
    ++g_stateRecv;
    if (g_stateRecv <= 10 || g_stateRecv % 100 == 0)
        DebugLog("[net] <- STATE #" + N(g_stateRecv) + " uid=" + N(uid)
                 + " parts=" + N(partCount) + " prone=" + N(prone) + " dead=" + N(dead));

    int wasProne = -1;
    bool changed = coop::ApplyState(uid, parts, (int)partCount, blood, prone, &wasProne,
                                   latchBits, nextKnockoutAt, koTimer, m.peer, carryingUid,
                                   sm.restHas ? sm.rest : 0);   /* R3: where the owner's ragdoll settled */
    coop::ApplyOwnerHunger(uid, sm.hungerHas, sm.hunger, sm.fed);   /* par5 (parity P5): the copy's hunger is the owner's */
    coop::ApplyOwnerLimbs(uid, sm.limbsHas, sm.limbs, m.peer);   /* LIMBS (P38 / P39): the copy's lost and robotic limbs are the owner's */
    coop::ApplyOwnerDeath(uid, dead);   /* par6 (parity P6): the owner's death kills the copy through the engine's path; a copy's own is refused */
    if (changed)
    {
        // F126: a peer pulled back out of a KO its authority never entered. LOUD, because
        // this is the event the whole of M4 step 3 exists to prevent, and a silent
        // correction would hide how often it happens.
        DebugLog("[M4] STATE corrected uid=" + N(uid) + " prone " + N(wasProne)
                 + " -> " + N(prone) + " (authority)");
    }
}

// MOVE payload: uid(u32) | x,y,z(3f) | velX,velZ(2f) | desiredSpeed(f) | faceX,faceZ(2f) | stamp(u32, the owner's clock in
// milliseconds) = 40 bytes. A payload without the stamp (36 bytes) is placed on arrival time instead.
// Older senders never reach here: OnHello disconnects on any protocol mismatch (review 4 item 5).
void OnMove(const Message& m)
{
    if (m.payload.size() < 36) { return; }   // silent: MOVE is high-rate, don't spam the log

    unsigned int uid = 0, stamp = 0;
    float v[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    std::memcpy(&uid, &m.payload[0], 4);
    std::memcpy(v, &m.payload[4], 32);
    const bool stamped = m.payload.size() >= 40;
    if (stamped) std::memcpy(&stamp, &m.payload[36], 4);

    // Authority check (commitment 3): only apply a position from the peer that OWNS the uid.
    // An unknown uid is refused - "not told" is deliberately distinct from "peer-owned".
    std::map<unsigned int, unsigned int>::const_iterator own = g_owner.find(uid);
    if (!RemoteMayWrite(uid, m.peer))
    {
        static long long s_lastRefuse = 0;
        if (++s_lastRefuse % 100 == 1)
            DebugLog("[net] MOVE refused for uid=" + N(uid) + " from peer " + N(m.peer)
                     + (own == g_owner.end() ? " (owner unknown)" : " (not the owner)"));
        return;
    }

    coop::ApplyRemoteMove(uid, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], stamped, stamp);
}

// MOVESTOP payload: uid(u32) | x,y,z(3f) | stamp(u32, the owner's clock in milliseconds) = 20 bytes. Taken only from the uid's
// owner (MOVE's test); ordered against MOVE by the stamp (replicate.cpp ApplyRemoteMoveStop).
void OnMoveStop(const Message& m)
{
    if (m.payload.size() < 20) { ++g_moveStopShort; ErrorLog("[net] malformed MOVESTOP (too short) - ignored"); return; }
    unsigned int uid = 0, stamp = 0;
    float v[3] = { 0, 0, 0 };
    std::memcpy(&uid, &m.payload[0], 4); std::memcpy(v, &m.payload[4], 12); std::memcpy(&stamp, &m.payload[16], 4);
    if (!RemoteMayWrite(uid, m.peer)) { ++g_moveStopRefused; return; }
    ++g_moveStopRecv[cooplive::IsRelayPeer(m.peer) ? 1 : 0];
    coop::ApplyRemoteMoveStop(uid, v[0], v[1], v[2], stamp);
}

// M2b TASK payload: uid(u32) | taskType(i32) | x,y,z(3f) = 20 bytes.
void OnTask(const Message& m)
{
    if (m.payload.size() < 20) { ErrorLog("[net] malformed TASK - ignored"); return; }

    unsigned int uid = 0;
    int type = 0;
    float v[3];
    std::memcpy(&uid,  &m.payload[0], 4);
    std::memcpy(&type, &m.payload[4], 4);
    std::memcpy(v,     &m.payload[8], 12);

    // Same authority rule as MOVE (commitment 3): only the owner may command a uid.
    std::map<unsigned int, unsigned int>::const_iterator own = g_owner.find(uid);
    if (!RemoteMayWrite(uid, m.peer))
    {
        static long long s_taskRefused = 0;   /* M7a (M5b review): throttled - the first 5, then every 100th */
        if (cooplive::LiveLogThis(++s_taskRefused))
            DebugLog("[net] TASK refused for uid=" + N(uid) + " from peer " + N(m.peer)
                     + (own == g_owner.end() ? " (owner unknown)" : " (not the owner)") + " (taskRefused " + N(s_taskRefused) + "; the first 5 and every 100th are logged)");
        return;
    }

    DebugLog("[net] <- TASK uid=" + N(uid) + " type=" + N(type));
    coop::ApplyRemoteTask(uid, type, v[0], v[1], v[2]);
}

// M3 HIT payload: uid(u32) | cutDir(i32) | comboId(i32) | Damages(24 bytes) = 36 bytes.
void OnHit(const Message& m)
{
    if (m.payload.size() < 48) { ErrorLog("[net] malformed HIT - ignored"); return; }

    unsigned int uid = 0, attackerUid = 0;
    int cutDir = 0, comboId = 0;
    std::memcpy(&uid,         &m.payload[0],  4);
    std::memcpy(&attackerUid, &m.payload[4],  4);
    std::memcpy(&cutDir,      &m.payload[8],  4);
    std::memcpy(&comboId,     &m.payload[12], 4);

    // Authority rule (commitment 3): only the uid's owner may report damage to it.
    std::map<unsigned int, unsigned int>::const_iterator own = g_owner.find(uid);
    if (!RemoteMayWrite(uid, m.peer))
    {
        static long long s_hitRefused = 0;   /* M7a (M5b review): throttled - the first 5, then every 100th */
        if (cooplive::LiveLogThis(++s_hitRefused))
            DebugLog("[net] HIT refused for uid=" + N(uid) + " from peer " + N(m.peer)
                     + (own == g_owner.end() ? " (owner unknown)" : " (not the owner)") + " (hitRefused " + N(s_hitRefused) + "; the first 5 and every 100th are logged)");
        return;
    }

    // F119: the authority's resulting per-limb health rides with the hit that caused it.
    unsigned int partCount = 0;
    std::memcpy(&partCount, &m.payload[40], 4);
    float parts[kMaxHealthParts * kPartFloats];
    float blood = 0.0f;
    const unsigned int nfh = partCount * kPartFloats;
    if (partCount > kMaxHealthParts || m.payload.size() < 44 + nfh * 4 + 4)
    {
        ErrorLog("[net] malformed HIT (health block: " + N(partCount) + " parts, payload "
                 + N((long long)m.payload.size()) + " bytes) - ignored");
        return;
    }
    for (unsigned int i = 0; i < nfh; ++i)
        std::memcpy(&parts[i], &m.payload[44 + i * 4], 4);
    size_t bloodAt = 44 + nfh * 4;
    std::memcpy(&blood, &m.payload[bloodAt], 4);

    // PARITY P-2: the attacker's engine handle. Absent bytes are treated as "not named"
    // rather than as a malformed message, so a size check here can never be the thing that
    // silently stops hits being played.
    coop::ObjId attackerId = {0, 0, 0, 0, 0};
    if (m.payload.size() >= bloodAt + 4 + 20)
    {
        unsigned int f[5];
        std::memcpy(f, &m.payload[bloodAt + 4], 20);
        attackerId.index = f[0]; attackerId.serial = f[1]; attackerId.type = f[2];
        attackerId.container = f[3]; attackerId.containerStamp = f[4];
    }
    // K2 (decision 61 follow-up): one trailing byte after the attacker's handle - this hit knocked the
    // victim down on the owner's game. A missing byte (an older sender) reads as no knock; no protocol
    // bump, because older receivers check only a minimum size and ignore the extra byte.
    unsigned char knock = 0;
    if (m.payload.size() >= bloodAt + 24 + 1)
        knock = (unsigned char)m.payload[bloodAt + 24];

    DebugLog("[net] <- HIT uid=" + N(uid) + " attacker=" + N(attackerUid) + " dir=" + N(cutDir)
             + " healthParts=" + N(partCount)
             + (coop::ObjIdValid(attackerId) ? " attackerId=" + coop::ObjIdString(attackerId)
                                             : std::string(" attackerId=none"))
             + " knock=" + N(knock != 0 ? 1 : 0));
    coop::ApplyRemoteHit(uid, attackerUid, cutDir, &m.payload[16], comboId,
                         parts, (int)partCount, blood, attackerId, knock != 0);
}

/* M7a A1 build 1 [a1b1-sp2] - a withdrawal (UNLOAD / DESPAWN) as received: a receipt is owed whenever this game's slot is listed,
   whether or not it held a copy, so "receipts expected" is exact on both sides; a copy that already carries a HIGHER gen than the
   withdrawal names heard of a later owner - the withdrawal is stale and not applied [review c]. */
static unsigned int CopyGenHere(unsigned int uid) { std::map<unsigned int, unsigned int>::const_iterator g = g_copyGen.find(uid); return g == g_copyGen.end() ? 0u : g->second; }
static void WithdrawReceiptNote(unsigned int senderKey, const cooplo::WithdrawMsg& w)
{
    const int me = coop::StoreMySlot();
    if (me >= 0 && cooplo::WithdrawListsSlot(w, me)) g_receiptOut[senderKey].push_back(w.seq);
}
static bool WithdrawStaleHere(unsigned int uid, unsigned int gen, const char* what)
{
    if (g_localOwned.find(uid) != g_localOwned.end()) return false;   /* this game runs it: RemoteMayWrite refuses it as before */
    if (!cooplo::WithdrawalStale(CopyGenHere(uid), gen)) return false;
    ++g_wdStale;
    if (cooplive::LiveLogThis(g_wdStale))
        DebugLog(std::string("[net] <- ") + what + " uid=" + N(uid) + " at gen " + N((long long)gen) + " IGNORED - the copy here carries gen " + N((long long)CopyGenHere(uid))
                 + " (a later owner is known; M7a A1 wdStale " + N(g_wdStale) + "; the first 5 and every 100th are logged)");
    return true;
}
static bool WithdrawDecodeOrSay(const Message& m, const char* what, cooplo::WithdrawMsg* w)
{
    if (!m.payload.empty() && cooplo::WithdrawDecode(&m.payload[0], m.payload.size(), w)) return true;
    ++g_wdMalformed;
    ErrorLog(std::string("[net] malformed ") + what + " (" + N((long long)m.payload.size()) + " bytes; protocol 118 sends uid | seq | gen | sector | expected receivers) - ignored");
    return false;
}

// F322 - the authority's engine destroyed this character; remove our copy.
//
// AUTHORITY-CHECKED like every other inbound write (F287). A DESPAWN is the most destructive
// message on this wire - it removes a character from the world - so accepting one for a uid we
// author would let a peer delete our own characters.
void OnDespawn(const Message& m)
{
    cooplo::WithdrawMsg w;   /* M7a A1 build 1 [a1b1-sp3]: uid | seq | gen | sector | expected receivers */
    if (!WithdrawDecodeOrSay(m, "DESPAWN", &w)) return;
    const unsigned int uid = w.uid;
    WithdrawReceiptNote(m.peer, w);   /* listed: acknowledged whatever happens below */
    if (WithdrawStaleHere(uid, w.gen, "DESPAWN")) return;
    if (!RemoteMayWrite(uid, m.peer))
    {
        DebugLog("[net] <- DESPAWN uid=" + N(uid) + " REFUSED - we author this uid, or the sender"
                 " is not its authority");
        return;
    }
    DebugLog("[net] <- DESPAWN uid=" + N(uid));
    coop::SquadIdxOwnerWithdrew(uid, 0);   /* M7a A1 build 2 [a1b2-sp1] (design 2.3): its recorded owner says it died - out of the squad index's given */
    coop::ApplyRemoteDespawn(uid);
    LostCopyForget(uid, "the owner removed it (DESPAWN)");   /* nothing to ask for */
}

} // namespace

// R1-a-b (review-r1a M2): the split-brain guard, asked again later for a write that waited (a
// parked prone write). A separate name, not a declaration of RemoteMayWrite, so the calls inside
// the anonymous namespace above stay unambiguous (C2668).
bool RemoteMayWriteStill(unsigned int uid, unsigned int fromPeer) { return RemoteMayWrite(uid, fromPeer); }

bool SessionIsHost() { return g_isHost; }
bool SessionLinked() { return g_transport != 0 && g_transport->State() == LINK_UP; }
unsigned int SessionPeerCount() { return g_transport != 0 ? g_transport->PeerCount() : 0; }   /* P7f (review-p6z H-1) */
bool SessionPeerRawId(unsigned int* out) { return g_transport != 0 && g_transport->CurrentPeerId(out); }   /* T-355 fold 2 */

/* ============ P7v (design-noworld-queue 1.2 / 5) - THE CLASSIFICATION TABLE AND THE LINK EDGES ============
   P7h's deferred inbox (4096 entries, drop-newest, count-only) is DELETED. It was a new loss channel that had
   not existed before it - "before P7h no inbound session message was ever dropped: what the gate did not poll
   stayed in ENet, and reliable messages were retransmitted" - and it discarded EDGES (ITEM_MOVE, SPAWN,
   DESPAWN, HIT, ITEM_CONFIRM) in preference to the self-healing LEVELS that filled it (review-p7h H-3). Its
   replacement is the one arrival queue in store.cpp, which drops nothing to make room, collapses levels in
   place, and answers a backlog with back-pressure and then a loud refusal.
   THE HOLE THIS ALSO CLOSES, WHICH NOBODY HAD FILED. SessionTick runs from CommandChannelTick EVERY TICK
   REGARDLESS OF GameplayRunning(). So at the title screen with the session link up (E38, decision 42) a
   peer's MSG_SPAWN or MSG_ITEM_MOVE reached OnSpawn / OnItemMove with NO WORLD. That is review-p6z C-1's own
   shape on the session route, and only the record half of it had ever been closed. */
long SessionLinkGen() { return ::InterlockedCompareExchange(&g_sessionLinkGen, 0, 0); }
long SessionClosingEpisode() { return ::InterlockedCompareExchange(&g_hostClosingEpisode, 0, 0); }
/* M5b fold 1 (review 2026-09-30 item 1): see session.h. Reads the same four inputs SessionTick's edge block reads. */
long SessionLinkGenAfterBatch()
{
    const long gen = SessionLinkGen();
    if (g_transport == 0) return gen;
    return coopgl::LinkGenAfterBatch(gen, g_transport->ConnectGen(), g_connectGenSeen,
                                     g_transport->State() == LINK_UP ? 1 : 0, g_lastLinkState == LINK_UP ? 1 : 0);
}
std::string SessionRefusalCounts()
{
    return N(g_helloViewDistanceUnknown) + "," + N(g_helloResentWithViewDistance) + ","
         + N(g_helloResendSkipped) + "," + N(g_welcomeProtocolRefused) + "," + N(g_helloProtocolRefused);
}
/* THE TWO EDGES, IN ONE PLACE EACH, so the pump and the tick cannot answer the same edge differently. */
void SessionOnLinkUp()
{
    ::InterlockedIncrement(&g_sessionLinkGen);
    if (g_hostClosingSeen != 0) DebugLog("[net] link UP: the host's earlier SESSION_CLOSING is CLEARED (mmo5 fold) - it belonged to the link that went down");
    ::InterlockedIncrement(&g_hostClosingEpisode);   /* M11a S3 review fold (F2): a relayed close from before this link UP no longer holds */
    g_hostClosingSeen = coopgl::HostClosingFlagStep(g_hostClosingSeen, coopgl::kHcLinkUp);
    g_lastLinkState = LINK_UP;
    DebugLog("[net] link UP (session generation " + N((long long)SessionLinkGen()) + ")");
    if (g_glSeriesOpen)
    {
        ++g_glRelinked;
        DebugLog("[net] link1: the game link RE-LINKED after " + N((long long)g_glAttempts) + " re-dial attempt(s) (series "
                 + N(g_glSeries) + ")");
        g_glSeriesOpen = 0; g_glAttempts = 0;
    }
    if (!g_isHost)
    {
        SendHello();
        /* DECISION 48 (P8a): the town-generation client declaration this edge used to make is RETIRED.  Joining a
           session says nothing about who may generate a town; the notebook's area map does, on both games. */
    }
}
void SessionOnLinkDown()
{
    ::InterlockedIncrement(&g_sessionLinkGen);
    const int goneSlot = coop::PeerGoneCaptureSession();   /* M8: the departed peer's rows and slot, captured ON THE EDGE - before PlayerFactionOnLinkDown forgets the slot and before a new link's PEER_SLOT can re-key anything */
    coop::StoreNoteLinkDown();   /* inv7e2 fold: a world loaded after this is a reload during the gap */
    coop::PlayerFactionOnLinkDown();   /* M5b fold 1 (item 3): the session peer's slot is forgotten ON THE EDGE, so a drop + re-connect in one Poll forgets it too */
    coop::WorldsyncOnSessionLinkDown();   /* M8 review F2: the re-announce latch too - a peer-gone action the drain discards must not cost the reconnect its re-announce */
    g_lastLinkState = LINK_DOWN;
    DebugLog("[net] link DOWN (session generation " + N((long long)SessionLinkGen()) + ") - the peer cleanup is"
             " QUEUED as an action (P7v), so it runs from the drain outside every load gate and never from"
             " inside the engine's own resetGame.");
    std::vector<char> slotBytes(4); std::memcpy(&slotBytes[0], &goneSlot, 4);   /* M8: the action carries the slot captured above */
    coop::InQueueEnqueue(coop::kActPeerGone, coop::kOriginLocal, coop::kScopeWorld, coop::kClassEdge, 0, 0, slotBytes);
}
/* ---- THE CLASSIFICATION TABLE. THE DEFAULT FOR ANY TYPE NOT NAMED HERE IS ENGINE-TOUCHING: an unclassified
   message is QUEUED, never dispatched. That is the fail-safe direction and it is what stops the next added
   message type from re-opening review-p6z C-1.
     MSG_HELLO / MSG_WELCOME / MSG_PING / MSG_PONG / MSG_BYE - handshake and liveness. Their handlers write
       plugin state and the transport only; the two protocol refusals that DID reach engine memory now queue
       a kActSessionLeave action instead of running the teardown here (review-p7h H-1).
   NOT CLASSIFIED, AND THEREFORE QUEUED BY DEFAULT: MSG_RELATION, MSG_RELSYNC. A handler earns the inline
   path by being READ, not by looking harmless; these two have not been read line by line and that is stated
   rather than assumed. Queueing them is safe and costs at most a frame.
   M2 (decisions 32/44/54) - NEITHER INLINE NOR QUEUED: RECORD, RECORD_GONE, DELETED_BITS, WORLD_LISTED, ZONES
   and SECTORMAP travel only through the notebook. One arriving on this link is DROPPED at the poll and counted
   nbOnlyDropped on the [net] REPORT line, which must read 0 (a pre-44 peer is refused at HELLO/WELCOME before
   it can send one). DELETED_BITS and WORLD_LISTED were the two inline types among them. ---- */
int SessionMsgIsInline(int type)
{
    if (type == (int)MSG_HELLO || type == (int)MSG_WELCOME || type == (int)MSG_PING
     || type == (int)MSG_PONG  || type == (int)MSG_BYE
     || type == (int)MSG_PEER_SLOT   /* M5b: applied at arrival, so every queued owner message drains after it */
     || type == (int)MSG_SESSION_CLOSING) return 1;   /* mmo5: INLINE on purpose - it only sets a flag, and the link DOWN right behind it bumps the session generation, which would discard a queued copy */
    return 0;
}
/* M2: the six types the notebook alone carries. The poll drops them before classification. */
static long long g_nbOnlyDropped = 0;
static int SessionMsgIsNotebookOnly(int type)
{
    return (type == (int)MSG_RECORD || type == (int)MSG_RECORD_GONE || type == (int)MSG_DELETED_BITS
         || type == (int)MSG_WORLD_LISTED || type == (int)MSG_ZONES || type == (int)MSG_SECTORMAP) ? 1 : 0;
}

void CharLastAreaForget(unsigned int uid);   /* M7a fold F9: defined beside g_charLastArea */
void RosterStreamForget(unsigned int uid);   /* fold 1 [a1b1f1-sp0] [F9]: defined beside g_roLastStreamAt - forgotten where g_owner / g_copyGen are */
void RosterStreamForgetAll();
void SessionOwnerMovedSend(unsigned int uid);   /* M7a2 item 3 [m7a2-sp1]: defined beside the reconnect sweep */
void SessionOwnerMovedAnnounce(unsigned int uid, bool prevKnown, unsigned int prevKey);   /* M7a2 fold 1 item 2 [m7a2f-sp1]: the taker's word */
// M-D - ownership transfer. Release: this instance stops being the authority for uid and records the new owner
// (its writes are accepted from now on). Take: this instance becomes the authority (SetLocalOwner's body).
/* the people this game ran and released to another game (every ReleaseLocalOwner: a hand-over's acknowledgement, a release's adoption,
   a dual run's yield, a revoke, a hire away); a person taken back or set as this game's again leaves it. Asked by the store's sleep and
   heartbeat gate (BlockSquadWrite): a squad holding such a person is this game's own squad handed over in place. MAIN THREAD. */
std::set<unsigned int> g_releasedHere;
bool IsUidReleasedHere(unsigned int uid) { return uid != 0 && g_releasedHere.find(uid) != g_releasedHere.end(); }
void MarkReleasedHere(unsigned int uid) { if (uid != 0) g_releasedHere.insert(uid); }   /* a body of this game's own world registered in place as another game's copy (spawn.cpp AdoptExistingTwin) */
void ReleaseLocalOwner(unsigned int uid, unsigned int toPeer, unsigned int gen)
{
    /* M7a A1 build 1 [a1b1-sp4]: the gen the new owner holds (0 = this game's gen + 1, the XFER / hire rule) moves to the copy table,
       BEFORE the OWNER_MOVED word below names it */
    const unsigned int newGen = gen != 0 ? gen : cooplo::GenTake(MineGenOf(uid));
    g_mineGen.erase(uid); g_copyGen[uid] = newGen;
    g_localOwned.erase(uid); g_releasedHere.insert(uid);
    coopown::OwnedMirrorErase(&g_ownedMirror, uid);   /* O1: beside the set, always */
    g_owner[uid] = OwnerKeyOf(toPeer);   /* M5b: the new owner as a player key */
    g_departedPending.erase(uid);   /* M8: a hand-over after a departure supersedes it */
    CharLastAreaForget(uid);   /* M7a fold F9 */
    coop::MedicalForgetCopy(uid);   /* the medical words this game held for it as a copy, if any, are not the new owner's */
    SessionOwnerMovedSend(uid);   /* M7a2 item 3 [m7a2-sp2]: every other game holding a copy re-keys it to the new owner */
}
void TakeLocalOwner(unsigned int uid, unsigned int gen)
{
    CharLastAreaForget(uid);   /* M7a fold F9: this game's first send of it names its sector afresh */
    std::map<unsigned int, unsigned int>::const_iterator prevOwn = g_owner.find(uid);   /* fold 1 item 2 [m7a2f-sp2]: the owner it is taken from */
    const bool prevKnown = prevOwn != g_owner.end();
    const unsigned int prevKey = prevKnown ? prevOwn->second : 0u;
    g_mineGen[uid] = gen != 0 ? gen : cooplo::GenReadopt(MineGenOf(uid), CopyGenOf(uid));   /* M7a A1 build 1 [a1b1-sp5]: the gen the XFER / hire named + 1; 0 = out-rank every record here */
    g_copyGen.erase(uid);
    g_localOwned.insert(uid); g_releasedHere.erase(uid);
    coopown::OwnedMirrorInsert(&g_ownedMirror, uid);   /* O1: beside the set, always */
    coop::MedicalForgetCopy(uid);   /* this game drives it now (marked above first, so no detour sees it as a copy with no words): the old owner's medical words for its copy go */
    g_owner[uid] = g_myPeerId;
    SessionOwnerMovedAnnounce(uid, prevKnown, prevKey);   /* before this game's first SPAWN / STATE of it (XFER, hire) */
    coop::SquadIdxTaken(uid);   /* M7a A1 build 2 [a1b2-sp4] (design 2.3): this game runs it again - out of the squad index */
}

void SetLocalOwner(unsigned int uid)
{
    // g_myPeerId is 0 until a WELCOME arrives, which is also the host's id - harmless,
    // because a peer id only has to be consistent WITHIN this process for the authority
    // check, and inbound messages carry the sender's id from the transport.
    // Both: g_localOwned is the durable fact, g_owner keeps the per-session view consistent for
    // anything that reads it directly.
    // M7a A1 build 1 [a1b1-sp6] [review F6]: a new person starts at gen 1; a person this game ran or held before (the sweep re-adopting
    // its own) takes the highest gen known here + 1, so a re-adoption always out-ranks any copy record elsewhere.
    g_mineGen[uid] = cooplo::GenReadopt(MineGenOf(uid), CopyGenOf(uid));
    g_copyGen.erase(uid);
    g_localOwned.insert(uid); g_releasedHere.erase(uid);
    coopown::OwnedMirrorInsert(&g_ownedMirror, uid);   /* O1: beside the set, always */
    coop::MedicalForgetCopy(uid);   /* as TakeLocalOwner: after the marks, and no copy's medical words outlive this game taking the character */
    g_owner[uid] = g_myPeerId;
}

/* M7a A1 build 1 [a1b1-sp7]: the generation tables asked from outside (hire.cpp, handoff.cpp). 0 = no row. MAIN THREAD. */
unsigned int MineGenOf(unsigned int uid) { std::map<unsigned int, unsigned int>::const_iterator g = g_mineGen.find(uid); return g == g_mineGen.end() ? 0u : g->second; }
unsigned int CopyGenOf(unsigned int uid) { return CopyGenHere(uid); }
unsigned int GenForTake(unsigned int uid, unsigned int giverGen) { const unsigned int c = CopyGenHere(uid); return (giverGen > c ? giverGen : c) + 1u; }
int PeerSlotOfKey(unsigned int key) { const unsigned int k = OwnerKeyOf(key); return cooplive::IsRelayPeer(k) ? (int)cooplive::RelayPeerSlot(k) : -1; }
int OwnerSlotOf(unsigned int uid) { std::map<unsigned int, unsigned int>::const_iterator o = g_owner.find(uid); return o == g_owner.end() ? -1 : PeerSlotOfKey(o->second); }

static bool CharRoadOpen();   /* the character stream's road is open: the session link or the world road (defined with the stream) */
bool NextOwnedUid(unsigned int* out)
{
    if (!CharRoadOpen()) return false;   /* the periodic STATE refresh runs on whichever road carries the characters */
    static unsigned int s_cursor = 0;

    std::vector<unsigned int> mine(g_localOwned.begin(), g_localOwned.end());
    if (mine.empty()) return false;

    *out = mine[s_cursor % mine.size()];
    ++s_cursor;
    return true;
}

void OwnedUidsSnapshot(std::vector<unsigned int>* out)   /* S1 */
{
    out->clear();
    if (!CharRoadOpen()) return;   /* this game's own characters, while any road to another player is open */
    out->assign(g_localOwned.begin(), g_localOwned.end());
}

int OwnedUidCount()
{
    if (!CharRoadOpen()) return 0;
    return (int)g_localOwned.size();
}

bool IsUidMine(unsigned int uid)
{
    // Answered from g_localOwned, NOT g_owner (F278). Two consequences, both wanted:
    //   * it survives leave/rejoin, because authorship does;
    //   * a remote peer cannot claim a uid we created - inbound OnSpawn writes g_owner, and
    //     g_owner no longer decides this question.
    // Unknown ownership is still NOT ours (commitment 3): if we did not create it, we do not act
    // as its authority. Deliberately distinct from "peer-owned".
    return g_localOwned.find(uid) != g_localOwned.end();
}

// O1 (recheck-c2b). The same question from ANY thread: answered from g_ownedMirror, which the three owned-set
// writers update beside the set, so the answer is IsUidMine's as of the last completed update. No lock, no
// allocation, no std::set walk. A uid refused by a full table answers "not mine" until the next tick's rebuild
// (OwnedMirrorMaintain) re-inserts it.
bool IsUidMineAnyThread(unsigned int uid)
{
    return coopown::OwnedMirrorHas(&g_ownedMirror, uid);
}
// M4 fold (defence in depth): an owner record or a character this game runs - AllocateUid's row check. MAIN THREAD.
bool UidHasSessionRow(unsigned int uid)
{
    return g_owner.find(uid) != g_owner.end() || g_localOwned.find(uid) != g_localOwned.end();
}
// kit3 (kit2 review + T522, review D8): ANY THREAD - true while a "not mine" from IsUidMineAnyThread is proof (ownedmirror.h).
bool OwnedMirrorExactAnyThread()
{
    return coopown::OwnedMirrorExact(&g_ownedMirror);
}
// O1-b (review-o1): once per SessionTick, on the main thread - the only writer. When the published table is
// crowded (under 1/4 empty, tombstones present) or refused an insert since the last rebuild, clear the spare
// buffer, insert every uid of g_localOwned (walking the set allocates nothing) and publish it with one
// interlocked pointer swap. The retired buffer is rewritten only by the next rebuild, a tick later at the
// earliest; a worker lookup that loaded the old pointer finishes long before that (ownedmirror.h).
void OwnedMirrorMaintain()
{
    if (!coopown::OwnedMirrorNeedsRebuild(&g_ownedMirror)) return;
    coopown::OwnedTable* t = coopown::OwnedMirrorRebuildBegin(&g_ownedMirror);
    for (std::set<unsigned int>::const_iterator it = g_localOwned.begin(); it != g_localOwned.end(); ++it)
        coopown::OwnedMirrorRebuildAdd(&g_ownedMirror, t, *it);
    coopown::OwnedMirrorRebuildPublish(&g_ownedMirror, t);
}
long OwnedMirrorStat(int which)
{
    switch (which)
    {
    case 0: return coopown::OwnedMirrorLive(&g_ownedMirror);
    case 1: return coopown::OwnedMirrorTombs(&g_ownedMirror);
    case 2: return coopown::OwnedMirrorEmpty(&g_ownedMirror);
    case 3: return g_ownedMirror.rebuilds;
    case 4: return g_ownedMirror.full;
    }
    return -1;
}

// E22b-2 (P6b) / review-p5y MEDIUM-3. Does THAT PEER own this uid? The same two questions RemoteMayWrite
// asks - authorship wins, and the recorded owner must be the sender - but WITHOUT touching
// `g_remoteMayWriteRefused`, which is the [M1] REPORT's split-brain number and must not start counting
// item-request refusals as well. It lives out here rather than beside RemoteMayWrite because that one sits
// in this file's ANONYMOUS namespace: items.cpp has to be able to link against this one.
// M5b fold 1 (item 6): every refusal bumps g_uidOwnedByPeerRefused, which only relayOwn[refused] reads (as a difference around
// ONE relayed dispatch) - SAY, CRIME and CARRY_BREAK refuse a relayed sender here, not in RemoteMayWrite.
bool UidOwnedByPeer(unsigned int uid, unsigned int fromPeer)
{
    if (uid == 0) { ++g_uidOwnedByPeerRefused; return false; }
    if (g_localOwned.find(uid) != g_localOwned.end()) { ++g_uidOwnedByPeerRefused; return false; }   // authorship wins, as above
    std::map<unsigned int, unsigned int>::const_iterator own = g_owner.find(uid);
    const bool may = (own != g_owner.end() && OwnerMatch(own->second, fromPeer));   /* M5b: by player number */
    if (!may) ++g_uidOwnedByPeerRefused;
    return may;
}

// tags1 (the name label's colour): the recorded owner of a uid, whoever it is.
bool UidOwnerPeer(unsigned int uid, unsigned int* peer)
{
    std::map<unsigned int, unsigned int>::const_iterator own = g_owner.find(uid);
    if (own == g_owner.end()) return false;
    if (peer != 0) *peer = own->second;
    return true;
}
/* P25 fold 1 M1 [p25f1-cpp]: the recorded owner key names the session peer (cooplive::IsSessionPeerKey with this link's slot). */
bool OwnerIsSessionPeer(unsigned int recorded) { return cooplive::IsSessionPeerKey(recorded, coop::LinkPeerSlot()); }

/* M7b slice 1 (T-197; protocol 113) - A COPY-EFFECT REQUEST GOES TO ITS TARGET'S OWNER, AN ANSWER BACK TO ITS ASKER, by player
   number (src/common/ownerroute.h AddrRoute): the session link when that player is the session peer, else LIVE SLOT. Every send
   counts exactly one of viaSession / viaLive / noOwner / noSlot in its group (a road that refused the send counts noSlot);
   toOwner counts the requests the owner table handed to a road (SendToOwner returned true; M7b fold 1) - an answer (ReplyTo) is
   not one. noOwner counts once per (group, uid) until that uid's owner is known (ownerroute.h AddrNoOwnerCountOnce; the sender
   keeps retrying). live.toSlot.noSuchSlot: a LIVE SLOT send whose slot this link's PLAYERS roster shows NOT in the world - M7b
   fold 1: not sent, counted noSlot too, false to the caller (not delivered: it retries or waits); a stale roster still sends. */
static long long g_addr[cooplive::kAddrGroupCount][5];   /* [toOwner, viaSession, viaLive, noOwner, noSlot] */
static long long g_liveToSlotNoSuchSlot = 0;
static long long g_itemNoArea = 0;   /* M7b slice 2: an item move / box push with no sector to name - sent WORLD */
static long long g_sideNoArea = 0;   /* M7b slice 4: a SAY / STATS / door holder's answer with no sector to name - sent WORLD (CRIME, a BOUNTY list and BUILD are WORLD by rule) */
static long long g_sideBoth = 0, g_sideWidened = 0, g_sideSlotUnknown = 0;   /* fold 1 (F4): both roads; AREA widened to WORLD_EXCEPT the peer; peer slot unknown - session only */
static long long g_nshRecv[2][3];   /* [0 session link, 1 world server][name, slave, hire] - received */
static long long g_sideRecv[2][7];   /* M7b slice 4: [0 session link, 1 notebook][say, stats, crime, bounty, talk, build, door] - received */
static long long g_itemReSentToAsked = 0, g_itemAskedReleased = 0, g_itemAskedGoneReleased = 0;   /* fold 1: re-sends held to the game asked; refusals that released one */
enum { kAddrColToOwner = 0, kAddrColSession = 1, kAddrColLive = 2, kAddrColNoOwner = 3, kAddrColNoSlot = 4 };
static void AddrCount(MsgType type, int col)
{
    const int g = cooplive::AddrGroupOf((unsigned int)type);
    if (g >= 0) ++g_addr[g][col];
}
static std::set<unsigned long long> g_addrNoOwnerSeen;   /* M7b fold 1: (group, uid) already counted noOwner */
/* uid: the request's target (SendToOwner), for the once-per-uid noOwner count; 0 for an answer (ReplyTo). */
static bool AddrSendToKey(bool haveKey, unsigned int key, MsgType type, const char* p, size_t n, unsigned int uid)
{
    if (p == 0 || n == 0) return false;
    const bool sessionUp = g_transport != 0 && g_transport->State() == LINK_UP;
    const int linkSlot = coop::LinkPeerSlot();
    const int ownerSlot = haveKey ? cooplive::AddrOwnerSlotOf(key, linkSlot) : cooplive::kAddrOwnerNone;
    const cooplive::AddrPlan a = cooplive::AddrRoute(ownerSlot, linkSlot, sessionUp, coop::StoreLiveReady());
    const int group = cooplive::AddrGroupOf((unsigned int)type);
    if (a.why != cooplive::kAddrWhyNoOwner) cooplive::AddrNoOwnerCountOnce(&g_addrNoOwnerSeen, group, uid, false);   /* the owner is known */
    if (a.road == cooplive::kAddrSession)
    {
        const bool ok = g_transport->Send(0, type, p, n, CH_RELIABLE);
        AddrCount(type, ok ? kAddrColSession : kAddrColNoSlot);
        return ok;
    }
    if (a.road == cooplive::kAddrLive)
    {
        if (!cooplive::AddrLiveSendGoes(coop::StoreRosterSlotInWorld((int)a.slot)))   /* M7b fold 1: not delivered - the caller retries / waits */
        { ++g_liveToSlotNoSuchSlot; AddrCount(type, kAddrColNoSlot); return false; }
        const std::vector<char> v(p, p + n);
        const bool ok = coop::StoreSendLive(cooplive::kRouteSlot, a.slot, (unsigned int)type, v, true);
        AddrCount(type, ok ? kAddrColLive : kAddrColNoSlot);
        return ok;
    }
    if (a.why != cooplive::kAddrWhyNoOwner) AddrCount(type, kAddrColNoSlot);
    else if (cooplive::AddrNoOwnerCountOnce(&g_addrNoOwnerSeen, group, uid, true)) AddrCount(type, kAddrColNoOwner);   /* M7b fold 1: once per uid */
    return false;
}
/* A request about `targetUid` to the player that owns it. This game's own uid has no one to ask (noOwner, once per uid). */
static bool SendToOwner(MsgType type, unsigned int targetUid, const char* p, size_t n)
{
    if (p == 0 || n == 0) return false;   /* M7b fold 1: nothing to hand off - counted nowhere */
    if (targetUid == 0 || g_localOwned.find(targetUid) != g_localOwned.end())
    {
        if (cooplive::AddrNoOwnerCountOnce(&g_addrNoOwnerSeen, cooplive::AddrGroupOf((unsigned int)type), targetUid, true)) AddrCount(type, kAddrColNoOwner);
        return false;
    }
    std::map<unsigned int, unsigned int>::const_iterator own = g_owner.find(targetUid);
    const bool have = own != g_owner.end();
    const bool ok = AddrSendToKey(have, have ? own->second : 0u, type, p, n, targetUid);
    if (ok) AddrCount(type, kAddrColToOwner);   /* M7b fold 1: counted after a successful hand-off only */
    return ok;
}
/* An answer to the player whose message asked (the handler's sender key: a relayed slot, or the session peer). */
static bool ReplyTo(unsigned int peer, MsgType type, const char* p, size_t n)
{
    return AddrSendToKey(true, peer, type, p, n, 0u);
}
static std::string AddrCountsString()
{
    std::string s = " addr[toOwner,viaSession,viaLive,noOwner,noSlot]";
    for (int g = 0; g < cooplive::kAddrGroupCount; ++g)
    {
        s += std::string(" addr.") + cooplive::AddrGroupName(g) + "=";
        for (int c = 0; c < 5; ++c) { if (c) s += ","; s += N(g_addr[g][c]); }
    }
    s += " sideNoArea=" + N(g_sideNoArea);   /* M7b slice 4: and what arrived by each road */
    s += " side[both,widened,slotUnknown]=" + N(g_sideBoth) + "," + N(g_sideWidened) + "," + N(g_sideSlotUnknown);   /* fold 1 (F4) */
    for (int r = 0; r < 2; ++r)
    {
        s += (r == 0) ? " sideRecv[say,stats,crime,bounty,talk,build,door].session=" : " sideRecv[say,stats,crime,bounty,talk,build,door].live=";
        for (int k = 0; k < 7; ++k) { if (k) s += ","; s += N(g_sideRecv[r][k]); }
    }
    for (int r = 0; r < 2; ++r)
    {
        s += (r == 0) ? " nshRecv[name,slave,hire].session=" : " nshRecv[name,slave,hire].live=";
        for (int k = 0; k < 3; ++k) { if (k) s += ","; s += N(g_nshRecv[r][k]); }
    }
    return s + " live.toSlot.noSuchSlot=" + N(g_liveToSlotNoSuchSlot) + " itemNoArea=" + N(g_itemNoArea)
        + " itemAsked[reSentToAsked,released,goneReleased]=" + N(g_itemReSentToAsked) + "," + N(g_itemAskedReleased) + "," + N(g_itemAskedGoneReleased);
}
static int CharRoadNow();   /* M7b slice 1: SendHit rides the character stream's road (defined with the stream, below) */
static bool CharSend(MsgType type, unsigned int uid, const char* b, size_t n, Channel ch, int keyHint);

bool SendHit(unsigned int victimUid, unsigned int attackerUid, int cutDirection,
             const char* damages24, int comboId,
             const float* parts, int partCount, float blood,
             const ObjId& attackerId, unsigned char knock)
{
    /* M7b slice 1: no session-link gate here - the character stream's road (CharRoadNow) decides, below */
    // uid(u32) | attackerUid(u32) | cutDir(i32) | comboId(i32) | Damages(24)
    //   | partCount(u32) | flesh(n * f32) | maxHealth(n * f32) | blood(f32)
    // F120: the CEILING travels with the value. Sending flesh alone could never converge -
    // the receiver clamped every authoritative value above its own maxHealthBase (T035).
    // F117: the attacker travels because the engine's hit function DEREFERENCES it - we
    // were passing null and it killed the receiving instance outright (T033).
    // F119: the resulting per-limb health travels in the SAME message as the hit that
    // caused it. One message keeps them atomic and ordered - a separate health packet
    // could arrive out of order and briefly show the wrong wound.
    std::vector<char> b;
    b.resize(44);
    std::memcpy(&b[0],  &victimUid,    4);
    std::memcpy(&b[4],  &attackerUid,  4);
    std::memcpy(&b[8],  &cutDirection, 4);
    std::memcpy(&b[12], &comboId,      4);
    std::memcpy(&b[16], damages24,    24);
    unsigned int n = (unsigned int)(partCount > 0 ? partCount : 0);
    std::memcpy(&b[40], &n, 4);
    // F127: the whole per-part record, kPartFloats floats each, in the engine's field order.
    for (unsigned int i = 0; i < n * kPartFloats; ++i)
    {
        size_t at = b.size();
        b.resize(at + 4);
        std::memcpy(&b[at], &parts[i], 4);
    }
    size_t at = b.size();
    b.resize(at + 4);
    std::memcpy(&b[at], &blood, 4);

    // PARITY P-2: the attacker's engine handle, appended last so the block that existed at
    // protocol 6 keeps its offsets and only one new field has to be reasoned about.
    // Five u32 in the order identity.h declares them. All-zero = "not named".
    const unsigned int idFields[5] = { attackerId.index, attackerId.serial, attackerId.type,
                                       attackerId.container, attackerId.containerStamp };
    at = b.size();
    b.resize(at + 20);
    std::memcpy(&b[at], idFields, 20);

    // K2 (decision 61 follow-up): one trailing byte, at bloodAt + 24: 1 = this hit knocked the victim
    // down here. Older receivers ignore it (they check only a minimum size).
    b.push_back((char)(knock != 0 ? 1 : 0));

    /* M7b slice 1: a HIT is the VICTIM'S OWNER's report (this game runs the victim - OnHit's RemoteMayWrite) to every game that
       holds a copy, so it rides the character stream's road like the SWING that caused it: the session link while the session
       peer is not proven reachable through the notebook, else LIVE AREA of the victim's sector (liverelay.h). */
    const int road = CharRoadNow();
    const bool ok = CharSend(MSG_HIT, victimUid, &b[0], b.size(), CH_RELIABLE, -1);
    AddrCount(MSG_HIT, !ok ? kAddrColNoSlot : (road == cooplive::kCharRoadSession ? kAddrColSession : kAddrColLive));
    return ok;
}

/* M7a (T-197 piece 7a; protocol 108; owner decisions 54(a), 57) - THE CHARACTER STREAM GOES THROUGH THE NOTEBOOK.
   SPAWN, CONTEXT, APPEARANCE, CLOTHING, MOVE, TASK, INTENT, STATE, COMBATMODE, SWING, DESPAWN and UNLOAD take ONE road each
   (cooplive::CharStreamRoad): LIVE whenever this game's notebook link is up and welcomed (coop::StoreLiveReady), the session link
   ONLY while it is not - never both, and a send a road refused is never retried on the other. On the notebook road every type
   but two goes route AREA with the character's sector READ LIVE at the send (the payload's own position for SPAWN and MOVE, the
   engine's for the rest - coop::CharAreaKeyNow) and, when it differs, the sector it was last sent in, so the message that carries
   a character over a delivery edge reaches the games on both sides (cooplive::CharAreaTarget). DESPAWN and UNLOAD go WORLD
   (cooplive::CharStreamRoute). A character whose sector cannot be read goes AREA in its last sector, or WORLD (noArea) if it has
   none. While the worldsync catch-up answers an ask (CharStreamToSlot) the stream goes route SLOT to the asking game only, and is
   refused rather than put on the session link. Counted per type and per road: live.sent / live.recv / session.sent /
   session.recv on the [net] REPORT line. MAIN THREAD. */
static long long g_charLiveSent[cooplive::kCharStreamTypes] = {0}, g_charLiveRecv[cooplive::kCharStreamTypes] = {0};
static long long g_charSessSent[cooplive::kCharStreamTypes] = {0}, g_charSessRecv[cooplive::kCharStreamTypes] = {0};
static long long g_charLiveFailed = 0, g_charNoArea = 0, g_charToSlotSent = 0, g_charToSlotRefused = 0, g_charRelayForeign = 0;
static int g_charToSlot = -1;
static std::map<unsigned int, int> g_charLastArea;   /* uid -> the sector its last AREA send named; erased by DESPAWN / UNLOAD */
void CharStreamToSlot(int slot) { g_charToSlot = slot; }
void CharLastAreaForget(unsigned int uid) { g_charLastArea.erase(uid); }   /* M7a fold F9: a hand-over (either side) */
/* M7a A1 build 1 [a1b1-sp12] (design 2.2 [review F10]): THE RECONNECT SWEEP IS RETIRED (g_cuSweep, its claims, its marks, its END and
   timeout withdrawals) - the ROSTER replaces it: at every notebook WELCOME each owner sends its per-sector HASH and each copy holder
   CHECKs every copy of each owner (welcomeChecks); a copy its owner no longer runs is answered NOT-LIVE and withdrawn. F4 stays:
   relayed SPAWNs refused for another player's record. */
static long long g_relaySpawnForeign = 0;
/* M7a2 (T-197 second half, part 1; game-to-game protocol 112) [m7a2-sp3]. MAIN THREAD, all of it.
   (its cuSweep counters - items 1, 2, 3 and 7 - were retired with the reconnect sweep [a1b1-sp12]; fold 1 [a1b1f1-spC] R8)
   cuAsk (item 7): asks booked, ENDs in, asks ended by every owner, ENDs of no booked ask (late / repeated), asks ended by the timeout.
   ownerMoved (item 3): sent, send refused, no slot for the new owner, in, taken, for an unknown uid, for one this game runs, refused.
   moveHold (item 6): MOVEs held, replaced by a newer one, refused (the hold is full), applied after the SPAWN, dropped (another sender),
   expired. */
static cooplive::CatchupAskBook g_cuAsks;
static long long g_cuAsksOpened = 0, g_cuAskEnds = 0, g_cuAsksDone = 0, g_cuAskEndsLate = 0, g_cuAsksTimedOut = 0;
static long long g_ownerMovedSent = 0, g_ownerMovedSendFailed = 0, g_ownerMovedNoSlot = 0, g_ownerMovedIn = 0, g_ownerMovedTaken = 0, g_ownerMovedUnknown = 0, g_ownerMovedMine = 0, g_ownerMovedRefused = 0;
static cooplive::MoveHoldBook g_moveHold;
static long long g_moveHeld = 0, g_moveHeldReplaced = 0, g_moveHeldFull = 0, g_moveHeldApplied = 0, g_moveHeldDropped = 0, g_moveHeldExpired = 0;
/* M7a2 fold 1 [m7a2f-sp0]. ownerMoved: deferred (item 1 - the old owner's word owed while its world link was down), sentLate (owed, sent
   once the link was ready - also counted in sent), owedDropped (no longer owed: this game runs it again, no record, or the owed set full),
   announced / announceSkipped (item 2 - the taker's word; skipped: no previous owner with a slot, no slot of its own, or no world link),
   noop (an OWNER_MOVED naming the owner on record), takenAnnounce (taken from the taker's own word). (its cuSweep counters - items 3, 4, 5 and 9 - were retired with the reconnect sweep; fold 1 R8) */
/* fold 2 [m7a2f2-sp0]: the owed words - uid -> kOwedRelease (the old owner's word) or the previous owner's key (the TAKER's word, owed
   while its world link is down). A later release or take replaces the entry; a failed send re-queues it; past kOwnerMovedOwedMax a new
   entry is refused, counted (owedFull) and logged (the first 5 and every 100th). (keptSilentOwner was retired with the reconnect sweep; fold 1 R8) */
static std::map<unsigned int, unsigned int> g_ownerMovedOwed;
const size_t kOwnerMovedOwedMax = 1024;
const unsigned int kOwedRelease = 0xFFFFFFFFu;
static long long g_ownerMovedOwedFull = 0;
static long long g_ownerMovedStale = 0;   /* M7a A1 build 1 [a1b1-sp13]: OWNER_MOVED naming a lower gen than the copy's record - ignored */
static long long g_ownerMovedDeferred = 0, g_ownerMovedSentLate = 0, g_ownerMovedOwedDropped = 0, g_ownerMovedAnnounced = 0, g_ownerMovedAnnounceSkipped = 0, g_ownerMovedNoop = 0, g_ownerMovedTakenAnnounce = 0;
static void OwedPut(unsigned int uid, unsigned int what)   /* fold 2 [m7a2f2-sp1] */
{
    if (g_ownerMovedOwed.size() >= kOwnerMovedOwedMax && g_ownerMovedOwed.find(uid) == g_ownerMovedOwed.end())
    {
        ++g_ownerMovedOwedFull;
        if (cooplive::LiveLogThis(g_ownerMovedOwedFull))
            DebugLog("[net] OWNER_MOVED for uid=" + N(uid) + " NOT owed - " + N((long long)kOwnerMovedOwedMax) + " hand-over words are owed already (ownerMoved owedFull " + N(g_ownerMovedOwedFull) + "; the first 5 and every 100th are logged)");
        return;
    }
    g_ownerMovedOwed[uid] = what; ++g_ownerMovedDeferred;
}
static double CuNowSec() { static LARGE_INTEGER f; static bool have = false; if (!have) { ::QueryPerformanceFrequency(&f); have = true; } LARGE_INTEGER c; ::QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart; }
void SessionDispatchOne(const Message& m);   /* M7a2: defined below (the handlers' switch) - a claim's SPAWN and a held MOVE run through it */
/* item 6: a relayed MOVE for a uid this game has no record of - held (the latest), applied after that character's SPAWN. */
static bool MoveHoldIfUnknown(unsigned int peer, const std::vector<char>& payload)
{
    if (!cooplive::IsRelayPeer(peer) || payload.size() < 36) return false;
    unsigned int uid = 0; std::memcpy(&uid, &payload[0], 4);
    if (cooplive::RelayedMoveHoldDecide(true, g_localOwned.find(uid) != g_localOwned.end(), g_owner.find(uid) != g_owner.end()) != cooplive::kMoveHoldHold) return false;
    const int r = cooplive::MoveHoldPut(&g_moveHold, uid, OwnerKeyOf(peer), payload, CuNowSec());
    if (r == cooplive::kMoveHoldPutFull) ++g_moveHeldFull; else if (r == cooplive::kMoveHoldPutReplaced) ++g_moveHeldReplaced; else ++g_moveHeld;
    return true;
}
static void MoveHoldReplayAfterSpawn(const std::vector<char>& spawn)
{
    if (g_moveHold.rows.empty() || spawn.size() < 4) return;
    unsigned int uid = 0; std::memcpy(&uid, &spawn[0], 4);
    std::vector<char> mv; unsigned int from = 0;
    if (!cooplive::MoveHoldTake(&g_moveHold, uid, &mv, &from)) return;
    std::map<unsigned int, unsigned int>::const_iterator own = g_owner.find(uid);
    if (g_localOwned.find(uid) != g_localOwned.end() || own == g_owner.end() || !OwnerMatch(own->second, from)) { ++g_moveHeldDropped; return; }
    Message m; m.type = MSG_MOVE; m.peer = from; m.payload.swap(mv);
    SessionDispatchOne(m);
    ++g_moveHeldApplied;
}
/* M7a A1 build 1 [a1b1-sp14]: an owner's CATCHUP_END now only ends this game's ask (the ask book) - the sweep half is retired */
static void SessionCatchupAskEndApply(unsigned int peer, const std::vector<char>& p)
{
    cooplive::CatchupEndMsg e;
    if (!cooplive::IsRelayPeer(peer) || !cooplive::CatchupEndDecode(p.empty() ? 0 : &p[0], p.size(), &e)) return;
    ++g_cuAskEnds;
    const int ab = cooplive::CatchupAskEnd(&g_cuAsks, e.askNo);   /* item 7 */
    if (ab == cooplive::kAskEndDone) ++g_cuAsksDone; else if (ab == cooplive::kAskEndUnknown) ++g_cuAskEndsLate;
}
void SessionCatchupAskedOpen(unsigned int askNo, unsigned int owners)
{
    if (owners == 0) return;
    cooplive::CatchupAskOpen(&g_cuAsks, askNo, owners, CuNowSec());
    ++g_cuAsksOpened;
}
/* item 3: THE OLD OWNER'S WORD AT A HAND-OVER - from ReleaseLocalOwner (XFER ACK, hire), route WORLD.
   M7a2 fold 1 [m7a2f-sp5]: item 1 - with its world link down the word is OWED (g_ownerMovedOwed) and sent by SessionCatchupTick once the
   link is ready, unless this game runs the character again or has no record of it; it names this game as the previous owner (12 bytes).
   Item 2 - the TAKER announces too (SessionOwnerMovedAnnounce, from TakeLocalOwner). */
static bool OwnerMovedSendNow(unsigned int uid)
{
    std::map<unsigned int, unsigned int>::const_iterator o = g_owner.find(uid);
    if (o == g_owner.end()) return false;
    const unsigned int key = OwnerKeyOf(o->second);
    if (!cooplive::IsRelayPeer(key)) { ++g_ownerMovedNoSlot; return false; }
    const int me = coop::StoreMySlot();
    std::vector<char> b;   /* M7a A1 build 1 [a1b1-sp21]: 16 bytes - the new owner's gen (this game's copy record of it, ReleaseLocalOwner) */
    cooplo::OwnerMovedEncode16(&b, uid, cooplive::RelayPeerSlot(key), me >= 0 ? (unsigned int)me : 0xFFFFu, CopyGenHere(uid));
    if (coop::StoreSendLive(cooplive::kRouteWorld, 0, cooplive::kInnerOwnerMoved, b, true)) { ++g_ownerMovedSent; return true; }
    ++g_ownerMovedSendFailed;
    OwedPut(uid, kOwedRelease);   /* fold 2 [m7a2f2-sp2]: re-queued, not dropped */
    return false;
}
void SessionOwnerMovedSend(unsigned int uid)
{
    if (g_owner.find(uid) == g_owner.end()) return;
    g_ownerMovedOwed.erase(uid);   /* fold 2 [m7a2f2-sp3]: this release replaces any word owed for it */
    if (!coop::StoreLiveReady()) { OwedPut(uid, kOwedRelease); return; }
    OwnerMovedSendNow(uid);
}
/* item 2 (manager decision): THE TAKER'S WORD - OWNER_MOVED {uid, new = this game, previous = the owner it took it from}, route WORLD, the
   moment it takes a character over (XFER, hire), so its world-server order puts it before this game's first SPAWN / STATE of it. */
/* fold 2 [m7a2f2-sp4]: the taker's word now - with no world link or no slot of its own yet it is OWED (re-queued on a failed send). */
static bool AnnounceSendNow(unsigned int uid, unsigned int pk)
{
    const int me = coop::StoreMySlot();
    if (me < 0 || !coop::StoreLiveReady()) { OwedPut(uid, pk); return false; }
    if ((int)cooplive::RelayPeerSlot(pk) == me) { ++g_ownerMovedAnnounceSkipped; return false; }
    std::vector<char> b; cooplo::OwnerMovedEncode16(&b, uid, (unsigned int)me, cooplive::RelayPeerSlot(pk), MineGenOf(uid));   /* M7a A1 build 1 [a1b1-sp22]: the taker's gen */
    if (coop::StoreSendLive(cooplive::kRouteWorld, 0, cooplive::kInnerOwnerMoved, b, true)) { ++g_ownerMovedAnnounced; return true; }
    ++g_ownerMovedSendFailed; OwedPut(uid, pk);
    return false;
}
void SessionOwnerMovedAnnounce(unsigned int uid, bool prevKnown, unsigned int prevKey)
{
    g_ownerMovedOwed.erase(uid);   /* item 1: this game runs it again - an old owner's word owed for it is void */
    const unsigned int pk = OwnerKeyOf(prevKey);
    if (!prevKnown || !cooplive::IsRelayPeer(pk)) { ++g_ownerMovedAnnounceSkipped; return; }
    AnnounceSendNow(uid, pk);
}
static void SessionOwnerMovedApply(unsigned int peer, const std::vector<char>& p)
{
    unsigned int uid = 0, slot = 0, prev = 0, gen = 0;   /* M7a A1 build 1 [a1b1-sp23]: 16 bytes with the new owner's gen */
    if (!cooplive::IsRelayPeer(peer) || !cooplo::OwnerMovedDecode16(p.empty() ? 0 : &p[0], p.size(), &uid, &slot, &prev, &gen)
        || slot > cooplive::kLiveSlotMax || prev > cooplive::kLiveSlotMax) return;
    ++g_ownerMovedIn;
    std::map<unsigned int, unsigned int>::iterator o = g_owner.find(uid);
    const bool known = o != g_owner.end();
    const bool mine = g_localOwned.find(uid) != g_localOwned.end();
    const unsigned int newKey = OwnerKeyOf(cooplive::RelayPeerId(slot));
    const bool newIsSender = OwnerMatch(newKey, peer);
    const int d = cooplive::OwnerMovedDecide(mine, known, known && OwnerMatch(o->second, peer), newIsSender,
                                             known && OwnerMatch(o->second, cooplive::RelayPeerId(prev)), known && OwnerMatch(o->second, cooplive::RelayPeerId(slot)));
    if (d == cooplive::kOwnerMovedUnknown) { ++g_ownerMovedUnknown; return; }   /* no copy here - its first SPAWN, from the new owner, is a first SPAWN */
    if (d == cooplive::kOwnerMovedMine) { ++g_ownerMovedMine; return; }   /* this game took it (the XFER's receiver) */
    if (d == cooplive::kOwnerMovedNoop) { ++g_ownerMovedNoop; if (gen > CopyGenHere(uid)) g_copyGen[uid] = gen; return; }   /* fold 1 item 2: the other word of the same hand-over (A1: its gen stamps the copy) */
    if (d == cooplive::kOwnerMovedTake && cooplo::OwnerMovedStale(CopyGenHere(uid), gen))   /* M7a A1 build 1 [a1b1-sp24]: a later owner is known here */
    {
        ++g_ownerMovedStale;
        if (cooplive::LiveLogThis(g_ownerMovedStale))
            DebugLog("[net] OWNER_MOVED for uid=" + N(uid) + " to slot " + N((long long)slot) + " at gen " + N((long long)gen) + " IGNORED - the copy here carries gen " + N((long long)CopyGenHere(uid))
                     + " (ownerMoved stale " + N(g_ownerMovedStale) + "; the first 5 and every 100th are logged)");
        return;
    }
    if (d != cooplive::kOwnerMovedTake)
    {
        ++g_ownerMovedRefused;
        if (cooplive::LiveLogThis(g_ownerMovedRefused))
            DebugLog("[net] OWNER_MOVED for uid=" + N(uid) + " from slot " + N((long long)cooplive::RelayPeerSlot(peer)) + " to slot " + N((long long)slot) + " (previous slot " + N((long long)prev)
                     + ") REFUSED - the sender is not its recorded owner, nor the new owner naming the recorded one as previous (ownerMoved refused " + N(g_ownerMovedRefused) + ")");
        return;
    }
    o->second = newKey; ++g_ownerMovedTaken;
    g_copyGen[uid] = gen;   /* M7a A1 build 1 [a1b1-sp25] */
    if (newIsSender) ++g_ownerMovedTakenAnnounce;
    g_departedPending.erase(uid);   /* M8: a hand-over after a departure supersedes it (as ReleaseLocalOwner) */
    if (cooplive::LiveLogThis(g_ownerMovedTaken))
        DebugLog("[net] OWNER_MOVED: uid=" + N(uid) + " handed over to slot " + N((long long)slot) + " (word of slot " + N((long long)cooplive::RelayPeerSlot(peer))
                 + (newIsSender ? ", the new owner" : ", the old owner") + ") - the copy here now takes that player's stream (ownerMoved taken " + N(g_ownerMovedTaken) + "; the first 5 and every 100th are logged)");
}
/* item 7: once a second - an ask nobody ended, a sweep nobody ended and a held MOVE nobody claimed end by the clock.
   fold 1 [m7a2f-sp6]: a sweep at its timeout first takes the claims on its still-marked copies (item 3), then WITHDRAWS the copies still
   marked (item 5 - their owner sent no END; the owner record is forgotten, so its next SPAWN brings a live character back); it waits while
   engine writes are blocked. The OWNER_MOVED words owed go out once the world link is ready (item 1). */
static void SessionCatchupTick()
{
    static double s_last = -1.0;
    const double now = CuNowSec();
    if (s_last >= 0.0 && now - s_last < 1.0 && now >= s_last) return;
    s_last = now;
    std::vector<unsigned int> ex;
    const size_t n = cooplive::CatchupAskExpire(&g_cuAsks, now, cooplive::kCatchupAskTimeoutSec, &ex);
    if (n != 0)
    {
        g_cuAsksTimedOut += (long long)n;
        if (cooplive::LiveLogThis(g_cuAsksTimedOut))
            DebugLog("[net] catch-up #" + N((long long)ex[0]) + ": not ended by every asked owner within " + N((long long)cooplive::kCatchupAskTimeoutSec)
                     + " s - ended by the clock (cuAsk timedOut " + N(g_cuAsksTimedOut) + "; the first 5 and every 100th are logged)");
    }
    /* M7a A1 build 1 [a1b1-sp26]: the OWNER_MOVED words owed go out from the outbox tick (OutboxTick) */
    g_moveHeldExpired += (long long)cooplive::MoveHoldExpire(&g_moveHold, now, cooplive::kMoveHoldSec);
}
/* M7a2 fold 1, manager ruling [m7a2f-ap1] - THE CATCH-UP'S ENGINE WRITES. CommandChannelTick calls this right AFTER coop::InQueueDrain() -
   the main thread, the same moment as the drain's own SPAWN / DESPAWN applies - so SessionTick stays POLLS AND ENQUEUES. (1) the reverse
   catch-up the store pump queued, 64 characters a call (worldsync.cpp, item 8); (2) M7a A1 build 1: the orphan-copy rule's withdrawals
   (liveowner.h OrphanCopyAction) - the reconnect sweep's timeout that stood here is retired with the sweep. */
static void LiveOwnerApplyTick();   /* M7a A1 build 1 [a1b1-sp38]: defined with the ROSTER below */
std::string LiveOwnerCountsString();
void LostCopyTick();                  /* defined beside OnResend */
void ReturnCheckTick();               /* defined beside OnResend: the copies asked of their owners after this game's world link came back */
std::string LostCopyCountsString();   /* defined beside OnResend */
void SessionCatchupApplyTick()
{
    coop::WorldsyncCatchupReverseTick();
    LostCopyTick();   /* once a second, every lost copy is looked at (asked for while its spot is loaded here) */
    ReturnCheckTick();   /* after this game's world link came back: each copy its owner has not streamed since is asked of its owner */
    coop::PlayerGoneTakeOverTick();   /* a final leaver's NPCs held for the area's taker */
    LiveOwnerApplyTick();   /* M7a A1 build 1 [a1b1-sp15]: the orphan-copy rule's withdrawals (engine writes, after the drain) - the sweep's timeout is retired */
}
static int CharRoadNow()   /* M7a fold F1: RELATION's rule - the session link while the session peer is not proven reachable through the notebook */
{
    return cooplive::CharStreamRoad(coop::StoreLiveReady(), g_transport != 0 && g_transport->State() == LINK_UP, SessionPeerRelayOk(), coop::LinkPeerSlot());
}
static bool CharRoadOpen() { return CharRoadNow() != cooplive::kCharRoadNone; }
/* M7b slice 4 fold 1 (F5): the road (and route) the last character-stream send took - a CRIME that follows a SPAWN goes the same way */
struct CharLastSent { bool valid, session; unsigned int uid, route, target; };
static CharLastSent g_charLastSent = { false, false, 0u, 0u, 0u };
static bool CharSend(MsgType type, unsigned int uid, const char* b, size_t n, Channel ch, int keyHint)
{
    const int ti = cooplive::CharStreamIndex((unsigned int)type);
    int road = CharRoadNow();
    g_charLastSent.valid = false;   /* fold 1 (F5) */
    if (g_charToSlot >= 0)   /* fold F1: a catch-up answer goes to the asker by SLOT - it asked through the notebook, so it is there */
    {
        if (!coop::StoreLiveReady()) { ++g_charToSlotRefused; return false; }
        road = cooplive::kCharRoadLive;
    }
    if (road == cooplive::kCharRoadNone || b == 0 || n == 0) return false;
    if (road == cooplive::kCharRoadSession)
    {
        const bool ok = g_transport->Send(0, type, b, n, ch);
        if (ok && ti >= 0) ++g_charSessSent[ti];
        if (ok) { g_charLastSent.valid = true; g_charLastSent.session = true; g_charLastSent.uid = uid; }   /* fold 1 (F5) */
        if (type == MSG_DESPAWN || type == MSG_UNLOAD) g_charLastArea.erase(uid);   /* fold F9: its next SPAWN names its sector afresh */
        return ok;
    }
    unsigned int route = cooplive::kRouteWorld, target = 0;
    bool reliable = (ch == CH_RELIABLE);   /* M16 fold 3 (T775): the caller's channel, as on the session link - a MOVE or STATE is latest-wins */
    if (g_charToSlot >= 0) { route = cooplive::kRouteSlot; target = (unsigned int)g_charToSlot; }
    else if (cooplive::CharStreamRoute((unsigned int)type) == (unsigned int)cooplive::kRouteArea)
    {
        std::map<unsigned int, int>::iterator last = g_charLastArea.find(uid);
        const int prev = (last != g_charLastArea.end()) ? last->second : -1;
        int key = keyHint >= 0 ? keyHint : coop::CharAreaKeyNow(uid);
        if (key < 0) key = prev;   /* unreadable now: the sector it was last sent in */
        if (key < 0) ++g_charNoArea;
        else
        {
            route = cooplive::kRouteArea; target = cooplive::CharAreaTarget(key, prev); g_charLastArea[uid] = key;
            if (prev >= 0 && prev != key) reliable = true;   /* M16 fold 3: the send that carries it over a sector edge (both sectors named) is never lost */
        }
    }
    else g_charLastArea.erase(uid);   /* DESPAWN / UNLOAD: its next SPAWN names its sector afresh */
    const std::vector<char> v(b, b + n);
    if (!coop::StoreSendLive(route, target, (unsigned int)type, v, reliable)) { ++g_charLiveFailed; return false; }
    if (ti >= 0) ++g_charLiveSent[ti];
    if (route == (unsigned int)cooplive::kRouteSlot) ++g_charToSlotSent;
    g_charLastSent.valid = true; g_charLastSent.session = false; g_charLastSent.uid = uid; g_charLastSent.route = route; g_charLastSent.target = target;   /* fold 1 (F5) */
    return true;
}
static std::string CharCountsArr(const long long* a)
{
    std::string s;
    for (int i = 0; i < cooplive::kCharStreamTypes; ++i) { if (i != 0) s += ","; s += N(a[i]); }
    return s;
}
std::string CharStreamCountsString()
{
    std::string names;
    for (int i = 0; i < cooplive::kCharStreamTypes; ++i) { if (i != 0) names += ","; names += cooplive::CharStreamName(i); }
    const int road = CharRoadNow();
    return std::string(" charRoad=") + (road == cooplive::kCharRoadLive ? "live" : (road == cooplive::kCharRoadSession ? "session" : "none"))
        + " live.sent[" + names + "]=" + CharCountsArr(g_charLiveSent) + " live.recv[" + names + "]=" + CharCountsArr(g_charLiveRecv)
        + " session.sent[" + names + "]=" + CharCountsArr(g_charSessSent) + " session.recv[" + names + "]=" + CharCountsArr(g_charSessRecv)
        + " charLive[failed,noArea,toSlotSent,toSlotRefused,foreignRefused]=" + N(g_charLiveFailed) + "," + N(g_charNoArea) + ","
        + N(g_charToSlotSent) + "," + N(g_charToSlotRefused) + "," + N(g_charRelayForeign)
        + " relaySpawnForeign=" + N(g_relaySpawnForeign)   /* M7a fold F4 */
        + LostCopyCountsString()   /* lostCopy[...] resendIn[...] */
        + LiveOwnerCountsString()   /* M7a A1 build 1 [a1b1-sp16]: roster[...] receipt[...] gen[...] xferRoad[...]; cuSweep[...] is retired */
        + " cuAsk[opened,ends,done,late,timedOut]=" + N(g_cuAsksOpened) + "," + N(g_cuAskEnds) + "," + N(g_cuAsksDone) + "," + N(g_cuAskEndsLate) + "," + N(g_cuAsksTimedOut)
        + /*[m7a2f-sp9]*/ " ownerMoved[sent,sendFailed,noSlot,in,taken,unknown,mine,refused,deferred,sentLate,owedDropped,announced,announceSkipped,noop,takenAnnounce,owedFull]=" /*[m7a2f2-sp11]*/ + N(g_ownerMovedSent) + "," + N(g_ownerMovedSendFailed) + "," + N(g_ownerMovedNoSlot)
        + "," + N(g_ownerMovedIn) + "," + N(g_ownerMovedTaken) + "," + N(g_ownerMovedUnknown) + "," + N(g_ownerMovedMine) + "," + N(g_ownerMovedRefused)
        + "," + N(g_ownerMovedDeferred) + "," + N(g_ownerMovedSentLate) + "," + N(g_ownerMovedOwedDropped) + "," + N(g_ownerMovedAnnounced)
        + "," + N(g_ownerMovedAnnounceSkipped) + "," + N(g_ownerMovedNoop) + "," + N(g_ownerMovedTakenAnnounce)   /* fold 1 [m7a2f-sp10] */
        + "," + N(g_ownerMovedOwedFull)   /* fold 2 [m7a2f2-sp12] */
        + " moveHold[held,replaced,full,applied,dropped,expired]=" + N(g_moveHeld) + "," + N(g_moveHeldReplaced) + "," + N(g_moveHeldFull)
        + "," + N(g_moveHeldApplied) + "," + N(g_moveHeldDropped) + "," + N(g_moveHeldExpired)
        + " moveStop[sentSession,sentLive,recvSession,recvLive,short,refused]=" + N(g_moveStopSent[0]) + "," + N(g_moveStopSent[1])
        + "," + N(g_moveStopRecv[0]) + "," + N(g_moveStopRecv[1]) + "," + N(g_moveStopShort) + "," + N(g_moveStopRefused);
}

bool SendState(unsigned int uid, const float* parts, int partCount,
               float blood, int prone, int dead,
               unsigned int latchBits, float nextKnockoutAt, float koTimer,
               unsigned int carryingUid, const float* rest, const coopstate::PoseWire* pose,
               const float* hunger,   /* par5 (parity P5, 79) */
               const cooplimb::LimbsWire* limbs)   /* LIMBS (133) */
{
    if (!CharRoadOpen()) return false;   /* M7a: the notebook road, or the session link while it is down */
    if (partCount <= 0 || (unsigned int)partCount > kMaxHealthParts) return false;

    // K1 (read-carry): built in src/common/statewire.h so the offline suite hits the same bytes.
    // uid | partCount | parts | blood | prone | dead | latchBits | nextKnockoutAt | koTimer (H029) | carryingUid (47)
    std::vector<char> b;
    coopstate::EncodeState(&b, uid, parts, (unsigned int)partCount, (unsigned int)kPartFloats, blood, prone, dead,
                           latchBits, nextKnockoutAt, koTimer, carryingUid, rest, pose, hunger, limbs);   /* R3: rest block (48); POSE: ACTPOSE (50); par5: hunger (79) */

    return CharSend(MSG_STATE, uid, &b[0], b.size(), CH_UNRELIABLE, -1);   /* M7a */
}

// P-15 / F220. RELIABLE on purpose: entering combat is an EDGE, not a level. A dropped
// unreliable packet would leave one screen fighting and the other standing still until the next
// edge, which is the exact defect this is meant to close.
bool SendCombatMode(unsigned int uid, bool on, unsigned int targetUid)
{
    if (!CharRoadOpen()) return false;   /* M7a: the notebook road, or the session link while it is down */

    char b[12];
    unsigned int onU = on ? 1u : 0u;
    std::memcpy(b,     &uid,       4);
    std::memcpy(b + 4, &onU,       4);
    std::memcpy(b + 8, &targetUid, 4);
    return CharSend(MSG_COMBATMODE, uid, b, sizeof(b), CH_RELIABLE, -1);   /* M7a */
}

// F348. RELIABLE for the same reason COMBATMODE is: a swing is an edge and there is no later
// message that repairs a lost one - the blow simply never appears on the peer's screen, which is
// the defect this exists to close. Small and infrequent (roughly one per blow per fighter), so
// the reliable channel costs nothing worth saving here.
/* ==== M7a A1 build 1 [a1b1-sp27] (design 1.2, 1.3, 2.6, 3.1-3.16) - THE ROAD TO ONE GAME, RECEIPTS AND THE OUTBOX, THE ROSTER ==== */
static bool SessionUpNow() { return g_transport != 0 && g_transport->State() == LINK_UP; }
static int g_dispCameBySession = 0;   /* fold 1 [a1b1f1-sp1] [F1]: 1 while SessionDispatchQueued runs a handler for a message that came on the session link */
static cooplo::Road RoadToSlot(int slot) { return cooplo::WorldFirstRoute(slot, coop::StoreLiveReady(), slot >= 0 ? coop::StoreRosterSlotInWorld(slot) : -1, SessionUpNow(), coop::LinkPeerSlot()); }
static long long g_toSlotLive = 0, g_toSlotSession = 0, g_toSlotNone = 0;
/* WorldFirstRoute (3.1): LIVE SLOT whenever this game's notebook link is ready and the target is IN_WORLD; the session link only while the
   notebook link is down and the target is the session peer; else not sent (false - the caller's recurring tick tries again). */
static bool SendToSlot(int slot, MsgType type, const std::vector<char>& b)
{
    if (b.empty()) return false;
    const cooplo::Road r = RoadToSlot(slot);
    if (r.road == cooplo::kRoadLive && coop::StoreSendLive(cooplive::kRouteSlot, (unsigned int)r.slot, (unsigned int)type, b, true)) { ++g_toSlotLive; return true; }
    if (r.road == cooplo::kRoadSession && g_transport->Send(0, type, &b[0], b.size(), CH_RELIABLE)) { ++g_toSlotSession; return true; }
    ++g_toSlotNone;
    return false;
}

/* ---- receipts (2.6): every UNLOAD / DESPAWN this game sends is a row until each expected game acknowledged it, its CHECK no longer
   lists the person (rosterClosed), it left the world (slotGone) or 60 s of road-up time passed (gaveUp). Resends go by SLOT to each
   game that has not acknowledged, never WORLD again [review b]. expected = acked + rosterClosed + slotGone + gaveUp [review F12]. ---- */
struct OutSlot { int end; double lastAt; int sends; int rosterClosed; OutSlot() : end(-1), lastAt(0.0), sends(0), rosterClosed(0) {} };
struct OutRow { unsigned int uid, gen, why; int type, sectorKey; bool expectKnown; std::map<int, OutSlot> slots; double roadUpSec; OutRow() : uid(0), gen(0), why(0), type(0), sectorKey(-1), expectKnown(false), roadUpSec(0.0) {} };
static std::map<unsigned int, OutRow> g_outbox;   /* seq -> row */
static unsigned int g_wdSeq = 0;
static long long g_rcSent = 0, g_rcExpected = 0, g_rcAcked = 0, g_rcRosterClosed = 0, g_rcSlotGone = 0, g_rcResent = 0, g_rcGaveUp = 0, g_rcUnexpected = 0, g_rcOut = 0, g_rcMalformed = 0;
/* the expected receivers (3.13): IN_WORLD games whose published player stands within 3 sectors; false = no roster / table yet */
static bool ReceiptExpectSlots(int sectorKey, std::vector<int>* out)
{
    out->clear();
    if (!coop::StoreLiveReady()) return false;
    if (sectorKey < 0 || sectorKey >= cooplive::kAreaKeyCount) return true;   /* no sector: no receipt - the ROSTER is the repair */
    int sl[256], xs[256], ys[256];
    const int n = coop::PeerPlayerSectorsTS(sl, xs, ys, 256);
    if (n < 0) return false;
    const int me = coop::StoreMySlot();
    const int sx = cooplive::AreaKeyX(sectorKey), sy = cooplive::AreaKeyY(sectorKey);
    for (int i = 0; i < n; ++i)
        if (sl[i] != me && cooplo::ReceiptExpected(coop::StoreRosterSlotInWorld(sl[i]), 1, cooplo::Cheb(xs[i], ys[i], sx, sy), cooplo::kReceiptRadius)) out->push_back(sl[i]);
    return true;
}
static void OutRowExpect(OutRow* r, const std::vector<int>& e, double lastAt, int sends)
{
    r->expectKnown = true;
    for (size_t i = 0; i < e.size(); ++i) { OutSlot o; o.lastAt = lastAt; o.sends = sends; r->slots[e[i]] = o; }
    g_rcExpected += (long long)e.size();
}
static bool SendWithdraw(MsgType type, unsigned int uid, unsigned int why)
{
    if (!CharRoadOpen()) return false;   /* M7a: the notebook road, or the session link while it is down - a refused road is the caller's, as before */
    std::map<unsigned int, int>::const_iterator la = g_charLastArea.find(uid);
    const int key = la != g_charLastArea.end() ? la->second : -1;   /* the sector its last AREA send named - read BEFORE CharSend forgets it; no engine read (a DESPAWN runs inside GameWorld::destroy) */
    cooplo::WithdrawMsg w; w.uid = uid; w.why = why;   /* why an UNLOAD was sent (cooplo::kWdWhy*): the receiver's squad index reads it */
    if (++g_wdSeq == 0) ++g_wdSeq;
    w.seq = g_wdSeq;
    w.gen = g_localOwned.find(uid) != g_localOwned.end() ? MineGenOf(uid) : CopyGenHere(uid);
    w.sectorKey = key >= 0 ? (unsigned int)key : cooplo::kNoSector;
    std::vector<int> expect;
    const bool known = ReceiptExpectSlots(key, &expect);
    if (expect.size() > cooplo::kWithdrawExpectMax) expect.resize(cooplo::kWithdrawExpectMax);
    w.expect = expect;
    std::vector<char> b;
    if (!cooplo::WithdrawEncode(&b, w)) return false;
    if (!CharSend(type, uid, &b[0], b.size(), CH_RELIABLE, -1)) return false;   /* M7a: route WORLD on the notebook road */
    ++g_rcSent;
    OutRow r; r.uid = uid; r.gen = w.gen; r.why = w.why; r.type = (int)type; r.sectorKey = key;
    if (known) OutRowExpect(&r, expect, CuNowSec(), 1);
    if (!known || !r.slots.empty()) g_outbox[w.seq] = r;   /* no roster yet: the expected set is computed at the first tick that has one */
    return true;
}
static void OnReceipt(const Message& m)
{
    std::vector<unsigned int> seqs;
    const int from = PeerSlotOfKey(m.peer);
    if (from < 0 || m.payload.empty() || !cooplo::ReceiptDecode(&m.payload[0], m.payload.size(), &seqs))
    {
        ++g_rcMalformed;
        if (cooplive::LiveLogThis(g_rcMalformed)) ErrorLog("[net] malformed RECEIPT (" + N((long long)m.payload.size()) + " bytes, sender slot " + N((long long)from) + ") - ignored (receipt malformed " + N(g_rcMalformed) + ")");
        return;
    }
    for (size_t i = 0; i < seqs.size(); ++i)
    {
        std::map<unsigned int, OutRow>::iterator it = g_outbox.find(seqs[i]);
        std::map<int, OutSlot>::iterator sl;
        if (it == g_outbox.end() || (sl = it->second.slots.find(from)) == it->second.slots.end() || sl->second.end >= 0) { ++g_rcUnexpected; continue; }
        sl->second.end = cooplo::kRcDoneAcked; ++g_rcAcked;
    }
}
/* one RECEIPT per sender per SessionTick, by SLOT; a receipt that cannot go now is dropped - the sender's resend asks again */
static void ReceiptFlush()
{
    if (g_receiptOut.empty()) return;
    for (std::map<unsigned int, std::vector<unsigned int> >::const_iterator it = g_receiptOut.begin(); it != g_receiptOut.end(); ++it)
    {
        const int slot = PeerSlotOfKey(it->first);
        for (size_t at = 0; at < it->second.size(); at += cooplo::kReceiptMaxSeqs)
        {
            const size_t to = (at + cooplo::kReceiptMaxSeqs < it->second.size()) ? at + cooplo::kReceiptMaxSeqs : it->second.size();
            std::vector<char> b;
            if (cooplo::ReceiptEncode(&b, std::vector<unsigned int>(it->second.begin() + (long)at, it->second.begin() + (long)to)) && SendToSlot(slot, MSG_RECEIPT, b)) ++g_rcOut;
        }
    }
    g_receiptOut.clear();
}
static void OutboxTick(double now, double dt)
{
    const bool live = coop::StoreLiveReady();
    for (std::map<unsigned int, OutRow>::iterator it = g_outbox.begin(); it != g_outbox.end(); )
    {
        OutRow& r = it->second;
        if (!r.expectKnown) { std::vector<int> e; if (ReceiptExpectSlots(r.sectorKey, &e)) OutRowExpect(&r, e, 0.0, 0); }
        if (live) r.roadUpSec += dt;   /* no clock runs while there is no road (owner 334 a) */
        int open = 0;
        for (std::map<int, OutSlot>::iterator sl = r.slots.begin(); sl != r.slots.end(); ++sl)
        {
            OutSlot& o = sl->second;
            if (o.end >= 0) continue;
            const int gone = coop::StoreRosterSlotInWorld(sl->first) == 0 ? 1 : 0;
            const int act = cooplo::ReceiptAction(now - o.lastAt, o.sends, RoadToSlot(sl->first).road != cooplo::kRoadNone ? 1 : 0, r.roadUpSec, 0, o.rosterClosed, gone);
            if (act == cooplo::kRcWait) { ++open; continue; }
            if (act == cooplo::kRcResend)
            {
                cooplo::WithdrawMsg w; w.uid = r.uid; w.seq = it->first; w.gen = r.gen; w.sectorKey = r.sectorKey >= 0 ? (unsigned int)r.sectorKey : cooplo::kNoSector; w.why = r.why; w.expect.push_back(sl->first);
                std::vector<char> b;
                if (cooplo::WithdrawEncode(&b, w) && SendToSlot(sl->first, (MsgType)r.type, b)) { ++o.sends; ++g_rcResent; }
                o.lastAt = now; ++open;
                continue;
            }
            o.end = act;
            if (act == cooplo::kRcDoneRoster) ++g_rcRosterClosed;
            else if (act == cooplo::kRcDoneGone) ++g_rcSlotGone;
            else if (act == cooplo::kRcGiveUp)
            {
                ++g_rcGaveUp;
                if (cooplive::LiveLogThis(g_rcGaveUp))
                    DebugLog(std::string("[net] RECEIPT GIVEN UP: ") + MsgTypeName((MsgType)r.type) + " uid=" + N(r.uid) + " seq=" + N((long long)it->first) + " to slot " + N((long long)sl->first)
                             + " - no acknowledgement in " + N((long long)cooplo::kReceiptGiveUpSec) + " s of road-up time after " + N((long long)o.sends) + " sends; that game's next CHECK still withdraws the copy (receipt gaveUp " + N(g_rcGaveUp) + ")");
            }
        }
        if (r.expectKnown && open == 0) g_outbox.erase(it++); else ++it;
    }
    /* the OWNER_MOVED words owed while the world link was down (M7a2 fold 1 item 1) - from here since A1 build 1 [a1b1-sp26] */
    if (!g_ownerMovedOwed.empty() && coop::StoreLiveReady())
    {
        std::map<unsigned int, unsigned int> owed; owed.swap(g_ownerMovedOwed);   /* fold 2 [m7a2f2-sp5]: a send failing again re-queues into the fresh map */
        long long late = 0, dropped = 0;
        for (std::map<unsigned int, unsigned int>::const_iterator it = owed.begin(); it != owed.end(); ++it)
        {
            const bool taker = it->second != kOwedRelease;   /* the taker's word is owed only while this game still runs it */
            const bool mineNow = IsUidMine(it->first);
            if (cooplive::OwnerMovedOwedDecide(true, taker ? !mineNow : mineNow, g_owner.find(it->first) != g_owner.end()) != cooplive::kOwedSend) { ++dropped; continue; }
            if (taker ? AnnounceSendNow(it->first, it->second) : OwnerMovedSendNow(it->first)) ++late;
        }
        g_ownerMovedSentLate += late; g_ownerMovedOwedDropped += dropped;
        DebugLog("[net] OWNER_MOVED: the world link is ready - " + N(late) + " of " + N((long long)owed.size()) + " hand-overs owed while it was down sent now, "
                 + N(dropped) + " no longer owed (ownerMoved deferred " + N(g_ownerMovedDeferred) + ", sentLate " + N(g_ownerMovedSentLate) + ")");
    }
}

/* ---- the ROSTER (1.3, 3.5-3.9, 3.16) ---- */
static long long g_roHashSent = 0, g_roHashRecv = 0, g_roChecksSent = 0, g_roChecksRecv = 0, g_roAnswersSent = 0, g_roRekeyed = 0, g_roRestamped = 0, g_roWithdrawn = 0,
                 g_roConflicts = 0, g_roYielded = 0, g_roOrphanWithdrawn = 0, g_roPendingAnswered = 0, g_roWelcomeChecks = 0, g_roMalformed = 0, g_roAnswerUnknown = 0,
                 g_roDualAsked = 0, g_roDualKept = 0, g_roPendingKept = 0;
static unsigned int g_roSeq = 0, g_roCheckNo = 0;
static double g_roLastHashAt = -1.0;
static long g_roHashGen = -1;
struct RoOwnerView
{
    std::map<int, std::pair<unsigned int, unsigned long long> > hash, pend;   /* sector -> (count, hash): the latest complete HASH / the one arriving */
    unsigned int pendSeq; double hashAt, lastCheckAt, lastFullAt; long edgeGen; std::set<unsigned int> recheck;
    RoOwnerView() : pendSeq(0), hashAt(0.0), lastCheckAt(-1.0e9), lastFullAt(-1.0e9), edgeGen(-1) {}
};
static std::map<int, RoOwnerView> g_roView;   /* owner slot -> its latest HASH and this game's CHECK clocks and re-checks */
struct RoCheckOut { int slot; double sentAt; RoCheckOut() : slot(-1), sentAt(0.0) {} };
static std::map<unsigned int, RoCheckOut> g_roChecks;   /* checkNo -> where it went; forgotten after 60 s */
static std::map<unsigned int, int> g_roConflictCount;   /* uid -> re-checks of one disagreement so far (3.8 bounded exit) */
static std::set<unsigned int> g_rtOwnAsked;   /* the people this game runs that its return check listed to the other in-world games (ReturnOwnList) */
static long long g_rtOwnListed = 0, g_rtOwnYielded = 0, g_rtOwnKept = 0;
static std::map<unsigned int, double> g_roLastStreamAt;   /* uid -> the last character-stream message for it (a NOT-LIVE after it is not taken) */
static std::map<unsigned long long, std::set<unsigned int> > g_roCheckIn;   /* (asker slot << 32 | checkNo) -> the uids its chunks listed so far */
static std::map<unsigned int, double> g_roOrphanSec;   /* 3.16: copy uid -> link-up seconds with no owner record */
static void RosterStreamSeen(unsigned int uid) { g_roLastStreamAt[uid] = CuNowSec(); }
void RosterStreamForget(unsigned int uid) { g_roLastStreamAt.erase(uid); }   /* [a1b1f1-sp0] [F9] */
void RosterStreamForgetAll() { g_roLastStreamAt.clear(); }
static void DualRunNote(unsigned int uid, unsigned int peer)
{
    const int slot = PeerSlotOfKey(peer);
    if (slot < 0) return;
    if (g_roView[slot].recheck.insert(uid).second) ++g_roDualAsked;
}
/* the owner: its LIVE people per sector, WORLD, every 5 s and at each notebook WELCOME edge */
static void RosterOwnerTick(double now)
{
    if (!coop::StoreLiveReady() || coop::StoreMySlot() < 0 || !coop::GameplayRunning() || coop::EngineWritesBlocked()) return;   /* no world, no bodies to read */
    const long gen = coop::StoreLiveGen();
    if (gen == g_roHashGen && g_roLastHashAt >= 0.0 && now - g_roLastHashAt < cooplo::kRosterSec && now >= g_roLastHashAt) return;
    g_roHashGen = gen; g_roLastHashAt = now;
    std::map<int, cooplo::HashRow> by;
    for (std::set<unsigned int>::const_iterator it = g_localOwned.begin(); it != g_localOwned.end(); ++it)
    {
        const int k = coop::CharAreaKeyNow(*it);   /* no body here (put away) = not live = not listed */
        if (k < 0) continue;
        cooplo::HashRow& h = by[k]; h.sectorKey = (unsigned int)k; ++h.count; h.hash ^= cooplo::RosterRowHash(*it, MineGenOf(*it));
    }
    std::vector<cooplo::HashRow> rows;
    for (std::map<int, cooplo::HashRow>::const_iterator it = by.begin(); it != by.end(); ++it) rows.push_back(it->second);
    if (++g_roSeq == 0) ++g_roSeq;
    std::vector<std::vector<char> > chunks;
    if (!cooplo::RosterHashEncode(&chunks, g_roSeq, rows)) return;
    for (size_t i = 0; i < chunks.size(); ++i) if (coop::StoreSendLive(cooplive::kRouteWorld, 0, (unsigned int)MSG_ROSTER, chunks[i], true)) ++g_roHashSent;
}
static bool RosterSendCheck(int slot, unsigned int sectorKey, const std::vector<cooplo::CheckRow>& rows, double now)
{
    if (++g_roCheckNo == 0) ++g_roCheckNo;
    std::vector<std::vector<char> > chunks;
    if (!cooplo::RosterCheckEncode(&chunks, g_roCheckNo, sectorKey, rows)) return false;
    for (size_t i = 0; i < chunks.size(); ++i) if (!SendToSlot(slot, MSG_ROSTER, chunks[i])) return false;
    RoCheckOut c; c.slot = slot; c.sentAt = now; g_roChecks[g_roCheckNo] = c;
    ++g_roChecksSent;
    return true;
}
/* the copy holder: per owner, CHECK the sectors near this game whose hash differs, every copy at an edge or every 30 s, and the re-checks */
static void RosterHolderTick(double now)
{
    if (!coop::StoreLiveReady() || coop::StoreMySlot() < 0 || !coop::GameplayRunning() || coop::EngineWritesBlocked()) return;   /* no world, no copies to read */
    const int me = coop::StoreMySlot();
    const long gen = coop::StoreLiveGen();
    int sl[256], xs[256], ys[256]; int px = -1, py = -1;
    const int n = coop::PeerPlayerSectorsTS(sl, xs, ys, 256);
    for (int i = 0; i < n; ++i) if (sl[i] == me) { px = xs[i]; py = ys[i]; }
    std::map<int, std::vector<cooplo::CheckRow> > all;
    std::map<int, std::map<int, std::vector<cooplo::CheckRow> > > bySector;
    std::map<int, std::map<int, std::pair<unsigned int, unsigned long long> > > mineHash;
    std::map<int, int> needSec;   /* fold 1 [a1b1f1-sp6] [F4]: owner slot -> are its per-sector hashes rebuilt this tick (cooplo::RosterSectorsNeeded) */
    for (std::map<unsigned int, unsigned int>::const_iterator it = g_owner.begin(); it != g_owner.end(); ++it)
    {
        if (g_localOwned.find(it->first) != g_localOwned.end()) continue;
        const int slot = PeerSlotOfKey(it->second);
        if (slot < 0 || slot == me) continue;   /* an owner with no slot cannot be asked */
        cooplo::CheckRow r; r.uid = it->first; r.gen = CopyGenHere(it->first);
        all[slot].push_back(r);
        std::map<int, int>::iterator ns = needSec.find(slot);
        if (ns == needSec.end())
        {
            std::map<int, RoOwnerView>::const_iterator vv = g_roView.find(slot);
            const int nd = vv == g_roView.end() ? 1 : cooplo::RosterSectorsNeeded(now - vv->second.lastCheckAt, vv->second.edgeGen != gen ? 1 : 0, vv->second.recheck.empty() ? 0 : 1, cooplo::kRosterSec);
            ns = needSec.insert(std::make_pair(slot, nd)).first;
        }
        if (ns->second == 0) continue;   /* [F4]: no CHECK of that owner can be due - no engine position read */
        const int k = coop::CharAreaKeyNow(it->first);
        if (k < 0) continue;
        bySector[slot][k].push_back(r);
        std::pair<unsigned int, unsigned long long>& h = mineHash[slot][k]; ++h.first; h.second ^= cooplo::RosterRowHash(r.uid, r.gen);
    }
    std::set<int> owners;
    for (std::map<int, std::vector<cooplo::CheckRow> >::const_iterator it = all.begin(); it != all.end(); ++it) owners.insert(it->first);
    for (std::map<int, RoOwnerView>::const_iterator it = g_roView.begin(); it != g_roView.end(); ++it) if (!it->second.recheck.empty()) owners.insert(it->first);
    for (std::set<int>::const_iterator o = owners.begin(); o != owners.end(); ++o)
    {
        RoOwnerView& v = g_roView[*o];
        const int edge = v.edgeGen != gen ? 1 : 0;
        std::set<int> nearSecs, diff;
        if (px >= 0 && v.hashAt > 0.0)
        {
            for (std::map<int, std::pair<unsigned int, unsigned long long> >::const_iterator h = mineHash[*o].begin(); h != mineHash[*o].end(); ++h)
                if (cooplo::Cheb(cooplive::AreaKeyX(h->first), cooplive::AreaKeyY(h->first), px, py) <= 2) nearSecs.insert(h->first);
            for (std::map<int, std::pair<unsigned int, unsigned long long> >::const_iterator h = v.hash.begin(); h != v.hash.end(); ++h)
                if (cooplo::Cheb(cooplive::AreaKeyX(h->first), cooplive::AreaKeyY(h->first), px, py) <= 2) nearSecs.insert(h->first);
            for (std::set<int>::const_iterator k = nearSecs.begin(); k != nearSecs.end(); ++k)
            {
                std::map<int, std::pair<unsigned int, unsigned long long> >::const_iterator a = mineHash[*o].find(*k), b = v.hash.find(*k);
                const std::pair<unsigned int, unsigned long long> mh = a == mineHash[*o].end() ? std::make_pair(0u, 0ULL) : a->second;
                const std::pair<unsigned int, unsigned long long> oh = b == v.hash.end() ? std::make_pair(0u, 0ULL) : b->second;
                if (mh != oh && mh.first != 0) diff.insert(*k);   /* only sectors where this game holds copies: an owner's person missing here is the catch-up's */
            }
        }
        const int copies = (int)all[*o].size();
        if (!cooplo::RosterCheckDue(copies, diff.empty() ? 0 : 1, now - v.lastCheckAt, edge && copies > 0, v.recheck.empty() ? 0 : 1, cooplo::kRosterSec, cooplo::kRosterQuietSec)) continue;
        bool sent = false;
        if (copies > 0 && cooplo::RosterCheckIsFull(edge, now - v.lastFullAt, cooplo::kRosterQuietSec))
        {
            if (RosterSendCheck(*o, cooplo::kRosterAllSectors, all[*o], now)) { sent = true; v.lastFullAt = now; if (edge) ++g_roWelcomeChecks; }
        }
        else for (std::set<int>::const_iterator k = diff.begin(); k != diff.end(); ++k) if (RosterSendCheck(*o, (unsigned int)*k, bySector[*o][*k], now)) sent = true;
        if (!v.recheck.empty())
        {
            std::vector<cooplo::CheckRow> rows;
            for (std::set<unsigned int>::const_iterator u = v.recheck.begin(); u != v.recheck.end(); ++u)
            { cooplo::CheckRow r; r.uid = *u; r.gen = g_localOwned.find(*u) != g_localOwned.end() ? MineGenOf(*u) : CopyGenHere(*u); rows.push_back(r); }
            if (RosterSendCheck(*o, cooplo::kRosterListed, rows, now)) { sent = true; v.recheck.clear(); }
        }
        if (sent) { v.lastCheckAt = now; if (copies > 0 || edge) v.edgeGen = gen; }
    }
    for (std::map<unsigned int, RoCheckOut>::iterator c = g_roChecks.begin(); c != g_roChecks.end(); ) { if (now - c->second.sentAt > 60.0) g_roChecks.erase(c++); else ++c; }
}
/* 3.16 [review a]: a copy (a puppet here) with no owner record is kept while its owner may re-claim it, withdrawn after 20 s of
   notebook-link-up time with engine writes allowed. Engine writes: SessionCatchupApplyTick, after the drain. */
static void RosterOrphanTick(double dt)
{
    if (!coop::StoreLiveReady() || !coop::GameplayRunning()) return;
    const bool blocked = coop::EngineWritesBlocked();
    std::vector<unsigned int> pu; coop::PuppetUidsSnapshot(&pu);
    std::set<unsigned int> seen;
    for (size_t i = 0; i < pu.size(); ++i)
    {
        const unsigned int uid = pu[i];
        if (g_localOwned.find(uid) != g_localOwned.end()) continue;
        seen.insert(uid);
        const int has = g_owner.find(uid) != g_owner.end() ? 1 : 0;
        if (has) { g_roOrphanSec.erase(uid); continue; }
        double& t = g_roOrphanSec[uid];
        if (!blocked) t += dt;
        if (cooplo::OrphanCopyAction(has, t, blocked ? 1 : 0, cooplo::kOrphanCopySec) != cooplo::kOrphanWithdraw) continue;
        coop::ApplyRemoteUnload(uid); ++g_roOrphanWithdrawn; g_roOrphanSec.erase(uid);
        if (cooplive::LiveLogThis(g_roOrphanWithdrawn))
            DebugLog("[net] ROSTER: copy uid=" + N(uid) + " WITHDRAWN - no owner record for " + N((long long)cooplo::kOrphanCopySec) + " s of world-link time (no owner re-claimed it; roster orphanWithdrawn "
                     + N(g_roOrphanWithdrawn) + "; the first 5 and every 100th are logged)");
    }
    for (std::map<unsigned int, double>::iterator it = g_roOrphanSec.begin(); it != g_roOrphanSec.end(); ) { if (seen.count(it->first) == 0) g_roOrphanSec.erase(it++); else ++it; }
}
/* THE RETURN CHECK'S OWN PEOPLE (lostcopy.h ReturnOwnYield). At this game's return to the world link it listed the NPCs it runs here
   to the other in-world games (ReturnOwnList): one may have been taken while this game was away (a final leave's take-over). Only a
   LIVE answer gives one up - the dual-run rule in RosterApplyAnswer, another game running it now at a higher generation; a NOT-LIVE
   or MOVED answer never does. A listed person's first answer ends its listing (counted kept or yielded), so a later dual-run re-check
   of it is the roster's own. */
static void RosterOwnReturnNote(unsigned int uid, int from, int gaveUp, unsigned int gen)
{
    std::set<unsigned int>::iterator it = g_rtOwnAsked.find(uid);
    if (it == g_rtOwnAsked.end()) return;
    g_rtOwnAsked.erase(it);
    if (!gaveUp) { ++g_rtOwnKept; return; }
    ++g_rtOwnYielded;
    if (g_rtOwnYielded <= 40 || cooplive::LiveLogThis(g_rtOwnYielded))
        DebugLog("[net] RETURN CHECK: person uid=" + N(uid) + " this game ran is GIVEN UP to slot " + N((long long)from) + " - that game answers it runs it now, at generation "
                 + N((long long)gen) + " (the dual-run rule): it was taken while this game was away (returnOwn yielded " + N(g_rtOwnYielded) + "; the first 40 are logged)");
}
static void RosterApplyAnswer(int from, double checkAt, const cooplo::AnswerRow& a)
{
    const int me = coop::StoreMySlot();
    if (!cooplo::AnswerSlotOk(a.status, a.slot, (unsigned int)cooplive::kLiveSlotMax)) { ++g_roMalformed; return; }   /* fold 1 [a1b1f1-sp8] [F10]: a MOVED row naming no live slot */
    if (g_localOwned.find(a.uid) != g_localOwned.end())   /* 3.9: a dual run - this game runs it too */
    {
        const int live = a.status == cooplo::kAnsLive ? 1 : 0;
        const int yields = (live && cooplo::DualRunResolve(MineGenOf(a.uid), a.gen, me, from) == cooplo::kIYield) ? 1 : 0;
        if (lostcopy::ReturnOwnYield(live, yields) == 0)   /* only a LIVE answer at a higher generation gives up a person this game runs */
        {
            if (live) ++g_roDualKept;
            RosterOwnReturnNote(a.uid, from, 0, a.gen);
            return;
        }
        if (coop::EngineWritesBlocked()) return;
        ReleaseLocalOwner(a.uid, cooplive::RelayPeerId((unsigned int)from), a.gen);
        coop::WorldStateOnOwnershipReleased(a.uid, 0);
        coop::AdoptRemotePuppet(a.uid);
        ++g_roYielded;
        DebugLog("[net] ROSTER: DUAL RUN of uid=" + N(a.uid) + " with slot " + N((long long)from) + " - its gen " + N((long long)a.gen) + " out-ranks this game's: this game YIELDS (the body becomes slot "
                 + N((long long)from) + "'s puppet; roster yielded " + N(g_roYielded) + ")");
        RosterOwnReturnNote(a.uid, from, 1, a.gen);
        return;
    }
    std::map<unsigned int, unsigned int>::iterator own = g_owner.find(a.uid);
    const int recorded = (own != g_owner.end() && OwnerMatch(own->second, cooplive::RelayPeerId((unsigned int)from))) ? 1 : 0;
    std::map<unsigned int, double>::const_iterator st = g_roLastStreamAt.find(a.uid);
    const int streamSince = (st != g_roLastStreamAt.end() && st->second > checkAt) ? 1 : 0;
    std::map<unsigned int, int>::const_iterator cc = g_roConflictCount.find(a.uid);
    const int act = cooplo::RosterApply(CopyGenHere(a.uid), a.status, a.gen, (int)a.slot == me ? 1 : 0, recorded, streamSince, cc == g_roConflictCount.end() ? 0 : cc->second);
    if (act == cooplo::kApRestamp) { g_copyGen[a.uid] = a.gen; ++g_roRestamped; g_roConflictCount.erase(a.uid); return; }
    if (act == cooplo::kApRekey)
    {
        const int to = a.status == cooplo::kAnsMoved ? (int)a.slot : from;
        g_owner[a.uid] = OwnerKeyOf(cooplive::RelayPeerId((unsigned int)to)); g_copyGen[a.uid] = a.gen; ++g_roRekeyed; g_roConflictCount.erase(a.uid);
        if (cooplive::LiveLogThis(g_roRekeyed)) DebugLog("[net] ROSTER: copy uid=" + N(a.uid) + " re-keyed to slot " + N((long long)to) + " at gen " + N((long long)a.gen) + " (word of slot " + N((long long)from) + "; roster rekeyed " + N(g_roRekeyed) + ")");
        return;
    }
    if (act == cooplo::kApWithdraw)
    {
        if (coop::EngineWritesBlocked()) return;   /* the next CHECK asks again */
        if (recorded) coop::SquadIdxOwnerWithdrew(a.uid, 1);   /* M7a A1 build 2 [a1b2-sp3] [review F9]: the recorded owner runs it nowhere - out of the squad index */
        coop::ApplyRemoteUnload(a.uid); g_owner.erase(a.uid); g_copyGen.erase(a.uid); g_roLastStreamAt.erase(a.uid); /* [F9] */ g_roConflictCount.erase(a.uid); ++g_roWithdrawn;
        if (cooplive::LiveLogThis(g_roWithdrawn) || g_roWithdrawn <= 50)
            DebugLog("[net] ROSTER: copy uid=" + N(a.uid) + " WITHDRAWN - slot " + N((long long)from) + (recorded ? " (its recorded owner)" : "") + " answered NOT-LIVE at gen " + N((long long)a.gen)
                     + " (roster withdrawn " + N(g_roWithdrawn) + "; the first 50, then every 100th are logged)");
        return;
    }
    if (act == cooplo::kApConflict)
    {
        ++g_roConflicts; ++g_roConflictCount[a.uid];
        const int to = (a.status == cooplo::kAnsMoved && (int)a.slot != me && a.slot != 0xFFFF) ? (int)a.slot : from;
        g_roView[to].recheck.insert(a.uid);
        return;
    }
    if (a.status == cooplo::kAnsPending) ++g_roPendingKept;
}
static void OnRoster(const Message& m)
{
    cooplo::RosterMsg r;
    const int from = PeerSlotOfKey(m.peer);
    if (from < 0 || m.payload.empty() || !cooplo::RosterDecode(&m.payload[0], m.payload.size(), &r))
    {
        ++g_roMalformed;
        if (cooplive::LiveLogThis(g_roMalformed)) ErrorLog("[net] malformed ROSTER (" + N((long long)m.payload.size()) + " bytes, sender slot " + N((long long)from) + ") - ignored (roster malformed " + N(g_roMalformed) + ")");
        return;
    }
    const double now = CuNowSec();
    if (r.kind == cooplo::kRosterHash)
    {
        RoOwnerView& v = g_roView[from];
        if (r.chunk == 0) { v.pend.clear(); v.pendSeq = r.seq; }
        else if (r.seq != v.pendSeq) return;
        for (size_t i = 0; i < r.hashes.size(); ++i) v.pend[(int)r.hashes[i].sectorKey] = std::make_pair(r.hashes[i].count, r.hashes[i].hash);
        if (r.chunk == r.lastChunk) { v.hash.swap(v.pend); v.pend.clear(); v.hashAt = now; ++g_roHashRecv; }
        return;
    }
    if (r.kind == cooplo::kRosterCheck)
    {
        ++g_roChecksRecv;
        std::vector<cooplo::AnswerRow> rows;
        const unsigned long long ck = ((unsigned long long)(unsigned int)from << 32) | r.seq;
        std::set<unsigned int>& listed = g_roCheckIn[ck];
        for (size_t i = 0; i < r.checks.size(); ++i)
        {
            const unsigned int uid = r.checks[i].uid;
            listed.insert(uid);
            const bool mine = g_localOwned.find(uid) != g_localOwned.end();
            const int live = (mine && coop::CharAreaKeyNow(uid) >= 0) ? 1 : 0;
            /* build 2 [review F1]: an open RELEASE / this frame's release batch / an XFER awaiting its ACK - PENDING; [a1b1-sp39]: and a body this
               game still has registered but could not read now (never a withdrawal on an unreadable read - only a put-away body is NOT-LIVE) */
            const int pending = (mine && (coop::HandoffRosterPending(uid) || (live == 0 && coop::FindSpawned(uid) != 0))) ? 1 : 0;
            int recSlot = -1; unsigned int recGen = 0;
            if (!mine) { std::map<unsigned int, unsigned int>::const_iterator o = g_owner.find(uid); if (o != g_owner.end()) { recSlot = PeerSlotOfKey(o->second); recGen = CopyGenHere(uid); } }
            cooplo::Answer a = cooplo::RosterAnswerFor(live, MineGenOf(uid), pending, recSlot, recGen, r.checks[i].gen);
            if (a.status == cooplo::kAnsAgree && live && r.sectorKey == cooplo::kRosterListed) { a.status = cooplo::kAnsLive; a.gen = MineGenOf(uid); }   /* a dual-run / re-check row is always answered */
            if (a.status == cooplo::kAnsAgree) continue;
            if (a.status == cooplo::kAnsPending) ++g_roPendingAnswered;
            cooplo::AnswerRow row; row.uid = uid; row.status = a.status; row.slot = a.slot < 0 ? 0xFFFFu : (unsigned int)a.slot; row.gen = a.gen; rows.push_back(row);
        }
        std::vector<std::vector<char> > chunks;
        if (cooplo::RosterAnswerEncode(&chunks, r.seq, g_roSeq, rows))
        {
            bool all = true;
            for (size_t i = 0; i < chunks.size(); ++i) if (!SendToSlot(from, MSG_ROSTER, chunks[i])) all = false;
            if (all) ++g_roAnswersSent;
        }
        if (r.chunk == r.lastChunk)   /* the roster shows the result (2.6): a withdrawal this game owes that game a receipt for is closed when its FULL CHECK no longer lists the person (fold 1 [a1b1f1-sp7] [F8]: a per-sector CHECK lists only that sector's copies) */
        {
            for (std::map<unsigned int, OutRow>::iterator it = g_outbox.begin(); it != g_outbox.end(); ++it)
            {
                std::map<int, OutSlot>::iterator sl = it->second.slots.find(from);
                if (sl == it->second.slots.end() || sl->second.end >= 0) continue;
                if (cooplo::ReceiptClosedByCheck((unsigned int)r.sectorKey, listed.count(it->second.uid) != 0 ? 1 : 0)) sl->second.rosterClosed = 1;
            }
            g_roCheckIn.erase(ck);
        }
        if (g_roCheckIn.size() > 4096) g_roCheckIn.clear();   /* chunks of checks whose last chunk never came */
        return;
    }
    std::map<unsigned int, RoCheckOut>::const_iterator c = g_roChecks.find(r.seq);
    if (c == g_roChecks.end() || c->second.slot != from) { ++g_roAnswerUnknown; return; }
    const double checkAt = c->second.sentAt;
    for (size_t i = 0; i < r.answers.size(); ++i) RosterApplyAnswer(from, checkAt, r.answers[i]);
}
/* SessionTick: the owner's HASH, the holder's CHECKs, the outbox and the receipts owed (plugin state and sends only) */
static void LiveOwnerTick()
{
    static double s_last = -1.0;
    const double now = CuNowSec();
    RosterOwnerTick(now);
    ReceiptFlush();
    if (s_last >= 0.0 && now - s_last < 1.0 && now >= s_last) return;
    double dt = s_last < 0.0 ? 0.0 : now - s_last;
    if (dt < 0.0 || dt > 5.0) dt = 1.0;
    s_last = now;
    RosterHolderTick(now);
    OutboxTick(now, dt);
}
/* SessionCatchupApplyTick (after the drain): the orphan-copy rule's withdrawals */
static void LiveOwnerApplyTick()
{
    static double s_last = -1.0;
    const double now = CuNowSec();
    if (s_last >= 0.0 && now - s_last < 1.0 && now >= s_last) return;
    double dt = s_last < 0.0 ? 0.0 : now - s_last;
    if (dt < 0.0 || dt > 5.0) dt = 1.0;
    s_last = now;
    RosterOrphanTick(dt);
}
static long long g_xferNoSlot = 0, g_leadLive = 0, g_leadLiveFailed = 0;
std::string LiveOwnerCountsString()
{
    return " roster[hashSent,hashRecv,checksSent,checksRecv,answersSent,rekeyed,restamped,withdrawn,conflicts,yielded,orphanWithdrawn,pendingAnswered,welcomeChecks,pendingKept,dualAsked,dualKept,answerUnknown,malformed]="
        + N(g_roHashSent) + "," + N(g_roHashRecv) + "," + N(g_roChecksSent) + "," + N(g_roChecksRecv) + "," + N(g_roAnswersSent) + "," + N(g_roRekeyed) + "," + N(g_roRestamped)
        + "," + N(g_roWithdrawn) + "," + N(g_roConflicts) + "," + N(g_roYielded) + "," + N(g_roOrphanWithdrawn) + "," + N(g_roPendingAnswered) + "," + N(g_roWelcomeChecks)
        + "," + N(g_roPendingKept) + "," + N(g_roDualAsked) + "," + N(g_roDualKept) + "," + N(g_roAnswerUnknown) + "," + N(g_roMalformed)
        + " receipt[sent,expected,acked,rosterClosed,slotGone,resent,gaveUp,unexpected,out,open,malformed]=" + N(g_rcSent) + "," + N(g_rcExpected) + "," + N(g_rcAcked) + "," + N(g_rcRosterClosed)
        + "," + N(g_rcSlotGone) + "," + N(g_rcResent) + "," + N(g_rcGaveUp) + "," + N(g_rcUnexpected) + "," + N(g_rcOut) + "," + N((long long)g_outbox.size()) + "," + N(g_rcMalformed)
        + " gen[mine,copy,wdStale,wdMalformed,ownerMovedStale]=" + N((long long)g_mineGen.size()) + "," + N((long long)g_copyGen.size()) + "," + N(g_wdStale) + "," + N(g_wdMalformed) + "," + N(g_ownerMovedStale)
        + " toSlot[live,session,none]=" + N(g_toSlotLive) + "," + N(g_toSlotSession) + "," + N(g_toSlotNone)
        + " xferRoad[noSlot,ackWrongSender]=" + N(g_xferNoSlot) + "," + N(coop::HandoffAckWrongSender())
        + " squadLeadRoad[live,liveFailed]=" + N(g_leadLive) + "," + N(g_leadLiveFailed);
}
// M-A step 2 - UNLOAD. Reliable. M7a A1 build 1 [a1b1-sp28]: uid | seq | gen | sector | the expected receivers (SendWithdraw).
bool SendUnload(unsigned int uid, int ownerStillRuns)
{
    return SendWithdraw(MSG_UNLOAD, uid, cooplo::UnloadWhy(ownerStillRuns));   /* M7a: route WORLD on the notebook road; the outbox resends by SLOT */
}
/* M7a A1 build 2 [a1b2-sp7] (design 1.3, 2.5; protocol 125): MSG_HANDBACK (67) is retired. RELEASE (68) goes to ONE slot by WorldFirstRoute
   (LIVE SLOT; the session link only while the notebook link is down and the slot is the session peer's) - false = no road, the release's
   own tick sends it again. RELEASE_ACK (69) goes back to the RELEASE's sender on the XFER_ACK's road (cooplo::XferAckRoad). */
bool SendRelease(int slot, const std::vector<char>& b)
{
    if (slot < 0 || b.empty()) return false;
    return SendToSlot(slot, MSG_RELEASE, b);
}
bool SendReleaseAck(unsigned int toPeer, const std::vector<char>& b)
{
    if (b.empty()) return false;
    const int slot = PeerSlotOfKey(toPeer);
    const cooplo::Road rs = RoadToSlot(slot);
    const int road = cooplo::XferAckRoad(g_dispCameBySession, cooplive::IsRelayPeer(toPeer) ? 1 : 0, slot, rs.road, SessionUpNow() ? 1 : 0, coop::LinkPeerSlot());
    if (road == cooplo::kRoadLive) return SendToSlot(slot, MSG_RELEASE_ACK, b);
    if (road == cooplo::kRoadSession && g_transport->Send(0, MSG_RELEASE_ACK, &b[0], b.size(), CH_RELIABLE)) { ++g_toSlotSession; return true; }
    ++g_toSlotNone;
    return false;
}
/* peer is uid's recorded owner (by player number, as RemoteMayWrite) */
bool IsRecordedOwner(unsigned int uid, unsigned int peer)
{
    std::map<unsigned int, unsigned int>::const_iterator o = g_owner.find(uid);
    return o != g_owner.end() && OwnerMatch(o->second, peer);
}
/* a copy's owner record and gen go (never a person this game runs) */
void ForgetCopyRecord(unsigned int uid)
{
    if (g_localOwned.find(uid) != g_localOwned.end()) return;
    g_owner.erase(uid); g_copyGen.erase(uid);
}
// P1 persistence - RECORD. See transport.h. (PutStr/GetStr moved up beside PutU32 for the WELCOME - E38.)
std::string (*g_recordTownOf)(const std::string&) = 0;   /* decision 34: the store answers "which town is this group's home" for the encoder */
void SetRecordTownLookup(std::string (*fn)(const std::string&)) { g_recordTownOf = fn; }
std::string (*g_recordHomeOf)(const std::string&) = 0;   /* the store answers "which building is this group's home" for the encoder */
void SetRecordHomeLookup(std::string (*fn)(const std::string&)) { g_recordHomeOf = fn; }
void EncodeRecordPayload(std::vector<char>* b, const std::string& worldId, const std::string& squadSid, const std::string& factionName, float x, float y, float z,
                         long long writtenAt, int owner, const std::vector<char>& bytes, unsigned flags)
{
    b->clear(); PutStr(b, worldId); PutStr(b, squadSid); PutStr(b, factionName);
    size_t at = b->size(); b->resize(at + 12); std::memcpy(&(*b)[at], &x, 4); std::memcpy(&(*b)[at + 4], &y, 4); std::memcpy(&(*b)[at + 8], &z, 4);
    PutU32(b, (unsigned int)(writtenAt & 0xFFFFFFFFu)); PutU32(b, (unsigned int)((unsigned long long)writtenAt >> 32)); PutU32(b, (unsigned int)owner);
    PutU32(b, (unsigned int)bytes.size()); b->insert(b->end(), bytes.begin(), bytes.end());
    PutStr(b, g_recordTownOf ? g_recordTownOf(worldId) : std::string());   /* decision 34: home town, last field (protocol 34 / store 32) */
    /* B12 / store protocol 41: the record's SEQUENCE NUMBER field, and THIS GAME ALWAYS SENDS 0 IN IT. The
       number is the notebook's - it is stamped by the one process that orders the folder's writes, and a
       game that chose its own would be choosing which of two writes wins. The field is written all the same
       so the message has one shape in both directions and the notebook's decoder has one length to expect. */
    PutU32(b, 0u); PutU32(b, 0u);
    b->push_back((char)(flags & 0xFFu));   /* the flags byte (factionkey.h kRecFacCode) */
    PutStr(b, g_recordHomeOf ? g_recordHomeOf(worldId) : std::string());   /* the group's home building key, after the flags byte - a reader one build behind stops at the flags byte */
}
bool DecodeRecordPayload(const std::vector<char>& p, std::string* worldId, std::string* squadSid, std::string* factionName, float* x, float* y, float* z,
                         long long* writtenAt, int* owner, std::vector<char>* bytes, std::string* town,
                         unsigned long long* seq, unsigned* flags, std::string* home)
{
    size_t at = 0;
    if (!GetStr(p, &at, worldId) || !GetStr(p, &at, squadSid) || !GetStr(p, &at, factionName) || p.size() < at + 12 + 12 + 4) return false;
    float v[3]; std::memcpy(v, &p[at], 12); at += 12; *x = v[0]; *y = v[1]; *z = v[2];
    unsigned int lo = 0, hi = 0, ow = 0, n = 0; GetU32(p, at, &lo); GetU32(p, at + 4, &hi); GetU32(p, at + 8, &ow); at += 12;
    if (!GetU32(p, at, &n) || p.size() < at + 4 + n) return false;
    *writtenAt = (long long)(((unsigned long long)hi << 32) | lo); *owner = (int)ow;
    bytes->assign(p.begin() + at + 4, p.begin() + at + 4 + n);
    if (town) { town->clear(); size_t at2 = at + 4 + n; if (at2 < p.size()) GetStr(p, &at2, town); }   /* decision 34: absent on an old sender = "" */
    if (seq)
    {
        /* B12 / store protocol 41: the sequence number rides after the home town, so reaching it means
           re-walking the town field - the town is a length-prefixed string and its length is not known
           from here. An absent field reads back as 0, which is "no notebook has stamped this record",
           and that is the honest answer for a sender that does not carry one. */
        *seq = 0ULL;
        size_t at3 = at + 4 + n;
        std::string skip;
        if (at3 < p.size() && GetStr(p, &at3, &skip) && at3 + 8 <= p.size())
        {
            unsigned int slo = 0, shi = 0; GetU32(p, at3, &slo); GetU32(p, at3 + 4, &shi);
            *seq = ((unsigned long long)shi << 32) | slo;
        }
    }
    if (flags)   /* the flags byte after the sequence number; absent = 0 */
    {
        *flags = 0u;
        size_t at4 = at + 4 + n;
        std::string skip;
        if (at4 < p.size() && GetStr(p, &at4, &skip) && at4 + 9 <= p.size()) *flags = (unsigned char)p[at4 + 8];
    }
    if (home)   /* the group's home building key after the flags byte; absent (an older sender) = "" */
    {
        home->clear();
        size_t at5 = at + 4 + n;
        std::string skip;
        if (at5 < p.size() && GetStr(p, &at5, &skip) && at5 + 9 < p.size()) { at5 += 9; GetStr(p, &at5, home); }
    }
    return true;
}
/* M2 (decisions 32/44/54): SendRecord, SendDeletedBits, SendWorldListed, SendRecordGone, SendSectorMap and SendZones
   are DELETED with their On* handlers - the notebook carries all six. EncodeRecordPayload / DecodeRecordPayload
   above stay: the store link encodes and decodes the same RECORD payload. */
/* M5a (T-197 piece 4; session protocol 106, store protocol 59) + FOLD 1 (review of 23f4216a #2a, #3) - ONE DELIVERY PER
   DESTINATION. A standing must reach every other player: the session peer (the session road) and every game the notebook
   admitted (the notebook road, LIVE - coop::StoreLiveReady). cooplive::LiveRoadDecide is the whole rule, offline-tested:
   a CHANGE or my own snapshot goes WORLD when the session peer is KNOWN reachable through the notebook (or there is no
   session link), else on the session road to it AND WORLD_EXCEPT its slot; an ASK (a RELSYNC this game originates) goes on
   the same roads (fold 2) - its notebook copy WORLD, marked PROOF for the session peer, which answers only the session
   copy - so every peer answers once; an ANSWER goes back ONLY on the road its ask
   came by - session in, session out; relayed in, SLOT to the asker. "Known reachable": a standing arrived THROUGH THE
   NOTEBOOK stamped with the session peer's slot (LinkPeerSlot), on this session link and this welcomed notebook link; a
   session-road RELSYNC whose sender says its notebook road is DOWN clears and BARS it, until a relayed ask NEWER than that
   one (fold 2: cooplive::PeerRelayBook). A send a
   road refused is never retried on the other road. A relayed message's sender is 0x80000000 | the origin's slot
   (cooplive::RelayPeerId), which no ownership check accepts until M5b. MAIN THREAD. */
static long long g_relViaLive = 0, g_relViaSession = 0, g_relLiveFailed = 0, g_relRecvLive = 0, g_relRecvSession = 0;
static long long g_relAnswerSession = 0, g_relAnswerSlot = 0, g_relPeerSlotUnknown = 0, g_relPeerOkSet = 0, g_relPeerOkCleared = 0;
static int g_relAnswerRoad = cooplive::kAnswerNone;   /* set only while OnRelSync's answer snapshot runs */
static unsigned int g_relAnswerSlotTo = 0;
static cooplive::PeerRelayBook g_relBook;   /* fold 2: the session peer's "reachable through the notebook" book */
static long g_relBookLive = -1, g_relBookSess = -1;
static int g_relBookSlot = -2;
static unsigned int g_relAskSeq = 0;
static long long g_relAskSent = 0, g_relProofOnly = 0, g_relBarredIgnored = 0;
static bool RelSessionUp() { return g_transport != 0 && g_transport->State() == LINK_UP; }
/* a fresh book whenever this game's welcomed notebook link, the session link or the peer's slot changes */
static void RelBookSync()
{
    const long lg = coop::StoreLiveGen(); const long sg = SessionLinkGen(); const int ps = coop::LinkPeerSlot();
    if (lg != g_relBookLive || sg != g_relBookSess || ps != g_relBookSlot) { g_relBook = cooplive::PeerRelayBook(); g_relBookLive = lg; g_relBookSess = sg; g_relBookSlot = ps; }
}
bool SessionPeerRelayOk()
{
    RelBookSync();
    return g_relBook.ok && RelSessionUp() && g_relBookSlot >= 0 && g_relBookLive != 0;
}
static bool RelSendOneRoad(MsgType type, const std::vector<char>& b, bool isAsk)
{
    if (b.empty()) return false;
    const cooplive::RoadPlan p = cooplive::LiveRoadDecide(g_relAnswerRoad, g_relAnswerSlotTo, coop::StoreLiveReady(), RelSessionUp(), isAsk, SessionPeerRelayOk(), coop::LinkPeerSlot());
    if (!p.session && !p.live) return false;
    if (g_relAnswerRoad == cooplive::kAnswerSession) ++g_relAnswerSession; else if (g_relAnswerRoad == cooplive::kAnswerSlot) ++g_relAnswerSlot;
    if (p.peerSlotUnknown) ++g_relPeerSlotUnknown;
    bool ok = true;
    if (p.session) { if (g_transport->Send(0, type, &b[0], b.size(), CH_RELIABLE)) ++g_relViaSession; else ok = false; }
    if (p.live) { if (coop::StoreSendLive(p.route, p.target, (unsigned int)type, b, true)) ++g_relViaLive; else { ++g_relLiveFailed; ok = false; } }
    return ok;
}
static void RelNoteRecv(unsigned int peer, bool isAsk, unsigned int askFlags, unsigned int askSeq)
{
    RelBookSync();
    if (cooplive::IsRelayPeer(peer))
    {
        ++g_relRecvLive;
        if (g_relBookSlot >= 0 && (int)cooplive::RelayPeerSlot(peer) == g_relBookSlot && RelSessionUp() && g_relBookLive != 0)
        {
            const bool wasBarred = g_relBook.barred;
            if (cooplive::PeerBookRelayed(&g_relBook, isAsk, askSeq)) ++g_relPeerOkSet;
            else if (wasBarred && g_relBook.barred) ++g_relBarredIgnored;
        }
    }
    else
    {
        ++g_relRecvSession;
        if (isAsk && (askFlags & cooplive::kAskRelayUp) == 0 && cooplive::PeerBookSessionAskRelayDown(&g_relBook, askSeq)) ++g_relPeerOkCleared;
    }
}
/* M7a fold (review 2026-09-30 F1): a relayed standing store.cpp dropped because this game had no running world still ARRIVED through
   the notebook stamped with its sender's slot - that is the proof the book keeps, so it is noted exactly as a dispatched one is. */
static long long g_relProofWhileBlocked = 0;
void SessionNoteRelayedStandingDropped(unsigned int peer, bool isAsk, const char* p, size_t n)
{
    unsigned int f = 0, s = 0;
    if (isAsk) cooplive::RelSyncDecode(p, n, &f, &s);
    RelNoteRecv(peer, isAsk, f, s);
    ++g_relProofWhileBlocked;
}
bool SendRelation(const std::string& ownerSid, const std::string& otherSid, float relation, float trust, float trustNeg, unsigned int flags, unsigned int reason)
{
    std::vector<char> b; PutStr(&b, ownerSid); PutStr(&b, otherSid);
    size_t at = b.size(); b.resize(at + 12); std::memcpy(&b[at], &relation, 4); std::memcpy(&b[at + 4], &trust, 4); std::memcpy(&b[at + 8], &trustNeg, 4);
    PutU32(&b, flags); PutU32(&b, reason);
    return RelSendOneRoad(MSG_RELATION, b, false);
}
bool SendRelSync()
{
    /* fold 2 (#1): an ask takes the change rule's roads - with the session peer not known reachable, the session road to
       it AND the notebook's WORLD, that copy marked PROOF ONLY for the session peer. One ask number for both copies. */
    const bool live = coop::StoreLiveReady();
    const cooplive::RoadPlan p = cooplive::LiveRoadDecide(cooplive::kAnswerNone, 0, live, RelSessionUp(), true, SessionPeerRelayOk(), coop::LinkPeerSlot());
    if (!p.session && !p.live) return false;
    const unsigned int seq = ++g_relAskSeq;
    const unsigned int up = live ? (unsigned int)cooplive::kAskRelayUp : 0u;
    std::vector<char> s, l;
    cooplive::RelSyncEncode(&s, up, seq);
    cooplive::RelSyncEncode(&l, up | (p.relayProof ? (unsigned int)cooplive::kAskProofOnly : 0u), seq);
    bool ok = true;
    if (p.session) { if (g_transport->Send(0, MSG_RELSYNC, &s[0], s.size(), CH_RELIABLE)) ++g_relViaSession; else ok = false; }
    if (p.live) { if (coop::StoreSendLive(p.route, p.target, (unsigned int)MSG_RELSYNC, l, true)) ++g_relViaLive; else { ++g_relLiveFailed; ok = false; } }
    if (ok) ++g_relAskSent;
    return ok;
}
void OnRelSync(const Message& m)
{
    unsigned int askFlags = 0, askSeq = 0;
    cooplive::RelSyncDecode(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), &askFlags, &askSeq);
    RelNoteRecv(m.peer, true, askFlags, askSeq);
    /* fold 2 (#1): my session peer's PROOF copy - its session copy is the one I answer */
    if (cooplive::IsRelayPeer(m.peer) && (askFlags & cooplive::kAskProofOnly) != 0 && coop::LinkPeerSlot() >= 0
        && (int)cooplive::RelayPeerSlot(m.peer) == coop::LinkPeerSlot()) { ++g_relProofOnly; return; }
    /* fold 1 (#2a, #3): the answer goes back on the road the ask came by - and only there */
    if (cooplive::IsRelayPeer(m.peer)) { g_relAnswerRoad = cooplive::kAnswerSlot; g_relAnswerSlotTo = cooplive::RelayPeerSlot(m.peer); }
    else g_relAnswerRoad = cooplive::kAnswerSession;
    coop::RelationsSendSnapshot();
    g_relAnswerRoad = cooplive::kAnswerNone;
}
void OnRelation(const Message& m)
{
    RelNoteRecv(m.peer, false, 0, 0);   /* M5a: which road it came by (fold 1: and whether the session peer is reachable through the notebook) */
    std::string a, b; size_t at = 0;
    if (!GetStr(m.payload, &at, &a) || !GetStr(m.payload, &at, &b) || m.payload.size() < at + 20) { ErrorLog("[net] malformed RELATION - ignored"); return; }
    float v[3]; std::memcpy(v, &m.payload[at], 12); unsigned int flags = 0, reason = 0; GetU32(m.payload, at + 12, &flags); GetU32(m.payload, at + 16, &reason);
    coop::SetWireRelayedSender(cooplive::IsRelayPeer(m.peer) ? (int)cooplive::RelayPeerSlot(m.peer) : -1);   /* fold 1: a relayed name is not the session peer's */
    coop::ApplyRemoteRelation(a, b, v[0], v[1], v[2], flags, reason);
    coop::SetWireRelayedSender(-1);
}

void OnUnload(const Message& m)
{
    cooplo::WithdrawMsg w;   /* M7a A1 build 1 [a1b1-sp30]: uid | seq | gen | sector | expected receivers */
    if (!WithdrawDecodeOrSay(m, "UNLOAD", &w)) return;
    const unsigned int uid = w.uid;
    WithdrawReceiptNote(m.peer, w);   /* listed: acknowledged whatever happens below */
    if (WithdrawStaleHere(uid, w.gen, "UNLOAD")) return;
    if (!RemoteMayWrite(uid, m.peer)) return;   // only the character's owner may unload our copy
    if (cooplo::UnloadClearsGiven(w.why) == 1) coop::SquadIdxOwnerWithdrew(uid, 1);   /* its recorded owner runs it nowhere (put away asleep, reloaded, retired) - out of the squad index */
    else coop::SquadIdxOwnerStillRuns(uid);   /* the owner's announce pass (this game's player left the area; the owner still runs the person): `given` stays, so this game never re-wakes the squad from its own world data */
    coop::ApplyRemoteUnload(uid);
    LostCopyForget(uid, "the owner withdrew it (UNLOAD)");   /* nothing to ask for */
}
/* M7a A1 build 2 [a1b2-sp8]: RELEASE (68) / RELEASE_ACK (69), from either road - exact lengths (cooplo::ReleaseDecode / ReleaseAckDecode);
   the handlers are handoff.cpp's (MAIN THREAD, the drain). */
void OnRelease(const Message& m)
{
    cooplo::ReleaseMsg r;
    if (m.payload.empty() || !cooplo::ReleaseDecode(&m.payload[0], m.payload.size(), &r)) { ErrorLog("[net] malformed RELEASE (" + N((long long)m.payload.size()) + " bytes) - ignored"); return; }
    coop::ApplyRemoteRelease(r, m.peer);
}
void OnReleaseAck(const Message& m)
{
    cooplo::ReleaseAckMsg a;
    if (m.payload.empty() || !cooplo::ReleaseAckDecode(&m.payload[0], m.payload.size(), &a)) { ErrorLog("[net] malformed RELEASE_ACK (" + N((long long)m.payload.size()) + " bytes) - ignored"); return; }
    coop::ApplyRemoteReleaseAck(a, m.peer);
}

// M-D - XFER: leader(u32) | reason(u32; protocol 84, coopsquad::kXferReason* - was "forced") | count(u32) | per uid 48 bytes. Reliable.
/* M7a A1 build 1 [a1b1-sp31] (design 1.3 [review F11], protocol 118): + count x u32 gen (the giver's; the taker holds gen + 1), and the
   road is WorldFirstRoute to the TARGET SLOT (LIVE SLOT; the session link only while the notebook link is down). No slot: not sent. */
typedef char A1XferMemberIs48[(sizeof(XferMember) == cooplo::kXferMemberSize) ? 1 : -1];
bool SendXfer(unsigned int leader, unsigned int reason, const XferMember* members, int count, int targetSlot)
{
    if (count <= 0 || count > 64) return false;
    if (targetSlot < 0) { ++g_xferNoSlot; return false; }
    std::vector<char> b; PutU32(&b, leader); PutU32(&b, reason); PutU32(&b, (unsigned int)count);
    size_t at = b.size(); b.resize(at + (size_t)count * sizeof(XferMember)); std::memcpy(&b[at], members, (size_t)count * sizeof(XferMember));
    for (int i = 0; i < count; ++i) PutU32(&b, MineGenOf(members[i].uid));
    return SendToSlot(targetSlot, MSG_XFER, b);
}
void OnXfer(const Message& m)
{
    unsigned int leader = 0, forced = 0, n = 0;
    if (m.payload.size() < 12 || !GetU32(m.payload, 0, &leader) || !GetU32(m.payload, 4, &forced) || !GetU32(m.payload, 8, &n)
        || !cooplo::XferLengthOk(m.payload.size(), n)) { ErrorLog("[net] malformed XFER (" + N((long long)m.payload.size()) + " bytes; protocol 118 carries a gen per member) - ignored"); return; }
    std::vector<XferMember> mem(n); std::memcpy(&mem[0], &m.payload[12], (size_t)n * sizeof(XferMember));
    std::vector<unsigned int> gens(n);   /* M7a A1 build 1 [a1b1-sp32]: the giver's gen per member */
    for (unsigned int i = 0; i < n; ++i) GetU32(m.payload, cooplo::XferGenAt(n) + (size_t)i * 4, &gens[i]);
    coop::ApplyRemoteXfer(leader, forced, &mem[0], (int)n, m.peer, &gens[0]);   /* protocol 84: the u32 is the reason code */
}
/* M7a A1 build 1 [a1b1-sp33]: back to the XFER's sender by slot (WorldFirstRoute); a session peer with no slot yet is answered on the
   session link, as before. Fold 1 [a1b1f1-sp2] [F1] (cooplo::XferAckRoad): an XFER that came on the session link is answered on it while
   it is up (one-sided outage: by slot the ACK would go to a world server that cannot deliver it, the giver re-owes and two games run
   the person); a slot with no road goes on the session link when that is up and the slot is the session peer's. */
bool SendXferAck(unsigned int leader, const unsigned int* takenUids, int count, unsigned int toPeer)
{
    std::vector<char> b; PutU32(&b, leader); PutU32(&b, (unsigned int)count);
    for (int i = 0; i < count; ++i) PutU32(&b, takenUids[i]);
    const int slot = PeerSlotOfKey(toPeer);
    const cooplo::Road rs = RoadToSlot(slot);
    const int road = cooplo::XferAckRoad(g_dispCameBySession, cooplive::IsRelayPeer(toPeer) ? 1 : 0, slot, rs.road, SessionUpNow() ? 1 : 0, coop::LinkPeerSlot());
    if (road == cooplo::kRoadLive) return SendToSlot(slot, MSG_XFER_ACK, b);
    if (road == cooplo::kRoadSession && g_transport->Send(0, MSG_XFER_ACK, &b[0], b.size(), CH_RELIABLE)) { ++g_toSlotSession; return true; }
    ++g_toSlotNone;
    return false;
}
void OnXferAck(const Message& m)
{
    unsigned int leader = 0, n = 0;
    if (m.payload.size() < 8 || !GetU32(m.payload, 0, &leader) || !GetU32(m.payload, 4, &n)) { ErrorLog("[net] malformed XFER_ACK - ignored"); return; }
    if (n > 256 || m.payload.size() < 8 + n * 4) { ErrorLog("[net] malformed XFER_ACK (uid list) - ignored"); return; }
    std::vector<unsigned int> uids(n);
    for (unsigned int i = 0; i < n; ++i) GetU32(m.payload, 8 + i * 4, &uids[i]);
    coop::ApplyRemoteXferAck(leader, n ? &uids[0] : 0, (int)n, m.peer);
}
/* T-1 B1 restructure (protocol 85) - MSG_SQUAD_LEAD. Reliable: sent by handoff.cpp's 1-Hz pass (first sight, change, new link, n = 0
   when this game runs no member of the squad any more). */
/* M7a A1 build 1 [a1b1-sp34] (design 1.3 [review F7]): the character stream's road - route AREA with the squad's sector (its acting leader's,
   else its first readable member's; none = WORLD) on the notebook, the session link while it is down - so every game holding copies
   there hears it. */
bool SendSquadLead(const coopsquad::SquadLeadMsg& sl)
{
    std::vector<char> b;
    if (!coopsquad::EncodeSquadLead(&b, sl)) return true;   /* not encodable: dropped, never retried forever */
    const int road = CharRoadNow();
    if (road == cooplive::kCharRoadSession) return g_transport->Send(0, MSG_SQUAD_LEAD, &b[0], b.size(), CH_RELIABLE);
    if (road != cooplive::kCharRoadLive) return false;
    int key = sl.acting != 0 ? coop::CharAreaKeyNow(sl.acting) : -1;
    for (size_t i = 0; key < 0 && i < sl.members.size(); ++i) key = coop::CharAreaKeyNow(sl.members[i]);
    const unsigned int route = key >= 0 ? (unsigned int)cooplive::kRouteArea : (unsigned int)cooplive::kRouteWorld;
    const unsigned int target = key >= 0 ? cooplive::CharAreaTarget(key, -1) : 0u;
    if (coop::StoreSendLive(route, target, (unsigned int)MSG_SQUAD_LEAD, b, true)) { ++g_leadLive; return true; }
    ++g_leadLiveFailed;
    return false;
}
/* T-1 B3 restructure (protocol 89): SendKeeper / OnKeeper retired - MSG_SQUAD_LEAD carries the squad's money. */
/* Refused whole when it does not decode; applied to the announcement book in handoff.cpp (plugin state only, no engine write). */
void OnSquadLead(const Message& m)
{
    coopsquad::SquadLeadMsg sl;
    const int r = m.payload.empty() ? coopsquad::kSquadLeadDecodeTooShort : coopsquad::DecodeSquadLead(&m.payload[0], m.payload.size(), &sl);
    if (r != coopsquad::kSquadLeadDecodeOk) { ErrorLog("[net] malformed SQUAD_LEAD (reason " + N((long long)r) + ") - ignored"); return; }
    coop::ApplyRemoteSquadLead(sl, OwnerKeyOf(m.peer));   /* M7a A1 build 1 [a1b1-sp35]: the book is keyed by the announcing game's PLAYER key (fold 1 [a1b1f1-sp5] [F2]) */
}


bool SendSwing(unsigned int uid, unsigned int targetUid)
{
    if (!CharRoadOpen()) return false;   /* M7a: the notebook road, or the session link while it is down */

    char b[8];
    std::memcpy(b,     &uid,       4);
    std::memcpy(b + 4, &targetUid, 4);
    return CharSend(MSG_SWING, uid, b, sizeof(b), CH_RELIABLE, -1);   /* M7a */
}

// M-B / P064 / H027 - CONTEXT: uid(u32) | squadType(i32) | memberType(i32) | buildingHand(32 B) | charHand(32 B) | bx,by,bz(3f) | squadSidLen(u32) squadSid | townSidLen(u32) townSid
bool SendContext(unsigned int uid, const std::string& squadSid, int squadType, const std::string& townSid,
                 const unsigned char* handRaw32, const unsigned char* charRaw32, float bx, float by, float bz, int memberType,
                 const std::string& platoonId)
{
    if (!CharRoadOpen()) return false;   /* M7a: the notebook road, or the session link while it is down */
    std::vector<char> b;
    PutU32(&b, uid); PutU32(&b, (unsigned int)squadType); PutU32(&b, (unsigned int)memberType);
    b.insert(b.end(), (const char*)handRaw32, (const char*)handRaw32 + 32);
    b.insert(b.end(), (const char*)charRaw32, (const char*)charRaw32 + 32);
    size_t at = b.size(); b.resize(at + 12);
    std::memcpy(&b[at], &bx, 4); std::memcpy(&b[at + 4], &by, 4); std::memcpy(&b[at + 8], &bz, 4);
    PutU32(&b, (unsigned int)squadSid.size()); b.insert(b.end(), squadSid.begin(), squadSid.end());
    PutU32(&b, (unsigned int)townSid.size());  b.insert(b.end(), townSid.begin(), townSid.end());
    PutU32(&b, (unsigned int)platoonId.size()); b.insert(b.end(), platoonId.begin(), platoonId.end());   // P1: the sender's platoon id = the squad's world id
    return CharSend(MSG_CONTEXT, uid, &b[0], b.size(), CH_RELIABLE, -1);   /* M7a */
}

// H019 - INTENT: uid(u32) | taskType(i32) | subjectUid(u32) | x,y,z(3f) | priority(i32) = 28 bytes, reliable.
bool SendIntent(unsigned int uid, int taskType, unsigned int subjectUid, float x, float y, float z, int priority)
{
    if (!CharRoadOpen()) return false;   /* M7a: the notebook road, or the session link while it is down */
    char b[28];
    std::memcpy(b, &uid, 4); std::memcpy(b + 4, &taskType, 4); std::memcpy(b + 8, &subjectUid, 4);
    float v[3] = { x, y, z }; std::memcpy(b + 12, v, 12); std::memcpy(b + 24, &priority, 4);
    return CharSend(MSG_INTENT, uid, b, sizeof(b), CH_RELIABLE, -1);   /* M7a */
}

// P25 fold 2 - INSIDE: uid(u32) | flags(u8, bit0 inside) | keyLen(u8) | key bytes (src/common/insidewire.h), reliable, the
// character stream's road (CharSend: AREA by the character's sector on the notebook, the session link while it is down).
bool SendInside(unsigned int uid, int inside, const char* key)
{
    if (!CharRoadOpen()) return false;
    std::vector<char> b;
    const size_t n = (key != 0) ? std::strlen(key) : 0;
    if (!p25inside::EncodeInside(&b, uid, inside, key, n)) return false;
    return CharSend(MSG_INSIDE, uid, &b[0], b.size(), CH_RELIABLE, -1);
}

// F322. Sent from the `GameWorld::destroy` detour, which F321 measured running on the MAIN thread -
// so this is an ordinary send, not a queued one. RELIABLE, because a lost despawn is permanent: the
// peer would keep that character for the rest of the session with nothing to correct it.
bool SendDespawn(unsigned int uid)
{
    return SendWithdraw(MSG_DESPAWN, uid, cooplo::kWdWhyNone);   /* M7a: route WORLD on the notebook road; M7a A1 build 1 [a1b1-sp29]: with seq, gen and the expected receivers */
}

bool SendTaskMsg(unsigned int uid, int taskType, float x, float y, float z)
{
    if (!CharRoadOpen()) return false;   /* M7a: the notebook road, or the session link while it is down */

    char b[20];
    std::memcpy(b,     &uid,      4);
    std::memcpy(b + 4, &taskType, 4);
    float v[3] = { x, y, z };
    std::memcpy(b + 8, v, 12);

    return CharSend(MSG_TASK, uid, b, sizeof(b), CH_RELIABLE, -1);   /* M7a */
}

bool SendMove(unsigned int uid, float x, float y, float z, float velX, float velZ, float desiredSpeed, float faceX, float faceZ,
              unsigned int stampMs)
{
    if (!CharRoadOpen()) return false;   /* M7a: the notebook road, or the session link while it is down */

    char b[40];   // H024: + facing (2f); + the owner's clock stamp (u32 ms). review 4 item 4: bytes written == bytes sent
    std::memcpy(b, &uid, 4);
    // F350: velX/velZ are a VELOCITY in units/second, not a facing. F419: the sixth float is the
    // authority's commanded desiredSpeed - the GAIT intent (walkSpeed for a walker, 999 for a
    // runner), which is what selects the copy's walk/run animation. -1 = unreadable; 0 is a real level.
    float v[8] = { x, y, z, velX, velZ, desiredSpeed, faceX, faceZ };
    std::memcpy(b + 4, v, 32);
    std::memcpy(b + 36, &stampMs, 4);   // the moment this sample was taken, on the owner's clock: the copy replays by it

    /* M5b proof verb (livemove <n>): the next n MOVEs go THROUGH THE NOTEBOOK by route WORLD - one road each. M7a: every other
       MOVE also goes through the notebook while it is up, by route AREA (CharSend below); this verb only forces WORLD. */
    if (g_moveViaLiveBudget > 0 && coop::StoreLiveReady())
    {
        --g_moveViaLiveBudget;
        const std::vector<char> v(b, b + sizeof(b));
        if (coop::StoreSendLive(cooplive::kRouteWorld, 0, (unsigned int)MSG_MOVE, v, true)) { ++g_moveViaLive; return true; }
        ++g_moveViaLiveFailed;
        return false;
    }
    return CharSend(MSG_MOVE, uid, b, sizeof(b), CH_UNRELIABLE, coop::AreaKeyAt(x, z));   /* M7a: the sector of the position it carries */
}

// MOVESTOP: uid(u32) | x,y,z(3f) | stamp(u32 ms) = 20 bytes, reliable, on the character stream's road (the road INTENT takes).
bool SendMoveStop(unsigned int uid, float x, float y, float z, unsigned int stampMs)
{
    if (!CharRoadOpen()) return false;   /* the notebook road, or the session link while it is down */
    char b[20];
    float v[3] = { x, y, z };
    std::memcpy(b, &uid, 4); std::memcpy(b + 4, v, 12); std::memcpy(b + 16, &stampMs, 4);
    const int road = CharRoadNow();
    if (!CharSend(MSG_MOVESTOP, uid, b, sizeof(b), CH_RELIABLE, coop::AreaKeyAt(x, z))) return false;
    ++g_moveStopSent[road == cooplive::kCharRoadLive ? 1 : 0];
    return true;
}

bool SendClothing(unsigned int uid, const coop::GarmentSet& set)
{
    if (!CharRoadOpen()) return false;   /* M7a: the notebook road, or the session link while it is down */

    std::vector<char> b;
    PutU32(&b, uid);
    coop::SerialiseGarments(set, &b);
    const bool sent = CharSend(MSG_CLOTHING, uid, &b[0], b.size(), CH_RELIABLE, -1);   /* M7a */
    /* review-inv6p2 LOW: the kit's marked rows are counted only once the message has gone (they were counted while serialising) */
    if (sent && set.details.size() == set.Count())
        coop::ItemOwnerNoteRowsCarried(coopgarment::GarmentMarked(set.details), coopgarment::GarmentUnknown(set.details));
    /* bag23 part 1: the kit's packs' marked rows, counted the same way once the message has gone */
    if (sent) for (size_t k = 0; k < set.bags.size(); ++k)
        coop::ItemOwnerNoteRowsCarried(coopbag::BagRowsMarked(set.bags[k].rows), coopbag::BagRowsUnknown(set.bags[k].rows));
    return sent;
}

// E22a / decision 38 (approved). The 0x2A record fields ride ONLY on an ADD - every other op names a slot the
// receiver already holds an item in, so re-sending an item's identity there would be bytes that can only
// disagree with what is already there.
/* M7b slice 2 (T-197; protocol 115) - THE EIGHT ITEM MESSAGES ON ONE ADDRESSED ROAD (src/common/ownerroute.h ItemAddrDecide /
   ItemTargetRoad). A move and a box push ride the character stream's road (CharRoadNow); an addressed send picks its road per
   TARGET (fold 1) - the session peer keeps the character road while the session link is its road, so a parity answer still
   reaches its asker before any later move of that box. A request goes to its target's owner or its key's writer, an answer back to the asker, a move and a
   box push to every game covering the area. Each counts one of viaSession / viaLive / noOwner / noSlot in addr.item; a request
   handed to a road counts toOwner too. MAIN THREAD. */
typedef char M7b2ItemRoadIsTheCharRoad[(cooplive::kItemRoadNone == cooplive::kCharRoadNone && cooplive::kItemRoadLive == cooplive::kCharRoadLive
    && cooplive::kItemRoadSession == cooplive::kCharRoadSession) ? 1 : -1];
static std::map<unsigned int, cooplive::ItemAskedSet> g_itemAsked;   /* request id -> every game it was asked of (fold 1), the re-send's game, the answerer */
static std::vector<unsigned int> g_itemAskedRing;          /* the order ids were first noted in: the oldest is forgotten first */
static size_t g_itemAskedNext = 0;
static const size_t kItemAskedMax = 4096;
static void ItemAskedNote(unsigned int id, unsigned int key)
{
    if (g_itemAsked.find(id) == g_itemAsked.end())
    {
        if (g_itemAskedRing.size() < kItemAskedMax) g_itemAskedRing.push_back(id);
        else
        {
            g_itemAsked.erase(g_itemAskedRing[g_itemAskedNext]);
            g_itemAskedRing[g_itemAskedNext] = id;
            g_itemAskedNext = (g_itemAskedNext + 1) % kItemAskedMax;
        }
    }
    cooplive::ItemAskedAdd(&g_itemAsked[id], key, coop::LinkPeerSlot());
}
static cooplive::ItemAskedSet* ItemAskedFind(unsigned int id)
{
    std::map<unsigned int, cooplive::ItemAskedSet>::iterator it = g_itemAsked.find(id);
    return (it == g_itemAsked.end()) ? 0 : &it->second;
}
/* the game follow-ups go to: the one whose answer was taken, else the one asked */
static bool ItemAskedKey(unsigned int id, unsigned int* key)
{
    const cooplive::ItemAskedSet* a = ItemAskedFind(id);
    return a != 0 && cooplive::ItemAskedFollowUp(*a, key);
}
/* fold 1: the game a RE-SEND under this id is held to (none: released by its refusal, or never asked) */
static bool ItemAskedStickyKey(unsigned int id, unsigned int* key)
{
    const cooplive::ItemAskedSet* a = ItemAskedFind(id);
    return a != 0 && cooplive::ItemAskedSticky(*a, key);
}
/* fold 1: an explicit refusal (ok 0) from the game a re-send is held to releases it - that game did not serve it */
static void ItemAskedRefusedBy(unsigned int id, unsigned int fromPeer)
{
    cooplive::ItemAskedSet* a = ItemAskedFind(id);
    if (a != 0 && cooplive::ItemAskedRelease(a, fromPeer, coop::LinkPeerSlot())) ++g_itemAskedReleased;
}
bool ItemAnswerFromAsked(unsigned int id, unsigned int fromPeer)
{
    cooplive::ItemAskedSet* a = ItemAskedFind(id);
    if (a == 0) return true;   /* no ask on record: the late / foreign handling judges it */
    return cooplive::ItemAskedAccept(a, fromPeer, coop::LinkPeerSlot());   /* any game it was asked of; the answerer is remembered */
}
int PlayerSlotOfKey(unsigned int key)
{
    const int s = cooplive::AddrOwnerSlotOf(key, coop::LinkPeerSlot());
    return (s >= 0) ? s : coop::LinkPeerSlot();
}
unsigned int PlayerKeyNow(unsigned int sender) { return cooplive::PlayerKeyOf(sender, coop::LinkPeerSlot()); }   /* fold 1 (F2) */
int ItemAskedSlot(unsigned int id)
{
    unsigned int key = 0;
    if (!ItemAskedKey(id, &key)) return coop::LinkPeerSlot();   /* no ask on record: the legacy reading (the one session peer) */
    return PlayerSlotOfKey(key);
}
/* One item message to one player (toSlot: ItemAddrDecide's answer). *sentKey: the key naming the player it went to. */
static bool ItemSendTo(MsgType type, int toSlot, const char* p, size_t n, unsigned int uid, unsigned int* sentKey, int* whyOut = 0)
{
    if (whyOut != 0) *whyOut = cooplive::kAddrWhyOk;   /* fold 2 (D4): why it was not sent (kAddrWhyOk = the send itself failed) */
    if (p == 0 || n == 0) return false;
    /* fold 1: the road per TARGET (ownerroute.h ItemTargetRoad) - the session peer on the session link while that is its road, any
       other game through the world server by slot (an answer to a request relayed from a third game goes even while the
       character road is the session link, as CharSend's catch-up answer does) */
    const bool sessionUp = g_transport != 0 && g_transport->State() == LINK_UP;
    const int linkSlot = coop::LinkPeerSlot();
    const int peerInWorld = (linkSlot >= 0) ? coop::StoreRosterSlotInWorld(linkSlot) : -1;
    const int targetInWorld = (toSlot >= 0) ? coop::StoreRosterSlotInWorld(toSlot) : -1;
    const cooplive::AddrPlan a = cooplive::ItemTargetRoad(toSlot, linkSlot, sessionUp, CharRoadNow(), peerInWorld, coop::StoreLiveReady(), targetInWorld);
    if (whyOut != 0) *whyOut = a.why;
    const int group = cooplive::AddrGroupOf((unsigned int)type);   /* M7b slice 4: the side messages' addressed sends use this road too */
    const bool noOwner = (a.why == cooplive::kAddrWhyNoOwner);
    if (!noOwner) cooplive::AddrNoOwnerCountOnce(&g_addrNoOwnerSeen, group, uid, false);
    if (a.road == cooplive::kAddrSession)
    {
        const bool ok = g_transport->Send(0, type, p, n, CH_RELIABLE);
        AddrCount(type, ok ? kAddrColSession : kAddrColNoSlot);
        if (ok && sentKey != 0) *sentKey = 0u;   /* the session peer - resolved to its slot at the answer */
        return ok;
    }
    if (a.road == cooplive::kAddrLive)
    {
        const std::vector<char> v(p, p + n);
        const bool ok = coop::StoreSendLive(cooplive::kRouteSlot, a.slot, (unsigned int)type, v, true);
        AddrCount(type, ok ? kAddrColLive : kAddrColNoSlot);
        if (ok && sentKey != 0) *sentKey = cooplive::RelayPeerId(a.slot);
        return ok;
    }
    if (a.why == cooplive::kAddrWhyNotInWorld) { ++g_liveToSlotNoSuchSlot; AddrCount(type, kAddrColNoSlot); return false; }   /* not delivered - the caller retries / times out */
    if (!noOwner) AddrCount(type, kAddrColNoSlot);
    else if (cooplive::AddrNoOwnerCountOnce(&g_addrNoOwnerSeen, group, uid, true)) AddrCount(type, kAddrColNoOwner);
    return false;
}
/* The player an ANSWER goes back to: the asker's key when the caller has it; else the game request `id` was asked of. */
static int ItemToAsker(bool have, unsigned int key)
{
    return cooplive::ItemAddrDecide(cooplive::kItemToAsker, false, 0u, -1, have, key, coop::LinkPeerSlot());
}
static int ItemToAskedOf(unsigned int id)
{
    unsigned int key = 0;
    const bool have = ItemAskedKey(id, &key);
    return ItemToAsker(have, key);
}
/* A move or a box push: every game covering the area (LIVE AREA; the sector now and, when it differs, the one before), WORLD when
   there is no sector (itemNoArea); the session peer on the session road. */
static bool ItemSendArea(MsgType type, int areaKey, int prevKey, const char* p, size_t n)
{
    if (p == 0 || n == 0) return false;
    const int road = CharRoadNow();
    if (road == cooplive::kCharRoadSession)
    {
        const bool ok = g_transport != 0 && g_transport->Send(0, type, p, n, CH_RELIABLE);
        AddrCount(type, ok ? kAddrColSession : kAddrColNoSlot);
        return ok;
    }
    if (road != cooplive::kCharRoadLive) return false;
    unsigned int route = cooplive::kRouteWorld, target = 0;
    const unsigned int at = cooplive::CharAreaTarget(areaKey, prevKey);
    if (at != cooplive::kAreaTargetNone) { route = cooplive::kRouteArea; target = at; }
    else if (cooplive::AddrGroupOf((unsigned int)type) == cooplive::kAddrGroupItem) ++g_itemNoArea;
    else ++g_sideNoArea;   /* M7b slice 4 */
    const std::vector<char> v(p, p + n);
    const bool ok = coop::StoreSendLive(route, target, (unsigned int)type, v, true);
    AddrCount(type, ok ? kAddrColLive : kAddrColNoSlot);
    return ok;
}
/* The ITEM_MOVE payload of `m`, for the area send and the one-game send alike. false = it will not encode (nothing to send).
   ownSent / bagSent: an ADD's owner / pack block went with it. */
static bool ItemMoveBytes(const coop::ItemMoveMsg& m, std::vector<char>& b, bool& ownSent, bool& bagSent)
{
    PutU32(&b, m.uid);
    b.push_back((char)(unsigned char)(m.op | (m.rqtHas != 0 ? coophold::kMoveTagFlag : 0)));   /* P105 build 2: the high bit = an 'RQT1' trailer */
    PutStr(&b, m.section);
    PutU32(&b, (unsigned int)m.x);
    PutU32(&b, (unsigned int)m.y);
    PutU32(&b, (unsigned int)m.quantity);
    if (m.op == 0)
    {
        PutStr(&b, m.baseSid); PutStr(&b, m.companySid); PutStr(&b, m.materialSid); PutStr(&b, m.colorSid);
        unsigned int q = 0, ch = 0;
        std::memcpy(&q, &m.quality, 4); std::memcpy(&ch, &m.charges, 4);
        PutU32(&b, q); PutU32(&b, ch);
        PutU32(&b, (unsigned int)m.functionKind);
        PutU32(&b, (unsigned int)m.level);
        PutU32(&b, (unsigned int)m.unique);
    }
    // E22c (P6e). A character uid is never 0, so uid == 0 is the whole signal that this move is on a STORAGE
    // BOX, and the building's instance id follows EVERYTHING ELSE. Appending rather than inserting is what
    // keeps protocol 37's field order intact for the character case, which is every move E22a and E22b send.
    // E22c-2 (P7n): the key, then the instance id BESIDE it. The id is allowed to be empty - that is the
    // ordinary state in a shipped town - and the receiver never keys on it; it counts whether it agrees with
    // the building the position key found. Both fields sit before the op-2 base sid so that block stays last.
    if (m.uid == 0) { PutStr(&b, m.boxKey); PutStr(&b, m.boxId); }
    /* inv6 (protocol 71) + items9 (protocol 78): an ADD ends with [BAG1 - a pack's contents, a holder's GROUND ADD] [OWN1 - the stolen
       mark], each only when there is something (coopistat::EncodeAddTail; op 0 has no op-2 sid, so this tail is last). A block that
       will not encode is dropped - the item goes without it - rather than the move lost. */
    ownSent = false; bagSent = false;
    if (m.op == 0)
    {
        int bs = 0, os = 0;
        coopistat::EncodeAddTail(&b, m.bag, m.owner, &bs, &os);
        ownSent = (os == 1); bagSent = (bs == 1);
        if (os < 0) coop::ItemOwnerNoteBadBlock();
    }
    /* items9 (protocol 78): op 4 CHARGES ends with the uses left and the item's base sid; one that will not encode is not sent. */
    if (m.op == 4 && !coopistat::EncodeChargesTail(&b, m.charges, m.baseSid)) return false;
    /* P7a fold 1 (review-p6s H-2). AN ABSOLUTE QUANTITY CARRIES WHAT IT IS ABOUT. The receiver refuses an
       op 2 aimed at cell (0,0) whose base sid is not what is in that cell, which is the signature of the
       wrong-cell merge write this fold removes at the sender. APPENDED AFTER EVERYTHING ELSE, exactly as the
       container key was and for the same reason: protocol 37's field order stays intact for every message
       that already existed, and a reader that stops before these bytes simply does not see them. */
    if (m.op == 2) PutStr(&b, m.baseSid);
    /* bag23 part 2 (protocol 81): a move INSIDE A PACK ends with the pack's path, LAST (op 2's sid above is always written, so the
       block is never read as a sid). op 4 has no path form; one that will not encode is not sent - a path is never guessed at. */
    if (m.bagPath.has != 0 && (m.op == 4 || !coopbag::EncodeBagPath(&b, m.bagPath)))
    { ErrorLog("[net] ITEM_MOVE op " + N(m.op) + " inside a pack: its path would not encode - NOT SENT"); return false; }
    /* P105 build 2 (protocol 126): the REQUEST TAG of a publication a HOLD / LAND caused, LAST of all (announced by the op byte) */
    if (m.rqtHas != 0) { coophold::ReqTag tg; tg.reqId = m.rqtId; tg.slot = m.rqtSlot; coophold::PutReqTag(&b, tg); }
    return true;
}
bool SendItemMove(const coop::ItemMoveMsg& m)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 2: the item road, not the session link alone */
    std::vector<char> b;
    bool ownSent = false, bagSent = false;
    if (!ItemMoveBytes(m, b, ownSent, bagSent)) return false;
    /* M7b slice 2: to every game covering the area - a character's sector read now (or the one its stream was last sent in), a
       box's, a shop piece's or a ground key's own (coop::ItemKeySector) */
    int areaKey = -1, prevKey = -1;
    if (m.uid != 0)
    {
        std::map<unsigned int, int>::const_iterator last = g_charLastArea.find(m.uid);
        prevKey = (last != g_charLastArea.end()) ? last->second : -1;
        areaKey = coop::CharAreaKeyNow(m.uid);
        if (areaKey < 0) areaKey = prevKey;
    }
    else
    {
        int sx = -1, sy = -1;
        if (coop::ItemKeySector(m.boxKey, &sx, &sy) != 0) areaKey = cooplive::AreaKey(sx, sy);
    }
    const bool sent = ItemSendArea(MSG_ITEM_MOVE, areaKey, prevKey, &b[0], b.size());
    if (sent && ownSent) coop::ItemOwnerNoteCarried("ITEM_MOVE", m.owner);
    if (sent && bagSent) coop::ItemOwnerNoteRowsCarried(coopbag::BagRowsMarked(m.bag), coopbag::BagRowsUnknown(m.bag));   /* items9 */
    return sent;
}
/* The same ITEM_MOVE to ONE game - the player `askerKey` names (the ground catch-up's ADD / GONE, answering only the game that
   listed its ground). */
bool SendItemMoveTo(const coop::ItemMoveMsg& m, unsigned int askerKey)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;
    std::vector<char> b;
    bool ownSent = false, bagSent = false;
    if (!ItemMoveBytes(m, b, ownSent, bagSent)) return false;
    const bool sent = ItemSendTo(MSG_ITEM_MOVE, ItemToAsker(true, askerKey), &b[0], b.size(), 0u, 0);
    if (sent && ownSent) coop::ItemOwnerNoteCarried("ITEM_MOVE", m.owner);
    if (sent && bagSent) coop::ItemOwnerNoteRowsCarried(coopbag::BagRowsMarked(m.bag), coopbag::BagRowsUnknown(m.bag));
    return sent;
}
/* One ITEM_MOVE on an every-game-but-one plan (cooplive::ExceptPlan). false with *whyOut kExceptWhyOk = it would not encode, or the
   send itself failed. */
static bool ItemSendOnPlan(const coop::ItemMoveMsg& m, const cooplive::ExceptPlan& e, int* whyOut)
{
    std::vector<char> b;
    bool ownSent = false, bagSent = false;
    if (!ItemMoveBytes(m, b, ownSent, bagSent)) { if (whyOut != 0) *whyOut = cooplive::kExceptWhyOk; return false; }
    bool sent = false;
    if (e.road == cooplive::kAddrSession)
    {
        sent = g_transport != 0 && g_transport->Send(0, MSG_ITEM_MOVE, &b[0], b.size(), CH_RELIABLE);
        AddrCount(MSG_ITEM_MOVE, sent ? kAddrColSession : kAddrColNoSlot);
    }
    else
    {
        sent = coop::StoreSendLive(e.route, e.target, (unsigned int)MSG_ITEM_MOVE, b, true);
        AddrCount(MSG_ITEM_MOVE, sent ? kAddrColLive : kAddrColNoSlot);
    }
    if (sent && ownSent) coop::ItemOwnerNoteCarried("ITEM_MOVE", m.owner);
    if (sent && bagSent) coop::ItemOwnerNoteRowsCarried(coopbag::BagRowsMarked(m.bag), coopbag::BagRowsUnknown(m.bag));
    return sent;
}
/* The same ITEM_MOVE to every game but the one `exceptKey` names - the area holder's ground word to the other games after it served
   that game's TAKE / PUT. The road is cooplive::ItemExceptRoad's; *whyOut its kExceptWhy* (kExceptWhyOk with false = the send
   itself failed); *planOut the plan. */
bool SendItemMoveExcept(const coop::ItemMoveMsg& m, unsigned int exceptKey, int* whyOut, cooplive::ExceptPlan* planOut)
{
    const cooplive::ExceptPlan e = cooplive::ItemExceptRoad(exceptKey, coop::LinkPeerSlot(), CharRoadNow(), coop::StoreLiveReady());
    if (planOut != 0) *planOut = e;
    if (whyOut != 0) *whyOut = e.why;
    if (e.road == cooplive::kAddrNone) return false;
    return ItemSendOnPlan(m, e, whyOut);
}
/* An ITEM_MOVE on a STORED plan - the same road, route and excepted slot as the message it follows; not sent (kExceptWhyRoadChanged)
   while that road is down. */
bool SendItemMoveOnPlan(const coop::ItemMoveMsg& m, const cooplive::ExceptPlan& plan, int* whyOut)
{
    const int w = cooplive::ExceptReplayWhy(plan.road, coop::StoreLiveReady(), g_transport != 0 && g_transport->State() == LINK_UP);
    if (whyOut != 0) *whyOut = w;
    if (w != cooplive::kExceptWhyOk) return false;
    return ItemSendOnPlan(m, plan, whyOut);
}

/* par1 (docs/design-loot2.md rev 2, 0.4). Reliable, on channel 0 - the SAME ordered channel as MSG_ITEM_MOVE, so a holder's
   move made after it read a box for its answer reaches the asker after the answer. The payload is built and checked by
   items.cpp through src/common/paritywire.h; this layer only carries it. */
/* M7b slice 2: the ASK to the holder of the sector it names (coop::ItemAreaWriterSlot; none known -> the session peer on the
   session road, else not sent - counted noOwner, the next parity round asks again); the ANSWER back to the asker; a PUSH (a box
   this game writes, newly filled) to every game covering its sector. */
bool SendParityReq(const std::vector<char>& bytes, int sx, int sy)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;
    if (bytes.empty()) return true;
    const int to = cooplive::ItemAddrDecide(cooplive::kItemToArea, false, 0u, coop::ItemAreaWriterSlot(sx, sy), false, 0u, coop::LinkPeerSlot());
    const bool ok = ItemSendTo(MSG_PARITY_REQ, to, &bytes[0], bytes.size(), 0u, 0);
    if (ok) AddrCount(MSG_PARITY_REQ, kAddrColToOwner);
    return ok;
}
bool SendParityBox(const std::vector<char>& bytes, unsigned int askerKey)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;
    if (bytes.empty()) return true;
    return ItemSendTo(MSG_PARITY_BOX, ItemToAsker(true, askerKey), &bytes[0], bytes.size(), 0u, 0);
}
bool SendParityPush(const std::vector<char>& bytes, int sx, int sy)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;
    if (bytes.empty()) return true;
    return ItemSendArea(MSG_PARITY_BOX, cooplive::AreaKey(sx, sy), -1, &bytes[0], bytes.size());
}
/* par1: decoded, counted and acted on in items.cpp (ParityNoteReq / ParityNoteBox), which refuses a malformed payload whole. */
void OnParityReq(const Message& m) { coop::ParityNoteReq(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), m.peer); }   /* M7b slice 2: the asker, for the answers */
void OnParityBox(const Message& m) { coop::ParityNoteBox(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), m.peer); }   /* fold 1: the sender - a box replacement only from its writer */
/* T-619: TOWN_PRICES from the world's price source (townprice::PriceSource) - on the session link to its peer, and through the world
   server to every other admitted game. The bytes are src/common/townprices.h's; items.cpp decodes, counts and files them
   (TownPricesNote, which takes a copy only from the source - townprice::TakeFrom). */
bool SendTownPrices(const std::vector<char>& bytes)
{
    if (g_transport == 0 || g_transport->State() != LINK_UP || bytes.empty()) return false;
    return g_transport->Send(0, MSG_TOWN_PRICES, &bytes[0], bytes.size(), CH_RELIABLE);
}
/* WORLD_EXCEPT the session peer's slot while the session link is up and that slot is known (the link brings it the table), else
   WORLD. 0 = not sent, 1 = WORLD, 2 = WORLD_EXCEPT. */
int SendTownPricesLive(const std::vector<char>& bytes)
{
    if (bytes.empty() || !coop::StoreLiveReady()) return 0;
    const int peer = (g_transport != 0 && g_transport->State() == LINK_UP) ? coop::LinkPeerSlot() : -1;
    if (peer >= 0) return coop::StoreSendLive(cooplive::kRouteWorldExcept, (unsigned int)peer, (unsigned int)MSG_TOWN_PRICES, bytes, true) ? 2 : 0;
    return coop::StoreSendLive(cooplive::kRouteWorld, 0u, (unsigned int)MSG_TOWN_PRICES, bytes, true) ? 1 : 0;
}
void OnTownPrices(const Message& m) { coop::TownPricesNote(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), m.peer); }

// E22a / decision 38 (approved) - MSG_ITEM_MOVE: ONE ITEM MOVE on a character the SENDER owns, applied to our ghost of it.
// APPLIED IMMEDIATELY, not queued, and that differs from CLOTHING/APPEARANCE on purpose: those are snapshots
// that must wait for the local copy to settle, while this is an ordered stream of deltas whose whole meaning is
// the order they arrive in. A malformed one is REFUSED rather than half-applied - a partial item move would put
// an item the owner does not have into a slot nobody can name.
static void OnItemMoveBody(const Message& m, const coophold::ReqTag* hdTag)
{
    coop::ItemMoveMsg im;
    size_t at = 0;
    unsigned int u = 0, xr = 0, yr = 0, qr = 0;
    if (!GetU32(m.payload, 0, &u) || m.payload.size() < 5) { ErrorLog("[net] malformed ITEM_MOVE (too short) - REFUSED"); return; }
    im.uid = u;
    at = 4;
    im.op = (int)(unsigned char)m.payload[at]; ++at;
    if (im.op < 0 || im.op > 4) { ErrorLog("[net] ITEM_MOVE with unknown op " + N(im.op) + " - REFUSED"); return; }   /* items9: op 4 CHARGES */
    if (!GetStr(m.payload, &at, &im.section)) { ErrorLog("[net] malformed ITEM_MOVE (section) - REFUSED"); return; }
    if (!GetU32(m.payload, at, &xr) || !GetU32(m.payload, at + 4, &yr) || !GetU32(m.payload, at + 8, &qr))
    { ErrorLog("[net] malformed ITEM_MOVE (slot) - REFUSED"); return; }
    im.x = (int)xr; im.y = (int)yr; im.quantity = (int)qr; at += 12;
    if (im.op == 0)
    {
        unsigned int qual = 0, chg = 0, fn = 0, lv = 0, uq = 0;
        if (!GetStr(m.payload, &at, &im.baseSid) || !GetStr(m.payload, &at, &im.companySid)
            || !GetStr(m.payload, &at, &im.materialSid) || !GetStr(m.payload, &at, &im.colorSid)
            || !GetU32(m.payload, at, &qual) || !GetU32(m.payload, at + 4, &chg)
            || !GetU32(m.payload, at + 8, &fn) || !GetU32(m.payload, at + 12, &lv) || !GetU32(m.payload, at + 16, &uq))
        { ErrorLog("[net] malformed ITEM_MOVE (item record) - REFUSED"); return; }
        std::memcpy(&im.quality, &qual, 4); std::memcpy(&im.charges, &chg, 4);
        im.functionKind = (int)fn; im.level = (int)lv; im.unique = (int)uq;
        if (im.baseSid.empty()) { ErrorLog("[net] ITEM_MOVE add with no base data sid - REFUSED"); return; }
        // E22c: the five u32s above were read at fixed offsets and `at` was never advanced past them, which
        // was harmless while nothing followed the item record. Something follows it now.
        at += 20;
    }
    // E22c (P6e): uid 0 means a storage box and the key MUST be there. A missing or empty one is refused
    // whole rather than applied to nothing - an unnamed box is not a box we can find.
    if (im.uid == 0)
    {
        if (!GetStr(m.payload, &at, &im.boxKey) || im.boxKey.empty())
        { ErrorLog("[net] ITEM_MOVE with owner uid 0 and no container key - REFUSED"); return; }
        // E22c-2 (P7n): the instance id beside it. AN EMPTY ONE IS NOT MALFORMED - most boxes have none -
        // but a field that is not there at all is, because every protocol-41 sender writes it.
        if (!GetStr(m.payload, &at, &im.boxId))
        { ErrorLog("[net] ITEM_MOVE with owner uid 0 and no container id field - REFUSED"); return; }
    }
    /* P7a fold 1: the op-2 base sid, if the sender put one there. OPTIONAL BY CONSTRUCTION - a sender that
       did not append it, or an entry whose fields could not be read, leaves this empty and the receiver's
       (0,0) test then stands aside. A failed read is NOT a refusal: there is nothing malformed about a
       message that ends where every op-2 message used to end. */
    /* inv6 (protocol 71): an ADD of a marked item ends with an 'OWN1' block. Cut or invalid: REFUSED (a stolen item must not
       arrive clean without anyone knowing); no block: a clean item. */
    /* items9 (protocol 78): ...then an optional BAG1 block before it (a holder's GROUND ADD of a pack). Anything else left over, or a
       cut block: REFUSED whole. */
    if (im.op == 0 && at < m.payload.size())
    {
        size_t te = 0;
        const int tw = coopistat::DecodeAddTail(&m.payload[0], m.payload.size(), at, &im.bag, &im.owner, &te);
        if (tw != coopistat::kTailOk)
        {
            if (tw == coopistat::kTailBadOwner) coop::ItemOwnerNoteBadRecv();
            ErrorLog("[net] ITEM_MOVE add with " + std::string(coopistat::TailWhy(tw)) + " after its record - REFUSED");
            return;
        }
        at = te;
    }
    /* items9 (protocol 78): op 4 CHARGES - the uses left and the base sid, LAST; missing, cut or bad: REFUSED. */
    if (im.op == 4)
    {
        size_t te = 0;
        const int tw = (at < m.payload.size()) ? coopistat::DecodeChargesTail(&m.payload[0], m.payload.size(), at, &im.charges, &im.baseSid, &te)
                                             : coopistat::kTailCut;
        if (tw != coopistat::kTailOk) { ErrorLog("[net] ITEM_MOVE charges with " + std::string(coopistat::TailWhy(tw)) + " - REFUSED"); return; }
        at = te;
    }
    if (im.op == 2 && at < m.payload.size() && !coopbag::BagPathAt(&m.payload[0], m.payload.size(), at)) { std::string s2; if (GetStr(m.payload, &at, &s2)) im.baseSid = s2; }
    /* bag23 part 2 (protocol 81): the optional pack path, LAST - the move is inside a pack. Cut or out of range: REFUSED. */
    if (at < m.payload.size() && coopbag::BagPathAt(&m.payload[0], m.payload.size(), at))
    {
        size_t pe = 0;
        if (coopbag::DecodeBagPath(&m.payload[0], m.payload.size(), at, &im.bagPath, &pe) != coopbag::kBagOk)
        { ErrorLog("[net] ITEM_MOVE with a pack path block that is cut or out of range - REFUSED"); return; }
        at = pe;
    }
    if (hdTag != 0) { im.rqtHas = 1; im.rqtId = hdTag->reqId; im.rqtSlot = hdTag->slot; }   /* P105 build 2 */
    coop::ApplyItemMove(im, m.peer);   /* M7b slice 2: the sender - refused unless it owns the character / writes the box */
}
/* P105 build 2 (protocol 126): an ITEM_MOVE whose op byte has the high bit set ends with the 'RQT1' request tag (coophold) - read off
   the end, the bit cleared, and the rest read exactly as before. A tag that is not whole REFUSES the message. */
void OnItemMove(const Message& m)
{
    if (m.payload.size() >= 5 && (((unsigned char)m.payload[4]) & coophold::kMoveTagFlag) != 0)
    {
        coophold::ReqTag tg;
        if (!coophold::TakeReqTag(&m.payload[0], m.payload.size(), &tg))
        { ErrorLog("[net] ITEM_MOVE with a request tag that is cut or not 'RQT1' - REFUSED"); return; }
        Message c = m;
        c.payload.resize(c.payload.size() - coophold::kReqTagLen);
        if (c.payload.size() < 5) { ErrorLog("[net] malformed ITEM_MOVE (nothing before its request tag) - REFUSED"); return; }
        c.payload[4] = (char)(unsigned char)(((unsigned char)c.payload[4]) & 0x7F);
        OnItemMoveBody(c, &tg);
        return;
    }
    OnItemMoveBody(m, 0);
}

// E22b / decision 38 part (2). The 0x2A record fields are written by both of the messages below, in the same
// order and by the same code, so a request's GIVE payload and a confirmation's TAKE payload cannot drift apart.
static void PutItemFields(std::vector<char>* b, const std::string& baseSid, const std::string& companySid,
                          const std::string& materialSid, const std::string& colorSid,
                          float quality, float charges, int functionKind, int level, int unique)
{
    PutStr(b, baseSid); PutStr(b, companySid); PutStr(b, materialSid); PutStr(b, colorSid);
    unsigned int q = 0, ch = 0;
    std::memcpy(&q, &quality, 4); std::memcpy(&ch, &charges, 4);
    PutU32(b, q); PutU32(b, ch);
    PutU32(b, (unsigned int)functionKind);
    PutU32(b, (unsigned int)level);
    PutU32(b, (unsigned int)unique);
}
static bool GetItemFields(const std::vector<char>& p, size_t* at, std::string* baseSid, std::string* companySid,
                          std::string* materialSid, std::string* colorSid,
                          float* quality, float* charges, int* functionKind, int* level, int* unique)
{
    unsigned int qual = 0, chg = 0, fn = 0, lv = 0, uq = 0;
    if (!GetStr(p, at, baseSid) || !GetStr(p, at, companySid) || !GetStr(p, at, materialSid) || !GetStr(p, at, colorSid))
        return false;
    if (!GetU32(p, *at, &qual) || !GetU32(p, *at + 4, &chg) || !GetU32(p, *at + 8, &fn)
        || !GetU32(p, *at + 12, &lv) || !GetU32(p, *at + 16, &uq)) return false;
    *at += 20;
    std::memcpy(quality, &qual, 4); std::memcpy(charges, &chg, 4);
    *functionKind = (int)fn; *level = (int)lv; *unique = (int)uq;
    return true;
}

bool SendItemRequest(const coop::ItemRequestMsg& r)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 2: the item road */
    std::vector<char> b;
    PutU32(&b, r.id);
    b.push_back((char)(unsigned char)((r.dir == coophold::kDirHold || r.dir == coophold::kDirLand) ? r.dir : (r.dir != 0 ? 1 : 0)));   /* P105 build 2: 2 HOLD / 3 LAND */
    PutU32(&b, r.ownerUid);
    PutStr(&b, r.ownerSection);
    PutU32(&b, (unsigned int)r.ownerX);
    PutU32(&b, (unsigned int)r.ownerY);
    PutU32(&b, (unsigned int)r.quantity);
    PutU32(&b, r.takerUid);
    PutStr(&b, r.takerSection);
    PutU32(&b, (unsigned int)r.takerX);
    PutU32(&b, (unsigned int)r.takerY);
    /* cell1 fold (review-cell1 R3, protocol 82): for EVERY direction - a TAKE names the record it wants. */
    PutItemFields(&b, r.baseSid, r.companySid, r.materialSid, r.colorSid,
                  r.quality, r.charges, r.functionKind, r.level, r.unique);
    // E22c (P6e), as in SendItemMove: the owner half is a STORAGE BOX when its uid is 0, and its instance id
    // is the LAST thing in the payload so the give-fields block keeps the position protocol 37 gave it.
    if (r.ownerUid == 0) { PutStr(&b, r.ownerBoxKey); PutStr(&b, r.ownerBoxId); }   /* E22c-2 (P7n): key, then the id beside it */
    if (r.takerUid == 0) PutStr(&b, r.takerBoxKey);   /* T-164 211 (protocol 98): the taker BOX's key (empty = no taker) */
    // E35 (P6o) / decision 41: AFTER the optional container key, so the key keeps the position protocol 38
    // gave it and this block is simply two more fields at the end. Both are UNCONDITIONAL - a trade flag of 0
    // is a real answer ("this is not a trade") and a reader that had to guess whether the fields are present
    // would be guessing about the difference between a purchase and a theft.
    b.push_back((char)(unsigned char)(r.trade == 1 ? 1 : (r.trade == 2 ? 2 : 0)));
    PutU32(&b, (unsigned int)r.price);
    /* T-164 B4-4 (protocol 92): a shop trade's 'SHR1' block, right after the trade block (src/common/shopwire.h; 'SHR3' since T-1 B5,
       protocol 121 - the stock kind, home or caravan). One that will not encode is not sent - a shop request without it would be
       applied as an ordinary box move. */
    if (r.shop.has != 0 && !coopshop::EncodeReq(&b, r.shop))
    { ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + ": its shop block would not encode - NOT SENT"); return false; }
    /* inv3a (protocol 70): a GIVE of a BACKPACK carries what is inside it, LAST, and only when there is something. The rows were
       read and capped by the giver (ItBagRowsOf), so an encode that still refuses is a defect - and it sends nothing rather than
       a pack without its contents. */
    if (!r.bag.empty() && !coopbag::EncodeBagRows(&b, r.bag))
    { ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + ": its backpack rows would not encode - NOT SENT"); return false; }
    /* inv6 (protocol 71): a GIVE of a MARKED item carries the owner identity, LAST. */
    bool ownSent = false;
    if (r.dir != 0 && r.owner.kind != 0)
    {
        std::vector<char> ob;
        if (coopmark::EncodeOwner(&ob, r.owner)) { b.insert(b.end(), ob.begin(), ob.end()); ownSent = true; }
        else coop::ItemOwnerNoteBadBlock();
    }
    /* bag23 part 2 (protocol 81): the owner half INSIDE A PACK - its path, LAST. One that will not encode is not sent. */
    if (r.ownerBag.has != 0 && !coopbag::EncodeBagPath(&b, r.ownerBag))
    { ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + ": its pack path would not encode - NOT SENT"); return false; }
    /* P105 build 2 (protocol 126): a HOLD / LAND ends with its 'HLD1' trailer, LAST of all (announced by the dir byte) */
    if (r.dir == coophold::kDirHold || r.dir == coophold::kDirLand)
    { coophold::HoldTail ht; ht.holdId = r.holdId; ht.how = r.holdHow; ht.slot = r.holdSlot; coophold::PutHoldTail(&b, ht); }
    /* M7b slice 2: a character's request to its OWNER (the owner table); a box's, a shop's or the ground's to the WRITER of its key
       (coop::ItemRequestTargetSlot: a player-owned box's owner, else the area holder). The game it went to is remembered by the id:
       its answer is taken only from that game, and PLACED / REVOKE / the shop tail go back to it. */
    int to = cooplive::kAddrOwnerNone;
    unsigned int stickyKey = 0u;
    bool held = false;
    if (ItemAskedStickyKey(r.id, &stickyKey))
    {   /* fold 1: a request RE-SENT under an id already asked (a ground PUT keeps its first id) goes to the game it was asked of
           until that game refuses it - only then to the current writer - so two holders never both serve one PUT.
           re-check fold (MED): a held game that has LEFT (out of the world roster, and for the session peer its session link down
           too) releases the hold here - its answer can never come, and the drop would otherwise stay unconfirmed for good. The
           small chance that it served the PUT before leaving is T-423's design (ground items across a holder change). */
        const int asked = ItemToAsker(true, stickyKey);
        const bool sessionUp = g_transport != 0 && g_transport->State() == LINK_UP;
        const bool isPeer = stickyKey == 0u || (asked >= 0 && asked == coop::LinkPeerSlot());
        const int inWorld = (asked >= 0) ? coop::StoreRosterSlotInWorld(asked) : -1;
        const bool gone = isPeer ? (!sessionUp && inWorld == 0) : (inWorld == 0);
        if (!gone) { to = asked; held = true; ++g_itemReSentToAsked; }
        else { ItemAskedRefusedBy(r.id, stickyKey); ++g_itemAskedGoneReleased; }
    }
    if (held) {}
    else if (r.ownerUid != 0)
    {
        std::map<unsigned int, unsigned int>::const_iterator own = g_owner.find(r.ownerUid);
        const bool have = own != g_owner.end() && g_localOwned.find(r.ownerUid) == g_localOwned.end();
        to = cooplive::ItemAddrDecide(cooplive::kItemToChar, have, have ? own->second : 0u, -1, false, 0u, coop::LinkPeerSlot());
    }
    else to = cooplive::ItemAddrDecide(cooplive::kItemToArea, false, 0u, coop::ItemRequestTargetSlot(r), false, 0u, coop::LinkPeerSlot());
    unsigned int askedKey = 0u;
    const bool sent = ItemSendTo(MSG_ITEM_REQUEST, to, &b[0], b.size(), r.ownerUid, &askedKey);
    if (sent) { ItemAskedNote(r.id, askedKey); AddrCount(MSG_ITEM_REQUEST, kAddrColToOwner); }
    if (sent && ownSent) coop::ItemOwnerNoteCarried("ITEM_REQUEST", r.owner);
    if (sent && !r.bag.empty()) coop::ItemOwnerNoteRowsCarried(coopbag::BagRowsMarked(r.bag), coopbag::BagRowsUnknown(r.bag));   /* inv6 phase 2; review-inv6p2 LOW (request) */
    return sent;
}

bool SendItemConfirm(const coop::ItemConfirmMsg& c)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 2: the item road */
    std::vector<char> b;
    PutU32(&b, c.id);
    b.push_back((char)(unsigned char)(c.ok != 0 ? (c.landHas != 0 ? coophold::kConfirmOkLand : 1) : 0));   /* P105 build 2: 2 = ok with an 'LND1' trailer */
    PutU32(&b, (unsigned int)c.quantity);
    if (c.ok != 0)
        PutItemFields(&b, c.baseSid, c.companySid, c.materialSid, c.colorSid,
                      c.quality, c.charges, c.functionKind, c.level, c.unique);
    // E35 (P6o): what the HOLDER settled, at the end and unconditional for the same reason as on the request.
    b.push_back((char)(unsigned char)(c.trade == 1 ? 1 : (c.trade == 2 ? 2 : 0)));
    PutU32(&b, (unsigned int)c.price);
    /* T-164 B4-4 (protocol 92): the answer to a shop request - the 'SHC1' block, right after the trade block (before GND1). */
    if (c.shop.has != 0 && !coopshop::EncodeCf(&b, c.shop))
    { ErrorLog("[net] ITEM_CONFIRM " + N((long long)c.id) + ": its shop block would not encode - NOT SENT"); return false; }
    /* inv5 (protocol 72): a refusal of a ground request says why - a 'GND1' block, the only thing after the trade block on ok 0. */
    if (c.ok == 0 && c.reason != 0) { PutU32(&b, coopground::kGroundReasonTag); b.push_back((char)(unsigned char)c.reason); }
    /* inv3a (protocol 70): a TAKE of a BACKPACK carries what is inside it, LAST, only on ok 1 and only when there is something. */
    if (c.ok != 0 && !c.bag.empty() && !coopbag::EncodeBagRows(&b, c.bag))
    { ErrorLog("[net] ITEM_CONFIRM " + N((long long)c.id) + ": its backpack rows would not encode - NOT SENT"); return false; }
    /* inv6 (protocol 71): a TAKE (ok 1) of a MARKED item carries the owner identity, LAST. */
    bool ownSent = false;
    if (c.ok != 0 && c.owner.kind != 0)
    {
        std::vector<char> ob;
        if (coopmark::EncodeOwner(&ob, c.owner)) { b.insert(b.end(), ob.begin(), ob.end()); ownSent = true; }
        else coop::ItemOwnerNoteBadBlock();
    }
    /* P105 build 2 (protocol 126): a LAND's answer ends with where the writer put the item - the 'LND1' trailer, LAST of all */
    if (c.ok != 0 && c.landHas != 0) { coophold::LandTail lt; lt.where = c.landWhere; lt.x = c.landX; lt.y = c.landY; coophold::PutLandTail(&b, lt); }
    const bool sent = ItemSendTo(MSG_ITEM_CONFIRM, ItemToAsker(c.toHave != 0, c.toKey), &b[0], b.size(), 0u, 0);   /* M7b slice 2: back to the asker */
    if (sent && ownSent) coop::ItemOwnerNoteCarried("ITEM_CONFIRM", c.owner);
    if (sent && c.ok != 0 && !c.bag.empty()) coop::ItemOwnerNoteRowsCarried(coopbag::BagRowsMarked(c.bag), coopbag::BagRowsUnknown(c.bag));   /* inv6 phase 2; review-inv6p2 LOW (confirm) */
    return sent;
}

// E22b-2 (P6b). Two one-line answers that close the two holes review-p5y HIGH-3 named: a TAKE the taker
// could not complete, and a GIVE the giver could not complete. Both are RELIABLE - a lost one turns into the
// owner's 10 s timeout, which rolls the TAKE back and leaves the GIVE standing.
bool SendItemPlaced(unsigned int id, int ok)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 2: the item road */
    std::vector<char> b;
    PutU32(&b, id);
    b.push_back((char)(unsigned char)(ok != 0 ? 1 : 0));
    return ItemSendTo(MSG_ITEM_PLACED, ItemToAskedOf(id), &b[0], b.size(), 0u, 0);   /* M7b slice 2: to the game request `id` was asked of */
}

/* M7b slice 4 (C3) + DOOR_STATE (C4) - T-197; protocol 121. THE SIDE MESSAGES ON THE CHARACTER STREAM'S ROAD (ownerroute.h
   SideRouteOf): one road per send - the notebook while CharRoadNow names it, the session link only while it does not. A message about
   this game's own character goes to every game covering its sector (ItemSendArea: AREA by the sector it is in now and, when it
   differs, the one its stream last named; WORLD with no sector, counted sideNoArea); an addressed one to one player (ItemSendTo: by
   SLOT, the session peer on the session link while that is its road); BUILD's PLACE / STATE / farm to every game (WORLD), its
   HELP_WORK / HAND_ACK to one player (SendBuildToSlot - by SLOT, the raw session peer while the slot is unknown or the notebook is
   down: fold 2 D1). Each counts one of viaSession /
   viaLive / noOwner / noSlot in addr.<say|stats|crime|bounty|talk|build|door>. MAIN THREAD. */
bool SideRoadOpen() { return CharRoadOpen(); }
/* M7b slice 4 FOLD 1 (review 2026-10-02 F4): an AREA / WORLD side message takes RELATION's road rule (liverelay.h SideRoadDecide):
   the notebook alone (AREA, or WORLD) once the session peer is proven reachable there or with no session link; until then the session
   link to the session peer AND the notebook WORLD_EXCEPT its slot (an AREA message widened, counted), so a third game on the notebook
   hears it - exactly once each. The peer's slot unknown: the session link only (counted). True when it was SENT (fold 2, D2 -
   ownerroute.h SideSpreadSent): with the session link in the spread, the session send's result; else the notebook's. */
static bool SideSendSpread(MsgType type, int areaKey, int prevKey, bool world, const char* p, size_t n)
{
    if (p == 0 || n == 0) return false;
    const unsigned int at = world ? cooplive::kAreaTargetNone : cooplive::CharAreaTarget(areaKey, prevKey);
    if (!world && at == cooplive::kAreaTargetNone) ++g_sideNoArea;
    const cooplive::SidePlan sp = cooplive::SideRoadDecide(coop::StoreLiveReady(), g_transport != 0 && g_transport->State() == LINK_UP,
                                                           SessionPeerRelayOk(), coop::LinkPeerSlot(), at);
    if (!sp.session && !sp.live) return false;
    if (sp.slotUnknown) ++g_sideSlotUnknown;
    if (sp.widened) ++g_sideWidened;
    if (sp.session && sp.live) ++g_sideBoth;
    bool sessOk = false, liveOk = false;
    if (sp.session)
    {
        const bool ok = g_transport->Send(0, type, p, n, CH_RELIABLE);
        AddrCount(type, ok ? kAddrColSession : kAddrColNoSlot);
        sessOk = ok;
    }
    if (sp.live)
    {
        const std::vector<char> v(p, p + n);
        const bool ok = coop::StoreSendLive(sp.route, sp.target, (unsigned int)type, v, true);
        AddrCount(type, ok ? kAddrColLive : kAddrColNoSlot);
        liveOk = ok;
    }
    return cooplive::SideSpreadSent(sp.session, sessOk, sp.live, liveOk);   /* fold 2 (D2) */
}
static bool SideSendChar(MsgType type, unsigned int uid, const char* p, size_t n)
{
    std::map<unsigned int, int>::const_iterator last = g_charLastArea.find(uid);
    const int prev = (last != g_charLastArea.end()) ? last->second : -1;
    int key = coop::CharAreaKeyNow(uid);
    if (key < 0) key = prev;   /* unreadable now: the sector its stream last named */
    return SideSendSpread(type, key, prev, false, p, n);   /* fold 1 (F4) */
}
/* to the OWNER of `uid` (a BOUNTY addition or clear); none on record or this game's own -> not sent, counted noOwner once per uid */
static bool SideSendToOwnerOf(MsgType type, unsigned int uid, const char* p, size_t n, int* toOut, int* whyOut = 0)
{
    std::map<unsigned int, unsigned int>::const_iterator own = g_owner.find(uid);
    const bool have = uid != 0 && own != g_owner.end() && g_localOwned.find(uid) == g_localOwned.end();
    const int to = cooplive::ItemAddrDecide(cooplive::kItemToChar, have, have ? own->second : 0u, -1, false, 0u, coop::LinkPeerSlot());
    if (toOut != 0) *toOut = to;
    return ItemSendTo(type, to, p, n, uid, 0, whyOut);
}
static bool SideSendWorld(MsgType type, const char* p, size_t n)
{
    return SideSendSpread(type, -1, -1, true, p, n);   /* fold 1 (F4): RELATION's rule - every other game, once */
}

/* P3 (read-parity3 GAP 3).  Reliable: a speech line is an event, and a lost one is a bubble the copy
   never shows.  The detour counts what this refuses (sayTooLong); with no road it is simply not sent. */
bool SendSay(unsigned int uid, const char* text, size_t len)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 4: the character stream's road, not the session link alone */
    std::vector<char> b;
    if (!coopsay::EncodeSay(&b, uid, text, len)) return false;
    return SideSendChar(MSG_SAY, uid, &b[0], b.size());   /* M7b slice 4: AREA - the speaker's sector */
}
/* P3 - MSG_SAY: refused whole when it does not decode; applied on the main thread from the drain. */
void OnSay(const Message& m)
{
    if (coop::EngineWritesBlocked()) { coop::SayNoteDroppedBlocked(); return; }   /* P3-b: the drain's own guard, restated */
    unsigned int uid = 0;
    std::string text;
    const int r = m.payload.empty() ? coopsay::kSayDecodeTooShort
                                    : coopsay::DecodeSay(&m.payload[0], m.payload.size(), &uid, &text);
    if (r != coopsay::kSayDecodeOk)
    {
        coop::SayNoteMalformed();
        DebugLog("[net] <- SAY malformed (reason " + N((long long)r) + ") - ignored");
        return;
    }
    coop::ApplyRemoteSay(uid, text, m.peer);
}

/* S1 (read-stats).  Reliable: a changed value is sent once, and the ~30 s resend is the repair, not the
   delivery. */
bool SendStats(unsigned int uid, const unsigned int* raw44)
{
    if (CharRoadNow() == cooplive::kCharRoadNone || raw44 == 0) return false;   /* M7b slice 4: the character stream's road */
    std::vector<char> b;
    coopstats::EncodeStatsMsg(&b, uid, raw44);
    return SideSendChar(MSG_STATS, uid, &b[0], b.size());   /* M7b slice 4: AREA - the character's sector */
}
/* S1 - MSG_STATS: refused whole when it does not decode (truncated, or a NaN / infinite float); only the
   uid's owner may send it; applied on the main thread from the drain. */
void OnStats(const Message& m)
{
    unsigned int uid = 0;
    unsigned int raw[coopstats::kStatsCount];
    int badIndex = -1;
    const int r = m.payload.empty() ? coopstats::kStatsDecodeTooShort
                                    : coopstats::DecodeStatsMsg(&m.payload[0], m.payload.size(), &uid, raw, &badIndex);
    if (r != coopstats::kStatsDecodeOk)
    {
        coop::StatsNoteBadBlock();
        return;
    }
    if (!RemoteMayWrite(uid, m.peer)) return;   // commitment 3: only the uid's owner reports its stats
    coop::ApplyRemoteStats(uid, raw, "STATS");
}

/* crime3 (T275 / F894).  Reliable: a crime is set and cleared once each; a lost one is a theft nobody sees or a copy
   that stays "committing" until the next change. */
bool SendCrime(unsigned int uid, const coopcrime::CrimeState& s)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 4: the character stream's road */
    std::vector<char> b;
    if (!coopcrime::EncodeCrime(&b, uid, s)) return false;
    return SideSendWorld(MSG_CRIME, &b[0], b.size());   /* M7b slice 4 (manager 2026-10-02): WORLD - a change goes once, so every game hears it; a game that gets its copy later gets the state after that SPAWN (fold 1 F5) */
}
/* M7b slice 4 fold 1 (F5): the state that follows a SPAWN - on exactly the road and route that SPAWN took (CharSend's last send), so it
   reaches exactly the games the SPAWN reached, right behind it on the same ordered channel. */
static bool SendAfterSpawn(MsgType type, unsigned int uid, const std::vector<char>& b)
{
    if (!g_charLastSent.valid || g_charLastSent.uid != uid || b.empty()) return false;
    bool ok = false;
    if (g_charLastSent.session) ok = g_transport != 0 && g_transport->Send(0, type, &b[0], b.size(), CH_RELIABLE);
    else ok = coop::StoreSendLive(g_charLastSent.route, g_charLastSent.target, (unsigned int)type, b, true);
    AddrCount(type, ok ? (g_charLastSent.session ? kAddrColSession : kAddrColLive) : kAddrColNoSlot);
    return ok;
}
bool SendCrimeAfterSpawn(unsigned int uid, const coopcrime::CrimeState& s)
{
    std::vector<char> b;
    if (!coopcrime::EncodeCrime(&b, uid, s)) return false;
    return SendAfterSpawn(MSG_CRIME, uid, b);
}
/* crime3 - MSG_CRIME: refused whole when it does not decode; only the uid's owner may send it (checked in ApplyRemoteCrime);
   queued for the K2 safe point, where the copy is written. */
void OnCrime(const Message& m)
{
    if (coop::EngineWritesBlocked()) { coop::CrimeNoteDroppedBlocked(); return; }
    unsigned int uid = 0;
    coopcrime::CrimeState s;
    const int r = m.payload.empty() ? coopcrime::kCrimeDecodeTooShort
                                    : coopcrime::DecodeCrime(&m.payload[0], m.payload.size(), &uid, &s);
    if (r != coopcrime::kCrimeDecodeOk)
    {
        coop::CrimeNoteMalformed();
        DebugLog("[net] <- CRIME malformed (reason " + N((long long)r) + ") - ignored");
        return;
    }
    coop::ApplyRemoteCrime(uid, s, m.peer);
}

/* crime5 (F899).  Reliable: a list is sent on a change (and re-sent every ~30 s while non-empty), an addition once. */
bool SendBounty(unsigned int uid, unsigned char kind, const std::vector<coopbounty::BountyEntry>& list, bool* mayDrop)
{
    if (mayDrop != 0) *mayDrop = false;   /* M7b slice 4 fold 2 (D4): kept unless BountyUnsentDrops says otherwise */
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 4: the character stream's road */
    std::vector<char> b;
    if (!coopbounty::EncodeBounty(&b, uid, kind, list)) return true;   /* not encodable: dropped, never retried forever */
    /* M7b slice 4: the owner's list goes WORLD (every game; re-sent every ~30 s); a copy's addition or clear by SLOT to the character's OWNER */
    if (cooplive::SideRouteOf(cooplive::kInnerBounty, (int)kind, false) == cooplive::kSideRouteWorld)
    {
        const bool lok = SideSendWorld(MSG_BOUNTY, &b[0], b.size());   /* the owner's LIST: every game */
        if (!lok && mayDrop != 0) *mayDrop = cooplive::BountyUnsentDrops(true, SideRoadOpen(), cooplive::kAddrWhyOk, cooplive::kAddrOwnerNone);
        return lok;
    }
    int to = cooplive::kAddrOwnerUnknown, why = cooplive::kAddrWhyOk;
    const bool ok = SideSendToOwnerOf(MSG_BOUNTY, uid, &b[0], b.size(), &to, &why);
    if (ok || to == cooplive::kAddrOwnerNone) return true;   /* no owner on record: dropped (counted noOwner), never retried forever */
    if (mayDrop != 0) *mayDrop = cooplive::BountyUnsentDrops(false, SideRoadOpen(), why, to);   /* fold 2 (D4) */
    return false;
}
/* crime5 - MSG_BOUNTY: refused whole when it does not decode; the ownership test and the write happen in crime.cpp. */
void OnBounty(const Message& m)
{
    if (coop::EngineWritesBlocked()) { coop::BountyNoteDroppedBlocked(); return; }
    unsigned int uid = 0; unsigned char kind = 0;
    coopbounty::BountyList list;
    const int r = m.payload.empty() ? coopbounty::kBountyDecodeTooShort
                                    : coopbounty::DecodeBounty(&m.payload[0], m.payload.size(), &uid, &kind, &list);
    if (r != coopbounty::kBountyDecodeOk)
    {
        coop::BountyNoteMalformed();
        DebugLog("[net] <- BOUNTY malformed (reason " + N((long long)r) + ") - ignored");
        return;
    }
    coop::ApplyRemoteBounty(uid, kind, list, m.peer);
}

/* arrest1 (docs/design-arrest.md 3).  Reliable: sent once per ended carry. */
bool SendCarryBreak(unsigned int body, unsigned int carrier)
{
    std::vector<char> b;
    PutU32(&b, body);
    PutU32(&b, carrier);
    return SendToOwner(MSG_CARRY_BREAK, carrier, &b[0], b.size());   /* M7b slice 1: to the CARRIER's owner (ApplyRemoteCarryBreak) */
}
/* arrest1 - MSG_CARRY_BREAK: exactly 8 bytes; the ownership tests happen in spawn.cpp (ApplyRemoteCarryBreak). */
void OnCarryBreak(const Message& m)
{
    if (coop::EngineWritesBlocked()) { coop::CarryBreakNoteDropped(); return; }
    if (m.payload.size() != 8) { coop::CarryBreakNoteDropped(); DebugLog("[net] <- CARRY_BREAK malformed - ignored"); return; }
    unsigned int body = 0, carrier = 0;
    std::memcpy(&body, &m.payload[0], 4);
    std::memcpy(&carrier, &m.payload[4], 4);
    coop::ApplyRemoteCarryBreak(body, carrier, m.peer);
}

/* arrest2 (docs/design-arrest.md 3).  Reliable: sent once per caging seen on this game. */
bool SendPrison(const cooprison::PrisonMsg& m, unsigned int askerPeer)
{
    std::vector<char> b;
    if (!cooprison::EncodePrison(&b, m)) return true;   /* not encodable: dropped, never retried forever */
    /* M7b slice 1: REFUSED and BED REFUSED answer the game that asked; IN, RELEASE, DEATH and BED IN go to the character's owner */
    if (m.kind == cooprison::kPrisonRefused || m.kind == cooprison::kPrisonBedRefused) return ReplyTo(askerPeer, MSG_PRISON, &b[0], b.size());
    /* P42: SHACKLE LOCK / CAGE LOCK about our own character is its owner's word - to every other game, on the character stream's
       road; about another game's character it is a request to that owner */
    if (cooprison::PrisonIsLockKind(m.kind) && g_localOwned.find(m.uid) != g_localOwned.end())
    {
        if (CharRoadNow() == cooplive::kCharRoadNone) return false;
        return SideSendWorld(MSG_PRISON, &b[0], b.size());
    }
    return SendToOwner(MSG_PRISON, m.uid, &b[0], b.size());
}
/* names1.  Reliable: the owner's character name - after every SPAWN (SendSpawn below -> coop::NameSendWithSpawn) and on the
   owner's rename (spawn.cpp NameTick, from the Character::setName hook). The caller cuts the name to 64 bytes. */
bool SendName(unsigned int uid, const std::string& name, bool afterSpawn)
{
    std::vector<char> b;
    if (!coopname::EncodeName(&b, uid, name)) return true;   /* not encodable: dropped, never retried forever */
    if (afterSpawn) return SendAfterSpawn(MSG_NAME, uid, b);   /* behind its SPAWN, to exactly the games that SPAWN reached */
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* the character stream's road */
    return SideSendWorld(MSG_NAME, &b[0], b.size());   /* a rename: WORLD (ownerroute.h SideRouteOf) */
}
/* slave1.  Reliable: the owner's SlaveStateEnum - after every SPAWN (SendSpawn below -> coop::SlaveSendWithSpawn) and on a
   change the owner's engine made (spawn.cpp SlaveTick, from the setSlaveState / periodicUpdate hooks). */
bool SendSlave(unsigned int uid, int state, unsigned int ownerUid, bool afterSpawn)
{
    std::vector<char> b;
    if (!coopslave::EncodeSlave(&b, uid, state, ownerUid)) return true;   /* not encodable: dropped, never retried forever */
    if (afterSpawn) return SendAfterSpawn(MSG_SLAVE, uid, b);   /* behind its SPAWN, to exactly the games that SPAWN reached */
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* the character stream's road */
    return SideSendWorld(MSG_SLAVE, &b[0], b.size());   /* a change: WORLD (ownerroute.h SideRouteOf) */
}
/* P11.  Reliable: one encoded MSG_CAPTURE / MSG_CAPTURE_DONE (items.cpp builds and counts them; false = not sent). */
bool SendCapture(const char* p, size_t n, unsigned int victimUid)
{
    return SendToOwner(MSG_CAPTURE, victimUid, p, n);   /* M7b slice 1: to the victim's owner */
}
bool SendCaptureDone(const char* p, size_t n, unsigned int askerPeer)
{
    return ReplyTo(askerPeer, MSG_CAPTURE_DONE, p, n);   /* M7b slice 1: back to the captor's game that asked */
}
/* P11 f3.  Reliable: one encoded MSG_CAPTURE_PLACED (items.cpp CapApplyDone). */
bool SendCapturePlaced(const char* p, size_t n, unsigned int victimUid)
{
    return SendToOwner(MSG_CAPTURE_PLACED, victimUid, p, n);   /* M7b slice 1: to the victim's owner, who holds the taken rows */
}
/* P104 fix.  Reliable: one MSG_SHOT (combat.cpp RangedEventsDrain; false = link down or not encodable - counted there). */
bool SendShot(const coopshot::ShotMsg& m)
{
    std::vector<char> b;
    if (!coopshot::EncodeShot(&b, m)) return false;
    return SendToOwner(MSG_SHOT, m.victimUid, &b[0], b.size());   /* M7b slice 1: to the victim's owner */
}
/* recruit1.  Reliable: one MSG_HIRE (hire.cpp; queued there, applied at the K2 safe point on arrival). */
bool SendHire(const coophire::HireMsg& m, unsigned int toPeer)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* the character stream's road */
    std::vector<char> b;
    if (!coophire::EncodeHire(&b, m)) return true;   /* not encodable: dropped, never retried forever */
    /* a REQ to the person's OWNER; an OK / NO / DONE back to the asker's key; a DONE with no asker (this game's own hire) to every game */
    if (m.kind == coophire::kHireReq) return SendToOwner(MSG_HIRE, m.uid, &b[0], b.size());
    if (cooplive::SideRouteOf(cooplive::kInnerHire, toPeer == kHireNoAsker ? 1 : 0, false) == cooplive::kSideRouteWorld)
        return SideSendWorld(MSG_HIRE, &b[0], b.size());
    return ReplyTo(toPeer, MSG_HIRE, &b[0], b.size());
}
/* P26 stages 1-3.  Reliable: one MSG_TALK (speech.cpp; a conversation step is an event). False when the character stream's road is closed, the other side's owner is unknown or not in the world, or it does
   not encode (the caller counts and ends its side). */
bool SendTalk(const cooptalk::TalkMsg& m)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 4: the character stream's road */
    std::vector<char> b;
    if (!cooptalk::EncodeTalk(&b, m)) return false;
    /* M7b slice 4: to the other side of the conversation - the NPC's owner when the NPC is not ours, else the target's owner */
    std::map<unsigned int, unsigned int>::const_iterator npc = g_owner.find(m.npcUid), tgt = g_owner.find(m.targetUid);
    const bool npcMine = m.npcUid != 0 && g_localOwned.find(m.npcUid) != g_localOwned.end();
    const bool tgtMine = m.targetUid != 0 && g_localOwned.find(m.targetUid) != g_localOwned.end();
    const bool haveNpc = m.npcUid != 0 && npc != g_owner.end(), haveTgt = m.targetUid != 0 && tgt != g_owner.end();
    const int to = cooplive::TalkToSlot(npcMine, haveNpc, haveNpc ? npc->second : 0u, tgtMine, haveTgt, haveTgt ? tgt->second : 0u, coop::LinkPeerSlot());
    return ItemSendTo(MSG_TALK, to, &b[0], b.size(), 0u, 0);
}
/* heal1.  Reliable: sent when a medic here raised a copy's treatment (rate-limited per copy in spawn.cpp TreatTick). */
bool SendTreat(const cooptreat::TreatMsg& m)
{
    std::vector<char> b;
    if (!cooptreat::EncodeTreat(&b, m)) return true;   /* not encodable: dropped, never retried forever */
    return SendToOwner(MSG_TREAT, m.uid, &b[0], b.size());   /* M7b slice 1: to the patient's owner */
}
/* build1-b.  Reliable: sent once per new piece this game's own player faction placed (build.cpp BdOnNewPiece).
   build1-c: also each STATE of such a piece (build.cpp BdMaybeSendState, main thread). */
bool SendBuild(const coopbuild::BuildMsg& m)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 4: the character stream's road */
    std::vector<char> b;
    if (!coopbuild::EncodeBuild(&b, m)) return true;   /* not encodable: dropped, never retried forever */
    return SideSendWorld(MSG_BUILD, &b[0], b.size());   /* M7b slice 4: WORLD - every game, the notebook stamps the sender (P106) */
}
int BuildSlotTarget(int slot)   /* M7b slice 4 fold 2 (D1): ownerroute.h BuildAddrTarget */
{
    const bool sessionUp = g_transport != 0 && g_transport->State() == LINK_UP;
    const int targetInWorld = (slot >= 0) ? coop::StoreRosterSlotInWorld(slot) : -1;
    return cooplive::BuildAddrTarget(slot, coop::LinkPeerSlot(), coop::StoreLiveReady(), sessionUp, targetInWorld);
}
int PlayerAddrOfKey(unsigned int key) { return cooplive::AddrOwnerSlotOf(key, coop::LinkPeerSlot()); }   /* fold 2 (D1): -2 kept */
bool SendBuildToSlot(const coopbuild::BuildMsg& m, int slot)   /* M7b slice 4 fold 1 (F6); fold 2 (D1): the raw session peer when needed */
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;
    const int to = BuildSlotTarget(slot);
    if (to == cooplive::kAddrOwnerNone) return false;   /* nobody reachable now: the caller holds it */
    std::vector<char> b;
    if (!coopbuild::EncodeBuild(&b, m)) return true;   /* not encodable: dropped, never retried forever */
    return ItemSendTo(MSG_BUILD, to, &b[0], b.size(), 0u, 0);
}
/* par16.  Reliable, on MSG_BUILD: the writer's farm state (farm.cpp FarmDrain) and a held game's harvest work (FarmTick).
   par16 fold #8: meant for EVERY linked game - M7b slice 4: on the notebook road it goes WORLD (every game), on the session link
   to its one peer; the receiver accepts a FARM only while its own ladder names another game the writer, and a FARM_OP only while it
   is the writer itself (farm.cpp FarmDrain). Counted in addr.build. */
bool SendFarm(const coopfarm::FarmMsg& m)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 4: the character stream's road */
    std::vector<char> b;
    if (!coopfarm::EncodeFarm(&b, m)) return true;   /* not encodable: dropped, never retried forever */
    return SideSendWorld(MSG_BUILD, &b[0], b.size());   /* M7b slice 4: WORLD */
}
/* build1-b - MSG_BUILD: refused whole when it does not decode. It is a whole state and is NOT dropped during a load
   (the arrival queue holds it); build.cpp queues it and creates the copy at the K2 safe point. build1-c: a STATE is held
   (the latest per key) and written onto the copy at the same safe point. */
void OnBuild(const Message& m)
{
    if (!m.payload.empty() && coopfarm::IsFarmKind(&m.payload[0], m.payload.size()) != 0)
    {   /* par16: kinds 6 / 7 are the farm's, 10 a worker's step on a production building, decoded by farmwire.h (DecodeBuild refuses them) */
        coopfarm::FarmMsg fm;
        const int fr = coopfarm::DecodeFarm(&m.payload[0], m.payload.size(), &fm);
        if (fr != coopfarm::kFarmDecodeOk)
        {
            coop::BuildNoteBad();
            DebugLog("[net] <- BUILD farm kind malformed (reason " + N((long long)fr) + ") - ignored");
            return;
        }
        coop::FarmNoteRecv(fm, m.peer);
        return;
    }
    coopbuild::BuildMsg bm;
    const int r = m.payload.empty() ? coopbuild::kBuildDecodeTooShort : coopbuild::DecodeBuild(&m.payload[0], m.payload.size(), &bm);
    if (r != coopbuild::kBuildDecodeOk)
    {
        coop::BuildNoteBad();
        DebugLog("[net] <- BUILD malformed (reason " + N((long long)r) + ") - ignored");
        return;
    }
    coop::BuildNoteRecv(bm, m.peer);
}
/* names1 - MSG_NAME: refused whole when it does not decode; the ownership test, the pending keep and the setter call are in
   spawn.cpp NameNoteRecv. Not dropped during a load: a name is a state (NameNoteRecv keeps it pending while writes are blocked). */
void OnName(const Message& m)
{
    unsigned int uid = 0;
    std::string name;
    const int r = m.payload.empty() ? coopname::kNameDecodeTooShort : coopname::DecodeName(&m.payload[0], m.payload.size(), &uid, &name);
    if (r != coopname::kNameDecodeOk)
    {
        coop::NameNoteBad(r == coopname::kNameDecodeTooLong);
        DebugLog("[net] <- NAME malformed (reason " + N((long long)r) + ") - ignored");
        return;
    }
    coop::NameNoteRecv(uid, name, m.peer);
}
/* slave1 - MSG_SLAVE: refused whole when it does not decode; the ownership test, the hold and the setter call (K2 safe
   point) are in spawn.cpp SlaveNoteRecv / SlaveSafePointDrain. Not dropped during a load: a slave state is a state. */
void OnSlave(const Message& m)
{
    unsigned int uid = 0;
    int state = -1;
    unsigned int ownerUid = coopslave::kSlaveOwnerUnknown;   /* P11 */
    const int r = m.payload.empty() ? coopslave::kSlaveDecodeTooShort : coopslave::DecodeSlave(&m.payload[0], m.payload.size(), &uid, &state, &ownerUid);
    if (r != coopslave::kSlaveDecodeOk)
    {
        coop::SlaveNoteBad();
        DebugLog("[net] <- SLAVE malformed (reason " + N((long long)r) + ") - ignored");
        return;
    }
    coop::SlaveNoteRecv(uid, state, ownerUid, m.peer);
}
/* P11 - MSG_CAPTURE / MSG_CAPTURE_DONE: decoded and queued in items.cpp (CaptureOnRequest / CaptureOnDone), applied at the K2
   safe point. Not dropped during a load: the queue waits for the safe point. */
void OnCapture(const Message& m)
{
    coop::CaptureOnRequest(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), m.peer);
}
void OnCaptureDone(const Message& m)
{
    coop::CaptureOnDone(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), m.peer);
}
void OnCapturePlaced(const Message& m)   /* P11 f3: queued in items.cpp, applied at the K2 safe point */
{
    coop::CaptureOnPlaced(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), m.peer);
}
/* P104 fix - MSG_SHOT: our character was struck by a bolt the other game's character fired (the bolt exists only there). Refused
   whole when it does not decode; we must drive the victim and the sender must drive the shooter (RemoteMayWrite). Counted, logged
   and queued in combat.cpp; played at the K2 safe point through the engine's own MedicalSystem::addWound. */
void OnShot(const Message& m)
{
    coopshot::ShotMsg s;
    const int dc = coopshot::DecodeShot(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), &s);
    if (dc != coopshot::kShotDecodeOk)
    {
        ErrorLog("[net] malformed SHOT (decode " + N((long long)dc) + ", " + N((long long)m.payload.size()) + " bytes) from peer "
                 + N((long long)m.peer) + " - ignored");
        coop::ShotOnNet(0u, 0u, 0, false, 3);
        return;
    }
    int refusal = 0;
    if (!IsUidMine(s.victimUid)) refusal = 1;
    else if (!RemoteMayWrite(s.shooterUid, m.peer)) refusal = 2;
    coop::ShotOnNet(s.victimUid, s.shooterUid, s.rec, s.onPurpose, refusal);
}
/* T-327 (protocol 111) - MSG_EFFECT. g_effViaRelay: did the message SessionDispatchQueued is dispatching come through the notebook
   (m.peer is the player key on both roads, so the road is read there, from the raw sender). */
static bool g_effViaRelay = false;
static int EffectSendOn(int road, unsigned int key, const std::vector<char>& b)
{
    if (b.empty()) return 0;
    if (road == cooffect::kRoadLive)
        return coop::StoreSendLive(cooplive::kRouteSlot, cooplive::RelayPeerSlot(key), (unsigned int)MSG_EFFECT, b, true) ? road : 0;
    if (road == cooffect::kRoadSession && g_transport != 0)
        return g_transport->Send(0, MSG_EFFECT, &b[0], b.size(), CH_RELIABLE) ? road : 0;
    return 0;
}
int SendEffectRequest(unsigned int targetUid, const std::vector<char>& b)
{
    unsigned int key = 0;
    if (!UidOwnerPeer(targetUid, &key)) return 0;
    const bool sessUp = g_transport != 0 && g_transport->State() == LINK_UP;
    return EffectSendOn(cooffect::EffectRequestRoad(coop::StoreLiveReady(), sessUp, key, coop::LinkPeerSlot(), SessionPeerRelayOk()), key, b);
}
int SendEffectAnswer(unsigned int toKey, bool viaRelay, const std::vector<char>& b)
{
    const bool sessUp = g_transport != 0 && g_transport->State() == LINK_UP;
    return EffectSendOn(cooffect::EffectAnswerRoad(viaRelay, coop::StoreLiveReady(), sessUp), toKey, b);
}
/* T-354 (protocol 130) - MSG_NOT_SHOWN. Sent from OnSpawn while SessionDispatchQueued dispatches that SPAWN, so g_effViaRelay
   names the road it came by, and the answer goes back on it (cooffect::EffectAnswerRoad - the same rule as an EFFECT answer). */
int SendNotShownToOwner(unsigned int uid, unsigned int ownerKey)
{
    unsigned char raw[coopuid::kNotShownSize];
    const long long total = coop::MirrorPeerRefusals();
    const unsigned int n = coopuid::EncodeNotShown(raw, (unsigned int)sizeof(raw), uid, coop::MirrorRefusalReasonForWire(),
                                                   total > 0xFFFFFFFFLL ? 0xFFFFFFFFu : (unsigned int)total);
    int sent = 0;
    if (n > 0)
    {
        std::vector<char> b((const char*)raw, (const char*)raw + n);
        const bool sessUp = g_transport != 0 && g_transport->State() == LINK_UP;
        const int road = cooffect::EffectAnswerRoad(g_effViaRelay, coop::StoreLiveReady(), sessUp);
        if (road == cooffect::kRoadLive)
            sent = coop::StoreSendLive(cooplive::kRouteSlot, cooplive::RelayPeerSlot(ownerKey), (unsigned int)MSG_NOT_SHOWN, b, true) ? road : 0;
        else if (road == cooffect::kRoadSession && g_transport != 0)
            sent = g_transport->Send(0, MSG_NOT_SHOWN, &b[0], b.size(), CH_RELIABLE) ? road : 0;
    }
    coop::NotShownNoteSent(uid, ownerKey, sent != 0);
    return sent;
}
/* A LOST COPY COMES BACK (src/common/lostcopy.h). This game's engine put away its copy of another game's character (spawn.cpp
   NotifyDespawn: an engine unload of a copy, not the owner's own UNLOAD or DESPAWN), or the owner streams a MOVE for a uid this game
   holds no copy of (replicate.cpp ApplyRemoteMove): the uid is booked with its owner and its last spot - never a uid whose SPAWN made
   no row here (g_lostRefused, from OnSpawn). A look at a row - at the event itself, and once a second for every row (LostCopyTick) -
   asks the owner to send the character again while that spot reads as loaded here by the engine's own zone byte
   (ZoneBuildingsInHereTri), within the row's visit budget (lostcopy::LostLookDecide); "no copy here" is LostCopyHereNow, the same
   test that writes the BACK line. A SPAWN that makes the copy marks the row HERE and writes BACK at once (LostCopyArrived, from
   OnSpawn), so a look right after it never asks. The row stays while the copy is back and its
   spot (the owner's last streamed spot) is still loaded; it goes after that, when the owner record is gone, at the owner's UNLOAD /
   DESPAWN, at a world teardown and when the session's owner records are cleared. The owner sends the state to this game alone
   (coop::WorldsyncResendAsk) and then answers (OnResend); an answer counts only from the row's owner. Every line names its uid and
   is written for that uid's first 12 events, then every 100th (lostcopy::LostUidLogThis); a row forgotten or dropped writes
   one line naming its uid and the reason, the first 20 such lines in the process (LostGoneLine).
   lostCopy[noted,asks,askFailed,answers,refused,back,dropped,full,open,refusedSkip,answerForeign,forgot] and
   resendIn[asks,answered,answerFailed,noRoad,malformed] on the [net] REPORT line. MAIN THREAD. */
static lostcopy::LostBook g_lost;
static std::map<unsigned int, unsigned long long> g_lostRefused;                 /* uid whose SPAWN was refused for good -> its mark's stamp */
static std::deque<std::pair<unsigned int, unsigned long long> > g_lostRefusedOrder;   /* marks in order: past kLostRefusedMax the oldest goes, only while its stamp is current */
static unsigned long long g_lostRefusedStamp = 0;
const size_t kLostRefusedMax = 8192;
static std::map<unsigned int, long long> g_lostUidLines;   /* uid -> its events so far (the per-uid log budget) */
const size_t kLostUidLinesMax = 4096;
static long long g_lcNoted = 0, g_lcAsks = 0, g_lcAskFailed = 0, g_lcAnswers = 0, g_lcRefused = 0, g_lcBack = 0, g_lcDropped = 0;
static long long g_lcRefusedSkip = 0, g_lcAnswerForeign = 0, g_lcForgot = 0;
static long long g_rsInAsks = 0, g_rsInAnswered = 0, g_rsInAnswerFailed = 0, g_rsInNoRoad = 0, g_rsInMalformed = 0;
static bool LostLineDue(unsigned int uid)
{
    std::map<unsigned int, long long>::iterator it = g_lostUidLines.find(uid);
    if (it == g_lostUidLines.end())
    {
        if (g_lostUidLines.size() >= kLostUidLinesMax) return false;   /* counted on the REPORT line all the same */
        it = g_lostUidLines.insert(std::make_pair(uid, 0LL)).first;
    }
    return lostcopy::LostUidLogThis(++it->second) != 0;
}
static int ResendSendOn(int road, unsigned int key, const std::vector<unsigned char>& raw)
{
    if (raw.empty()) return 0;
    std::vector<char> b((const char*)&raw[0], (const char*)&raw[0] + raw.size());
    if (road == cooffect::kRoadLive)
        return coop::StoreSendLive(cooplive::kRouteSlot, cooplive::RelayPeerSlot(key), (unsigned int)MSG_RESEND, b, true) ? road : 0;
    if (road == cooffect::kRoadSession && g_transport != 0)
        return g_transport->Send(0, MSG_RESEND, &b[0], b.size(), CH_RELIABLE) ? road : 0;
    return 0;
}
static void LostRowErase(size_t i) { g_lost.rows.erase(g_lost.rows.begin() + (std::ptrdiff_t)i); }
/* T-601 D2: a row forgotten or dropped is written with its uid and the reason - the first kLostGoneLinesMax such lines in the
   process, then counted only (lostCopy forgot / dropped on the REPORT line). Called after the counter is raised. */
static long long g_lostGoneLines = 0;
const long long kLostGoneLinesMax = 20;
static void LostGoneLine(unsigned int uid, const char* what)
{
    if (g_lostGoneLines >= kLostGoneLinesMax) return;
    ++g_lostGoneLines;
    DebugLog("[net] lost copy uid=" + N(uid) + " " + std::string(what ? what : "?") + " (lostCopy forgot " + N(g_lcForgot)
             + ", dropped " + N(g_lcDropped) + ")");
}
/* The copy is here: the uid table holds a live row for the uid under that uid - a copy made this frame counts before anything has
   looked at it. Address compares only; nothing is read through the object. */
static bool LostCopyHereNow(unsigned int uid)
{
    const void* o = coop::SpawnedRawObject(uid);
    return o != 0 && coop::FindSpawnedUid(o) == uid;
}
/* One look at row i; true when the row was removed. */
static bool LostLook(size_t i)
{
    unsigned int key = 0;
    const unsigned int uid = g_lost.rows[i].uid;
    const bool ownerOk = lostcopy::LostNoteAllowed(IsUidMine(uid) ? 1 : 0, UidOwnerPeer(uid, &key) ? 1 : 0, g_lostRefused.count(uid) != 0 ? 1 : 0) != 0;
    if (ownerOk && key != g_lost.rows[i].ownerKey) lostcopy::LostNote(&g_lost, uid, key, 0, 0.0f, 0.0f, 0.0f);   /* a new owner was never asked */
    lostcopy::LostRow& r = g_lost.rows[i];
    const bool haveCopy = LostCopyHereNow(uid);
    if (haveCopy) { float px = 0.0f, py = 0.0f, pz = 0.0f; if (coop::PuppetAuthorityPos(uid, &px, &py, &pz)) { r.x = px; r.y = py; r.z = pz; r.hasPos = 1; } }
    /* a running world with engine writes allowed (a copy asked for inside a load gate would be made into a world being torn down or
       built), and the engine's own "zone loaded" byte, which never reads a stale yes */
    const bool loaded = r.hasPos != 0 && coop::GameplayRunning() && !coop::EngineWritesBlocked() && coop::ZoneBuildingsInHereTri(r.x, r.y, r.z) == 1;
    const int a = lostcopy::LostLookDecide(haveCopy ? 1 : 0, ownerOk ? 1 : 0, loaded ? 1 : 0, r);
    if (haveCopy && r.state != lostcopy::kLcHere && (a == lostcopy::kLaHere || a == lostcopy::kLaDrop))
    {
        ++g_lcBack;
        if (LostLineDue(uid))
            DebugLog("[net] lost copy uid=" + N(uid) + " is BACK after " + N((long long)r.asksTotal) + " RESEND ask(s) ("
                     + N((long long)r.asksThisVisit) + " of this visit; lostCopy back " + N(g_lcBack) + ")");
    }
    if (a == lostcopy::kLaDrop)
    {
        ++g_lcDropped;
        LostGoneLine(uid, ownerOk ? "dropped - the copy is here and its visit is over"
                                  : "dropped - no other game's owner record allows asking for it (gone, this game's own, or its SPAWN refused here)");
        LostRowErase(i);
        return true;
    }
    int sent = 0;
    const int noRoadFirst = lostcopy::LostNoRoadFirst(r);
    if (a == lostcopy::kLaAsk)
    {
        std::vector<unsigned int> u(1, uid); std::vector<int> v(1, (int)lostcopy::kRvAsk); std::vector<unsigned char> raw;
        const bool sessUp = g_transport != 0 && g_transport->State() == LINK_UP;
        if (lostcopy::EncodeResend(&raw, lostcopy::kRsAsk, u, v))
            sent = ResendSendOn(cooffect::EffectRequestRoad(coop::StoreLiveReady(), sessUp, r.ownerKey, coop::LinkPeerSlot(), SessionPeerRelayOk()), r.ownerKey, raw);
        if (sent) ++g_lcAsks; else ++g_lcAskFailed;
        if ((sent || noRoadFirst) && LostLineDue(uid))   /* a road outage is written once */
            DebugLog("[net] -> RESEND ask uid=" + N(uid) + " to its owner (player key " + N(r.ownerKey) + ")" + std::string(sent ? "" : " NOT SENT (no road)")
                     + ": no copy here and it stands in an area loaded here (ask " + N((long long)((loaded && !r.loadedPrev) ? 1 : r.asksThisVisit + 1)) + " of "
                     + N((long long)lostcopy::kLostAsksPerVisit) + " this visit; lostCopy asks " + N(g_lcAsks) + ")");
    }
    lostcopy::LostLookApply(&r, loaded ? 1 : 0, a, sent);
    return false;
}
static void LostBookUid(unsigned int uid, bool hasPos, float x, float y, float z)
{
    unsigned int key = 0;
    if (uid == 0) return;
    if (LostCopyHereNow(uid)) return;   /* the uid table holds a live copy (one made this frame too): nothing is lost */
    const bool known = UidOwnerPeer(uid, &key);
    const bool refused = g_lostRefused.count(uid) != 0;
    if (!lostcopy::LostNoteAllowed(IsUidMine(uid) ? 1 : 0, known ? 1 : 0, refused ? 1 : 0))
    {
        if (refused && known && !IsUidMine(uid))
        {
            ++g_lcRefusedSkip;
            if (LostLineDue(uid))
                DebugLog("[net] lost copy uid=" + N(uid) + " NOT booked: this game made no row for its SPAWN (lostCopy refusedSkip " + N(g_lcRefusedSkip) + ")");
        }
        return;
    }
    const int at = lostcopy::LostFind(g_lost, uid);
    const bool wasHere = at >= 0 && g_lost.rows[(size_t)at].state == lostcopy::kLcHere;
    if (lostcopy::LostNote(&g_lost, uid, key, hasPos ? 1 : 0, x, y, z) == 1 || wasHere)
    {
        ++g_lcNoted;
        if (LostLineDue(uid))
            DebugLog("[net] lost copy uid=" + N(uid) + " of player key " + N(key) + (wasHere ? " LOST AGAIN in the same visit" : " booked")
                     + ": no copy here while its owner runs it" + std::string(hasPos ? "" : " (no spot known yet)") + " (lostCopy noted " + N(g_lcNoted) + ")");
    }
    const int i = lostcopy::LostFind(g_lost, uid);
    if (i >= 0) LostLook((size_t)i);
}
void LostCopyNote(unsigned int uid, bool hasPos, float x, float y, float z)
{
    if (uid != 0) g_lostRefused.erase(uid);   /* the copy existed here, so its SPAWN made a row after all */
    LostBookUid(uid, hasPos, x, y, z);
}
void LostCopyMove(unsigned int uid, float x, float y, float z) { LostBookUid(uid, true, x, y, z); }
void LostCopyRefusedHere(unsigned int uid, bool refused)
{
    if (uid == 0) return;
    if (!refused) { g_lostRefused.erase(uid); return; }
    const unsigned long long stamp = ++g_lostRefusedStamp;
    g_lostRefused[uid] = stamp;
    g_lostRefusedOrder.push_back(std::make_pair(uid, stamp));
    while (g_lostRefusedOrder.size() > kLostRefusedMax)
    {
        const std::pair<unsigned int, unsigned long long> old = g_lostRefusedOrder.front();
        g_lostRefusedOrder.pop_front();
        std::map<unsigned int, unsigned long long>::iterator m = g_lostRefused.find(old.first);
        if (m != g_lostRefused.end() && m->second == old.second) g_lostRefused.erase(m);   /* a newer mark of the same uid stays */
    }
    const int i = lostcopy::LostFind(g_lost, uid);
    if (i >= 0) { LostRowErase((size_t)i); ++g_lcForgot; LostGoneLine(uid, "forgotten - this game refused its SPAWN for good"); }
}
void LostCopyArrived(unsigned int uid)
{
    if (uid == 0 || g_lost.rows.empty() || !LostCopyHereNow(uid)) return;
    const int i = lostcopy::LostFind(g_lost, uid);
    if (i < 0) return;
    lostcopy::LostRow& r = g_lost.rows[(size_t)i];
    if (!lostcopy::LostArrived(&r)) return;
    ++g_lcBack;
    if (LostLineDue(uid))
        DebugLog("[net] lost copy uid=" + N(uid) + " is BACK after " + N((long long)r.asksTotal) + " RESEND ask(s) ("
                 + N((long long)r.asksThisVisit) + " of this visit; lostCopy back " + N(g_lcBack) + ")");
}
void LostCopyMoveSeen(unsigned int uid)
{
    if (g_lost.rows.empty()) return;
    const int i = lostcopy::LostFind(g_lost, uid);
    if (i >= 0 && lostcopy::LostHereMoveSeen(&g_lost.rows[(size_t)i]) && LostLineDue(uid))
        DebugLog("[net] lost copy uid=" + N(uid) + " has stayed back for " + N((long long)lostcopy::kLostHereMovesReset)
                 + " of its owner's MOVEs - this visit's ask count starts again");
}
int CharStreamSlotFor(int askerSlot)
{
    const int road = CharRoadNow();
    if (road == cooplive::kCharRoadLive) return askerSlot >= 0 ? askerSlot : -2;
    if (road == cooplive::kCharRoadSession) return (askerSlot < 0 || askerSlot == coop::LinkPeerSlot()) ? -1 : -2;
    return -2;
}
bool LostCopyAsked(unsigned int uid)
{
    const int i = lostcopy::LostFind(g_lost, uid);
    return i >= 0 && (g_lost.rows[(size_t)i].state == lostcopy::kLcAsked || g_lost.rows[(size_t)i].state == lostcopy::kLcAnswered);
}
void LostCopyForget(unsigned int uid, const char* why)
{
    const int i = lostcopy::LostFind(g_lost, uid);
    if (i < 0) return;
    LostRowErase((size_t)i); ++g_lcForgot;
    LostGoneLine(uid, (std::string("forgotten - ") + (why ? why : "?")).c_str());
}
void LostCopyForgetAll()
{
    g_lcForgot += (long long)g_lost.rows.size();
    g_lost.rows.clear(); g_lostRefused.clear(); g_lostRefusedOrder.clear(); g_lostUidLines.clear();
}
void LostCopyTick()
{
    static long long s_lastSec = -1;
    if (g_lost.rows.empty()) return;
    const long long sec = (long long)CuNowSec();
    if (sec == s_lastSec) return;
    s_lastSec = sec;
    for (size_t i = 0; i < g_lost.rows.size(); ) { if (!LostLook(i)) ++i; }
}
/* THE RETURN CHECK (src/common/lostcopy.h ReturnLookDecide). MAIN THREAD, after the drain (SessionCatchupApplyTick). */
static std::vector<lostcopy::ReturnRow> g_rtBook;
static double g_rtSince = 0.0;
static long g_rtGenSeen = 0;
static long long g_rtEdges = 0, g_rtBooked = 0, g_rtFull = 0, g_rtAsks = 0, g_rtAskFailed = 0, g_rtAnswers = 0, g_rtHeard = 0, g_rtKept = 0;
static long long g_rtWithdrawn = 0, g_rtGaveUp = 0, g_rtDropped = 0, g_rtLogged = 0;
static std::map<int, double> g_rtAbsentSince;   /* owner slot -> when this link's world roster first read it absent since the return (cleared when it reads present) */
static std::set<int> g_rtGoneRun;               /* owner slots whose leave the return check handled as PLAYER_GONE's since the return */
static bool g_rtRosterSeen = false, g_rtOwnDue = false;
static double g_rtUpAt = 0.0;   /* the last look that saw this game's world link up (StoreLiveGen != 0): a return's link-down is measured from it */
static int g_rtOwnLooks = 0;
static long long g_rtOwnerGone = 0, g_rtOwnGaveUp = 0;
const int kRtOwnLooksMax = 30;
const long long kRtLogCap = 40;
static void RtLog(const std::string& s) { if (g_rtLogged >= kRtLogCap) return; ++g_rtLogged; DebugLog(s); }
static bool ReturnOwnerKey(unsigned int uid, unsigned int* key)
{
    if (IsUidMine(uid) || !UidOwnerPeer(uid, key) || !cooplive::IsRelayPeer(*key)) return false;
    const int me = coop::StoreMySlot();
    return me < 0 || (int)cooplive::RelayPeerSlot(*key) != me;
}
static void ReturnBook(long gen, double downSec)
{
    g_rtBook.clear();
    g_rtSince = CuNowSec();
    ++g_rtEdges;
    g_rtAbsentSince.clear(); g_rtGoneRun.clear(); g_rtRosterSeen = false; g_rtOwnLooks = 0; g_rtOwnAsked.clear();
    g_rtOwnDue = downSec >= cooppg::kPlayerGoneHoldSec + cooppg::kReturnAwayMarginSec;   /* the own-people listing: only after a link-down past the hold and its margin (a leave by the world server's rule); it waits for this link's roster */
    std::vector<unsigned int> pu; coop::PuppetUidsSnapshot(&pu);
    long long booked = 0;
    for (size_t i = 0; i < pu.size(); ++i)
    {
        unsigned int key = 0;
        if (!ReturnOwnerKey(pu[i], &key)) continue;
        if (g_rtBook.size() >= lostcopy::kReturnBookMax) { ++g_rtFull; continue; }
        lostcopy::ReturnRow r; r.uid = pu[i]; r.ownerKey = key; r.state = lostcopy::kRtWaiting; r.asks = 0; r.looksWaiting = 0; r.verdict = 0; r.unsent = 0;
        g_rtBook.push_back(r); ++booked;
    }
    g_rtBooked += booked;
    RtLog("[net] RETURN CHECK: the world-server link is back (link " + N((long long)gen) + ") - " + N(booked) + " copies of other players' characters booked;"
          " each one its owner has not streamed to this game since is asked of its owner (RESEND) and withdrawn if the owner answers it is not announced"
          " (returnCheck edges " + N(g_rtEdges) + ", full " + N(g_rtFull) + "); this game's link was down " + N((long long)downSec) + " s: the people it runs are "
          + std::string(g_rtOwnDue ? "listed to the other in-world games once this link's roster is here" : "NOT listed (a link-down within the hold and its margin is no leave)"));
}
/* THE RETURN CHECK'S OWN PEOPLE: after a link-down past the hold and its margin, once this link's roster and player table are here,
   every NPC this game runs with a body here is listed to every other in-world game (a listed CHECK - the dual-run detector's road);
   this game's player characters and player-faction people are not listed (a final leave never takes them over). Only a LIVE answer
   gives a person up (the dual-run rule; RosterOwnReturnNote counts each first answer). Once per return; a player table that never
   comes is given up after kRtOwnLooksMax looks (logged). */
static void ReturnOwnList(int me)
{
    if (!g_rtOwnDue || !coop::StoreLiveReady() || coop::StoreRosterSlotInWorld(me) < 0 || coop::EngineWritesBlocked()) return;
    int sl[256], xs[256], ys[256];
    const int n = coop::PeerPlayerSectorsTS(sl, xs, ys, 256);
    if (n < 0)
    {
        if (++g_rtOwnLooks < kRtOwnLooksMax) return;
        g_rtOwnDue = false; ++g_rtOwnGaveUp;
        RtLog("[net] RETURN CHECK: no fresh table of this link's players after " + N((long long)g_rtOwnLooks) + " looks - the people this game runs are not listed to"
              " the other games at this return (returnOwn gaveUp " + N(g_rtOwnGaveUp) + ")");
        return;
    }
    g_rtOwnDue = false;
    std::set<int> others;
    for (int i = 0; i < n && i < 256; ++i) if (sl[i] >= 0 && sl[i] != me && coop::StoreRosterSlotInWorld(sl[i]) == 1) others.insert(sl[i]);
    std::vector<unsigned int> own;
    for (std::set<unsigned int>::const_iterator it = g_localOwned.begin(); it != g_localOwned.end() && own.size() < lostcopy::kReturnBookMax; ++it)
        if (coop::CharAreaKeyNow(*it) >= 0 && coop::CopyIsPlayerCharacter(*it) == 0) own.push_back(*it);
    if (!others.empty())
        for (size_t i = 0; i < own.size(); ++i)
        {
            g_rtOwnAsked.insert(own[i]);
            for (std::set<int>::const_iterator o = others.begin(); o != others.end(); ++o) g_roView[*o].recheck.insert(own[i]);
        }
    if (!others.empty()) g_rtOwnListed += (long long)own.size();
    RtLog("[net] RETURN CHECK: " + N((long long)(others.empty() ? 0 : own.size())) + " people this game runs listed to " + N((long long)others.size())
          + " other in-world game(s) - one another game answers it runs now at a higher generation (taken while this game was away) is given up to it"
          " (returnOwn listed " + N(g_rtOwnListed) + ")");
}
void ReturnCheckTick()
{
    const long gen = coop::StoreLiveGen();
    if (gen != 0)
    {
        const double upNow = CuNowSec();
        if (lostcopy::ReturnEdge(g_rtGenSeen, gen)) ReturnBook(gen, upNow - g_rtUpAt);   /* down from the last look that saw the old link up */
        g_rtGenSeen = gen;
        g_rtUpAt = upNow;
    }
    if (g_rtBook.empty() && !g_rtOwnDue) return;
    static double s_last = -1.0;
    const double now = CuNowSec();
    if (s_last >= 0.0 && now - s_last < 1.0 && now >= s_last) return;
    if (!coop::GameplayRunning()) return;
    const int me = coop::StoreMySlot();
    if (!g_rtRosterSeen)   /* the first look waits for this link's PLAYERS roster: an owner's presence is read from it */
    {
        if (me < 0 || coop::StoreRosterSlotInWorld(me) < 0) return;
        g_rtRosterSeen = true;
    }
    s_last = now;
    if (me >= 0) ReturnOwnList(me);
    if (g_rtBook.empty()) return;
    const bool roadUp = coop::StoreLiveReady();
    const bool blocked = coop::EngineWritesBlocked();
    for (size_t i = 0; i < g_rtBook.size(); ++i)   /* how long each owner has been out of this link's world roster, without a break */
    {
        const int os = (int)cooplive::RelayPeerSlot(g_rtBook[i].ownerKey);
        const int iw = coop::StoreRosterSlotInWorld(os);
        if (iw == 1) g_rtAbsentSince.erase(os);
        else if (iw == 0 && g_rtAbsentSince.find(os) == g_rtAbsentSince.end()) g_rtAbsentSince[os] = now;
    }
    std::set<int> goneSlots;
    std::vector<lostcopy::ReturnRow> next;
    std::map<unsigned int, std::vector<size_t> > askBy;   /* owner key -> the rows of `next` asked at this look */
    for (size_t i = 0; i < g_rtBook.size(); ++i)
    {
        lostcopy::ReturnRow r = g_rtBook[i];
        unsigned int key = 0;
        const int ownerSame = (ReturnOwnerKey(r.uid, &key) && key == r.ownerKey) ? 1 : 0;
        std::map<unsigned int, double>::const_iterator st = g_roLastStreamAt.find(r.uid);
        const int heard = (st != g_roLastStreamAt.end() && st->second > g_rtSince) ? 1 : 0;
        const int a = lostcopy::ReturnLookDecide(LostCopyHereNow(r.uid) ? 1 : 0, ownerSame, heard, roadUp ? 1 : 0, r);
        if (a == lostcopy::kRtaWithdraw && blocked) { next.push_back(r); continue; }   /* withdrawn at a look with engine writes allowed */
        if (a == lostcopy::kRtaAsk) { askBy[r.ownerKey].push_back(next.size()); next.push_back(r); continue; }
        if (a == lostcopy::kRtaWait) { lostcopy::ReturnLookApply(&r, a, 0, roadUp ? 1 : 0); next.push_back(r); continue; }
        const std::string who = "copy uid=" + N(r.uid) + " of player key " + N(r.ownerKey);
        if (a == lostcopy::kRtaDrop) { ++g_rtDropped; continue; }
        if (a == lostcopy::kRtaKeepHeard)
        {
            ++g_rtHeard;
            RtLog("[net] RETURN CHECK: " + who + " KEPT - its owner streamed it to this game since the return (returnCheck heard " + N(g_rtHeard) + ")");
            continue;
        }
        if (a == lostcopy::kRtaKeepAnswered)
        {
            ++g_rtKept;
            RtLog("[net] RETURN CHECK: " + who + " KEPT - its owner answered: " + std::string(lostcopy::ResendVerdictName(r.verdict)) + " (returnCheck kept " + N(g_rtKept) + ")");
            continue;
        }
        if (a == lostcopy::kRtaGiveUp)
        {
            const int os = (int)cooplive::RelayPeerSlot(r.ownerKey);
            std::map<int, double>::const_iterator ab = g_rtAbsentSince.find(os);
            if (g_rtGoneRun.find(os) == g_rtGoneRun.end()
                && lostcopy::ReturnGiveUpOwnerGone(coop::StoreRosterSlotInWorld(os), ab != g_rtAbsentSince.end() ? now - ab->second : 0.0,
                                                   cooppg::kPlayerGoneHoldSec + cooppg::kReturnAwayMarginSec) == 1)
            {
                if (blocked) { next.push_back(r); continue; }   /* that player's leave is handled at a look with engine writes allowed */
                goneSlots.insert(os);
                continue;
            }
            ++g_rtGaveUp;
            RtLog("[net] RETURN CHECK: " + who + " KEPT after " + N((long long)r.asks) + " unanswered ask(s) and " + N((long long)r.unsent) + " that found no road"
                  " - a copy that may be live is not taken away on silence"
                  " (returnCheck gaveUp " + N(g_rtGaveUp) + ")");
            continue;
        }
        /* kRtaWithdraw: the owner runs it but has not announced it to this game - its withdrawal went while this game was away */
        coop::SquadIdxOwnerWithdrew(r.uid, 1);   /* its recorded owner runs it nowhere here - out of the squad index, as the roster's withdrawal does */
        coop::ApplyRemoteUnload(r.uid); g_owner.erase(r.uid); g_copyGen.erase(r.uid); g_roLastStreamAt.erase(r.uid); g_roConflictCount.erase(r.uid);
        ++g_rtWithdrawn;
        RtLog("[net] RETURN CHECK: " + who + " WITHDRAWN - its owner answered it is not announced to this game: the owner's withdrawal went while this"
              " game was off the world-server link (returnCheck withdrawn " + N(g_rtWithdrawn) + ")");
    }
    const bool sessUp = g_transport != 0 && g_transport->State() == LINK_UP;
    for (std::map<unsigned int, std::vector<size_t> >::const_iterator k = askBy.begin(); k != askBy.end(); ++k)
    {
        for (size_t at = 0; at < k->second.size(); at += lostcopy::kResendMax)
        {
            std::vector<unsigned int> u; std::vector<int> v;
            for (size_t j = at; j < k->second.size() && j < at + lostcopy::kResendMax; ++j) { u.push_back(next[k->second[j]].uid); v.push_back((int)lostcopy::kRvAsk); }
            std::vector<unsigned char> raw;
            int sent = 0;
            if (lostcopy::EncodeResend(&raw, lostcopy::kRsAsk, u, v))
                sent = ResendSendOn(cooffect::EffectRequestRoad(coop::StoreLiveReady(), sessUp, k->first, coop::LinkPeerSlot(), SessionPeerRelayOk()), k->first, raw);
            if (sent) g_rtAsks += (long long)u.size(); else g_rtAskFailed += (long long)u.size();
            for (size_t j = at; j < k->second.size() && j < at + lostcopy::kResendMax; ++j) lostcopy::ReturnLookApply(&next[k->second[j]], lostcopy::kRtaAsk, sent ? 1 : 0, 1);
            RtLog("[net] RETURN CHECK: -> RESEND ask to player key " + N(k->first) + " for " + N((long long)u.size()) + " silent copies (first uid=" + N(u[0]) + ")"
                  + std::string(sent ? "" : " NOT SENT (no road)") + " (returnCheck asks " + N(g_rtAsks) + ", askFailed " + N(g_rtAskFailed) + ")");
        }
    }
    g_rtBook.swap(next);
    for (std::set<int>::const_iterator g = goneSlots.begin(); g != goneSlots.end(); ++g)
    {
        g_rtGoneRun.insert(*g);
        ++g_rtOwnerGone;
        std::map<int, double>::const_iterator ab = g_rtAbsentSince.find(*g);
        RtLog("[net] RETURN CHECK: player slot " + N((long long)*g) + " never answered and has been out of this link's world roster for "
              + N((long long)(ab != g_rtAbsentSince.end() ? now - ab->second : 0.0)) + " s, past the world server's hold and its margin: it left while this game was away (its"
              " PLAYER_GONE went only to the games connected then) - its leave is handled here as PLAYER_GONE's, with no NPC taken here (returnCheck ownerGone " + N(g_rtOwnerGone) + ")");
        std::vector<lostcopy::ReturnRow> keep;
        for (size_t i = 0; i < g_rtBook.size(); ++i) if ((int)cooplive::RelayPeerSlot(g_rtBook[i].ownerKey) != *g) keep.push_back(g_rtBook[i]);
        g_rtBook.swap(keep);
        coop::OnPlayerGone((unsigned int)*g, "the return check: that player left while this game was off the world link", 1);
    }
}
/* the owner's RESEND answer for a booked copy (only from the owner on record) */
static void ReturnCheckAnswer(unsigned int senderKey, const std::vector<unsigned int>& uids, const std::vector<int>& verdicts)
{
    if (g_rtBook.empty()) return;
    for (size_t i = 0; i < uids.size() && i < verdicts.size(); ++i)
        for (size_t j = 0; j < g_rtBook.size(); ++j)
            if (g_rtBook[j].uid == uids[i] && lostcopy::LostAnswerFromOwner(g_rtBook[j].ownerKey, senderKey)) { ++g_rtAnswers; lostcopy::ReturnAnswerApply(&g_rtBook[j], verdicts[i]); }
}
/* A stand-in leave's NPC this game would take (OnPlayerGone standIn): the real PLAYER_GONE went to the in-world games while this game
   was away and one of them may have taken it, so this game does not; the NPC is listed to every other in-world game (a listed CHECK)
   and the taker's answer re-keys the copy here (RosterApplyAnswer), which settles the wait. */
static void ReturnGoneListHeld(unsigned int uid, unsigned int goneSlot)
{
    const int me = coop::StoreMySlot();
    for (int sl = 0; sl < 256; ++sl)
        if (sl != me && sl != (int)goneSlot && coop::StoreRosterSlotInWorld(sl) == 1) g_roView[sl].recheck.insert(uid);
}
static std::string ReturnCheckCountsString()
{
    return " returnCheck[edges,booked,full,asks,askFailed,answers,heard,kept,withdrawn,gaveUp,dropped,open]=" + N(g_rtEdges) + "," + N(g_rtBooked) + "," + N(g_rtFull)
        + "," + N(g_rtAsks) + "," + N(g_rtAskFailed) + "," + N(g_rtAnswers) + "," + N(g_rtHeard) + "," + N(g_rtKept) + "," + N(g_rtWithdrawn) + "," + N(g_rtGaveUp)
        + "," + N(g_rtDropped) + "," + N((long long)g_rtBook.size())
        + " returnOwn[listed,yielded,kept,gaveUp,ownerGone]=" + N(g_rtOwnListed) + "," + N(g_rtOwnYielded) + "," + N(g_rtOwnKept) + "," + N(g_rtOwnGaveUp) + "," + N(g_rtOwnerGone);
}
std::string LostCopyCountsString()
{
    return " lostCopy[noted,asks,askFailed,answers,refused,back,dropped,full,open,refusedSkip,answerForeign,forgot]=" + N(g_lcNoted) + "," + N(g_lcAsks) + ","
        + N(g_lcAskFailed) + "," + N(g_lcAnswers) + "," + N(g_lcRefused) + "," + N(g_lcBack) + "," + N(g_lcDropped) + "," + N(g_lost.full) + ","
        + N((long long)g_lost.rows.size()) + "," + N(g_lcRefusedSkip) + "," + N(g_lcAnswerForeign) + "," + N(g_lcForgot)
        + " resendIn[asks,answered,answerFailed,noRoad,malformed]=" + N(g_rsInAsks) + "," + N(g_rsInAnswered) + "," + N(g_rsInAnswerFailed) + ","
        + N(g_rsInNoRoad) + "," + N(g_rsInMalformed) + ReturnCheckCountsString();
}
/* An ASK (the characters are ours) or an ANSWER (to our ask). The state goes to the asker alone on the character stream's road:
   LIVE SLOT when the stream rides the world road, the session link when it rides that and the asker is the session peer; with
   neither, every uid is answered NO ROAD. The answer goes back on the road the ask came by (cooffect::EffectAnswerRoad). */
void OnResend(const Message& m)
{
    int kind = 0; std::vector<unsigned int> uids; std::vector<int> verdicts;
    const int r = m.payload.empty() ? (int)lostcopy::kRsDecodeShort
        : lostcopy::DecodeResend((const unsigned char*)&m.payload[0], m.payload.size(), &kind, &uids, &verdicts);
    if (r != lostcopy::kRsDecodeOk)
    {
        ++g_rsInMalformed;
        if (cooplive::LiveLogThis(g_rsInMalformed)) DebugLog("[net] <- RESEND from player key " + N(m.peer) + " malformed (reason " + N((long long)r) + ") - ignored");
        return;
    }
    const unsigned int senderKey = OwnerKeyOf(m.peer);
    if (kind == lostcopy::kRsAnswer)
    {
        ReturnCheckAnswer(senderKey, uids, verdicts);   /* a copy asked about after this game's world link came back */
        for (size_t i = 0; i < uids.size(); ++i)
        {
            const int at = lostcopy::LostFind(g_lost, uids[i]);
            if (at >= 0 && !lostcopy::LostAnswerFromOwner(g_lost.rows[(size_t)at].ownerKey, senderKey))
            {
                ++g_lcAnswerForeign;
                if (LostLineDue(uids[i]))
                    DebugLog("[net] <- RESEND answer uid=" + N(uids[i]) + " from player key " + N(m.peer) + " IGNORED: not the owner this game has on record (lostCopy answerForeign " + N(g_lcAnswerForeign) + ")");
                continue;
            }
            ++g_lcAnswers;
            if (verdicts[i] != lostcopy::kRvSent) ++g_lcRefused;
            if (at >= 0) lostcopy::LostAnswerApply(&g_lost.rows[(size_t)at], verdicts[i]);
            if (LostLineDue(uids[i]))
                DebugLog("[net] <- RESEND answer uid=" + N(uids[i]) + " from player key " + N(m.peer) + ": " + std::string(lostcopy::ResendVerdictName(verdicts[i]))
                         + (at >= 0 ? "" : " (no row here any more)") + " (lostCopy answers " + N(g_lcAnswers) + ")");
        }
        return;
    }
    ++g_rsInAsks;
    const bool sessUp = g_transport != 0 && g_transport->State() == LINK_UP;
    const int askerSlot = cooplive::IsRelayPeer(senderKey) ? (int)cooplive::RelayPeerSlot(senderKey) : coop::LinkPeerSlot();
    int streamSlot = CharStreamSlotFor(askerSlot);
    if (streamSlot == -1 && !cooplive::IsSessionPeerKey(senderKey, coop::LinkPeerSlot())) streamSlot = -2;   /* the session link reaches the session peer only */
    const bool road = streamSlot != -2;
    std::vector<int> out;
    if (road) coop::WorldsyncResendAsk(streamSlot, askerSlot, uids, &out);
    else { out.assign(uids.size(), (int)lostcopy::kRvNoRoad); ++g_rsInNoRoad; }
    std::vector<unsigned char> raw;
    int sent = 0;
    if (lostcopy::EncodeResend(&raw, lostcopy::kRsAnswer, uids, out))
        sent = ResendSendOn(cooffect::EffectAnswerRoad(g_effViaRelay, coop::StoreLiveReady(), sessUp), m.peer, raw);
    if (sent) ++g_rsInAnswered; else ++g_rsInAnswerFailed;
    for (size_t i = 0; i < uids.size() && i < out.size(); ++i)
        if (LostLineDue(uids[i]))
            DebugLog("[net] <- RESEND ask uid=" + N(uids[i]) + " from player key " + N(m.peer) + ": " + std::string(lostcopy::ResendVerdictName(out[i]))
                     + std::string(out[i] != lostcopy::kRvSent ? "" : (streamSlot >= 0 ? " to that game alone (LIVE SLOT)" : " to that game alone (session link)"))
                     + "; answer " + std::string(sent ? "sent" : "NOT sent") + " (resendIn asks " + N(g_rsInAsks) + ")");
}
/* The owner: decoded here, counted and logged in spawn.cpp (NotShownOnNet); a uid this game does not run changes nothing. */
void OnNotShown(const Message& m)
{
    unsigned int uid = 0, theirs = 0; int reason = 0;
    const int r = m.payload.empty() ? (int)coopuid::kNsDecodeShort
        : coopuid::DecodeNotShown((const unsigned char*)&m.payload[0], (unsigned int)m.payload.size(), &uid, &reason, &theirs);
    coop::NotShownOnNet(r, uid, reason, theirs, m.peer, r == coopuid::kNsDecodeOk && IsUidMine(uid));
}
/* Decoded, checked, counted and applied in effect.cpp (EffectOnNet): a REQUEST names our character and the sender's eater; an
   ANSWER is the victim's owner's APPLIED / DONE / REFUSED for our eater's request. */
void OnEffect(const Message& m)
{
    coop::EffectOnNet(m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), m.peer, g_effViaRelay);
}
/* recruit1 - MSG_HIRE: refused whole when it does not decode; queued in arrival order, applied at the K2 safe point (hire.cpp). */
void OnHire(const Message& m)
{
    coophire::HireMsg h;
    const int r = m.payload.empty() ? coophire::kHireDecodeTooShort : coophire::DecodeHire(&m.payload[0], m.payload.size(), &h);
    if (r != coophire::kHireDecodeOk)
    {
        coop::HireNoteBad();
        DebugLog("[net] <- HIRE malformed (reason " + N((long long)r) + ") - ignored");
        return;
    }
    coop::HireNoteRecv(h, m.peer);
}
/* P26 stages 1-3 - MSG_TALK: refused whole when it does not decode; queued in arrival order, applied at the K2 safe point
   (speech.cpp TkSafePoint), which also holds it while the engine's writes are blocked. */
void OnTalk(const Message& m)
{
    cooptalk::TalkMsg tm;
    const int r = m.payload.empty() ? cooptalk::kTalkDecodeTooShort : cooptalk::DecodeTalk(&m.payload[0], m.payload.size(), &tm);
    if (r != cooptalk::kTalkDecodeOk)
    {
        coop::TalkNoteBad();
        DebugLog("[net] <- TALK malformed (reason " + N((long long)r) + ") - ignored");
        return;
    }
    coop::TalkNoteRecv(tm, m.peer);
}
/* heal1 - MSG_TREAT: refused whole when it does not decode; the ownership test and the write happen in spawn.cpp. */
void OnTreat(const Message& m)
{
    if (coop::EngineWritesBlocked()) { coop::TreatNoteDropped(); return; }
    cooptreat::TreatMsg tm;
    const int r = m.payload.empty() ? cooptreat::kTreatDecodeTooShort : cooptreat::DecodeTreat(&m.payload[0], m.payload.size(), &tm);
    if (r != cooptreat::kTreatDecodeOk)
    {
        coop::TreatNoteDropped();
        DebugLog("[net] <- TREAT malformed (reason " + N((long long)r) + ") - ignored");
        return;
    }
    coop::ApplyRemoteTreat(tm, m.peer);
}
/* arrest2 - MSG_PRISON: refused whole when it does not decode; the ownership test and the caging happen in spawn.cpp. */
void OnPrison(const Message& m)
{
    if (coop::EngineWritesBlocked()) { coop::PrisonNoteDropped(); return; }
    cooprison::PrisonMsg pm;
    const int r = m.payload.empty() ? cooprison::kPrisonDecodeTooShort : cooprison::DecodePrison(&m.payload[0], m.payload.size(), &pm);
    if (r != cooprison::kPrisonDecodeOk)
    {
        coop::PrisonNoteDropped();
        DebugLog("[net] <- PRISON malformed (reason " + N((long long)r) + ") - ignored");
        return;
    }
    /* par6 fold (review-par6 #1): kind 4 is a DEATH REQUEST for a character this game drives - medical.cpp, not the cage code */
    if (pm.kind == cooprison::kPrisonDeath) { coop::ApplyOwnerDeathRequest(pm.uid, pm.cageKey, m.peer); return; }
    coop::ApplyRemotePrison(pm, m.peer);
}

/* E45 (P8e).  Reliable: a door state is a level, not a stream, and a dropped one leaves the two
   games disagreeing until the next change - which for a settled door may be never. */
bool SendDoorState(const std::string& key, int state, int locked, unsigned int gen, int origin, const float* pos)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 4 (C4): the character stream's road */
    if (key.empty() || key.size() > 4096) return false;
    if (state != 0 && state != 1) return false;   /* OPENING and CLOSING never go on the wire */
    {
        std::vector<char> b;
        PutStr(&b, key);
        b.push_back((char)(unsigned char)state);
        b.push_back((char)(unsigned char)(coopdoor::DoorLockWordValid(locked) != 0 ? locked : 0));   /* T-160: the lock word - bit 0 locked, bit 1 wantsToLock */
        PutU32(&b, gen);
        /* P8n: THE ORIGIN BYTE, APPENDED AND NOT INSERTED.  0 = the area holder's answer, 1 = an
           actor report from a game that does not hold this door.  It goes last because the decoder's
           length test is a MINIMUM, so a reader that does not know about it reads the message exactly
           as before and a reader that does sees the byte when it is there.  That is why the protocol
           version was 42 when this byte was added and is 43 since B10 (see kProtocolVersion); the byte is optional under both. */
        b.push_back((char)(unsigned char)(origin != 0 ? 1 : 0));
        /* M7b slice 4 (C4): the holder's answer to every game covering the door's sector; an actor report to that sector's holder
           (AREA while no holder is known - only a game that holds the door adopts a report) */
        const int area = (pos != 0) ? coop::AreaKeyAt(pos[0], pos[2]) : -1;
        const int holder = (origin != 0 && area >= 0) ? coop::ItemAreaWriterSlot(cooplive::AreaKeyX(area), cooplive::AreaKeyY(area)) : -1;
        if (cooplive::SideRouteOf(cooplive::kInnerDoorState, origin != 0 ? 1 : 0, holder >= 0) == cooplive::kSideRouteSlot)
            return ItemSendTo(MSG_DOOR_STATE, holder, &b[0], b.size(), 0u, 0);
        return SideSendSpread(MSG_DOOR_STATE, area, -1, false, &b[0], b.size());   /* fold 1 (F4) */
    }
}
// E45 (P8e) - MSG_DOOR_STATE: the holder's answer for one door.  Refused whole on any malformed
// field: a half-read key names a door nobody can name, and applying a state to the wrong door is
// worse than applying none.  The apply itself is in doors.cpp, on the main thread, behind
// EngineWritesBlocked() - this message is not in the inline classification table, so it arrives
// here from the no-world queue's drain and never from inside a load.
void OnDoorState(const Message& m)
{
    std::string key;
    size_t at = 0;
    unsigned int gen = 0;
    if (!GetStr(m.payload, &at, &key)) { DebugLog("[net] <- DOOR_STATE malformed (key) - ignored"); return; }
    {
        /* P8n-b (review-p8n L-8): THE TRAILING BYTE IS PARSED BY A PURE FUNCTION AND AN UNKNOWN ORIGIN
           IS IGNORED.  This read the byte and passed it through as an int, and doors.cpp tests it only
           against kDoorOriginActor - so a byte of 2..255 (a corrupted packet, or a future build's third
           origin) took the HOLDER arm and was applied as authority.  The length test is inside the
           helper now, so one function decides short / absent / holder / actor / unknown and the offline
           suite sweeps all five. */
        const int parsed = coopdoor::DoorParseOrigin(
            (unsigned long)m.payload.size(), (unsigned long)at,
            (m.payload.size() >= at + 7) ? (int)(unsigned char)m.payload[at + 6] : 0);
        if (parsed == coopdoor::kDoorOriginParseShort)
        { DebugLog("[net] <- DOOR_STATE malformed (short) - ignored"); return; }
        if (parsed == coopdoor::kDoorOriginParseUnknown)
        {
            coop::NoteDoorOriginUnknown();
            DebugLog("[net] <- DOOR_STATE with an origin byte that is neither 0 nor 1 - IGNORED, not"
                     " taken as the holder's word (doorOriginUnknown)");
            return;
        }
        {
            const int state = (int)(unsigned char)m.payload[at];
            const int locked = (int)(unsigned char)m.payload[at + 1];
            const int origin = (parsed == coopdoor::kDoorOriginParseActor)
                               ? coopdoor::kDoorOriginActor : coopdoor::kDoorOriginHolder;
            if (!GetU32(m.payload, at + 2, &gen)) { DebugLog("[net] <- DOOR_STATE malformed (gen) - ignored"); return; }
            coop::ApplyDoorState(key, state, locked, gen, m.peer, origin);
        }
    }
}

bool SendItemRevoke(unsigned int id)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 2: the item road */
    std::vector<char> b;
    PutU32(&b, id);
    return ItemSendTo(MSG_ITEM_REVOKE, ItemToAskedOf(id), &b[0], b.size(), 0u, 0);   /* M7b slice 2: to the game request `id` was asked of */
}

/* T-164 B4-4 fold 2 (protocol 96): MSG_ITEM_REVOKE with the shop tail 'SHK1' {u32 tag, u32 epoch, u8 kind} after the id - kind 0 the
   shop revoke (a late ok: the holder undoes its halves), kind 1 the SHOP ACK (settled: the holder drops its record). */
bool SendShopTail(unsigned int id, unsigned int epoch, int kind)
{
    if (CharRoadNow() == cooplive::kCharRoadNone) return false;   /* M7b slice 2: the item road */
    std::vector<char> b;
    PutU32(&b, id);
    coopshop::EncodeTail(&b, epoch, kind);
    return ItemSendTo(MSG_ITEM_REVOKE, ItemToAskedOf(id), &b[0], b.size(), 0u, 0);   /* M7b slice 2: to the game shop request `id` was asked of */
}

// E22b - MSG_ITEM_REQUEST: the peer touched an inventory it does not own and is asking US, the owner, to make
// the move. Refused whole on any malformed field: a half-read request would name a slot nobody can name.
static void OnItemRequestBody(const Message& m, const coophold::HoldTail* hdTail, int hdDir)
{
    coop::ItemRequestMsg r;
    size_t at = 0;
    unsigned int id = 0, ownerUid = 0, takerUid = 0, ox = 0, oy = 0, qty = 0, tx = 0, ty = 0;
    if (!GetU32(m.payload, 0, &id) || m.payload.size() < 5) { ErrorLog("[net] malformed ITEM_REQUEST (too short) - REFUSED"); return; }
    r.id = id; at = 4;
    r.dir = (int)(unsigned char)m.payload[at]; ++at;
    if (r.dir != 0 && r.dir != 1) { ErrorLog("[net] ITEM_REQUEST with unknown direction " + N(r.dir) + " - REFUSED"); return; }
    if (!GetU32(m.payload, at, &ownerUid)) { ErrorLog("[net] malformed ITEM_REQUEST (owner uid) - REFUSED"); return; }
    at += 4;
    r.ownerUid = ownerUid;
    if (!GetStr(m.payload, &at, &r.ownerSection)) { ErrorLog("[net] malformed ITEM_REQUEST (owner section) - REFUSED"); return; }
    if (!GetU32(m.payload, at, &ox) || !GetU32(m.payload, at + 4, &oy) || !GetU32(m.payload, at + 8, &qty)
        || !GetU32(m.payload, at + 12, &takerUid))
    { ErrorLog("[net] malformed ITEM_REQUEST (owner slot) - REFUSED"); return; }
    r.ownerX = (int)ox; r.ownerY = (int)oy; r.quantity = (int)qty; r.takerUid = takerUid; at += 16;
    if (!GetStr(m.payload, &at, &r.takerSection)) { ErrorLog("[net] malformed ITEM_REQUEST (taker section) - REFUSED"); return; }
    if (!GetU32(m.payload, at, &tx) || !GetU32(m.payload, at + 4, &ty))
    { ErrorLog("[net] malformed ITEM_REQUEST (taker slot) - REFUSED"); return; }
    r.takerX = (int)tx; r.takerY = (int)ty; at += 8;
    /* cell1 fold (review-cell1 R3, protocol 82): the item block for EVERY direction - a TAKE names the record it wants. */
    if (!GetItemFields(m.payload, &at, &r.baseSid, &r.companySid, &r.materialSid, &r.colorSid,
                       &r.quality, &r.charges, &r.functionKind, &r.level, &r.unique))
    { ErrorLog("[net] malformed ITEM_REQUEST (item record) - REFUSED"); return; }
    if (r.baseSid.empty()) { ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + std::string(r.dir != 0 ? " give" : " take") + " with no base data sid - REFUSED"); return; }
    if (r.ownerUid == 0)
    {
        if (!GetStr(m.payload, &at, &r.ownerBoxKey) || r.ownerBoxKey.empty())
        { ErrorLog("[net] ITEM_REQUEST with owner uid 0 and no container key - REFUSED"); return; }
        // E22c-2 (P7n): the instance id beside it, empty allowed and absent not.
        if (!GetStr(m.payload, &at, &r.ownerBoxId))
        { ErrorLog("[net] ITEM_REQUEST with owner uid 0 and no container id field - REFUSED"); return; }
    }
    /* T-164 211 (protocol 98): a taker uid of 0 carries the taker BOX's key next - empty = no taker (a ground put that names none) */
    if (r.takerUid == 0 && !GetStr(m.payload, &at, &r.takerBoxKey))
    { ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + " with taker uid 0 and no taker box field - REFUSED"); return; }
    // E35 (P6o) / decision 41. Refused whole if the block is missing: both peers speak protocol 39 or the
    // link never came up (the HELLO check), so an absent block is a malformed message, not an old peer - and
    // a request whose trade half could not be read must not be applied as if it were a free transfer.
    {
        unsigned int price = 0;
        if (at + 5 > m.payload.size()) { ErrorLog("[net] malformed ITEM_REQUEST (trade block) - REFUSED"); return; }
        r.trade = (int)(unsigned char)m.payload[at]; ++at;
        if (r.trade != 0 && r.trade != 1 && r.trade != 2)
        { ErrorLog("[net] ITEM_REQUEST with unknown trade kind " + N(r.trade) + " - REFUSED"); return; }
        if (!GetU32(m.payload, at, &price)) { ErrorLog("[net] malformed ITEM_REQUEST (price) - REFUSED"); return; }
        r.price = (int)price; at += 4;
        if (r.price < 0) { ErrorLog("[net] ITEM_REQUEST with a negative price - REFUSED"); return; }
    }
    /* T-164 B4-4 (protocol 92): the optional 'SHR1' shop block, right after the trade block. A cut or out-of-range one REFUSES the
       message: a shop request read without it would be applied as an ordinary box move. */
    if (at < m.payload.size() && coopshop::ReqTagAt(&m.payload[0], m.payload.size(), at))   /* T-1 B5: 'SHR3', or the older 'SHR2' (a home's) */
    {
        if (!coopshop::DecodeReq(&m.payload[0], m.payload.size(), &at, &r.shop))
        { ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + " with a shop block that is cut or out of range - REFUSED"); return; }
    }
    /* inv3a (protocol 70): the optional BAG1 block. A block that is there but is not whole and in range REFUSES THE MESSAGE:
       applying the item half without its contents is exactly the loss this block exists to end. */
    if (at < m.payload.size() && !coopmark::OwnerBlockAt(&m.payload[0], m.payload.size(), at) && !coopbag::BagPathAt(&m.payload[0], m.payload.size(), at))   /* inv6: an OWN1 block may follow, or stand alone; bag23 part 2: or the BAGP block */
    {
        size_t bagEnd = 0;
        const int bw = coopbag::DecodeBagRows(&m.payload[0], m.payload.size(), at, &r.bag, &bagEnd);
        if (bw == coopbag::kBagBadOwner) coop::ItemOwnerNoteBadRecv();   /* inv6 phase 2 */
        if (bw != coopbag::kBagOk)
        { ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + " with a backpack block that is " + std::string(coopbag::BagDecodeWhy(bw)) + " - REFUSED"); return; }
        if (r.dir == 0 && !r.bag.empty()) { ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + ": a TAKE carries no backpack rows - REFUSED"); return; }
        at = bagEnd;
    }
    /* inv6 (protocol 71): the optional OWN1 block, LAST. Anything else left over, or a cut block, REFUSES the message. */
    if (at < m.payload.size() && !coopbag::BagPathAt(&m.payload[0], m.payload.size(), at))
    {
        size_t oe = 0;
        const int ow = coopmark::DecodeOwner(&m.payload[0], m.payload.size(), at, &r.owner, &oe);
        if (ow != coopmark::kOwnOk)
        { coop::ItemOwnerNoteBadRecv(); ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + " with trailing bytes that are not a whole owner block (" + std::string(coopmark::OwnerDecodeWhy(ow)) + ") - REFUSED"); return; }
        if (r.dir == 0) { ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + ": a TAKE carries no owner block - REFUSED"); return; }
        at = oe;
        if (at != m.payload.size() && !coopbag::BagPathAt(&m.payload[0], m.payload.size(), at)) { ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + " with bytes after its owner block - REFUSED"); return; }
    }
    /* bag23 part 2 (protocol 81): the optional BAGP block, LAST - the owner half is inside a pack. Anything else, cut or out of range:
       REFUSED (a request is never applied to a guessed inventory). */
    if (at < m.payload.size())
    {
        size_t pe = 0;
        if (coopbag::DecodeBagPath(&m.payload[0], m.payload.size(), at, &r.ownerBag, &pe) != coopbag::kBagOk || pe != m.payload.size())
        { ErrorLog("[net] ITEM_REQUEST " + N((long long)r.id) + " with trailing bytes that are not a whole pack path block - REFUSED"); return; }
    }
    if (hdTail != 0) { r.dir = hdDir; r.holdId = hdTail->holdId; r.holdHow = hdTail->how; r.holdSlot = hdTail->slot; }   /* P105 build 2 */
    coop::ApplyItemRequest(r, m.peer);
}
/* P105 build 2 (protocol 126): dir 2 HOLD / 3 LAND end with the 'HLD1' trailer (coophold) - read off the end, the rest read as a
   take-shaped request (dir 0 rules: no backpack rows, no owner block), then the direction restored. Not whole: REFUSED. */
void OnItemRequest(const Message& m)
{
    if (m.payload.size() >= 5 && ((unsigned char)m.payload[4] == coophold::kDirHold || (unsigned char)m.payload[4] == coophold::kDirLand))
    {
        coophold::HoldTail ht;
        const int dir = (int)(unsigned char)m.payload[4];
        if (!coophold::TakeHoldTail(&m.payload[0], m.payload.size(), &ht))
        { ErrorLog("[net] ITEM_REQUEST " + std::string(dir == coophold::kDirHold ? "HOLD" : "LAND") + " with an 'HLD1' block that is cut or out of range - REFUSED"); return; }
        Message c = m;
        c.payload.resize(c.payload.size() - coophold::kHoldTailLen);
        if (c.payload.size() < 5) { ErrorLog("[net] malformed ITEM_REQUEST (nothing before its HLD1 block) - REFUSED"); return; }
        c.payload[4] = 0;
        OnItemRequestBody(c, &ht, dir);
        return;
    }
    OnItemRequestBody(m, 0, 0);
}

// E22b-2 (P6b) - MSG_ITEM_PLACED: the taker's answer about a TAKE we granted. Refused whole if it is short:
// a half-read id would name somebody else's pending request.
void OnItemPlaced(const Message& m)
{
    unsigned int id = 0;
    if (!GetU32(m.payload, 0, &id) || m.payload.size() < 5) { ErrorLog("[net] malformed ITEM_PLACED (too short) - REFUSED"); return; }
    const int ok = (int)(unsigned char)m.payload[4];
    coop::ApplyItemPlaced(id, ok, m.peer);
}

// E22b-2 (P6b) - MSG_ITEM_REVOKE: the giver could not remove its own copy of a GIVE we granted.
void OnItemRevoke(const Message& m)
{
    unsigned int id = 0;
    if (!GetU32(m.payload, 0, &id)) { ErrorLog("[net] malformed ITEM_REVOKE (too short) - REFUSED"); return; }
    /* T-164 B4-4 fold 2 (protocol 96): the optional shop tail - a shop revoke / ack; a cut or out-of-range tail REFUSES the message */
    size_t at = 4;
    if (at < m.payload.size() && coopshop::TagAt(&m.payload[0], m.payload.size(), at, coopshop::kTailTag))
    {
        coopshop::ShopTail t;
        if (!coopshop::DecodeTail(&m.payload[0], m.payload.size(), &at, &t))
        { ErrorLog("[net] ITEM_REVOKE " + N((long long)id) + " with a shop tail that is cut or out of range - REFUSED"); return; }
        coop::ApplyShopTail(id, m.peer, t.epoch, t.kind);
        return;
    }
    coop::ApplyItemRevoke(id, m.peer);
}

// E22b - MSG_ITEM_CONFIRM: the owner's answer to a request WE sent. This is the moment - and the only moment -
// at which our own copy of the item changes.
static void OnItemConfirmBody(const Message& m, const coophold::LandTail* hdLand)
{
    coop::ItemConfirmMsg c;
    size_t at = 0;
    unsigned int id = 0, qty = 0;
    if (!GetU32(m.payload, 0, &id) || m.payload.size() < 9) { ErrorLog("[net] malformed ITEM_CONFIRM (too short) - REFUSED"); return; }
    c.id = id; at = 4;
    c.ok = (int)(unsigned char)m.payload[at]; ++at;
    if (!GetU32(m.payload, at, &qty)) { ErrorLog("[net] malformed ITEM_CONFIRM (quantity) - REFUSED"); return; }
    c.quantity = (int)qty; at += 4;
    if (c.ok != 0)
    {
        if (!GetItemFields(m.payload, &at, &c.baseSid, &c.companySid, &c.materialSid, &c.colorSid,
                           &c.quality, &c.charges, &c.functionKind, &c.level, &c.unique))
        { ErrorLog("[net] malformed ITEM_CONFIRM (item record) - REFUSED"); return; }
        if (c.baseSid.empty()) { ErrorLog("[net] ITEM_CONFIRM ok with no base data sid - REFUSED"); return; }
    }
    // E35 (P6o): the holder's own trade answer. Refused whole for the same reason the request's is - a
    // confirmation whose price could not be read would have the requester settle its purse by a guess.
    {
        unsigned int price = 0;
        if (at + 5 > m.payload.size()) { ErrorLog("[net] malformed ITEM_CONFIRM (trade block) - REFUSED"); return; }
        c.trade = (int)(unsigned char)m.payload[at]; ++at;
        if (c.trade != 0 && c.trade != 1 && c.trade != 2)
        { ErrorLog("[net] ITEM_CONFIRM with unknown trade kind " + N(c.trade) + " - REFUSED"); return; }
        if (!GetU32(m.payload, at, &price)) { ErrorLog("[net] malformed ITEM_CONFIRM (price) - REFUSED"); return; }
        c.price = (int)price; at += 4;
        if (c.price < 0) { ErrorLog("[net] ITEM_CONFIRM with a negative price - REFUSED"); return; }
    }
    /* T-164 B4-4 (protocol 92): the optional 'SHC1' shop answer, right after the trade block (before GND1). Cut or out of range:
       REFUSED (the requester's own 10 s timeout then gives the reservation / escrow back). */
    if (at < m.payload.size() && coopshop::TagAt(&m.payload[0], m.payload.size(), at, coopshop::kCfTag))
    {
        if (!coopshop::DecodeCf(&m.payload[0], m.payload.size(), &at, &c.shop))
        { ErrorLog("[net] ITEM_CONFIRM " + N((long long)c.id) + " with a shop block that is cut or out of range - REFUSED"); return; }
    }
    /* inv5 (protocol 72): a refusal may end with a 'GND1' block {u32 tag, u8 reason} - the ground's reason (1 not holder, 2 not found).
       It is the whole remainder when present, so the blocks below then see nothing. An unknown reason REFUSES the message. */
    if (c.ok == 0 && at + 5 == m.payload.size())
    {
        unsigned int tag = 0;
        if (GetU32(m.payload, at, &tag) && tag == coopground::kGroundReasonTag)
        {
            c.reason = (int)(unsigned char)m.payload[at + 4];
            at += 5;
            if (c.reason != coopground::kGroundReasonNotHolder && c.reason != coopground::kGroundReasonNotFound
                && c.reason != coopground::kGroundReasonBusy && c.reason != coopground::kGroundReasonUnreadable)   /* inv5p1 fold: 3 busy, 4 unreadable */
            { ErrorLog("[net] ITEM_CONFIRM " + N((long long)c.id) + " with an unknown ground reason " + N((long long)c.reason) + " - REFUSED"); return; }
        }
    }
    /* inv3a (protocol 70): the optional BAG1 block, refused whole when it is not whole and in range (see OnItemRequest). A
       refused confirmation is never applied, so the owner's ten-second timeout rolls its kept pack back WITH its contents. */
    if (at < m.payload.size() && !coopmark::OwnerBlockAt(&m.payload[0], m.payload.size(), at))   /* inv6: an OWN1 block may follow, or stand alone */
    {
        size_t bagEnd = 0;
        const int bw = coopbag::DecodeBagRows(&m.payload[0], m.payload.size(), at, &c.bag, &bagEnd);
        if (bw == coopbag::kBagBadOwner) coop::ItemOwnerNoteBadRecv();   /* inv6 phase 2 */
        if (bw != coopbag::kBagOk)
        { ErrorLog("[net] ITEM_CONFIRM " + N((long long)c.id) + " with a backpack block that is " + std::string(coopbag::BagDecodeWhy(bw)) + " - REFUSED"); return; }
        if (c.ok == 0 && !c.bag.empty()) { ErrorLog("[net] ITEM_CONFIRM " + N((long long)c.id) + ": a refusal carries no backpack rows - REFUSED"); return; }
        at = bagEnd;
    }
    /* inv6 (protocol 71): the optional OWN1 block, LAST, ok 1 only. Anything else left over, or a cut block, REFUSES the message
       (the owner's ten-second timeout then rolls its kept item back). */
    if (at < m.payload.size())
    {
        size_t oe = 0;
        const int ow = coopmark::DecodeOwner(&m.payload[0], m.payload.size(), at, &c.owner, &oe);
        if (ow != coopmark::kOwnOk)
        { coop::ItemOwnerNoteBadRecv(); ErrorLog("[net] ITEM_CONFIRM " + N((long long)c.id) + " with trailing bytes that are not a whole owner block (" + std::string(coopmark::OwnerDecodeWhy(ow)) + ") - REFUSED"); return; }
        if (c.ok == 0) { ErrorLog("[net] ITEM_CONFIRM " + N((long long)c.id) + ": a refusal carries no owner block - REFUSED"); return; }
        at = oe;
        if (at != m.payload.size()) { ErrorLog("[net] ITEM_CONFIRM " + N((long long)c.id) + " with bytes after its owner block - REFUSED"); return; }
    }
    if (hdLand != 0) { c.landHas = 1; c.landWhere = hdLand->where; c.landX = hdLand->x; c.landY = hdLand->y; }   /* P105 build 2 */
    coop::ApplyItemConfirm(c, m.peer);   /* M7b slice 2: taken only from the game the request was asked of */
    if (c.ok == 0) ItemAskedRefusedBy(c.id, m.peer);   /* fold 1: an explicit refusal from the game a re-send is held to releases it */
}
/* P105 build 2 (protocol 126): ok byte 2 = ok with the 'LND1' trailer (coophold) - read off the end, the byte set to 1, the rest read
   exactly as before. Not whole: REFUSED. */
void OnItemConfirm(const Message& m)
{
    if (m.payload.size() >= 5 && (unsigned char)m.payload[4] == coophold::kConfirmOkLand)
    {
        coophold::LandTail lt;
        if (!coophold::TakeLandTail(&m.payload[0], m.payload.size(), &lt))
        { ErrorLog("[net] ITEM_CONFIRM with an 'LND1' block that is cut or out of range - REFUSED"); return; }
        Message c = m;
        c.payload.resize(c.payload.size() - coophold::kLandTailLen);
        if (c.payload.size() < 5) { ErrorLog("[net] malformed ITEM_CONFIRM (nothing before its LND1 block) - REFUSED"); return; }
        c.payload[4] = 1;
        OnItemConfirmBody(c, &lt);
        return;
    }
    OnItemConfirmBody(m, 0);
}

bool SendAppearance(unsigned int uid, const RecordCopy& rec)
{
    if (!CharRoadOpen()) return false;   /* M7a: the notebook road, or the session link while it is down */

    std::vector<char> b;
    PutU32(&b, uid);
    SerialiseRecord(rec, &b);
    return CharSend(MSG_APPEARANCE, uid, &b[0], b.size(), CH_RELIABLE, -1);   /* M7a */
}

bool LinkIsUp()
{
    return g_transport != 0 && g_transport->State() == LINK_UP;
}

float PeerViewDistance() { return g_peerViewDistance; }
unsigned int SessionProtocolVersion() { return kProtocolVersion; }   /* M11a S1: carried in the STORE_HELLO's tail, checked by the world server (manager decision 1(a)) */
std::string SessionPeerName() { return g_peerName; }   /* mp4 */

// F311. Clearing this is not optional bookkeeping - a stale value is INDISTINGUISHABLE from a
// fresh one at every readout, so it must die with the session. Also tells worldsync to drop a
// radius that was derived from it, since that radius is now a fact about nobody.
void ClearPeerViewDistance()
{
    if (g_peerViewDistance != 0.0f)
        DebugLog("[net] peer view distance cleared (was " + N((long long)g_peerViewDistance)
                 + ") - it belonged to a peer that is gone");
    g_peerViewDistance = 0.0f;
    g_peerName.clear();   /* mp4: the players list's name belongs to the same peer */
    coop::ForgetDerivedRadius();
}

bool SendSpawn(unsigned int uid, const std::string& templateName, float x, float y, float z,
               const std::string& factionName, bool keepContainer, const void* character)
{
    if (!CharRoadOpen())   /* M7a: the notebook road, or the session link while it is down */
        return false;
    if (coopslot::WireLacksSlot(factionName)) { coop::NoteHeldForSlot(); return false; }   /* stand1 fold (review-stand1 1d): my faction has no slot yet - HELD (not sent, the row stays unannounced); re-announced when the slot arrives */

    // P1 (read-parity3 GAP 1): the owner's age travels, read NOW from CharacterAnimal +0x700 through
    // coop::ReadAnimalAge01 (combat.cpp - the PlausibleObject + getter-offset animal test, SEH-guarded).
    // A human or an unreadable character sends 0.0 - what the copy was created with before - and is counted.
    float age = 0.0f;
    bool clampedOut = false;
    if (coop::ReadAnimalAge01(character, &age)) { age = coopspawn::SpawnAgeClamp(age, &clampedOut); ++g_spawnAgeSent; }
    else { age = 0.0f; ++g_spawnAgeUnread; }

    std::vector<char> b;
    // F115: modifiers travel with the spawn (faction by NAME - both installs load identical static data,
    // F072). P1: the age follows them. One encoder, shared with the offline suite: src/common/spawnage.h.
    coopspawn::EncodeSpawn(&b, uid, x, y, z, templateName, factionName, keepContainer, age);
    // S1 (read-stats): the owner's 44 saved stat values ride after the age (statsHas 0 when unreadable).
    unsigned int statsRaw[coopstats::kStatsCount];
    const bool statsHave = coop::StatsReadOwned(character, statsRaw);
    coopspawn::AppendSpawnStats(&b, statsHave ? statsRaw : 0);
    /* T-303 (protocol 102): the character's dead / knocked-out state rides the SPAWN, read now, so every announce - creation,
       adoption, link-up, reload, a rejoin - creates the copy as the character is here (a corpse is never announced standing). */
    const unsigned int spawnFlags = coop::SpawnOwnerFlags(character);
    coopspawn::AppendSpawnFlags(&b, spawnFlags);

    bool ok = CharSend(MSG_SPAWN, uid, &b[0], b.size(), CH_RELIABLE, coop::AreaKeyAt(x, z));   /* M7a: the sector of the position it carries */
    if (ok) coop::SpawnNoteFlagsSent(spawnFlags);   /* T-303 fold 1: counted only for a SPAWN the send accepted */
    if (ok && statsHave) coop::StatsNoteSpawnCarried(uid, statsRaw);
    if (ok) coop::NameSendWithSpawn(uid, character);   /* names1: the character's current name follows every SPAWN on the same reliable channel */
    if (ok) coop::SlaveSendWithSpawn(uid, character);  /* slave1: and its slave state */
    if (ok) coop::CrimeSendWithSpawn(uid);   /* M7b slice 4 fold 1 (F5): and its crime state, on the SPAWN's road */
    /* T-1 B3 restructure (protocol 89): no money follows a SPAWN - the squad's MSG_SQUAD_LEAD carries it */

    // Ownership is recorded by the CALLER at creation time (SetLocalOwner), not here:
    // T023 found that recording it only on a successful send meant a locally-created
    // object read as "not ours" whenever no session was up, and the combat detour then
    // took the puppet branch and made it invulnerable. Authorship confers authority;
    // whether the peer has been told is a different question.

    std::ostringstream ageText; ageText.imbue(std::locale::classic()); ageText << age;
    DebugLog("[net] -> SPAWN uid=" + N(uid) + " '" + templateName + "' age=" + ageText.str()
             + (clampedOut ? " (clamped)" : "")
             + " dead=" + N((spawnFlags & coopspawn::kSpawnFlagDead) != 0 ? 1 : 0)
             + " ko=" + N((spawnFlags & coopspawn::kSpawnFlagKo) != 0 ? 1 : 0)   /* T-303 */
             + (ok ? "" : " (SEND FAILED)"));
    return ok;
}

/* THE SESSION'S END - what a leave, a new host or a new join takes from the owner table (peergone.h SessionEndKeepsRow). With the world-
   server link up, the rows of world-road players this game did not author stay, with their copy generations, stream marks and lost-copy
   rows: those players are still reached through the world server and only the session ended. Every other row goes; with keepWorldRows
   false (the world link is down, or is closed with the session - ConfigLeave) every row goes. */
long long g_sessEndKept = 0, g_sessEndCleared = 0;
void SessionForgetRows(bool keepWorldRows, const char* why)
{
    if (!keepWorldRows)
    {
        g_sessEndCleared += (long long)g_owner.size();
        g_owner.clear(); g_copyGen.clear(); coop::net::RosterStreamForgetAll(); LostCopyForgetAll();   /* copy gens live with g_owner; g_mineGen lives with g_localOwned */
        return;
    }
    long long kept = 0, cleared = 0;
    for (std::map<unsigned int, unsigned int>::iterator it = g_owner.begin(); it != g_owner.end(); )
    {
        if (cooppg::SessionEndKeepsRow(true, g_localOwned.find(it->first) != g_localOwned.end(), it->second)) { ++kept; ++it; continue; }
        const unsigned int uid = it->first;
        g_copyGen.erase(uid); coop::net::RosterStreamForget(uid); LostCopyForget(uid, "its owner row ended with the session");
        g_owner.erase(it++); ++cleared;
    }
    g_sessEndKept += kept; g_sessEndCleared += cleared;
    if (kept > 0 || cleared > 0)
        DebugLog("[net] session end (" + std::string(why != 0 ? why : "?") + "): the world-server link is up - " + N(kept) + " owner rows of world-road players kept"
                 " (still reached through the world server), " + N(cleared) + " rows of the session and of this game's own cleared (sessionEnd kept "
                 + N(g_sessEndKept) + ", cleared " + N(g_sessEndCleared) + ")");
}

bool SessionHost(const std::string& backendName, unsigned short port)
{
    BackendId id = ParseBackend(backendName);
    if (id == BACKEND_NONE)
    {
        ErrorLog("[net] unknown backend '" + backendName + "' (expected: direct | steam)");
        return false;
    }

    /* E38 - ALREADY HOSTING ON THIS PORT IS NOT A REASON TO START AGAIN. The line below this is SessionLeave(),
       which drops the peer and runs OnPeerGone over its whole world. Under E38 the config file starts the host
       at the title screen and the harness still sends `host enet 7777` after the load, so without this test
       every run would tear its own session down and rebuild it at exactly the moment E38 exists to avoid. A
       DIFFERENT port or backend still means what it always meant: leave and re-host. */
    if (g_transport != 0 && g_isHost && g_hostPort == port && g_hostBackend == id)
    {
        DebugLog("[net] already HOSTING on port " + N(port) + " - the request is the state we are in, so nothing is torn down (E38: the config file started this at the title screen)");
        return true;
    }

    SessionLeave();
    g_transport = CreateTransport(id);
    if (g_transport == 0) return false;   // CreateTransport logged why
    g_transport->SetPeerTimeouts(coopgl::kGameLinkTimeoutLimit, coopgl::kGameLinkTimeoutMinimumMs, coopgl::kGameLinkTimeoutMaximumMs);   /* link1 */

    std::string err;
    if (!g_transport->Host(port, &err))
    {
        ErrorLog("[net] host failed: " + err);
        SessionLeave();
        return false;
    }
    g_isHost   = true;
    g_myPeerId = 0;
    g_hostPort = port; g_hostBackend = id; g_joinAddr.clear(); g_joinPort = 0;   /* E38: what "already hosting" means */
    SessionForgetRows(coop::StoreLiveReady(), "a new host");   /* the world-road rows stay while the world link is up */
    g_departedPending.clear();   /* M8 review F4: a previous session's captured marks are no one's in this one */
    DebugLog(std::string("[net] session HOSTING on ") + g_transport->BackendName()
             + " port " + N(port));
    return true;
}

bool SessionJoin(const std::string& backendName, const std::string& address, unsigned short port)
{
    BackendId id = ParseBackend(backendName);
    if (id == BACKEND_NONE)
    {
        ErrorLog("[net] unknown backend '" + backendName + "' (expected: direct | steam)");
        return false;
    }

    /* E38 - A LIVE LINK TO THE SAME HOST IS KEPT. The test is deliberately narrower than the host one: only a
       link that is actually UP is left alone. A join still CONNECTING may have been dialled at a host that was
       not listening yet, and re-dialling it is the behaviour every run before E38 relied on. */
    if (g_transport != 0 && !g_isHost && g_joinAddr == address && g_joinPort == port && g_transport->State() == LINK_UP)
    {
        DebugLog("[net] already JOINED to " + address + ":" + N(port) + " and the link is UP - nothing is torn down (E38: the config file joined at the title screen)");
        return true;
    }

    SessionLeave();
    g_transport = CreateTransport(id);
    if (g_transport == 0) return false;
    g_transport->SetPeerTimeouts(coopgl::kGameLinkTimeoutLimit, coopgl::kGameLinkTimeoutMinimumMs, coopgl::kGameLinkTimeoutMaximumMs);   /* link1 */

    std::string err;
    if (!g_transport->Join(address, port, &err))
    {
        ErrorLog("[net] join failed: " + err);
        SessionLeave();
        return false;
    }
    g_isHost = false;
    g_joinAddr = address; g_joinPort = port; g_hostBackend = id; g_hostPort = 0;   /* E38: what "already joined" means */
    SessionForgetRows(coop::StoreLiveReady(), "a new join");   /* the world-road rows stay while the world link is up */
    g_departedPending.clear();   /* M8 review F4: a previous session's captured marks are no one's in this one */
    DebugLog(std::string("[net] session JOINING ") + address + ":" + N(port)
             + " on " + g_transport->BackendName());
    return true;
}

/* ---- mmo5 (e47-mmo-design.md 6, 7 item 5): SESSION_CLOSING. The host says it is leaving just before it closes the link, so
   a joiner can tell "the host closed the world" from "the connection was lost". Sent on the deliberate paths only - the
   `leave` verb (command_channel.cpp, and the leave save's continuation in store.cpp) and the quit latch (NoteQuitting) -
   never by the SessionLeave a host/join makes first, nor by a protocol refusal. ---- */
/* g_hostClosingSeen is defined beside g_isHost (mmo5 fold) */
long long g_closingSent = 0, g_closingSeenCount = 0, g_closingIgnoredOnHost = 0;
void SessionSendClosing(const char* why)
{
    coop::StoreSendHostClosingLive(why);   /* M11a S3 (decision 7(a) (i)): the world's operator ALSO says it through the world server, route WORLD - a world-road game has no session link to hear it on */
    if (g_transport == 0 || !g_isHost || g_transport->State() != LINK_UP) return;
    const bool ok = g_transport->Send(0, MSG_SESSION_CLOSING, 0, 0, CH_RELIABLE);
    g_transport->Flush();   /* F069: a message left in the queue dies with the link or the process */
    ++g_closingSent;
    DebugLog(std::string("[net] -> SESSION_CLOSING (mmo5: the host is leaving - ") + (why != 0 ? why : "?") + ") sent="
             + (ok ? "1" : "0") + " - a joiner saves, pauses and shows the host-left window");
}
void OnSessionClosing(const Message& m)
{
    if (g_isHost)
    {
        ++g_closingIgnoredOnHost;
        DebugLog("[net] <- SESSION_CLOSING from peer " + N(m.peer) + " IGNORED: this game is the host (only a host announces"
                 " its leave; the host-left window is joiner-only)");
        return;
    }
    ++g_closingSeenCount;
    g_hostClosingSeen = coopgl::HostClosingFlagStep(g_hostClosingSeen, coopgl::kHcReceived);
    DebugLog("[net] <- SESSION_CLOSING from the host (peer " + N(m.peer) + ") - mmo5: the host is leaving on purpose; the store"
             " tick saves, pauses and shows the host-left window");
}
void SessionHostLeftInputs(int* joiner, int* linkUp, int* dialing, unsigned int* redials, int* closingSeen)
{
    const LinkState ls = (g_transport != 0) ? g_transport->State() : LINK_DOWN;
    *joiner = (g_transport != 0 && !g_isHost) ? 1 : 0;
    *linkUp = (ls == LINK_UP) ? 1 : 0;
    *dialing = (ls == LINK_CONNECTING) ? 1 : 0;
    *redials = g_glSeriesOpen ? g_glAttempts : 0;
    *closingSeen = g_hostClosingSeen;
}
/* ui1 (owner 2026-09-27, connection trouble step 2): how long the host has been silent - the transport's own last-receive
   time for the host peer. The host never asks (0). */
/* ui1 review LOW: "Reconnected to the host." waits for the host's acceptance - a WELCOME on the CURRENT link, not the raw
   link-up. (A WELCOME this build refuses for its protocol also counts; the same build on both sides never is.) */
bool SessionHostAccepted()
{
    if (g_transport == 0 || g_isHost || g_transport->State() != LINK_UP) return false;
    return g_welcomeLinkGen == SessionLinkGen();
}
unsigned int SessionHostSilenceMs()
{
    if (g_transport == 0 || g_isHost) return 0;
    return g_transport->HostSilenceMs();
}
long long g_closingDropped = 0;
void SessionHostClosingNotActionable()
{
    if (g_hostClosingSeen == 0) return;
    g_hostClosingSeen = coopgl::HostClosingFlagStep(g_hostClosingSeen, coopgl::kHcNotActionable);
    ++g_closingDropped;
    DebugLog("[net] SESSION_CLOSING DROPPED (mmo5 fold): it arrived while this joiner's world was not running or before the host"
             " was seen in this world - it is not kept to fire later");
}
std::string SessionClosingToken()
{
    return N(g_closingSent) + "," + N(g_closingSeenCount) + "," + N(g_closingIgnoredOnHost) + "," + N(g_closingDropped);
}

void SessionLeave()
{
    if (g_transport)
    {
        // review-s6 H1 (and the "one call away" item): leaving is the same event for our world as the peer dropping - the peer's
        // copies here have no feed. Run the cleanup while the transport still exists (OnPeerGone is skipped without one).
        coop::ItemsEscrowSessionEnd();   /* inv7c (D6): leaving ends the session - escrowed TAKEs go back to their owner first */
        if (g_lastLinkState == LINK_UP) { const int goneSlot = coop::PeerGoneCaptureSession(); coop::StoreNoteLinkDown(); coop::PlayerFactionOnLinkDown(); coop::OnPeerGone(goneSlot); }   /* M8: captured before the slot is forgotten */   /* inv7e2 fold: leaving a LIVE link is this game's link going down (T418: B's leave logs no link DOWN); the SessionLeave a host/join makes first is not. M5b fold 1 (item 3): it forgets the peer's slot like SessionOnLinkDown */
        g_lastLinkState = LINK_DOWN;
        if (g_transport->State() == LINK_UP)
        {
            g_transport->Send(0, MSG_BYE, 0, 0, CH_RELIABLE);
            g_transport->Flush();   // F069: without this the BYE dies in the queue
        }
        g_transport->Disconnect();
        delete g_transport;
        g_transport = 0;
        DebugLog("[net] session closed");
    }
    g_isHost   = false;
    g_myPeerId = 0;
    /* P7v: THE INBOX THIS USED TO CLEAR NO LONGER EXISTS, and nothing clears the arrival queue either. The
       session generation below is bumped instead: queued entries of the session that just ended are stamped
       with the old value and the drain discards them when it REACHES them - lazily, in arrival order, with
       every discarded gone-mark's permanent bit set first. That is what removes review-p7h H-2 (a loop that
       refilled the inbox this function had just emptied) and review-p7p H-2 (a selective clear that dropped
       one origin's deletes while keeping the other's records). */
    ::InterlockedIncrement(&g_sessionLinkGen);
    g_connectGenSeen = 0;
    g_sessionLeavePending = 0;
    ::InterlockedIncrement(&g_hostClosingEpisode);   /* M11a S3 review fold (F2): nor one from before the leave */
    g_hostClosingSeen = coopgl::HostClosingFlagStep(g_hostClosingSeen, coopgl::kHcSessionLeft);   /* mmo5: an announcement belongs to the session that carried it */
    if (g_glSeriesOpen) { ++g_glAbandoned; DebugLog("[net] link1: the game link's re-dial series " + N(g_glSeries) + " ends - the session was left (" + N((long long)g_glAttempts) + " attempt(s))"); }
    g_glSeriesOpen = 0; g_glAttempts = 0;   /* link1: a left session is never re-dialled - there is no transport to dial with */
    /* M8 review F4: A LEAVE WITH THE LINK ALREADY DOWN. Its DOWN edge captured the departed peer's rows and queued the peer-gone action,
       and the generation bump above makes the drain DISCARD that action - whose sweep would then find g_owner cleared below, count
       every mark alreadyGone and leave the copies standing with no owner. So the sweep runs HERE, before the clear, while the engine
       may be written; with writes blocked (a load, a teardown) the marks are spent unswept. Either way none outlives the session.
       With the link up, OnPeerGone above has already taken every mark. */
    if (!g_departedPending.empty() && !coop::EngineWritesBlocked()) coop::PeerGoneSweepPending("the session was left with the departed peer's rows still captured");
    g_departedPending.clear();
    coop::MedicalForgetAllCopies();   /* the owners' medical words per copy end with the session that carried them */
    // Remote ownership claims die with the session that carried them. OURS do not - see the note
    // on g_localOwned, which is deliberately not touched here.
    SessionForgetRows(coop::StoreLiveReady(), "the session was left");   /* the world-road rows stay while the world link is up */
    // F311. So does the peer's view distance, and for exactly the same reason: it is a fact ABOUT
    // A PEER, and this peer is gone. Left standing, the next session's host would keep adopting
    // against the PREVIOUS client's setting while every instrument agreed it was the current one -
    // `OnHello` would print it, `DeriveRadiusFromViewDistance` would see no change and log nothing,
    // and the report would say `radiusSource=client-viewDistance`. Four instruments, all agreeing,
    // all wrong, which is the failure shape F302 exists to end.
    ClearPeerViewDistance();
}

/* M5b (T-197 piece 5; session protocol 107) - THE SESSION PEER'S PLAYER NUMBER, SAID BY THE PEER ITSELF. Owner records are
   keyed by the owning player's permanent slot, so this game must know which slot the game on the session link holds. It
   used to be learnt from any slot-numbered faction name that arrived here (playerfaction.cpp ResolveWireFaction), which a
   relayed SPAWN / CONTEXT or a third player's name on this link could move; now only PEER_SLOT sets it. Sent once the
   session is ready (the host's WELCOME / the joiner's WELCOME) and again whenever this game's slot changes. On arrival the
   claims recorded under the raw link id before it (none in the designed order: PEER_SLOT goes at the title) are re-keyed. */
static void PeerSlotAnnounceTick()
{
    if (g_transport == 0 || g_transport->State() != LINK_UP) return;
    const long gen = SessionLinkGen();
    if (g_slotReadyGen != gen) return;
    const int mine = coop::StoreMySlot();   /* the slot the WELCOME of THIS notebook link gave, never one remembered from another world */
    if (mine < 0 || (unsigned int)mine > cooplive::kLiveSlotMax) return;
    if (g_slotSentGen == gen && g_slotSentValue == mine) return;
    std::vector<char> b; PutU32(&b, (unsigned int)mine);
    if (!g_transport->Send(0, MSG_PEER_SLOT, &b[0], b.size(), CH_RELIABLE)) return;
    g_slotSentGen = gen; g_slotSentValue = mine; ++g_peerSlotSent;
    DebugLog("[net] -> PEER_SLOT " + N((long long)mine) + " (this game's permanent player number, for the session peer's owner records)");
}
void OnPeerSlot(const Message& m)
{
    unsigned int slot = 0;
    if (m.payload.size() < 4 || !GetU32(m.payload, 0, &slot) || slot > cooplive::kLiveSlotMax)
    {
        ++g_peerSlotBad;
        ErrorLog("[net] <- PEER_SLOT malformed (" + N((long long)m.payload.size()) + " bytes) - ignored (peerSlot bad " + N(g_peerSlotBad) + ")");
        return;
    }
    const int mine = coop::StoreMySlot();   /* M5b fold 1 (item 4): THIS notebook link's slot only - never one remembered from another world (as PeerSlotAnnounceTick) */
    if (mine >= 0 && (int)slot == mine)
    {
        ++g_peerSlotSelf;
        ErrorLog("[net] <- PEER_SLOT " + N((long long)slot) + " is THIS game's own number - two games on one slot? Refused; the peer's claims stay keyed by the link (peerSlot self " + N(g_peerSlotSelf) + ")");
        return;
    }
    const int was = coop::LinkPeerSlot();
    ++g_peerSlotRecv;
    if (was >= 0 && was != (int)slot) ++g_peerSlotChanged;
    coop::NoteLinkPeerSlotAnnounced((int)slot);
    long long n = 0;
    const unsigned int key = cooplive::RelayPeerId(slot);
    /* M5b fold 1 (item 2): a CHANGED number also moves the claims recorded under the peer's OLD number, else they stay keyed by a
       slot the peer no longer holds and its own messages are refused for them. */
    const bool moved = (was >= 0 && was != (int)slot);
    const unsigned int wasKey = moved ? cooplive::RelayPeerId((unsigned int)was) : 0u;
    for (std::map<unsigned int, unsigned int>::iterator it = g_owner.begin(); it != g_owner.end(); ++it)
    {
        if (g_localOwned.find(it->first) != g_localOwned.end()) continue;
        if (g_departedPending.find(it->first) != g_departedPending.end()) continue;   /* M8: a row captured at a DOWN edge is the DEPARTED peer's - never re-keyed to this link's */
        if (!cooplive::IsRelayPeer(it->second) || (moved && it->second == wasKey)) { it->second = key; ++n; }
    }
    g_ownerRekeyed += n;
    DebugLog("[net] <- PEER_SLOT " + N((long long)slot) + (moved ? " (was " + N((long long)was) + ")" : std::string())
             + " - the session peer's claims are keyed by its player number; " + N(n) + " re-keyed from the link id"
             + (moved ? std::string(" or its old number") : std::string()) + " (ownerRekeyed " + N(g_ownerRekeyed) + ")");
}
void SessionMoveViaLive(int n)
{
    g_moveViaLiveBudget = n < 0 ? 0 : n;
    DebugLog("[net] livemove: the next " + N((long long)g_moveViaLiveBudget) + " MOVE(s) go through the notebook (LIVE WORLD) when its road is up, else on the session link");
}

/* ONE DISPATCHER, used by the inline table above and by the drain's replay below, so a message applied live
   and the same message applied out of the queue cannot take different code (lesson 11). */
void SessionDispatchOne(const Message& m)
{
    switch (m.type)
    {
    case MSG_HELLO:   OnHello(m);   break;
    case MSG_WELCOME: OnWelcome(m); break;
    case MSG_PING:    OnPing(m);    break;
    case MSG_PONG:    OnPong(m);    break;
    case MSG_PEER_SLOT: OnPeerSlot(m); break;   /* M5b: the session peer's permanent slot */
    case MSG_SPAWN:   OnSpawn(m);   break;
    case MSG_APPEARANCE: OnAppearance(m); break;
    case MSG_CLOTHING:   OnClothing(m);   break;
    case MSG_ITEM_MOVE:  OnItemMove(m);  break;   /* E22a / decision 38 */
    case MSG_ITEM_REQUEST: OnItemRequest(m); break;   /* E22b / decision 38 part (2) */
    case MSG_ITEM_CONFIRM: OnItemConfirm(m); break;   /* E22b */
    case MSG_ITEM_PLACED:  OnItemPlaced(m);  break;   /* E22b-2 (P6b) - the rollback's ack */
    case MSG_ITEM_REVOKE:  OnItemRevoke(m);  break;   /* E22b-2 (P6b) - undo a give */
    case MSG_PARITY_REQ:   OnParityReq(m);   break;   /* par1: the other game asks us, the holder, about its boxes */
    case MSG_PARITY_BOX:   OnParityBox(m);   break;   /* par1: the holder's answer for one of our boxes */
    case MSG_COMBATMODE: OnCombatMode(m); break;
    case MSG_SWING:      OnSwing(m);      break;
    case MSG_XFER:       OnXfer(m);       break;
    case MSG_XFER_ACK:   OnXferAck(m);    break;
    case MSG_SQUAD_LEAD: OnSquadLead(m);  break;   /* T-1 B1 restructure: the other game's announced squad leaders */
    /* 58 (MSG_KEEPER) retired by protocol 89: an 88 game never gets past HELLO / WELCOME */
    case MSG_UNLOAD:     OnUnload(m);     break;
    case MSG_RELEASE:    OnRelease(m);    break;   /* M7a A1 build 2 [a1b2-sp5]: an engine put-away offered for adoption (or a REVOKE) */
    case MSG_RELEASE_ACK: OnReleaseAck(m); break;   /* M7a A1 build 2 */
    case MSG_ROSTER:     OnRoster(m);     break;   /* M7a A1 build 1 [a1b1-sp36] */
    case MSG_RECEIPT:    OnReceipt(m);    break;   /* M7a A1 build 1 */
    case MSG_RELATION:   OnRelation(m);   break;
    case MSG_RELSYNC:    OnRelSync(m);    break;
    case MSG_INTENT:     OnIntent(m);     break;
    case MSG_INSIDE:     OnInside(m);     break;   /* P25 fold 2: the owner says which building its character is in */
    case MSG_MOVESTOP:   OnMoveStop(m);   break;   /* the owner's stated stop for one of its characters */
    case MSG_CONTEXT:    OnContext(m);    break;
    case MSG_DESPAWN: OnDespawn(m); break;
    case MSG_MOVE:    OnMove(m);    break;
    case MSG_TASK:    OnTask(m);    break;
    case MSG_HIT:     OnHit(m);     break;
    case MSG_STATE:   OnState(m);   break;
    case MSG_DOOR_STATE: OnDoorState(m); break;   /* E45 / decision 40 */
    case MSG_SAY:        OnSay(m);       break;   /* P3: an NPC speech line from the character's owner */
    case MSG_STATS:      OnStats(m);     break;   /* S1: the owner's 44 stat values */
    case MSG_CRIME:      OnCrime(m);     break;   /* crime3: the owner's current crime for a character */
    case MSG_BOUNTY:     OnBounty(m);    break;   /* crime5: a bounty list (owner) or a copy's additions */
    case MSG_CARRY_BREAK: OnCarryBreak(m); break;  /* arrest1: the body's game says its carry by our carrier ended */
    case MSG_PRISON:     OnPrison(m);    break;   /* arrest2: the other game's guard caged our character */
    case MSG_TREAT:      OnTreat(m);     break;   /* heal1: the other game's medic treated our character's copy */
    case MSG_NAME:       OnName(m);      break;   /* names1: the owner's character name for our copy */
    case MSG_SLAVE:      OnSlave(m);     break;   /* slave1: the owner's slave state for our copy */
    case MSG_CAPTURE:    OnCapture(m);   break;   /* P11: the other game's slaver processed our character's copy */
    case MSG_CAPTURE_DONE: OnCaptureDone(m); break;   /* P11: the owner's answer to our capture request */
    case MSG_CAPTURE_PLACED: OnCapturePlaced(m); break;   /* P11 f3: which taken rows landed in the captor's slaver */
    case MSG_SHOT:       OnShot(m);      break;   /* P104 fix: the other game's bolt struck our character's copy there */
    case MSG_EFFECT:     OnEffect(m);    break;   /* T-327: a request about our character / an answer for our eater */
    case MSG_NOT_SHOWN:  OnNotShown(m);  break;   /* T-354: our character is not shown on the sender's game (its table refused it) */
    case MSG_RESEND:     OnResend(m);    break;   /* a lost copy's ask (we own the character) / the owner's answer to ours */
    case MSG_TOWN_PRICES: OnTownPrices(m); break;   /* T-619: one town's local trade multipliers, by either road */
    case MSG_HIRE:       OnHire(m);      break;   /* recruit1: a hire request / answer / result */
    case MSG_TALK:       OnTalk(m);      break;   /* P26 stages 1-3: a conversation PROMPT / ANSWER / END */
    case MSG_BUILD:      OnBuild(m);     break;   /* build1-b: the other player placed a construction */
    case MSG_SESSION_CLOSING: OnSessionClosing(m); break;   /* mmo5: the host is leaving on purpose */
    case MSG_BYE:
        DebugLog("[net] <- BYE from peer " + N(m.peer));
        break;
    default:
        DebugLog(std::string("[net] <- ") + MsgTypeName(m.type)
                 + " (no handler yet, M0) len=" + N((long long)m.payload.size()));
        break;
    }
}
void SessionDispatchQueued(int type, unsigned int peer, const std::vector<char>& payload)
{
    if (type == (int)cooplive::kInnerCatchupEnd) { SessionCatchupAskEndApply(peer, payload); return; }   /* M7a A1 build 1 [a1b1-sp20]: the ask book only (the sweep is retired) */
    if (type == (int)cooplive::kInnerOwnerMoved) { SessionOwnerMovedApply(peer, payload); return; }   /* M7a2 item 3 [m7a2-sp5]: queued behind that owner's stream */
    /* No transport test here on purpose: every send inside every handler already refuses on a dead or
       down link, and an entry whose link generation no longer matches was discarded before this call. */
    Message m;
    m.type = (MsgType)type;
    /* M5b: every game-to-game handler sees the SENDING PLAYER's key - the session peer's slot once PEER_SLOT said it, the
       stamped slot for a relayed one - so a sender id a handler stores and compares later (pending item requests, conversations,
       door publishers, the parked STATE wants ...) is the same on both roads. RELATION and RELSYNC keep the raw id: their
       handlers read the ROAD from it (M5a). */
    m.peer = (type == (int)MSG_RELATION || type == (int)MSG_RELSYNC) ? peer : OwnerKeyOf(peer);
    m.payload = payload;
    /* M7a: the character stream counted by the road it came by (the raw sender: a relayed one carries the marker) */
    const int csi = cooplive::CharStreamIndex((unsigned int)type);
    if (csi >= 0) { if (cooplive::IsRelayPeer(peer)) ++g_charLiveRecv[csi]; else ++g_charSessRecv[csi]; }
    {   /* M7b slice 4: the side messages counted by the road they came by */
        const int sg = cooplive::AddrGroupOf((unsigned int)type);
        if (sg >= cooplive::kAddrGroupSay && sg <= cooplive::kAddrGroupDoor) ++g_sideRecv[cooplive::IsRelayPeer(peer) ? 1 : 0][sg - cooplive::kAddrGroupSay];
        if (sg >= cooplive::kAddrGroupName && sg <= cooplive::kAddrGroupHire) ++g_nshRecv[cooplive::IsRelayPeer(peer) ? 1 : 0][sg - cooplive::kAddrGroupName];
    }
    /* M7a: the RELAYED-OWNER GATE (cooplive::RelayedStreamGate) - APPEARANCE, CLOTHING, COMBATMODE, SWING, INTENT and CONTEXT have
       no owner test in their handlers (they apply to a copy this game holds, which was enough while the session peer was the
       only sender). Through the notebook any game can send them, so a relayed one for a uid this game runs, or whose recorded
       owner is another player, is refused and counted (relayOwn[refused], charLive[foreignRefused]). An unknown uid passes:
       CONTEXT comes before its SPAWN. */
    if (cooplive::IsRelayPeer(peer) && cooplive::CharStreamNeedsOwnerGate((unsigned int)type) && payload.size() >= 4)
    {
        unsigned int guid = 0; std::memcpy(&guid, &payload[0], 4);
        std::map<unsigned int, unsigned int>::const_iterator gown = g_owner.find(guid);
        const bool known = (gown != g_owner.end());
        const int gate = cooplive::RelayedStreamGate(g_localOwned.find(guid) != g_localOwned.end(), known, known && OwnerMatch(gown->second, peer));
        if (gate != cooplive::kStreamGatePass)
        {
            ++g_relayOwnIn; ++g_relayOwnRefused; ++g_charRelayForeign;
            if (gate == cooplive::kStreamGateOwnedHere) DualRunNote(guid, peer);   /* M7a A1 build 1 [a1b1-sp17]: another game streams a person this game runs - a CHECK settles it (3.9) */
            if (cooplive::LiveLogThis(g_charRelayForeign))
                DebugLog(std::string("[net] relayed ") + MsgTypeName((MsgType)type) + " for uid=" + N(guid) + " from slot " + N((long long)cooplive::RelayPeerSlot(peer))
                         + " REFUSED - " + (gate == cooplive::kStreamGateOwnedHere ? "this game runs that character" : "another player owns that character")
                         + " (charLive foreignRefused " + N(g_charRelayForeign) + "; the first 5 and every 100th are logged)");
            return;
        }
    }
    if (cooplive::IsRelayPeer(peer) && type == (int)MSG_SPAWN && payload.size() >= 4)   /* M7a fold (review 2026-09-30 F4) */
    {
        unsigned int suid = 0; std::memcpy(&suid, &payload[0], 4);
        std::map<unsigned int, unsigned int>::const_iterator sown = g_owner.find(suid);
        const bool sknown = sown != g_owner.end() && g_localOwned.find(suid) == g_localOwned.end();   /* this game's own uid: OnSpawn refuses and counts it */
        if (cooplive::RelayedSpawnRefused(sknown, sknown && OwnerMatch(sown->second, peer)))
        {
            ++g_relaySpawnForeign; ++g_relayOwnIn; ++g_relayOwnRefused;
            if (cooplive::LiveLogThis(g_relaySpawnForeign))
                DebugLog("[net] relayed SPAWN for uid=" + N(suid) + " from slot " + N((long long)cooplive::RelayPeerSlot(peer))
                         + " REFUSED - its recorded owner is another player and no hand-over named this one (relaySpawnForeign " + N(g_relaySpawnForeign) + ")");
            return;
        }
    }
    if (csi >= 0 && payload.size() >= 4) { unsigned int su = 0; std::memcpy(&su, &payload[0], 4); RosterStreamSeen(su); }   /* M7a A1 build 1 [a1b1-sp18]: a stream after a CHECK keeps a NOT-LIVE copy */
    if (type == (int)MSG_MOVE && MoveHoldIfUnknown(peer, payload)) return;   /* M7a2 item 6 [m7a2-sp7]: before its catch-up state - held */
    const bool relayedOwn = cooplive::IsRelayPeer(peer) && type != (int)MSG_RELATION && type != (int)MSG_RELSYNC;
    const __int64 refusedBefore = g_remoteMayWriteRefused;
    const long long ownedRefusedBefore = g_uidOwnedByPeerRefused;   /* M5b fold 1 (item 6): SAY / CRIME / CARRY_BREAK refuse through UidOwnedByPeer */
    g_effViaRelay = cooplive::IsRelayPeer(peer);   /* T-327: MSG_EFFECT answers on the road its request came by */
    g_dispCameBySession = cooplive::IsRelayPeer(peer) ? 0 : 1;   /* [a1b1f1-sp1] [F1]: the XFER_ACK answers on the road its XFER came by */
    SessionDispatchOne(m);
    g_effViaRelay = false;
    g_dispCameBySession = 0;
    if (relayedOwn) { ++g_relayOwnIn; if (g_remoteMayWriteRefused != refusedBefore || g_uidOwnedByPeerRefused != ownedRefusedBefore) ++g_relayOwnRefused; }
    if (csi >= 0 && payload.size() >= 4 && (g_remoteMayWriteRefused != refusedBefore || type == (int)MSG_SPAWN))   /* M7a A1 build 1 [a1b1-sp19]: the dual-run detector (3.9). Fold 1 [a1b1f1-sp3] [F1]: both roads - the session link's stream feeds it too */
    { unsigned int luid = 0; std::memcpy(&luid, &payload[0], 4); if (g_localOwned.find(luid) != g_localOwned.end()) DualRunNote(luid, peer); }
    if (type == (int)MSG_SPAWN) MoveHoldReplayAfterSpawn(payload);   /* M7a2 item 6 [m7a2-sp8]: a MOVE held for it applies now */
}
void SessionActionDiscarded(int act)
{
    if (act == coop::kActPeerGone) coop::PeerGoneSweepPending("the queued peer-gone action was discarded (its link generation moved)");   /* M8: the rows it captured are still the departed peer's - removed now (engine writes are allowed at this point of the drain), not left behind or re-keyed */
    /* P7w (F599 / review-p7v H-2). The entry was stamped with the session generation at enqueue and the
       drain found that generation had moved, so the action speaks for a link that no longer exists and is
       NOT performed. Everything it would have done is either already done by the new link's own edges or
       is wrong for them - SetTownGenClientMode(false) after a healthy reconnect declared true is the E8
       residual, sticky for the rest of the process. What is NOT automatic is the latch. */
    if (act == coop::kActSessionLeave && g_sessionLeavePending != 0)
    {
        g_sessionLeavePending = 0;
        ErrorLog("[net] a queued session TEARDOWN was discarded at the drain - the session generation moved"
                 " between the refusal and the drain, so the refusal belonged to a link that is already gone"
                 " and performing it would have torn down a DIFFERENT, healthy session (F599). The latch that"
                 " holds the poll loop shut is cleared with it: it is cleared only inside SessionLeave, so"
                 " discarding the action without clearing it would silence this session permanently."
                 " Counted as droppedStaleAction[sessionLeave].");
    }
}
void SessionPerformAction(int act, const std::vector<char>& payload)
{
    if (act == coop::kActPeerGone) { int goneSlot = -1; if (payload.size() == 4) std::memcpy(&goneSlot, &payload[0], 4); coop::OnPeerGone(goneSlot); return; }   /* M8 */
    if (act == coop::kActSessionLeave)
    {
        SessionLeave();   /* clears the latch itself - see the note inside it */
        return;
    }
    if (act == coop::kActResendHello)
    {
        if (g_transport != 0 && g_transport->State() == LINK_UP && !g_isHost && !coop::EngineWritesBlocked())
        {
            SendHello();
            ++g_helloResentWithViewDistance;
        }
        else ++g_helloResendSkipped;
        return;
    }
}
/* ---- THE ONE POLL. SessionTick and the load gates' pump are the SAME BODY (design-noworld-queue 5.1), so
   they cannot classify or edge-detect differently. Returns true if a MSG_WELCOME was dispatched. ---- */
bool SessionPollAndQueue()
{
    if (g_transport == 0) return false;
    /* BACK-PRESSURE (design-noworld-queue 1.4 step 1). Past the high-water mark with the predicate blocked,
       this origin's socket is NOT POLLED: ENet buffers, and reliable channels retransmit - which is exactly
       the pre-P7h behaviour that lost nothing. Unreliable LEVELS are dropped by ENet, which is correct for
       levels and is the only loss this design permits. */
    if (coop::InQueueSaturated(coop::kOriginSession) != 0 && coop::EngineWritesBlocked()) return false;

    std::vector<Message> in;
    g_transport->Poll(&in);

    bool welcomed = false;
    for (size_t i = 0; i < in.size(); ++i)
    {
        /* Nothing further from a session being refused is worth queueing. */
        if (g_sessionLeavePending != 0) break;
        if (g_transport == 0) break;
        const Message& m = in[i];
        /* M2 (decisions 32/44/54): a notebook-only type is neither applied nor queued - dropped and counted. */
        if (SessionMsgIsNotebookOnly((int)m.type) != 0) { ++g_nbOnlyDropped; continue; }
        if (SessionMsgIsInline((int)m.type) != 0)
        {
            if (m.type == MSG_WELCOME) welcomed = true;
            SessionDispatchOne(m);
            continue;
        }
        /* MSG_MOVE is the one message this build can prove is a LEVEL: unreliable, one per owned character
           per send cadence, and superseded by the next one for the same uid - the uid is its first four
           bytes. M16 fold 5: MSG_STATE is a FULL SNAPSHOT of one character - latest wins (the send bound,
           sendbound.h, keeps only the newest waiting one per character); this queue still keeps every STATE
           (classed EDGE below, never merged), which costs one queue slot each and nothing else.
           MSG_APPEARANCE and MSG_CLOTHING are level-SHAPED and are deliberately left as EDGES: the default is
           EDGE until the handler has been read, and mis-collapsing an edge is a permanent divergence while
           mis-keeping a level costs one queue slot. */
        int cls = coop::kClassEdge;
        unsigned int subject = 0;
        if (m.type == MSG_MOVE && m.payload.size() >= 4) { cls = coop::kClassLevel; std::memcpy(&subject, &m.payload[0], 4); }
        /* M2: kScopeSave was taken here by RECORD and RECORD_GONE, the two that describe the SAVE rather than this
           engine instance of the world. Both are notebook-only now and were dropped above, so everything the
           session link still queues names a live-world object. */
        const int scope = coop::kScopeWorld;
        /* P3-b (review-p3 LOW): a speech line is a moment, not a state - one arriving during a load is dropped
           and counted, never queued to be shown late over a world that has moved on. */
        if (m.type == MSG_SAY && coop::EngineWritesBlocked()) { coop::SayNoteDroppedBlocked(); continue; }
        /* crime3: a crime arriving during a load names a world that is being rebuilt - dropped and counted like SAY. */
        if (m.type == MSG_CRIME && coop::EngineWritesBlocked()) { coop::CrimeNoteDroppedBlocked(); continue; }
        if (m.type == MSG_BOUNTY && coop::EngineWritesBlocked()) { coop::BountyNoteDroppedBlocked(); continue; }   /* crime5 */
        if (m.type == MSG_CARRY_BREAK && coop::EngineWritesBlocked()) { coop::CarryBreakNoteDropped(); continue; }   /* arrest1 */
        /* A death request (PRISON kind DEATH) arriving during a load is dropped and counted: a kill made against the old world must
           not land in the newly loaded one. Other PRISON and TREAT messages arriving during a load are queued like any other edge:
           the load bumps the world generation once, at its start, so an entry queued during it carries the new generation and the
           drain runs it in the new world. Of those lost anyway, only IN (a caging) and TREAT are sent again by their sender until
           the owner's word shows them; RELEASE, BED IN and the lock requests are applied after the load and checked again by
           their handlers there. */
        if (m.type == MSG_PRISON && coop::EngineWritesBlocked() && !m.payload.empty()
            && cooprison::PrisonPayloadIsDeath(&m.payload[0], m.payload.size())) { coop::PrisonNoteDeathDroppedLoad(); continue; }
        coop::InQueueEnqueue((int)m.type, coop::kOriginSession, scope, cls, subject, m.peer, m.payload);
    }
    /* ---- P7v fold 3: THE EDGES RUN HERE, BELOW THE LOOP, AND BOTH REASONS ARE LOAD-BEARING.
       ORDERING: HEAD ran OnPeerGone after the dispatch loop because "the messages already polled arrived
       BEFORE the disconnect and a SPAWN among them would re-create exactly what the cleanup is removing"
       (review-session S6). A kActPeerGone enqueued above the loop would carry a LOWER arrival sequence than
       those messages and invert that.
       THE GENERATION STAMP, which is the stronger half: messages polled in the same batch as the DISCONNECT
       belong to the link that just died, so they must carry the OLD session generation and be DISCARDED at
       the drain rather than applied after the cleanup (design-noworld-queue 2.3). Bumping above the loop
       would stamp them with the new generation and keep them.
       THE EDGE ITSELF IS READ FROM A MONOTONIC CONNECT COUNTER AND NOT FROM State() (review-p7h M-5): ENet's
       Poll can drain a DISCONNECT and a CONNECT in ONE call, leaving state_ at LINK_UP with no edge visible.
       A state comparison cannot see that pair; a counter cannot miss it.
       THE COST, STATED: a message arriving in the same Poll as a CONNECT is stamped with the pre-bump
       generation and discarded. On a first connect there are none - the peer cannot send before the
       handshake - and on a drop-then-reconnect-in-one-poll discarding is what the paragraph above wants. ---- */
    if (g_transport == 0) return welcomed;
    const unsigned int cg = g_transport->ConnectGen();
    if (cg != g_connectGenSeen)
    {
        g_connectGenSeen = cg;
        if (g_lastLinkState == LINK_UP) SessionOnLinkDown();
    }
    const LinkState now = g_transport->State();
    if (now == LINK_UP && g_lastLinkState != LINK_UP) SessionOnLinkUp();
    else if (now != LINK_UP && g_lastLinkState == LINK_UP) SessionOnLinkDown();
    else if (now != LINK_UP) g_lastLinkState = now;
    return welcomed;
}
/* link1 (T416): THE JOINER RE-DIALS ITS GAME LINK WHILE ITS WORLD RUNS, the way StoreRedialTick re-dials the notebook.
   Before this, a game link lost in a running world stayed down for the rest of the run: the only automatic dial was
   config.cpp's title-screen retry, which stops at world load. The decision is coopgl::GameRedialDecide (offline suite).
   It dials through the SAME transport (ITransport::Join), not SessionJoin: SessionJoin runs SessionLeave, which bumps
   the session generation and would discard the kActPeerGone the drop has queued before the drain performs it. The
   drop's own edge already queued that cleanup; the re-link's edge (SessionOnLinkUp) sends the HELLO, exactly as the
   harness's `join` after a load does. Main thread only (SessionTick); never from the load gates' pump. */
static void GameLinkRedialTick()
{
    int sock = coopgl::kGlSockDown;
    if (g_transport != 0)
    {
        const LinkState ls = g_transport->State();
        if (ls == LINK_UP) sock = coopgl::kGlSockUp;
        else if (ls == LINK_CONNECTING) sock = coopgl::kGlSockConnecting;
    }
    const int world = (coop::GameplayRunning() && !coop::EngineWritesBlocked()) ? 1 : 0;
    const DWORD now = ::GetTickCount();
    const int act = coopgl::GameRedialDecide(g_transport != 0 ? 1 : 0, g_isHost ? 1 : 0,
                                             (!g_joinAddr.empty() && g_joinPort != 0) ? 1 : 0, sock, world,
                                             (unsigned int)(now - g_glLastMs), g_glAttempts);
    if (act == coopgl::kGameRedialNotJoiner || act == coopgl::kGameRedialIdle) return;
    if (act == coopgl::kGameRedialWaitConnecting || act == coopgl::kGameRedialSkipNoWorld) { g_glLastMs = now; return; }
    if (!g_glSeriesOpen)
    {
        g_glSeriesOpen = 1; g_glAttempts = 0; g_glLastMs = now; ++g_glSeries;
        DebugLog("[net] link1: the game link to " + g_joinAddr + ":" + N((long long)g_joinPort) + " is DOWN in a running world -"
                 " re-dialling it 2, 4, 8 and 16 s apart, then every 30 s, until it answers or this game leaves (series "
                 + N(g_glSeries) + "; attempts are counted on the [net] REPORT line as gameRedial[...], not logged one by one)");
        return;
    }
    if (act != coopgl::kGameRedialNow) return;
    ++g_glAttempts; ++g_glAttemptsTotal; g_glLastMs = now;
    std::string err;
    if (!g_transport->Join(g_joinAddr, g_joinPort, &err)) ++g_glDialFailed;   /* stays down; the next interval runs */
}
void SessionTick()
{
    ++g_tick;
    OwnedMirrorMaintain();   /* O1-b: main thread, once per tick, link or not */
    SessionCatchupTick();   /* M7a2 item 7 [m7a2-sp9]: the catch-up's clocks, link or not */
    LiveOwnerTick();   /* M7a A1 build 1 [a1b1-sp37]: ROSTER HASH / CHECK, the outbox's resends, the receipts owed - link or not (the world road needs no session) */
    /* M7a2 fold 1 item 8 [m7a2f-ap2] (was [m7a2f-sp13]): the reverse catch-up's walk runs in SessionCatchupApplyTick, after the drain (manager ruling) */
    if (g_transport == 0) return;
    SessionPollAndQueue();
    PeerSlotAnnounceTick();   /* M5b: after the poll, so a ready edge seen this tick announces this tick */
    GameLinkRedialTick();   /* link1: after the poll, so this tick's DOWN edge has already been seen */
}
bool SessionPumpDeferring()
{
    return SessionPollAndQueue();
}

bool SessionPing()
{
    if (g_transport == 0 || g_transport->State() != LINK_UP)
    {
        if (coop::StoreWorldRoadPing("the ping verb")) return true;   /* M11a S3 (decision 4(a)): a world-road game pings the operator by LIVE SLOT; a session-road game is unchanged */
        DebugLog("[net] ping: no live link");
        return false;
    }
    std::vector<char> b;
    PutU32(&b, (unsigned int)(++g_pingSeq));
    g_pingSentTick = g_tick;
    bool ok = g_transport->Send(0, MSG_PING, &b[0], b.size(), CH_RELIABLE);
    DebugLog("[net] -> PING seq=" + N((long long)g_pingSeq) + (ok ? "" : " (SEND FAILED)"));
    return ok;
}

void SessionReport()
{
    if (g_transport == 0)
    {
        DebugLog("[net] REPORT: no session " + coop::SteamProbeReportToken() + " " + coop::UpnpReportToken() + " " + AddrCountsString() + LostCopyCountsString());   /* the side-message, lost-copy, name / slave / hire road counters still print with no session link (the world road carries them) */
        return;
    }
    std::stringstream ss;
    ss << "[net] REPORT backend=" << g_transport->BackendName()
       << " role=" << (g_isHost ? "HOST" : "CLIENT")
       << " link=" << LinkStateName(g_transport->State())
       << " myPeer=" << N(g_myPeerId)
       << " sent=" << N((long long)g_transport->SentCount())
       << " recv=" << N((long long)g_transport->RecvCount())
       << " enetRTT=" << N(g_transport->RoundTripMs()) << "ms"
       << " " << coop::SteamProbeReportToken()   /* t290s0-report-line */
       << " " << coop::UpnpReportToken()   /* T-53: upnp[asked,mapped,already,failed,removed,noRouter,off] */
       << " " << coop::HandoffReleaseReportToken()   /* M7a A1 build 2 [a1b2-sp6]: release[...] index[...] */
       << g_transport->SendBoundToken();   /* M16 (T-197): what waits to be sent on the session link, appended */

    // T019: this used to print g_owner.size() under the label "ownedUids", which counts
    // every uid with a KNOWN owner - including the peer's. Two distinct numbers now, so
    // the authority map can be read at a glance without being misread.
    // From g_localOwned, because that is what IsUidMine answers (F279). Counting g_owner filtered
    // by peer id used to be the same thing and stopped being so: g_owner is cleared on leave, so
    // after a rejoin this printed uidsMine=0 while ~130 uids were owned and actively streaming -
    // the one diagnostic that exists to read the authority map at a glance, reading zero in exactly
    // the situation F279 was written for.
    //
    // F287 RECORDED THIS AS FIXED AND IT WAS NOT. The edit targeted a differently-formatted block,
    // silently matched nothing, and I never checked. A review caught the divergence between the
    // findings file and the code. **An edit that is not verified is not a change.**
    size_t mine = g_localOwned.size();
    ss << " uidsKnown=" << N((long long)g_owner.size())
       << " uidsMine=" << N((long long)mine)
       << " stateRecv=" << N(g_stateRecv)
       // review-session S6. `peerGone` counts LINK_UP -> down edges handled this game session, and
       // `peerGoneClaims` the peer claims dropped across all of them - not what is held now.
       << " peerGone=" << N(g_peerGoneEvents)
       << " peerGoneClaims=" << N(g_peerGoneClaims)
       << " peerGoneM8[captured,alreadyGone,discardSweeps,pending]=" << N(g_peerGoneCaptured) << "," << N(g_peerGoneAlreadyGone) << "," << N(g_peerGoneDiscardSweeps) << "," << N((long long)g_departedPending.size())   /* M8: alreadyGone = captured rows PLAYER_GONE removed first (or re-claimed) - never removed twice */
       << " peerSlot[sent,recv,self,bad,changed]=" << N(g_peerSlotSent) << "," << N(g_peerSlotRecv) << "," << N(g_peerSlotSelf) << "," << N(g_peerSlotBad) << "," << N(g_peerSlotChanged)   /* M5b */
       << " linkPeerSlot=" << N((long long)coop::LinkPeerSlot()) << " ownerRekeyed=" << N(g_ownerRekeyed)
       << " relayOwn[in,refused]=" << N(g_relayOwnIn) << "," << N(g_relayOwnRefused)
       << AddrCountsString()   /* M7b slice 1: each request by road, per type group */
       << " moveViaLive[sent,failed]=" << N(g_moveViaLive) << "," << N(g_moveViaLiveFailed)
       /* P7v: deferredIn[...] is RETIRED with the inbox it counted. The queue's own numbers are on the
          [STORE] REPORT line - inQueue[...], inDrain[...] and picture[...] - because there is one queue for
          both links and printing half of it beside one of them would invite the wrong reading. */
       << " sessionLinkGen=" << N((long long)SessionLinkGen())
       << " hello[viewDistanceUnknown,resentWithViewDistance,resendSkipped,welcomeProtocolRefused,helloProtocolRefused]="
       << SessionRefusalCounts()
       << " nbOnlyDropped=" << N(g_nbOnlyDropped)   /* M2: notebook-only types that still arrived on this link - must read 0 */
       << " relViaLive=" << N(g_relViaLive) << " relViaSession=" << N(g_relViaSession) << " relLiveFailed=" << N(g_relLiveFailed)
       << CharStreamCountsString() << coop::WorldsyncCatchupCounts()   /* M7a: the character stream per type and road; the catch-up's state */
       << " relRecvLive=" << N(g_relRecvLive) << " relRecvSession=" << N(g_relRecvSession)   /* M5a: RELATION + RELSYNC by road, sent and received */
       << " relAnswer[session,slot]=" << N(g_relAnswerSession) << "," << N(g_relAnswerSlot)
       << " relPeer[ok,set,cleared,slotUnknown]=" << N(SessionPeerRelayOk() ? 1 : 0) << "," << N(g_relPeerOkSet) << "," << N(g_relPeerOkCleared) << "," << N(g_relPeerSlotUnknown) << " relProofWhileBlocked=" << N(g_relProofWhileBlocked)   /* M5a fold 1; M7a fold F1 */
       << " relAsk[sent,proofOnly,barred,barredIgnored]=" << N(g_relAskSent) << "," << N(g_relProofOnly) << "," << N(g_relBook.barred ? 1 : 0) << "," << N(g_relBarredIgnored)   /* M5a fold 2 */
       << " welcomeNotebookSwapped=" << N(g_welcomeNotebookSwapped)   /* join1: a loopback notebook address replaced by the dialled host */
       << " spawnAge[sent,unread,absent,clamped]=" << N(g_spawnAgeSent) << "," << N(g_spawnAgeUnread) << ","
       << N(g_spawnAgeAbsent) << "," << N(g_spawnAgeClamped)   /* P1: the SPAWN age - see g_spawnAgeSent */
       << " spawnUidOwnRefused=" << N(g_spawnUidOwnRefused)   /* M4 fold: SPAWNs naming a uid this game runs - must read 0 */
       << " gameRedial[series,attempts,relinked,dialFailed,abandoned]=" << N(g_glSeries) << "," << N(g_glAttemptsTotal) << ","
       << N(g_glRelinked) << "," << N(g_glDialFailed) << "," << N(g_glAbandoned)   /* link1: the joiner's in-world re-dial */
       << " say[sent,tooLong,passOffThread,passNoUid,passReplay,localPuppetDropped,applied,unknownUid,noDialogue,malformed,"
          "sendEventWouldBlock,dropClearedLine,replaySent,replayDropped,droppedBlocked]="
       << coop::SayCountsString()   /* P3: speech bubbles - see speech.cpp */
       << " " << coop::TalkCountsString();   /* P26 stage 0: conversation starts by target kind and event - speech.cpp */
    DebugLog(ss.str());
}

} // namespace net

// review-session S6 - ONE EVENT, PUBLISHED ON THE LINK-DOWN EDGE, NO TIMERS ANYWHERE.
//
// The defect it closes: when the PEER goes away (crash, quit, timeout) rather than us leaving,
// `SessionLeave` never runs. `g_owner` kept every claim the departed peer made, every puppet built
// from those SPAWNs stayed in the world for the rest of the game, and the host's next CONNECT hands
// the reconnecting client `nextPeerId_++` - so every surviving `g_owner[uid] == 1` entry failed
// `RemoteMayWrite(uid, 2)` and the new session could not even despawn what the old one left.
//
// Order is not arbitrary: the copies go first (so the platoons are empty when they are retired), the
// claims are dropped after the copies (the removal path reads nothing from `g_owner`, but a claim
// erased first would make the same uid unattributable if anything later did), and the announce marks
// go last so the re-announce cursor is reset after everything it could have named is gone.
//
// Defined in `coop`, not `coop::net`: it is an event the whole plugin observes, and the modules it
// calls into (spawn, worldsync, store) all live there. It reads `net`'s tables directly because the
// authority map is this file's (architecture commitment 1) and must not grow a second accessor.
/* M8 (T-197 piece 8; src/common/peergone.h) - PER-PLAYER LEAVE. Two roads remove a departed player's rows, and they agree:
   - PLAYER_GONE {slot} from the notebook (store.cpp queues it, scope SAVE, so a game with no world keeps it until one runs):
     OnPlayerGone removes every row this game did not author whose owner key is RelayPeerId(slot) - that player's, no one else's
     (owner decisions 53, 54) - skipped for the SESSION peer while that link is up (M8 review F1: the session road owns that player
     until the M11 flip).
   - the SESSION link's own peer-gone: PeerGoneCaptureSession runs AT THE DOWN EDGE, before PlayerFactionOnLinkDown forgets the link
     peer's slot, and records (uid -> key) every row keyed to that peer - its slot's key, or a raw transport id (only this link
     writes raw keys). The queued cleanup (OnPeerGone) or, if that action is discarded, PeerGoneSweepPending removes those rows
     only while each still has the key it had at the edge. Nothing read at drain time decides whose a row is.
   A removed row is erased and its mark dropped, so the road that runs second finds nothing of the same uid; a claim made after
   the edge (a SPAWN, a hand-over) drops the mark; PEER_SLOT does not re-key a marked row. MAIN THREAD. */
int PeerGoneCaptureSession()
{
    const int slot = LinkPeerSlot();
    if (slot >= 0) net::g_lastCapturedSessionSlot = slot;   /* M8 re-check 2a: names the session peer until the re-linked peer's PEER_SLOT */
    const int n = cooppg::PeerGoneSessionCapture(net::g_owner, net::g_localOwned, slot, &net::g_departedPending);
    net::g_peerGoneCaptured += (long long)n;
    DebugLog("[net] peer gone (M8): " + net::N((long long)n) + " rows of the session peer captured at the link's DOWN edge (slot "
             + net::N((long long)slot) + (slot < 0 ? std::string(" - never announced, so only the rows still keyed by the link") : std::string()) + "; "
             + net::N((long long)net::g_departedPending.size()) + " pending) - each removed once, by whichever road runs first");
    return slot;
}
namespace {
/* the rows the session road removes now: captured at a DOWN edge and still keyed as they were then. Every mark is spent. */
void PeerGoneTakePending(std::vector<unsigned int>* uids, long long* alreadyGone)
{
    *alreadyGone += cooppg::PeerGoneTake(net::g_owner, net::g_localOwned, &net::g_departedPending, uids);   /* M8 review F9: the rule is pure (peergone.h), swept by the offline suite */
}
}

void OnPeerGone(int goneSlot)
{
    ++net::g_peerGoneEvents;

    // WHOSE claims are the peer's (M8): the rows captured at the link's DOWN edge (PeerGoneCaptureSession) that still carry the key
    // they had then. Not "everything not mine" any more - that also took every relayed third game's rows - and not read at drain
    // time, when a NEW link's PEER_SLOT may already have arrived. Authorship stays the durable fact (F278/F279): a row this game
    // authored is never captured. A captured row PLAYER_GONE removed first is counted peerGoneM8 alreadyGone, not removed twice.
    std::vector<unsigned int> peerUids;
    long long alreadyGone = 0;
    PeerGoneTakePending(&peerUids, &alreadyGone);
    net::g_peerGoneAlreadyGone += alreadyGone;

    coop::TalkForgetPeer(coop::kTalkAllPeers);   /* P26s1 fold 1 L7: the link itself went down - every peer's. P26 stages 1-3: every open conversation ends on this side BEFORE the peer's copies (a mirrored NPC among them) are dropped */
    long long destroyed = 0, withdrawn = 0, absent = 0;
    // F495 (T183): a hand-off IS our unload of that sector, so at a drop we hold no copy of a handed-off squad - nothing to keep; the
    // keep-asleep branch that stood here left a kept character in no ownership table (review-p3w H2). Squads return via record/reconnect.
    for (size_t i = 0; i < peerUids.size(); ++i)
    {
        const int r = DropPeerOwnedCopy(peerUids[i]);
        if (r == 1) ++destroyed; else if (r == 0) ++withdrawn; else ++absent;
    }
    coop::NameForgetPeer(goneSlot);   /* names1: names held for copies the gone peer never had built here (every one while its slot is unknown) */
    coop::SlaveForgetPeer(goneSlot);  /* slave1: slave states held for copies the gone peer never had built here */
    coop::HireForgetPeer(goneSlot);   /* recruit1: requests in flight to the gone peer and promises to it are dropped */

    const int platoons = RetirePeerContextPlatoons(goneSlot);

    // The claims themselves. OURS are untouched: `g_localOwned` is the durable authorship record and
    // survives a link by design (F278) - it is `g_owner` that is session-scoped.
    for (size_t i = 0; i < peerUids.size(); ++i) { net::g_owner.erase(peerUids[i]); net::g_copyGen.erase(peerUids[i]); coop::net::RosterStreamForget(peerUids[i]); }   /* [a1b1-sp9] [F9] */
    ReplicateForgetUids(peerUids.empty() ? 0 : &peerUids[0], (int)peerUids.size());   /* M8 */
    net::g_peerGoneClaims += (long long)peerUids.size();

    const int marks = WorldsyncPeerGone(goneSlot);   /* M8: only that slot's loaded bit when it is known */
    net::ClearPeerViewDistance();   // F311: a fact about a peer, and this peer is gone
    /* M2: the peer's session-link loaded set (B9-b cleared it here) is gone with MSG_ZONES. The ladder's R2 reads
       the notebook's area map, which stops listing a departed game on the notebook's own clock. */
    StorePeerGone();
    /* DECISION 48 (P8a): the sticky declaration this site existed to clear is gone.  A game whose session has ended is
       unlinked, and the pre-link arm in towngen.cpp answers that the same way on every game. */

    DebugLog("[net] peer gone (session link, slot " + net::N((long long)goneSlot) + "; the rows captured at its DOWN edge, "
             + net::N(alreadyGone) + " of them already removed by PLAYER_GONE or re-claimed - M8): despawned " + net::N(destroyed) + " puppets, cleared "
             + net::N((long long)peerUids.size()) + " claims, retired " + net::N((long long)platoons)
             + " context platoons"
             + " (withdrawn " + net::N(withdrawn) + " under H030, " + net::N(absent)
             + " claims had no copy here, " + net::N((long long)marks)
             + " uids marked not-announced). The peer FACTION object stays - the world still holds"
             " it and its puppets are gone; ResetPlayerFactionState belongs to a world teardown, not"
             " to a link.");
}

/* M8: a discarded peer-gone action's rows - still the departed peer's, so removed now rather than left behind or re-keyed to the
   next link. Only the rows and their copies: the platoons, names, slaves and hires belong to the link the NEXT peer-gone ends. */
int PeerGoneSweepPending(const char* why)
{
    std::vector<unsigned int> uids;
    long long alreadyGone = 0;
    PeerGoneTakePending(&uids, &alreadyGone);
    net::g_peerGoneAlreadyGone += alreadyGone;
    if (uids.empty() && alreadyGone == 0) return 0;
    if (!uids.empty()) coop::TalkForgetPeer(coop::kTalkAllPeers);   /* M8 review F3 (P26 rule): the conversations of the link this sweep stands in for end BEFORE its copies go - OnPeerGone's order */
    long long destroyed = 0, withdrawn = 0, absent = 0;
    for (size_t i = 0; i < uids.size(); ++i)
    {
        const int r = DropPeerOwnedCopy(uids[i]);
        if (r == 1) ++destroyed; else if (r == 0) ++withdrawn; else ++absent;
    }
    for (size_t i = 0; i < uids.size(); ++i) { net::g_owner.erase(uids[i]); net::g_copyGen.erase(uids[i]); coop::net::RosterStreamForget(uids[i]); }   /* [a1b1-sp10] [F9] */
    ReplicateForgetUids(uids.empty() ? 0 : &uids[0], (int)uids.size());
    net::g_peerGoneClaims += (long long)uids.size();
    ++net::g_peerGoneDiscardSweeps;
    DebugLog("[net] peer gone (M8 sweep: " + std::string(why) + "): " + net::N((long long)uids.size()) + " captured rows removed (despawned "
             + net::N(destroyed) + ", withdrawn " + net::N(withdrawn) + ", no copy " + net::N(absent) + "), " + net::N(alreadyGone)
             + " already removed by PLAYER_GONE or re-claimed");
    return (int)uids.size();
}

/* A FINAL LEAVE'S NPCs ARE TAKEN OVER (peergone.h GoneTakeOverDecide). PLAYER_GONE is the world server's word after its
   hold, so a blip never reaches here. Each of the leaver's rows this game holds a copy of is looked at once at PLAYER_GONE and, while it
   is held or kept for the area's taker, once a second after (PlayerGoneTakeOverTick, after the drain) - kGoneTakeLooksMax looks per first
   receiver (one that does not take is passed over and the order asked again), and it goes as the leaver's other rows went only when
   no untried receiver is left. */
int HandoffGoneFirstReceiver(int goneSlot, unsigned int uid, const std::vector<int>& tried, int* firstOut);   /* handoff.cpp */
void HandoffTakeGone(unsigned int uid);                                         /* handoff.cpp */
int CopyIsPlayerCharacter(unsigned int uid);                                    /* spawn.cpp */
struct GoneWait { unsigned int uid; unsigned int slot; int looks; int looksHere; int first; int standIn; std::vector<int> tried; };   /* looks: all; looksHere: at the present first receiver `first` (or hold); standIn: the return check's leave, never a take here; tried: receivers passed over */
static std::vector<GoneWait> g_goneWait;
static bool GoneWaitHas(unsigned int uid) { for (size_t i = 0; i < g_goneWait.size(); ++i) if (g_goneWait[i].uid == uid) return true; return false; }
const size_t kGoneWaitMax = 4096;
static long long g_gtTaken = 0, g_gtWaited = 0, g_gtHeld = 0, g_gtSettled = 0, g_gtEnded = 0, g_gtPlayer = 0, g_gtNoCopy = 0, g_gtNone = 0, g_gtFull = 0, g_gtNext = 0, g_gtLogged = 0;
const long long kGtLogCap = 40;
static void GtLog(const std::string& s) { if (g_gtLogged >= kGtLogCap) return; ++g_gtLogged; DebugLog(s); }
static std::string GoneTakeOverToken()
{
    return net::N(g_gtTaken) + "," + net::N(g_gtWaited) + "," + net::N(g_gtHeld) + "," + net::N(g_gtSettled) + "," + net::N(g_gtEnded) + "," + net::N(g_gtPlayer)
         + "," + net::N(g_gtNoCopy) + "," + net::N(g_gtNone) + "," + net::N(g_gtFull) + "," + net::N((long long)g_goneWait.size()) + "," + net::N(g_gtNext);
}
/* one look at one of the leaver's rows (live reads: the copy, its faction, the area map); a take is made here. looks: the looks so far
   at firstBefore (a new first receiver starts at 0). standIn: the return check's leave - a take becomes a wait (passed over after
   kGoneTakeLooksMax looks) and the NPC is listed to the in-world games so the real taker's ownership reaches this game.
   *pcOut = the copy's kind (CopyIsPlayerCharacter), *kOut = the receiver order's answer, *firstOut = its first slot. */
static int GoneTakeOverLook(unsigned int uid, unsigned int slot, int looks, int firstBefore, int standIn, const std::vector<int>& tried, int* pcOut, int* kOut, int* firstOut)
{
    const int pc = CopyIsPlayerCharacter(uid);
    int first = -1;
    const int k = (pc == 0) ? HandoffGoneFirstReceiver((int)slot, uid, tried, &first) : 0;
    if (first >= 0 && first != firstBefore) looks = 0;
    const int me = StoreMySlot();
    const int d = cooppg::GoneTakeOverDecide(pc == 0 ? 0 : 1, pc == -2 ? 0 : 1, k, first, me, looks);
    *pcOut = pc; *kOut = k; *firstOut = first;
    if (d == cooppg::kGtTake && standIn != 0)
    {
        net::ReturnGoneListHeld(uid, slot);
        if (looks == 0)
            GtLog("[net] PLAYER_GONE slot " + net::N((long long)slot) + ": NPC uid=" + net::N(uid) + " NOT taken here although this game (slot " + net::N((long long)me)
                  + ") is first of its area's receiver order - this leave is the return check's: the world server's PLAYER_GONE went to the in-world games while this game"
                  " was away, so the copy waits for the taker's word (listed to the in-world games)");
        return looks < cooppg::kGoneTakeLooksMax ? cooppg::kGtWait : cooppg::kGtNext;
    }
    if (d == cooppg::kGtTake)
    {
        HandoffTakeGone(uid);
        ++g_gtTaken;
        GtLog("[net] PLAYER_GONE slot " + net::N((long long)slot) + ": NPC uid=" + net::N(uid) + " TAKEN OVER here - this game (slot " + net::N((long long)me)
              + ") is first of its area's receiver order (" + net::N((long long)k) + " game(s)) (goneTakeOver taken " + net::N(g_gtTaken) + "; the first 40 lines are logged)");
    }
    return d;
}
/* PLAYER_GONE: the leaver's rows this game takes, or keeps for the area's taker, leave `uids`; the rest go as before. The summary text. */
static std::string GoneTakeOverSplit(unsigned int slot, int standIn, std::vector<unsigned int>* uids)
{
    std::vector<unsigned int> drop;
    long long taken = 0, waiting = 0, held = 0, player = 0, noCopy = 0, none = 0;
    for (size_t i = 0; i < uids->size(); ++i)
    {
        const unsigned int uid = (*uids)[i];
        if (GoneWaitHas(uid)) continue;   /* already held or kept for its taker by an earlier word of this leave: not looked at again, not dropped */
        int pc = 0, k = 0, first = -1;
        const int d = GoneTakeOverLook(uid, slot, 0, -1, standIn, std::vector<int>(), &pc, &k, &first);
        if (d == cooppg::kGtTake) { ++taken; continue; }
        if ((d == cooppg::kGtWait || d == cooppg::kGtHold) && g_goneWait.size() < kGoneWaitMax)
        {
            if (d == cooppg::kGtWait) { ++waiting; ++g_gtWaited; } else { ++held; ++g_gtHeld; }
            GoneWait w; w.uid = uid; w.slot = slot; w.looks = 1; w.looksHere = 1; w.first = first; w.standIn = standIn; g_goneWait.push_back(w);
            continue;
        }
        if (d == cooppg::kGtWait || d == cooppg::kGtHold) ++g_gtFull;
        else if (pc == -2) { ++noCopy; ++g_gtNoCopy; }
        else if (pc != 0) { ++player; ++g_gtPlayer; }
        else { ++none; ++g_gtNone; }
        drop.push_back(uid);
    }
    uids->swap(drop);
    return " NPC take-over: " + net::N(taken) + " taken here, " + net::N(waiting) + " kept for the area's taker, " + net::N(held)
         + " held for a fresh area map; " + net::N(player) + " player characters, " + net::N(noCopy) + " rows with no copy here and " + net::N(none)
         + " NPCs no game has the area of go as before (goneTakeOver[taken,waited,held,settled,ended,player,noCopy,none,full,waitingNow,next]=" + GoneTakeOverToken() + ")";
}
void PlayerGoneTakeOverTick()
{
    if (g_goneWait.empty()) return;
    static DWORD s_last = 0;
    const DWORD now = ::GetTickCount();
    if (s_last != 0 && now - s_last < 1000) return;
    if (EngineWritesBlocked() || !GameplayRunning()) return;
    s_last = now;
    std::vector<GoneWait> next;
    std::vector<unsigned int> dropped;
    std::set<unsigned int> droppedSlots;
    for (size_t i = 0; i < g_goneWait.size(); ++i)
    {
        GoneWait w = g_goneWait[i];
        const unsigned int goneKey = cooplive::RelayPeerId(w.slot);
        if (cooppg::GoneTakeSettled(net::g_owner, net::g_localOwned, w.uid, goneKey))
        {
            ++g_gtSettled;
            std::map<unsigned int, unsigned int>::const_iterator o = net::g_owner.find(w.uid);
            const std::string how = net::IsUidMine(w.uid) ? std::string(" is this game's now")
                : (o != net::g_owner.end() ? " was taken over by slot " + net::N((long long)cooplive::RelayPeerSlot(o->second)) + " (its OWNER_MOVED re-keyed the copy here)"
                                           : std::string(" has no owner row any more"));
            GtLog("[net] PLAYER_GONE slot " + net::N((long long)w.slot) + ": NPC uid=" + net::N(w.uid) + how + " after " + net::N((long long)w.looks)
                  + " look(s) (goneTakeOver settled " + net::N(g_gtSettled) + ")");
            continue;
        }
        int pc = 0, k = 0, first = -1;
        const int d = GoneTakeOverLook(w.uid, w.slot, w.looksHere, w.first, w.standIn, w.tried, &pc, &k, &first);
        if (d == cooppg::kGtTake) continue;
        if (first >= 0 && first != w.first) { w.first = first; w.looksHere = 0; }   /* a new first receiver: its looks start here */
        if (d == cooppg::kGtWait || d == cooppg::kGtHold) { ++w.looks; ++w.looksHere; next.push_back(w); continue; }
        if (d == cooppg::kGtNext)   /* the first receiver did not take it in time: the order is asked again without it, so the next in line takes it */
        {
            cooppg::GoneTakeTriedAdd(&w.tried, first);
            ++g_gtNext; ++w.looks; w.looksHere = 0; next.push_back(w);
            GtLog("[net] PLAYER_GONE slot " + net::N((long long)w.slot) + ": NPC uid=" + net::N(w.uid) + " - slot " + net::N((long long)first) + ", first of its area's receiver order,"
                  " did not take it in " + net::N((long long)cooppg::kGoneTakeLooksMax) + " looks: passed over, the order is asked again without it (" + net::N((long long)w.tried.size())
                  + " passed over; goneTakeOver next " + net::N(g_gtNext) + ")");
            continue;
        }
        ++g_gtEnded;
        const int r = DropPeerOwnedCopy(w.uid);
        net::g_owner.erase(w.uid); net::g_copyGen.erase(w.uid); coop::net::RosterStreamForget(w.uid); net::g_departedPending.erase(w.uid);
        dropped.push_back(w.uid); droppedSlots.insert(w.slot);
        const std::string why = pc != 0 ? std::string("its copy is gone or unreadable")
            : (k < 0 ? std::string("no fresh area map") : (k == 0 ? (w.tried.empty() ? std::string("no game has its area loaded")
            : "no game of its area's receiver order took it - " + net::N((long long)w.tried.size()) + " passed over in turn") : std::string("an undecided answer")));
        GtLog("[net] PLAYER_GONE slot " + net::N((long long)w.slot) + ": NPC uid=" + net::N(w.uid) + " NOT taken over after " + net::N((long long)w.looks) + " look(s) ("
              + why + ") - it goes as before (" + (r == 1 ? std::string("despawned") : (r == 0 ? std::string("withdrawn") : std::string("no copy"))) + "; goneTakeOver ended "
              + net::N(g_gtEnded) + ")");
    }
    g_goneWait.swap(next);
    if (!dropped.empty()) ReplicateForgetUids(&dropped[0], (int)dropped.size());
    for (std::set<unsigned int>::const_iterator ds = droppedSlots.begin(); ds != droppedSlots.end(); ++ds)
    {
        const int platoons = RetirePeerContextPlatoons((int)*ds);   /* after its copies went: the platoons built here for that player's characters (one still holding a copy stays) */
        if (platoons > 0) GtLog("[net] PLAYER_GONE slot " + net::N((long long)*ds) + ": " + net::N((long long)platoons) + " context platoons retired after the end of the wait");
    }
}

/* M8: PLAYER_GONE {slot} - exactly that player's rows (its key, not authored here), their copies, its conversations, its held names and
   slave states, the hire requests to it and promises to it, the context platoons built for its characters, the squads handed to it and
   its loaded bit. Every other player's rows of each stay. NOT here: the announce marks - those belong to the session link (they
   re-announce this game's own uids to the peer on that link), whose own road runs them. */
void OnPlayerGone(unsigned int slot, const char* how, int standIn)
{
    std::vector<unsigned int> uids;
    cooppg::PeerGoneSelect(net::g_owner, net::g_localOwned, slot, &uids);
    coop::TalkForgetPeer(cooplive::RelayPeerId(slot));   /* that player's conversations end before its copies go (P26s1 L7) */
    const std::string takeOver = GoneTakeOverSplit(slot, standIn, &uids);   /* the NPCs this game takes, or keeps for the area's taker, leave the list */
    long long destroyed = 0, withdrawn = 0, absent = 0, cleared = 0;
    for (size_t i = 0; i < uids.size(); ++i)
    {
        const int r = DropPeerOwnedCopy(uids[i]);
        if (r == 1) ++destroyed; else if (r == 0) ++withdrawn; else ++absent;
    }
    coop::NameForgetPeer((int)slot);   /* names held for copies that player never had built here */
    coop::SlaveForgetPeer((int)slot);  /* slave states held for that player's copies */
    coop::HireForgetPeer((int)slot);   /* hire requests to that player cancelled, promises to it released, its undrained hire messages dropped */
    const int platoons = RetirePeerContextPlatoons((int)slot);   /* after its copies went: the platoons built here for its characters */
    for (size_t i = 0; i < uids.size(); ++i) { cleared += (long long)net::g_owner.erase(uids[i]); net::g_copyGen.erase(uids[i]); coop::net::RosterStreamForget(uids[i]); /* [a1b1-sp11] [F9] */ net::g_peerGoneAlreadyGone += (long long)net::g_departedPending.erase(uids[i]); }   /* M8 review F5: a captured row PLAYER_GONE removed first is counted peerGoneM8 alreadyGone HERE - its mark is spent, so the session road never sees it */
    ReplicateForgetUids(uids.empty() ? 0 : &uids[0], (int)uids.size());
    const int armed = WorldsyncPlayerGone((int)slot);
    ++net::g_pgApplied;
    net::g_pgUidsDropped += (long long)uids.size(); net::g_pgOwnersCleared += cleared; net::g_pgLoadedBitArmed += (long long)armed;
    net::g_pgDestroyed += destroyed; net::g_pgWithdrawn += withdrawn; net::g_pgAbsent += absent;
    DebugLog("[net] PLAYER_GONE slot " + net::N((long long)slot) + " (" + std::string(how) + "): " + net::N((long long)uids.size()) + " uids of that player dropped - despawned "
             + net::N(destroyed) + ", withdrawn " + net::N(withdrawn) + " under H030, " + net::N(absent) + " had no copy here; " + net::N(cleared)
             + " claims cleared; " + net::N((long long)platoons) + " context platoons retired; its loaded bit " + (armed != 0 ? std::string("cleared and armed to drop at its next absence") : std::string("- none (a slot past 31 has no bit)"))
             + ". Every other player's rows are untouched (M8; owner decisions 53, 54)." + takeOver);
}
void PlayerGoneApply(const std::vector<char>& payload, const char* how)
{
    unsigned int slot = 0;
    if (!cooppg::PlayerGoneDecode(payload.empty() ? 0 : &payload[0], payload.size(), &slot))
    {
        ++net::g_pgMalformed;
        DebugLog("[net] PLAYER_GONE malformed (" + net::N((long long)payload.size()) + " bytes, " + std::string(how) + ") - ignored (playerGone malformed " + net::N(net::g_pgMalformed) + ")");
        return;
    }
    const int mine = StoreMySlot();
    if (mine >= 0 && (unsigned int)mine == slot)
    {
        ++net::g_pgSelf;
        DebugLog("[net] PLAYER_GONE names THIS game's own slot " + net::N((long long)slot) + " (" + std::string(how) + ") - an earlier connection of ours the notebook closed; nothing removed (playerGone self " + net::N(net::g_pgSelf) + ")");
        return;
    }
    OnPlayerGone(slot, how, 0);
}
void PlayerGoneNoteRecv() { ++net::g_pgRecv; }
std::string PlayerGoneCountsString()
{
    return "playerGone[recv,uidsDropped,ownersCleared,loadedBitArmed,destroyed,withdrawn,absent,applied,self,malformed,skippedSessionPeer]=" + net::N(net::g_pgRecv) + "," + net::N(net::g_pgUidsDropped)
         + "," + net::N(net::g_pgOwnersCleared) + "," + net::N(net::g_pgLoadedBitArmed) + "," + net::N(net::g_pgDestroyed) + "," + net::N(net::g_pgWithdrawn)
         + "," + net::N(net::g_pgAbsent) + "," + net::N(net::g_pgApplied) + "," + net::N(net::g_pgSelf) + "," + net::N(net::g_pgMalformed) + "," + net::N(net::g_pgSkippedSessionPeer)
         + " goneTakeOver[taken,waited,held,settled,ended,player,noCopy,none,full,waitingNow,next]=" + GoneTakeOverToken()
         + " sessionEnd[kept,cleared]=" + net::N(net::g_sessEndKept) + "," + net::N(net::g_sessEndCleared);
}

} // namespace coop

