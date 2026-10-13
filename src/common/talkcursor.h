/* src/common/talkcursor.h - T-737: THE TALK CURSOR OF ANOTHER GAME'S NPC FOLLOWS THAT GAME'S ANSWER (speech.cpp 'TALK CURSOR').
 *
 * The hover on a person picks the cursor with Dialogue 0x679FC0 (PlayerInterface::characterSelected through 0x67A170 / 0x67A190), and
 * the same test decides whether the click gives the talk order. For an NPC another game drives, that game's NPC is the one that talks,
 * so the hover answers with that game's last answer for this talker (MSG_TALK CURSOR_ASK / CURSOR_ANSWER, talkwire.h) and the copy's
 * own line pick is never used. The decisions are here, pure, so the offline suite (src/coop-test/test_main.cpp) tests the same code.
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include "talkwire.h"

namespace cooptalk {

inline const char* TalkCursorAnswerName(int a)
{
    return a == kTalkCursorNo ? "no" : (a == kTalkCursorTalk ? "talk" : (a == kTalkCursorParked ? "talk(parked)"
         : (a == kTalkCursorUnknown ? "unknown" : "?")));
}

/* 0x679FC0's tests after its parked-line test: 1 = the NPC may talk now (its line pick decides), 0 = it never talks now - no event-1
   ("the player talks to me") conversation list, dead, or of a player faction. */
inline int TalkCursorMayTalk(int hasList, int dead, int playerFaction)
{
    return (hasList && !dead && !playerFaction) ? 1 : 0;
}

/* The NPC's game, in 0x679FC0's order: a parked interrupt line first, then the never-talks tests, then the line pick (lineFound). */
inline int TalkCursorNpcAnswer(int parked, int hasList, int dead, int playerFaction, int lineFound)
{
    if (parked) return kTalkCursorParked;
    if (!TalkCursorMayTalk(hasList, dead, playerFaction)) return kTalkCursorNo;
    return lineFound ? kTalkCursorTalk : kTalkCursorNo;
}

/* The hover here, on an NPC another game drives: what 0x679FC0 answers and why. */
const int kTalkHoverFiltered = 0;   /* false: it never talks now - nothing is asked */
const int kTalkHoverNoAnswer = 1;   /* false: no answer from the NPC's game yet - the ordinary cursor, never the copy's own pick */
const int kTalkHoverNo       = 2;   /* false: the NPC's game said it has nothing to say */
const int kTalkHoverUnknown  = 3;   /* as no answer: the NPC's game could not tell (the NPC or the talker's copy is not there) */
const int kTalkHoverTalk     = 4;   /* true: the talk cursor */
const int kTalkHoverParked   = 5;   /* true: the NPC holds a parked line there - the plain talk cursor here */
inline int TalkCursorHover(int mayTalk, int haveAnswer, int answer)
{
    if (!mayTalk) return kTalkHoverFiltered;
    if (!haveAnswer) return kTalkHoverNoAnswer;
    if (answer == kTalkCursorTalk) return kTalkHoverTalk;
    if (answer == kTalkCursorParked) return kTalkHoverParked;
    if (answer == kTalkCursorNo) return kTalkHoverNo;
    return kTalkHoverUnknown;
}
inline int TalkCursorHoverTrue(int h) { return (h == kTalkHoverTalk || h == kTalkHoverParked) ? 1 : 0; }
inline const char* TalkCursorHoverName(int h)
{
    switch (h)
    {
    case kTalkHoverFiltered: return "never (no ask)";
    case kTalkHoverNoAnswer: return "no answer yet";
    case kTalkHoverNo:       return "no";
    case kTalkHoverUnknown:  return "unknown (as no answer)";
    case kTalkHoverTalk:     return "talk";
    case kTalkHoverParked:   return "talk(parked)";
    default:                 return "?";
    }
}

/* What 0x679FC0 returns for the hover's decision h: false, true, or the engine's own answer (the copy's pick). click: characterSelected
   gives the order now (its click byte set) or the order menu is built - there an NPC with no answer yet (none, or unknown) gets the
   engine's own answer, as before the talk cursor existed: the click then becomes the P25 REQUEST and the NPC's game decides. A hover
   (the cursor only) with no answer yet is false - the ordinary cursor. */
const int kTalkUseFalse = 0, kTalkUseTrue = 1, kTalkUseEngine = 2;
inline int TalkCursorUse(int h, int click)
{
    if (h == kTalkHoverNoAnswer || h == kTalkHoverUnknown) return click ? kTalkUseEngine : kTalkUseFalse;
    return TalkCursorHoverTrue(h) ? kTalkUseTrue : kTalkUseFalse;
}

/* 1 = the hover asks the NPC's game now: a fresh hover of that NPC (FreshHover), or no answer at all for this (NPC, talker) - unless
   an ASK for that pair is already queued or waiting for its ANSWER. */
inline int TalkCursorShouldAsk(int fresh, int haveAnswer, int pending)
{
    if (pending) return 0;
    return (fresh || !haveAnswer) ? 1 : 0;
}

/* 1 = this characterSelected call is a fresh hover: another character than the last call's, or the same one not seen on the frame
   just before (the mouse left it - characterSelected is not called over empty ground - and came back). Frames: the mod's main-loop
   frame count. */
inline int TalkCursorFreshHover(int sameWho, long long lastFrame, long long nowFrame)
{
    return (!sameWho || nowFrame - lastFrame > 1 || nowFrame < lastFrame) ? 1 : 0;
}

/* The asker's bounds. A copy made here queues one preload ASK; preloads go at most kTalkCursorPreloadPerWindow per
   kTalkCursorPreloadWindowMs, so walking into a town sends its people's asks a few at a time. An ASK waits kTalkReqWaitMs for its
   ANSWER (at most kTalkCursorWaitCap waiting; the oldest goes first); the cache holds at most kTalkCursorCacheCap answers. */
const int kTalkCursorPreloadPerWindow = 4;
const unsigned long kTalkCursorPreloadWindowMs = 500;
const unsigned int kTalkCursorQueueCap = 256;
const unsigned int kTalkCursorPreloadCap = 1024;
const unsigned int kTalkCursorWaitCap = 64;
const unsigned int kTalkCursorCacheCap = 4096;

/* 1 = one more preload ASK may go now (counted in *sent), 0 = it waits. *winStart / *sent: the window's start and how many went in it.
   Wrap-safe: the 32-bit difference of GetTickCount values. */
inline int TalkCursorPreloadMay(unsigned long* winStart, int* sent, unsigned long nowMs)
{
    if (*sent <= 0 || (unsigned int)((unsigned int)nowMs - (unsigned int)*winStart) >= (unsigned int)kTalkCursorPreloadWindowMs) { *winStart = nowMs; *sent = 0; }
    if (*sent >= kTalkCursorPreloadPerWindow) return 0;
    ++*sent;
    return 1;
}

/* The cache key: one answer per (NPC, talker). */
inline unsigned long long TalkCursorKey(unsigned int npcUid, unsigned int talkerUid)
{
    return ((unsigned long long)npcUid << 32) | (unsigned long long)talkerUid;
}

}   // namespace cooptalk
