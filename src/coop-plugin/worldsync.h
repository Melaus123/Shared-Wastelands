// worldsync.h - P033 / M7. The REPLICATION half of P-16 Option A.
//
// THE DECISION THIS SERVES (user, 2026-08-08): "a world that doesn't match isn't a multiplayer
// world." Option A - the peer stops generating its own world population and receives the
// authority's. Option B, a bounded compensation that leaves the crowds different, was rejected.
//
// WHERE THE OTHER HALF GOT TO. T080/F274 answered the precondition: the peer's generator CAN be
// switched off. With `worldgen leaf` the peer created ZERO world characters, kept only the 12
// save-derived characters both machines already agree on, and loaded FASTER than the stock control.
// So the peer can be reduced to a known common core. This file fills it back in from the authority.
// P4z (2026-09-03): `leaf` no longer means "create nothing" - it is AREA-GATED, so characters are
// created where this game holds the area (decision 33) and refused only where another player holds it.
//
// WHAT THIS IS NOT. It does not create anything. Every character it replicates already exists on
// the authority - the engine made them. This walks them, gives each a uid, and pushes them down the
// SAME spawn/appearance/clothing path that has carried hand-spawned characters for the whole
// project. **No new replication mechanism is invented here**, which is deliberate: the existing path
// is measured (appearance 4/4 sex, mesh, hair and height across two independent samples; position
// drift 1.4 units settled; `moveRecv` equal to the sum of per-uid applied counters at all six
// samples). Reusing it means a failure is a failure of SCALE, not of a fresh untested mechanism.
//
// WHY SCALE IS THE WHOLE RISK, and it is measured rather than feared:
//   * the bill is ~100-130 characters (T078/F265) against the 4-6 ever replicated before;
//   * appearance is 1,449-1,531 B each, so a full world is roughly 160 KB (F267);
//   * F267's other two blockers - a 64-slot uid mirror and a linear-scan reverse lookup on the
//     combat/hit/medical hot paths - are cleared in `spawn.cpp` as a prerequisite for this file
//     existing at all.
//
// SO IT IS THROTTLED, AND THE THROTTLE IS THE POINT. Adopting 130 characters in one tick would
// send ~160 KB in one frame and give the peer 130 characters to create in one pump. The engine's
// own reaction to that is UNKNOWN, and "unknown" is not something to discover at full scale. A
// budget per tick makes the burst a parameter instead of an accident, and makes a partial result
// readable: if adoption fails at character 40, the log says 40 rather than "it broke".
//
// ORDER MATTERS AND IS NOT ARBITRARY. Characters are adopted nearest-first from the authority's own
// viewpoint. If the run is cut short, or the budget is set low, what the peer HAS is the population
// the player is standing in - which is the part a player could actually notice missing.
#pragma once

class Character;   // F318: TrackForLiveness takes one; nothing here dereferences it

#include <string>   /* M7a: WorldsyncCatchupCounts */
#include <vector>   /* M7a2 [m7a2-wh0]: WorldsyncCatchupReverse */

