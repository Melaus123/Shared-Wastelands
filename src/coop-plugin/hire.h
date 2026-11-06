// hire.h - recruit1 R0-a (docs/design-recruit1.md): hiring a recruit the OTHER game runs. The game that runs the person
// confirms (first request wins); on OK the hirer's game takes the person over (handoff's receiver pair) and hires it with the
// engine's own PlayerInterface::recruit, then pays; on DONE the owner sets its character to the peer stand-in faction in the
// hirer's squad and releases it (handoff's ACK side). R0-b: the detour on Dialogue::_doActions 0x67FAD0 HOLDS a player's hire
// reply on a person the other game runs (the line is not run, endDialogue closes the chat, the same REQ as the lever); the
// TEST-ONLY lever `hiretest near [price] | near same [<x> <z> [price]] | uid <n> [price]` drives the same flow.
#pragma once
#include <string>
#include <vector>
#include "../common/hirewire.h"
#include "../common/givewire.h"   /* T-1135: giving characters - ButtonView, GiveMsg */
class Character;   // T-556: HireRecruitNoEdit / HireSetFaction
class Faction;
namespace coop {
void InstallHire();                                                       // the 0x67FAD0 detour (R0-b hold)
void HireNoteRecv(const coophire::HireMsg& m, unsigned int fromPeer);    // MAIN THREAD (dispatch): queued for the K2 safe point
void HireNoteBad();                                                       // a MSG_HIRE that did not decode
void HireSafePointDrain();                                                // MAIN THREAD, worker paused (combat.cpp K2)
void HireJoinAskRecv(unsigned int uid, unsigned int freerUid, unsigned int fromPeer);   // T-980, MAIN THREAD: the freer's game - the prisoner's owner asks it to hire the prisoner (queued for the safe point)
void HireJoinNoRecv(unsigned int uid, unsigned int freerUid, int reason, unsigned int fromPeer);   // T-980, MAIN THREAD: the prisoner's owner - the freer's game did not ask; the ask ends
void HireForgetPeer(int slot);                                            // the player in `slot` left: my requests to it, my promises to it and its undrained messages dropped (-1: every row)
// T-1135 giving characters (src/common/givewire.h), MAIN THREAD. GiveButtonRead: the PLAYERS tab's GIVE button for the player in
// receiverSlot - this player's own characters in the world's selection, read live (their uids and names come back; returns how many).
// GiveOfferArm: an offer for those uids, sent at the next safe point after the same checks. GiveOfferWaiting / GiveOfferStillWaiting:
// the first offer waiting here for its box / whether that one still waits. GiveOfferAnswer: the box's ACCEPT / REFUSE (1 answered, 0 not).
// GiveNoteRecv: a decoded give message (net/session.cpp OnHire), queued for the safe point. GiveTestArm: the TEST-ONLY lever
// `givetest offer <slot> <uid> [<uid>..] | givetest answer accept|refuse | givetest show [<slot>]` ([GIVE] lines).
int  GiveButtonRead(int receiverSlot, swgive::ButtonView* view, std::vector<unsigned int>* uids, std::vector<std::string>* names);
void GiveOfferArm(int receiverSlot, const std::vector<unsigned int>& uids, const std::string& from);
bool GiveOfferWaiting(unsigned int* offerId, int* giverSlot, std::vector<std::string>* names);
bool GiveOfferStillWaiting(unsigned int offerId, int giverSlot);
int  GiveOfferAnswer(unsigned int offerId, int giverSlot, bool accept);
void GiveNoteRecv(const swgive::GiveMsg& m, unsigned int fromPeer);
std::string GiveTestArm(const std::string& arg);
std::string SquadTestArm(const std::string& arg);                         // `squadtest add <ownUid|first> <n> [templateSid]` (TEST-ONLY): armed for the K2 safe point
std::string HireTestArm(const std::string& arg);                          // `hiretest [editor] near [price] | [editor] near same [<x> <z> [price]] | [editor] uid <n> [price]` (TEST-ONLY; editor = recruit with the character editor)

std::string HireTestArm(const std::string& arg);                          // `hiretest [editor] near [price] | [editor] near same [<x> <z> [price]] | [editor] uid <n> [price] | [editor] nearestpeer [<radius>]` (TEST-ONLY; editor = recruit with the character editor; nearestpeer = the nearest character of another player, T-1022)
void OwnSquadUids(std::vector<unsigned int>* out);                        // MAIN THREAD: every character this game drives in its player faction (the test levers' squad)
void ReportHire();                                                        // one [HIRE] REPORT line
unsigned int HireLastBoughtUid();                                         // T-1228 TEST-ONLY: the newest uid a T478 BOUGHT confirm put in this game's player faction (0 = none this session)
int HireRecruitNoEdit(::Character* c);                                   // T-556: PlayerInterface::recruit(c, edit=false); 1 joined, 0 refused, <0 not called / fault
int HireSetFaction(::Character* c, ::Faction* f, void* platoon);         // T-556: Character vt+0xA0 setFaction; 1 called, 0 fault
std::string CopySquadNearestLever();                                       // `copysquad nearest` (TEST-ONLY): the nearest copy within 2000 u, as copysquad <uid>
std::string CopySquadLever(unsigned int uid);                            // `copysquad <uid>` (TEST-ONLY): a copy moved into a new squad this game numbers
std::string SplitSquadArm(const std::string& arg);                        // `splitsquad [n]` (TEST-ONLY): arms moving the last n members of the first player squad into a new player squad
std::string EditorStateArm(const std::string& arg);                       // `editorstate [watch <N>]` (TEST-ONLY, read-only): the character editor's state at the safe point
void EditorStateTick();                                                   // MAIN THREAD, worker paused (HireSafePointDrain): writes an armed / watched [EDITOR] state line
void SplitSquadDrain();                                                   // MAIN THREAD, worker paused (combat.cpp K2): runs an armed splitsquad, writes the result to the status file
}
