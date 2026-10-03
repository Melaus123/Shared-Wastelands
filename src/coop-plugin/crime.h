// crime.h - crime3 (docs/design-crime.md section 3 A; H-crime-owner CONFIRMED by T275 / F894): a character's current crime
// travels from the game that drives it to every game that holds a copy, so THAT game's own witnesses react to it.
//
// T275 (Confirmed): a theft by B's character among A's NPCs got no reaction on either game - B's copies of A's NPCs never
// sense (their AI is gated) and A's copy of the thief carried no crime (the act writes it on B only). The owner now reads its
// characters' crime fields (Character +0x148 crime, +0x150 victim faction, +0x168..+0x178 victim hand, +0x180 expiry) and
// sends MSG_CRIME (src/common/crimewire.h) on a change; the other game writes it onto its copy through the engine's own
// BountyManager::notifyCrimeWitnessed 0x851F40 (a cleared crime through setCrime 0x851DB0 with crime 0) at the K2 safe point
// (combat.cpp detour_tsRagdollUpdates, the AI worker paused), then its witnesses sense the copy as in single player.
//
// crime5 (3 C): a bounty a guard adds to a COPY goes to the owner, and the owner's list is written onto every copy (below).
// Not in this step (design-crime.md 3 D and section 4): arrests of
// a character another game drives are not carried (spawn.cpp K1 v2 gap); a crime whose victim is a PLAYER faction (one player
// stealing from the other) is not sent - decision 4 is the user's.
#pragma once

#include <string>

#include "../common/bountywire.h"   /* crime5: coopbounty::BountyList */

namespace coopcrime { struct CrimeState; }

namespace coop {

// MAIN THREAD, from the command-channel pump (~4 Hz). The owner's sender: each owned, announced character's crime fields
// are sampled (at most kCrimeScanPerTick a call) and a change goes out as MSG_CRIME. Does nothing while EngineWritesBlocked().
void CrimeTick();

// MAIN THREAD (the session drain). A decoded MSG_CRIME: checked against the sender's ownership and queued (latest per uid
// wins) for CrimeApplyDrain.
void ApplyRemoteCrime(unsigned int uid, const coopcrime::CrimeState& s, unsigned int fromPeer);
void CrimeNoteMalformed();
void CrimeNoteDroppedBlocked();

// crime5 (docs/design-crime.md 3 C): MAIN THREAD (the session drain). A decoded MSG_BOUNTY: kind 0 = the owner's whole list for
// a copy here (accepted only from the uid's owner), kind 1 = what a guard on the other game added to its copy of a character
// THIS game drives. Queued for the K2 safe point, where CrimeApplyDrain writes it and samples the maps it sends.
void ApplyRemoteBounty(unsigned int uid, unsigned char kind, const coopbounty::BountyList& list, unsigned int fromPeer);
void BountyNoteMalformed();
void BountyNoteDroppedBlocked();
// par20 TEST-ONLY lever (`crimetest bountyclear <uid>`, MAIN THREAD): arms one erase of every non-player bounty on character
// <uid> as this game sees it, done at the next K2 safe point - on the NON-owner's game a copy-side clear, which must reach
// the owner (MSG_BOUNTY kind 2) and stay cleared after the owner's next list.
std::string BountyTestClearArm(unsigned int uid);
// TEST-ONLY lever (`crimetest bountyset <personName> <lawNpcName> <amount>`, MAIN THREAD; names already keyed by
// coopsay::BountySetParse): arms one bounty write on this game's own person, done at the next K2 safe point (crime.cpp).
std::string BountyTestSetArm(const std::string& personKey, const std::string& lawKey, int amount);

// MAIN THREAD, worker paused: the K2 safe point (combat.cpp). Writes the queued crimes onto this game's copies.
void CrimeApplyDrain();
// M7b slice 4 fold 1 (F5), MAIN THREAD (net::SendSpawn): a character's crime state - the last one sent, with its latest sampled
// expiry - follows each SPAWN of it on the SPAWN's own road, so a game that gets its copy later gets its crime too. None: nothing.
void CrimeSendWithSpawn(unsigned int uid);

// pvp1: MAIN THREAD, at plugin start beside the other hooks - BountyManager::setCrime refuses a crime that a player's
// character commits against a player's faction (user decision 2026-09-24: no automatic reaction between players).
void InstallCrime();

// One [CRIME] REPORT line.
void ReportCrime();

} // namespace coop
