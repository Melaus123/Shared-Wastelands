// worldgen.h - P032. COUNT the engine's own world population generation, and (behind a switch)
// STOP it.
//
// WHY THIS EXISTS
//
// The user has chosen P-16 Option A: the peer stops generating its own world population and
// receives the authority's. Their reason: "a world that doesn't match isn't a multiplayer world."
//
// T078 priced the job and killed the cheap version of it. Only 12 of ~110 world characters carry
// the same handle on both machines (F265), so the save-derived population is NOT already shared and
// the bill is the whole world - roughly 100-130 characters against the 4-6 replicated today.
//
// Option A has two halves. This file is the FIRST half: can the peer's generator be switched off
// at all? `docs/P16-world-population-decision.md` named that as the precondition, and said flatly
// that if it cannot be done then Option A does not exist.
//
// PROBE BEFORE GATE. Nothing here changes behaviour by default. The counters run first so that the
// gate, when it is switched on, is measured against a known baseline rather than against an
// assumption - the same shape as `medgate` and `combatpulse`, both of which earned it.
//
// WHERE THE HOOKS ARE, AND WHY THESE AND NOT OTHERS
//
// F263 corrected an earlier mistake worth repeating here: `RootObjectFactory::create` is where a
// creation is REQUESTED, not where it happens - `RootObjectFactory::process` is, and
// `mainThreadUpdate` drains a deferred queue straight into `process`, bypassing `create` entirely.
// `process` has no address of its own here. Hooking its public callers is the available route.
//
// Four functions, all public, all resolvable:
//
//   createRandomCharacter    - THE character creator. Every world character passes through it,
//                              including the six calls `createRandomSquad` makes per squad, so
//                              counting here also counts squad members without hooking the
//                              15-argument squad function.
//   createCharacterForBuilding \  the town-population path: who ends up standing inside a
//   populateBuilding           /  building that has already been placed.
//   checkForRepopulateTown   - the town repopulation entry on ZoneManager.
//
// `Faction::_spawnASquad` is PROTECTED and therefore not hookable this way. Its members still get
// counted at `createRandomCharacter`, which is the leaf.
//
// WHAT IS GATED
//
// ALL FOUR, and `createRandomCharacter` is the one that matters.
//
// F268 mapped the call graph with the thunk-following Xrefs from F264: `createRandomCharacter` is
// the LEAF every world character passes through, and it has exactly two callers. F269 then read
// what those callers do with its result:
//
//   RootObjectFactory::createRandomSquad  - all SIX sites guard with `if (result != 0)`
//   ActivePlatoon::restoreSquad           - both sites guard (one `goto`, one `if`)
//
// Eight of eight. A null here is an outcome the engine's own code EXPECTS, not one it might merely
// survive - which is the difference between the single approach that has worked in this project
// and the four that were capped.
//
// An earlier version left this un-gated because the callers were unread and F117 shows the engine
// dereferencing such results unconditionally elsewhere. That was the right default then; the
// callers have been read now.
//
// The other three (`createCharacterForBuilding`, `populateBuilding`, `checkForRepopulateTown`) are
// gated only in `off` mode, as SEPARATE COUNTERS showing which routes were actually travelled.
//
// `checkForRepopulateTown` returns **FALSE** when suppressed. An earlier version of this comment
// said `true`, justified as "the value the engine's own early-out returns for already-handled,
// rather than a guess" - and that was exactly backwards (F272). The sole caller,
// `ZoneMapContent::_activate`, tests `if (... && result != '\0')` and ENTERS the repopulation work
// on non-zero. Returning `true` told the engine to repopulate, which is why T079's gated instance
// called `populateBuilding` 25 times while the ungated control called it 0.
//
// The lesson is worth more than the fix: **to know what a boolean return MEANS, read the CALLER.**
// A callee's early-out tells you when it returns a value; only the caller tells you what it causes.
//
// OUR OWN SPAWNS ARE UNAFFECTED, AND STRUCTURALLY SO:
//
//   createRandomCharacter -> RootObjectFactory::create      (the world's path, gated)
//   our own spawn         -> RootObjectFactory::create      (enters BELOW the gate)
//
// No "is this one ours?" test, no flag, no ordering assumption. It is a property of the call graph.
//
// WHAT THIS STILL DOES NOT ESTABLISH: eight guarded call sites say each individual null is handled.
// They do NOT say a world that creates NO characters still loads, streams and runs. Only a run
// answers that.
#pragma once

#include <string>   /* P18 fold 2: TownRepopCommand */

namespace coop {

// Install the four detours. Failure of any one is reported and is NOT fatal - a missing instrument
// must not take the rest down with it (the P026 rule).
void InstallWorldGen();

// Runtime switch. THREE states, because two were not enough (F273):
//   1 = on   - the engine generates its world exactly as it does today (default)
//   2 = leaf - ONLY `createRandomCharacter` refuses; the three upstream hooks run untouched.
//              THE CLEAN ARM, and the one Option A would actually ship.
//   0 = off  - all four refuse. The upstream three change WHICH code paths run rather than only
//              what they produce (F271), so a failure in this mode is harder to attribute.
// Keeping both suppressing modes is what lets one run compare them; a fix that cannot be switched
// off cannot be measured against its own absence.
void SetWorldGenMode(int mode);

// Counters into the [M6]/[P032] report line. Read-only.
void ReportWorldGen();

// P18 fold 2 - TEST LEVER `townrepop on|off|show`: while on, every town the engine asks about is rebuilt by its own path (any thread).
std::string TownRepopCommand(const std::string& arg);

// PROBE P072 (T263; REMOVE once the runtime-furniture question is answered - 04-probes.md).
void P072Tick();   /* MAIN THREAD, every frame: drains the building layout ring (worldgen.cpp) */

// P5c - MAIN THREAD, every tick. Refreshes the cached session flags the leaf gate reads from the
// engine's worker threads (net::SessionLinked/IsHost are virtual calls on a transport the main
// thread deletes - towngen.cpp:40).
void WorldGenTick();

// F322 - destroy a local object through the engine's own (hooked) path, so P038's detour runs on it
// and our removal is counted beside the engine's. MAIN THREAD. false = not hooked or no world, in
// which case the caller must NOT report the object as destroyed.
bool DestroyLocalObject(void* obj);

} // namespace coop
