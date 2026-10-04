// hire.h - recruit1 R0-a (docs/design-recruit1.md): hiring a recruit the OTHER game runs. The game that runs the person
// confirms (first request wins); on OK the hirer's game takes the person over (handoff's receiver pair) and hires it with the
// engine's own PlayerInterface::recruit, then pays; on DONE the owner sets its character to the peer stand-in faction in the
// hirer's squad and releases it (handoff's ACK side). R0-b: the detour on Dialogue::_doActions 0x67FAD0 HOLDS a player's hire
// reply on a person the other game runs (the line is not run, endDialogue closes the chat, the same REQ as the lever); the
// TEST-ONLY lever `hiretest near [price] | near same [<x> <z> [price]] | uid <n> [price]` drives the same flow.
#pragma once
#include <string>
#include "../common/hirewire.h"
class Character;   // T-556: HireRecruitNoEdit / HireSetFaction
class Faction;
namespace coop {
void InstallHire();                                                       // the 0x67FAD0 detour (R0-b hold)
void HireNoteRecv(const coophire::HireMsg& m, unsigned int fromPeer);    // MAIN THREAD (dispatch): queued for the K2 safe point
void HireNoteBad();                                                       // a MSG_HIRE that did not decode
void HireSafePointDrain();                                                // MAIN THREAD, worker paused (combat.cpp K2)
void HireForgetPeer(int slot);                                            // the player in `slot` left: my requests to it, my promises to it and its undrained messages dropped (-1: every row)
std::string HireTestArm(const std::string& arg);                          // `hiretest near [price] | near same [<x> <z> [price]] | uid <n> [price]` (TEST-ONLY)
void ReportHire();                                                        // one [HIRE] REPORT line
int HireRecruitNoEdit(::Character* c);                                   // T-556: PlayerInterface::recruit(c, edit=false); 1 joined, 0 refused, <0 not called / fault
int HireSetFaction(::Character* c, ::Faction* f, void* platoon);         // T-556: Character vt+0xA0 setFaction; 1 called, 0 fault
std::string CopySquadNearestLever();                                       // `copysquad nearest` (TEST-ONLY): the nearest copy within 2000 u, as copysquad <uid>
std::string CopySquadLever(unsigned int uid);                            // `copysquad <uid>` (TEST-ONLY): a copy moved into a new squad this game numbers
}
