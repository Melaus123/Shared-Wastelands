// effect.h - T-327 first slice: eating another game's character is a request to its owner (MSG_EFFECT 65, protocol 111).
// A sneak knockout (Task_StealthKO) of another game's character rides the same message (kind 3 KO).
// See effect.cpp and src/common/effectwire.h.
#pragma once

#include <string>
#include <cstddef>

namespace coop {

void InstallEffectHook();          // Character::gettingEaten (row Character_beingEaten) and the sneak knockout's task update and chance - from InstallCombatHook
void EffectTick();                 // MAIN THREAD, every frame: the eater's sends and stop rules, the owner's pair expiry, queued answers, the sneak knockout's slots
void EffectSafePointDrain();       // K2 safe point, AFTER the engine's ragdoll pass (MAIN THREAD, worker paused): the owner's bites, the eatbite lever, the owner's sneak knockouts
void EffectOnNet(const char* p, size_t n, unsigned int fromKey, bool viaRelay);   // MAIN THREAD: net/session.cpp OnEffect
void ReportEffect();               // the [EFFECT] REPORT line
// ANY THREAD (the knockout gate, medical.cpp): the request id when `ret` is the sneak knockout's own knockout call and this game
// holds a KNOCKED OUT outcome for that victim (its owner applies the knockout; the copy falls on the owner's STATE); 0 otherwise.
unsigned int EffectKoAwaitingReq(unsigned long long ret, unsigned int victimUid);
// MAIN THREAD (spawn.cpp, the owner's STATE on a copy): true while a sneak knockout of this copy waits for its verdict or the
// engine's task has not read it yet - the copy's knockdown waits meanwhile (the task ends without text or experience when it finds
// its victim down).
bool EffectKoHoldsKnockdown(unsigned int victimUid);
// TEST-ONLY lever `eatbite <eaterUid | animal> <victimUid> <seconds>`: victim->gettingEaten(1.0, eater) THROUGH the hooked
// entry once per frame at the K2 safe point for <seconds> (1..120), or until the call answers 1. MAIN THREAD (command channel).
std::string EatBiteLever(const std::string& eaterArg, unsigned int victimUid, float seconds);
// appearance.cpp (P10's animal test): the nearest living, standing animal this game DRIVES within radiusM of the anchor; 0 none.
unsigned int NearestOwnedAnimal(unsigned int anchorUid, float radiusM, float* distSqUnits);

} // namespace coop
