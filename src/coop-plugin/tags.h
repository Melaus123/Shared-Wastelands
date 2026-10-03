// tags.h - the name tag over each visible copy of ANOTHER player's character: that player's name over their faction's name,
// coloured by how this game's player faction and theirs stand (src/common/nametag.h). The mod's own MyGUI text labels,
// projected from the character's world position every in-game frame (.modding/investigations/nameplates.md option C).
#pragma once
#include <string>

namespace coop {
// MAIN THREAD (spawn.cpp ApplyRemoteSpawn, both branches): a copy now exists for `uid`. It is REGISTERED for a label
// only when it stands in a player's stand-in faction (replicate.cpp PeerFactionPod == 1) - another player's own characters;
// NPC copies keep their own world factions and are never labelled.
void TagsNoteCopy(unsigned int uid);
// MAIN THREAD (spawn.cpp RemoveLocalCopy): the copy is going - its label is destroyed now, not found later by a sweep.
void TagsForgetUid(unsigned int uid);
// ANY THREAD: something a tag shows may have changed - a PLAYERS roster arrived (store.cpp), a stand-in was created or renamed
// (playerfaction.cpp), a standing between this game's player faction and a stand-in moved (relations.cpp) - every label
// rebuilds its lines and colour on the next tick. An event, not a timer.
void TagsCaptionsDirty();
// ANY THREAD (spawn.cpp SpawnWorldTeardown): the world is going - every label is destroyed on the next main-thread tick.
void TagsWorldTeardown();
// MAIN THREAD, every in-game frame (coop.cpp detour_mainLoop): the toggle key, then position / hide each label.
void TagsTick();
// MAIN THREAD, title pump (coop.cpp): no world is loaded - any label still standing is destroyed.
void TagsTitleTick();
// The `tags on|off` verb (test lever). ON by default each session; nothing is persisted.
void TagsSetOn(bool on);
// The `tags lift <units>` verb: the height above the feet the label is drawn at (0 = back to automatic).
void TagsSetLift(float lift);
std::string TagsReportLine();   // "[TAGS] REPORT ..."
}
