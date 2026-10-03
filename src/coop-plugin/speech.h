// speech.h - P3 (read-parity3 GAP 3; the user saw it in T244): NPC speech bubbles are said by the game
// driving the character and shown on its copies; a copy no longer invents its own lines. See speech.cpp.
#pragma once

#include <string>

class Character;   /* recruit1: SayOnCopy */
namespace cooptalk { struct TalkMsg; }   /* P26 stages 1-3: src/common/talkwire.h */

namespace coop {

// Hook Dialogue::say 0x67F2F0 and (P3-b) Dialogue::sendEvent 0x683F00. Installed beside the other session
// hooks; a failure is logged in words.
void InstallSpeech();

// MAIN THREAD (the session drain): show `text` on the copy of `uid` through the ORIGINAL say(text, 0).
// Refused (counted) when there is no copy, the sender does not own it, or its Dialogue is not plausible.
void ApplyRemoteSay(unsigned int uid, const std::string& text, unsigned int fromPeer);
bool SayOnCopy(::Character* c, const std::string& text);   /* recruit1: a line said by this game's copy through the ORIGINAL say (the hire refusal). MAIN THREAD */

// An arriving MSG_SAY did not decode.
void SayNoteMalformed();

// P3-b (review-p3 LOW): a MSG_SAY arrived while EngineWritesBlocked() - dropped, never queued.
void SayNoteDroppedBlocked();

// "sent,tooLong,passOffThread,passNoUid,passReplay,localPuppetDropped,applied,unknownUid,noDialogue,malformed,
//  sendEventWouldBlock,dropClearedLine,replaySent,replayDropped,droppedBlocked sayHook=<state> sendEventHook=<state>"
std::string SayCountsString();
void ReportCrimeTest();   /* crime11: one [CRIMETEST] REPORT line - the lever's counters (P077 removed) */
/* crimetest (crime1, docs/design-crime.md s2) - a TEST-ONLY lever: `crimetest steal` arms one request (MAIN THREAD);
   CrimeTestDrain applies it at the K2 safe point (combat.cpp detour_tsRagdollUpdates, worker paused). The safe point runs
   only while a world is loaded: a request armed at the title screen waits for the next world's first safe point. */
std::string CrimeTestArm(const std::string& kind);
void CrimeTestDrain();

/* P26 stage 0 (.modding/investigations/p26-npc-dialogue.md) - MEASUREMENT ONLY. Log-only detours on the engine's two
   conversation starters (installed by InstallSpeech) log [TALK] lines and count starts by target kind and event.
   talktest [npcUid|near] [targetUid|near] [event] - a TEST-ONLY lever: armed here (MAIN THREAD), TalkTestDrain sends the
   event through Dialogue::sendEvent at the K2 safe point and logs what started. TalkCountsString: the [net] REPORT counters
   "talk[started,withCopy,withMine,windowWouldOpen,...]=... talkByEvent=[..] sendEventWouldBlockByEvent=[..] talkHooks=..". */
std::string TalkTestArm(const std::string& arg);
void TalkTestDrain();
std::string TalkCountsString();
/* P26 stage 6 talksight (TEST-ONLY levers, speech.cpp 'TALKSIGHT LEVER'). MAIN THREAD (the command channel).
   TalkSightArm: `talksight <npcUid | nearest <dialogueName>> <targetUid> [seconds] [walkover]` - checks and arms; TalkTestDrain tries
   Dialogue::sendEvent(event 3) at every K2 safe point. When the NPC is not loaded yet (the uid is not here / no carrier near the
   target) it WAITS for it to load, up to the command's own seconds (T718), instead of refusing at once.
   TalkCarriersCommand: `talkcarriers <x> <z> <radius>` (read-only).
   TalkNearPoint: `playerteleport near <uid | nearest <dialogueName>> <units> [maxdy <n>]` - 1 = the point <units> from that character toward
   (P26lvl maxdy: the point's terrain must be within n of the character's feet - the straight line first, then 16 points on
   the circle; none -> refused in words)
   this game's player (*x, *z); 0 = refused (*why); 2 = the character is not loaded yet: it WAITS up to 60 s for it to load (T718).
   command_channel.cpp moves the player there with PlayerTeleportAbs.
   TalkWaitTick: MAIN THREAD, CommandChannelTick - re-checks a pending wait every 500 ms; 1 = a near wait passed (*x, *z, the wait
   in *waitedMs): the caller teleports exactly as the command does. */
std::string TalkSightArm(const std::string& arg);
std::string TalkCarriersCommand(float x, float z, float radius);
/* The carried hand-in fixture's TEST-ONLY levers. MAIN THREAD.
   TalkCarriersActCommand: `talkcarriers <x> <z> <radius> [ev <n>] [act <type>]` (read-only) - the living non-player characters
   near (x,z) whose event-<ev> conversation list holds a line carrying dialogue action <act>; one [TALKSIGHT] line each.
   LeverNearestNamed: the nearest loaded character (2D) within maxDist of `from` (0 = this game's first own player character)
   whose display name keys as nameKey (coopsay::TalkPersonKey). kLeverPickOwnLiving: this game's own, living, non-player, with a
   uid (crimetest bountyset's person, koself name); kLeverPickAny: any (crimetest bountyset's law NPC); kLeverPickDowned: living
   and down (prone or ragdoll), either game's (capturetest carry name). *uid (0 = not replicated), *dist, *name = its display
   name; 0 = none (*why says so in words). */
const int kLeverPickOwnLiving = 0, kLeverPickAny = 1, kLeverPickDowned = 2;
std::string TalkCarriersActCommand(float x, float z, float radius, int ev, int act);
::Character* LeverNearestNamed(const std::string& nameKey, int pick, ::Character* from, float maxDist, unsigned int* uid, float* dist,
                               std::string* name, std::string* why);
int TalkNearPoint(const std::string& arg, float* x, float* z, std::string* why);
int TalkWaitTick(float* x, float* z, unsigned long* waitedMs);
/* P25 T729 (TEST-ONLY): `talktest nearestname <npc name> <targetUid> [event] [copy | mine]` (P25 T729b: copy / mine = only a copy
   the other game drives / only this game's own) - the nearest living, conscious non-player character
   with that name (case-insensitive, '_' = a space) to the target, picked when the command runs (waits up to 60 s for it to load,
   re-checked by TalkWaitTick), then exactly `talktest <uid> <target> <event>`. `playerteleport near name <npc name> <units>
   [maxdy <n>]` goes through TalkNearPoint. */
std::string TalkTestNameArm(const std::string& arg);

/* P26 stages 1-3 (.modding/investigations/p26-npc-dialogue.md Q3; speech.cpp 'P26 STAGES 1-3'): an NPC's conversation with the
   OTHER player's character opens THAT player's own dialogue window - MSG_TALK PROMPT / ANSWER / END.
   TalkNoteRecv: MAIN THREAD (the session drain) - queued, applied at the K2 safe point. TalkNoteBad: a MSG_TALK that did not
   decode. TalkForgetPeer: MAIN THREAD (OnPeerGone, before the peer's copies are dropped) - every open conversation ends here.
   TalkSkipDoActions: hire.cpp detour_doActions - true only for the mirrored NPC line's own actions during our apply (they ran on
   the NPC's game). TalkPromptArm: the TEST-ONLY lever `talkprompt ...` (command_channel.cpp). */
void TalkNoteRecv(const cooptalk::TalkMsg& m, unsigned int fromPeer);
void TalkNoteBad();
void TalkForgetPeer(unsigned int peer);   /* P26s1 fold 1 L7: that peer's conversations; kTalkAllPeers = every one (OnPeerGone) */
const unsigned int kTalkAllPeers = 0xFFFFFFFFu;
/* P26s1 fold 1 M3: hire.cpp's Dialogue::_doActions hook state (1 installed) - B shows a PROMPT only with it (the line's actions
   are skipped through it; without it they would run a second time). Defined in hire.cpp, namespace coop. */
int HireDoActionsHookState();
bool TalkSkipDoActions(void* dlg);
/* P26 stage 4: hire.cpp detour_doActions (MAIN THREAD) - true = a line of a conversation this game forwards to the other player
   carries a TARGET-side part - P26 stage 5: THE ROUTER (cooptalk::TalkActSide): its NPC-side part runs here, its target-side part
   goes to the other game as MSG_TALK ACT; true = handled (the original must NOT run). */
bool TalkDeferTargetActs(void* dlg, void* line);
/* P26s6 S6-3 (hire.cpp detour_doActions, ANY THREAD - the thread the engine runs the line on): TalkS6LeaderTarget, BEFORE the
   original - the other player's character this line's TALK_TO_LEADER (2) would send this game's NPC's squad leader to (0 none);
   TalkS6LeaderOrder, AFTER the original - gives that order exactly as the engine does for a player target. Defined in speech.cpp. */
void* TalkS6LeaderTarget(void* dlg, void* line);
void TalkS6LeaderOrder(void* dlg, void* target);
/* P26 stage 5 - defined in hire.cpp, namespace coop. HireCallDoActions: the engine's _doActions through the trampoline (1 ran,
   0 no trampoline, -1 faulted). HireForTalk: B - the other game's NPC hires itself into this game's squad through the
   owner-applied hire road (1 asked, 0 not: *why). MAIN THREAD. */
int HireCallDoActions(void* dlg, void* line);
int HireForTalk(unsigned int npcUid, unsigned int hirerUid, int price, int joinType, std::string* why);
std::string TalkPromptArm(const std::string& arg);

} // namespace coop
