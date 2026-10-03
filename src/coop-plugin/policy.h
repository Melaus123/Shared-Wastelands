// policy.h - E36 / decision 40 (approved 2026-09-04): THE BASE ACCESS POLICY.
//
// Decision 40 splits two things this project used to run together:
//   AUTHORITY - which game APPLIES a change to a building. Decision 38 part 3: the game holding that patch of
//               map, never the owner, so a base stays usable after its owner logs off. Built in P6e (E22c).
//   ACCESS    - who is ALLOWED to make the change. That is this file, and it is a hosting option on the whole
//               session rather than a property of any one building.
//
// The three modes, verbatim from decision 40:
//   shared - any player may use any player-built box. The co-op default.
//   owner  - only the owning player's faction. Compared BY THE FACTION'S STRING ID, because the engine's own
//            ownership test (Faction+0x250 `isPlayer`) answers "a player faction" and cannot tell two player
//            factions apart at all (build/read-locks.md 1.5).
//   locked - the owner may use it; every other player falls through to THE ENGINE'S OWN lock-and-pick
//            behaviour, which is what a stranger's box already does in single-player. No plugin refusal.
//
// WHO HOLDS THE VALUE. The notebook process (SharedWastelandsServer.exe, options.txt) - see store.h's StoreSendOption and
// store.cpp's MSG_OPTIONS handler, which is the ONLY writer of it on this side.
#pragma once
#include <string>

namespace coop {

// The option, as this game currently knows it. kPolicyUnknown is NOT a synonym for kPolicyShared: it means no
// notebook process has said anything yet, and the first request judged under it says so in the log.
enum { kPolicyUnknown = 0, kPolicyShared = 1, kPolicyOwner = 2, kPolicyLocked = 3 };

// PRELOAD, MAIN THREAD: the three hooks `shared` needs - OwnedByAPlayerFaction 0x546340 (what a CLICK does)
// and the two getMouseCursor bodies 0x546C30 / 0x547010 (what the CURSOR says it will do). The first is scoped
// to UseableStuff::getDefaultTask's own two call sites by return address; the other two are pure return-value
// remaps. See policy.cpp for the addresses, the enum values and where each came from.
void InstallPolicy();
// MAIN THREAD, from playerfaction.cpp the moment the `coop-peer` stand-in faction exists (created or reused).
// Caches the pointer the hook compares against and emits PROBE P028 once.
void PolicyNotePeerFaction(void* faction);
// ANY CALLER. `building` is a Building* (a UseableStuff for a storage box); `requesterFactionSid` is the
// faction string id of whoever is asking, as THIS game knows it - use PolicyRequesterSid to get it.
bool BasePolicyAllows(void* building, const std::string& requesterFactionSid);
// The requester's faction string id.
//
// WHO IS ASKING IS A PROPERTY OF THE CALL PATH, NEVER OF A NUMBER (review-p6h HIGH-1). `local` is true only on
// this game's own pre-send check; every request that arrived over the wire passes local=false and the peer id
// it came from. The old signature read `uid == 0` as "me", but the transport gives peer id 0 to THE HOST
// (net/transport.h:127, net/enet_transport.cpp:173), so on a client game every request from the host was being
// judged as if the local player had made it - `owner` and `locked` did not restrict the host there at all.
// `requesterPeer` is carried for the log and for the day there is a third player; today every remote requester
// is the one `coop-peer` stand-in, because that is the only peer identity this game's zone translator mints.
std::string PolicyRequesterSid(unsigned int requesterPeer, bool local);
// P028's answer: 1 = the stand-in IS a player faction (so `locked` cannot lean on the engine and refuses at the
// plugin level instead), 0 = it is not, -1 = never observed.
int PolicyPeerIsPlayer();
// MAIN THREAD: the engine has torn the world down and freed the factions - forget the stand-in's address and
// re-arm P028 for the next world.
void PolicyForgetPeerFaction();
// PROBE P029 (review-p6m HIGH-1's open leg): WHICH CONCRETE CLASS a building is, so a run can say whether a
// player's storage box is a StorageBuilding (whose getDefaultTask is the constant 0x1A, with no ownership term
// in it at all - so the `shared` click gate never applied to one) or a UseableStuff (the one class the gate
// does reach). See policy.cpp for the vtable table these answers are read against.
//
// ANY THREAD, both of them; neither allocates, locks or calls an engine function.
//   PolicyVtableRvaPod   TOUCHES THE OBJECT - call it only where the building is known live (a detour, not a
//                        deferred drain). Returns the vtable pointer minus the module base, or 0 if that could
//                        not be read or does not lie inside this module.
//   PolicyClassNamePod   TOUCHES NOTHING BUT THE IMAGE - it walks the RTTI from the RVA, so it is safe to call
//                        long after the building is gone. Writes `.?AV<ClassName>@@` into `buf` and returns 1,
//                        or leaves `buf` empty and returns 0 when the walk is not cheaply readable.
unsigned int PolicyVtableRvaPod(void* obj);
int PolicyClassNamePod(unsigned int vtRva, char* buf, int cap);
std::string PolicyReport();   // the " basePolicy[...]" tokens for the [STORE] REPORT line

}
