// handoff.h - M-D step 1: SQUAD ownership transfer, forced by the owner leaving the sector (decisions 11-13).
//
// Rule (docs/authority-model.md 10a/10b, user-approved 2026-09-02): the unit is the squad (platoon); a squad
// whose sector is outside the ring around the owner's player is about to be unloaded by the owner's engine
// (6 s countdown), so it is handed to the other instance NOW if that instance has the sector loaded - mid-goal,
// mid-fight. Decision 15 (user, 2026-09-02): ownership is STICKY - no transfer inside an overlap; the goal-boundary
// crossing rule (M-D step 2, T133-T136) is retired. The wire keeps `forced`/reason for a future load balancer.
#pragma once
#include "net/session.h"
#include <string>
#include <vector>
class Character;
namespace coopsquad { struct SquadLeadMsg; }   /* T-1 B1 restructure: src/common/squadlead.h */
namespace cooplo { struct ReleaseMsg; struct ReleaseAckMsg; }   /* src/common/liveowner.h [a1b2-hh0] */
namespace coop {
void HandoffTick();                                                                  // MAIN THREAD, 1 Hz inside
void ApplyRemoteXfer(unsigned int leader, unsigned int reason, const net::XferMember* m, int count, unsigned int fromPeer, const unsigned int* gens);   /* M7a A1 build 1 [a1b1-hh0]: + the giver's gen per member */   /* protocol 84: reason (coopsquad::kXferReason*) */
void ApplyRemoteXferAck(unsigned int leader, const unsigned int* takenUids, int count, unsigned int fromPeer);   // F447
/* M7a A1 build 2 [a1b2-hh1] (design 2.3, 2.5), MAIN THREAD. ReleaseOnPutAway: NotifyDespawn's UNLOAD branch, own uid - collected into this
   frame's RELEASE batch (objWhole 0 = an off-thread destroy drained later: nothing read, its UNLOAD goes). ReleasePendingHas: uid is in an
   open release, this frame's batch, or an XFER awaiting its ACK - its UNLOAD is held. ApplyRemoteRelease / Ack: the RELEASE (68) and
   RELEASE_ACK (69) handlers (net/session.cpp). SquadIdxTaken: net::TakeLocalOwner - out of the squad index. SquadIdxOwnerWithdrew:
   the recorded owner's DESPAWN (unload 0) or UNLOAD / NOT-LIVE answer (unload 1) [review F9]. */
void ReleaseOnPutAway(unsigned int uid, const void* obj, int objWhole);
bool ReleasePendingHas(unsigned int uid);
void ApplyRemoteRelease(const cooplo::ReleaseMsg& m, unsigned int fromPeer);
void ApplyRemoteReleaseAck(const cooplo::ReleaseAckMsg& a, unsigned int fromPeer);
void SquadIdxTaken(unsigned int uid);
void SquadIdxOwnerWithdrew(unsigned int uid, int unload);
void SquadIdxOwnerStillRuns(unsigned int uid);   /* an announce-pass UNLOAD from the recorded owner: `given` stays (counted) */
void HandoffForgetWorldRequest();                /* M7a3f1: world teardown (ZonesForgetHeldGrid, possibly OFF the main thread inside a __finally) - POD request only; the main thread empties the owed state at its next use. g_pending is kept */
/* M7a3f3 T-425 [m7a3f3-hh0]: THE HANDED-OVER SQUADS (handoff.cpp) - (faction, engine squad id) of every squad this game released to another
   game; marked at the ACK, unmarked at a take back (XFER) or a sweep adoption the area rule allows, cleared at world teardown. MAIN THREAD. */
bool SquadKeyOf(::Character* c, void** faction, std::string* id);   /* c's squad: its faction and the engine's squad id; false = no squad / unreadable */
bool HandedOverHas(void* faction, const std::string& id);
bool HandedOverHasId(const std::string& id);   /* any faction: the T-300 drop queue's re-check (it walks every faction by id) */
bool HandedOverAny();
/* M7a3f4 [m7a3f4-hh0]: the left-behind notes (a person of the squad left this game without being taken - the squad is not marked, an
   earlier mark has no effect). The sweep, meeting a rebuilt person of a squad, clears its note and its mark (the person is live here
   again). A player gone: the squads handed to that player leave the index (HandedOverForgetPlayer, drained on the main thread). */
bool HandedOverLeftBehindAny();
void HandedOverLeftBehindRebuilt(void* faction, const std::string& id);
void HandedOverForgetPlayer(int slot);   /* MAIN THREAD - the player in `slot` left: the people handed to it leave the squad index (-1: the whole index) */
void HandoffRevokesPlayerGone(int slot);   /* fold 2 [a1b2f2-hh0] [review G2]: MAIN THREAD - a player left: the REVOKEs waiting from it and owed to it drop (-1: none, counted) */
void HandedOverUnmark(void* faction, const std::string& id, int why);   /* why 0 = a member taken back by hand-over, 1 = adopted by the sweep */
std::string HandoffReleaseReportToken();     /* [a1b2-hh2]: the release[...] / index[...] tokens of the [net] REPORT line */
bool HandoffPendingHas(unsigned int uid);   /* M4 fold 2 (re-check L-C): this game's XFER naming uid awaits its ACK - MAIN THREAD */
void ReportHandoff();
// `playerteleport dx dz`: move the watched player by (dx, dz) with the engine's own placement - the harness's way
// to make the host's engine unload a sector while the client still holds it.
bool PlayerTeleport(float dx, float dz);
bool PlayerTeleportAbs(float x, float z);   // to an absolute world x,z (y kept)
// P25 fold 2 (T780, TEST): to x,z at height y, or (ground) at the terrain's height there (GroundTerrainHeightAt; refused, logged,
// when there is none). MAIN THREAD. PlayerTeleportGround = the same at the player's own x,z.
bool PlayerTeleportAbsY(float x, float z, bool ground, float y);
bool PlayerTeleportGround();
// T-1 B0 (TEST LEVER): `playerteleport peer` - the watched player to about 5 m from this game's copy of the other player's lead
// character (the copy that is its own squad's leader, else the lowest uid). Logs one [TRADER] teleport peer line either way.
bool PlayerTeleportPeer();
// par23b: the engine's own whole-character teleport (Character::teleport 0x5C9BF0) for ONE character - the same
// call PlayerTeleport makes, exposed for the packbuytest test lever. false = the call faulted.
bool TeleportCharacter(::Character* c, float x, float y, float z);
// `squadnudge dx dz`: move the first squad this instance owns by (dx, dz) - harness lever (T137: proves no transfer
// happens inside an overlap).
bool SquadNudge(float dx, float dz);

// T-1 B1 (owner decisions 104/110): THE SQUAD READER - plain memory reads under SEH, never Character::getSquad 0x790F70 (which
// CREATES a squad, or returns one of the wrong faction, when Character+0x658 is null) and never getSquadLeader 0x791FF0 (it
// WRITES +0xA8). Character+0x658 = ActivePlatoon* (null = no squad); ActivePlatoon+0x78 = Platoon*, whose +0x1D8 must point
// back at the ActivePlatoon; +0xA0 leader; +0x60 member array, +0x58 count; +0xB4/+0xB8/+0xBC the squad position the engine
// sleeps and wakes the squad by (its leader's); +0xE8 the player-squad flag (read as a byte). Read, decompile 2026-09-28.
const int kSquadViewCap = 64;
struct SquadView
{
    void* ap; void* platoon; ::Character* leader;
    ::Character* acting;                    // +0xA8 the engine's acting leader (written by getSquadLeader 0x791FF0 when +0xA0 cannot lead)
    int count;                              // the engine's member count
    int stored;                             // members[] filled (count capped at kSquadViewCap)
    ::Character* members[kSquadViewCap];
    float x, y, z; int posUsable;           // the squad position; posUsable = finite and not the untouched origin
    int playerFlag;                         // ActivePlatoon+0xE8 != 0 (read as the qword the engine compares - review L1)
};
int ReadSquadAt(void* activePlatoon, SquadView* out);   // 1 read; 0 not a squad (null / back-pointer mismatch); -1 faulted
int ReadSquadOf(::Character* c, SquadView* out);        // the same, from a character (+0x658)
void* SquadActivePlatoonOf(::Character* c);             // the verified ActivePlatoon*, 0 = no squad
// The position that decides which game runs `c`: its squad's position when it is in a squad whose position is usable
// (1, *x/*z set); 0 = no squad or no usable position - the caller keeps the character's own position. The caller keeps its
// player-faction and stand-in short-circuits FIRST.
int SquadDecisionPos(::Character* c, float* x, float* z);
// T-164 B4-3 (M4): the same from an already-read squad (a trader squad with a home building: the home building's position).
int SquadDecisionPosAt(const SquadView& sv, float* x, float* z);
// T-1 B1 restructure (protocol 85): MSG_SQUAD_LEAD from the other game (src/common/squadlead.h) - the acting and formal leader its
// engine has for one of its squads and the members it runs there. The follow pass uses the acting leader AS IS; the formal leader
// becomes the copy squad's +0xA0 through the engine's own ActivePlatoon::setSquadLeader at the K2 safe point. MAIN THREAD (the drain).
void ApplyRemoteSquadLead(const coopsquad::SquadLeadMsg& m, unsigned int sender);   /* M7a A1 build 1 [a1b1-hh1]: the book is keyed by the announcing game */
/* M7a A1 build 1 [a1b1-hh2], MAIN THREAD. HandoffRosterPending [review F1]: uid is still being handed on by this game - build 2: an open
   release, this frame's release batch or an XFER naming it awaiting its ACK (ReleasePendingHas; a ROSTER CHECK is answered PENDING). SquadLeadReannounce [review F7]: the squads this game runs whose last announcement went to one of a catch-up ask's sectors announce their leader again at the next pass (fold 1 [a1b1f1-hh0] [F5]).
   HandoffAckWrongSender: XFER_ACKs ignored because they did not come from the XFER's target slot [review F11]. */
bool HandoffRosterPending(unsigned int uid);
// PROBE-START: P119 - MAIN THREAD: an engine put-away of this game's own non-player person (NotifyDespawn, object whole); the frame's batch
// logged per squad at the next HandoffTick; a keep sample of the live squads every 10 s (log only)
void P119NotePutAway(unsigned int uid, const void* obj, const char* reason);
void P119Flush();
// PROBE-END: P119
void SquadLeadReannounce(const std::vector<int>& sectorKeys);
long long HandoffAckWrongSender();
void SquadLeaderSafePointDrain();                       // K2 safe point (combat.cpp), 1 Hz inside
/* T-1 B3 restructure (protocol 89): a squad's money rides MSG_SQUAD_LEAD. MAIN THREAD, all of them. */
void SquadCatsSafePointDrain();                         // K2 safe point (combat.cpp): copy pots written, taken squads adopted
void SquadCatsAnnounceNow(void* keeper);                // the request road moved the pot of a squad whose leader this game runs
void SquadCatsNoteUndo(int kind, int shortfall);        /* F4 / T-1 B3 fold (M2, L1): 1 undone (shortfall: cats the pot could not give), 0 failed, 2 not the runner any more (nothing owed), 3 the pot changed */
std::string SquadCatsReport();                          // " squadCats[...]=..." for the [XFER] / [KEEPER] REPORT lines
// Owner 110: from the Faction::updateActivePlatoons 0x6BA810 pre-hook (towngen.cpp) - each of this faction's awake squads
// whose members are ALL copies of characters the other game runs, with one member on ground loaded here, gets its
// stay-awake timer (ActivePlatoon+0xB0) refreshed to 4.0 s. MAIN THREAD, once a second per faction.
void CopySquadKeepAwake(void* faction);
}