namespace coop {

// Adopt world characters this instance owns and replicate them to the peer. Call from the main
// pump; it does its own throttling and returns immediately when there is nothing to do.
void WorldSyncTick();
void WorldSyncReannounceAll();   /* stand1 fold (1d): MAIN THREAD - this game's slot arrived: walk the re-announce queue again */

// `worldsync on|off|once <n>` - ON by default since decision 39 (2026-09-04): the pass claims the engine's own awake
// world characters for announcement, and since decision 31(c) a character nobody announced publishes no world events
// at all, so shipping it off meant a real session wrote nothing to the notebook. Nothing here runs before the session
// link is up, and the lever still turns it off.
//   on       - adopt continuously, `kAdoptPerTick` per tick, until the world is covered
//   off      - stop adopting. Characters already adopted STAY adopted; this is not a rollback,
//              and the log says so, because a command whose name suggests more than it does is
//              how a reader concludes a mechanism failed when it was never asked to undo anything
//   once <n> - adopt at most n more characters, then stop. The controlled-experiment lever: it is
//              how a run measures 10 characters before risking 130
void SetWorldSync(int mode, int budget);

// `worldradius <units>` - only adopt characters within this distance of our own viewpoint.
// 0 = unbounded. T084 measured 88.9% position parity for characters adopted within 500 units and
// 21.6% beyond 3000, so an unbounded sweep spends most of its budget on characters it cannot
// actually replicate - and fills the uid table doing it.
//
// An explicit command WINS: once set this way the radius is never overwritten by the derive.
void SetWorldRadius(float r);

// F302 - THIS machine's own `GameOptions::viewDistance`, for the handshake to send to the peer.
// false = could not read it, in which case the caller must send 0 ("I cannot tell you") rather
// than a default. The HOST does all the adopting and needs the CLIENT's value, not its own, so
// this is only ever used as the OUTBOUND half of the handshake - never as a local substitute.
bool LocalViewDistance(float* out);

// F311 - drop a radius that was derived from a peer's view distance, because that peer is gone and
// the number is now a fact about nobody. An EXPLICIT `worldradius` command is NOT forgotten: it is
// the operator's intent, not a fact about a peer.
void ForgetDerivedRadius();

// F318 - register a character for P034's liveness checking. **BOTH instances must call this.**
// The host calls it when it adopts a world character; the CLIENT must call it for every character
// it creates on the authority's instruction, because those are ordinary world objects the moment
// they exist and the engine can destroy them whenever it likes.
//
// T087: the host had 103 tracked rows and the client had ZERO while holding 62 raw character
// pointers, so on the client no pointer could ever be retired - and the client crashed
// dereferencing one inside `ApplyRemoteHit`.
void TrackForLiveness(unsigned int uid, ::Character* obj, bool authored);   // authored (recorded, read by nothing since F495) = adopted from our own world (the sweep / a twin), not created on the peer's instruction

// F322 - the engine's GameWorld, as an opaque pointer, or 0 when there is no world (this is
// reachable from the title screen). Opaque deliberately: worldgen.cpp declares its own minimal
// GameWorld to match the mangled name of the function it hooks, and including the real header there
// would collide with it. Same object either way; only the declaration differs.
void* WorldPtr();

// P039 / F331 - IS THE ENGINE PAUSED, AND AT WHAT SPEED.
//
// T091's host stopped simulating while frames and the network pump kept running, and the freeze
// line could not say why. A pause is the obvious candidate and **nothing in this project had ever
// read the flag**, so it could not be ruled in or out.
//
// Two trivial getters, both disassembled rather than taken from the header (whose RVA comments are
// stale - F020):
//     GameWorld::isPaused()                0xDEDC0   MOVZX EAX, byte ptr [RCX + 0x8B9] ; RET
//     GameWorld::frameSpeedScale() 0x66BCD0  MOVSS XMM0, dword ptr [RCX + 0x700] ; RET
// Both offsets appear in the `togglePause` decompile as the same two fields, so the getters and the
// setter agree - cross-checked, not assumed.
//
// **PAUSED IS NOT ONE FLAG.** `togglePause` computes it as `arg | (frameSpeedMultiplier == 0)`, and
// `setFrameSpeedMultiplier` (0x787170) writes the multiplier WITHOUT touching the byte. So a
// multiplier of zero is a stopped engine whether or not the byte says so, and any caller that
// branches on the byte alone will confidently report "not paused" in exactly the case that matters.
//
// **MECHANISM: PARTLY ESTABLISHED, AND AN EARLIER VERSION OF THIS COMMENT CLAIMED MORE.** It said
// `togglePause` pauses "the worker threads whose counters F145 watches". It does call
// `ThreadWannabe::setPaused`, but on two members, `PhysicsInterface* physics`
// (+0x18) and `AudioSystemGlobal* audioThread` (+0x8C0) - **not** the AI backthread, which is
// `_AINonRenderThread` at +0x790 and which `togglePause` never touches.
//
// What IS read (F335, via `tools/ghidra-scripts/VtblSlot.java`): `mainLoop_GPUSensitiveStuff`
// branches between `GameWorld::charsUpdate` (0x7862F0, per-character virtual slot +0xE8 =
// `periodicUpdate`) and `GameWorld::charsUpdatePaused` (0x7866F0, slot +0x270 =
// `Character::pausedUpdate`). **A paused frame runs a DIFFERENT per-character entry point, not
// none.** Our three counters sit on +0xD8 (`threadedUpdate`), +0xE0 (`update`) and
// `AI::periodicTick` - **none of them is either dispatched slot**, so whether a pause stops
// them depends on what `periodicUpdate` reaches that `pausedUpdate` does not. The AI funnel is the
// plausible half (F059/F060); the other two are driven from different lists and that half is
// **still open.**
//
// THIS IS A MEASUREMENT, NOT A DIAGNOSIS, and it does not depend on the mechanism being known:
// `paused=1` (or a zero multiplier) at a freeze says the engine was stopped and moves the question
// to what stopped it; a running speed with the byte clear rules the explanation out and leaves the
// worker threads as the story.
//
// Returns false when GAMEPLAY IS NOT RUNNING (`GameplayRunning()` - did the in-game frame counter
// advance this tick). A pointer test cannot answer this: `ou` is a static in the exe image and is
// never null (F034/F333), and a cumulative `MainLoopFrames() > 0` answers "has gameplay EVER
// started", which stays true forever after a quit to menu (F337). The caller prints the refusal as
// itself rather than as a plausible zero.
bool PauseSnapshot(bool* paused, float* speedMul);

// H030: true when this uid was announced to the peer (or predates the announce gate). The stream senders
// (replicate.cpp MOVE/INTENT, medical.cpp STATE) skip uids the peer was never told about.
bool AnnouncedToPeer(unsigned int uid);
// crime5 (review-crime5 8): the same answer with no counting - for samplers that ask every frame and would otherwise inflate
// streamGateWithheld. MAIN THREAD.
bool AnnouncedToPeerQuiet(unsigned int uid);

// E9 / decision 35 amended (F524) - OUR OWN ENGINE HAS UNLOADED THIS CHARACTER; TELL THE PEER TO DROP ITS COPY.
//
// The 1 Hz announce pass can only withdraw a character it can still resolve, and an unload destroys the character and
// retires its index row before the pass next runs - so the pass never sees the population that streams out. T219 is what
// that costs: the client unloaded a town and its host kept ~50 copies, driven by the host's own AI under uids the host
// can never announce, unload or despawn. This is called from the destroy path instead, where the uid is still in hand.
//
// Sends an UNLOAD (retire the copy), never a DESPAWN (a death). No-op unless the uid is currently marked announced.
// Returns true when a message went out. MAIN THREAD only.
enum { kUnloadWhyPutAway = 0, kUnloadWhyReloaded = 1, kUnloadWhyHeldSettled = 2, kUnloadWhyRetire = 3, kUnloadWhyAnnounce = 4, kUnloadWhyCount = 5 };   /* M7a3f2: why an UNLOAD was sent (logged first 20 each, then counted) */
bool WithdrawAnnouncedOnOwnUnload(unsigned int uid, int why = kUnloadWhyPutAway);
bool WithdrawHeldUnload(unsigned int uid);   /* M7a3 P3: an UNLOAD held for an owed hand-over, released (nobody holds the sector): sent now, else owed to the P8m drain. true = sent */

// inv7e2 (T418; fold: the WORLD-LOAD generation) in which this game's OWN SWEEP adopted a uid (1 = tracked, 0 = not a sweep adoption
// of this world, or forgotten). Set at adoption, erased when the uid arrives by XFER or is withdrawn below, cleared at
// world teardown. MAIN THREAD.
int SweepAdoptGen(unsigned int uid, long* gen);
void ForgetSweepAdopt(unsigned int uid);
// inv7e2: true only for an explicit "announced" mark (E9's conservative rule). MAIN THREAD.
bool AnnouncedExplicit(unsigned int uid);
// M7a3f3 H1 [m7a3f3-wh0]: the three states of the announce mark - -1 no entry (a character TAKEN by hand-over, or a row from before the
// gate), 0 an EXPLICIT withheld mark (the other games have no copy), 1 announced (coopsquad::kAnn*). MAIN THREAD.
int AnnouncedState(unsigned int uid);
void NoteAnnouncedOnTake(unsigned int uid);   /* M7a3f3 [m7a3f3-ws2] */
// inv7e2: a reloaded copy's character was retired - unregister it (tracking, adopted row, uid->object index) and withdraw
// what this game announced for it. 1 = the peer's copy is withdrawn (now, or already by the unload-destroy path),
// 0 = it was never announced, 2 = the send was declined and the P8m registry owes it. MAIN THREAD.
int WithdrawReloadedCopy(unsigned int uid, bool wasAnnounced);

// Counters into the report line. Read-only.
void ReportWorldSync();

// review-p3o H2 - drop the P034 liveness rows (each holds a raw object address) and the per-uid
// announce marks before GameWorld::_clearAndDestroyGameWorldStuff frees the characters they name.
// Touches no engine memory. MAIN THREAD, and only from the store's world-teardown hook.
void WorldsyncWorldTeardown();

// review-session S6 - the peer's link went down. Marks every adopted uid as NOT announced (set to
// 0, never erased - an absent uid reads as ANNOUNCED) and resets the re-announce cursor, so a
// reconnecting peer is told about everything from scratch and nothing is streamed at it before it
// has been. The adopted rows are OURS and are kept. Returns how many uids are now marked.
int WorldsyncPeerGone(int slot);   /* M8: slot = the link peer's, captured at its DOWN edge (-1 unknown) - only its loaded bit is forgotten when known */
int WorldsyncPlayerGone(int slot);  /* M8: PLAYER_GONE - that slot's loaded bit only; returns the bits armed (0 or 1) */
void WorldsyncOnSessionLinkDown();  /* M8 review F2: the session link's DOWN edge clears the link-up latch, so every reconnect re-announces */

/* P8a (build/read-roster-t236.md 4c): THE ADOPTION PASS'S OWN VERDICT FOR ONE CHARACTER.  `d33`'s
   skippedOtherHeld is a per-tick-per-character aggregate - T236a's 472,148 over 6,830 ticks divides to 69.1
   characters standing refused against 130 the roster showed with no uid at all, the same order and not the
   same number, so the read had to record the attribution as INFERRED.  This answers it per character.  It is
   the SAME predicate the sweep takes, called and not copied, and it moves no counter (lesson 11).
   MAIN THREAD, read-only.  One of: unreadable, alreadySpawned, guard, playerFaction, skippedOtherHeld,
   skippedNoMap, adoptableNoMap, radius, adoptable. */
const char* AdoptGateFor(void* character);

/* M7a (T-197 piece 7a): the notebook's sector key (src/common/liverelay.h AreaKey) of a world position, and of an owned
   character's position READ NOW (-1 = no such character, or not readable). net/session.cpp routes the character stream by them.
   MAIN THREAD. */
int AreaKeyAt(float x, float z);
int CharAreaKeyNow(unsigned int uid);
/* M7a: the catch-up's own counters for the [net] REPORT line. */
std::string WorldsyncCatchupCounts();
/* M7a2 [m7a2-wh1]: MAIN THREAD, a running world. Reverse catch-up (item 5): this game's own CATCHUP ASKED - its characters in those
   sectors re-sent by AREA to the games already covering them. A hand-over settled (item 2): a character a catch-up held back is
   re-sent to those askers while it is still this game's. */
void WorldsyncCatchupReverse(unsigned int askNo, const std::vector<int>& keys);
void WorldsyncCatchupHandoverSettled(unsigned int uid);
/* M7a2 fold 1 item 8 [m7a2f-wh0]: WorldsyncCatchupReverse only QUEUES the sectors (the store pump calls it); this walks them - MAIN THREAD,
   net::SessionCatchupApplyTick (after the drain) [m7a2f-ap5], a running world with engine writes allowed, at most 64 characters a call. */
void WorldsyncCatchupReverseTick();

} // namespace coop
