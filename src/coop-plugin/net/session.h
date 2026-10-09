// session.h - M0 session layer: handshake, authority map, pump, diagnostics.
#pragma once
#include "../../common/hirewire.h"   /* recruit1: coophire::HireMsg */
#include "../../common/talkwire.h"   /* P26 stages 1-3: cooptalk::TalkMsg */

namespace coopstate { struct PoseWire; }   /* POSE (read-poses): src/common/statewire.h */
namespace cooplimb { struct LimbsWire; }   /* LIMBS: src/common/limbwire.h */
namespace coopcrime { struct CrimeState; }   /* crime3: src/common/crimewire.h */
namespace coopbounty { struct BountyEntry; }   /* crime5: src/common/bountywire.h */
namespace cooprison { struct PrisonMsg; }   /* arrest2: src/common/prisonwire.h */
namespace cooptreat { struct TreatMsg; }    /* heal1: src/common/treatwire.h */
namespace coopbuild { struct BuildMsg; }    /* build1-b: src/common/buildwire.h */
namespace coopfarm { struct FarmMsg; }      /* par16: src/common/farmwire.h */
namespace cooplive { struct ExceptPlan; }    /* src/common/ownerroute.h */
namespace coopsquad { struct SquadLeadMsg; }   /* T-1 B1 restructure: src/common/squadlead.h */
namespace coopshot { struct ShotMsg; }   /* P104 fix: src/common/shotwire.h */

#include <string>
#include <vector>

#include "../identity.h"

