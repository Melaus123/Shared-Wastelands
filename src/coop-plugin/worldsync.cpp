// worldsync.cpp - P033 / M7. See worldsync.h for why this exists.
//
// MAIN THREAD ONLY. It walks the engine's character update list, reads game data and sends on the
// transport - none of which is safe off the pump. T080 measured every world-generation hook firing
// on the main thread, so there is no worker-thread path into this at all.

#include "worldsync.h"
#include "spawn.h"
#include "../common/orphanpurge.h"   /* P97 (p97-announce): AnnounceArea - the announce sweep's area answer, a pure function */
#include "zones.h"   // M-A step 2: OtherHasSector - announce only into sectors the peer holds
#include "store.h"   // review-p5b HIGH-1: StoreRelayLinked - which clock the announce gate's freshness guard must read
#include "appearance.h"
#include "net/session.h"
#include "ai_spike.h"   // GetTarget - the viewpoint the sweep orders by
#include "soak.h"       // F333: MainLoopFrames - has gameplay started at all
#include "identity.h"   // P034: the engine's own handle, for stale-pointer detection
#include "../common/slotwire.h"   /* stand1 fold (1d): WireLacksSlot */
#include "../common/liverelay.h"   /* M6 (T-197 piece 6): CATCHUP ASK / CATCHUP_END and the sector key */
#include "medical.h"   /* M7a: StatePush - the catch-up's STATE */
#include "combat.h"    /* M7a: CombatModeResendOwned - the catch-up's COMBATMODE */
#include "replicate.h"   /* M7a2 item 9 [m7a2-ws0]: IntentResendOwned - the catch-up's INTENT */
#include "playerfaction.h"   // P3: the player faction travels as "@player:<name>" (decision 23)
#include "../common/standinpurge.h"   /* inv7a-b: IsStandInRecord - the purge's own stand-in test */
#include <map>   /* area2 fold: the sweep's per-faction stand-in answer */
#include "../common/uidtable.h"   /* mirror1 fold (review-mirror1 #3): SweepRefusalAction / LogRefusal, offline-tested */
#include "../common/retirewithdraw.h"   /* P8m (H-ghost): the pending-withdraw transition as a pure function, swept by the offline suite */

#include "coop_log.h"
#include "handoff.h"   /* T-1 B1: SquadDecisionPos - a squad member is decided by its squad's position */
#include "../common/squadwriter.h"   /* M7a3f3 [m7a3f3-ws0]: kAnn* and HandedOverSweepAction */
#include "game/Character.h"
#include "addresses.h"   /* mig3: coop::GameWorldPtr() / OptionsPtr() - our rows for what `ou` / `options` imported */
#include "game/GameWorld.h"
#include "game/GameData.h"
#include "game/Faction.h"
#include "game/OptionsHolder.h"   // F298: the player's own viewDistance
#include <ogre/OgreVector3.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>   /* stale1: sprintf of two addresses */

#include <string>
#include <sstream>
#include <locale>
#include <cmath>

