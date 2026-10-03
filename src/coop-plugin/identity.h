// identity.h - cross-instance identity for objects WE DID NOT SPAWN.
//
// Why this exists (parity register P-2, the project's highest-priority parity gap):
//
//   When a hit arrives whose ATTACKER is not one of our replicated characters, we could
//   not play it. The engine dereferences the attacker unconditionally (F117), so passing
//   null kills the receiving instance - and the only safe option left was to drop the
//   visual and apply the health silently. On the peer's screen a character then takes
//   damage with NOTHING VISIBLY HITTING THEM, in 1-23 of every ~50-110 hits (49% in T038).
//
//   The compensation was to accept that. This module exists to try NOT to.
//
// The insight this rests on: both instances load the SAME SAVE, so the ambient NPC that
// threw the punch very probably EXISTS ON BOTH MACHINES ALREADY. It has no uid because our
// uid mirror only covers characters we created - not because the character is missing. The
// problem was never replication, it was ADDRESSING.
//
// The engine already has a cross-process name for such an object: its `hand` - an
// {index, serial, type, container, containerStamp} reference resolved through the handle
// registry. Save-loaded objects are re-registered on load through `addExisting(o, handle)`,
// i.e. with the handle taken FROM THE SAVE rather than freshly minted, so the same
// character in two processes that loaded the same save carries the same handle.
//
// Evidence for that, from logs we already had (no game run spent - F110's rule):
//   T026 pid A : CAPTURED (save-loaded) handle='1-3182251008-1-3796020480-1'
//   T028 pid A : CAPTURED (save-loaded) handle='1-3182251008-1-3796020480-1'
// Two separate process launches, identical handle, and the serial is a large random-looking
// value rather than a small counter - which is what a value read from a file looks like, and
// is NOT what `nextRecordId()` produces (factory spawns in the same logs got 86059568, 973999040,
// 3045166080 - different every launch).
//
// WHAT THIS DOES NOT CLAIM. Objects the world creates at RUNTIME (wandering squads, town
// population spawned by the faction managers) get their handles from `nextRecordId()`, and those
// are allocation-order dependent - two instances need not agree. So this is expected to
// resolve SOME attackers and not others, and every call site therefore counts both outcomes
// separately. The fraction it closes is a measurement, not an assumption.
//
// SAFETY. `Capture` is called from the combat detour on the AI worker thread, so it does
// nothing but read five aligned integers out of the object - no allocation, no locks, no
// engine calls that could take one. `Resolve` runs on the main thread and goes through the
// engine's OWN registry lookup, which checks the serial and hands back null for a stale or
// mismatched handle - that serial check is what stops a recycled index resolving to the
// wrong character, and it is the engine's guarantee, not ours.
#pragma once

#include <string>

class Character;

namespace coop {

// The engine's `hand`, flattened to five integers so it can travel on the wire and be
// stored in the lock-free hit queue. All-zero means "no identity captured".
struct ObjId
{
    unsigned int index;
    unsigned int serial;
    unsigned int type;             // itemType; CHARACTER == 1
    unsigned int container;
    unsigned int containerStamp;
};

// True if anything was captured. A null/empty handle is a legitimate answer, not an error.
bool ObjIdValid(const ObjId& id);

// Read a character's embedded handle. SAFE ON A DETOUR THREAD: five aligned loads behind a
// pointer guard, nothing else. Returns false and zeroes `out` if the object is unreadable.
bool CaptureObjId(const void* character, ObjId* out);

// Resolve an identity captured on the OTHER instance to a local character. MAIN THREAD.
// Returns 0 when this instance has no such object - which is a normal outcome (the object
// may be runtime-created and thus unmatched, or simply not streamed in here).
::Character* ResolveObjId(const ObjId& id);

// 'index-serial-container-containerStamp-type', the same shape the engine's own
// hand::toString() prints, so a log line here can be diffed against a P009 line.
std::string ObjIdString(const ObjId& id);

} // namespace coop