namespace coop {

struct RecordCopy;
struct GarmentSet;
struct ItemMoveMsg;   /* E22a / decision 38: one item move on a character this game owns (items.h) */
struct ItemRequestMsg;   /* E22b / decision 38 part (2): a move on an inventory this game does NOT own (items.h) */
struct ItemConfirmMsg;   /* E22b: the owner's answer to one request (items.h) */

namespace net {

// Player-chosen backend (no default - user amendment 2026-08-05).
// backendName: "direct" (ENet) or "steam" (post-PoC).
bool SessionHost(const std::string& backendName, unsigned short port);
bool SessionJoin(const std::string& backendName, const std::string& address, unsigned short port);
void SessionLeave();
void SessionForgetRows(bool keepWorldRows, const char* why);   /* the owner rows a session's end takes (peergone.h SessionEndKeepsRow); false = every row */
/* mmo5 (e47-mmo-design.md 6): the HOST, on a deliberate leave or exit, tells the joiner before it closes the link
   (MSG_SESSION_CLOSING, reliable, flushed). A no-op on a joiner or with the link down. MAIN THREAD. */
void SessionSendClosing(const char* why);
/* mmo5: what the host-left decision reads - a joiner's session exists, the game link is up / dialling, link1's re-dials in the
   open series, and whether the host's SESSION_CLOSING arrived in this session. MAIN THREAD. */
void SessionHostLeftInputs(int* joiner, int* linkUp, int* dialing, unsigned int* redials, int* closingSeen);
bool SessionHostAccepted();   /* ui1 review LOW: the link is up AND the host's WELCOME arrived on this link */
unsigned int SessionHostSilenceMs();   /* ui1: ms since the host was last heard (ENet's own clock); 0 on the host or with no peer up */
std::string SessionClosingToken();   /* mmo5: "sent,seen,ignoredOnHost,dropped" for the report */
void SessionHostClosingNotActionable();   /* mmo5 fold: drop a SESSION_CLOSING that arrived where it cannot be acted on */

// Drain the transport and apply inbound messages. MAIN THREAD ONLY - called from the
// existing in-game pump alongside the command channel.
void SessionTick();

// Send a PING and log the RTT when the PONG returns (T017 acceptance).
bool SessionPing();

// One-line diagnostic dump (netstat command).
void SessionReport();

// True if this instance is hosting (used for uid allocation parity).
bool SessionIsHost();
bool SessionLinked();   // P1: the transport is up
bool SessionPeerRawId(unsigned int* out);   /* T-355 fold 2: the session link's current peer's transport id (raw, unfolded); false with no peer up */
unsigned int SessionPeerCount();   /* P7f (review-p6z H-1): the backend's own connected-peer count, for a caller that wants the transport's number rather than our cached LinkState */

/* ====== P7v (design-noworld-queue 5) - THE PUMP IS ORDINARY NOW, AND IT RUNS THE LINK EDGES ======
   SessionPumpWelcomeOnly is RETIRED and so is its 4096-entry drop-newest inbox. This is the SAME BODY
   SessionTick runs: Poll, run the link edges, dispatch the handshake/plugin-state table inline, and enqueue
   everything else into the ONE arrival queue in store.cpp. review-p7h H-2 (the loop refills the inbox
   SessionLeave has just cleared) disappears structurally - there is no inbox and no edge clears the queue.
   WHAT IT BUYS THAT THE OLD PUMP COULD NOT. On a client the HELLO had exactly one call site, SessionTick's
   LINK_UP edge. The pump polled - which is what flips the transport to LINK_UP in the first place - but never
   ran that edge, so a link that came up inside a load gate burned the whole 8 s waiting for a WELCOME nothing
   had asked for, and the log blamed the host (review-p7h H-4, review-p7q H-3(b)). The edge is now read from
   the transport's monotonic ConnectGen(), so a DISCONNECT and a CONNECT drained in one Poll are both seen.
   Returns true if a MSG_WELCOME was dispatched on this call. MAIN THREAD ONLY. */
bool SessionPumpDeferring();
/* THE SESSION LINK'S GENERATION, ANY THREAD. Bumped on the session link's UP and DOWN edges; every queued
   session entry carries the value it was stamped with, and the drain discards an entry whose stamp no longer
   matches. This is what gives a session RECORD its own edge - the one message with no second delivery
   (review-p7p H-3) - without letting the OTHER link's bounce touch it (review-p7p H-2). */
long SessionLinkGen();
long SessionClosingEpisode();   /* M11a S3 review fold (F2): moves on a session link UP and on a leave (the relayed close latch's episode) */
/* M5b fold 1 (review 2026-09-30 item 1), MAIN THREAD: the generation the session link will have once the CURRENT Poll's edges
   have run (SessionLinkGen() when none is pending). A stamp taken by a handler running inline in SessionTick's receive loop -
   the HELLO / WELCOME ready stamps - must use this: the edges run below that loop (coopgl::LinkGenAfterBatch). */
long SessionLinkGenAfterBatch();
/* THE DRAIN'S REPLAY OF ONE QUEUED SESSION MESSAGE. Runs exactly the handler SessionTick used to run, with
   the sender's peer id restored, so the live path and the deferred path cannot answer the same message
   differently (lesson 11). MAIN THREAD, called only from coop::InQueueDrain. */
void SessionDispatchQueued(int type, unsigned int peer, const std::vector<char>& payload);
/* THE DRAIN'S PERFORMANCE OF ONE QUEUED ACTION - coop::kActPeerGone, kActSessionLeave or kActResendHello.
   These are engine-touching or link-touching work that used to run straight out of a message handler inside
   the engine's own load call (review-p7h H-1: OnWelcome's protocol refusal walked and destroyed engine
   objects inside resetGame). They are queued like records now and performed here, by construction outside
   every gate, because a gate holds the load depth for its whole body. MAIN THREAD. */
void SessionPerformAction(int act, const std::vector<char>& payload);   /* M8: kActPeerGone's payload is the departed link peer's slot (i32, -1 unknown), captured at the DOWN edge */
/* P7w, folding F599. THE DRAIN DISCARDED A QUEUED ACTION - it belonged to a link generation that has since
   moved. Called INSTEAD of SessionPerformAction, never as well as it. It exists because kActSessionLeave
   latches g_sessionLeavePending, which is cleared only inside SessionLeave and which breaks the poll loop
   while it is set: discarding that action without clearing the latch would silence the session for the life
   of the process. Safe for every kind; the other two carry no latch. */
void SessionActionDiscarded(int act);
/* "viewDistanceUnknown,resentWithViewDistance,welcomeProtocolRefused,helloProtocolRefused" as one comma
   string, for the [STORE] REPORT line. A HELLO sent while the predicate was blocked carries view distance 0
   ("I cannot tell you", which the wire format already allows) and queues its own resend; the two refusal
   counts are the protocol mismatches whose teardown is now deferred rather than run inside a load. */
std::string SessionRefusalCounts();

// M1: replicate a template spawn to the peer. No-op (returns false) if no live link.
// F115: the spawn MODIFIERS travel with the spawn. They used to live in per-instance
// globals that only one side ever set, so the two "replicated" copies were different
// characters. Faction travels by NAME (both installs load identical static data).
// P1 (read-parity3 GAP 1): `character` is the owner's own character; its age (CharacterAnimal +0x700)
// is read at the moment of sending and travels as a trailing f32 (0.0 for a human / unreadable / 0).
bool SendSpawn(unsigned int uid, const std::string& templateName, float x, float y, float z,
               const std::string& factionName, bool keepContainer, const void* character);

// M2a: stream a position. UNRELIABLE, latest-wins - a stale position is worthless, and
// every packet carries the full value so a drop self-heals on the next send.
//
// F350 - the last two floats are the authority's VELOCITY in world units per second, measured
// from two successive samples of the same position stream. They were declared as `dirX/dirZ`
// ("last streamed facing, unused until M2b") and **every caller sent 0.0f**, so the peer had never
// been told whether the authority was moving at all - which is the whole of P-20's cause. A
// velocity puts heading, speed and stopped-ness in the two floats that were already on the wire,
// so the payload is unchanged at 28 bytes.
// stampMs: the moment the sample was taken, on this game's clock (milliseconds; the copy replays its owner's route by it).
bool SendMove(unsigned int uid, float x, float y, float z, float velX, float velZ, float desiredSpeed, float faceX, float faceZ,
              unsigned int stampMs);
// MSG_MOVESTOP: this game's character `uid` (any it streams) stopped at (x, y, z) at stampMs (the clock SendMove stamps with).
bool SendMoveStop(unsigned int uid, float x, float y, float z, unsigned int stampMs);
bool SendIntent(unsigned int uid, int taskType, unsigned int subjectUid, float x, float y, float z, int priority);   // H019
// P25 fold 2: MSG_INSIDE - the owner's word on which building its own character is in (key: nul-terminated, "" = none).
bool SendInside(unsigned int uid, int inside, const char* key);
// M-D - handoff wire records (POD; sizeof must match on both instances - same binary).
struct XferMember { unsigned int uid; float x, y, z, fx, fz; int intentType; unsigned int intentSubject; float ix, iy, iz; unsigned int pad; };
bool SendUnload(unsigned int uid, int ownerStillRuns);   /* ownerStillRuns 1 = the announce pass (the sender still runs the person; the receiver keeps its squad index), 0 = the sender runs it nowhere (put away, reloaded, retired) */
bool SendXfer(unsigned int leader, unsigned int reason, const XferMember* members, int count, int targetSlot);   /* protocol 84: reason = coopsquad::kXferReason*. M7a A1 build 1 [a1b1-sh0]: + each member's gen; by WorldFirstRoute to targetSlot (-1: not sent) */
bool SendXferAck(unsigned int leader, const unsigned int* takenUids, int count, unsigned int toPeer);   // protocol 27 (F447): the ACK names the uids taken. M7a A1 build 1 [a1b1-sh1]: back to the XFER's sender by slot (fold 1 [a1b1f1-sh0] [F1]: on the session link when the XFER came by it)
/* M7a A1 build 2 [a1b2-sh0] (design 1.3, 2.5), MAIN THREAD: RELEASE (68) to one slot (WorldFirstRoute; false = no road - the release's
   tick tries again); RELEASE_ACK (69) back to the sender (the XFER_ACK's road); IsRecordedOwner: peer is uid's recorded owner (by
   player); ForgetCopyRecord: a copy's owner record and gen go (an offer this game dropped - its copy was removed). */
bool SendRelease(int slot, const std::vector<char>& b);
bool SendReleaseAck(unsigned int toPeer, const std::vector<char>& b);
bool IsRecordedOwner(unsigned int uid, unsigned int peer);
void ForgetCopyRecord(unsigned int uid);
bool SendSquadLead(const coopsquad::SquadLeadMsg& m);   /* protocol 85: MSG_SQUAD_LEAD. False when it did not go - no road (the world link not ready and the session link down) or the send refused; due again at the next pass (not encodable: dropped, reads as sent; fold 1 [a1b1f1-sh1] R8) */
/* protocol 89 (T-1 B3 restructure): MSG_KEEPER retired - the squad's money rides SendSquadLead */
void ReleaseLocalOwner(unsigned int uid, unsigned int toPeer, unsigned int gen = 0);   // M-D: we stop being the authority for uid. M7a A1 [a1b1-sh2]: gen = the new owner's (0 = this game's + 1)
void TakeLocalOwner(unsigned int uid, unsigned int gen = 0);                           // M-D: we become the authority for uid. M7a A1: gen = the one this game holds now (0 = above every record here)
/* M7a A1 build 1 [a1b1-sh3], MAIN THREAD: the generation tables (0 = no row); GenForTake = max(the giver's gen, this game's copy record) + 1;
   PeerSlotOfKey: the slot of a sender / owner key (-1 none); OwnerSlotOf: the slot of uid's recorded owner
   (build 2 [a1b2-sh1]: XferPeerSlot retired - the forced XFER goes to the receiver-ring pick, handoff.cpp ReceiversFor). */
unsigned int MineGenOf(unsigned int uid);
unsigned int CopyGenOf(unsigned int uid);
unsigned int GenForTake(unsigned int uid, unsigned int giverGen);
int PeerSlotOfKey(unsigned int key);
int OwnerSlotOf(unsigned int uid);
void SessionCatchupApplyTick();   /* M7a2 fold 1 [m7a2f-ap3]: MAIN THREAD, CommandChannelTick right after coop::InQueueDrain() - the catch-up's engine writes */
bool SendContext(unsigned int uid, const std::string& squadSid, int squadType, const std::string& townSid,
                 const unsigned char* handRaw32, const unsigned char* charRaw32, float bx, float by, float bz, int memberType,
                 const std::string& platoonId);   // M-B / P064 / H027; P1: + the sender's platoon id (the squad's world id)
bool SendRelSync();   // ask the peer to re-send its owned standings (after my world was rebuilt)
bool SessionPeerRelayOk();   // M5a fold 1, MAIN THREAD: the session peer is KNOWN reachable through the notebook (a relayed standing stamped with its slot arrived on this session link and this welcomed notebook link)
void SetRecordTownLookup(std::string (*fn)(const std::string&));   /* decision 34: the store supplies each note's home town to the encoder */
void SetRecordHomeLookup(std::string (*fn)(const std::string&));   /* the store supplies each group's home building key to the encoder (after the flags byte) */
/* M2 (decisions 32/44/54): SendRecord, SendRecordGone, SendWorldListed, SendDeletedBits, SendZones and SendSectorMap are
   DELETED - the notebook carries all six, and the session link drops any that still arrive (nbOnlyDropped). */
bool SendRelation(const std::string& ownerSid, const std::string& otherSid, float relation, float trust, float trustNeg, unsigned int flags, unsigned int reason);   // P3 piece 2
void EncodeRecordPayload(std::vector<char>* b, const std::string& worldId, const std::string& squadSid, const std::string& factionName, float x, float y, float z,
                         long long writtenAt, int owner, const std::vector<char>& bytes, unsigned flags = 0);   /* the store link's RECORD payload - since M2 the only link that carries one; flags: factionkey.h kRecFacCode */
bool DecodeRecordPayload(const std::vector<char>& p, std::string* worldId, std::string* squadSid, std::string* factionName, float* x, float* y, float* z,
                         long long* writtenAt, int* owner, std::vector<char>* bytes, std::string* town,
                         unsigned long long* seq, unsigned* flags = 0, std::string* home = 0);   /* B12 / store protocol 41: the NOTEBOOK'S sequence number for the record, or 0; `seq` may be null; flags: the byte after it (factionkey.h), 0 when absent; home: the group's home building key after the flags byte, "" when absent */

// M2b: replicate a task order. RELIABLE - unlike a position, a dropped order never
// self-heals; the puppet would simply never do the thing.
bool SendTaskMsg(unsigned int uid, int taskType, float x, float y, float z);
/* E45 / decision 40 (P8e), P8n - ONE DOOR'S STATE.  `state` is a TERMINAL state (0 CLOSED, 1 OPEN)
   and nothing else may be passed; `gen` is this game's monotonic publish counter, which is what lets
   the receiver drop an out-of-order copy.  `origin` is coopdoor::kDoorOriginHolder (0) when the
   sender holds the door's patch of map and kDoorOriginActor (1) when it does not and is telling the
   holder what its own world just did - the last-actor rule.  It rides as an OPTIONAL TRAILING BYTE,
   so a message without it reads as a holder publish and the protocol version does not move.
   M7b slice 4 (C4; protocol 121): the character stream's road - on the notebook the holder's answer goes AREA (the sector of `pos`,
   the door's x/y/z; 0 = not read: WORLD, counted sideNoArea) and an actor report by SLOT to that sector's holder (AREA while none
   is known). False = no road, or not sent. MAIN THREAD. SideRoadOpen: that road is open (the notebook or the session link). */
bool SendDoorState(const std::string& key, int state, int locked, unsigned int gen, int origin, const float* pos);
bool SideRoadOpen();
// P3 (read-parity3 GAP 3): one speech line said by a character this game authors. RELIABLE; false when the
// character stream's road is closed (neither the notebook nor the session link), no road took it, or the text is longer than
// 512 bytes. MAIN THREAD (the Dialogue::say detour).
bool SendSay(unsigned int uid, const char* text, size_t len);
// S1 (read-stats): the 44 stat values of a character this game drives (src/common/statswire.h). RELIABLE;
// false when the link is down. MAIN THREAD (StatsTick).
bool SendStats(unsigned int uid, const unsigned int* raw44);
// crime3 (T275 / F894): the current crime of a character this game drives (src/common/crimewire.h). RELIABLE; false when
// the link is down or the sid is too long. MAIN THREAD (CrimeTick).
bool SendCrime(unsigned int uid, const coopcrime::CrimeState& s);
// M7b slice 4 fold 1 (F5): the crime state that follows a SPAWN of `uid` on the very road (and route) that SPAWN just took; false
// when the last character-stream send was not that SPAWN or the send failed. MAIN THREAD (crime.cpp CrimeSendWithSpawn).
bool SendCrimeAfterSpawn(unsigned int uid, const coopcrime::CrimeState& s);
// crime5: a character's bounty list (kind 0, owner) or a copy's additions (kind 1) - src/common/bountywire.h. RELIABLE; false
// when the link is down or the list does not encode. MAIN THREAD (CrimeTick).
bool SendBounty(unsigned int uid, unsigned char kind, const std::vector<coopbounty::BountyEntry>& list,
                bool* mayDrop = 0);   // M7b slice 4 fold 2 (D4): on false, *mayDrop = it may be dropped (ownerroute.h BountyUnsentDrops)
// arrest1 (docs/design-arrest.md 3): this game's body `body` is no longer carried here by the other game's carrier
// `carrier` (or was refused as awake). RELIABLE; false when the link is down. MAIN THREAD (CarryApplyTick).
bool SendCarryBreak(unsigned int body, unsigned int carrier);
// arrest2 (docs/design-arrest.md 3): this game's guard caged its copy of `m.uid` (src/common/prisonwire.h). RELIABLE; false
// when the link is down (a message that does not encode is dropped and reads as sent, as SendBounty). MAIN THREAD (PrisonTick).
// M7b slice 1: kinds IN, RELEASE, DEATH and BED IN go to the owner of `m.uid`; kinds REFUSED and BED REFUSED go back to
// `askerPeer`, the player whose request asked (the handler's sender key; 0 = the session peer).
bool SendPrison(const cooprison::PrisonMsg& m, unsigned int askerPeer = 0);
// heal1: a medic here treated our copy of `m.uid` - its owner raises its own character's treatment to these values. False
// when the link is down (a message that does not encode is dropped and reads as sent). MAIN THREAD (TreatTick).
bool SendName(unsigned int uid, const std::string& name, bool afterSpawn = false);   // names1: MSG_NAME - the owner's character name (<= 64 bytes, cut by the caller)
bool SendTalk(const cooptalk::TalkMsg& m);   // P26 stages 1-3: MSG_TALK PROMPT/ANSWER/END, by SLOT to the other side's owner. False when the character stream's road is closed, that owner is unknown or not in the world, or it does not encode. MAIN THREAD
const unsigned int kHireNoAsker = 0xFFFFFFFFu;   // SendHire's toPeer when there is none: a REQ goes to the person's owner, a DONE to every game
bool SendHire(const coophire::HireMsg& m, unsigned int toPeer);   // recruit1: MSG_HIRE REQ/OK/NO/DONE; OK / NO / DONE to toPeer (the asker's key). False when no road or no player to address (not encodable: dropped, reads as sent)
bool SendCapture(const char* p, size_t n, unsigned int victimUid);       // P11: MSG_CAPTURE - an encoded capturewire.h request (items.cpp); M7b: to the victim's owner
bool SendCaptureDone(const char* p, size_t n, unsigned int askerPeer);   // P11: MSG_CAPTURE_DONE - an encoded capturewire.h answer (items.cpp); M7b: back to the asker
bool SendCapturePlaced(const char* p, size_t n, unsigned int victimUid);   // P11 f3: MSG_CAPTURE_PLACED - which taken rows landed (items.cpp); M7b: to the victim's owner
bool SendShot(const coopshot::ShotMsg& m);   // P104 fix: MSG_SHOT - our bolt struck the other game's character's copy. False when the link is down or it does not encode. MAIN THREAD (combat.cpp)
/* T-327: MSG_EFFECT. The request goes to the TARGET's recorded owner (LIVE route SLOT when the notebook road is up, else the
   session link - cooffect::EffectRequestRoad); the answer goes back on the road its request came by. Both return the road
   used (cooffect::kRoadSession 1 / kRoadLive 2) or 0 = not sent (no owner record, no road, or the send failed). MAIN THREAD. */
int SendEffectRequest(unsigned int targetUid, const std::vector<char>& b);
int SendEffectAnswer(unsigned int toKey, bool viaRelay, const std::vector<char>& b);
/* T-354: MSG_NOT_SHOWN to the owner of a SPAWN this game's table refused, on the road the SPAWN came by. 0 = no road. */
int SendNotShownToOwner(unsigned int uid, unsigned int ownerKey);
/* A LOST COPY (src/common/lostcopy.h; net/session.cpp beside OnResend). MAIN THREAD, all of them.
   LostCopyNote: this engine put away its copy of another game's character (spawn.cpp NotifyDespawn; the owner's last streamed spot
   when known) - booked; the copy existed, so the uid is no longer one this game's table refused. LostCopyMove: a MOVE came for a uid
   this game holds no copy of (replicate.cpp ApplyRemoteMove) - booked unless this game's table refused its SPAWN. A booked uid's owner
   is asked to send it again while its spot is loaded here. LostCopyRefusedHere: OnSpawn's verdict on a SPAWN - no row was made for
   the uid (refused: never booked) or one was. LostCopyArrived: after a SPAWN, a booked uid whose copy is here is BACK (its row
   HERE, so no ask follows in this visit). LostCopyAsked: a RESEND ask for the uid awaits its SPAWN or its answer (the creation
   core's stale-row rule). LostCopyForget / LostCopyForgetAll: the owner's UNLOAD / DESPAWN (`why` is written on the uid's
   "forgotten" line, first 20 such lines); a world teardown or the session's owner records cleared. */
void LostCopyNote(unsigned int uid, bool hasPos, float x, float y, float z);
void LostCopyMove(unsigned int uid, float x, float y, float z);
void LostCopyRefusedHere(unsigned int uid, bool refused);
void LostCopyArrived(unsigned int uid);
bool LostCopyAsked(unsigned int uid);
void LostCopyForget(unsigned int uid, const char* why);
void LostCopyForgetAll();
/* A MOVE arrived for `uid` (replicate.cpp ApplyRemoteMove, before its puppet is looked up): a lost row whose copy is back counts it
   (lostcopy::LostHereMoveSeen). */
void LostCopyMoveSeen(unsigned int uid);
/* The road to one asking game for a character's state (a RESEND ask, or one a hand-over held back): its slot while the character
   stream rides the world road, -1 (the session link) while it rides that and the asker is the session peer, -2 for no road to that
   game alone. MAIN THREAD. */
int CharStreamSlotFor(int askerSlot);
bool SendSlave(unsigned int uid, int state, unsigned int ownerUid, bool afterSpawn = false);   // slave1: MSG_SLAVE - the owner's SlaveStateEnum (0..3) for one of its characters
bool SendTreat(const cooptreat::TreatMsg& m);
// M7b slice 4 fold 1 (F6) + fold 2 (D1): a BUILD message to one player (HELP_WORK to the piece's owner, HAND_ACK to the placer).
// slot: a slot, -2 = the session peer before its slot was known (cooplive::kAddrOwnerSessionPeer), -1 = not known. By SLOT while
// the notebook is up and the slot is known; else the RAW SESSION PEER while the session link is up (the send before fold 1); else
// not sent (ownerroute.h BuildAddrTarget). False with no road or that game not in the world. Not encodable: dropped, reads as sent.
// BuildSlotTarget: whom it would go to now (a slot, -2 the session peer, -1 nobody - the caller holds it). PlayerAddrOfKey: a
// sender's key as such a slot (-2 = the session peer, its slot not yet known; never -1). MAIN THREAD.
bool SendBuildToSlot(const coopbuild::BuildMsg& m, int slot);
int BuildSlotTarget(int slot);
int PlayerAddrOfKey(unsigned int key);
// build1-b: a piece this game's own player faction placed (MSG_BUILD PLACE). False when the link is down (a message that
// does not encode is dropped and reads as sent; build.cpp checks BuildEncodable first). MAIN THREAD (BdOnNewPiece).
bool SendBuild(const coopbuild::BuildMsg& m);
// par16: a farm's growth state (writer -> other, kind 6) or a held game's harvest (kind 7), on MSG_BUILD. False when the link is down.
bool SendFarm(const coopfarm::FarmMsg& m);

// P-15 / F220 - tell the peer this character ENTERED or LEFT combat, and against whom.
//
// Sends the CAUSE, not the appearance: T066 proved the peer's combat controller is already
// being ticked and is simply never told to fight, so its own engine produces the stance,
// animations and portrait state once it is in combat mode against a target it can resolve.
// RELIABLE - entering combat is an edge, and a dropped edge leaves one screen fighting and
// the other standing still.
bool SendCombatMode(unsigned int uid, bool on, unsigned int targetUid);

// F348 - the authority's character has begun a blow against `targetUid`. Sent on the transition
// INTO the engine's attack state, so it reports a swing that actually started rather than one the
// AI merely intended. RELIABLE: there is no later message that repairs a lost swing.
bool SendSwing(unsigned int uid, unsigned int targetUid);

// M3: replicate a resolved hit. RELIABLE - a dropped hit is a permanent health divergence.
// F117: the ATTACKER travels with the hit. The engine's hit function dereferences the
// attacker unconditionally, so a null one kills the receiving instance (T033).
// F119: the resulting per-limb health rides in the SAME message as the hit that caused it,
// so the visible event and its authoritative outcome cannot arrive out of order.
// PARITY P-2: `attackerId` is the engine's own cross-process name for an attacker that has
// no uid (an ambient world NPC). It is what lets the peer find the SAME character in its own
// world and play the hit for real, instead of applying damage with nothing visibly causing
// it. All-zero when the authority could not name the attacker; see identity.h.
// K2: `knock` = this hit knocked the victim down on the owner's game (one trailing byte; absent = 0).
bool SendHit(unsigned int victimUid, unsigned int attackerUid, int cutDirection,
             const char* damages24, int comboId,
             const float* parts, int partCount, float blood,
             const ObjId& attackerId, unsigned char knock);

// M4 (F126): periodic full state for an owned character. UNRELIABLE and periodic, like
// M2a's position stream - a dropped snapshot is superseded by the next one, so reliability
// buys nothing. This is what makes divergence self-healing: anything that drifts for ANY
// reason is corrected within a second, without having to know why it drifted.
// F147: the LATCH BLOCK travels with the state. `unconcious` is what the peer's own collapse
// decision reads - T047 caught it holding 0 while the authority held 1, and undoing our written
// knockout within 9-125 ms, eight times in 38 seconds.
bool SendState(unsigned int uid, const float* parts, int partCount,
               float blood, int prone, int dead,
               unsigned int latchBits, float nextKnockoutAt, float koTimer,
               unsigned int carryingUid,    // K1 (read-carry): the body this carrier carries, 0 = nothing
               const float* rest,           // R3 (read-ragdoll, 48): where its ragdoll settled, 0 = none
               const coopstate::PoseWire* pose,   // POSE (read-poses, 50): the owner's in-place pose, 0 = none
               const float* hunger = 0,    // par5 (parity P5) session 79: the owner's {hunger, fed}, 0 = not carried
               const cooplimb::LimbsWire* limbs = 0);   // LIMBS (133): the owner's four limbs, 0 = not carried

// H010a (F156): replicate the authority's appearance ROLL. RELIABLE - a dropped roll leaves
// the peer showing a different person permanently, and unlike a position it never self-heals.
//
// Sent SEPARATELY from the SPAWN message and later than it, which is not an optimisation but
// a requirement found by measurement: at the moment the spawn path returns, the appearance
// has not been applied yet and reads as defaults on both sides (F157). Sending it inline
// would have replicated "no appearance" and looked like a pass.
//
// H010b (F160): what travels is the appearance RECORD - the typed name->value maps the
// engine derives the character FROM - not the derived fields themselves. Writing the derived
// fields was measured failing: updateAppearance() re-derives them from this record, so the
// only write that survives is a write to the record.
bool SendAppearance(unsigned int uid, const RecordCopy& rec);

// The authority's WORN ITEMS (F173: item-set parity was 1/4, concentrated in the legs slot).
// RELIABLE - like the appearance record, a dropped one leaves the peer permanently dressed as
// someone else and never self-heals.
//
// Sent SEPARATELY from and AFTER the appearance record, and that ordering is load-bearing:
// applying the appearance rebuilds attachments, so clothing written first would be undone.
// ENet's reliable channel preserves order, and the apply side re-checks anyway.
bool SendClothing(unsigned int uid, const GarmentSet& set);

// E22a / decision 38 (approved): ONE ITEM MOVE on a character this game owns. Sent from ItemsTick on the
// MAIN THREAD, after the ownership test - never from the detour that saw the move. RELIABLE and ordered:
// a remove that overtook its add would leave the ghost holding an item the owner no longer has.
bool SendItemMove(const ItemMoveMsg& m);
// The same ITEM_MOVE to ONE game, the player askerKey names (the ground catch-up's ADD / GONE answer to an arrival's listing).
bool SendItemMoveTo(const ItemMoveMsg& m, unsigned int askerKey);
/* The same ITEM_MOVE to every game but the one `exceptKey` names (cooplive::ItemExceptRoad); *whyOut: its kExceptWhy*; *planOut
   (may be 0): the road it took, for a later message that must follow it on the same road (SendItemMoveOnPlan). */
bool SendItemMoveExcept(const ItemMoveMsg& m, unsigned int exceptKey, int* whyOut, cooplive::ExceptPlan* planOut);
bool SendItemMoveOnPlan(const ItemMoveMsg& m, const cooplive::ExceptPlan& plan, int* whyOut);
// par1: MSG_PARITY_REQ / MSG_PARITY_BOX, already encoded (src/common/paritywire.h). False when the item road is down.
// M7b slice 2: the ask to the holder of sector (sx, sy); the answer back to the asker's key; a push to every game covering (sx, sy).
bool SendParityReq(const std::vector<char>& bytes, int sx, int sy);
bool SendTownPrices(const std::vector<char>& bytes);   /* T-619: one town's local trade multipliers, the price source -> its session peer (session link, RELIABLE); false = not sent */
int  SendTownPricesLive(const std::vector<char>& bytes);   /* the same through the world server to every other admitted game but the session peer; 0 = not sent, 1 = WORLD, 2 = WORLD_EXCEPT the session peer */
bool SendParityBox(const std::vector<char>& bytes, unsigned int askerKey);
bool SendParityPush(const std::vector<char>& bytes, int sx, int sy);

// E22b / decision 38 part (2): ASK the owner to make a move we are not allowed to make ourselves, and the
// owner's answer. Both RELIABLE: a lost request is a click that never happens and a lost confirmation strands
// the requester's pending entry until it times out, and nothing re-sends either.
bool SendItemRequest(const ItemRequestMsg& r);
bool SendItemConfirm(const ItemConfirmMsg& c);
// E22b-2 (P6b). The taker's "it landed" / "it did not", and the giver's "undo your half".
bool SendItemPlaced(unsigned int id, int ok);
bool SendItemRevoke(unsigned int id);
bool SendShopTail(unsigned int id, unsigned int epoch, int kind);   // T-164 B4-4 fold 2: MSG_ITEM_REVOKE + 'SHK1' - kind 0 shop revoke, 1 shop ack
// M7b slice 2 (T-197; protocol 115): the request id remembers the game it was asked of (PLACED / REVOKE / the shop tail go back
// to it). MAIN THREAD. ItemAnswerFromAsked: is fromPeer that game (true when no ask is on record). ItemAskedSlot: its slot
// (LinkPeerSlot when none is on record - the legacy reading). PlayerSlotOfKey: a sender key's slot (the session peer's raw key:
// LinkPeerSlot).
bool ItemAnswerFromAsked(unsigned int id, unsigned int fromPeer);
int  ItemAskedSlot(unsigned int id);
int  PlayerSlotOfKey(unsigned int key);
// M7b slice 4 fold 1 (F2): a sender id as a PLAYER key now (cooplive::PlayerKeyOf with this link's peer slot). ANY THREAD.
unsigned int PlayerKeyNow(unsigned int sender);

// Is the transport actually connected? P033 needs this BEFORE it mints a uid: adopting a world
// character while the link is down would allocate a uid and register the character locally while
// its SPAWN message goes nowhere, and nothing in this project re-sends a spawn. The send functions
// already refuse when the link is down, but by then the uid is spent.
// F322 - tell the peer that our engine has DESTROYED this character, so it can remove its copy.
// Called from the `GameWorld::destroy` detour, which runs on the main thread (F321). RELIABLE: a
// lost despawn is permanent, because nothing else would ever correct it.
bool SendDespawn(unsigned int uid);
/* M7a (T-197 piece 7a): while slot >= 0 the character stream (SPAWN ... UNLOAD) goes LIVE route SLOT to that game only - the
   worldsync catch-up's answer to one ask; -1 = back to AREA / WORLD. Never by the session link. MAIN THREAD. */
void SessionNoteRelayedStandingDropped(unsigned int peer, bool isAsk, const char* p, size_t n);   // M7a fold F1, MAIN THREAD: a relayed RELATION / RELSYNC dropped with no running world still proves its sender reaches this game (relPeer book)
void SessionCatchupAskedOpen(unsigned int askNo, unsigned int owners);   // M7a2 item 7 [m7a2-sh1], MAIN THREAD: book this game's ask - it ends at its owners' ENDs or the timeout
/* M7a A1 build 1 [a1b1-sh4]: SessionCatchupSweepOpen retired with the reconnect sweep (the ROSTER replaces it) */
void CharStreamToSlot(int slot);

bool LinkIsUp();

// F302 - the CONNECTED PEER's render distance, sent in HELLO. Returns 0.0 when we have not been
// told (no session yet, or the peer could not read its own setting) - **0 means unknown and must
// never be substituted with a default**, because the whole point of this value is that the machine
// asking cannot derive it locally.
//
// Only the client sends HELLO, so on the HOST this is the CLIENT's setting. That is the one the
// adoption radius needs: the host is the authority and simulates its whole world regardless, so
// the only question a radius answers is what the client needs sent to it (user, 2026-08-07).
float PeerViewDistance();
unsigned int SessionProtocolVersion();   /* M11a S1: kProtocolVersion - the game-to-game protocol the STORE_HELLO's tail carries (the world server checks it) */
std::string SessionPeerName();   /* mp4: HOST - the joined player's name from its HELLO (DisplayNameOk passed), "" = none or not yet. MAIN THREAD */

// F311 - drop it, because a stale value is indistinguishable from a fresh one at every readout.
// Called on SessionLeave and when a session starts; also tells worldsync to forget any radius
// derived from it.
void ClearPeerViewDistance();

// Do we own this uid? Answered from the authority map (commitment 1's single source).
// MAIN THREAD ONLY (O1, recheck-c2b): it walks g_localOwned, a std::set the main thread mutates, so a find from a
// worker can race an erase and walk a freed node. A detour that can run on an engine worker uses IsUidMineAnyThread.
bool IsUidMine(unsigned int uid);
bool IsUidReleasedHere(unsigned int uid);
void MarkReleasedHere(unsigned int uid);    /* T-574: a body of this game's own world became another game's copy in place (a twin adopted). MAIN THREAD. */   /* T-574: this game ran the person and released it to another game (taken back: no). MAIN THREAD. */
// M4 fold (review 2026-09-29 H1, defence in depth): does the session hold a row for this uid - an owner record (g_owner) or
// a character this game runs (g_localOwned)? spawn.cpp AllocateUid skips (and counts) a minted uid that does. MAIN THREAD.
bool UidHasSessionRow(unsigned int uid);

// O1 (recheck-c2b): IsUidMine for ANY thread - a lock-free, allocation-free read of a fixed mirror of the owned set
// (common/ownedmirror.h) updated beside every g_localOwned insert/erase. Same answer as of the last update.
bool IsUidMineAnyThread(unsigned int uid);
// kit3 (kit2 review + T522, review D8): false while the mirror may be missing an owned uid (a refused insert since the last
// rebuild, or every slot live) - a "not mine" answer is then no proof. ANY THREAD.
bool OwnedMirrorExactAnyThread();
// O1-b (review-o1): MAIN THREAD, once per tick (SessionTick calls it). Rebuilds the mirror from g_localOwned into
// its spare buffer and swaps it in when crowded (under 1/4 empty with tombstones) or after a refused insert.
void OwnedMirrorMaintain();
// The [M1] REPORT numbers: 0 live, 1 tombstones, 2 empty (published table), 3 rebuilds, 4 refused inserts (all time).
long OwnedMirrorStat(int which);

// E22b-2 (P6b) / review-p5y MEDIUM-3. Does THAT PEER own this uid? The same two questions RemoteMayWrite
// asks - authorship wins, and the recorded owner must be the sender - WITHOUT touching
// `g_remoteMayWriteRefused`, which is the [M1] REPORT's split-brain number and must not start counting
// item-request refusals as well (that conflation is review-p5y LOW-2's shape).
bool UidOwnedByPeer(unsigned int uid, unsigned int fromPeer);
// tags1: WHICH peer told us it owns this uid (g_owner). False when none has. MAIN THREAD.
bool UidOwnerPeer(unsigned int uid, unsigned int* peer);
// P25 fold 1 M1 [p25f1-h]: is this recorded owner key (UidOwnerPeer's) the session peer - the one game SendTalk reaches? MAIN THREAD.
bool OwnerIsSessionPeer(unsigned int recorded);
// M5b proof verb 'livemove <n>': this game's next n MOVEs go through the notebook (LIVE, route WORLD) instead of the session
// link - one road each; 0 stops it. MAIN THREAD.
void SessionMoveViaLive(int n);

// Round-robin over the uids WE own, for the periodic state push. Returns false when we own
// none. Main thread only.
bool NextOwnedUid(unsigned int* out);
// S1: every uid this game drives, in one copy (empty when the link is down). MAIN THREAD.
void OwnedUidsSnapshot(std::vector<unsigned int>* out);
int  OwnedUidCount();   // H032: how many uids this instance owns (sizes the STATE refresh per tick)

// Record that WE own a uid we just created. Must be called on every local creation, not
// only when a SPAWN is successfully sent: authorship is what confers authority, and
// whether a peer has been told yet is a separate question (T023/F092).
void SetLocalOwner(unsigned int uid);

// E16 (review-p5i / F528). RemoteMayWrite is the split-brain guard: it REFUSES a remote write for a uid this
// game authored, and for a uid whose recorded owner is not the sender. Both refusals were silent, so a run in
// which the peer's STATE / MOVE / HIT / DESPAWN for a character was dropped here read exactly like a run in
// which none was sent. Counted at both refusing returns; printed as `remoteMayWriteRefused` in the [M1] REPORT.
// Spelled `__int64` - which is precisely what LONG64 is (winnt.h: typedef signed __int64 LONG64) - so this
// header stays free of <Windows.h>: fifteen translation units include it, several of them ahead of the Kenshi
// and Ogre headers, and pulling the Win32 macro set in front of those is not a risk this counter is worth.
extern volatile __int64 g_remoteMayWriteRefused;

// R1-a-b (review-r1a M2): RemoteMayWrite asked again for a write that waited - a parked prone
// write re-asks with the peer its STATE came from, so a handoff while it waited drops it. Same
// answer and same refusal counting as the guard. MAIN THREAD.
bool RemoteMayWriteStill(unsigned int uid, unsigned int fromPeer);

} // namespace net

// review-session S6 - THE LINK-DOWN EVENT. Published once on the LINK_UP -> down edge (SessionTick),
// never on a timer, and never when WE leave (SessionLeave already tears the session down).
//
// The peer is gone, so nothing will ever arrive to remove what it left here. This despawns the
// copies it owned (H030 withdraws a twin or a player-faction character of ours instead of destroying
// it), drops its ownership claims, retires the context platoons its announcements built, marks every
// adopted uid unannounced so a reconnect is told everything from scratch, and forgets its view
// distance. OUR authorship (g_localOwned) and the store's records survive it. MAIN THREAD.
void OnPeerGone(int goneSlot);   /* M8: goneSlot = the link peer's slot captured at its DOWN edge (-1 unknown); the rows removed are the ones captured there */
/* M8 (T-197 piece 8; src/common/peergone.h) - PER-PLAYER LEAVE. MAIN THREAD, all of them.
   PeerGoneCaptureSession: at the session link's DOWN edge, BEFORE PlayerFactionOnLinkDown - records the link peer's rows (uid -> key)
   and returns its slot. PeerGoneSweepPending: removes what is still captured (a discarded peer-gone action). PlayerGoneApply: the
   notebook's PLAYER_GONE {u16 slot} - that player's rows and loaded bit, no one else's. */
int PeerGoneCaptureSession();
int PeerGoneSweepPending(const char* why);
void PlayerGoneApply(const std::vector<char>& payload, const char* how);
void PlayerGoneNoteRecv();
std::string PlayerGoneCountsString();
void PlayerGoneTakeOverTick();   /* a final leaver's NPCs held for the area's taker, looked at once a second after the drain (MAIN THREAD) */

} // namespace coop