namespace coop {

namespace {

const int kModeOff  = 0;
const int kModeOn   = 1;
const int kModeOnce = 2;

// Adoptions attempted per tick. The pump runs ~900x/s, so even 2 per tick clears a 130-character
// world in well under a second of wall time (F041 measured ~900/s at the TITLE screen; F071
// measured ~118/s IN GAME, which is the rate that matters here) - the throttle spreads the ~160 KB of
// appearance payload and the peer's creation work across frames, NOT to be slow. It is small
// deliberately: the cost of it being too low is a slightly longer fill, and the cost of it being
// too high is a burst whose effect on the engine is UNKNOWN.
// 2026-09-02 (T127): measured 110 adoptions in 28 s at 2/30 - a visible minute of missing people for the user,
// and the burst risk is answered (120-154 characters adopted per run, T121-T127, no incident). 6 per sweep, every 6 ticks.
const int kAdoptPerTick = 6;

// Run the sweep on 1 tick in 30. See the note in WorldSyncTick: the walk is O(characters) and the
// engine cannot change its population faster than its own 2-second top-up, so a per-frame walk is
// pure cost - most of it spent after the sweep is already complete.
const long long kSweepEveryTicks = 6;

// ADOPTION RADIUS. T084 measured drift against distance-at-adoption over all 147 characters and the
// split is not subtle:
//
//   adopted  <500 units away :  n= 18   88.9% within 2.0 units   median drift    1.1
//   adopted 500-3000 away    :  n= 27   40.7%                    median drift   29.1
//   adopted >=3000 away      :  n=102   21.6%                    median drift 2956.3
//
// **Characters adopted near the player replicate almost perfectly. Characters adopted far away
// almost never do** - and 102 of 147 were in that far bucket, which is simultaneously why position
// parity sat at 33% and why the uid table filled and stopped adoption at 147.
//
// So both of T084's remaining failures have ONE cause: the sweep adopts the whole map rather than
// the player's surroundings. Bounding it does not give up parity we had - a character drifting
// 2,956 units is not replicated in any useful sense - it stops spending the budget where it does
// not work.
//
// MECHANISM: NOT ESTABLISHED. The plausible reading is that the peer has not streamed those distant
// regions, so the character exists but is not simulated and cannot walk toward the streamed target
// (the drive is a walk command, not a teleport). That is a hypothesis and is written down as one.
//
// Settable, because the right radius is a measurement rather than a guess and the next run should
// be able to move it without a rebuild. 0 = unbounded (the T084 behaviour).
float g_adoptRadius = 0.0f;   /* decision 36 (2026-09-03): unlimited by default on every role - the awake list and the held areas bound it (F519) */

// WHERE the radius came from. Four genuinely different provenances, and conflating them is how a
// guess gets read later as a measurement (F299's `radiusFromView=` was a bool covering three of
// these). Printed by name in the report.
const int kRadiusFallback = 0;   // the compiled-in default - 0 (unlimited) since decision 36
const int kRadiusClient   = 1;   // the CLIENT's viewDistance, from HELLO. The correct source (F302)
const int kRadiusExplicit = 2;   // a `worldradius` command. Wins over everything; never overwritten
int g_radiusSource = kRadiusFallback;
const float kFallbackRadius = 0.0f;   /* decision 36 (2026-09-03): unlimited by default on every role - the awake list and the held areas bound it (F519) */
bool g_saidWaiting = false;   // F311: per SESSION, not per process - reset by ForgetDerivedRadius
long long g_radiusDeriveIgnored = 0;   /* decision 36: how many times the retired derive was called and did nothing */

const char* RadiusSourceName(int s)
{
    return s == kRadiusClient   ? "client-VIEW_DISTANCE"
         : s == kRadiusExplicit ? "explicit-command"
                                : "DEFAULT-unlimited";
}
long long g_skippedTooFar = 0;
long long g_tick = 0;

// E28 / decision 39 (approved 2026-09-04) - ON BY DEFAULT. This pass is what claims the engine's own awake world
// characters for announcement, and since decision 31(c) an unannounced character's world events are not published
// at all - so shipping it off meant a real session announced nothing and wrote nothing to the notebook
// (review-p5q HIGH-1). Nothing here can run early: the sweep refuses outright until the session link is up (see
// `if (!net::LinkIsUp()) return;` below), which is the same guard that made the off default look safe. The
// `worldsync` lever still overrides in both directions.
int  g_mode = kModeOn;
int  g_budget    = 0;      // remaining adoptions in `once` mode
long long g_adopted     = 0;
long long g_skippedOurs = 0;   // NOTE: the walk skips already-ours characters before AdoptExisting
                               // is reached, so this can only rise if that ordering changes. Kept
                               // because a counter that is structurally zero is worth saying so at.
long long g_unreadable  = 0;
long long g_noTemplate  = 0;
long long g_noFaction   = 0;
long long g_sendFailed  = 0;
long long g_registerFailed = 0;   // registrations refused - counted and skipped; never stops the sweep (mirror1 fold, review-mirror1 #3)
bool g_sweepComplete = false;
bool g_refusedClient = false;   // host-only guard, reported once

// P034 - IS THE STALE-POINTER PROBLEM REAL? A PROBE, NOT A FIX.
//
// F278 records the hazard: nothing is ever removed from the uid mirror, and `AdoptExisting` now
// registers characters the ENGINE owns, whose destruction this mod neither controls nor observes -
// on an engine that streams the world by region. If an address is freed and recycled,
// FindSpawnedUid returns a DEAD character's uid: the peer's copy would follow whoever inherited the
// address, hits would be attributed to a retired uid, and a genuinely new character would be
// permanently invisible to the sweep.
//
// The fix for that is real work - dead-flagging slots without breaking the write-once property the
// lock-free index depends on. **So measure first.** Building the fix before knowing whether the
// condition ever occurs would be attempt 2 on an untested theory, which this project has paid for.
//
// The test uses the engine's OWN mechanism rather than a heuristic: every adopted character's
// `hand` is captured at adoption, and the engine's registry resolves a handle through a SERIAL
// check that returns null for a recycled index (identity.cpp). So `ResolveObjId(id) != obj` is the
// engine telling us the pointer no longer means what it did.
const int kMaxAdopted = 4096;   // T-354: kMaxMirror is 4096 (mirror1 (crash T487) made it 2048)
struct Adopted { unsigned int uid; const void* obj; ObjId id; bool retired; bool permanent; bool authored; };   // authored (F494/F495): recorded, read by nothing since the keep-asleep branch was removed; THIS game adopted it from its own world (F494: the table is liveness for both adopted and received characters)
Adopted g_adoptedRows[kMaxAdopted] = {0};
// M-A step 2 (decision 1: a non-owner holds puppets only for sectors it has loaded; T130 found ~74 copies pushed
// into sectors the client had not loaded). Per adopted uid: 1 = announced, 0 = withheld/unloaded.
// M7a: "announced" is ANNOUNCED TO THE NOTEBOOK while its road is up (the SPAWN went route AREA; which games heard it is the
// notebook's delivery set, and a game that newly has the area is re-sent the character by the catch-up - cooplive::AnnounceDecide),
// and to the session peer only while the notebook road is down.
std::map<unsigned int, int> g_announced;
long long g_withheld = 0, g_unloadsSent = 0, g_reannouncedOnReload = 0, g_announceTicks = 0, g_announceStale = 0, g_announceSkippedHandover = 0, g_streamGateWithheld = 0;
long long g_withdrawnOwnUnload = 0;   /* E9: UNLOADs sent because WE no longer have the sector loaded - the half of decision 35 that the peer-held test cannot see. A subset of g_unloadsSent, never an addition to it. */
bool PeerHoldsSectorOf(const Ogre::Vector3& pos) { return OtherHasSector(SectorOf(pos.x, pos.z)); }
int  g_adoptedCount = 0;
long long g_livenessRowsReused = 0, g_livenessFull = 0;   /* mirror1: a settled row reused / a character left unchecked */
unsigned int g_validateCursor = 0;   // unsigned: a signed overflow would index negatively
long long g_stalePointers = 0;   // resolved to a DIFFERENT object - recycling actually happened
long long g_unresolvable  = 0;   // every failed resolve, whether or not it led to a retirement
long long g_validated     = 0;
long long g_noHandle      = 0;   // could not capture a handle at adoption - not testable
long long g_retired       = 0;   // pointer withdrawn from use after a failed resolve
long long g_restored      = 0;   // came back - the failure was transient (region streaming)
/* stale1 (T276 log read, cloud/ANSWERS.md "crime3 - T276 log read"): a copy UNLOADed on a teleport and re-SPAWNed under the
   SAME uid got a new row, but the OLD row (old address) stayed. When the engine reused the old address, that row's stale
   test DECLINED the mark and retired the uid (F337) - killing the NEW live copy: the other player's character froze for
   the rest of the session (A 151 / B 148 "MOVE for unknown uid ... WE ONCE HELD IT"). */
long long g_rowsSuperseded = 0;    // older rows of a uid set aside when the uid was registered again with a new object
long long g_staleUidSpared = 0;    // a stale row whose uid now names a DIFFERENT live object: that uid is not retired

// RE-ANNOUNCE ON LINK-UP. SendSpawn has exactly two callers - creation and adoption - and neither
// fires when a link comes up, so a peer that joins AFTER adoption never hears about any of it. The
// host then streams MOVE and STATE for ~130 uids the peer refuses, because its own ownership map
// has no entry for them. An adversarial review found this as the unfixed quarter of F279: that
// change turned "silently dead" into "loudly useless", which is better and still not working.
//
// Safe to repeat: ApplyRemoteSpawn is idempotent (CreateAt ignores a uid it already holds), so
// re-announcing to a peer that already has the character costs a packet and changes nothing.
bool      g_linkWasUp   = false;
int       g_reannounceAt = -1;   // cursor into g_adoptedRows; -1 = nothing to do
long long g_reannounced  = 0;
long long g_reannounceSkippedNotMine = 0;   /* announce1: link-up rows skipped - a copy of the peer's character, or superseded */

// A SECOND, INDEPENDENT WITNESS FOR "IS IT GONE".
//
// P034 asks the engine's handle registry. A review raised two ways that answer can be a FALSE
// POSITIVE - a handle whose type is not CHARACTER (now refused at capture), and, if the registry
// lookup keys on the container fields, **every routine squad change would read as destruction**.
// I tried to settle the second by decompiling `hand::getCharacter` (0x7974F0) and the registry
// lookup behind it (0x4FDF90): the lookup passes the whole hand through a VIRTUAL call, so the
// field usage is behind a vtable dispatch and **cannot be resolved statically.** NOT ESTABLISHED.
//
// So ask something that does not depend on handles at all: **is the pointer still in the engine's
// own character update list?** That is a pointer COMPARE against a list the engine maintains - it
// never dereferences our stored pointer, so it is safe even if the object is freed.
//
//   still in the list  -> the character is ALIVE and the handle failure is a false positive
//   not in the list    -> the engine is not updating it; treating it as gone is justified
//
// This is what turns "the handle says gone" into "the handle says gone AND the world agrees", and
// it makes the false-positive rate a MEASURED number (`falseGone`) instead of an open worry.
long long g_falseGone = 0;   // handle failed, but the object is still in the update list

// Local copies, matching what spawn.cpp/combat.cpp/identity.cpp each keep. Every engine pointer is
// validated before use (F051), and PlausibleObject additionally requires a vtable inside the game
// image - POLYMORPHIC TYPES ONLY, which Character, GameData and Faction all are.
bool PlausiblePtr(const void* p)
{
    uintptr_t v = (uintptr_t)p;
    if (v < 0x10000 || v >= 0x00007FFFFFFFFFFFull) return false;
    if (v & 0x7) return false;
    __try { volatile uintptr_t probe = *(uintptr_t*)v; (void)probe; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

bool PlausibleObject(const void* p)
{
    if (!PlausiblePtr(p)) return false;
    uintptr_t vtable = 0;
    __try { vtable = *(uintptr_t*)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    return vtable > base && vtable < base + 0x4000000;
}

std::string S(long long v)
{
    std::stringstream ss; ss.imbue(std::locale::classic()); ss << v; return ss.str();
}

std::string F1(float v)
{
    std::stringstream ss; ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed); ss.precision(1); ss << v; return ss.str();
}

// F298, and the framing is the USER'S rather than mine: "npc's so far away that they dont render to
// a normal player probably dont need to be streamed to the client until they're within render
// distance."
//
// F294 reached a distance bound empirically - characters adopted beyond 3,000 units drift by a
// median of 2,956 - and then I picked 2,000 as a round number. **The engine already knows the right
// number, and it is a PLAYER SETTING**: `GameOptions::viewDistance`, reached through the game's
// options global (row OptionsHolderGlobal). A character this machine cannot render does not need to be in position on it.
//
// **F302 - AND IT IS THE CLIENT'S SETTING, NOT THIS MACHINE'S.** F299 read `options` locally and
// flagged that the HOST does all the adopting and was therefore reading the wrong machine. The user
// settled it, and the reasoning is the correct one:
//
// > *"it should be based on client view distance, so that the client can see based on their own
// > setting, the host should already see everything based on their view distance as the authority."*
//
// The host is the authority - it simulates its whole world regardless of any radius. The only
// question a radius answers is **what does the CLIENT need sent to it.** Both error directions are
// real: a client set to see FURTHER gets empty space where the host declined to replicate; a client
// set to see LESS pays bandwidth and a uid slot for characters it will never draw.
//
// So the client sends its viewDistance in HELLO (protocol 15) and the host adopts against that.
// Only the client sends HELLO, so the value is unambiguous - there is no "which side is this from?"
// to get wrong.
//
// The source is NAMED in the log and in the report (`radiusSource=`), never implied. A fallback that
// reads as a derived value is the label defect this project has now paid for four times, and here
// there are four genuinely different provenances.
//
// Since decision 36 the default is 0 (unlimited) on every role; an explicit `worldradius` lever
// still overrides it.
//
// Split out because `__try` cannot live in a function that requires object unwinding, and the
// caller builds std::strings for its log lines. Known pattern in this project.
bool ReadViewDistance(float* out)
{
    if (!PlausiblePtr(coop::OptionsPtr())) return false;
    __try { *out = coop::OptionsPtr()->viewDistance; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// RETIRED by decision 36, completed in P5f. The host used to narrow its adoption distance to the
// CLIENT's viewDistance (F302's derive). Decision 36 made the distance UNLIMITED on every role -
// the set of characters a game can announce is already bounded by the engine's awake list and by
// the areas it holds (decision 33) - so a distance derived from the client's render setting can now
// only make the host announce LESS than the rule says, which is F519's failure wearing a new hat.
//
// The function and its caller are KEPT so the wire message is still consumed and still reportable:
// the client's value continues to arrive at handshake, is still readable at net::PeerViewDistance(),
// and is still printed as `clientViewDistance=` in the [P033] report. Nothing acts on it. An explicit
// `worldradius` command remains the one thing that sets a radius. `kRadiusClient` stays in the enum
// so an old log naming that source still reads correctly.
void DeriveRadiusFromViewDistance()
{
    ++g_radiusDeriveIgnored;
    if (g_saidWaiting) return;   // once per session (ForgetDerivedRadius resets it)
    g_saidWaiting = true;
    DebugLog("[P033] client VIEW_DISTANCE ignored - the adoption distance is unlimited on every role (decision 36)");
}

} // namespace

// Defined below; declared here because WorldSyncTick calls it and P034 reads better next to the
// rows it validates than hoisted above them.
void ValidateAdopted(int howMany);

// F302. THIS machine's own render distance, for the handshake to send to the other side.
// Deliberately lives here rather than in session.cpp: the `options` include and the guarded read
// already exist here, and one implementation means the two ends of the field cannot drift apart.
// Returns false when it cannot be read - the caller sends 0, which means "I cannot tell you", and
// nobody substitutes a plausible number for it.
bool LocalViewDistance(float* out)
{
    if (out == 0) return false;
    *out = 0.0f;
    return ReadViewDistance(out);
}

void* WorldPtr() { return PlausiblePtr(coop::GameWorldPtr()) ? (void*)coop::GameWorldPtr() : 0; }

// P039 / F331. See worldsync.h for why this exists and what was read to justify it.
//
// Both calls are one-instruction getters, so there is no engine state to disturb and nothing to
// deadlock on.
//
// F333 - THE GUARD THAT MATTERS HERE IS **NOT** THE POINTER CHECK. The first version returned false
// when `PlausiblePtr(ou)` failed and promised the caller an `UNREADABLE` reading at the title
// screen. That branch is **dead code**: F034 established that the GameWorld is a static inside the
// exe image, so `ou` is non-null and valid from preload onward - `setGameSpeed` even reaches these
// same two fields through absolute addresses. A pointer check cannot tell the title screen from a
// loaded game, and the probe would have printed a confident `paused=0 speedMul=0.00` for a world
// that had not started.
//
// `GameplayRunning()` is the real test - "did the in-game frame counter advance this tick", which
// is the only form that also closes again after a quit to menu (F337; a cumulative `> 0` test does
// not). The plausibility check is kept because it costs nothing and is the file's convention, not
// because it is expected to fire.
bool PauseSnapshot(bool* paused, float* speedMul)
{
    if (paused == 0 || speedMul == 0) return false;
    *paused   = false;
    *speedMul = 0.0f;
    // F337 - "running now", not "has ever run". `MainLoopFrames() > 0` is cumulative and would keep
    // answering after a quit to menu, reporting the pause state of a world nobody is in.
    if (!GameplayRunning()) return false;      // no gameplay - say so, do not answer
    if (!PlausiblePtr(coop::GameWorldPtr())) return false;
    *paused   = coop::GameWorldPtr()->isPaused();
    *speedMul = coop::GameWorldPtr()->frameSpeedScale();
    return true;
}

void ForgetDerivedRadius()
{
    g_saidWaiting = false;   // the next session gets to say its own piece
    if (g_radiusSource != kRadiusClient) return;   // explicit command and fallback both stand
    DebugLog("[P033] adopt radius " + F1(g_adoptRadius) + " was derived from a peer's VIEW_DISTANCE"
             " and that peer is gone - reverting to DEFAULT-unlimited " + F1(kFallbackRadius)
             + ". A radius belonging to a peer that has left is a fact about nobody, and it would"
             " otherwise read as current at every instrument (F311).");
    g_adoptRadius  = kFallbackRadius;
    g_radiusSource = kRadiusFallback;
}

void SetWorldRadius(float r)
{
    g_adoptRadius = r;
    g_radiusSource = kRadiusExplicit;   // an explicit command wins; the derive must not overwrite it
    DebugLog("[P033] adopt radius = " + F1(r)
             + (r <= 0.0f ? " (UNBOUNDED - adopts the whole map, which T084 measured as 21.6%"
                            " position parity beyond 3000 units and a filled uid table)"
                          : " units. Characters beyond this are NOT adopted and are counted in"
                            " skippedTooFar."));
}

void SetWorldSync(int mode, int budget)
{
    g_mode   = mode;
    g_budget = budget;
    if (mode != kModeOff) g_sweepComplete = false;

    if (mode == kModeOff)
        DebugLog("[P033] worldsync OFF. NOTE: characters already adopted STAY adopted and keep"
                 " streaming - this stops further adoption, it does not undo any.");
    else if (mode == kModeOnce)
        DebugLog("[P033] worldsync ONCE budget=" + S(budget)
                 + " - at most this many more characters will be adopted, then it stops itself.");
    else
        DebugLog("[P033] worldsync ON - adopting up to " + S(kAdoptPerTick)
                 + " per tick until the world is covered.");
}

// H030: MOVE/STATE/INTENT are streamed only for uids announced to the peer (M-A step 2 withholds the SPAWN; T131's
// client logged 'MOVE refused ... owner unknown' for every withheld uid). Rows adopted before the gate count as announced.
// decision 33 (F513): may THIS game announce a world character standing at (x, z)? Yes when no other player holds the area.
// P8e (audit C5, F669): NO FRESH MAP IS NO LONGER A ROLE QUESTION. It used to be `return isHost` - the
// world's default, as in towngen - so a client whose map aged out stopped announcing world characters
// where a host kept going. Now nobody announces while there is no map - and P97 (owner 334 a / 337 a) retired
// the bounded wait after which slot 0 used to: the sweep re-walks every un-adopted character each pass, so the
// holder adopts and announces once the map is fresh again. One answer for every game.
long long g_worldAdoptable = 0, g_skippedOtherHeld = 0, g_skippedNoMap = 0;
/* P8e (build/read-host-audit.md C5/C6, F669): the number that replaces the role (its sibling,
   the bounded no-map wait's counter, is RETIRED by P97).  `reannounceDrained` is rows the re-announce queue actually walked, which nothing counted while the
   drain was host-only and a client's zero could not be told from a queue that was empty. */
long long g_reannounceDrained = 0;
// PROBE-START: P021 - T209: the client reached the area rule 12 times all run; count every drop in the main loop
long long g_p21Seen = 0, g_p21Spawned = 0, g_p21Guard = 0, g_p21Rule = 0, g_p21Radius = 0, g_p21Ticks = 0; int g_p21Logged = 0;
long long g_sweepContextSkipped = 0; int g_sweepContextLogged = 0;   /* rebuilt people of a context platoon the sweep did not adopt */
long long g_sweepHandedOverSkipped = 0, g_sweepHandedOverAdopted = 0; int g_sweepHandedOverLogged = 0;   /* M7a3f3 T-425 [m7a3f3-ws2] */
long long g_sweepStandInSkipped = 0;   /* inv7a-b: stand-in copies (faction RECORD coop-p<n> / coop-peer) the sweep met and did not adopt (pass 0 only) */
static ::Faction* SweepFactionPod(::Character* c)   /* area2 fold: the faction read under its own frame (the purge's PurgeFactionPod form) */
{
    __try { return c->getOwnerFactionDirect(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// PROBE-START: P022 - are the "already spawned" candidates real mirrors or stale index hits at recycled addresses?
int g_p22Logged = 0; bool g_p22EyeMine = false; long long g_p22Stale = 0, g_p22Real = 0;
// PROBE-END: P022
// PROBE-END: P021
/* P8e: and the `isHost` term is GONE - see the comment above and audit row C5.
   P8a: THE AREA RULE, ONCE, WITH ITS ANSWER NAMED.  The sweep needs a yes/no and the roster needs to know
   WHICH no, and two copies of one rule is the hand this project removes on sight (lesson 11).
   P97 (owner 334 a / 337 a): THREE answers now - the fourth, the no-map fallback that let slot 0 adopt after
   20 s, is RETIRED.  The answer is coopor::AnnounceArea (src/common/orphanpurge.h), a pure function of the
   area map's answer swept by the offline suite; with no fresh map it is "skip" on every game. */
enum { kAreaAdoptable = coopor::kAnnounceAdoptable, kAreaOtherHeld = coopor::kAnnounceOtherHeld, kAreaNoMap = coopor::kAnnounceNoMap };
static int AnnounceAreaDecide(float x, float z)
{
    return coopor::AnnounceArea(HeldByOtherTS(SectorOf(x, z)));
}
/* review-p8a M-4: the dead first read is gone.  `const int held = HeldByOtherTS(...)` was never used
   again - AnnounceAreaDecide reads it a second time - and HeldByOtherTS takes g_heldLock, so it was a
   locked read on the main thread once per character per tick for no result. */
static bool AnnounceAreaMine(float x, float z, bool count)   /* count: once per tick per character (review-p4q HIGH-3) */
{
    const int d = AnnounceAreaDecide(x, z);
    if (d == kAreaOtherHeld) { if (count) ++g_skippedOtherHeld; return false; }
    if (d == kAreaNoMap) { if (count) ++g_skippedNoMap; return false; }
    if (count) ++g_worldAdoptable; return true;
}


/* P8m (H-ghost) - RETIREMENT PRODUCES A WITHDRAWAL, AND THE WITHDRAWAL NO LONGER DEPENDS ON FINDING AN
   OBJECT THAT IS GONE.

   THE HOLE.  P034 retires a mirror when a character's handle stops resolving - normally because the
   holder's engine streamed the region out.  MirrorRetire (spawn.cpp:1239) sets `dead` in two local
   tables and SENDS NOTHING.  The only mechanism that ever withdrew an announcement was AnnouncePass,
   whose first step is `FindSpawned(uid)`, and FindSpawned is BUILT to answer 0 for a retired object
   (spawn.cpp:823).  So the pass whose job is "tell the peer to drop this character" skipped exactly
   the population that needed dropping.  T236d, game A: retired=205, despawnSent=90, unloadsSent=1,
   zero `-> UNLOAD` lines in the whole run - and game B kept 7 puppets of characters A's own engine had
   removed 0.56 s after adoption, standing there for the remaining 250 s of the run.  Decision 47 is
   convergent state: a puppet must not outlive the character it mirrors.

   THE REPAIR IS A REGISTRY, NOT A SCAN.  Retirement PUSHES the uid into `g_retireWithdraw`; the
   announce cadence drains it.  The drain asks nothing about the object - the object is gone, that is
   the premise - so it cannot be defeated by the lookup that caused the hole.  The decision itself is
   pure and lives in src/common/retirewithdraw.h, where the offline suite drives every transition.

   RECURRENCE-COVERED.  A pending uid stays pending until the send ACTUALLY happens: a transport that
   declines counts a deferral and leaves the entry owed.  Only three things end a pending entry - the
   send succeeded, the peer was never told about this uid (nothing to withdraw), or the character came
   back and the withdrawal is CANCELLED.  Retire and restore inside one tick are two steps over the
   same entry, so the drain sees the FINAL state and sends nothing.

   WHY THE ANNOUNCED CHECK USES E9'S CONSERVATIVE RULE.  Only an explicit `g_announced[uid] == 1` is
   withdrawn, exactly as WithdrawAnnouncedOnOwnUnload does: an absent or 0 entry means the peer very
   likely never heard of this uid, and T236d's `streamGateWithheld=1458` says that is the common case.
   Sending 205 UNLOADs for characters the stream gate had already withheld would be noise on the wire
   and a lie in the counters.  It also means a link drop is self-cleaning: WorldsyncPeerGone marks every
   uid NOT announced, so pending withdrawals for a peer that no longer exists resolve without a send.

   BOUNDED.  At most kMaxWithdrawPerPass sends per pass, and ONE log line per pass - never one per uid
   per tick.  A uid the cap defers is still pending and goes out on the next pass, 1 Hz later. */
static std::map<unsigned int, int> g_retireWithdraw;   /* uid -> cooprw state; entries only ever added by a retire */
long long g_rwSent = 0;                 /* UNLOADs this mechanism put on the wire */
long long g_rwDeferredLinkDown = 0;     /* ATTEMPTS the transport declined - one entry can produce many */
long long g_rwCancelledByRestore = 0;   /* the character came back before the send: nothing was sent */
long long g_rwNotAnnounced = 0;         /* the peer was never told about this uid: nothing to withdraw */
static const int kMaxWithdrawPerPass = 64;

/* THE ONE PLACE THE COUNTERS MOVE, so the identity below cannot drift from the transitions. */
static void RetireWithdrawCount(int count)
{
    if (count == cooprw::kCountSent) ++g_rwSent;
    else if (count == cooprw::kCountDeferredLinkDown) ++g_rwDeferredLinkDown;
    else if (count == cooprw::kCountCancelledByRestore) ++g_rwCancelledByRestore;
    else if (count == cooprw::kCountNotAnnounced) ++g_rwNotAnnounced;
}

/* Called from the retire and restore edges of ValidateAdopted.  MAIN THREAD (ValidateAdopted is). */
static void RetireWithdrawNote(unsigned int uid, int ev)
{
    if (uid == 0) return;
    std::map<unsigned int, int>::iterator it = g_retireWithdraw.find(uid);
    const int state = (it == g_retireWithdraw.end()) ? cooprw::kAbsent : it->second;
    int count = cooprw::kCountNone;
    const int next = cooprw::RetireWithdrawStep(state, ev, &count);
    RetireWithdrawCount(count);
    if (cooprw::RetireWithdrawKeep(next) == 0)
    {
        if (it != g_retireWithdraw.end()) g_retireWithdraw.erase(it);
        return;
    }
    if (it == g_retireWithdraw.end()) g_retireWithdraw[uid] = next;
    else it->second = next;
}

/* The GAUGE, not a total: how many uids are owed a withdrawal right now. */
static long long RetireWithdrawPending()
{
    long long n = 0;
    for (std::map<unsigned int, int>::const_iterator it = g_retireWithdraw.begin(); it != g_retireWithdraw.end(); ++it)
        if (cooprw::RetireWithdrawShouldSend(it->second)) ++n;
    return n;
}

/* M7a3f2 (T781): every UNLOAD this game sends, by why - the first 20 of each why logged, the rest counted (unloadSent[...] on the
   world-sync REPORT). MAIN THREAD. */
static long long g_unloadSentWhy[kUnloadWhyCount] = { 0, 0, 0, 0, 0 };
static void UnloadSentNote(unsigned int uid, int why)
{
    static const char* const kWhy[kUnloadWhyCount] = { "put-away", "reloaded", "held-settled", "retire", "announce" };
    if (why < 0 || why >= kUnloadWhyCount) why = kUnloadWhyPutAway;
    if (g_unloadSentWhy[why]++ < 20) DebugLog("[M1] -> UNLOAD uid=" + S(uid) + " why=" + kWhy[why] + " (the first 20 of each why are logged, the rest counted in unloadSent)");
}

/* THE DRAIN.  No event the drain applies can return kAbsent, so no entry is erased here and mutating
   `it->second` while iterating is safe. */
static void RetireWithdrawDrain()
{
    int sends = 0, sent = 0, resolved = 0;
    unsigned int firstUid = 0;
    for (std::map<unsigned int, int>::iterator it = g_retireWithdraw.begin(); it != g_retireWithdraw.end(); ++it)
    {
        if (cooprw::RetireWithdrawShouldSend(it->second) == 0) continue;
        const unsigned int uid = it->first;
        int count = cooprw::kCountNone;
        std::map<unsigned int, int>::iterator a = g_announced.find(uid);
        if (a == g_announced.end() || a->second != 1)
        {
            it->second = cooprw::RetireWithdrawStep(it->second, cooprw::kEvNotAnnounced, &count);
            RetireWithdrawCount(count);
            ++resolved;
            continue;
        }
        if (ReleasePendingHas(uid)) continue;   /* M7a3 P3: HELD while its squad's hand-over is owed - still pending here */
        if (sends >= kMaxWithdrawPerPass) continue;   /* still pending, still owed - next pass */
        ++sends;
        if (net::SendUnload(uid, 0))   /* retired: this game runs it nowhere */
        {
            it->second = cooprw::RetireWithdrawStep(it->second, cooprw::kEvSent, &count);
            RetireWithdrawCount(count);
            a->second = 0; ++g_unloadsSent; UnloadSentNote(uid, kUnloadWhyRetire);   /* M7a3f2 */
            if (firstUid == 0) firstUid = uid;
            ++sent; ++resolved;
        }
        else
        {
            it->second = cooprw::RetireWithdrawStep(it->second, cooprw::kEvSendFailed, &count);
            RetireWithdrawCount(count);
        }
    }
    if (sent > 0)
        DebugLog("[P034] retire-withdraw: sent " + S((long long)sent) + " UNLOAD(s) for characters this"
                 " engine no longer has, first uid=" + S(firstUid) + "; " + S(RetireWithdrawPending())
                 + " still pending (P8m)");
    else if (resolved > 0 && (g_announceTicks % 60) == 0)
        DebugLog("[P034] retire-withdraw: " + S((long long)resolved) + " retired uid(s) needed no UNLOAD"
                 " (the peer was never told about them); " + S(RetireWithdrawPending()) + " pending (P8m)");
}

bool AnnouncedToPeerQuiet(unsigned int uid)
{
    std::map<unsigned int, int>::const_iterator it = g_announced.find(uid);
    return it == g_announced.end() || it->second == 1;
}

bool AnnouncedToPeer(unsigned int uid)
{
    std::map<unsigned int, int>::const_iterator it = g_announced.find(uid);
    if (it == g_announced.end() || it->second == 1) return true;
    ++g_streamGateWithheld;
    return false;
}

// E9 - called from spawn.cpp's NotifyDespawn on an unload-destroy, while the uid is still in hand. Deliberately
// conservative about an ABSENT map entry: AnnouncePass reads absent as "announced" for rows that predate the gate, but
// here an absent entry would mean sending the peer an UNLOAD for a uid it was very likely never told about, so only an
// explicit 1 is withdrawn. Every adoption writes an entry either way (worldsync.cpp:714 / :729).
bool WithdrawAnnouncedOnOwnUnload(unsigned int uid, int why)
{
    std::map<unsigned int, int>::iterator it = g_announced.find(uid);
    if (it == g_announced.end() || it->second != 1) return false;
    if (ReleasePendingHas(uid)) return true;   /* M7a3 P3: HELD - its squad's hand-over is owed; the owed outcome decides */
    if (!net::SendUnload(uid, why == kUnloadWhyAnnounce ? 1 : 0)) return false;
    it->second = 0; ++g_unloadsSent; ++g_withdrawnOwnUnload;
    UnloadSentNote(uid, why);   /* M7a3f2 */
    return true;
}

/* inv7e2 (T418): uid -> the WORLD-LOAD generation (fold, review-inv7e2 HIGH) in which THIS game's own sweep adopted it
   from its own loaded world. Cleared at world teardown. */
static std::map<unsigned int, long> g_sweepAdoptGen;
int SweepAdoptGen(unsigned int uid, long* gen)
{
    std::map<unsigned int, long>::const_iterator it = g_sweepAdoptGen.find(uid);
    if (it == g_sweepAdoptGen.end()) { *gen = -1; return 0; }
    *gen = it->second; return 1;
}
void ForgetSweepAdopt(unsigned int uid) { g_sweepAdoptGen.erase(uid); }
bool AnnouncedExplicit(unsigned int uid)
{
    std::map<unsigned int, int>::const_iterator it = g_announced.find(uid);
    return it != g_announced.end() && it->second == 1;
}
/* M7a3f3 H1 [m7a3f3-ws1]: only an EXPLICIT 0 is "withheld"; the take path (handoff.cpp TakeLocalOwner) writes no entry */
int AnnouncedState(unsigned int uid)
{
    std::map<unsigned int, int>::const_iterator it = g_announced.find(uid);
    if (it == g_announced.end()) return coopsquad::kAnnNoEntry;
    return it->second == 0 ? coopsquad::kAnnWithheld : coopsquad::kAnnAnnounced;
}
/* M7a3f3 [m7a3f3-mg0] (fold-3 leftover 4, manager): a character TAKEN by hand-over - the giver keeps it as its puppet, so the
   other games WERE told about it. Recorded as announced, so this game's own unload or put-away of it withdraws the giver's copy
   (E9's WithdrawAnnouncedOnOwnUnload sends only for an explicit 1) instead of leaving it there for good. */
void NoteAnnouncedOnTake(unsigned int uid)
{
    if (uid != 0) g_announced[uid] = 1;
}
int WithdrawReloadedCopy(unsigned int uid, bool wasAnnounced)
{
    g_sweepAdoptGen.erase(uid);
    for (int i = 0; i < g_adoptedCount; ++i)
        if (g_adoptedRows[i].uid == uid && !g_adoptedRows[i].permanent)
        {
            if (g_adoptedRows[i].obj != 0) MirrorRetire(g_adoptedRows[i].obj);   /* address compare only */
            g_adoptedRows[i].retired = true; g_adoptedRows[i].permanent = true;
        }
    if (!wasAnnounced) return 0;
    if (!AnnouncedExplicit(uid)) return 1;               /* E9's unload-destroy path already sent the UNLOAD */
    if (WithdrawAnnouncedOnOwnUnload(uid, kUnloadWhyReloaded)) return 1;
    RetireWithdrawNote(uid, cooprw::kEvRetire);         /* declined: owed - the P8m drain sends it */
    return 2;
}

/* M7a3 P3: the owed hand-over was dropped because no game holds the sector - the UNLOAD it held goes now; a send that fails is
   owed to the P8m drain (its second chance), exactly as a declined own-unload withdrawal. */
bool WithdrawHeldUnload(unsigned int uid)
{
    if (WithdrawAnnouncedOnOwnUnload(uid, kUnloadWhyHeldSettled)) return true;
    if (AnnouncedExplicit(uid)) RetireWithdrawNote(uid, cooprw::kEvRetire);
    return false;
}

// M-A step 2 - once a second: a character announced into a sector the peer no longer holds is UNLOADED on the
// peer; one withheld whose sector the peer now holds is announced (CONTEXT + SPAWN).
static void AnnouncePass()
{
    ++g_announceTicks;
    /* P8m - BEFORE the freshness guard below.  A retired character is gone whoever holds its sector, so
       the withdrawal owes nothing to the peer-zone clock, and routing it through a gate that returns
       early would be exactly the "consumed by a gate deferral" failure this registry exists to avoid. */
    RetireWithdrawDrain();
    if (!RelayMapFresh()) { ++g_announceStale; return; }   /* H030 (F445): a silent peer has dropped NOTHING - judge only a fresh set */
    /* review-p5b HIGH-1: the guard reads the clock the ANSWER comes from - PeerHoldsSectorOf -> OtherHasSector answers
       from the notebook's loaded-by map, and that map has its own clock.
       M2 (decisions 32/44/54; manager ruling 2026-09-22) - THE FREEZE RULE. The session-link ZONES/SECTORMAP arm
       (PeerZonesFresh) is gone, so while the NOTEBOOK link is down mid-session (T240/T241's kill-and-restart) there
       is NO live source of who has which area loaded. The pass then neither withdraws nor re-announces anything: it
       returns here and each frozen pass is counted announceStale; the W1-b effective map is carried unchanged; the
       writer ladder answers NO-ANSWER for anything it cannot decide without the notebook. */
    for (int i = 0; i < g_adoptedCount; ++i)
    {
        const unsigned int uid = g_adoptedRows[i].uid;
        if (!net::IsUidMine(uid)) continue;                 // handed off: not ours to announce
        /* M4 fold 2 (re-check L-C): a hand-over of this uid is in flight (XFER sent, no ACK yet). The receiver may already have
           taken it, so a SPAWN (or UNLOAD) now would reach a game that runs it - its own-uid SPAWN refusal (session.cpp) is the
           backstop, not the rule. Skipped and counted; the next pass after the ACK or the abandon decides. */
        if (HandoffPendingHas(uid)) { ++g_announceSkippedHandover; continue; }
        ::Character* c = FindSpawned(uid);
        if (!PlausibleObject(c) || !PlausibleObject(*(void**)((char*)c + 0x448))) continue;
        Ogre::Vector3 pos = c->worldPosition();
        const bool held = PeerHoldsSectorOf(pos);
        /* W1-b (T240; review 2026-09-22 H2/H3): with the relay present `held` reads the EFFECTIVE area map (zones.cpp
           ApplyRelayAreaMap -> g_otherLoaded), in which a reporter absent from the latest AREAMAP keeps its last known
           areas - so an absent reporter never reads as holding nothing, and a present one that walked away reads exactly
           as it reported. The withdraw leg below is therefore the plain per-row test again; there is no drop queue. */
        /* E9 / decision 35 amended (F524): "the peer still holds the sector" was the ONLY reason this pass ever kept a
           character announced, and it is not sufficient - a character standing in a sector I have unloaded is one I no
           longer drive, whoever else holds it. SectorLoadedHere reads g_loaded, the same set the relay is told about
           (zones.cpp:421), so the peer is being told to drop exactly what I have stopped reporting. */
        /* E15 (review-p5h HIGH-1, T221): both legs now ask the ENGINE whether this game has that position loaded -
           coop::IsPositionLoadedHere - instead of the cached g_loaded set. g_loaded is a 7x7 ring probed at sector
           CENTRES and refreshed at most once a second, and it lagged a teleport by ~19 s in T219; a character of mine
           standing just outside it read !mineLoaded while the peer still held its sector, so the withdraw leg fired
           and the re-announce leg (which did not test mineLoaded at all) undid it on the next pass - 26 SPAWNs and 26
           UNLOADs per uid in T221. The two legs now use the SAME predicate, read live. */
        const bool mineLoaded = coop::IsPositionLoadedHere(pos.x, pos.y, pos.z);
        std::map<unsigned int, int>::iterator it = g_announced.find(uid);
        const int state = (it == g_announced.end()) ? 1 : it->second;   // rows adopted before this code: assume announced
        /* M7a: the pass's rule is cooplive::AnnounceDecide with no reporter gained (the catch-up is the one caller that has one) */
        const int ann = cooplive::AnnounceDecide(held, mineLoaded, state == 1, false);
        if (ann == cooplive::kAnnUnload) { if (ReleasePendingHas(uid)) { /* M7a3f1 #8: HELD - its squad's hand-over is owed; the owed outcome decides */ } else if (net::SendUnload(uid, 1)) { g_announced[uid] = 0; ++g_unloadsSent; UnloadSentNote(uid, kUnloadWhyAnnounce); if (held) ++g_withdrawnOwnUnload; } }   /* held == true here means !mineLoaded is the reason, i.e. this is a withdrawal the old rule would NOT have made */
        else if (ann == cooplive::kAnnSpawn)
        {
            GameData* gd = c->getRecordDirect(); Faction* f = c->getOwnerFactionDirect();
            if (!PlausibleObject(gd) || !PlausibleObject(f)) continue;
            SendContextFor(uid, c);
            if (net::SendSpawn(uid, gd->name, pos.x, pos.y, pos.z, WireFactionName(f), false, c)) { WatchLocalRoll(uid); g_announced[uid] = 1; ++g_reannouncedOnReload; }
        }
    }
}

/* stand1 fold (review-stand1 1d): this game's slot has just arrived - every row goes again through the link-up edge's own path; a
   SPAWN held for the slot left its row unannounced, so AnnouncePass re-sends it as well */
void WorldSyncReannounceAll() { if (g_adoptedCount > 0) g_reannounceAt = 0; DebugLog("[P033] re-announcing " + S(g_adoptedCount) + " rows: this game's slot is now known (stand1 fold)"); }

void WorldSyncTick()
{
    { static long long s_t = 0; if (++s_t % 118 == 0 && g_mode != 0) AnnouncePass(); }   // M-A step 2, 1 Hz
    // Link-up EDGE, checked before anything else returns early - a re-announce is needed whether
    // or not adoption is currently on, and whether or not this instance is still sweeping.
    bool up = net::LinkIsUp();
    if (up && !g_linkWasUp) coop::ZonesDisarmDropOnAbsence();   /* W1-c: the peer is back, so its next absence from a notebook map is carried, not dropped */
    if (up && !g_linkWasUp && g_adoptedCount > 0)
    {
        g_reannounceAt = 0;
        DebugLog("[P033] link came up with " + S(g_adoptedCount) + " characters already adopted -"
                 " re-announcing them to the peer. Without this the peer never hears about anything"
                 " adopted before it joined.");
    }
    g_linkWasUp = up;
    {   /* M7a fold (review 2026-09-30 F3): what was in flight on the notebook road when THIS game's notebook link went down is lost -
           the stream now takes the session link, so everything is announced to the session peer again, as at its link-up edge */
        static bool s_liveWas = false;
        const bool liveNow = StoreLiveReady();
        if (s_liveWas && !liveNow && up && g_adoptedCount > 0)
        {
            g_reannounceAt = 0;
            DebugLog("[P033] this game's world-server link went down - re-announcing " + S(g_adoptedCount) + " characters to the session peer (M7a fold F3)");
        }
        s_liveWas = liveNow;
    }

    DeriveRadiusFromViewDistance();

    // P034 runs whatever the adoption mode is - the hazard is about characters ALREADY adopted, so
    // switching adoption off must not switch the detector off with it.
    ValidateAdopted(2);

    // THROTTLE THE WHOLE SWEEP. The pump runs ~900x/s and the walk below is O(characters) with a
    // FindSpawnedUid on each - at ~130 characters that is ~234,000 lookups per second, forever,
    // including long after the sweep has finished and there is nothing left to adopt. The engine's
    // own population top-up runs every 2 SECONDS (F276), so anything faster than a few times a
    // second is measuring a world that cannot have changed.
    //
    // At 1 in 30 ticks the sweep still runs ~30x/s and still adopts up to 60 characters/s, so a
    // full ~130-character world fills in about two seconds - while the idle cost drops thirtyfold.
    ++g_tick;
    if (g_tick % kSweepEveryTicks != 0) return;

    // Drain the re-announce queue at the same throttle as adoption, and for the same reason: it is
    // the same ~1.5 KB of appearance per character and the same construction work on the peer.
    /* P8e (audit C6, F669): WHICHEVER GAME ANNOUNCED THE ROWS RE-ANNOUNCES THEM.  g_adoptedRows is this
       game's own list of what it adopted and told the peer about; nothing about draining it needs a
       role, and while the drain was host-only a CLIENT-generated town's residents were announced once
       and never again - F630's shape, a role rule surviving a holder rule, one layer down. */
    if (g_reannounceAt >= 0 && net::SideRoadOpen())   /* a road to the other players is open: the world server's, or the old link */
    {
        for (int k = 0; k < kAdoptPerTick && g_reannounceAt < g_adoptedCount; ++k)
        {
            Adopted& row = g_adoptedRows[g_reannounceAt++];
            ++g_reannounceDrained;   /* P8e: rows WALKED, so a zero can be told from an empty queue */
            /* announce1 (review-stale1 5): the table holds rows for the PEER's characters too (TrackForLiveness runs for
               every copy we create) and, since stale1, superseded rows. Only a character THIS game drives is announced -
               the same test AnnouncePass makes - so a reconnect never SPAWNs the peer's own uids back to it (its OnSpawn
               would record us as their owner) or marks them announced here (a later retire would send it an UNLOAD). */
            if (row.permanent || !net::IsUidMine(row.uid)) { ++g_reannounceSkippedNotMine; continue; }
            ::Character* c = FindSpawned(row.uid);   // returns 0 for a RETIRED entry, which is what
                                                     // we want: never re-announce a dead pointer
            if (!PlausibleObject(c)) continue;
            GameData* gd = c->getRecordDirect();
            Faction*  f  = c->getOwnerFactionDirect();
            if (!PlausibleObject(gd) || !PlausibleObject(f)) continue;
            if (!PlausibleObject(*(void**)((char*)c + 0x448))) continue;
            Ogre::Vector3 pos = c->worldPosition();
        // keepContainer = FALSE for world population, and the reason is a user-visible defect.
        //
        // `CreateAt` resolves the container as the WATCHED PLAYER'S PLATOON (`ref->getSquad()`),
        // so `keepContainer=true` puts every adopted world character into the client player's own
        // SQUAD. The user saw it directly: "the client having characters added ... they're also
        // populating the client's squad list." A Dust Bandit is not in your squad; a UI that says
        // it is, is wrong.
        //
        // `true` was inherited from the hand-spawn path, where it is correct - those spawns are
        // deliberately squadmates. For world population it never was. F103/T029's warning about the
        // container-less variant wandering 6,296 units applies to an UNDRIVEN spawn; these are
        // adopted as suppressed puppets and driven every tick, and the drift they do show is
        // already explained by adoption distance (F294), not by their container.
            if (coopslot::WireLacksSlot(WireFactionName(f))) { coop::NoteHeldForSlot(); g_announced[row.uid] = 0; continue; }   /* stand1 fold (1d): held, not dropped */
            if (!PeerHoldsSectorOf(pos)) { if (RelayMapFresh()) g_announced[row.uid] = 0; ++g_withheld; continue; }   /* M7a3 P3: with no fresh area map nobody can say the peer lacks it - the entry is left as it was */   // M-A step 2
            SendContextFor(row.uid, c);   // M-B: CONTEXT before SPAWN, same reliable channel, so the copy is created with it
            if (net::SendSpawn(row.uid, gd->name, pos.x, pos.y, pos.z, WireFactionName(f), false, c))
            {
                WatchLocalRoll(row.uid);
                ++g_reannounced; g_announced[row.uid] = 1;
            }
        }
        if (g_reannounceAt >= g_adoptedCount)
        {
            g_reannounceAt = -1;
            DebugLog("[P033] re-announce complete: " + S(g_reannounced) + " sent in total.");
        }
    }

    if (g_mode == kModeOff) return;
    if (g_mode == kModeOnce && g_budget <= 0) return;

    // Adopting with no other player in the world would mint uids and register characters whose SPAWN goes nowhere. Refuse
    // instead, and let the sweep run when another player is here (the old link up, or another slot IN_WORLD on the roster); a
    // player who arrives later asks for what it lacks by the catch-up.
    if (!coop::PlayersPresent()) return;

    // ONLY THE HOST ADOPTS. Option A has exactly one canonical world, and it is the host's; the
    // client's own population is suppressed at the leaf precisely so it can receive that one.
    //
    // Without this, `worldsync on` sent to a client would have BOTH sides adopt their own world
    // characters and send them to each other - each peer building the other's crowd on top of its
    // own, with no overlap between the two sets because the handles differ (T078/F265 measured only
    // 12 of ~110 shared). The result is roughly double population on both machines and worse parity
    // than doing nothing. Nothing in the command surface prevented that; the command is per-instance
    // and the tests only ever sent it to the host.
    //
    // Refused once, loudly, rather than silently no-oping: a run that sent this to the wrong
    // instance and saw nothing happen would look exactly like a broken sweep.
    // P3 (decision 23, T157/F474): the world is the host's (Option A) - the client adopts NONE of it. But with each player on
    // their own save, the client's OWN player squad exists only on the client, and nobody else can announce it: the client
    // sweeps for its player-faction characters only. Everything else below is unchanged.
    // decision 33 (2026-09-03, F513) replaces P3's player-only client: every game announces its player-faction characters AND the
    // world characters standing in areas it holds; the peer's mirrors are already excluded by FindSpawnedUid below.
    const bool isHost = net::SessionIsHost();
    if (!isHost && !g_refusedClient)
    {
        g_refusedClient = true;
        DebugLog("[P033] sweep: player-faction characters + world characters in areas this game holds (decision 33)");
    }

    if (!PlausiblePtr(coop::GameWorldPtr())) return;
    /* M4 fold L3 (review 2026-09-29): no uid can be minted this tick - not WELCOMEd on this link, no uid seat / block yet, or the
       seat's counters spent with no free seat left. Counted ONCE here for the tick (uidNoSlotDeferred / uidNoBlockDeferred on the [M1] REPORT), and
       no character is searched for; a later tick retries. */
    /* M4 fold 2 (re-check L-A): a tick that cannot mint still walks the candidates - the orphan notes and the stand-in purge
       requests of pass 0 are not about minting - and stops only before the registration of `best` (below). */
    const bool uidMintable = coop::SpawnUidMintReady();

    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    size_t n = all.size();
    if (n > 20000) return;   // same refusal bound the census uses - do not walk an implausible list

    // Our own viewpoint, so the sweep can take the nearest characters first. Falls back to no
    // ordering if we have nothing placed yet, which is the correct behaviour rather than an
    // arbitrary origin: at that point every character is equally (un)important.
    ::Character* ref = GetTarget();
    bool haveRef = PlausibleObject(ref);
    Ogre::Vector3 eye(0, 0, 0);
    if (haveRef) eye = ref->worldPosition();

    // PROBE-START: P021 - every 10 s: the viewpoint's own sector as the grid sees it (T209: the client refused in its OWN town?)
    {
        ++g_p21Ticks; static double lastEye = 0.0; const double nowE = (double)::GetTickCount() / 1000.0;
        if (haveRef && nowE - lastEye >= 10.0)
        {
            lastEye = nowE; const Sector se = SectorOf(eye.x, eye.z); g_p22EyeMine = (HeldByOtherTS(se) == 0);   /* P022 */
            DebugLog("[PROBE] P021 eye sector " + S((long long)se.x) + "," + S((long long)se.y) + " held=" + S((long long)HeldByOtherTS(se)) + " listSize=" + S((long long)n)
                     + " seen/spawned/guard/rule/radius=" + S(g_p21Seen) + "/" + S(g_p21Spawned) + "/" + S(g_p21Guard) + "/" + S(g_p21Rule) + "/" + S(g_p21Radius));
        }
    }
    // PROBE-END: P021
    std::map<const void*, int> standInByFaction;   /* area2 fold: faction pointer -> stand-in by record, for this sweep only */
    std::map<const void*, int> refusedThisTick;    /* mirror1 fold (review-mirror1 #3): registration refused this tick - not re-selected before the next */
    int done = 0;
    for (int pass = 0; pass < kAdoptPerTick; ++pass)
    {
        if (g_mode == kModeOnce && g_budget <= 0) break;

        // Pick the NEAREST un-adopted character, not the first one the iterator offers. Allocation
        // order is meaningless to a player; distance is not. If the sweep is interrupted or the
        // budget is small, what the peer has is the crowd the player is standing in.
        ::Character* best = 0;
        double bestD2 = 0.0;

        for (GameHashSet< ::Character*>::type::const_iterator it = all.begin();
             it != all.end(); ++it)
        {
            ::Character* c = *it;
            if (!PlausibleObject(c)) continue;
            if (!refusedThisTick.empty() && refusedThisTick.count((const void*)c) != 0) continue;   /* mirror1 fold #3 */
            ++g_p21Seen;   /* P021 */
            {
                const unsigned int su = FindSpawnedUid(c);
                if (su != 0)
                {
                    ++g_p21Spawned;
                    // PROBE-START: P022
                    if (g_p22EyeMine && pass == 0)
                    {
                        const bool agrees = (const void*)FindSpawned(su) == (const void*)c;
                        if (agrees) ++g_p22Real; else ++g_p22Stale;
                        if (g_p22Logged < 8 && PlausibleObject(*(void**)((char*)c + 0x448)))
                        {
                            ++g_p22Logged; ::Faction* pf = c->getOwnerFactionDirect();
                            DebugLog("[PROBE] P022 spawned-hit: uid=" + S((long long)su) + " forwardMapAgrees=" + S((long long)(agrees ? 1 : 0)) + " mine=" + S((long long)(net::IsUidMine(su) ? 1 : 0))
                                     + " player=" + S((long long)(IsPlayerFaction(pf) ? 1 : 0)) + " faction='" + (PlausibleObject(pf) ? WireFactionName(pf) : std::string("?")) + "'");
                        }
                    }
                    // PROBE-END: P022
                    continue;   // already ours - includes everything adopted
                }
            }
                                                    // on previous ticks and our own spawns

            /* inv7a-b (Read 2026-09-27): A STAND-IN COPY IS NEVER THIS GAME'S OWN. The purge's fallbacks can run before a woken
               stand-in squad's members exist; adopting such a copy announced it as ours, and the purge then took it for a live
               copy (known) and skipped it for good. The purge's own record test (the stand-in table is empty after a load). */
            {
                /* area2 fold (review-area2 LOW): a guarded faction read, and the record answer cached per faction pointer for this sweep */
                ::Faction* sf = SweepFactionPod(c);
                int standIn = 0;
                std::map<const void*, int>::const_iterator fc = standInByFaction.find((const void*)sf);
                if (fc != standInByFaction.end()) standIn = fc->second;
                else { standIn = (PlausibleObject(sf) && coopsp::IsStandInRecord(StandInRecordSlot(sf)) != 0) ? 1 : 0; standInByFaction[(const void*)sf] = standIn; }
                if (standIn != 0)
                {
                    if (pass == 0) { ++g_sweepStandInSkipped; StandinPurgeRequest((const void*)c); }
                    continue;
                }
            }
            // worldPosition dereferences the animation object (F205), so guard it the way the
            // census and roster do rather than trusting the character pointer alone.
            if (!PlausibleObject(*(void**)((char*)c + 0x448))) { ++g_p21Guard; continue; }
            Ogre::Vector3 p = c->worldPosition();
            /* T-1 B1 (owner 104): a non-player character in a squad is decided by its SQUAD position - the engine sleeps and
               wakes the squad whole by it (its leader's) - so both games put the whole squad on the same side. dp = the
               position that decides (the area rule, the orphan note and the adoption radius); p stays the character's own. */
            const bool sweepPlayerF = IsPlayerFaction(c->getOwnerFactionDirect());
            Ogre::Vector3 dp = p;
            if (!sweepPlayerF) { float sqx = 0, sqz = 0; if (SquadDecisionPos(c, &sqx, &sqz) == 1) { dp.x = sqx; dp.z = sqz; } }
            /* a rebuilt person of a CONTEXT platoon (built here for another game's announced squad; its people arrive by SPAWN) is never
               adopted: a body in it that is not a registered copy was rebuilt by this game's engine from the platoon's sleeping data, and
               adopting it would SPAWN a second copy of that game's person under a fresh uid. Noted for the orphan clean-up; the refused
               wake's drop (store.cpp detour_activate) removes the platoon. */
            if (!sweepPlayerF)
            {
                SquadView csv;
                if (ReadSquadOf(c, &csv) == 1 && IsContextPlatoon(csv.platoon) == 1)
                {
                    if (pass == 0)
                    {
                        ++g_sweepContextSkipped;
                        if (g_sweepContextLogged < 8)
                        {
                            ++g_sweepContextLogged;
                            const Sector cs = SectorOf(dp.x, dp.z);
                            DebugLog("[P033] sweep: NOT adopted - a rebuilt person of a context platoon (another game's squad, sector " + S((long long)cs.x) + "," + S((long long)cs.y)
                                     + "); noted for the orphan clean-up (sweepContextSkipped=" + S(g_sweepContextSkipped) + "; the first 8 logged)");
                        }
                        OrphanNote((void*)c, dp.x, dp.z);
                    }
                    continue;
                }
            }
            /* M7a3f3 T-425 [m7a3f3-ws3]: a rebuilt person of a squad this game HANDED to another game (its own world data still holds the squad;
               a walk-back, a wake or an in-ring put-away re-creates it) is not adopted while that game runs a live copy of the squad here or
               the area is not known to be this game's alone (HeldByOtherTS 1 or -1) - adopting it SPAWNed a fresh uid to the other game,
               which then showed two. Noted for the orphan clean-up instead. A fresh map with no other holder and no live copy here: adopted,
               and the squad is this game's again (unmarked). */
            if (!sweepPlayerF && (HandedOverAny() || HandedOverLeftBehindAny()))   /* M7a3f4 [m7a3f4-ws0] */
            {
                void* hf = 0; std::string hid;
                const bool hkey = SquadKeyOf(c, &hf, &hid);
                if (hkey) HandedOverLeftBehindRebuilt(hf, hid);   /* M7a3f4 [m7a3f4-ws1]: a left-behind person of the squad is live here again - its note (and mark) go */
                if (hkey && HandedOverHas(hf, hid))
                {
                    const Sector hs = SectorOf(dp.x, dp.z);
                    const int hact = coopsquad::HandedOverSweepAction(1, CtxPlatoonLiveForId(hf, hid), HeldByOtherTS(hs));
                    if (hact == coopsquad::kHoSweepSkip)
                    {
                        if (pass == 0)
                        {
                            ++g_sweepHandedOverSkipped;
                            if (g_sweepHandedOverLogged < 8)
                            {
                                ++g_sweepHandedOverLogged;
                                DebugLog("[P033] sweep: NOT adopted - a rebuilt person of squad id='" + hid + "', which this game handed to another game (sector "
                                         + S((long long)hs.x) + "," + S((long long)hs.y) + "); noted for the orphan clean-up (sweepHandedOverSkipped=" + S(g_sweepHandedOverSkipped) + "; the first 8 logged)");
                            }
                            OrphanNote((void*)c, dp.x, dp.z);
                        }
                        continue;
                    }
                    ++g_sweepHandedOverAdopted;
                    HandedOverUnmark(hf, hid, 1);
                }
            }
            if (!sweepPlayerF && !AnnounceAreaMine(dp.x, dp.z, pass == 0))
            {
                ++g_p21Rule;
                // PROBE-START: P021 - the first 8 refusals: where, and what the grid says
                if (g_p21Logged < 8)
                {
                    ++g_p21Logged; const Sector sc = SectorOf(dp.x, dp.z);
                    DebugLog("[PROBE] P021 refused: sector " + S((long long)sc.x) + "," + S((long long)sc.y) + " held=" + S((long long)HeldByOtherTS(sc))
                             + " isHost=" + S((long long)(isHost ? 1 : 0)) + " pos=" + S((long long)dp.x) + "," + S((long long)dp.z));
                }
                // PROBE-END: P021
                /* orphan1 (decision 37): unregistered (FindSpawnedUid == 0 above), not player-faction, and in an area ANOTHER
                   game holds - created here while unlinked, and nobody will announce it. Noted for OrphanPurgeTick. */
                if (pass == 0 && HeldByOtherTS(SectorOf(dp.x, dp.z)) == 1) OrphanNote((void*)c, dp.x, dp.z);
                continue;   // decision 33
            }
            if (!haveRef) { best = c; break; }
            double dx = dp.x - eye.x, dz = dp.z - eye.z;   /* T-1 B1: the deciding position */
            double d2 = dx * dx + dz * dz;
            // Outside the radius: not adopted, and counted so the omission is a NUMBER rather than
            // a silence. A world we deliberately do not replicate must say how much it left out.
            if (g_adoptRadius > 0.0f && d2 > (double)g_adoptRadius * (double)g_adoptRadius) { ++g_p21Radius; continue; }   /* P021 */
            if (best == 0 || d2 < bestD2) { best = c; bestD2 = d2; }
        }

        if (best == 0)
        {
            // Recount what the radius excluded, so `skippedTooFar` reflects the world as it is now
            // rather than accumulating across passes.
            if (g_adoptRadius > 0.0f && haveRef)
            {
                long long tooFar = 0;
                for (GameHashSet< ::Character*>::type::const_iterator it2 = all.begin();
                     it2 != all.end(); ++it2)
                {
                    ::Character* c2 = *it2;
                    if (!PlausibleObject(c2) || FindSpawnedUid(c2) != 0) continue;
                    if (!PlausibleObject(*(void**)((char*)c2 + 0x448))) continue;
                    Ogre::Vector3 p2 = c2->worldPosition();
                    const bool p2Player = IsPlayerFaction(c2->getOwnerFactionDirect());
                    if (!p2Player) { float sqx = 0, sqz = 0; if (SquadDecisionPos(c2, &sqx, &sqz) == 1) { p2.x = sqx; p2.z = sqz; } }   /* T-1 B1: the squad position decides */
                    if (!p2Player && !AnnounceAreaMine(p2.x, p2.z, false)) continue;   // decision 33
                    double ddx = p2.x - eye.x, ddz = p2.z - eye.z;
                    if (ddx * ddx + ddz * ddz > (double)g_adoptRadius * (double)g_adoptRadius) ++tooFar;
                }
                g_skippedTooFar = tooFar;
            }
            // Nothing left. Say so ONCE - a sweep that has finished and one that never started
            // look identical in a log otherwise, and this runs every tick.
            if (!g_sweepComplete)
            {
                g_sweepComplete = true;
                DebugLog("[P033] sweep complete - no un-adopted characters remain. adopted="
                         + S(g_adopted));
            }
            return;
        }

        if (!uidMintable) return;   /* M4 fold 2 (L-A): no uid this tick - `best` is not registered; a later tick retries */

        // Read everything the spawn path needs BEFORE minting a uid. A character that cannot
        // supply a template or a faction must not consume a uid or a mirror slot - the peer could
        // not build it anyway, and a half-registered character would stream position updates for
        // an object that does not exist on the other side.
        GameData* gd = best->getRecordDirect();
        if (!PlausibleObject(gd)) { ++g_noTemplate; ++g_unreadable; continue; }
        std::string templateName = gd->name;
        if (templateName.empty()) { ++g_noTemplate; continue; }

        Faction* f = best->getOwnerFactionDirect();
        if (!PlausibleObject(f)) { ++g_noFaction; continue; }
        std::string factionName = WireFactionName(f);   // P3: "@player:<name>" for our own player faction

        if (!PlausibleObject(*(void**)((char*)best + 0x448))) { ++g_unreadable; continue; }
        Ogre::Vector3 pos = best->worldPosition();

        bool tablesFull = false, noUid = false;
        unsigned int uid = AdoptExisting(best, &tablesFull, &noUid);
        /* M4 (owner 203, 2026-09-29): no uid could be minted - this game has no notebook slot yet, or its counter is spent
           (both counted in AllocateUid). No other character can be adopted this tick either; `best` is not marked, the sweep
           is not called complete, and a later tick retries it. */
        if (uid == 0 && noUid) return;
        if (uid == 0 && !tablesFull)
        {
            // "Already ours", or a retired entry that was restored because the character came back.
            // Ordinary - skip it and carry on. Conflating this with a table-full refusal is what let
            // one region stream-out shut the sweep off for a session with a false reason.
            ++g_skippedOurs;
            continue;
        }
        if (uid == 0)
        {
            // AdoptExisting returns 0 for two different reasons and they need different responses:
            // "already ours" is ordinary (skipped above), and a registration REFUSAL is handled here.
            // mirror1 fold (review-mirror1 #3): a refusal used to switch world adoption OFF for the session,
            // on the premise that full stays full - which mirror1 made false (rows are released). Now the
            // character is skipped (counted) and adoption goes on. coopuid::SweepRefusalAction: no free row
            // -> end THIS tick (nothing else can register before a row frees; the next tick retries); a free
            // row (refused for another reason) -> skip only this character for the rest of the tick, so the
            // same character is not re-selected pass after pass.
            ++g_registerFailed;
            const int hasRoom = UidTableHasRoom() ? 1 : 0;
            if (coopuid::LogRefusal(g_registerFailed))
                ErrorLog("[P033] adoption REFUSED for one character ("
                         + std::string(hasRoom ? "registration did not take" : "uid table full")
                         + ") - skipped; adoption continues and retries it on a later tick. registerFailed="
                         + S(g_registerFailed) + " adopted=" + S(g_adopted)
                         + ". Logged for the first 5 and every 100th refusal.");
            if (coopuid::SweepRefusalAction(hasRoom) == coopuid::kSweepEndTick) return;
            refusedThisTick[(const void*)best] = 1;
            continue;
        }

        net::SetLocalOwner(uid);
        g_sweepAdoptGen[uid] = StoreWorldGenNow();   /* inv7e2 fold: picked up by THIS game's sweep from its own world - stamped with the world-load generation */

        // keepContainer = true: F103/T029 measured the null-container variant running away. The
        // world character already belongs to a real squad on this side, and the peer builds its
        // own copy into one - matching the behaviour hand-spawned characters have always had.
        // keepContainer = FALSE for world population, and the reason is a user-visible defect.
        //
        // `CreateAt` resolves the container as the WATCHED PLAYER'S PLATOON (`ref->getSquad()`),
        // so `keepContainer=true` puts every adopted world character into the client player's own
        // SQUAD. The user saw it directly: "the client having characters added ... they're also
        // populating the client's squad list." A Dust Bandit is not in your squad; a UI that says
        // it is, is wrong.
        //
        // `true` was inherited from the hand-spawn path, where it is correct - those spawns are
        // deliberately squadmates. For world population it never was. F103/T029's warning about the
        // container-less variant wandering 6,296 units applies to an UNDRIVEN spawn; these are
        // adopted as suppressed puppets and driven every tick, and the drift they do show is
        // already explained by adoption distance (F294), not by their container.
        const bool slotHeld = coopslot::WireLacksSlot(factionName);   /* stand1 fold (review-stand1 1d): my faction has no slot yet - HELD: the row stays unannounced and is re-sent on release */
        if (slotHeld) coop::NoteHeldForSlot();
        const bool peerHolds = !slotHeld && PeerHoldsSectorOf(pos);   // M-A step 2: announce only into a sector the peer holds
        if (!peerHolds) { g_announced[uid] = 0; ++g_withheld; }
        else SendContextFor(uid, best);   // M-B / P064: the context travels with the announcement (same reliable channel)
        if (peerHolds && !net::SendSpawn(uid, templateName, pos.x, pos.y, pos.z, factionName, false, best))
        {
            ++g_sendFailed;
            // The uid stays allocated and the character stays registered. Un-registering here
            // would leave the character adoptable again next tick and produce an unbounded retry
            // loop against a link that is down; the send failure is counted instead and the
            // character simply has no counterpart until something re-sends.
        }

        // The appearance roll cannot ride the SPAWN (F156/F157) - it has not settled yet for a
        // fresh spawn. For an ADOPTED character it settled long ago, but the same watcher is used
        // rather than a second path: one mechanism, already measured 4/4 on sex, mesh, hair and
        // height, is worth more than a shortcut that is probably fine.
        if (peerHolds) { WatchLocalRoll(uid); g_announced[uid] = 1; }

        // Record the row for P034. Capture failure is counted rather than ignored: a character we
        // cannot handle-check is a character this probe is BLIND to, and a blind spot that reads as
        // "no problem found" is the failure mode this project keeps re-learning.
        TrackForLiveness(uid, best, true);   // the sweep: adopted from OUR world - the authorship record (F494)

        ++g_adopted;
        ++done;
        if (g_mode == kModeOnce) --g_budget;

        DebugLog("[P033] adopted uid=" + S(uid) + " '" + templateName + "' fac='" + factionName
                 + "' pos=" + F1(pos.x) + "," + F1(pos.y) + "," + F1(pos.z)
                 + " distXZ=" + (haveRef ? F1((float)std::sqrt(bestD2)) : std::string("n/a")));
    }
}

// P034. Validates a few adopted rows per call, round-robin, on the main thread. Cheap by design:
// the engine's registry lookup and a pointer compare, a handful of rows at a time.
// F318 - **P034 EXISTED ONLY ON THE ADOPTING SIDE, AND THAT IS WHY THE CLIENT DIED.**
//
// T087, from the two logs side by side, and it is not subtle:
//
//   HOST   A:  rows=103  validated=211833  stalePointers=5  unresolvable=3873  retired=10
//   CLIENT B:  rows=0    validated=0       stalePointers=0  unresolvable=0     retired=0
//
// while B's own `[M1] REPORT spawned=62`. **The client held 62 engine-owned character pointers and
// nothing ever asked whether any of them was still alive**, because rows were only ever recorded by
// the host's adoption sweep. `MirrorRetire` is reachable exclusively from the validator below, so
// on the client no pointer could ever be retired, and `FindSpawned` could never refuse one.
//
// B then crashed inside `ApplyRemoteHit` on its 44th inbound HIT - the `[net] <- HIT` line printed
// and the `[M3b]` line that follows it in all 43 previous hits never did.
//
// F278 wrote this hazard down for characters *we adopt*. The same hazard applies to characters *we
// create on instruction from the authority*: once created they are ordinary world objects, and the
// engine's region streaming can destroy them whenever it likes. Being the one who asked for a
// character to exist confers no control over when it stops existing.
//
// So registration is now its own function and BOTH sides call it. Nothing about the validator
// changes - it was already correct and already measured; it was simply never given anything to
// look at on one of the two machines.
void TrackForLiveness(unsigned int uid, ::Character* obj, bool authored)
{
    if (obj == 0) return;
    // Capture failure is counted rather than ignored: a character we cannot handle-check is one
    // this probe is BLIND to, and a blind spot that reads as "no problem found" is the failure mode
    // this project keeps re-learning.
    /* stale1: every older row of this uid describes an earlier registration - set it aside, so that object's address (or
       the same address with an older handle: review-stale1 1, a re-SPAWN at a reused address) can never be read as this
       uid going stale. The first such row is reused for the new registration, so re-SPAWNs do not fill the table
       (review-stale1 4). */
    int reuse = -1;
    for (int i = 0; i < g_adoptedCount; ++i)
        if (g_adoptedRows[i].uid == uid)
        {
            if (!g_adoptedRows[i].permanent) { g_adoptedRows[i].permanent = true; ++g_rowsSuperseded; }
            if (reuse < 0) reuse = i;
        }
    /* mirror1 (crash T487): a full table reuses a SETTLED row (permanent - superseded, or proven to hold another object;
       ValidateAdopted and WithdrawReloadedCopy skip those). Before this a full table returned SILENTLY and every later
       character went without a liveness check. What is still refused is counted. */
    if (reuse < 0 && g_adoptedCount >= kMaxAdopted)
        for (int i = 0; i < g_adoptedCount; ++i)
            if (g_adoptedRows[i].permanent) { reuse = i; ++g_livenessRowsReused; break; }
    if (reuse < 0 && g_adoptedCount >= kMaxAdopted) { ++g_livenessFull; return; }
    Adopted& row = (reuse >= 0) ? g_adoptedRows[reuse] : g_adoptedRows[g_adoptedCount++];
    row.uid = uid; row.obj = obj; row.retired = false; row.permanent = false; row.authored = authored;
    if (!CaptureObjId(obj, &row.id)) { ++g_noHandle; row.id.index = row.id.serial = 0; }
}

/* F337 retires the uid when the mark is declined - right when this row IS the uid's object, wrong when the uid was
   registered again with another object (stale1). A RAW registry compare (review-stale1 3: FindSpawned answers 0 for a
   temporarily retired new object): the uid is retired only when the registry has no object for it or still names this
   row's. MAIN THREAD. */
static void RetireStaleUidIfThisRow(const Adopted& row)
{
    const void* current = SpawnedRawObject(row.uid);
    if (current != 0 && current != row.obj)
    {
        ++g_staleUidSpared;
        char b[96];
        std::sprintf(b, " %p - the uid now names another object %p", row.obj, current);
        DebugLog("[P034] uid=" + S(row.uid) + " stale row for an OLD object" + std::string(b) + ", so the uid is NOT retired (stale1).");
        return;
    }
    RetireStaleUid(row.uid);
}

void ValidateAdopted(int howMany)
{
    if (g_adoptedCount == 0) return;
    // The command channel rides BOTH pumps (in-game and title screen), so this can be reached after
    // a quit-to-menu with rows still holding handles from a world that no longer exists.
    // ResolveObjId calls into the engine's registry, so refuse when there is no world.
    if (!PlausiblePtr(coop::GameWorldPtr())) return;
    for (int k = 0; k < howMany && k < g_adoptedCount; ++k)
    {
        Adopted& row = g_adoptedRows[g_validateCursor % g_adoptedCount];
        ++g_validateCursor;
        if (row.permanent) continue;                     // settled: resolved to a different object, or superseded (stale1)
        /* stale1 (review-stale1 2): a row whose uid the registry no longer maps to this row's object is out of date - the
           uid was erased (despawn / unload) or registered again. It must not retire, withdraw or mark anything on that
           uid's behalf, including MirrorRetire on an address another uid now owns. F337's own case (the address re-adopted
           under a DIFFERENT uid) still counts as current: this row's uid still maps to this address. */
        if (SpawnedRawObject(row.uid) != row.obj) { row.permanent = true; ++g_rowsSuperseded; continue; }
        if (!ObjIdValid(row.id)) continue;               // never had a handle; counted at adoption

        ++g_validated;
        ::Character* now = ResolveObjId(row.id);

        if (now == 0)
        {
            // Counted on EVERY failed resolve. It was left dangling by an earlier edit - printed but
            // never incremented - so it would have read 0 forever while `retired=` carried the real
            // number, and anyone diffing it against T083's `unresolvable=21` would have read a
            // silent zero as a fix.
            ++g_unresolvable;
            // THE CRASH FIX. T083: 21 adopted characters stopped resolving, this probe reported
            // every one - and nothing withdrew the pointer, so the streamer, the drift watch and
            // the combat watch went on dereferencing freed objects. One read came back with the
            // bit pattern of 1.0f sitting in an integer field and position floats in health
            // fields, and the instance crashed eleven seconds later.
            // **Detection without retirement is not a safety feature.**
            //
            // Retire on the FIRST failure, because one dereference of freed memory is enough to
            // crash - there is no safe streak to wait out. But retire REVERSIBLY: a failed resolve
            // is not proof of destruction. A character that streamed out with its region resolves
            // again when it streams back, and dropping it permanently would silently remove a live
            // character from replication.
            // Second witness before withdrawing anything.
            bool stillInWorld = false;
            if (PlausiblePtr(coop::GameWorldPtr()))
            {
                const GameHashSet< ::Character*>::type& live = coop::GameWorldPtr()->activeCharacters();
                for (GameHashSet< ::Character*>::type::const_iterator li = live.begin();
                     li != live.end(); ++li)
                    if ((const void*)*li == row.obj) { stillInWorld = true; break; }
            }

            if (stillInWorld)
            {
                // DISCRIMINATE, because "still in the update list" is true for BOTH the case this
                // witness was built for and its exact opposite.
                //
                // A review caught it: address reuse ALSO makes the handle stop resolving (the old
                // serial no longer matches - that is the whole basis for trusting the registry), so
                // a recycled address lands here, matches the NEW occupant, and would be waved
                // through as a false positive. The true stale-pointer case would then be neither
                // detected nor retired, and `stalePointers` would go back to being a guarantee
                // rather than a measurement - the same defect F284 was written to remove, arriving
                // by a different road.
                //
                // The object is provably live here (the engine is updating it), so re-capturing its
                // handle is safe. Same index+serial => the object is what we adopted and the handle
                // test is wrong about it. Different => the address was reused by someone else.
                ObjId fresh;
                bool gotFresh = CaptureObjId(row.obj, &fresh);
                if (gotFresh && (fresh.index != row.id.index || fresh.serial != row.id.serial))
                {
                    row.permanent = true;
                    if (!row.retired) { row.retired = true; ++g_retired; RetireWithdrawNote(row.uid, cooprw::kEvRetire); }
                    // F334 - mark it DESTROYED, not merely retired. "The address holds a different
                    // object" is the permanent case, and two mechanisms read that flag to mean it.
                    // F336 - and pass the uid, because THIS ROW MAY BE OUT OF DATE. If the address
                    // was already re-adopted under a new uid, marking it would permanently kill a
                    // live row; the mark is refused in that case and the refusal is counted.
                    bool marked = MirrorMarkDestroyed(row.obj, row.uid);
                    if (!marked) RetireStaleUidIfThisRow(row);   // F337, guarded by stale1
                    ++g_stalePointers;
                    ErrorLog("[P034] uid=" + S(row.uid) + " STALE POINTER CONFIRMED - the address is"
                             " live but now holds a DIFFERENT object (handle index/serial changed)."
                             " Permanently retired"
                             + std::string(marked
                                 ? " and marked destroyed (F334)."
                                 : "; mark DECLINED (F336) - this address has already been"
                                   " re-adopted under a different uid, so the row we would have"
                                   " killed is live. THIS uid is retired instead (F337)."));
                    continue;
                }
                // The engine is still updating this object, so it is NOT destroyed and the handle
                // test is wrong about it. Do not retire - withdrawing a live character would
                // silently drop it from replication, which is worse than the hazard we are
                // guarding against. Counted, and reported once, because the RATE is the finding.
                ++g_falseGone;
                if (g_falseGone == 1)
                    ErrorLog("[P034] uid=" + S(row.uid) + " handle failed to resolve BUT the object"
                             " is still in the engine's character update list, so it is ALIVE."
                             " NOT retired. The handle test has false positives; falseGone counts"
                             " them. Reported once.");
                continue;
            }

            if (!row.retired)
            {
                row.retired = true;
                ++g_retired;
                MirrorRetire(row.obj);
                /* P8m - the peer is owed an UNLOAD for this uid.  Queued, not sent: the send belongs to
                   the announce cadence, and it must survive a link that is down. */
                RetireWithdrawNote(row.uid, cooprw::kEvRetire);
                DebugLog("[P034] uid=" + S(row.uid) + " RETIRED - its handle no longer resolves."
                         " Nothing will dereference this pointer again. This may be destruction OR"
                         " a region streaming out; it is re-checked and restored if it comes back.");
            }
            continue;
        }

        if ((const void*)now == row.obj)
        {
            if (row.retired)
            {
                row.retired = false;
                ++g_restored;
                MirrorRestore(row.obj);
                /* P8m - CANCELS a withdrawal that has not gone out yet.  If one already has, the entry is
                   simply forgotten and the existing re-announce leg of AnnouncePass sends the SPAWN again
                   once the sector is held and loaded (state == 0 && held && mineLoaded). */
                RetireWithdrawNote(row.uid, cooprw::kEvRestore);
                DebugLog("[P034] uid=" + S(row.uid) + " RESTORED - the handle resolves again to the"
                         " same object, so the earlier failure was transient (streamed out and"
                         " back), NOT destruction.");
            }
            continue;
        }

        // Resolved to a DIFFERENT object: the address was reused. This is the true stale-pointer
        // case, and it is only REACHABLE because retirement is reversible. An earlier version
        // flagged the row on the first failed resolve and never looked again - which made this
        // branch unreachable by construction, so `stalePointers=0` would have been a guarantee
        // rather than a measurement. Caught in review before it could mislead a run.
        row.permanent = true;
        if (!row.retired) { row.retired = true; ++g_retired; RetireWithdrawNote(row.uid, cooprw::kEvRetire); }
        // F334 permanent, F336 uid-checked: see the sibling branch above for why both matter.
        bool marked = MirrorMarkDestroyed(row.obj, row.uid);
        if (!marked) RetireStaleUidIfThisRow(row);   // F337, guarded by stale1
        ++g_stalePointers;
        ErrorLog("[P034] uid=" + S(row.uid) + " STALE POINTER CONFIRMED - the handle now resolves"
                 " to a DIFFERENT object. The address was reused. Permanently retired"
                 + std::string(marked
                     ? " and marked destroyed (F334)."
                     : "; mark DECLINED (F336) - already re-adopted under a different uid."
                       " THIS uid is retired instead (F337)."));
    }
}


const char* AdoptGateFor(void* character)
{
    ::Character* c = (::Character*)character;
    if (!PlausibleObject(c)) return "unreadable";
    if (FindSpawnedUid(c) != 0) return "alreadySpawned";
    /* worldPosition dereferences the animation object (F205); the sweep guards it here and so does this. */
    if (!PlausibleObject(*(void**)((char*)c + 0x448))) return "guard";
    {
        Ogre::Vector3 p = c->worldPosition();
        if (IsPlayerFaction(c->getOwnerFactionDirect())) return "playerFaction";   /* the sweep never asks the area rule about these */
        { float sqx = 0, sqz = 0; if (SquadDecisionPos(c, &sqx, &sqz) == 1) { p.x = sqx; p.z = sqz; } }   /* T-1 B1: the sweep's own deciding position */
        {
            const int d = AnnounceAreaDecide(p.x, p.z);
            if (d == kAreaOtherHeld) return "skippedOtherHeld";
            if (d == kAreaNoMap) return "skippedNoMap";
        }
        if (g_adoptRadius > 0.0f)
        {
            ::Character* ref = GetTarget();
            if (PlausibleObject(ref))
            {
                Ogre::Vector3 eye = ref->worldPosition();
                const double dx = (double)p.x - (double)eye.x, dz = (double)p.z - (double)eye.z;
                if (dx * dx + dz * dz > (double)g_adoptRadius * (double)g_adoptRadius) return "radius";
            }
        }
    }
    return "adoptable";
}

/* orphan1 (decision 37): the report token - people removed / skipped by OrphanPurgeTick, and the leaf gate's unlinked refusals. */
static std::string OrphanReportTokenWs()
{
    long long removed = 0, skipped = 0;
    OrphanReportNumbers(&removed, &skipped);
    return " orphan[removed,skipped,gatedHeldUnlinked]=" + S(removed) + "," + S(skipped) + "," + S(WorldGenGatedHeldUnlinked()) + OrphanReportDetail();
}
void ReportWorldSync()
{
    const char* mode = (g_mode == kModeOff) ? "off" : (g_mode == kModeOnce ? "once" : "on");
    DebugLog(std::string("[P033] REPORT mode=") + mode
             + " budgetLeft=" + S(g_budget)
             + " adopted=" + S(g_adopted)
             + " sweepComplete=" + S(g_sweepComplete ? 1 : 0)
             + " | skippedAlreadyOurs=" + S(g_skippedOurs)
             + " unreadable=" + S(g_unreadable)
             + " noTemplate=" + S(g_noTemplate)
             + " noFaction=" + S(g_noFaction)
             + " sendFailed=" + S(g_sendFailed)
             + " registerFailed=" + S(g_registerFailed)
             + " | [P034] rows=" + S(g_adoptedCount) + " rowsReused=" + S(g_livenessRowsReused) + " livenessFull=" + S(g_livenessFull)
             + " validated=" + S(g_validated)
             + " stalePointers=" + S(g_stalePointers)
             + " unresolvable=" + S(g_unresolvable)
             + " noHandleAtAdopt=" + S(g_noHandle)
             + " retired=" + S(g_retired)
             + " restored=" + S(g_restored)
             + " unloadSent[putAway,reloaded,heldSettled,retire,announce]=" + S(g_unloadSentWhy[0]) + "," + S(g_unloadSentWhy[1]) + "," + S(g_unloadSentWhy[2]) + "," + S(g_unloadSentWhy[3]) + "," + S(g_unloadSentWhy[4])
             + " reannounced=" + S(g_reannounced) + " withheld=" + S(g_withheld) + " announceSkippedHandover=" + S(g_announceSkippedHandover) + " p22[stale,real]=" + S(g_p22Stale) + "," + S(g_p22Real) + " p21[seen,spawned,guard,rule,radius,ticks]=" + S(g_p21Seen) + "," + S(g_p21Spawned) + "," + S(g_p21Guard) + "," + S(g_p21Rule) + "," + S(g_p21Radius) + "," + S(g_p21Ticks) + " d33[worldAdoptable,skippedOtherHeld,announceRefusedNoMap]=" + S(g_worldAdoptable) + "," + S(g_skippedOtherHeld) + "," + S(g_skippedNoMap) + " reannounceDrained=" + S(g_reannounceDrained) + "   (P8e / audit C5+C6: the third d33 number was printed as skippedNoMap and is announceRefusedNoMap now - SAME EVENT, renamed because it no longer depends on the session role.  P97 (owner 334 a / 337 a): the fourth d33 number, announceNoMapFallback, and the roster reason adoptableNoMapFallback are RETIRED with the bounded no-map wait - with no fresh map every game skips - so a readout writes ABSENT for them and not zero; the token adoptableNoMap is RETIRED with the host arm, so a readout writes ABSENT and not zero.  reannounceDrained is rows the re-announce queue walked on THIS game, which nothing counted while the drain was host-only - a client's zero could not be told from an empty queue.)" + " unloadsSent=" + S(g_unloadsSent) + " withdrawnOwnUnload=" + S(g_withdrawnOwnUnload) + " reannouncedOnReload=" + S(g_reannouncedOnReload) + " announceTicks=" + S(g_announceTicks) + " announceStale=" + S(g_announceStale) + " streamGateWithheld=" + S(g_streamGateWithheld) + " announceAbsentReporter=" + S(coop::ZonesAnnounceAbsentReporter()) + " effCarriedCells=" + S((long long)coop::ZonesEffCarriedCells()) + " areaMap[rows,seats,rekeyed,bytes]=" + coop::ZonesAreaMapCounters() /* M9 (T-197) */ + " effDroppedAfterPeerGone=" + S(coop::ZonesEffDroppedAfterPeerGone())
             + " falseGone=" + S(g_falseGone)
             + " sweepStandInSkipped=" + S(g_sweepStandInSkipped)   /* inv7a-b */
             + " sweepHandedOver[skipped,adopted]=" + S(g_sweepHandedOverSkipped) + "," + S(g_sweepHandedOverAdopted) + " sweepContextSkipped=" + S(g_sweepContextSkipped)   /* M7a3f3 T-425 [m7a3f3-ws4] */
             + OrphanReportTokenWs()   /* orphan1: orphan[removed,skipped,gatedHeldUnlinked]= */
             + " reannounceSkippedNotMine=" + S(g_reannounceSkippedNotMine)
             + " stale1[rowsSuperseded,staleUidSpared]=" + S(g_rowsSuperseded) + "," + S(g_staleUidSpared)
             // F336/F337 - beside `stalePointers`, which is the number they qualify. A confirmed
             // stale row is marked permanently destroyed UNLESS the address has already been
             // re-adopted under a new uid, in which case the mark is declined and the OLD uid is
             // retired instead. `stalePointers = marked + declined`, and `declined` is the rate of
             // the destroy->recycle->re-adopt race that the previously deployed build was quietly
             // churning uids through.
             + " markDestroyedDeclined=" + S(MarkDestroyedDeclinedCount())
             + " staleUidRetired=" + S(StaleUidRetiredCount())
             /* P8m (H-ghost) - THE WITHDRAWAL OF A RETIRED CHARACTER, and the identity a readout can check.
                Every retirement edge in ValidateAdopted increments `retired` and queues exactly one entry, and
                every entry ends in exactly one of three resolutions or is still pending, so:

                    retired == retireWithdraw[sent] + retireWithdraw[notAnnounced]
                             + retireWithdraw[cancelledByRestore] + retireWithdraw[pending]

                `pending` is a GAUGE (entries owed right now); the other three are cumulative resolutions.
                `deferredLinkDown` is an ATTEMPT count - one entry can be deferred many times - so it is NOT
                a term in the identity and must never be subtracted from it.  `retireWithdraw[sent]` is a
                SUBSET of `unloadsSent`, never an addition to it, exactly as withdrawnOwnUnload is. */
             + " retireWithdraw[pending,sent,deferredLinkDown,cancelledByRestore,notAnnounced]="
             + S(RetireWithdrawPending()) + "," + S(g_rwSent) + "," + S(g_rwDeferredLinkDown) + ","
             + S(g_rwCancelledByRestore) + "," + S(g_rwNotAnnounced)
             + " | radius=" + F1(g_adoptRadius)
             // F302 - the SOURCE by name, not a bool. DEFAULT-unlimited is the compiled-in default
             // (0 = unlimited, decision 36) and says so; "client-viewDistance" is the only correct
             // provenance for a DERIVED number.
             + " radiusSource=" + RadiusSourceName(g_radiusSource)
             // decision 36: the derive is retired and does nothing - this is how many times it was CALLED, so "retired" is a number rather than a claim.
             + " radiusDeriveIgnored=" + S(g_radiusDeriveIgnored)
             + " clientViewDistance=" + (net::SessionIsHost() && net::PeerViewDistance() > 0.0f
                                         ? F1(net::PeerViewDistance())
                                         : std::string("none"))
             + " skippedTooFar=" + S(g_skippedTooFar));
}

// review-p3o H2 - THE LIVENESS ROWS, DROPPED BEFORE THE ENGINE FREES WHAT THEY NAME.
//
// `Adopted::obj` is a `const void*` at every adopted character; it is compared and passed to
// `MirrorMarkDestroyed` at :818/:878, never dereferenced. That makes a stale row harmless to READ
// and dangerous to BELIEVE: after `GameWorld::_clearAndDestroyGameWorldStuff` every one of these
// addresses can be handed to a different character, and then a compare matches the wrong object.
// The `ObjId` re-resolve at those sites only partly covers it, and `ValidateAdopted` itself calls
// into the engine's registry, so the rows must not outlive the world.
//
// Called from `detour_worldTeardown` (store.cpp) BEFORE `orig_worldTeardown`, main thread.
// Touches no engine memory: it zeroes our own array and resets the two cursors into it.
//
// `g_announced` goes with them. It holds uids, not pointers, so it cannot dangle - but it is the
// per-uid answer to "has the peer been told about this character", every one of those characters is
// being destroyed, and the rows it is keyed against are gone. Uids are never reused (spawn.cpp
// mints a per-process prefix plus a monotonic counter), so this is housekeeping rather than a fix,
// and it is counted separately so the line does not claim otherwise.
//
// **`g_sweepComplete` AND `g_mode` ARE LEFT ALONE, DELIBERATELY.** Clearing them would restart the
// adoption sweep in the world that loads next - a behaviour change outside teardown's remit, and
// one nobody has asked for. The consequence is worth stating rather than leaving to be discovered:
// after a mid-session game load the world sweep does NOT re-run, so the newly loaded world's
// population is not adopted. That is the same as today; this function does not make it worse.
void WorldsyncWorldTeardown()
{
    const int rows = g_adoptedCount;
    for (int i = 0; i < g_adoptedCount && i < kMaxAdopted; ++i)
    {
        g_adoptedRows[i].uid = 0;
        g_adoptedRows[i].obj = 0;
        g_adoptedRows[i].id.index = g_adoptedRows[i].id.serial = 0;
        g_adoptedRows[i].id.type = g_adoptedRows[i].id.container = g_adoptedRows[i].id.containerStamp = 0;
        g_adoptedRows[i].retired = false;
        g_adoptedRows[i].permanent = false;
    }
    g_adoptedCount   = 0;
    g_validateCursor = 0;
    g_reannounceAt   = -1;   // a re-announce walking the rows must not carry on over an empty table

    const size_t announced = g_announced.size();
    g_announced.clear();
    /* P8m: the rows these entries name are gone and uids are never reused, so nothing is owed to anyone.
       Cleared rather than drained - a teardown is not a withdrawal, and counting it as one would put a
       number that means "we told the peer" on an event where we told it nothing. */
    g_retireWithdraw.clear();
    g_sweepAdoptGen.clear();   /* inv7e2: the world these adoptions came from is gone */

    DebugLog("[P033] world teardown: forgot " + S((long long)rows)
             + " adopted liveness rows and " + S((long long)announced) + " announce marks");
}

int AreaKeyAt(float x, float z) { const Sector s = SectorOf(x, z); return cooplive::AreaKey(s.x, s.y); }
int CharAreaKeyNow(unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    if (!PlausibleObject(c)) return -1;
    Ogre::Vector3 pos = c->worldPosition();
    return AreaKeyAt(pos.x, pos.z);
}
static long long g_cuResent = 0, g_cuAnnounced = 0, g_cuLooks = 0, g_cuState = 0, g_cuCombat = 0, g_cuFailed = 0, g_cuKept = 0;
/* M7a2 [m7a2-ws1]: INTENT sent, ASKs answered already (dedup), characters held back by a hand-over, re-sent when it settled, settled
   away (the new owner answers), held rows refused (full), reverse catch-ups run, characters re-sent and announced by them. */
static long long g_cuIntent = 0, g_cuDedup = 0, g_cuHeld = 0, g_cuHeldResent = 0, g_cuHeldReleased = 0, g_cuHeldFull = 0, g_cuReverse = 0, g_cuReverseChars = 0, g_cuReverseAnnounced = 0;
static std::vector<unsigned long long> g_cuAnsweredRecent;       /* item 7: the (slot, ask) pairs answered lately */
static std::map<unsigned int, std::set<int> > g_cuHeldFor;       /* item 2: uid -> the requesters a catch-up skipped it for */
const size_t kCuHeldForMax = 512;
/* M7a2 fold 1 item 8 [m7a2f-ws0]: the reverse catch-up's queue (sectors queued by the pump / being walked), its cursor and counters:
   queued (CATCHUP ASKED rounds queued), budgetHit (a tick stopped at the budget), dropped (the world stopped with sectors queued). */
static std::set<int> g_cuRevPending, g_cuRevActive;
static int g_cuRevCursor = 0;
static unsigned int g_cuRevAskPending = 0, g_cuRevAsk = 0;
static long long g_cuRevQueued = 0, g_cuRevBudgetHit = 0, g_cuRevDropped = 0, g_cuRevPassResent = 0, g_cuRevPassAnnounced = 0, g_cuRevPassFailed = 0, g_cuRevPassBudgetHits = 0;   /* M7a fold 4 (T760 V7): the walk's budget stops, for its round line */
/* M7a fold 4 (T760 V7): a character to send whose wire reads failed (CatchupWire: its GameData or faction) - counted here, not as kept. */
static long long g_cuWireReadFailed = 0;
const long long kCuRevBudget = 64;
std::string WorldsyncCatchupCounts()
{
    return " catchUpState[resent,announced,looks,state,combat,failed,kept,intent,dedup,held,heldResent,heldReleased,heldFull,reverse,reverseChars,reverseAnnounced,reverseQueued,reverseBudgetHit,reverseDropped,wireReadFailed]=" /*[m7a2f-ws1]; fold 4: wireReadFailed appended last*/
        + S(g_cuResent) + "," + S(g_cuAnnounced) + "," + S(g_cuLooks) + "," + S(g_cuState) + "," + S(g_cuCombat) + "," + S(g_cuFailed) + "," + S(g_cuKept)
        + "," + S(g_cuIntent) + "," + S(g_cuDedup) + "," + S(g_cuHeld) + "," + S(g_cuHeldResent) + "," + S(g_cuHeldReleased) + "," + S(g_cuHeldFull)
        + "," + S(g_cuReverse) + "," + S(g_cuReverseChars) + "," + S(g_cuReverseAnnounced)
        + "," + S(g_cuRevQueued) + "," + S(g_cuRevBudgetHit) + "," + S(g_cuRevDropped) + "," + S(g_cuWireReadFailed);   /* [m7a2f-ws2] */
}
/* M7a2: the live reads the catch-up takes of one row (AnnouncePass's plausibility tests), and the wire's faction. */
static bool CatchupCharReads(unsigned int uid, ::Character** c, Ogre::Vector3* pos, int* key)
{
    ::Character* ch = FindSpawned(uid);
    if (!PlausibleObject(ch) || !PlausibleObject(*(void**)((char*)ch + 0x448))) return false;
    *pos = ch->worldPosition();
    const Sector s = SectorOf(pos->x, pos->z);
    *key = cooplive::AreaKey(s.x, s.y); *c = ch;
    return true;
}
static bool CatchupWire(::Character* c, GameData** gd, Faction** f)
{
    *gd = c->getRecordDirect(); *f = c->getOwnerFactionDirect();
    return PlausibleObject(*gd) && PlausibleObject(*f) && !coopslot::WireLacksSlot(WireFactionName(*f));
}
/* ONE CHARACTER'S FULL STATE - CONTEXT, SPAWN, then APPEARANCE + CLOTHING when its roll has settled, STATE, COMBATMODE (OFF too) and
   (M7a2 item 9) INTENT. The road is the caller's: net::CharStreamToSlot(slot) for one game, -1 for AREA. */
static bool CatchupSendState(unsigned int uid, ::Character* c, GameData* gd, Faction* f, const Ogre::Vector3& pos)
{
    SendContextFor(uid, c);
    if (!net::SendSpawn(uid, gd->name, pos.x, pos.y, pos.z, WireFactionName(f), false, c)) return false;
    if (AppearanceResendOwned(uid)) ++g_cuLooks;
    if (StatePush(uid)) ++g_cuState;
    if (CombatModeResendOwned(uid)) ++g_cuCombat;
    if (IntentResendOwned(uid)) ++g_cuIntent;
    return true;
}

/* M6 (T-197 piece 6, store protocol 60) - THE CATCH-UP ANSWER. The notebook asks this game (CATCHUP ASK, queued as a
   notebook message and applied here by the drain: MAIN THREAD, a running world) for what it owns in the listed sectors, on
   behalf of a game that has just come to have them in its delivery area. The walk is AnnouncePass's own (this game's
   adopted rows, IsUidMine, the same plausibility tests).
   M7a: each character of mine standing in those sectors is decided by cooplive::CatchupCharDecide (AnnounceDecide with the reporter
   GAINED). RESEND: its full state goes to the asker ONLY, by LIVE SLOT (CatchupSendState). SPAWN (not announced, another game now
   holds the area): announced to the notebook now, route AREA, exactly as the pass would a second later. UNLOAD / KEEP: left to the pass.
   M7a2: a repeated (slot, ask) is not answered again (item 7); a character whose hand-over is in flight is HELD - named in the END so the
   asker's sweep keeps its copy, and re-sent to the asker when the hand-over settles (item 2); INTENT rides with the state (item 9). The
   asker is answered last with LIVE SLOT CATCHUP_END {ask number, characters sent, the sectors asked, the uids held}. */
void WorldsyncCatchupAsk(const std::vector<char>& payload)
{
    cooplive::CatchupMsg a;
    if (!cooplive::CatchupDecode(payload.empty() ? 0 : &payload[0], payload.size(), &a) || a.kind != (unsigned int)cooplive::kCatchupAsk)
    { StoreNoteCatchupAnswer(false, false, 0); DebugLog("[P033] catch-up: a malformed CATCHUP ASK - not answered"); return; }
    static long s_cuAnsweredGen = 0;   /* fold 1 item 6 [m7a2f-ws3]: the list is per world-server link - a restarted world server asks from 0 again */
    if (!cooplive::CatchupAnswerFirstOnLink(&g_cuAnsweredRecent, &s_cuAnsweredGen, StoreNotebookLinkGen(), a.slot, a.askNo))
    {
        ++g_cuDedup;
        DebugLog("[P033] catch-up #" + S((long long)a.askNo) + " for slot " + S((long long)a.slot) + ": already answered - not answered twice (catchUpState dedup " + S(g_cuDedup) + ")");
        return;
    }
    SquadLeadReannounce(a.keys);   /* M7a A1 build 1 [a1b1-ws0] [review F7]: a game that newly has the area hears the leaders of the squads there again (handoff.cpp; fold 1 [a1b1f1-ws0] [F5]: only those) */
    std::set<int> want(a.keys.begin(), a.keys.end());
    long long mine = 0, resent = 0, announced = 0, failed = 0, held = 0;
    cooplive::CatchupEndMsg end; end.askNo = a.askNo; end.keys = a.keys;
    for (int i = 0; i < g_adoptedCount && i < kMaxAdopted; ++i)
    {
        const unsigned int uid = g_adoptedRows[i].uid;
        if (uid == 0 || !net::IsUidMine(uid)) continue;
        ::Character* c = 0; Ogre::Vector3 pos; int key = -1;
        if (!CatchupCharReads(uid, &c, &pos, &key) || key < 0 || want.count(key) == 0) continue;
        ++mine;
        std::map<unsigned int, int>::iterator an = g_announced.find(uid);
        const int state = (an == g_announced.end()) ? 1 : an->second;
        const int ann = cooplive::CatchupCharDecide(HandoffPendingHas(uid), PeerHoldsSectorOf(pos), coop::IsPositionLoadedHere(pos.x, pos.y, pos.z), state == 1);
        if (ann == cooplive::kCatchupHeld)   /* item 2: the receiver may already run it - named, and re-sent here when the hand-over settles */
        {
            ++held; ++g_cuHeld;
            if (end.held.size() < (size_t)cooplive::kCatchupEndHeldMax) end.held.push_back(uid); else ++g_cuHeldFull;
            if (g_cuHeldFor.size() < kCuHeldForMax || g_cuHeldFor.count(uid) != 0) g_cuHeldFor[uid].insert((int)a.slot); else ++g_cuHeldFull;
            continue;
        }
        if (ann != cooplive::kAnnResend && ann != cooplive::kAnnSpawn) { ++g_cuKept; continue; }
        GameData* gd = 0; Faction* f = 0;
        if (!CatchupWire(c, &gd, &f)) { ++g_cuWireReadFailed; continue; }   /* fold 4: a failed read, not a keep */
        if (ann == cooplive::kAnnSpawn)
        {
            SendContextFor(uid, c);
            if (net::SendSpawn(uid, gd->name, pos.x, pos.y, pos.z, WireFactionName(f), false, c)) { WatchLocalRoll(uid); g_announced[uid] = 1; ++announced; ++g_cuAnnounced; }
            else { ++failed; ++g_cuFailed; }
            continue;
        }
        net::CharStreamToSlot((int)a.slot);   /* RESEND: the asker only */
        if (CatchupSendState(uid, c, gd, f, pos)) { ++resent; ++g_cuResent; }
        else { ++failed; ++g_cuFailed; }
        net::CharStreamToSlot(-1);
    }
    net::CharStreamToSlot(-1);
    const long long sentChars = resent + announced;
    end.count = (unsigned int)sentChars;
    std::vector<char> eb;
    const bool sent = cooplive::CatchupEndEncode(&eb, end) && StoreSendLive(cooplive::kRouteSlot, a.slot, cooplive::kInnerCatchupEnd, eb, true);
    StoreNoteCatchupAnswer(true, sent, sentChars);
    DebugLog("[P033] catch-up #" + S((long long)a.askNo) + " for slot " + S((long long)a.slot) + ": " + S((long long)a.keys.size()) + " sectors, "
             + S(mine) + " of this game's characters stand there - " + S(resent) + " re-sent to that game (SLOT), " + S(announced) + " announced (AREA), "
             + S(held) + " held (a hand-over in flight), " + S(failed) + " failed - CATCHUP_END " + (sent ? "sent" : "NOT sent (no notebook road)"));
}

/* M7a2 item 5 - THE REVERSE CATCH-UP. The notebook has just told THIS game (CATCHUP ASKED) that these sectors are newly in its delivery
   area; the other games there are asked for their characters, and here this game re-sends ITS characters standing in them to the games
   already covering them: route AREA (the stream's own road), each decided as an ASK's are - RESEND = full state, SPAWN = announced,
   HELD / KEEP / UNLOAD = nothing (the pass and the hand-over settle those). MAIN THREAD, a running world (store.cpp). */
void WorldsyncCatchupReverse(unsigned int askNo, const std::vector<int>& keys)
{   /* M7a2 fold 1 item 8 [m7a2f-ws4]: THE PUMP ONLY QUEUES (its rule: it polls and enqueues) - WorldsyncCatchupReverseTick sends */
    if (keys.empty()) return;
    g_cuRevPending.insert(keys.begin(), keys.end()); g_cuRevAskPending = askNo; ++g_cuRevQueued;
    SquadLeadReannounce(keys);   /* M7a A1 build 1 [a1b1-ws1] [review F7]: the reverse catch-up re-announces the squads in its sectors too (plugin state only; [F5]) */
}
/* fold 1 item 8: net::SessionCatchupApplyTick (after the drain) [m7a2f-ap6], MAIN THREAD. One walk of this game's rows over the queued sectors, at most kCuRevBudget characters
   standing there a tick, resuming at the row it stopped at; sectors queued during a walk are walked next. Each character is decided as
   before (CatchupCharDecide). Whether the target game ALREADY holds a character's SPAWN is not known cheaply here - there is no per-game
   acknowledgement - so only CatchupCharDecide's own tests (another game holds the sector, announced) skip one. A row removed mid-walk can
   shift the cursor past one row; the announce pass covers it. */
void WorldsyncCatchupReverseTick()
{
    if (g_cuRevActive.empty() && g_cuRevPending.empty()) return;
    if (!GameplayRunning()) { g_cuRevActive.clear(); g_cuRevPending.clear(); g_cuRevCursor = 0; ++g_cuRevDropped; return; }
    if (EngineWritesBlocked()) return;   /* a load gate: the walk waits */
    if (g_cuRevActive.empty())
    {
        g_cuRevActive.swap(g_cuRevPending); g_cuRevAsk = g_cuRevAskPending; g_cuRevCursor = 0;
        g_cuRevPassResent = 0; g_cuRevPassAnnounced = 0; g_cuRevPassFailed = 0; g_cuRevPassBudgetHits = 0;
    }
    long long done = 0;
    net::CharStreamToSlot(-1);
    for (; g_cuRevCursor < g_adoptedCount && g_cuRevCursor < kMaxAdopted; ++g_cuRevCursor)
    {
        const unsigned int uid = g_adoptedRows[g_cuRevCursor].uid;
        if (uid == 0 || !net::IsUidMine(uid)) continue;
        ::Character* c = 0; Ogre::Vector3 pos; int key = -1;
        if (!CatchupCharReads(uid, &c, &pos, &key) || key < 0 || g_cuRevActive.count(key) == 0) continue;
        if (done >= kCuRevBudget) { ++g_cuRevBudgetHit; ++g_cuRevPassBudgetHits; return; }   /* this row is the first one walked next tick */
        ++done;
        std::map<unsigned int, int>::iterator an = g_announced.find(uid);
        const int state = (an == g_announced.end()) ? 1 : an->second;
        const int ann = cooplive::CatchupCharDecide(HandoffPendingHas(uid), PeerHoldsSectorOf(pos), coop::IsPositionLoadedHere(pos.x, pos.y, pos.z), state == 1);
        if (ann != cooplive::kAnnResend && ann != cooplive::kAnnSpawn) continue;
        GameData* gd = 0; Faction* f = 0;
        if (!CatchupWire(c, &gd, &f)) { ++g_cuWireReadFailed; continue; }   /* fold 4: counted, as the ask's are */
        if (ann == cooplive::kAnnSpawn)
        {
            SendContextFor(uid, c);
            if (net::SendSpawn(uid, gd->name, pos.x, pos.y, pos.z, WireFactionName(f), false, c)) { WatchLocalRoll(uid); g_announced[uid] = 1; ++g_cuRevPassAnnounced; }
            else ++g_cuRevPassFailed;
            continue;
        }
        if (CatchupSendState(uid, c, gd, f, pos)) ++g_cuRevPassResent; else ++g_cuRevPassFailed;
    }
    ++g_cuReverse; g_cuReverseChars += g_cuRevPassResent; g_cuReverseAnnounced += g_cuRevPassAnnounced; g_cuFailed += g_cuRevPassFailed;
    /* fold 4 (T760 V7): a round that stopped at the budget is written too, so a round that sent nothing can be read */
    if (g_cuRevPassResent + g_cuRevPassAnnounced + g_cuRevPassFailed > 0 || g_cuRevPassBudgetHits > 0)
        DebugLog("[P033] reverse catch-up #" + S((long long)g_cuRevAsk) + ": " + S((long long)g_cuRevActive.size()) + " sectors newly in this game's delivery area - "
                 + S(g_cuRevPassResent) + " of its characters there re-sent (AREA), " + S(g_cuRevPassAnnounced) + " announced, " + S(g_cuRevPassFailed)
                 + " failed (M7a2; fold 1: walked in the session tick, " + S(kCuRevBudget) + " a tick, budgetHit " + S(g_cuRevBudgetHit) + ")"
                 + (g_cuRevPassBudgetHits > 0 ? std::string(", budget hits ") + S(g_cuRevPassBudgetHits) + (g_cuRevPassResent + g_cuRevPassAnnounced + g_cuRevPassFailed == 0 ? ", all kept" : "") : std::string()));
    g_cuRevActive.clear(); g_cuRevCursor = 0;
}

/* M7a2 item 2 - A HAND-OVER SETTLED (handoff.cpp: its ACK - before a taken member is released - or its abandonment). A character a
   catch-up held back is re-sent to each asker it was held from WHILE IT IS STILL THIS GAME'S: an aborted hand-over leaves it here, and a
   completed one is followed by this game's OWNER_MOVED (net::ReleaseLocalOwner), on the same road, after this state. */
void WorldsyncCatchupHandoverSettled(unsigned int uid)
{
    std::map<unsigned int, std::set<int> >::iterator h = g_cuHeldFor.find(uid);
    if (h == g_cuHeldFor.end()) return;
    std::set<int> slots; slots.swap(h->second); g_cuHeldFor.erase(h);
    ::Character* c = 0; Ogre::Vector3 pos; int key = -1; GameData* gd = 0; Faction* f = 0;
    if (!net::IsUidMine(uid) || !CatchupCharReads(uid, &c, &pos, &key) || !CatchupWire(c, &gd, &f)) { ++g_cuHeldReleased; return; }
    long long sentN = 0;
    for (std::set<int>::const_iterator s = slots.begin(); s != slots.end(); ++s)
    {
        net::CharStreamToSlot(*s);
        if (CatchupSendState(uid, c, gd, f, pos)) { ++sentN; ++g_cuHeldResent; } else ++g_cuFailed;
        net::CharStreamToSlot(-1);
    }
    DebugLog("[P033] catch-up: uid=" + S((long long)uid) + " held back by a hand-over in flight - the hand-over settled, its state re-sent to "
             + S(sentN) + " of " + S((long long)slots.size()) + " askers (M7a2)");
}

// review-session S6 - THE PEER IS GONE; NOTHING HERE HAS BEEN TOLD ANYTHING ANY MORE.
//
// The adopted rows STAY: they name OUR characters, which are alive, still ours, and still adopted.
// What dies with the link is the per-uid answer to "has the peer been told about this uid", because
// the peer that was told is gone and a reconnect is a peer that knows nothing.
//
// **The marks are set to 0, NOT erased**, and that direction is the whole point. `AnnouncedToPeer`
// treats an ABSENT uid as announced (the pre-gate default for rows adopted before M-A step 2), so
// clearing the map would open the MOVE/STATE/INTENT gate for every uid at the moment the new peer
// has none of them - exactly the "MOVE refused ... owner unknown" flood H030 closed. Explicitly
// unannounced is the honest state, and the link-up re-announce turns them back on one by one.
//
// `g_reannounceAt` is reset because a walk in progress belongs to the dead session, and
// `g_linkWasUp` because the up-edge that starts the next walk must fire even if the link comes back
// before the next worldsync tick - the drop would otherwise never be seen here at all.
// Touches no engine memory. MAIN THREAD.
int WorldsyncPeerGone(int slot)
{
    for (int i = 0; i < g_adoptedCount && i < kMaxAdopted; ++i)
        if (g_adoptedRows[i].uid != 0) g_announced[g_adoptedRows[i].uid] = 0;
    for (std::map<unsigned int, int>::iterator it = g_announced.begin(); it != g_announced.end(); ++it)
        it->second = 0;

    g_reannounceAt = -1;
    g_linkWasUp    = false;
    /* W1-b: the SESSION peer is gone, so every area it was carried as holding in the effective map goes with it. This is the only
       caller besides PLAYER_GONE (WorldsyncPlayerGone, M8): a notebook link-down does NOT forget it - the peer did not go anywhere when the notebook did (T240), and
       forgetting there would read it as holding nothing on the restarted notebook's first map. (M2 deleted the relay-absent
       HostNote path; the map is carried unchanged through a notebook outage - the freeze rule in AnnouncePass.) */
    HandoffRevokesPlayerGone(slot);   /* fold 2 [a1b2f2-ws0] [review G2]: the REVOKEs waiting from the departed peer and owed to it drop */
    HandedOverForgetPlayer(slot);   /* the squads handed to the departed link peer leave the squad index (every squad while its slot is unknown) - drained on the main thread (handoff.cpp) */
    coop::ZonesForgetEffectiveMask(slot);   /* M8: only the departed link peer's bit when its slot is known; every bit, as before, while it is not */

    const int marks = (int)g_announced.size();
    DebugLog("[P033] peer gone: " + S((long long)marks) + " uids marked NOT announced (the rows"
             " themselves are ours and are kept); a re-announce starts on the next link-up edge");
    return marks;
}
/* M8: PLAYER_GONE {slot} - that player's loaded bit, its REVOKEs and the squads handed to it. The announce marks are the SESSION link's
   (they re-announce this game's own uids to the peer on that link), and a player leaving through the notebook says nothing about that
   link. Returns the bits armed. */
int WorldsyncPlayerGone(int slot)
{
    HandoffRevokesPlayerGone(slot);   /* fold 2 [a1b2f2-ws1] [review G2]: the REVOKEs waiting from that player and owed to it drop */
    HandedOverForgetPlayer(slot);     /* the squads this game handed to that player are this game's again; every other player's stay handed */
    return coop::ZonesForgetEffectiveMask(slot);
}
/* M8 review F2: THE SESSION LINK'S DOWN EDGE clears the link-up latch at once (net/session.cpp SessionOnLinkDown). WorldsyncPeerGone
   clears it too, but it runs from the QUEUED peer-gone action - and a drop + reconnect before the drain discards that action, so
   WorldSyncTick read UP on both sides of the gap and never re-announced, while the other game's sweep of that discarded action had
   removed its copies of our characters: they stayed gone for the session. Cleared at the edge, every reconnect is a link-up edge here
   and re-announces. The announce marks stay WorldsyncPeerGone's business. Touches no engine memory. MAIN THREAD. */
void WorldsyncOnSessionLinkDown() { g_linkWasUp = false; }

} // namespace coop

