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
// T-1262 (MAIN THREAD): a body of another game's character burned HERE in a corpse furnace by this game's carrier (spawn.cpp
// NotifyDespawn -> CopyBurnedHere): a "burn pending" row opens and the body's owner gets MSG_EFFECT kind 7 BURN (effectwire.h).
// false = no row (the caller books the lost copy, as before T-1262).
bool EffectBurnOpen(unsigned int body, unsigned int carrier, bool hasPos, float x, float y, float z);
// T-1262 (MAIN THREAD, net/session.cpp OnSpawn): true = a SPAWN of a body whose burn is pending - not built (counted, logged). Review
// fold 2: its owner still runs the body, so the request is sent again (cooffect::BurnRowStep), up to kBurnSpawnResendsMax times a
// row; the next one closes the row and books the lost copy (the re-ask of before T-1262).
bool EffectBurnHoldSpawn(unsigned int uid);
// T-1262 (MAIN THREAD, net/session.cpp LostBookUid): true = the body's burn is pending - not booked as a lost copy (counted).
bool EffectBurnHoldsLost(unsigned int uid);
// T-1262 (MAIN THREAD, net/session.cpp LostLook): true = the body's burn is pending - no RESEND ask (silent).
bool EffectBurnPending(unsigned int uid);
// T-1262 (MAIN THREAD, net/session.cpp OnDespawn, the owner's accepted DESPAWN): the body's burn-pending row closes.
void EffectBurnOwnerDespawned(unsigned int uid);
// appearance.cpp (P10's animal test): the nearest living, standing animal this game DRIVES within radiusM of the anchor; 0 none.
unsigned int NearestOwnedAnimal(unsigned int anchorUid, float radiusM, float* distSqUnits);

} // namespace coop
