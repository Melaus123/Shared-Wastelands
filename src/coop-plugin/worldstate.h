// worldstate.h - E5 / decision 31(c): the unique-NPC state map is the whole of "what has happened in this world"
// in 1.0.65 (docs/persistence-service.md 24; engine facts in .modding/03-systems/world-states.md).
//
// This game watches the engine's own writers of that map, queues every change it makes itself to the main thread,
// and sends it to the notebook process as UNIQUE_STATE. A state arriving from the notebook is written back through
// the engine's own setter, behind a re-entrancy flag so the write does not bounce straight out again.
//
// ---- THE RULES (E20, amended by E27; the long form and the engine facts are in worldstate.cpp's header) ----
// A named character can be loaded on BOTH games at once, so every rule here turns on ONE question, asked on the
// main thread: which of FOUR things is the character carrying this record?
//
//   OURS      - we own the uid. We are the only game allowed to PUBLISH its state. A received DEAD kills it
//               through the engine's own death call; a received alive/imprisoned is refused and RE-ASSERTED.
//   A TWIN    - this save's OWN character, registered under the peer's uid by AdoptExistingTwin because both
//               players run the same save (spawn.h IsTwinUid). It is NOT a copy somebody else built: it is the
//               SAME INDIVIDUAL. It never publishes (the owner speaks for it), but a received DEAD DOES kill it,
//               because the owner's death of that individual is the truth about that individual.
//   A GHOST   - a body the spawn factory built here for the peer's character. It never publishes and a received
//               DEAD NEVER kills it: the owner's own character stream does not carry death to it, so a kill here
//               would put a corpse on this game the owner never made. Its table entry is written; its body is not.
//   NOTHING   - no loaded character carries the record (a squad unload, a respawn clear). Nobody is left to ask,
//               so the shadow's last-known-owner flag decides, and "never established" means do not publish.
//
// E27 amends three things. A RE-ASSERT IS DAMPED: never the state this game last sent for that id, and at most one
// per id per 5 s - the hand-off deliberately leaves both games owning one uid for a round trip, and without a
// damper the two re-assert reflexes answer each other at link speed. THE OWNED-HERE FLAG IS CLEARED when this game
// releases ownership (WorldStateOnOwnershipReleased above). AN APPLY REFRESHES THAT FLAG from the classification -
// ours 1, twin or ghost 0, nothing loaded unchanged - including the apply whose state already matches, which is
// the commonest one once two games have converged and previously established nothing at all.
#pragma once
#include <string>
namespace coop {
void InstallWorldState();     // preload, main thread: the FIVE hooks (declareDead, uniqueStateUpdate, the squad-unload writer, the shared setter, and the respawn clear)
void WorldStateTick();        // MAIN THREAD, every frame: drains the detours' ring, reads the state map (E19: the only reader) and sends what changed
void WorldStateWorldTeardown();   // MAIN THREAD, from the store's teardown broadcast: the shadow, the register and the ring all name the world being destroyed
void ApplyRemoteUniqueState(const std::string& sid, int state, int playerInvolved, unsigned int back);   // MAIN THREAD: a state from the notebook; back = how many times the world server says that character was brought back

// T-556 (owner 493) - MAIN THREAD: this game has just brought a named character back (resurrect.cpp, right after the new
// character exists and the TAKE went to the world server). The map entry, which reads DEAD, is written ALIVE at once the way the
// engine's own periodic update writes ALIVE into an existing entry, and until the world server confirms the bring-back (its
// UNIQUE_STATE with back > 0) a DEAD it sends for that id does not kill the character here. Returns a line for the log.
std::string WorldStateBroughtBack(const std::string& sid);

// E27 / review-p5q HIGH-3 - MAIN THREAD: this game has just given a character's ownership away (handoff.cpp's ACK
// path, immediately after net::ReleaseLocalOwner). The shadow's "we owned this record" flag is the ONLY thing the
// send gate has left to go on once the character is no longer loaded here, and nothing else can put it back to 0
// in time: after the hand-off the character is still loaded here as a puppet, so only a later rung entry that
// reaches the drain before the engine unloads the squad would demote it - a race the hand-off exists BECAUSE we
// are losing. Losing it meant publishing for a character the peer now owns. `character` is the Character*; the
// shadow row is found by its game-data record (Character+0x40), which is what the state map is keyed by. A
// character with no shadow row, or one that never claimed ownership, is a no-op.
// E32 (verify-p5u MEDIUM-1): BY UID FIRST. `character` is FindSpawned's answer, and FindSpawned returns 0 for a
// RETIRED row - which is the state of every squad the engine's unload countdown is already running on, i.e. exactly
// the squads a hand-off is about. The clear then did nothing and the flag stayed set. The uid is carried by the ACK
// itself, so it is passed straight in and the row is found through the record this game last saw that uid carrying;
// the Character* stays as the fallback for a uid nothing has been recorded for.
void WorldStateOnOwnershipReleased(unsigned int uid, const void* character);
// E32 (verify-p5t MEDIUM-4): the set of ids the NOTEBOOK has named is not world state and does not go at a world
// teardown - but it does go when the notebook link drops, because the next notebook may be a different one with a
// different key list, and the first-sight rule reads that list to decide whether a record is news.
void WorldStateNotebookReset();
std::string WorldStateReport();   // "sent,recv,applied,unknownSid,unchanged" - THE DRAIN'S SENDS ONLY (E27 gave the apply's re-assert its own counters): the wire total for unique states is sent + reassertSent, and a pre-E27 run's `sent` is not comparable with a later one's   // the [STORE] REPORT token
std::string WorldStateDetail();   // the writer/ring detail that follows it
}
