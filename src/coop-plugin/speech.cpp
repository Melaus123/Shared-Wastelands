/* speech.cpp - P3 (read-parity3 GAP 3; the user saw it in T244): NPC SPEECH BUBBLES FOLLOW THE GAME
   DRIVING THE CHARACTER.

   The engine shows a bubble through Dialogue::say(const std::string& text, DialogLineData* line) 0x67F2F0
   (Dialogue: me +0x150, speechBubblePanel +0x228, speechTextTimer +0x230).  What is said, and when, comes
   from each game's own AI and perception, and the line and its text variant are CRT rand() draws - so a
   copy on the other game either says nothing or says a different line (read-parity3 GAP 3, Confirmed).

   THE RULE, in the one detour below (MAIN THREAD only - anything else is counted and passed through):
     - the speaker is a character THIS game authors: the engine says it, then the exact text goes to the
       peer as MSG_SAY (reliable, at most 512 bytes);
     - the speaker is a COPY driven by the other game, and the call is not our own apply: DROPPED - the
       copy must not invent lines its owner never said;
     - anything else (no uid: this game's unreplicated characters) is the engine's, untouched.
   The receiver (ApplyRemoteSay, from the session drain on the main thread) calls the ORIGINAL say(text, 0)
   on the copy's Dialogue (Character +0x280) with a flag up, so the detour lets exactly that call through.

   THE PARKED LINE.  say() parks the text in Dialogue +0x58 and returns when the predicate at 0x25C690 on
   the global 0x2133840 is set, and Dialogue::update 0x684600 says it again later (the call returning to
   0x68472E) - Read from the disassembly.  That replay is recognised by its return address, and since P3-b
   it is trusted only when the plugin handled that very text on that Dialogue (see below).

   P3-b (review-p3, 2026-09-22):
     - A COPY NEVER STARTS ITS OWN CHAT.  Dialogue::sendEvent 0x683F00 (bool, (Dialogue*, Character* who,
       EventTriggerEnum)) is detoured too: on the main thread, when `me` is a copy the other game drives (the
       same uid / IsUidMine test as say), every event except EV_PLAYER_TALK_TO_ME (1) is skipped and returns
       false (sendEventBlocked) - P3-c: NO LONGER; the event now runs and is only COUNTED (sendEventWouldBlock), see detour_sendEvent.  That keeps the copy from reaching sayLine at all, so it is never left half
       way through a chain whose lines were dropped.
     - THE BACKSTOP.  A copy's own say() is still dropped; when the dropped call carries the Dialogue's
       currentLine (+0x198, which sayLine 0x682F10 sets before it says), currentLine is cleared so update
       ends the chat instead of advancing a dead chain (dropClearedLine).
     - REPLAYS ARE CHECKED, NOT TRUSTED.  A parked-line replay counts as already handled only if the plugin
       handled THAT text on THAT Dialogue (sent it, or applied it) - a small main-thread table.  An unhandled
       replay is a NEW line: sent on the owner (replaySent), dropped on a copy (replayDropped).
     - passThrough is split into passOffThread, passNoUid and passReplay.

   The std::string argument is only ever read by reference, and no function here that has __try holds a
   C++ object (C2712). */
#include "speech.h"
#include "crime.h"   /* par20: BountyTestClearArm (crimetest bountyclear) */
#include "addresses.h"      /* P8h: the three RVAs below come from the address table */
#include "spawn.h"          /* FindSpawned / FindSpawnedUid */
#include "store.h"          /* StoreMainThreadId */
#include "net/session.h"    /* IsUidMine, UidOwnedByPeer, SendSay */
#include "../common/saywire.h"
#include "playerfaction.h"  /* crimetest: IsPlayerFaction / IsPeerFaction */
#include "relations.h"      /* crime7: RelationsWireSid - the optional victim-faction filter */
#include "replicate.h"      /* pvp1: AttackLocal - `crimetest attackplayer` */
#include "coop_log.h"
#include "game/Character.h"   /* crimetest: Character::getOwnerFactionDirect */
#include "game/Faction.h"     /* talktest nearestfaction: Faction::getName */
#include "game/GameWorld.h"   /* rel3 (T317): attacknear walks ou->activeCharacters() */
#include "game/hand.h"   /* crimetest: the victim's hand, rebuilt locally from its five id fields */
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */
#include "ai_spike.h"   /* P26 stage 0 fold 1: EngineThreadNameOf - which engine thread the off-thread starts run on */
#include "../common/talkwire.h"     /* P26 stages 1-3: MSG_TALK */
#include "../common/liveenvelope.h" /* P25 fold re-check: cooplive::SamePlayer (a waiting request's owner key vs the PROMPT/END sender) */
#include "game/GameData.h"          /* P26 stages 1-3: a dialogue line's GameData string id */
#include "game/GameDataManager.h"   /* P26 stages 1-3: gamedata.getData / listRecordsOfType */
#include <Windows.h>
#include <intrin.h>
#include <algorithm>   /* P26 stage 0 fold 1: std::sort / std::lower_bound over the drain's pointers */
#include <cstdio>
#include <cstdlib>   /* crimetest sentence: std::atof */
#include <cstring>
#include <cmath>
#include <string>
#include <map>      /* P26 stages 1-3 */
#include <vector>   /* P26 stages 1-3 */

#pragma intrinsic(_ReturnAddress)

namespace coop {
int GroundTerrainHeightAt(float x, float z, float* out);   /* P26lvl maxdy: combat.cpp - the terrain height at (x,z) (groundline's SEH-wrapped getTerrainHeightFast): 1 height, 0 no terrain (-99), -1 no row / faulted. MAIN THREAD */
int GroundAnimalAppPod(::Character* c);   /* P26f3: appearance.cpp - the P10 class test (appearance vtable == AppearanceAnimalVt): 1 animal, 0 not, -1 unreadable. MAIN THREAD */
namespace {

unsigned long long kDialogueSayRva = 0; static coop::AddrReg kDialogueSayRva_reg("DialogueSay", &kDialogueSayRva);   /* Steam_1.0.65 0x67F2F0 */
unsigned long long kDialogueUpdateSayRetRva = 0; static coop::AddrReg kDialogueUpdateSayRetRva_reg("DialogueUpdateSayRet", &kDialogueUpdateSayRetRva);   /* Steam_1.0.65 0x68472E: Dialogue::update's replay of a parked line returns here */
unsigned long long kDialogueSendEventRva = 0; static coop::AddrReg kDialogueSendEventRva_reg("DialogueSendEvent", &kDialogueSendEventRva);   /* Steam_1.0.65 0x683F00: bool Dialogue::sendEvent(Character* who, EventTriggerEnum what) */
unsigned long long kDialogueStartConvRva = 0; static coop::AddrReg kDialogueStartConvRva_reg("DialogueStartConversation", &kDialogueStartConvRva);   /* Steam_1.0.65 0x683500: bool Dialogue::startConversation(Character* who, DialogLineData* line, EventTriggerEnum, bool) - P26 stage 0, log only */
unsigned long long kDialogueStartPlayerConvRva = 0; static coop::AddrReg kDialogueStartPlayerConvRva_reg("DialogueStartPlayerConversation", &kDialogueStartPlayerConvRva);   /* Steam_1.0.65 0x683890: bool Dialogue::startPlayerConversation(Character* who, DialogLineData* line) - P26 stage 0, log only */

const size_t kDlgMe         = 0x150;   /* Dialogue::me - the speaking Character */
const size_t kCharDialogue  = 0x280;   /* Character's Dialogue, as 0x5CED00 uses it (this[0x50]) */
const size_t kDlgParkedLen  = 0x68;    /* size of the parked std::string at +0x58 (update tests it, then zeroes it) */
const size_t kDlgCurLine    = 0x198;   /* Dialogue::currentLine - sayLine 0x682F10 stores its line here before saying it */

typedef void (*SayFn)(void* dlg, const std::string& text, void* line);
SayFn     orig_say = 0;
uintptr_t g_base   = 0;
int       g_sayHook = 0;              /* 0 not installed (no address), 1 installed, -1 AddHook FAILED */
typedef bool (*SendEventFn)(void* dlg, void* who, int what);
SendEventFn orig_sendEvent = 0;
int       g_sendEventHook = 0;        /* same states as g_sayHook */
coopsay::SayMarkTable g_sayMarks;     /* MAIN THREAD only: which parked text the plugin handled, per Dialogue */
volatile LONG g_sayApplying = 0;       /* set only around our own call in ApplyRemoteSay (main thread) */

volatile LONG64 g_saySent               = 0;   /* MSG_SAY put on the wire for a character this game authors */
volatile LONG64 g_sayTooLong            = 0;   /* an authored line longer than 512 bytes - said here, NOT sent */
volatile LONG64 g_sayPassOffThread      = 0;   /* the engine's own call off the main thread, untouched */
volatile LONG64 g_sayPassNoUid          = 0;   /* a speaker with no uid (unreplicated), untouched */
volatile LONG64 g_sayPassReplay         = 0;   /* a parked line the plugin already handled: said, not re-sent */
volatile LONG64 g_saySendEventBlocked   = 0;   /* a copy's own chat, stopped at Dialogue::sendEvent */
volatile LONG64 g_sayDropClearedLine    = 0;   /* a dropped copy line was the Dialogue's currentLine - cleared */
volatile LONG64 g_sayReplaySent         = 0;   /* owner: a parked line the plugin never handled - sent as new */
volatile LONG64 g_sayReplayDropped      = 0;   /* copy: a parked line the plugin never handled - dropped */
volatile LONG64 g_sayDroppedBlocked     = 0;   /* a MSG_SAY arriving while EngineWritesBlocked() - dropped, never queued */
volatile LONG64 g_sayLocalPuppetDropped = 0;   /* a copy's own engine-chosen line, dropped */
volatile LONG64 g_sayApplied            = 0;   /* an arriving line shown on the copy */
volatile LONG64 g_sayUnknownUid         = 0;   /* arriving: no copy here for that uid, or the sender does not own it */
volatile LONG64 g_sayNoDialogue         = 0;   /* arriving: the copy's Dialogue pointer was not plausible (or say has no address) */
volatile LONG64 g_sayMalformed          = 0;   /* arriving: the payload did not decode */
long long g_sayLogged = 0;
const long long kSayLogLimit = 20;

int SayOnMainThread()
{
    const unsigned long m = StoreMainThreadId();
    return (m != 0 && ::GetCurrentThreadId() == (DWORD)m) ? 1 : 0;
}

void* SayReadPtr(const void* base, size_t off)
{
    void* v = 0;
    if (base == 0) return 0;
    __try { v = *(void* const*)((const char*)base + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return v;
}

int SayPlausiblePtr(const void* p)
{
    const uintptr_t v = (uintptr_t)p;
    if (v < 0x10000 || v >= 0x00007FFFFFFFFFFFull) return 0;
    if (v & 0x7) return 0;
    __try { volatile uintptr_t probe = *(const uintptr_t*)v; (void)probe; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

/* 1 when the write happened.  No C++ object here (C2712). */
int SayWritePtr(void* base, size_t off, void* v)
{
    if (base == 0) return 0;
    __try { *(void**)((char*)base + off) = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

/* After a say the plugin handled: if the engine parked the text instead of showing it, remember that THIS
   text on THIS Dialogue is ours, so its replay is recognised. MAIN THREAD. */
void SayMarkIfParked(void* dlg, const std::string& text)
{
    if (SayReadPtr(dlg, kDlgParkedLen) == 0) return;
    g_sayMarks.Mark(dlg, coopsay::SayTextHash(text.data(), text.size()), (unsigned int)text.size());
}

/* A copy's say was dropped. If it was saying the Dialogue's currentLine (sayLine set it just before), that
   line was never shown and must not be advanced from - clear it so Dialogue::update ends the chat. */
void SayDropClearLine(void* dlg, void* line)
{
    if (line == 0) return;
    if (SayReadPtr(dlg, kDlgCurLine) != line) return;
    if (SayWritePtr(dlg, kDlgCurLine, 0)) ::InterlockedIncrement64(&g_sayDropClearedLine);
}

void SayLog(const char* dir, unsigned int uid, const std::string& text)
{
    if (g_sayLogged >= kSayLogLimit) return;
    ++g_sayLogged;
    char b[64];
    std::sprintf(b, " uid=%u len=%u text='", uid, (unsigned int)text.size());
    DebugLog(std::string("[SAY] ") + dir + b + text.substr(0, 120) + "'");
}

void SaySend(unsigned int uid, const std::string& text, volatile LONG64* sentCounter)
{
    if (text.size() > (size_t)coopsay::kSayMaxText) { ::InterlockedIncrement64(&g_sayTooLong); return; }
    if (net::SendSay(uid, text.data(), text.size()))
    {
        ::InterlockedIncrement64(sentCounter);
        SayLog("->", uid, text);
    }
}

void detour_say(void* dlg, const std::string& text, void* line)
{
    const int applying = g_sayApplying != 0 ? 1 : 0;
    const int onMain = applying ? 1 : SayOnMainThread();
    unsigned int uid = 0;
    int replay = 0, handled = 0, mine = 0;
    if (!applying && onMain)
    {
        replay = (kDialogueUpdateSayRetRva != 0
                  && (uintptr_t)_ReturnAddress() == g_base + (uintptr_t)kDialogueUpdateSayRetRva) ? 1 : 0;
        void* me = SayReadPtr(dlg, kDlgMe);
        uid = (me != 0) ? FindSpawnedUid(me) : 0;
        if (uid != 0) mine = net::IsUidMine(uid) ? 1 : 0;
        if (uid != 0 && replay)
            handled = g_sayMarks.Take(dlg, coopsay::SayTextHash(text.data(), text.size()), (unsigned int)text.size());
    }
    switch (coopsay::SayClassify(applying, onMain, uid != 0 ? 1 : 0, mine, replay, handled))
    {
    case coopsay::kSayActApply:                     /* our own apply - counted and marked there */
        orig_say(dlg, text, line);
        return;
    case coopsay::kSayActPassOffThread:
        ::InterlockedIncrement64(&g_sayPassOffThread);
        orig_say(dlg, text, line);
        return;
    case coopsay::kSayActPassNoUid:
        ::InterlockedIncrement64(&g_sayPassNoUid);
        orig_say(dlg, text, line);
        return;
    case coopsay::kSayActPassReplay:                /* sent (owner) or applied (copy) when it was first said */
        ::InterlockedIncrement64(&g_sayPassReplay);
        orig_say(dlg, text, line);
        return;
    case coopsay::kSayActSend:
        orig_say(dlg, text, line);
        SayMarkIfParked(dlg, text);
        SaySend(uid, text, &g_saySent);
        return;
    case coopsay::kSayActReplaySend:
        orig_say(dlg, text, line);
        SaySend(uid, text, &g_sayReplaySent);
        return;
    case coopsay::kSayActReplayDrop:
        ::InterlockedIncrement64(&g_sayReplayDropped);
        return;
    default:                                        /* kSayActDrop */
        ::InterlockedIncrement64(&g_sayLocalPuppetDropped);
        SayDropClearLine(dlg, line);
        return;
    }
}

// ===========================================================================================
// CRIMETEST LEVER (crime1, docs/design-crime.md section 2) - a TEST-ONLY dev verb (class b), not player behaviour.
// `crimetest steal` arms one request; the K2 safe point (GameWorld::threadSafeRagdollUpdates 0x7D17E0, combat.cpp - the
// worker thread is paused there, crime2 answer 2) drains it: this game's first OWN player-faction character becomes the
// offender, the nearest character this game does NOT own (a copy), of a non-player, non-peer faction, within 500 u
// becomes the victim, and BountyManager::notifyCrimeWitnessed 0x851F40 (this = offender+0xF0; Faction* against,
// const hand& againstWho, int expiry game seconds, CrimeEnum) is called with 3 STEALING and expiry 20 - what
// assessKidnapping passes (crime2 answer 1). The offender's crime fields are read back (Character +0x148 int crime,
// +0x180 float expiry - crime2 answer 1b) and logged with every input. Witnesses then react through their own senses.
// ===========================================================================================
unsigned long long kNotifyCrimeWitnessedRva = 0; static coop::AddrReg kNotifyCrimeWitnessedRva_reg("NotifyCrimeWitnessed", &kNotifyCrimeWitnessedRva);   /* Steam_1.0.65 0x851F40 */
typedef void (*NotifyCrimeFn)(void* bountyManager, void* againstFaction, const hand& againstWho, int expirySeconds, int crime);
const size_t kCharBountyMgr   = 0xF0;    /* Character -> its BountyManager (embedded) */
const size_t kCharCrimeInt    = 0x148;   /* BountyManager +0x58: committingCrime (int CrimeEnum) */
const size_t kCharCrimeExpiry = 0x180;   /* BountyManager +0x90: crimeExpiry (float, game seconds) */
const size_t kCharHand        = 0x58;    /* the character's own hand; its five id fields at +0x8..+0x18 (H027) */
const int    kCrimeStealing   = 3;
const int    kCrimeExpirySec  = 20;
const float  kCrimeVictimMaxDist = 500.0f;
const float  kCrimeVictimMaxDistFiltered = 3000.0f;   /* crime8: T282 found no victim of any kind within 500 u on either game */
volatile LONG g_crimeTestPending = 0;    /* armed by the verb (main thread), drained at the safe point (main thread): 1 steal, 2 stealnear, 3 stealplayer, 4 attackplayer, 5 attacknear */
long long g_crimeTestArmed = 0, g_crimeTestDone = 0, g_crimeTestNoOffender = 0, g_crimeTestNoVictim = 0, g_crimeTestNoAddr = 0,
          g_crimeTestFaulted = 0, g_crimeTestBlocked = 0, g_crimeTestEffective = 0;   /* effective: the read-back shows STEALING with a fresh expiry */

int CrimeFactionOf(::Character* c, ::Faction** out)
{
    *out = 0;
    __try { *out = c->getOwnerFactionDirect(); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* rel3: `attacknear` wants a living victim. rel3 (T317): 1 dead, 0 living, -1 the read faulted - the caller still
   refuses an unreadable one (fails closed) but counts it apart from the dead, so a run can tell the two apart. */
int CrimeIsDead(::Character* c)
{
    __try { return c->hasDied() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int CrimeReadHandFields(::Character* c, unsigned int* w7)
{
    __try { const unsigned int* w = (const unsigned int*)((const char*)c + kCharHand); for (int i = 0; i < 7; ++i) w7[i] = w[i]; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CrimeCall(NotifyCrimeFn fn, ::Character* offender, ::Faction* f, const hand* h)
{
    __try { fn((char*)offender + kCharBountyMgr, (void*)f, *h, kCrimeExpirySec, kCrimeStealing); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CrimeReadBack(::Character* c, int* crime, float* expiry)
{
    __try { *crime = *(const int*)((const char*)c + kCharCrimeInt); *expiry = *(const float*)((const char*)c + kCharCrimeExpiry); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}


// ===========================================================================================
// P26 STAGE 0 (.modding/investigations/p26-npc-dialogue.md, 'Staged plan' 0) - MEASUREMENT ONLY, no behaviour change.
// The engine's two conversation starters are detoured to LOG and COUNT who starts a conversation with whom, on which event,
// and whether the reply window would open. Each detour calls the ORIGINAL first, with the same arguments, and returns its
// result unchanged; only then does it read and log.
//   Dialogue::startConversation 0x683500 (bool; Dialogue*, Character* who, DialogLineData* line, EventTriggerEnum, bool) -
//     sendEvent's road for every event but 1 (683f00:100-126, Read);
//   Dialogue::startPlayerConversation 0x683890 (bool; Dialogue*, Character* who, DialogLineData* line) - event 1 only, and
//     the one starter that calls setInDialog(true), i.e. opens the reply window (the file's Q1 step 3, Confirmed call scan).
// The event of a start is the event of the sendEvent call it runs inside (detour_sendEvent keeps it, main thread); a start
// outside any sendEvent logs ev=direct. The kinds of the speaker and the target: coopsay::TalkTargetKind (saywire.h).
// Replies: Dialogue +0x238 is the std::vector of reply ids listPlayerReplies 0x67F6A0 fills (67f6a0:46 clears it, :177 tests
// begin == end, Read). After startPlayerConversation it is this start's list (its sayLine takes the GUI branch - conversation
// type 1, Read); after startConversation it is fresh only for a GUI-branch line, so there it is logged (replyVec) and not
// counted. windowWouldOpen = startPlayerConversation returned true with a current line and at least one reply - the
// condition 683890 opens the window on (Read); the distance gate inside it is already in the return value.
// ===========================================================================================
typedef unsigned long long (*StartConvFn)(void* dlg, void* who, void* line, int ev, char force);
typedef unsigned long long (*StartPlayerConvFn)(void* dlg, void* who, void* line);
StartConvFn       orig_startConv = 0;
StartPlayerConvFn orig_startPlayerConv = 0;
int g_startConvHook = 0;          /* 1 installed, -1 AddHook FAILED, -2 no table address, 0 not tried */
int g_startPlayerConvHook = 0;    /* same states */
const size_t kDlgReplyBegin = 0x238;   /* Dialogue: std::vector<std::string> reply ids - begin (67f6a0:177) */
const size_t kDlgReplyEnd   = 0x240;   /* ... end */
const long long kTalkStartLogLimit = 40;      /* [TALK] lines for started conversations */
const long long kTalkMissLogLimit  = 10;      /* [TALK] lines for a start that returned false with a line in hand */
const float kTalkNearMax = 3000.0f;           /* talktest near: the file's lever radius */
int g_talkCurEvent = -1;          /* MAIN THREAD: the event of the sendEvent call running now (-1 none) */
int g_talkLever = 0;              /* MAIN THREAD: 1 only around talktest's own sendEvent call */
int g_talkSightCall = 0;          /* MAIN THREAD: 1 only around talksight's own sendEvent call (P26 stage 6): its STARTED lines are always logged */
long long g_talkCalls = 0, g_talkStarted = 0, g_talkWithCopy = 0, g_talkWithMine = 0, g_talkWithNpc = 0, g_talkNoTarget = 0,
          g_talkWindow = 0, g_talkBySpeakerCopy = 0, g_talkPlayerStarts = 0, g_talkDirect = 0, g_talkNotStarted = 0,
          g_talkNoLine = 0, g_talkLeverStarts = 0, g_talkStartLogged = 0, g_talkMissLogged = 0;
volatile LONG64 g_talkOffThread = 0;   /* a start off the main thread: the original ran, nothing read */
long long g_talkByEvent[coopsay::kTalkEventBuckets];     /* started conversations, by event (zero-initialised) */
long long g_copyEvByEvent[coopsay::kTalkEventBuckets];   /* sendEventWouldBlock split by event (a copy's own sendEvent) */

/* P26 stage 0 fold 1 (T631): OFF-THREAD STARTS. T631 counted ~22,000 starter calls per 10 min OFF the main thread on the host (437
   on the client) and read nothing from them, so natural play was invisible. Off the main thread the detours now record PLAIN
   VALUES only - the Dialogue, its speaker pointer (Dialogue +0x150: one SEH-guarded read of the object the engine has just used
   on this very thread), the target pointer, the line pointer, the event, the thread id - and count started / not-started by
   event with Interlocked ops. A STARTED call goes into a fixed lock-free ring (no lock, no allocation, no log). The K2 safe point
   (TalkTestDrain: main thread, worker paused) drains the ring and classifies each entry with TalkKindOf ONLY after each pointer
   checks out as a live character - the uid registry maps it back to itself (FindSpawnedUid -> FindSpawned, address compares) or
   it is a member of GameWorld's character update list - and a speaker must still own that Dialogue (Character +0x280). A pointer
   that passes neither is counted 'stale' and never dereferenced. startConversation's event is its own argument; an off-thread
   startPlayerConversation takes its event from a per-thread (TLS) slot that detour_sendEvent sets around its OFF-thread call
   (g_talkCurEvent is main-thread only). */
const LONG kTalkOffRing = 1024;                 /* ring slots; a power of two */
const long long kTalkOffStartLogLimit = 40;     /* [TALK] lines for off-thread starts */
const int kTalkOffKinds = 7;                    /* coopsay::TalkTargetKind 0..5, then 6 = stale (not a live character at the drain) */
struct TalkOffEntry { volatile LONG state; int ev; int player; unsigned long tid; void* dlg; void* me; void* who; void* line; };   /* state: 0 free, 1 writing, 2 ready */
struct TalkOffItem { int ev; int player; unsigned long tid; void* dlg; void* me; void* who; void* line; };   /* one drained entry (MAIN THREAD) */
TalkOffEntry g_talkOffRing[kTalkOffRing];
volatile LONG g_talkOffHead = 0;                /* producers' claim counter (wraps; masked) */
volatile LONG g_talkOffPending = 0;             /* entries marked ready, not yet drained */
volatile DWORD g_talkTls = TLS_OUT_OF_INDEXES;  /* per thread: 1 + the event of this thread's running sendEvent, 0 none */
volatile LONG g_talkOffTid = 0;                 /* the first thread an off-thread start ran on */
volatile LONG64 g_talkOffStarted = 0, g_talkOffNotStarted = 0, g_talkOffNoLine = 0, g_talkOffWhoNull = 0, g_talkOffPlayerCalls = 0,
                g_talkOffOtherTid = 0, g_talkOffDropped = 0, g_talkOffStartedDirect = 0, g_talkOffNotStartedDirect = 0;
volatile LONG64 g_talkOffStartedByEvent[coopsay::kTalkEventBuckets];      /* off-thread STARTED, by event */
volatile LONG64 g_talkOffNotStartedByEvent[coopsay::kTalkEventBuckets];   /* off-thread not started, by event */
long long g_talkOffDrained = 0, g_talkOffStale = 0, g_talkOffStartLogged = 0, g_talkOffTidLogged = 0;   /* MAIN THREAD */
long long g_talkOffByTarget[kTalkOffKinds];     /* MAIN THREAD: drained off-thread starts by the target's kind */
long long g_talkOffBySpeaker[kTalkOffKinds];    /* MAIN THREAD: ... by the speaker's kind */

/* P26 stage 0 fold 1: the event of THIS thread's running sendEvent (-1 none, or no TLS slot). Keeps the thread's last error. */
int TalkOffThreadEvent()
{
    const DWORD ti = g_talkTls;
    if (ti == TLS_OUT_OF_INDEXES) return -1;
    const DWORD le = ::GetLastError();
    const uintptr_t v = (uintptr_t)::TlsGetValue(ti);
    ::SetLastError(le);
    return v == 0 ? -1 : (int)(unsigned int)(v - 1);
}

/* P26s6 ARRIVAL PROBE (T714, log-only). SEEK_AND_TALK_AND_SEND_SIGNAL (0x66; also 0x9F / 0xA1) is the action class
   Task_TalktoPlayer (Steam 1.0.65, Confirmed bytes: task factory 0x32EB90 byte table 0x3303C8[0x65] = case 0x2B -> ctor
   0x335D70, vtable 0x16BDB60 = {dtor 0x33E610, startAction 0x34C540, runAction 0x34C760, endAction ret, 0x32DEA0}).
   startAction (Read 34c540): event = the task owner's OrdersReceiver flag (+0x650 -> +0x20 -> +0x264; 0 -> 1), mapped by
   0x335DC0 (9 -> 0x11 when the target is inside), then Dialogue::sendEvent(owner +0x280, target, event) through THIS entry
   (call at 0x34C6B4); a false result ends the task. runAction (Read 34c760) sends 0x3C (give up) via 0x6845E0 and repeats
   event 9. So every arrival passes here: counted by event (any thread, Interlocked only), and the last event-1 call kept
   in one slot (g_arrBusy guards it: a writer that loses the race is only counted) for the K2 safe point to log. */
const int kArrEvN = 4;
const int kArrEvs[kArrEvN] = { 1, 9, 0x11, 0x3C };
volatile LONG64 g_arrCalls[kArrEvN], g_arrTrue[kArrEvN];
struct ArrSlot { void* dlg; void* who; unsigned long long retRva; int result; unsigned long tid; };
ArrSlot g_arrLast;                /* written / read only while holding g_arrBusy */
volatile LONG g_arrBusy = 0;      /* 0 free, 1 held */
volatile LONG g_arrSeq = 0;       /* bumped after each event-1 slot write */
/* ANY THREAD. No lock, no allocation, no log; GetCurrentThreadId does not touch the last-error value. */
static void TkArrNote(void* dlg, void* who, int what, bool r, void* ret)
{
    int i = -1;
    for (int k = 0; k < kArrEvN; ++k) if (kArrEvs[k] == what) { i = k; break; }
    if (i < 0) return;
    ::InterlockedIncrement64(&g_arrCalls[i]);
    if (r) ::InterlockedIncrement64(&g_arrTrue[i]);
    if (what != 1) return;
    if (::InterlockedCompareExchange(&g_arrBusy, 1, 0) != 0) return;
    g_arrLast.dlg = dlg; g_arrLast.who = who; g_arrLast.result = r ? 1 : 0; g_arrLast.tid = ::GetCurrentThreadId();
    g_arrLast.retRva = (g_base != 0 && (uintptr_t)ret > g_base) ? (unsigned long long)((uintptr_t)ret - g_base) : 0;
    ::InterlockedIncrement(&g_arrSeq);
    ::InterlockedExchange(&g_arrBusy, 0);
}

/* P3-b: a copy the other game drives never starts a chat of its own - only the player talking to it. */
bool detour_sendEvent(void* dlg, void* who, int what)
{
    void* const arrRet = _ReturnAddress();   /* P26s6 arrival probe: the engine call site (the thunk is a plain jmp) */
    if (SayOnMainThread() != 0)
    {
        void* me = SayReadPtr(dlg, kDlgMe);
        const unsigned int uid = (me != 0) ? FindSpawnedUid(me) : 0;
        const int puppet = (uid != 0 && !net::IsUidMine(uid)) ? 1 : 0;
        /* P3-c (re-check-p3b MEDIUM): COUNT, DO NOT BLOCK. A copy's sendEvent also starts the chats whose ACTIONS are
           gameplay on this game - a guard's reaction to the local player's theft or trespass (events 8/9/0x11/0x29-0x2c:
           _doActions 0x67FAD0 -> sendEvent 0x42 to the squad, 0x5D1640(...,0x1d,who)) and event 0xd's cage unlock
           (0x672EA0 / 0x673870) - and the owner game never learns of a crime committed here. Blocking them silently
           removed those reactions. The copy's LINE is still dropped at say() (the backstop) and its current line
           cleared, so it neither invents a bubble nor sticks mid-chat. The counter keeps its name and now measures
           the events the rule WOULD block, so a run can size the question 'report crimes to the owner' (open). */
        if (!coopsay::SendEventAllowed(puppet, what))
        {
            ::InterlockedIncrement64(&g_saySendEventBlocked);
            ++g_copyEvByEvent[coopsay::TalkEventBucket(what)];   /* P26 stage 0: the same count, split by event */
        }
        /* P26 stage 0: the starters below log the event they run under; nested sendEvents restore the outer one */
        const int prevEv = g_talkCurEvent;
        g_talkCurEvent = what;
        const bool r = orig_sendEvent(dlg, who, what);
        g_talkCurEvent = prevEv;
        TkArrNote(dlg, who, what, r, arrRet);   /* P26s6 arrival probe */
        return r;
    }
    /* P26 stage 0 fold 1: off the main thread only this thread's own TLS slot is set (and restored), so an off-thread
       startPlayerConversation knows its event. Same arguments, same result; the thread's last-error value is kept. */
    const DWORD ti = g_talkTls;
    if (ti == TLS_OUT_OF_INDEXES)
    {
        const bool r0 = orig_sendEvent(dlg, who, what);
        TkArrNote(dlg, who, what, r0, arrRet);   /* P26s6 arrival probe */
        return r0;
    }
    DWORD le = ::GetLastError();
    void* const prevSlot = ::TlsGetValue(ti);
    ::TlsSetValue(ti, (void*)((uintptr_t)(unsigned int)what + 1));
    ::SetLastError(le);
    const bool r = orig_sendEvent(dlg, who, what);
    le = ::GetLastError();
    ::TlsSetValue(ti, prevSlot);
    ::SetLastError(le);
    TkArrNote(dlg, who, what, r, arrRet);   /* P26s6 arrival probe (keeps the last-error value) */
    return r;
}

/* P26 stage 0: the character's display name (at most 47 bytes) into out[48]; '-' no character, '?' unreadable. No C++ object
   here (C2712). */
void TalkName(::Character* c, char* out)
{
    out[0] = '-'; out[1] = 0;
    if (c == 0) return;
    out[0] = '?';
    if (SayPlausiblePtr(c) == 0) return;
    __try
    {
        const std::string& s = c->shownName;
        size_t n = s.size();
        if (n > 47) n = 47;
        std::memcpy(out, s.c_str(), n);
        out[n] = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = '?'; out[1] = 0; }
}

/* P26 stage 0: the kind of one character (coopsay::TalkTargetKind) and its uid (0 = not replicated). MAIN THREAD. */
int TalkKindOf(::Character* c, unsigned int* uidOut)
{
    *uidOut = 0;
    if (c == 0) return coopsay::TalkTargetKind(0, 0, 0, 0, 0, 0);
    ::Faction* f = 0;
    const int fr = (SayPlausiblePtr(c) != 0 && CrimeFactionOf(c, &f) && f != 0) ? 1 : 0;
    const unsigned int u = FindSpawnedUid(c);
    *uidOut = u;
    return coopsay::TalkTargetKind(1, fr, (fr && IsPlayerFaction(f)) ? 1 : 0, (fr && IsPeerFaction(f)) ? 1 : 0,
                                   u != 0 ? 1 : 0, (u != 0 && net::IsUidMine(u)) ? 1 : 0);
}

/* P26 stage 0: the number of reply ids in Dialogue +0x238; -1 when the vector does not read as one. */
int TalkReplyCount(void* dlg)
{
    const char* b = (const char*)SayReadPtr(dlg, kDlgReplyBegin);
    const char* e = (const char*)SayReadPtr(dlg, kDlgReplyEnd);
    if (b == 0 && e == 0) return 0;
    if (b == 0 || e < b) return -1;
    const size_t bytes = (size_t)(e - b);
    if (bytes % sizeof(std::string) != 0 || bytes / sizeof(std::string) > 256) return -1;
    return (int)(bytes / sizeof(std::string));
}

/* P26 stage 0: one start, AFTER the original ran. player = startPlayerConversation. MAIN THREAD. Reads and counts only. */
void TalkNote(int player, void* dlg, void* who, void* line, int ev, unsigned long long ret)
{
    const int started = (ret & 0xFF) != 0 ? 1 : 0;
    ++g_talkCalls;
    if (!started)
    {
        ++g_talkNotStarted;
        if (line == 0) { ++g_talkNoLine; return; }   /* _chooseDialog found no line - the common case, never logged */
        if (g_talkMissLogged >= kTalkMissLogLimit && !g_talkLever) return;
    }
    ::Character* me = (::Character*)SayReadPtr(dlg, kDlgMe);
    unsigned int suid = 0, tuid = 0;
    const int sk = TalkKindOf(me, &suid);
    const int tk = TalkKindOf((::Character*)who, &tuid);
    const int replies = TalkReplyCount(dlg);
    const void* cur = SayReadPtr(dlg, kDlgCurLine);
    const int window = (player && started && cur != 0 && replies > 0) ? 1 : 0;
    if (started)
    {
        ++g_talkStarted;
        if (player) ++g_talkPlayerStarts;
        if (g_talkLever) ++g_talkLeverStarts;
        if (tk == coopsay::kTalkTargetPartnerCopy) ++g_talkWithCopy;
        else if (tk == coopsay::kTalkTargetMine) ++g_talkWithMine;
        else if (tk == coopsay::kTalkTargetNpcMine || tk == coopsay::kTalkTargetNpcCopy) ++g_talkWithNpc;
        else ++g_talkNoTarget;
        if (sk == coopsay::kTalkTargetNpcCopy) ++g_talkBySpeakerCopy;
        if (window) ++g_talkWindow;
        if (ev < 0) ++g_talkDirect; else ++g_talkByEvent[coopsay::TalkEventBucket(ev)];
        if (g_talkStartLogged >= kTalkStartLogLimit && !g_talkLever && !g_talkSightCall) return;   /* P26 stage 6: talksight's starts always logged */
        if (!g_talkLever && !g_talkSightCall) ++g_talkStartLogged;
    }
    else if (!g_talkLever) ++g_talkMissLogged;
    char sn[48], tn[48], evs[16], b[640];
    TalkName(me, sn);
    TalkName((::Character*)who, tn);
    if (ev < 0) std::strcpy(evs, "direct"); else std::sprintf(evs, "0x%X", (unsigned int)ev);
    std::sprintf(b, "[TALK] %s %s ev=%s speaker uid=%u %s '%s' -> target uid=%u %s '%s' line=%p %s=%d windowWouldOpen=%d host=%d%s",
                 player ? "startPlayerConversation" : "startConversation", started ? "STARTED" : "not-started", evs,
                 suid, coopsay::TalkTargetName(sk), sn, tuid, coopsay::TalkTargetName(tk), tn, line,
                 player ? "replies" : "replyVec", replies, window, net::SessionIsHost() ? 1 : 0, g_talkLever ? " (talktest)" : (g_talkSightCall ? " (talksight)" : ""));
    DebugLog(std::string(b));
}

/* P26 stage 0 fold 1: one start OFF the main thread, AFTER the original ran. Interlocked counts and plain values only - no lock,
   no allocation, no log, and no read of a game object except Dialogue::me of the Dialogue this thread has just used (SEH-guarded).
   Only a STARTED call is queued for the main thread; a full slot is counted (dropped), never waited on. */
void TalkOffNote(int player, void* dlg, void* who, void* line, int ev, unsigned long long ret)
{
    const unsigned long tid = ::GetCurrentThreadId();
    ::InterlockedIncrement64(&g_talkOffThread);
    ::InterlockedCompareExchange(&g_talkOffTid, (LONG)tid, 0);
    if ((unsigned long)g_talkOffTid != tid) ::InterlockedIncrement64(&g_talkOffOtherTid);
    if (player) ::InterlockedIncrement64(&g_talkOffPlayerCalls);
    if (who == 0) ::InterlockedIncrement64(&g_talkOffWhoNull);
    if ((ret & 0xFF) == 0)
    {
        ::InterlockedIncrement64(&g_talkOffNotStarted);
        if (line == 0) ::InterlockedIncrement64(&g_talkOffNoLine);
        if (ev < 0) ::InterlockedIncrement64(&g_talkOffNotStartedDirect);
        else ::InterlockedIncrement64(&g_talkOffNotStartedByEvent[coopsay::TalkEventBucket(ev)]);
        return;
    }
    ::InterlockedIncrement64(&g_talkOffStarted);
    if (ev < 0) ::InterlockedIncrement64(&g_talkOffStartedDirect);
    else ::InterlockedIncrement64(&g_talkOffStartedByEvent[coopsay::TalkEventBucket(ev)]);
    void* const me = SayReadPtr(dlg, kDlgMe);
    const unsigned long slot = (unsigned long)::InterlockedIncrement(&g_talkOffHead) & (unsigned long)(kTalkOffRing - 1);
    TalkOffEntry& e = g_talkOffRing[slot];
    if (::InterlockedCompareExchange(&e.state, 1, 0) != 0) { ::InterlockedIncrement64(&g_talkOffDropped); return; }
    e.ev = ev; e.player = player; e.tid = tid; e.dlg = dlg; e.me = me; e.who = who; e.line = line;
    ::InterlockedExchange(&e.state, 2);   /* a full barrier: the fields above are visible before 'ready' */
    ::InterlockedIncrement(&g_talkOffPending);
}

// ===========================================================================================
// P26 STAGES 1-3 (.modding/investigations/p26-npc-dialogue.md Q3, 'Staged plan' 1-3) - AN NPC'S CONVERSATION WITH THE OTHER
// PLAYER'S CHARACTER OPENS THAT PLAYER'S OWN DIALOGUE WINDOW. MSG_TALK 59 (src/common/talkwire.h): PROMPT / ANSWER / END.
//
// ON THE NPC'S GAME (A). Dialogue::startPlayerConversation 0x683890 - the one starter that opens the window - toward a character
//   of a peer's stand-in faction (IsPeerFaction on RootObject +0x10: one guarded read and pointer compares, any thread) runs
//   EXACTLY as vanilla (the line, the NPC line's own _doActions, the reply list), with the Dialogue MARKED first. A mark is a
//   slot in a fixed lock-free table; Dialogue::setInDialog 0x673C10 does nothing for a marked Dialogue, on or off, called
//   directly or replayed by Dialogue::update from the engine's own off-thread queue (off the main thread setInDialog pushes op 2
//   / 3 onto Dialogue +0x80 and returns: Confirmed bytes 0x673C1D-0x673C8F). So this game's window never opens for the other
//   player's character. Then, ON THE MAIN THREAD, the said line (Dialogue +0x198), its GameData string id (DialogLineData
//   +0x1D8), the NPC text (+0x278), the reply ids (+0x238) and texts (+0x258) go to the character's owner as PROMPT, and the
//   NPC's Dialogue stays in its conversation. The engine's own enders keep working: Dialogue::endDialogue 0x673DA0 on an open
//   forwarded conversation sends END.
//   THREADING: the starters mostly run on the AI thread (T631/T636/T639). Off the main thread the detour only MARKS (Interlocked,
//   no lock, no allocation, no log) and the stage-0 ring carries the start to the K2 safe point (TalkOffDrain, main thread),
//   which checks the pointers are live characters before it reads the conversation and sends. An off-thread endDialogue on an
//   open conversation flips its mark to 'ending' (Interlocked) and the safe point sends END. Nothing touches the GUI or the
//   link off the main thread. A mark whose ring entry was lost is swept after two safe points (the conversation is ended).
//   NOT OURS TO FORWARD (the NPC is a copy or has no uid, the target has no uid or no owning peer, the link is down, the send
//   failed, no reply): the window stays shut and the conversation is ended at the next safe point - the other player's
//   character is never answered for on this game.
// ON THE ADDRESSED CHARACTER'S GAME (B), at the K2 safe point. The NPC's COPY starts the same line (DialogDataManager::getData
//   0x6AE090 on the line's GameData) with B's own character through the ORIGINAL startPlayerConversation, under an apply flag
//   (the NPC line's _doActions is skipped - A ran it: hire.cpp detour_doActions asks TalkSkipDoActions). B's engine opens its
//   OWN window (text, portraits, word swaps, number keys; T-288 keeps it from pausing the session). A reply click
//   (Dialogue::replyClicked(int) 0x683360) on that copy is NOT run: its index and reply id go back as ANSWER and the window stays
//   until END; the window's end (endDialogue, or setInDialog(false), outside our own calls) is ANSWER CLOSED. One mirrored
//   conversation at a time: another PROMPT is answered BUSY.
// STAGE 4 (P26s4). A APPLIES the ANSWER through the engine's own reply path: Dialogue::replyClicked(int) 0x683360 on the NPC's
//   Dialogue with the index of B's reply id in A's OWN list (+0x238), at the K2 safe point - the reply line's _doActions, the next
//   NPC line (replyClicked(string) 0x683170 -> sayLine 0x682F10) and its reply list run exactly as for a local player (the window
//   writers stay skipped for the marked Dialogue). The next line goes out as a new PROMPT (same convId; seq + 1 in the log); B says
//   it on the copy through sayLine under the apply flag and its window updates in place. The conversation ends on A only when the
//   engine ends it (endDialogue -> END; inside the apply END ANSWERED) or on B's CLOSED / BUSY / NO_CHAR / SHOW_FAILED. P26s5 fold 5
//   (owner decision 229): NO answer deadline - the conversation waits for the player as in single player (PROMPT deadlineMs 0);
//   only a held end waiting for an ACT_RESULT is bounded (30 s, TkTimeouts -> END TIMEOUT). A reply id not
//   in A's list, or a next line that cannot go on the wire -> END NOT_FORWARDABLE. Link loss (OnPeerGone): both sides end every
//   open conversation (while the world is blocked: at the next safe point). TARGET-SIDE ACTIONS (8 / 9 / 10 money, 57 knockout,
//   3 / 18 hire) of a line in a forwarded conversation are NOT run on A: the line is held, counted and logged 'target action
//   deferred to stage 5' (TalkDeferTargetActs from hire.cpp detour_doActions).
// P26s1 FOLD 1 - A PLAYER'S OWN VANILLA CONVERSATIONS STAY UNTOUCHED ON BOTH GAMES. A: a start on a marked Dialogue toward
//   anyone but the other player's character releases the mark before the engine runs (TkRelease); sayLine's two window
//   writers (setConversationReplyGUI 0x6736E0, setResponesGUI 0x6735E0) are skipped for a marked Dialogue like setInDialog.
//   B: a PROMPT is BUSY while this game's window shows any conversation or either Dialogue is already talking; the mirror
//   closes when the window moves to another conversation. Both: a liveness sweep at every safe point.
// ===========================================================================================
unsigned long long kDialogueSetInDialogRva = 0; static coop::AddrReg kDialogueSetInDialogRva_reg("DialogueSetInDialog", &kDialogueSetInDialogRva);   /* Steam_1.0.65 0x673C10: void Dialogue::setInDialog(bool on) */
unsigned long long kDialogueReplyClickedRva = 0; static coop::AddrReg kDialogueReplyClickedRva_reg("DialogueReplyClicked", &kDialogueReplyClickedRva);   /* Steam_1.0.65 0x683360: void Dialogue::replyClicked(int index) */
unsigned long long kDialogueSayLineRva = 0; static coop::AddrReg kDialogueSayLineRva_reg("DialogueSayLine", &kDialogueSayLineRva);   /* P26 stage 4: Steam_1.0.65 0x682F10: bool Dialogue::sayLine(DialogLineData*) - triggerNextLine 0x683C90's call for the next line (Read decomp_683c90) */
unsigned long long kTkEndDialogueRva = 0; static coop::AddrReg kTkEndDialogueRva_reg("DialogueEndDialogue", &kTkEndDialogueRva);   /* Steam_1.0.65 0x673DA0: void Dialogue::endDialogue(bool) - hire.cpp binds the same row */
unsigned long long kDialogDataGetDataRva = 0; static coop::AddrReg kDialogDataGetDataRva_reg("DialogDataManagerGetData", &kDialogDataGetDataRva);   /* Steam_1.0.65 0x6AE090: static DialogLineData* DialogDataManager::getData(GameData*) - a hash lookup, 0 when absent */
unsigned long long kDlgSetConvReplyGuiRva = 0; static coop::AddrReg kDlgSetConvReplyGuiRva_reg("DialogueSetConversationReplyGUI", &kDlgSetConvReplyGuiRva);   /* P26s1 fold 1: Steam_1.0.65 0x6736E0: void Dialogue::setConversationReplyGUI() - DialogueWindow::setNPCText 0x721F00 on the ONE window (0x2132770) with +0x278; off the main thread it queues itself as op 6 (+0x80) and is replayed through the same entry (callers 0x683122 sayLine, 0x6846A0 update) */
unsigned long long kDlgSetResponsesGuiRva = 0; static coop::AddrReg kDlgSetResponsesGuiRva_reg("DialogueSetResponesGUI", &kDlgSetResponsesGuiRva);   /* P26s1 fold 1: Steam_1.0.65 0x6735E0: void Dialogue::setResponesGUI() - DialogueWindow::setResponses 0x722EB0 on the ONE window with +0x258; op 5 off the main thread (callers 0x67FA5B listPlayerReplies, 0x684696 update) */
unsigned long long kDialogueWindowRva = 0; static coop::AddrReg kDialogueWindowRva_reg("DialogueWindowGlobal", &kDialogueWindowRva);   /* P26s1 fold 1: Steam_1.0.65 0x2132770: DialogueWindow* - the one dialogue window (setInDialog 0x673D50 / 0x673D77 load it) */

typedef void (*SetInDialogFn)(void* dlg, bool on);
typedef void (*ReplyClickedFn)(void* dlg, int index);
typedef void (*TkEndDialogueFn)(void* dlg, bool definitelyTheEnd);
typedef void* (*DlgGetDataFn)(void* gameData);
typedef unsigned long long (*SayLineFn)(void* dlg, void* line);   /* P26 stage 4: 0x682F10 returns 0 (no line) / 1 in al */
SetInDialogFn   orig_setInDialog = 0;
ReplyClickedFn  orig_replyClicked = 0;
TkEndDialogueFn orig_endDialogue = 0;
int g_setInDialogHook = 0, g_replyClickedHook = 0, g_endDialogueHook = 0;   /* 1 installed, -1 AddHook FAILED, -2 no table address */
typedef void (*DlgGuiFn)(void* dlg);   /* P26s1 fold 1: setConversationReplyGUI / setResponesGUI - `this` only (0x6736E0 / 0x6735E0, Read) */
DlgGuiFn orig_setConvReplyGui = 0, orig_setResponsesGui = 0;
int g_convReplyGuiHook = 0, g_responsesGuiHook = 0;   /* same states */
int g_tkOn = 0;   /* A's side runs only with startPlayerConversation, setInDialog, endDialogue and (fold 1) both window writers hooked */

const size_t kDlgReplyTextBegin = 0x258;   /* Dialogue: std::vector<std::string> reply texts (replyClicked 0x683399 reads +0x258 / +0x260) */
const size_t kDlgReplyTextEnd   = 0x260;
const size_t kDlgNpcText        = 0x278;   /* Dialogue: the NPC line's final text (sayLine 682f10:69) */
const size_t kLineGameData      = 0x1D8;   /* DialogLineData::data, GameData* */
const size_t kRootFaction       = 0x10;    /* RootObject -> Faction* (hire.cpp kCharFactionOff) */
const size_t kDlgTargetHand     = 0x158;   /* P26s1 fold 1: Dialogue's conversation target, a hand: startPlayerConversation 0x683AA4 -> hand::operator=(RootObject*) 0x791A70 copies who +0x60..+0x70 to hand +0x8..+0x18 */
const size_t kDlgConvEvent      = 0x188;   /* P26s1 fold 1: Dialogue's current conversation event (int): the starters write it (0x683996 = 1), endDialogue clears it (0x674163) */
const size_t kDlgEnded          = 0x148;   /* P26s1 fold 2 N2: Dialogue's 'ended' flag (char) - the engine's own busy test reads it first (0x683890:25 / 0x684B30:10); +0x188 is not cleared by every end */
const size_t kWinDialogue       = 0xD0;    /* P26s1 fold 1: DialogueWindow's Dialogue: show writes it (0x726D4A), hide(dlg) clears it only when it is dlg (0x7213F9 / 0x72141F) */
const long long kTkFailLogCap = 40;       /* P26s1 fold 1 L5: 'NOT forwarded' / 'NOT shown' lines */
const size_t kTkInboundPromptCap = 64, kTkInboundCap = 256;   /* P26s1 fold 1 L4 */
const LONG kTkPromptMaxAge = 8;           /* P26s1 fold 1 L4: safe points a PROMPT may wait (it waits only while the world is blocked) */
const int kTkMarks = 32;
const long long kTkLogCap = 80;

struct TkMark { void* volatile dlg; void* volatile me; volatile LONG state; volatile LONG gen;
                volatile LONG engEnded; };   /* P26s5 fold 4: engEnded 1 = TkConv.engineEnded (the engine ended it; this game's side still open) - any thread */   /* state 0 free, 1 starting, 2 open, 3 ending off-thread, 4 ending by us,
   5 RELEASED (P26s1 fold 1 H1: a start toward a character that is not the other player's took the Dialogue off the main thread -
   the window is no longer kept shut, the ring and 'end later' leave it alone, the safe point finishes it) */
TkMark g_tkMarks[kTkMarks];
volatile LONG g_tkGen = 0;   /* K2 safe points seen - the sweep's clock */

struct TkConv { unsigned int convId, npcUid, targetUid, peer; void* me;
                void* line; unsigned int seq; unsigned long sentTick; int ev;   /* P26 stage 4: the prompted line, PROMPTs sent, GetTickCount at the last one, the start's event */
                int engineEnded;
                int endClock; unsigned long endTick; };   /* P26s5 fold 5: endClock 1 = a held end waits for an ACT_RESULT since endTick (GetTickCount) - TkTimeouts' only clock (decision 229) */
                /* P26s5 fold 3: 1 = the engine ended it inside the applied answer; this game's side stays open until that call returned (and a held closing-line part ran) - never ended again by us */
const unsigned int kTkHeldEndWaitMs = 30000;   /* P26s5 fold 5 (decision 229): no ANSWER deadline any more; this bounds only a held end waiting for the other game's ACT_RESULT (TkTimeouts) */
std::map<void*, TkConv> g_tkConvs;   /* A, MAIN THREAD: Dialogue -> its open forwarded conversation */
unsigned int g_tkNextConv = 0;
struct TkLater { void* dlg; void* me; };
std::vector<TkLater> g_tkEndLater;   /* A, MAIN THREAD: conversations to end at the next safe point (not forwarded) */

struct TkMirror { int active; int answered; unsigned int peer, convId, npcUid, targetUid; void* dlg; void* me; int seen; };   /* seen: fold 1 M5 - the window has shown dlg */
TkMirror g_tkMirror;                     /* B, MAIN THREAD: the one mirrored conversation (zero-initialised) */
void* volatile g_tkMirrorDlgAny = 0;     /* B: g_tkMirror.dlg for an any-thread compare */
volatile LONG g_tkMirrorClosedOff = 0;   /* B: the mirrored conversation ended off the main thread */
void* g_tkApplyDlg = 0;                  /* B, MAIN THREAD: set only around our own engine calls on the mirrored copy */
int   g_tkApplyStart = 0;                /* ... 1 only around our startPlayerConversation (its NPC-line _doActions is skipped) */
/* P26 stage 4 */
void* g_tkStartingDlg = 0;               /* P26s4 fold 1 M4: A, MAIN THREAD - the Dialogue inside startPlayerConversation now (its first line is not forwarded yet) */
unsigned int g_tkClosedPeer = 0, g_tkClosedConv = 0;   /* P26s4 fold 1 M1: B, MAIN THREAD - the last mirrored {peer, convId} whose window closed here */
void* g_tkApplyA = 0;                    /* A, MAIN THREAD: the Dialogue whose ANSWER replyClicked is applying - an end inside it is END ANSWERED */
std::vector<TkLater> g_tkLinkLaterA;     /* A: conversations of a link lost while the world was blocked (marks kept) - ended at the next safe point */
TkMirror g_tkLinkLaterB;                 /* B: the mirror of a link lost while the world was blocked (active = pending) */
long long g_tkApplied = 0, g_tkApplyFault = 0, g_tkNextPrompt = 0, g_tkNextUnfwd = 0, g_tkUnmapped = 0, g_tkTimeouts = 0,
          g_tkEndAnswered = 0, g_tkLinesDeferred = 0, g_tkActsDeferred = 0, g_tkItemLines = 0, g_tkLinkLater = 0,
          g_tkNextRecv = 0, g_tkNextShown = 0, g_tkNextFailed = 0;
/* P26 stage 5 - the router */
unsigned int g_tkActSeq = 0;             /* A, MAIN THREAD: the last ACT's seq (never 0 on the wire) */
long long g_tkActSent = 0, g_tkActSendFail = 0, g_tkActNpcRan = 0, g_tkActNpcFault = 0, g_tkActResOk = 0, g_tkActResRefused = 0,
          g_tkActRecv = 0, g_tkActApplied = 0, g_tkActRefused = 0, g_tkActFault = 0, g_tkActHires = 0, g_tkActUnsupported = 0,
          g_tkActLinesRun = 0;
void* g_tkActView[cooptalk::kTalkMaxActs];   /* MAIN THREAD: the action pointers of one viewed _doActions call */
/* P26s5 fold 1 - A, MAIN THREAD: a line's NPC-side part HELD until the other game answers its ACT (by seq) */
struct TkActHold { unsigned int seq, convId, peer, n; void* dlg; void* me; void* line; void* acts[cooptalk::kTalkMaxActs];
                   int endsTalk, endsType;
                   int endHeld, endReason; const char* endWhy;   /* P26s5 fold 2: the conversation's end held until this answer - its END reason and why (literals) */
                   int takeOrders; unsigned int npcUid, carriedUid; };   /* a carried-person line (10 / 30 / 40): takeOrders 1 = after APPLIED this
                   game gives its NPC (npcUid) the take and cage orders itself, on the person (carriedUid) the other player's character carries here */
/* P26s5 fold 2: TkActHold.line is the engine's SHARED DialogLineData (DialogDataManager's, never written or freed here - endDialogue
   0x673DA0 only clears the Dialogue's +0x198); the private 0x320 copy is made at run time inside TkRunViewPod (g_tkLineCopy). */
const size_t kTkActHoldCap = 16;
std::vector<TkActHold> g_tkActHolds;
long long g_tkActHeld = 0, g_tkActHoldRefused = 0, g_tkActHoldLost = 0, g_tkActHoldDropped = 0, g_tkActCarriedNone = 0,
          g_tkAct28Refused = 0, g_tkActEndsTalk = 0, g_tkNotices = 0;
/* carried-person lines: A - sent, the engine's case run here (this game owns the person), take orders given / not given / faulted;
   B - applied (hand-over marked), the engine's case run on our own character, refused NO_CARRY */
long long g_tkCarriedSent = 0, g_tkCarriedNpcEngine = 0, g_tkTakeOrders = 0, g_tkTakeRefused = 0, g_tkTakeFault = 0,
          g_tkCarriedApplied = 0, g_tkCarriedTalkerEngine = 0, g_tkCarriedNoCarry = 0;
long long g_tkEndHeld = 0, g_tkEndHeldRan = 0, g_tkEndHeldNotRun = 0, g_tkEndHeldRepeat = 0, g_tkAnsWhileHeld = 0;   /* P26s5 fold 2 */
volatile LONG64 g_tkEndReused = 0;   /* P26s5 fold 4: a new startConversation on a Dialogue whose forwarded conversation the engine had ended (any thread) */
long long g_tkEndKept = 0, g_tkEndReEnd = 0;   /* P26s5 fold 3: engine ends inside an applied answer that kept this game's side open; the engine's later ends of it passed */
const int kTkNoticeNotAvailable = 100;   /* TkRefusalNotice: TRADE / CHARACTER_EDITOR with another player's NPC */
/* P26s5 fold 1: the private copies of a line the view hands _doActions - DialogLineData is 0x320 bytes (operator new(800) before
   the constructor 0x67A5E0: 67d340:48/73/98, 6adff0:17; the constructor's highest write +0x310). One per nesting depth (an
   action can start a conversation whose first line comes back through the router inside the outer _doActions). */
const size_t kTkLineSize = 0x320;
const int kTkViewDepthMax = 4;
__declspec(align(16)) char g_tkLineCopy[kTkViewDepthMax][kTkLineSize];
int g_tkViewDepth = 0;
struct TkIn { cooptalk::TalkMsg m; unsigned int peer; LONG gen; };   /* gen: fold 1 L4 - the safe-point clock when it arrived */
std::vector<TkIn> g_tkInbound;           /* MAIN THREAD: arrived MSG_TALK, applied at the K2 safe point */

volatile LONG64 g_tkMarked = 0, g_tkMarkFull = 0, g_tkSuppressOn = 0, g_tkSuppressOff = 0, g_tkEndingOff = 0, g_tkOffNotStarted = 0;
long long g_tkPromptSent = 0, g_tkPromptNoReply = 0, g_tkNotOurs = 0, g_tkNotStarted = 0, g_tkSendFailed = 0, g_tkRestarted = 0,
          g_tkEndSent = 0, g_tkEndedLater = 0, g_tkOrphans = 0, g_tkStale = 0, g_tkAnsReply = 0, g_tkAnsMapped = 0, g_tkAnsClosed = 0,
          g_tkAnsOther = 0, g_tkAnsUnknown = 0, g_tkLinkEnded = 0,
          g_tkPromptRecv = 0, g_tkShown = 0, g_tkShowFailed = 0, g_tkBusy = 0, g_tkNoChar = 0, g_tkAnsSent = 0, g_tkClosedSent = 0,
          g_tkEndRecv = 0, g_tkEndUnknown = 0, g_tkActsSkipped = 0, g_tkClicks = 0, g_tkMismatch = 0, g_tkBad = 0, g_tkLogged = 0;
volatile LONG64 g_tkReleasedOff = 0, g_tkRelRefused = 0, g_tkNotCreatorOff = 0, g_tkGuiSkipped = 0;   /* P26s1 fold 1, any thread */
volatile LONG64 g_tkOwnWinRefused = 0, g_tkMirrorNewStart = 0;   /* P26s1 fold 2 N1 / N3, any thread */
long long g_tkReleased = 0, g_tkNotCreator = 0, g_tkSweptConv = 0, g_tkSweptMirror = 0, g_tkWinOther = 0, g_tkInDropped = 0,
          g_tkPromptAged = 0, g_tkHandMismatch = 0, g_tkIdTooLong = 0, g_tkFailLogged = 0, g_tkEndLaterSkipped = 0;   /* P26s1 fold 1 */

// ===========================================================================================
// P25 (.modding/investigations/p25-dialogue-actions-2026-09-30.md, protocol 109) - A PLAYER'S CONVERSATION WITH AN NPC THE OTHER
// GAME OWNS RUNS ON THAT GAME, THROUGH THE P26 ROAD (manager decisions 2026-09-30 (1)-(3)).
// B (the talker's game), ANY THREAD: Dialogue::startPlayerConversation 0x683890 toward THIS game's own player-faction character on a
//   Dialogue whose speaker (+0x150) is a copy the other game drives is NOT run (it returns 0 - the engine's own "not started", as its
//   camera gate gives) when the engine's own talk gate passes here (TkP25GatePod - the same test, 683890:31-34); the click goes into
//   a fixed lock-free ring and, at the K2 safe point, out as MSG_TALK REQUEST. No local window, no local line, no local action. The
//   NPC's game answers with the P26 PROMPT (its reqId) - B's mirror shows it - or END NOT_STARTED; the link down, a request already
//   waiting, or no answer within 30 s: the click does NOTHING (decision (1): one world, no local-only conversation), counted and
//   logged. A click whose NPC turns out to be this game's after all (the any-thread ownership read can miss) runs the engine's own
//   start at the safe point.
// A (the NPC's owner), K2 safe point: a REQUEST for its own, living, conscious, idle NPC and the sender's copy of the talker within the
//   talk gate of the NPC calls Dialogue::sendEvent(npc, copy, 1) - the engine's own player-talk entry - so _chooseDialog runs over
//   the REAL NPC's package and lock map, and the P26 hook marks and forwards the start (PROMPT with the reqId). Two holes a
//   requested start hits every time are closed here:
//   - THE CAMERA GATE (683890:31-34): startPlayerConversation starts only when `who` is within the float at 0x16FF49C (9216.0,
//     StartPlayerConvGateDist) of this game's VIEW POINT - PlayerInterface::0x7F1DC0 = PlayerInterface +0x30 -> +0x58
//     Ogre::Node::getPosition, the camera centre. The talker's copy is next to the NPC, not near A's camera. Scope of the bypass:
//     ONLY the call whose return address is 0x68391C (StartPlayerConvViewRet, startPlayerConversation's own call), ONLY on a thread
//     inside a SCOPED start - a start MARKED for the P26 road (who = the other player's character, a peer copy) whose speaker is an
//     NPC THIS game drives: the P26 walk-over arrival (any thread; manager 2026-09-30 after T706) and every P25 REQUEST start - the
//     view point is answered as that NPC's position, so the engine's gate becomes "the talker is within 9216 u of the NPC" (a P25
//     request is checked for that first: why far). Never for this game's own characters, never for a copy's start. Every other
//     caller (the threaded spawn check 0x8F4200, the camera itself) gets the original, untouched. talkP25Gate[scoped, bypassed (the
//     camera alone would have refused), refused (the gate refused even so: the talker beyond 9216 u of the NPC)].
//   - THE FIRST LINE (P26's g_tkStartingDlg leftover): the start's own first line runs inside startPlayerConversation before the
//     conversation is forwarded; with any target-side part (7 player-talk first lines, incl. Seto's / agnu's instant hire, which
//     would make the NPC join the stand-in faction on A) it is HELD (its _doActions is not run there) and, once the PROMPT went out,
//     handed to the P26 router (TalkDeferTargetActs): the target-side part goes to B as ACT after the PROMPT, the NPC-side part
//     waits for B's ACT_RESULT, as for any reply line.
// Both: TRADE / CHARACTER_EDITOR (1 / 11) end the conversation with the type on the END; B opens its OWN trade window over the NPC's
//   copy / the editor for its own character (decision (2)). Class-3 lines (character hand-overs) are refused by the P26 router;
//   carried-person lines (10 / 30 / 40) are split by owner (talkwire.h TalkCarriedNpcEngine / TalkCarriedTalkerEngine).
// ===========================================================================================
unsigned long long kP25ViewPointRva = 0; static coop::AddrReg kP25ViewPointRva_reg("PlayerInterfaceViewPoint", &kP25ViewPointRva);   /* Steam_1.0.65 0x7F1DC0 (1.0.68 0x7F2960): Vector3* (PlayerInterface*, Vector3* out) - +0x30 -> +0x58 Ogre::Node::getPosition, this game's view point */
unsigned long long kP25ViewRetRva = 0; static coop::AddrReg kP25ViewRetRva_reg("StartPlayerConvViewRet", &kP25ViewRetRva);   /* Steam_1.0.65 0x68391C (1.0.68 0x6843AC): startPlayerConversation's call of the view point returns here */
unsigned long long kP25GateDistRva = 0; static coop::AddrReg kP25GateDistRva_reg("StartPlayerConvGateDist", &kP25GateDistRva);   /* Steam_1.0.65 0x16FF49C (1.0.68 0x1700514): the talk gate, float 9216.0 (comiss 0x68393A) */
unsigned long long kP25PlayerIfaceRva = 0; static coop::AddrReg kP25PlayerIfaceRva_reg("PlayerInterfaceGlobal", &kP25PlayerIfaceRva);   /* Steam_1.0.65 0x2133630 (var) - hire.cpp binds the same row */
unsigned long long kP25TradeInstRva = 0; static coop::AddrReg kP25TradeInstRva_reg("TradeWindowInstance", &kP25TradeInstRva);   /* Steam_1.0.65 0x4FDE00: the trade window's singleton getter (DA_TRADE, 67fad0:1569) */
unsigned long long kP25TradeOpenRva = 0; static coop::AddrReg kP25TradeOpenRva_reg("TradeWindowOpen", &kP25TradeOpenRva);   /* Steam_1.0.65 0x951620: (singleton, Character* npc, Character* target, bool) - DA_TRADE's open (67fad0:1570) */
unsigned long long kP25CharEditorRva = 0; static coop::AddrReg kP25CharEditorRva_reg("PlayerInterfaceCharacterEditor", &kP25CharEditorRva);   /* Steam_1.0.65 0x7F2AD0: (PlayerInterface*, Character* target) - DA_CHARACTER_EDITOR (67fad0:1722) */
typedef void* (*P25ViewPointFn)(void* pi, void* out);
typedef void* (*P25TradeInstFn)();
typedef void (*P25TradeOpenFn)(void* inst, void* npc, void* target, char flag);
typedef void (*P25CharEditorFn)(void* pi, void* target);
typedef void* (*P25GetPosFn)(void* self, void* out);   /* the character's vt +0x40 getPosition (683890:32) */
P25ViewPointFn orig_p25ViewPoint = 0;
int g_p25ViewHook = 0;           /* 1 installed, -1 AddHook FAILED, -2 no table address */
int g_p25On = 0;                 /* the P26 road is on (g_tkOn): B intercepts, A answers a REQUEST */
void* g_p25StartDlg = 0;         /* A, MAIN THREAD: the Dialogue a REQUEST's own sendEvent runs on now */
unsigned int g_p25ReqIdNow = 0;  /* A, MAIN THREAD: ... and its reqId (the PROMPT carries it) */
void* g_p25FirstLine = 0;        /* A, MAIN THREAD: that start's first line, held (it has a target-side part) */
/* A, ANY THREAD: the scoped camera gate. A start of THIS game's NPC (speaker owner 1) toward the other player's character (a peer
   copy - TkPeerTarget), marked for the P26 road - the P26 walk-over arrival (usually the AI thread) and every P25 REQUEST start -
   runs with this thread's TLS slot pointing at a P25GateCtx on detour_startPlayerConv's stack; the view-point hook answers only
   that thread's call from startPlayerConversation. Never for this game's own characters (who is a peer copy). */
struct P25GateCtx { void* dlg; void* who; float camDist; float npcDist; int subst; };
volatile DWORD g_p25GateTls = TLS_OUT_OF_INDEXES;
volatile LONG g_p25GateArmedN = 0;           /* threads inside a scoped start now - the hook's first (cheap) test */
float g_p25CamDist = -1.0f;                  /* A, MAIN THREAD: the real view point's distance from the talker at the last main-thread scoped start */
volatile LONG64 g_p25GateSubst = 0, g_p25GateScoped = 0, g_p25GateBypassed = 0, g_p25GateRefused = 0;   /* any thread */
struct TkReqSlot { volatile LONG state; void* dlg; void* me; void* who; void* line; };   /* state 0 free, 1 writing, 2 ready */
const int kTkReqSlots = 8;
TkReqSlot g_tkReqSlots[kTkReqSlots];         /* B: intercepted clicks, any thread -> the safe point */
volatile LONG64 g_p25LocalHeld = 0, g_p25RingFull = 0, g_p25Dup = 0, g_p25GateLocal = 0, g_p25GateUnread = 0;
int g_p25NoIntercept = 0;        /* B, MAIN THREAD: 1 only around the engine's own start of a click that was not a copy after all */
struct TkReqPend { int active; unsigned int reqId, peer, npcUid, targetUid; unsigned long tick; };
TkReqPend g_p25Pend;             /* B, MAIN THREAD: the one REQUEST waiting for its PROMPT / END (zero-initialised) */
unsigned int g_p25NextReq = 0;
long long g_p25ReqSent = 0, g_p25Prompted = 0, g_p25Relocal = 0, g_p25EndUnknown = 0, g_p25TradeLocal = 0, g_p25EditorLocal = 0,
          g_p25LocalFault = 0, g_p25ReqRecv = 0, g_p25StartedA = 0, g_p25FirstLineHeld = 0, g_p25FirstLineRouted = 0,
          g_p25FirstLineDropped = 0;
long long g_p25WhyA[cooptalk::kTalkWhyCount];   /* A: END NOT_STARTED sent, by why */
volatile LONG64 g_p25MineMiss = 0, g_p25StartUnwound = 0, g_p25NestedCleared = 0;   /* P25 fold 1 [p25f1-glob]: any thread */
volatile LONG g_p25MissUid = 0;   /* P25 fold 1 L2: the last talker uid the owned mirror did not list (any thread -> the drain's recheck) */
long long g_p25MineMissConfirmed = 0, g_p25StalePrompt = 0, g_p25ThirdOwner = 0;   /* P25 fold 1: MAIN THREAD */
long long g_p25WhyB[cooptalk::kTalkWhyCount];   /* B: requests that opened nothing, by why (A's END, or B's own refusal) */
long long g_p25ReqNoLine = 0;   /* P25 fold 2 [p25f2-glob]: B, MAIN THREAD - requests sent for a click whose copy found no line */
int g_p25StartSeen = 0, g_p25StartHadLine = 0;   /* P25 fold 2: A, MAIN THREAD - the REQUEST's startPlayerConversation ran / had a line */

void TkLog(const std::string& s)
{
    if (g_tkLogged >= kTkLogCap) return;
    ++g_tkLogged;
    DebugLog(s);
}

/* P26s1 fold 1 L5: a refusal line has its own cap, so a busy session's ordinary lines never hide why something was not done. */
void TkLogFail(const std::string& s)
{
    if (g_tkFailLogged >= kTkFailLogCap) return;
    ++g_tkFailLogged;
    DebugLog(s);
}

/* 1 = who is a character of a peer's stand-in faction (a copy the other player drives). ANY THREAD: one guarded read, then
   pointer compares (IsPeerFaction). No C++ object here (C2712). */
int TkPeerTarget(void* who)
{
    if (who == 0) return 0;
    ::Faction* f = 0;
    __try { f = *(::Faction* const*)((const char*)who + kRootFaction); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return (f != 0 && IsPeerFaction(f)) ? 1 : 0;
}

int TkMarkFind(void* dlg)
{
    if (dlg == 0) return -1;
    for (int i = 0; i < kTkMarks; ++i) if (g_tkMarks[i].dlg == dlg) return i;
    return -1;
}

/* ANY THREAD. The Dialogue's mark (an existing one is kept as it is), -1 when the table is full (or the claim kept colliding), -2
   when the Dialogue's mark is RELEASED (5) and waits for its safe point - the new start is refused, as with a full table.
   P26s1 fold 1 M1: *created = 1 only when THIS call made the mark - only the creator may drop it for a start that did not happen.
   P26s1 fold 1 L1: after the claim every other slot is rescanned; a second slot holding the same Dialogue (another thread claimed
   between the find and the CAS) makes this claim back out and try again (both sides see each other at worst: both retry). */
int TkMarkAdd(void* dlg, int* created)
{
    *created = 0;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const int f = TkMarkFind(dlg);
        if (f >= 0)
        {
            if (g_tkMarks[f].state == 5) return -2;
            ::InterlockedExchange(&g_tkMarks[f].engEnded, 0);   /* P26s5 fold 4: a new forwarded start - TkAfterStart replaces the old record */
            return f;
        }
        int i = 0;
        for (; i < kTkMarks; ++i)
            if (::InterlockedCompareExchangePointer((PVOID volatile*)&g_tkMarks[i].dlg, dlg, 0) == 0) break;
        if (i == kTkMarks) return -1;
        int dup = 0;
        for (int j = 0; j < kTkMarks; ++j) if (j != i && g_tkMarks[j].dlg == dlg) { dup = 1; break; }
        if (dup) { ::InterlockedExchangePointer((PVOID volatile*)&g_tkMarks[i].dlg, 0); continue; }
        g_tkMarks[i].me = SayReadPtr(dlg, kDlgMe);
        ::InterlockedExchange(&g_tkMarks[i].engEnded, 0);   /* P26s5 fold 4 */
        ::InterlockedExchange(&g_tkMarks[i].gen, g_tkGen);
        ::InterlockedExchange(&g_tkMarks[i].state, 1);
        ::InterlockedIncrement64(&g_tkMarked);
        *created = 1;
        return i;
    }
    return -1;
}

/* P26s1 fold 1: 1 = dlg has a mark that keeps THIS game's window shut (starting, open, ending). A RELEASED mark (5) does not - the
   conversation on it is the engine's own. ANY THREAD. */
int TkMarkSuppresses(void* dlg)
{
    const int i = TkMarkFind(dlg);
    if (i < 0) return 0;
    const LONG st = g_tkMarks[i].state;
    if (g_tkMarks[i].dlg != dlg) return 0;   /* P26s1 fold 2 N4: the slot was reused between the find and the read - st is another Dialogue's */
    return (st >= 1 && st <= 4) ? 1 : 0;
}

/* P26s1 fold 2 N5: A, MAIN THREAD - move mark i to `to` (1 starting / 2 open) only from 1 or 2, by compare-and-swap: a mark an
   off-thread start released (5) or an off-thread end took (3) is never pulled back. 1 moved (or already there), 0 not. */
int TkMarkMove(int i, LONG to)
{
    if (i < 0 || i >= kTkMarks) return 0;
    const LONG st = g_tkMarks[i].state;
    if (st != 1 && st != 2) return 0;
    return (::InterlockedCompareExchange(&g_tkMarks[i].state, to, st) == st) ? 1 : 0;
}

void TkMarkDrop(int i)
{
    if (i < 0 || i >= kTkMarks) return;
    ::InterlockedExchange(&g_tkMarks[i].state, 0);
    ::InterlockedExchange(&g_tkMarks[i].engEnded, 0);   /* P26s5 fold 4 */
    g_tkMarks[i].me = 0;
    ::InterlockedExchangePointer((PVOID volatile*)&g_tkMarks[i].dlg, 0);
}

/* up to cap bytes of the std::string at p into out: the length copied, -1 unreadable. No C++ object here (C2712). */
int TkReadStrPod(const void* p, char* out, int cap)
{
    if (p == 0) return -1;
    __try
    {
        const std::string& s = *(const std::string*)p;
        size_t n = s.size();
        if (n > (size_t)cap) n = (size_t)cap;
        if (n > 0) std::memcpy(out, s.data(), n);
        return (int)n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

std::string TkStr(const void* p, int cap)
{
    char b[cooptalk::kTalkMaxText + 1];
    if (cap > (int)cooptalk::kTalkMaxText) cap = (int)cooptalk::kTalkMaxText;
    const int n = TkReadStrPod(p, b, cap);
    return n > 0 ? std::string(b, (size_t)n) : std::string();
}

/* P26s1 fold 1 L6: the full length of the std::string at p, -1 unreadable. No C++ object here (C2712). */
int TkStrLenPod(const void* p)
{
    if (p == 0) return -1;
    __try
    {
        const size_t n = ((const std::string*)p)->size();
        return n > 0x7FFFFFFF ? 0x7FFFFFFF : (int)n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* the std::vector<std::string> at dlg + beginOff / endOff: its first max elements, each cut to cap. Its size, -1 unreadable.
   over (P26s1 fold 1 L6, optional): set to 1 when one of the elements read is longer than cap. */
int TkReadStrVec(void* dlg, size_t beginOff, size_t endOff, std::vector<std::string>* out, int max, int cap, int* over = 0)
{
    out->clear();
    const char* b = (const char*)SayReadPtr(dlg, beginOff);
    const char* e = (const char*)SayReadPtr(dlg, endOff);
    if (b == 0 && e == 0) return 0;
    if (b == 0 || e < b || (size_t)(e - b) % sizeof(std::string) != 0) return -1;
    const int n = (int)((size_t)(e - b) / sizeof(std::string));
    if (n > 256) return -1;
    for (int i = 0; i < n && i < max; ++i)
    {
        const char* s = b + (size_t)i * sizeof(std::string);
        if (over != 0 && TkStrLenPod(s) > cap) *over = 1;
        out->push_back(TkStr(s, cap));
    }
    return n;
}

/* the GameData string id of a DialogLineData (+0x1D8 data -> stringID), at most cap bytes; -1 unreadable. No C++ object. */
int TkLineSidPod(const void* line, char* out, int cap)
{
    if (line == 0) return -1;
    __try
    {
        const ::GameData* gd = *(::GameData* const*)((const char*)line + kLineGameData);
        if (gd == 0) return -1;
        const std::string& s = gd->stringID;
        size_t n = s.size();
        if (n > (size_t)cap) return -1;   /* longer than the wire allows: not forwardable */
        if (n > 0) std::memcpy(out, s.data(), n);
        return (int)n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

std::string TkLineSid(const void* line)
{
    char b[cooptalk::kTalkMaxId + 1];
    const int n = TkLineSidPod(line, b, (int)cooptalk::kTalkMaxId);
    return n > 0 ? std::string(b, (size_t)n) : std::string();
}

/* A Character's Dialogue when it reads as one (me == c), else 0 (TalkDialogueOf's test). */
void* TkDialogueOf(void* c)
{
    if (c == 0 || SayPlausiblePtr(c) == 0) return 0;
    void* dlg = SayReadPtr(c, kCharDialogue);
    if (dlg == 0 || SayPlausiblePtr(dlg) == 0 || SayReadPtr(dlg, kDlgMe) != c) return 0;
    return dlg;
}

/* 1 = c is a live character now: the uid registry maps it back to itself, or it is in GameWorld's character update list.
   MAIN THREAD. Address compares only - c itself is not read. */
int TkLiveChar(void* c)
{
    if (c == 0) return 0;
    const unsigned int u = FindSpawnedUid(c);
    if (u != 0 && FindSpawned(u) == (::Character*)c) return 1;
    if (coop::GameWorldPtr() == 0) return 0;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return 0;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
        if ((void*)*it == c) return 1;
    return 0;
}

int TkLiveOwner(void* dlg, void* me) { return (dlg != 0 && me != 0 && TkLiveChar(me) && TkDialogueOf(me) == dlg) ? 1 : 0; }

/* The engine's endDialogue(dlg, true) WITHOUT our detour (the trampoline, or the entry when not hooked). 1 called, 0 not. */
int TkEndPod(void* dlg)
{
    TkEndDialogueFn fn = orig_endDialogue;
    if (fn == 0 && g_endDialogueHook != 1 && kTkEndDialogueRva != 0 && g_base != 0) fn = (TkEndDialogueFn)(g_base + (uintptr_t)kTkEndDialogueRva);
    if (fn == 0) return 0;
    __try { fn(dlg, true); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

std::string TkU(unsigned int v) { char b[16]; std::sprintf(b, "%u", v); return std::string(b); }
std::string TkI(long long v) { char b[24]; std::sprintf(b, "%lld", v); return std::string(b); }
std::string TkNameStr(void* c) { char n[48]; TalkName((::Character*)c, n); return std::string(n); }

void TkPurseTalkEnded(unsigned int uid);   /* below, with the purse table */

/* A, MAIN THREAD: end THIS game's side of a forwarded (or not-forwarded) conversation. The mark is kept through the engine's
   endDialogue (so its setInDialog(false) leaves the window alone), then dropped; END goes to the peer when sendReason != 0 and
   the conversation had been forwarded. callEnd 0 = the engine has already ended it. */
void TkEndOwn(void* dlg, void* me, int sendReason, const char* why, int callEnd, int endsType = 0)   /* P25: endsType on the END (1 / 11) */
{
    std::map<void*, TkConv>::iterator it = g_tkConvs.find(dlg);
    TkConv c;
    std::memset(&c, 0, sizeof(c));
    int had = 0;
    if (it != g_tkConvs.end()) { c = it->second; had = 1; g_tkConvs.erase(it); }
    if (had) TkPurseTalkEnded(c.targetUid);   /* a later talk never judges money replies by this talk's purse */
    if (had && c.engineEnded) callEnd = 0;   /* P26s5 fold 3: the engine already ended it - not again */
    if (me == 0 && had) me = c.me;
    const int i = TkMarkFind(dlg);
    if (i >= 0) { if (me == 0) me = g_tkMarks[i].me; ::InterlockedExchange(&g_tkMarks[i].state, 4); }
    int ended = -1;
    if (callEnd) ended = (!EngineWritesBlocked() && TkLiveOwner(dlg, me)) ? TkEndPod(dlg) : 0;
    if (i >= 0) TkMarkDrop(i);
    int sent = -1;
    if (had && sendReason != 0)
    {
        cooptalk::TalkMsg e;
        e.kind = cooptalk::kTalkEnd; e.convId = c.convId; e.npcUid = c.npcUid; e.targetUid = c.targetUid; e.reason = sendReason;
        e.endsType = endsType;   /* P25 step 5: the other game opens that window itself */
        sent = net::SendTalk(e) ? 1 : 0;
        if (sent) ++g_tkEndSent;
    }
    if (!had && ended != 1) return;   /* a not-forwarded start whose Dialogue is gone: counted by the caller only */
    TkLog("[TALK] A end conv=" + TkU(c.convId) + " npc uid=" + TkU(c.npcUid) + " -> target uid=" + TkU(c.targetUid)
          + " why=" + why + " endDialogue=" + TkI(ended) + " END=" + (sent < 0 ? std::string("not sent")
          : (sent ? std::string("sent ") + cooptalk::TalkEndName(sendReason) : std::string("FAILED (link down)"))));
}

/* P26s1 fold 1: A, MAIN THREAD - a not-forwarded conversation still waiting to be ended on dlg is not ended after all (a new start
   replaced it, or the Dialogue was released to a conversation of this game's own). */
void TkCancelEndLater(void* dlg)
{
    for (size_t k = 0; k < g_tkEndLater.size(); )
        if (g_tkEndLater[k].dlg == dlg) g_tkEndLater.erase(g_tkEndLater.begin() + (long)k); else ++k;
}

/* P26s1 fold 1 H1: a start on a MARKED Dialogue toward a character that is NOT the other player's (this game's own player's
   character, or anyone) releases the mark BEFORE the original runs: the new conversation is the engine's own - its window opens,
   its lines reach the window, and nothing of ours ends it. MAIN THREAD: the old forwarded conversation (if any) ends now (END
   RESTARTED; no engine call - the new start replaces it) and a pending 'end later' is cancelled. OFF THREAD: the mark flips to
   RELEASED (5) from starting / open / ending-off-thread (never from 4 - our own end is already dropping it); the safe point
   finishes it. No C++ object on the off-thread path. */
void TkRelease(void* dlg)
{
    if (SayOnMainThread())
    {
        ++g_tkReleased;
        TkCancelEndLater(dlg);
        TkEndOwn(dlg, 0, cooptalk::kTalkEndRestarted, "the NPC started a conversation with a character that is not the other player's", 0);
        return;
    }
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const int i = TkMarkFind(dlg);
        if (i < 0) return;
        const LONG st = g_tkMarks[i].state;
        if (st < 1 || st > 3) return;
        if (::InterlockedCompareExchange(&g_tkMarks[i].state, 5, st) != st) continue;
        if (g_tkMarks[i].dlg != dlg) { ::InterlockedCompareExchange(&g_tkMarks[i].state, st, 5); continue; }   /* the slot was reused: put it back */
        ::InterlockedIncrement64(&g_tkReleasedOff);
        return;
    }
}

/* P26s1 fold 1 L3: 1 = the Dialogue's conversation target (the hand at +0x158) names who - the five id dwords at hand +0x8..+0x18
   equal who's own hand's (Character +0x58, H027; hand::operator=(RootObject*) 0x791A70 copies who +0x60..+0x70). No C++ object. */
int TkHandIs(void* dlg, void* who)
{
    if (dlg == 0 || who == 0) return 0;
    __try
    {
        const unsigned int* a = (const unsigned int*)((const char*)dlg + kDlgTargetHand + 8);
        const unsigned int* b = (const unsigned int*)((const char*)who + kCharHand + 8);
        for (int k = 0; k < 5; ++k) if (a[k] != b[k]) return 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* P26s1 fold 1: an int at base + off, 0 when unreadable. No C++ object here (C2712). */
int TkReadIntPod(const void* base, size_t off)
{
    if (base == 0) return 0;
    __try { return *(const int*)((const char*)base + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* P26s1 fold 2 N2: a byte at base + off, -1 when unreadable. No C++ object here (C2712). */
int TkReadBytePod(const void* base, size_t off)
{
    if (base == 0) return -1;
    __try { return (int)*(const unsigned char*)((const char*)base + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* P26s1 fold 1 H3 / fold 2 N2: 1 = this Dialogue is in a conversation now - its 'ended' flag (+0x148) is 0 AND it has a current line
   (+0x198) or a conversation event (+0x188), the engine's own test (0x683890:25 / 0x684B30:10). +0x188 is not cleared by every end:
   without the flag one old conversation would keep the Dialogue busy for good. An unreadable flag counts as not busy (as before). */
int TkDlgBusy(void* dlg) { return (TkReadBytePod(dlg, kDlgEnded) == 0 && (SayReadPtr(dlg, kDlgCurLine) != 0 || TkReadIntPod(dlg, kDlgConvEvent) != 0)) ? 1 : 0; }

/* P26s1 fold 1 H3 / M5: the Dialogue THIS game's one dialogue window shows now, 0 none; (void*)1 when the window's address is not
   in the table (nothing can be told). */
void* TkWindowDlg()
{
    if (kDialogueWindowRva == 0 || g_base == 0) return (void*)1;
    void* w = SayReadPtr((void*)(g_base + (uintptr_t)kDialogueWindowRva), 0);
    return (w != 0) ? SayReadPtr(w, kWinDialogue) : 0;
}

/* A, MAIN THREAD: the PROMPT for the conversation now on dlg. 1 built; 0 no current line (the engine already ended it); -1 a
   current line but no reply (or unreadable); -2 the line's string id is unreadable or too long; -3 (P26s1 fold 1 L6) a reply's
   string id is longer than the wire allows - the other game could not name it back. */
int TkBuildPrompt(void* dlg, const TkConv& c, int ev, cooptalk::TalkMsg* p)
{
    void* cur = SayReadPtr(dlg, kDlgCurLine);
    if (cur == 0) return 0;
    p->kind = cooptalk::kTalkPrompt; p->convId = c.convId; p->npcUid = c.npcUid; p->targetUid = c.targetUid;
    p->event = (ev < 0 || ev > 254) ? 255 : ev;
    p->lineSid = TkLineSid(cur);
    if (p->lineSid.empty()) return -2;
    p->npcText = TkStr((const char*)dlg + kDlgNpcText, (int)cooptalk::kTalkMaxText);
    int over = 0;
    const int n = TkReadStrVec(dlg, kDlgReplyBegin, kDlgReplyEnd, &p->replyIds, (int)cooptalk::kTalkMaxReplies, (int)cooptalk::kTalkMaxId, &over);
    if (n <= 0) return -1;
    if (over) return -3;   /* P26s1 fold 1 L6 */
    TkReadStrVec(dlg, kDlgReplyTextBegin, kDlgReplyTextEnd, &p->replyTexts, (int)cooptalk::kTalkMaxReplies, (int)cooptalk::kTalkMaxText);
    while (p->replyTexts.size() < p->replyIds.size()) p->replyTexts.push_back(std::string());
    while (p->replyTexts.size() > p->replyIds.size()) p->replyTexts.pop_back();
    p->deadlineMs = 0;   /* P26s5 fold 5 (decision 229): no answer deadline - 0 = none (talkwire.h) */
    return 1;
}

/* A, MAIN THREAD: after a start toward a peer's character - direct (right after the original returned) or from the ring (the
   K2 safe point). mark = the Dialogue's mark. */
void TkAfterStart(void* dlg, void* who, int started, int ev, int mark, int created)
{
    std::map<void*, TkConv>::iterator old = g_tkConvs.find(dlg);
    if (!started)
    {
        ++g_tkNotStarted;
        if (!created) { ++g_tkNotCreator; return; }   /* P26s1 fold 1 M1: a mark this call did not make is not this call's to drop */
        if (old == g_tkConvs.end() && g_tkMarks[mark].state == 1) TkMarkDrop(mark);
        return;
    }
    TkCancelEndLater(dlg);   /* P26s1 fold 1: the new start replaced a not-forwarded one still waiting to be ended */
    ::InterlockedExchange(&g_tkMarks[mark].gen, g_tkGen);
    unsigned int oldTarget = 0;   /* the replaced talk's target: its purse goes once this start has ended or replaced it */
    if (old != g_tkConvs.end())
    {
        const TkConv o = old->second;
        oldTarget = o.targetUid;
        g_tkConvs.erase(old);
        ++g_tkRestarted;
        cooptalk::TalkMsg e;
        e.kind = cooptalk::kTalkEnd; e.convId = o.convId; e.npcUid = o.npcUid; e.targetUid = o.targetUid; e.reason = cooptalk::kTalkEndRestarted;
        if (net::SendTalk(e)) ++g_tkEndSent;
        TkLog("[TALK] A conv=" + TkU(o.convId) + " replaced by a new start on the same Dialogue - END RESTARTED");
    }
    void* me = SayReadPtr(dlg, kDlgMe);
    const unsigned int nuid = (me != 0) ? FindSpawnedUid(me) : 0;
    const unsigned int tuid = FindSpawnedUid(who);
    unsigned int peer = 0;
    const char* why = 0;
    if (nuid == 0) why = "the NPC has no uid (the other game has no copy of it)";
    else if (!net::IsUidMine(nuid)) why = "the NPC is a copy the other game drives (its owner decides)";
    else if (tuid == 0) why = "the target has no uid";
    else if (net::IsUidMine(tuid)) why = "the target is this game's own";
    else if (!net::UidOwnerPeer(tuid, &peer)) why = "no peer has claimed the target";
    else if (!TkHandIs(dlg, who)) { why = "the Dialogue's conversation target (+0x158) is not that character"; ++g_tkHandMismatch; }   /* P26s1 fold 1 L3 */
    TkConv c;
    std::memset(&c, 0, sizeof(c));
    c.npcUid = nuid; c.targetUid = tuid; c.peer = peer; c.me = me;
    cooptalk::TalkMsg p;
    int built = 0;
    if (why == 0)
    {
        c.convId = ++g_tkNextConv;
        if (c.convId == 0) c.convId = ++g_tkNextConv;
        built = TkBuildPrompt(dlg, c, ev, &p);
        if (built == 1 && dlg == g_p25StartDlg) p.reqId = g_p25ReqIdNow;   /* P25: the REQUEST this start answers */
        if (built == 0)
        {
            ++g_tkPromptNoReply; TkMarkDrop(mark);
            if (oldTarget != 0) TkPurseTalkEnded(oldTarget);
            TkLog("[TALK] A start toward uid=" + TkU(tuid) + " ended inside the engine (no current line) - nothing to forward");
            return;
        }
        if (built == -1) why = "the line has no reply the target could give";
        else if (built == -2) why = "the line's string id is unreadable or longer than 128 bytes";
        else if (built == -3) why = "a reply's string id is longer than 128 bytes (the other game could not name it back)";
        if (built == -3) ++g_tkIdTooLong; else if (built < 0) ++g_tkPromptNoReply;   /* P26s1 fold 1 L6 */
    }
    else ++g_tkNotOurs;
    if (why == 0)
    {
        if (!TkMarkMove(mark, 2)) why = "the Dialogue's mark was released or taken by an off-thread end first";   /* P26s1 fold 2 N5 */
        else if (!net::SendTalk(p)) { ++g_tkSendFailed; why = "the PROMPT could not be sent (link down)"; }
    }
    if (why != 0)
    {
        if (oldTarget != 0) TkPurseTalkEnded(oldTarget);   /* the replaced talk ended and nothing took its place */
        if (!TkMarkMove(mark, 1))   /* P26s1 fold 2 N5: released (5) / ending (3) - not this start's to keep shut; the safe point finishes it */
        {
            TkLogFail("[TALK] A start npc uid=" + TkU(nuid) + " -> target uid=" + TkU(tuid) + " NOT forwarded: " + why
                      + " - the mark is no longer this start's (released or ending); the safe point finishes it");
            return;
        }
        TkLater l; l.dlg = dlg; l.me = me;
        g_tkEndLater.push_back(l);
        TkLogFail("[TALK] A start npc uid=" + TkU(nuid) + " '" + TkNameStr(me) + "' -> target uid=" + TkU(tuid) + " '" + TkNameStr(who)
              + "' NOT forwarded: " + why + " - this game's window kept shut; the conversation ends at the next safe point");
        return;
    }
    c.line = SayReadPtr(dlg, kDlgCurLine); c.seq = 1; c.sentTick = ::GetTickCount(); c.ev = p.event;   /* P26 stage 4 */
    g_tkConvs[dlg] = c;
    if (oldTarget != 0 && oldTarget != c.targetUid) TkPurseTalkEnded(oldTarget);   /* after the new talk is in the table: the replaced target's purse */
    ++g_tkPromptSent;
    std::string rs;
    for (size_t k = 0; k < p.replyIds.size(); ++k) rs += (k ? " | " : "") + p.replyIds[k] + ":'" + p.replyTexts[k].substr(0, 60) + "'";
    char evs[8];
    std::sprintf(evs, "0x%X", (unsigned int)p.event);
    TkLog("[TALK] A PROMPT sent conv=" + TkU(c.convId) + " npc uid=" + TkU(nuid) + " '" + TkNameStr(me) + "' -> target uid="
          + TkU(tuid) + " '" + TkNameStr(who) + "' peer=" + TkU(peer) + " ev=" + evs + " line=" + p.lineSid + " replies="
          + TkI((long long)p.replyIds.size()) + " text='" + p.npcText.substr(0, 120) + "' [" + rs + "]" + (p.reqId != 0 ? " req=" + TkU(p.reqId) + " (P25: the other player's own click)" : std::string(""))
          + " - this game's window kept shut; seq=1, no answer deadline");   /* P26s5 fold 5 */
}

/* A: a start the stage-0 ring carried to the safe point (MAIN THREAD). live = the NPC and the target passed the drain's checks. */
/* P26s1 fold 1 H1: the ring carries EVERY off-thread startPlayerConversation, so only a live entry toward the other player's character
   may touch the mark - a start toward anyone else released it (TkRelease) and is the engine's own. A stale entry no longer drops
   the mark: an open conversation whose NPC is gone is ended by the liveness sweep (M2), a start never forwarded by the orphan sweep. */
void TkFromRing(void* dlg, void* who, int ev, int live)
{
    const int i = TkMarkFind(dlg);
    if (i < 0) return;
    if (!live) { ++g_tkStale; return; }
    if (!TkPeerTarget(who)) return;
    const LONG st = g_tkMarks[i].state;
    if (st != 1 && st != 2) return;   /* released (5) or ending (3 / 4): the safe point finishes it */
    TkAfterStart(dlg, who, 1, ev, i, 0);
}

/* B: close the mirror (MAIN THREAD). */
void TkMirrorOff()
{
    g_tkMirror.active = 0;
    g_tkMirror.dlg = 0;
    ::InterlockedExchangePointer((PVOID volatile*)&g_tkMirrorDlgAny, 0);
}

/* P26s1 fold 1 M2 / M4: 1 = the mirrored NPC copy is still that uid's character and still owns the mirrored Dialogue (TkApplyEnd's
   test). MAIN THREAD. */
int TkMirrorLive(const TkMirror& m)
{
    return (m.dlg != 0 && m.me != 0 && FindSpawned(m.npcUid) == (::Character*)m.me && TkDialogueOf(m.me) == m.dlg) ? 1 : 0;
}

/* B: the mirrored window ended outside our own calls - ANSWER CLOSED (always: P26s4 fold 1 M1). Off the main thread only a
   flag is set; the safe point finishes it. */
void TkMirrorClosed(void* dlg, const char* why)
{
    if (SayOnMainThread() == 0) { ::InterlockedExchange(&g_tkMirrorClosedOff, 1); return; }
    if (!g_tkMirror.active || dlg != g_tkMirror.dlg || dlg == g_tkApplyDlg) return;
    const TkMirror m = g_tkMirror;
    TkMirrorOff();
    g_tkClosedPeer = m.peer; g_tkClosedConv = m.convId;   /* P26s4 fold 1 M1: a later PROMPT naming it is answered CLOSED, never re-opened */
    /* P26s4 fold 1 M1: CLOSED goes also after this game's reply - in stage 4 the NPC's game goes on after an ANSWER (its next line
       comes as a PROMPT), so it must learn the window closed; a CLOSED that reaches it after its side ended is ignored there. */
    cooptalk::TalkMsg a;
    a.kind = cooptalk::kTalkAnswer; a.convId = m.convId; a.npcUid = m.npcUid; a.targetUid = m.targetUid;
    a.result = cooptalk::kTalkAnsClosed; a.index = -1;
    const int sent = net::SendTalk(a) ? 1 : 0;
    if (sent) ++g_tkClosedSent;
    TkLog("[TALK] B window closed conv=" + TkU(m.convId) + " by " + why + " - ANSWER CLOSED " + (sent ? "sent" : "FAILED (link down)")
          + (m.answered ? " (after this game's reply - the NPC's game ends its side)" : ""));
}

/* A character's faction's purse - Faction (+0x10) -> Ownerships (+0x80) -> money (+0x88): hire.cpp MoneyPod's reads (Confirmed
   there), the object _doActions' TAKE_MONEY / GIVE_MONEY write (67fad0:1636, 1688) and the one the engine's money condition reads
   for the local player (0x670AA0). 1 read, 0 not. ANY THREAD, no C++ object. */
int TkPurseMoneyPod(void* c, int* out)
{
    if (c == 0) return 0;
    __try
    {
        const char* f = *(const char* const*)((const char*)c + kRootFaction);
        if (f == 0) return 0;
        const char* o = *(const char* const*)(f + 0x80);
        if (o == 0) return 0;
        *out = *(const int*)(o + 0x88);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* B, MAIN THREAD: the player clicked reply `index` in the mirrored window - ANSWER, the engine's own reply is NOT run here. */
void TkMirrorAnswer(int index)
{
    ++g_tkClicks;
    if (g_tkMirror.answered) { TkLog("[TALK] B click index=" + TkI(index) + " conv=" + TkU(g_tkMirror.convId) + " ignored - already answered, waiting for the NPC's game"); return; }
    std::vector<std::string> ids;
    TkReadStrVec(g_tkMirror.dlg, kDlgReplyBegin, kDlgReplyEnd, &ids, 256, (int)cooptalk::kTalkMaxId);
    cooptalk::TalkMsg a;
    a.kind = cooptalk::kTalkAnswer; a.convId = g_tkMirror.convId; a.npcUid = g_tkMirror.npcUid; a.targetUid = g_tkMirror.targetUid;
    a.result = cooptalk::kTalkAnsReply; a.index = index;
    a.replyId = (index >= 0 && (size_t)index < ids.size()) ? ids[(size_t)index] : std::string();
    {   /* the talker's purse: the NPC's game judges the next line's money replies by it */
        int pv = -1;
        ::Character* const me = FindSpawned(g_tkMirror.targetUid);
        a.purse = (me != 0 && TkPurseMoneyPod(me, &pv)) ? pv : -1;
    }
    const int sent = net::SendTalk(a) ? 1 : 0;
    if (sent) { ++g_tkAnsSent; g_tkMirror.answered = 1; }
    TkLog("[TALK] B ANSWER " + std::string(sent ? "sent" : "FAILED (link down)") + " conv=" + TkU(a.convId) + " index=" + TkI(index)
          + " id=" + a.replyId + " - the reply runs on the NPC's game; the window stays until its next line (PROMPT) or its END");
}

/* A: a start toward the other player's character keeps THIS game's window shut. */
void detour_setInDialog(void* dlg, bool on)
{
    if (g_tkOn && TkMarkSuppresses(dlg))   /* P26s1 fold 1: a RELEASED mark no longer keeps the window shut */
    {
        ::InterlockedIncrement64(on ? &g_tkSuppressOn : &g_tkSuppressOff);
        return;
    }
    if (!on && dlg != 0 && dlg == g_tkMirrorDlgAny) TkMirrorClosed(dlg, "setInDialog(false)");   /* B */
    orig_setInDialog(dlg, on);
}

/* P26s5 fold 2 - A, MAIN THREAD: the index of the newest held line of dlg's forwarded conversation convId, -1 none; *npcParts =
   the NPC-side actions all its held lines still wait to run. */
static int TkHoldLast(void* dlg, unsigned int convId, unsigned int* npcParts)
{
    int last = -1;
    *npcParts = 0;
    for (size_t k = 0; k < g_tkActHolds.size(); ++k)
        if (g_tkActHolds[k].dlg == dlg && g_tkActHolds[k].convId == convId) { last = (int)k; *npcParts += g_tkActHolds[k].n; }
    return last;
}

/* P26s5 fold 2 - A, MAIN THREAD. SINGLE-PLAYER ORDER: _doActions runs the whole line (both halves), then the conversation ends
   (0x683170: the reply's _doActions, then sayLine 0x682F10 of the next line, which ends a player conversation at 682f10:29-35).
   A forwarded line's NPC-side part waits for the other game's ACT_RESULT (fold 1), so an end of that conversation while a held
   line still has an NPC-side part is HELD on its newest held line: TkApplyActResult runs it after the NPC-side part (APPLIED) or
   with nothing run (a refusal). Not after the end instead: endDialogue 0x673DA0 clears the dialog package +0x190, the event
   +0x188, the current line +0x198, the +0x2A0 hand and +0xC8 (its full end, +0x2C8 == 0xB - sayLine sets it, 682f10:24), and
   _doActions reads +0x190 for the line's locks and DA_LOCK_THIS_DIALOG 36 (67fad0:187, 1204) and writes +0xC8 (67fad0:367).
   The held end starts the conversation's 30 s ACT_RESULT clock (P26s5 fold 5: endClock; TkTimeouts then ends it with nothing run); link loss, the liveness
   sweep, a restart and an off-thread end still end it at once (the held line then finds no conversation and runs nothing).
   1 = held (the caller must NOT end it now), 0 = end it as before. why: a literal (kept in the hold). */
static int TkHoldEndFor(void* dlg, int reason, const char* why, const char* whose)
{
    std::map<void*, TkConv>::iterator it = g_tkConvs.find(dlg);
    if (it == g_tkConvs.end() || SayOnMainThread() == 0) return 0;
    unsigned int parts = 0;
    const int hh = TkHoldLast(dlg, it->second.convId, &parts);
    if (hh < 0) return 0;
    TkActHold& h = g_tkActHolds[(size_t)hh];
    if (h.endHeld) { ++g_tkEndHeldRepeat; return 1; }   /* asked again (Dialogue::update 0x684600:115 / 153): already held */
    if (parts == 0) return 0;   /* nothing of the NPC's side waits: the end runs now, as before */
    h.endHeld = 1; h.endReason = reason; h.endWhy = why;
    it->second.endClock = 1; it->second.endTick = ::GetTickCount();   /* P26s5 fold 5: the held end's ACT_RESULT clock - the only clock left (decision 229) */
    ++g_tkEndHeld;
    TkLog("[TALK] A conv=" + TkU(h.convId) + " " + whose + " - end held until the answer to ACT seq=" + TkU(h.seq) + ": its NPC-side part ("
          + TkU(parts) + " action(s)) runs first, then the conversation ends (single-player order); no ACT_RESULT within 30 s - nothing runs");   /* P26s5 fold 5 */
    return 1;
}

/* A: an open forwarded conversation the engine ends sends END. B: the mirrored window's end is ANSWER CLOSED. */
void detour_endDialogue(void* dlg, bool fin)
{
    const int i = g_tkOn ? TkMarkFind(dlg) : -1;
    const LONG st = (i >= 0) ? g_tkMarks[i].state : 0;
    if (dlg != 0 && dlg == g_tkMirrorDlgAny) TkMirrorClosed(dlg, "endDialogue");
    if (st == 2 && SayOnMainThread())   /* P26s5 fold 3: a conversation the engine already ended (its closing line said after the end) */
    {
        std::map<void*, TkConv>::iterator ee = g_tkConvs.find(dlg);
        if (ee != g_tkConvs.end() && ee->second.engineEnded)   /* the engine's own later end (update 0x684600:115 / 153): to the engine, this side stays */
        {
            ++g_tkEndReEnd;
            orig_endDialogue(dlg, fin);
            return;
        }
    }
    /* P26s5 fold 2: a main-thread end (definitelyTheEnd) of an open forwarded conversation whose held line's NPC-side part waits */
    if (st == 2 && fin && SayOnMainThread()
        && TkHoldEndFor(dlg, dlg == g_tkApplyA ? cooptalk::kTalkEndAnswered : cooptalk::kTalkEndEngine,
                        dlg == g_tkApplyA ? "the engine ended it while applying the answer (endDialogue)" : "the engine ended it (endDialogue)",
                        dlg == g_tkApplyA ? "the engine's end inside the applied answer (endDialogue)" : "the engine's end (endDialogue)"))
        return;
    orig_endDialogue(dlg, fin);
    if (st != 2) return;
    if (SayOnMainThread())
    {
        /* P26s5 fold 3: inside the applied answer the engine's end is not this game's end yet - sayLine ends a player conversation
           whose line has no children and THEN says that closing line (682f10:33-35, :84 -> 0x682E80 -> _doActions 682e80:21):
           the conversation and its mark stay, so the router splits the closing line like a reply line; TkAfterApply ends it */
        std::map<void*, TkConv>::iterator ce = (dlg == g_tkApplyA) ? g_tkConvs.find(dlg) : g_tkConvs.end();
        if (ce != g_tkConvs.end())
        {
            ce->second.engineEnded = 1;
            if (i >= 0 && g_tkMarks[i].dlg == dlg) ::InterlockedExchange(&g_tkMarks[i].engEnded, 1);   /* P26s5 fold 4: for detour_startConv (any thread) */
            ++g_tkEndKept;
            TkLog("[TALK] A conv=" + TkU(ce->second.convId) + " the engine ended it inside the applied answer (endDialogue) - this game's side"
                  " stays open until the answer is applied: a closing line said after the end goes through the router");
        }
        else if (dlg == g_tkApplyA)   /* P26 stage 4: the engine ended it while applying B's answer */
        {
            ++g_tkEndAnswered;
            TkEndOwn(dlg, 0, cooptalk::kTalkEndAnswered, "the engine ended it while applying the answer (endDialogue)", 0);
        }
        else TkEndOwn(dlg, 0, cooptalk::kTalkEndEngine, "the engine ended it (endDialogue)", 0);
    }
    else if (::InterlockedCompareExchange(&g_tkMarks[i].state, 3, 2) == 2)
    {
        if (g_tkMarks[i].dlg != dlg) ::InterlockedCompareExchange(&g_tkMarks[i].state, 2, 3);   /* P26s1 fold 1 L2: the slot now holds another Dialogue - its state goes back */
        else ::InterlockedIncrement64(&g_tkEndingOff);
    }
}

/* B: a reply clicked in the mirrored window goes back as ANSWER instead of running here. */
void detour_replyClicked(void* dlg, int index)
{
    if (SayOnMainThread() != 0 && g_tkMirror.active && dlg == g_tkMirror.dlg && dlg != g_tkApplyDlg)
    {
        TkMirrorAnswer(index);
        return;
    }
    orig_replyClicked(dlg, index);
}

/* P26s1 fold 1 H2: sayLine's setConversationReplyGUI 0x6736E0 (setNPCText on the ONE window) and listPlayerReplies'
   setResponesGUI 0x6735E0 (setResponses) do not ask whose conversation the window shows - a marked Dialogue's line would rewrite
   THIS game's own player's window (its text and replies; a click would then run the wrong reply). Skipped for a marked Dialogue,
   as setInDialog is. Off the main thread each queues itself (ops 6 / 5) and is replayed through the same entry: the skip covers
   both. ANY THREAD - no C++ object. */
void detour_setConvReplyGui(void* dlg)
{
    if (g_tkOn && TkMarkSuppresses(dlg)) { ::InterlockedIncrement64(&g_tkGuiSkipped); return; }
    orig_setConvReplyGui(dlg);
}

void detour_setResponsesGui(void* dlg)
{
    if (g_tkOn && TkMarkSuppresses(dlg)) { ::InterlockedIncrement64(&g_tkGuiSkipped); return; }
    orig_setResponsesGui(dlg);
}

unsigned long long detour_startConv(void* dlg, void* who, void* line, int ev, char force)
{
    /* P26s5 fold 4: the engine already ended this Dialogue's forwarded conversation (fold 3 keeps this game's side open for a closing
       line's answer) and now starts a new one on it: that side ends FIRST (TkRelease - main thread END RESTARTED now, off the main
       thread mark -> 5 for the safe point; no C++ object on that path), so the router, the window writers and a held part never take
       the new conversation for the old one. The held part then does not run (endHeldNotRun). */
    if (g_tkOn && dlg != 0)
    {
        const int mi = TkMarkFind(dlg);
        if (mi >= 0 && g_tkMarks[mi].engEnded != 0 && g_tkMarks[mi].dlg == dlg) { ::InterlockedIncrement64(&g_tkEndReused); TkRelease(dlg); }
    }
    const unsigned long long r = orig_startConv(dlg, who, line, ev, force);   /* always, unchanged */
    if (SayOnMainThread() == 0) { TalkOffNote(0, dlg, who, line, ev, r); return r; }   /* P26 stage 0 fold 1: recorded, read later */
    TalkNote(0, dlg, who, line, ev, r);
    return r;
}

int TkS6Owner(void* c);   /* P25: P26s6's owner read (defined below) */

/* P25: A - the scoped start's view point becomes its NPC's position: the NPC (Dialogue +0x150) and the talker are read through their
   own vt +0x40 getPosition, as startPlayerConversation reads the talker (683890:32); the real view point's distance from the talker
   (camDist) and the NPC's (npcDist) are kept for the counters. ANY THREAD, no C++ object. */
void TkP25SubstPod(P25GateCtx* x, float* o)
{
    __try
    {
        void* const me = *(void* const*)((const char*)x->dlg + kDlgMe);
        if (me == 0 || x->who == 0) return;
        float n[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float w[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        void* const* vn = *(void* const* const*)me;
        ((P25GetPosFn)vn[0x40 / sizeof(void*)])(me, n);
        void* const* vw = *(void* const* const*)x->who;
        ((P25GetPosFn)vw[0x40 / sizeof(void*)])(x->who, w);
        const float cx = o[0] - w[0], cy = o[1] - w[1], cz = o[2] - w[2];
        const float nx = n[0] - w[0], ny = n[1] - w[1], nz = n[2] - w[2];
        x->camDist = sqrtf(cx * cx + cy * cy + cz * cz);
        x->npcDist = sqrtf(nx * nx + ny * ny + nz * nz);
        o[0] = n[0]; o[1] = n[1]; o[2] = n[2];
        x->subst = 1;
        ::InterlockedIncrement64(&g_p25GateSubst);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}

/* P25: A - the view point startPlayerConversation's camera gate compares against. The original always runs; ONLY its call from
   startPlayerConversation (return address StartPlayerConvViewRet) on a thread whose TLS slot holds a scoped start's context is
   answered as that start's NPC position. ANY THREAD: the armed count is read first; the thread's last error is kept; no C++ object. */
void* detour_p25ViewPoint(void* pi, void* out)
{
    void* const r = orig_p25ViewPoint(pi, out);
    if (g_p25GateArmedN != 0 && out != 0 && kP25ViewRetRva != 0 && (uintptr_t)_ReturnAddress() == g_base + (uintptr_t)kP25ViewRetRva)
    {
        const DWORD ti = g_p25GateTls;
        if (ti != TLS_OUT_OF_INDEXES)
        {
            const DWORD le = ::GetLastError();
            P25GateCtx* const x = (P25GateCtx*)::TlsGetValue(ti);
            ::SetLastError(le);
            if (x != 0) TkP25SubstPod(x, (float*)out);
        }
    }
    return r;
}

/* P25: B - the engine's own talk gate for who, as startPlayerConversation makes it (683890:31-34): who's position (vt +0x40) within
   the gate float (0x16FF49C) of this game's view point (0x7F1DC0 on the PlayerInterface). 1 passes, 0 not (the engine would not
   start either), -1 cannot be told (a fault / no row). ANY THREAD, no C++ object. */
int TkP25GatePod(void* who)
{
    if (who == 0 || g_base == 0 || kP25ViewPointRva == 0 || kP25GateDistRva == 0 || kP25PlayerIfaceRva == 0) return -1;
    __try
    {
        void* const pi = *(void* const*)(g_base + (uintptr_t)kP25PlayerIfaceRva);
        if (pi == 0) return -1;
        float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float w[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        const P25ViewPointFn vf = (orig_p25ViewPoint != 0) ? orig_p25ViewPoint : (P25ViewPointFn)(g_base + (uintptr_t)kP25ViewPointRva);
        vf(pi, v);
        void* const* vt = *(void* const* const*)who;
        ((P25GetPosFn)vt[0x40 / sizeof(void*)])(who, w);
        const float gate = *(const float*)(g_base + (uintptr_t)kP25GateDistRva);
        const float dx = w[0] - v[0], dy = w[1] - v[1], dz = w[2] - v[2];
        return (dx * dx + dy * dy + dz * dz <= gate * gate) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* P25: B - one intercepted click into the ring (a click already waiting on the same Dialogue is not queued twice). ANY THREAD:
   Interlocked only, no allocation, no log. 1 queued (or already waiting), 0 the ring is full (the click does nothing). */
int TkP25Queue(void* dlg, void* me, void* who, void* line)
{
    for (int i = 0; i < kTkReqSlots; ++i)
        if (g_tkReqSlots[i].state == 2 && g_tkReqSlots[i].dlg == dlg) { ::InterlockedIncrement64(&g_p25Dup); return 1; }
    for (int i = 0; i < kTkReqSlots; ++i)
    {
        if (::InterlockedCompareExchange(&g_tkReqSlots[i].state, 1, 0) != 0) continue;
        g_tkReqSlots[i].dlg = dlg; g_tkReqSlots[i].me = me; g_tkReqSlots[i].who = who; g_tkReqSlots[i].line = line;
        ::InterlockedExchange(&g_tkReqSlots[i].state, 2);
        return 1;
    }
    ::InterlockedIncrement64(&g_p25RingFull);
    return 0;
}

/* P25: B - 1 = this start is this game's own player's character talking to an NPC copy the other game drives: it becomes a REQUEST
   and the engine's start is NOT run here (the caller returns 0). ANY THREAD: guarded reads, pointer compares, the any-thread owned
   mirror; no C++ object. */
int TkP25Intercept(void* dlg, void* who, void* line)
{
    if (!g_p25On || dlg == 0 || who == 0) return 0;
    /* P25 fold 2 [p25f2-a]: a start with NO line (this game's copy found nothing to say) still becomes a REQUEST when it is the
       player's talk-to event (1) - the copy's out-of-date state (AI off, its own town / lock state) must not veto a conversation the
       real NPC would have, nor allow one it refuses: the NPC's owner decides. Any other no-line start stays the engine's. */
    const int onMainI = (SayOnMainThread() != 0) ? 1 : 0;
    if (!cooptalk::TalkP25ClickEvent(line != 0 ? 1 : 0, onMainI ? g_talkCurEvent : TalkOffThreadEvent())) return 0;
    if (onMainI && (g_p25NoIntercept != 0 || dlg == g_tkApplyDlg)) return 0;
    void* const me = SayReadPtr(dlg, kDlgMe);
    if (me == 0 || me == who) return 0;
    const int spk = TkS6Owner(me);
    ::Faction* const wf = (::Faction*)SayReadPtr(who, kRootFaction);
    const unsigned int wu = FindSpawnedUid(who);
    const int own = (wu != 0 && net::IsUidMineAnyThread(wu) && wf != 0 && IsPlayerFaction(wf)) ? 1 : 0;
    /* P25 fold 1 L2 [p25f1-L2a]: a talker of this game's player faction with a uid the any-thread owned mirror does not list, on a start
       that WOULD be a REQUEST were it listed: the mirror may have missed it - the start runs locally as before (the engine's call is
       already under way; no main-thread table here), but it is counted and its uid left for the drain's main-thread recheck. */
    if (own == 0 && wu != 0 && wf != 0 && IsPlayerFaction(wf) && cooptalk::TalkP25Intercept(1, 1, spk, TkPeerTarget(me), 0))
    {
        ::InterlockedIncrement64(&g_p25MineMiss);
        ::InterlockedExchange(&g_p25MissUid, (LONG)wu);
    }
    if (!cooptalk::TalkP25Intercept(1, own, spk, TkPeerTarget(me), 0)) return 0;
    const int gate = TkP25GatePod(who);
    if (gate == 0) { ::InterlockedIncrement64(&g_p25GateLocal); return 0; }   /* the engine's own gate refuses it here too: as before */
    if (gate < 0) ::InterlockedIncrement64(&g_p25GateUnread);
    ::InterlockedIncrement64(&g_p25LocalHeld);
    TkP25Queue(dlg, me, who, line);
    return 1;
}

/* P25 fold 1 C1 + L3 [p25f1-C1h]: the engine's startPlayerConversation with this thread's start context set and ALWAYS restored.
   gx != 0 (a scoped start): this thread's TLS slot -> gx and g_p25GateArmedN + 1 for the call. gx == 0 while any start is scoped
   (ArmedN != 0): this thread's slot is CLEARED for the call when it holds an outer scoped start's context (L3: a nested unscoped start
   never answers the camera gate as the outer start's NPC). On the main thread g_tkStartingDlg = dlg for the call (P26s4 fold 1 M4).
   All three are restored in __finally, so a fault inside the engine's call cannot leave the slot pointing at a dead stack frame or
   the armed count up (C1). The fault is NOT caught here: it propagates exactly as before - TalkSendPod's __except still counts it and
   answers "faulted", a caller with no guard still sees the engine's own fault - because catching it would hand every caller a
   "not started" for a start the engine half-ran (its window / lock state partly set), which no caller expects. The unwind is counted
   (talkP25f1 startUnwound). ANY THREAD; no C++ object (C2712); the thread's last error kept. */
static unsigned long long TkP25CallStart(void* dlg, void* who, void* line, P25GateCtx* gx, int onMain)
{
    unsigned long long r = 0;
    void* const prevStarting = g_tkStartingDlg;
    const DWORD gti = g_p25GateTls;
    void* volatile prevCtx = 0;
    volatile int swapped = 0, armed = 0;
    __try
    {
        if (onMain) g_tkStartingDlg = dlg;
        if (gti != TLS_OUT_OF_INDEXES && (gx != 0 || g_p25GateArmedN != 0))
        {
            const DWORD le = ::GetLastError();
            void* const cur = ::TlsGetValue(gti);
            if (gx != 0 || cur != 0)
            {
                prevCtx = cur;
                ::TlsSetValue(gti, gx);
                swapped = 1;
                if (gx == 0) ::InterlockedIncrement64(&g_p25NestedCleared);
            }
            ::SetLastError(le);
        }
        if (gx != 0)
        {
            ::InterlockedIncrement(&g_p25GateArmedN);
            armed = 1;
            ::InterlockedIncrement64(&g_p25GateScoped);
        }
        r = orig_startPlayerConv(dlg, who, line);   /* the same arguments, the result unchanged */
    }
    __finally
    {
        if (armed) ::InterlockedDecrement(&g_p25GateArmedN);
        if (swapped)
        {
            const DWORD le = ::GetLastError();
            ::TlsSetValue(gti, prevCtx);
            ::SetLastError(le);
        }
        if (onMain) g_tkStartingDlg = prevStarting;
        if (AbnormalTermination()) ::InterlockedIncrement64(&g_p25StartUnwound);
    }
    return r;
}

unsigned long long detour_startPlayerConv(void* dlg, void* who, void* line)
{
    /* P26 stages 1-3: a start toward the OTHER player's character is marked FIRST (any thread), so this game's window stays shut;
       with no free mark the start is refused (the NPC simply does not start - never a window for the other player's character) */
    /* P26s1 fold 1 H1: a start on a MARKED Dialogue toward anyone else releases the mark first (TkRelease). */
    int mark = -1, created = 0;
    if (g_tkOn && dlg != 0)
    {
        if (TkPeerTarget(who))
        {
            /* P26s1 fold 2 N1: this game's own window shows dlg (its player is talking to this NPC) - a start toward the other player's
               character is refused: its mark would swallow the engine's setInDialog(dlg, 0) and freeze this game's window. TkWindowDlg
               is two SEH-guarded pointer reads, no C++ object - any thread. */
            if (TkWindowDlg() == dlg) { ::InterlockedIncrement64(&g_tkOwnWinRefused); return 0; }
            mark = TkMarkAdd(dlg, &created);
            if (mark == -2) { ::InterlockedIncrement64(&g_tkRelRefused); return 0; }
            if (mark < 0) { ::InterlockedIncrement64(&g_tkMarkFull); return 0; }
        }
        else if (TkMarkFind(dlg) >= 0) TkRelease(dlg);
    }
    /* P26s1 fold 2 N3: B - a start on the mirrored Dialogue that is not our own apply (this game's player talking to the NPC copy)
       ends the mirror first, so replyClicked stops turning that player's clicks into ANSWERs. g_tkApplyDlg is main-thread only; an
       off-thread start is never ours (TkMirrorClosed only sets a flag there). */
    if (dlg != 0 && dlg == g_tkMirrorDlgAny && (SayOnMainThread() == 0 || dlg != g_tkApplyDlg))
    {
        ::InterlockedIncrement64(&g_tkMirrorNewStart);
        TkMirrorClosed(dlg, "a new start on the mirrored Dialogue");
    }
    /* P25: B - this game's own player's character talking to an NPC copy the other game drives: the NPC's game runs the conversation
       (REQUEST at the safe point); no local window, no local line - the start returns 0, as the engine's own gate refusal does */
    if (TkP25Intercept(dlg, who, line)) return 0;
    const int onMain = (SayOnMainThread() != 0) ? 1 : 0;   /* P25 fold 1 C1 [p25f1-C1a]: g_tkStartingDlg (P26s4 fold 1 M4) is set and restored in TkP25CallStart */
    if (onMain && dlg != 0 && dlg == g_p25StartDlg && !g_p25StartSeen)   /* P25 fold 2 [p25f2-b1]: A - the REQUEST's own start ran */
    {
        g_p25StartSeen = 1;
        g_p25StartHadLine = (line != 0) ? 1 : 0;
    }
    /* P25 (manager 2026-09-30): THE SCOPED CAMERA GATE - a MARKED start (the other player's character as `who`: a peer copy) of an
       NPC THIS game drives - the P26 walk-over arrival on any thread, a P25 REQUEST start - runs with this thread's gate context, so
       startPlayerConversation's view point is its NPC (the talker must be within 9216 u of the NPC, not of this game's camera).
       Nothing else is scoped: not this game's own characters, not a copy's start, not a start the table could not mark. */
    P25GateCtx gx;
    gx.dlg = dlg; gx.who = who; gx.camDist = -1.0f; gx.npcDist = -1.0f; gx.subst = 0;
    const DWORD gti = g_p25GateTls;
    const int scoped = (mark >= 0 && g_p25ViewHook == 1 && gti != TLS_OUT_OF_INDEXES && TkS6Owner(SayReadPtr(dlg, kDlgMe)) == 1) ? 1 : 0;
    /* P25 fold 1 C1/L3 [p25f1-C1b]: the slot, the armed count and g_tkStartingDlg are set, cleared and restored in TkP25CallStart */
    const unsigned long long r = TkP25CallStart(dlg, who, line, scoped ? &gx : 0, onMain);
    if (scoped)
    {
        const float gate = (g_base != 0 && kP25GateDistRva != 0) ? *(const float*)(g_base + (uintptr_t)kP25GateDistRva) : 0.0f;
        if (gx.subst && gx.camDist > gate) ::InterlockedIncrement64(&g_p25GateBypassed);   /* the camera gate alone would have refused this start */
        if (gx.subst && (r & 0xFF) == 0 && gx.npcDist > gate) ::InterlockedIncrement64(&g_p25GateRefused);   /* refused by the gate even with the NPC as view point */
        if (onMain) g_p25CamDist = gx.camDist;
    }
    if (SayOnMainThread() == 0)   /* P25 fold 1 C1 [p25f1-C1c]: g_tkStartingDlg was restored in TkP25CallStart */
    {
        TalkOffNote(1, dlg, who, line, TalkOffThreadEvent(), r);   /* P26 stage 0 fold 1; a marked start is forwarded from the drain */
        if (mark >= 0 && (r & 0xFF) == 0)
        {
            ::InterlockedIncrement64(&g_tkOffNotStarted);
            if (!created) ::InterlockedIncrement64(&g_tkNotCreatorOff);   /* P26s1 fold 1 M1 */
            else if (g_tkMarks[mark].state == 1) TkMarkDrop(mark);
        }
        return r;
    }
    TalkNote(1, dlg, who, line, g_talkCurEvent, r);
    if (mark >= 0) TkAfterStart(dlg, who, (r & 0xFF) != 0 ? 1 : 0, g_talkCurEvent, mark, created);   /* P26 stages 1-3 */
    return r;
}

// ===========================================================================================
// P26 STAGE 6 (P26s6) - ON THE NPC'S GAME (A): THE OTHER PLAYER'S CHARACTER COUNTS AS A PLAYER FOR THE NATURAL APPROACH.
// Single player: an NPC sees the player's character (SensoryData::assessNeutral 0x8587B0, event 3), picks a line whose conditions
// need DC_IS_PLAYER (0x24) on the target, and the line's TALK_TO_LEADER (2) sends its squad leader over (order 0x66); the arrival
// fires event 1 and startPlayerConversation opens the window. On A the other player's character is a copy in a stand-in faction
// whose Faction +0x250 (PlayerInterface* isPlayer) is 0, so every one of those steps says "not a player" (Read: 675ec0:412,
// 672160:232, 0x6823B6 - Confirmed bytes). Game data (Read, fcs parse): 113 of 251 event-3 dialogues are player-gated and 26 of
// the 27 that walk over (TALK_TO_LEADER) are - "You Stop!", "Give us your stuff!", muggers, Holy Nation / Shek / Empire gate
// checks. The stand-in's flag itself is never touched (353 inline engine tests read it: UI, AI, crime, towns, saves); instead
// three narrow gates answer "player" for the other player's character, ONLY when the speaking NPC is one THIS game drives
// (its uid is ours - an NPC the other game drives, or one with no copy there, stays vanilla):
//   S6-1 Dialogue::_checkCondition 0x675EC0 - DC_IS_PLAYER toward it answers as for a player: (0 < val) (675ec0:66, :412).
//   S6-2 Dialogue::getSpeaker 0x672160 - T_TARGET_IF_PLAYER (2) resolves to it through the same hand T_TARGET (1) reads
//        (672160:200-229 = the case-1 road without the +0x250 test, Read).
//   S6-3 hire.cpp detour_doActions (any thread) - a line with TALK_TO_LEADER toward it gives the engine's own order
//        (0x6823C4-0x682411): Character::issueOrder(ActivePlatoon::getSquadLeader(Character::getSquad(npc)), 0, 0x66, target,
//        false, true, *AddOrderArgGlobal) and *(*(*(npc+0x650)+0x20)+0x264) = 1. The target is the one _doActions acts on
//        (67fad0:280-365: the conversation hand +0x158, as T_TARGET), resolved BEFORE the line runs.
// The walk-over's event 1 then reaches detour_startPlayerConv (AI thread: mark, ring, K2 safe point - stages 1-5 unchanged).
// THREADING: all three run on the AI thread. Each is a compare first; then one guarded read (TkPeerTarget), then the uid index
// (FindSpawnedUid: address compares) and net::IsUidMineAnyThread - no lock, no allocation, no log; Interlocked counters
// only (talkP26s6 on the REPORT line). IsUidMineAnyThread answers from the owned mirror: while that may miss a uid, "not ours"
// - stage 6 then does nothing (fails closed). Deferred (LOW): enemy / ally sight events 0x42 / 0x43 (remarks only), A's camera
// gate in startPlayerConversation (9216 units), the target-side part of a spoken (non-window) line - counted as chainTgt.
// ===========================================================================================
unsigned long long kS6CheckCondRva = 0; static coop::AddrReg kS6CheckCondRva_reg("DialogueCheckCondition", &kS6CheckCondRva);   /* Steam_1.0.65 0x675EC0: bool Dialogue::_checkCondition(DialogConditionEnum, ComparisonEnum, int val, Character* target, Character* actualConversationTarget) */
unsigned long long kS6GetSpeakerRva = 0; static coop::AddrReg kS6GetSpeakerRva_reg("DialogueGetSpeaker", &kS6GetSpeakerRva);   /* Steam_1.0.65 0x672160: Character* Dialogue::getSpeaker(TalkerEnum who, DialogLineData*, bool isForWordswaps) */
unsigned long long kS6SquadLeaderRva = 0; static coop::AddrReg kS6SquadLeaderRva_reg("ActivePlatoon_leader", &kS6SquadLeaderRva);   /* Steam_1.0.65 0x791FF0: Character* ActivePlatoon::getSquadLeader() */
unsigned long long kS6GetPlatoonRva = 0; static coop::AddrReg kS6GetPlatoonRva_reg("Character_getSquad", &kS6GetPlatoonRva);   /* Steam_1.0.65 0x790F70 - gamecalls.cpp binds the same row */
unsigned long long kS6AddOrderRva = 0; static coop::AddrReg kS6AddOrderRva_reg("AddOrder", &kS6AddOrderRva);   /* Steam_1.0.65 0x5D1640 - spawn.cpp binds the same row */
unsigned long long kS6AddOrderArgRva = 0; static coop::AddrReg kS6AddOrderArgRva_reg("AddOrderArgGlobal", &kS6AddOrderArgRva);   /* Steam_1.0.65 0x2246C68 - issueOrder's 7th argument (0x6823DB) */
typedef bool (*S6CheckCondFn)(void* dlg, int cond, int cmp, int val, void* target, void* convTarget);
typedef void* (*S6GetSpeakerFn)(void* dlg, int who, void* line, char forWordswaps);
typedef void* (*S6GetPlatoonFn)(void* ch);
typedef void* (*S6SquadLeaderFn)(void* platoon);
typedef void (*S6AddOrderFn)(void* who, void* building, int taskType, void* target, char playerOrder, char a6, unsigned long long a7);
S6CheckCondFn  orig_s6CheckCond = 0;
S6GetSpeakerFn orig_s6GetSpeaker = 0;
int g_s6CondHook = 0, g_s6SpkHook = 0;   /* 1 installed, -1 AddHook FAILED, -2 no table address, 0 not tried */
int g_s6On = 0;                          /* both hooks and forwarding (g_tkOn) - set once at install */
const int kS6DcIsPlayer = 0x24;          /* DialogConditionEnum DC_IS_PLAYER */
const int kS6DcMoney = 2;                /* DialogConditionEnum: the money condition 0x670AA0 answers from THIS game's player's purse */
const int kS6TalkerTarget = 1, kS6TalkerTargetIfPlayer = 2;   /* TalkerEnum T_TARGET / T_TARGET_IF_PLAYER */
const int kS6ActTalkToLeader = 2;        /* DialogActionEnum DA_TALK_TO_LEADER (67fad0:1541) */
const int kS6TaskSeekTalk = 0x66;        /* SEEK_AND_TALK_AND_SEND_SIGNAL: lea r8d, [rbx + 0x64] with rbx = 2 (0x6823F6) */
const int kS6ActsCap = 32;               /* actions read per line */
const size_t kS6CharAi = 0x650, kS6AiSub = 0x20, kS6TalkFlag = 0x264;   /* 0x6823FF-0x682411 */
volatile LONG64 g_s6CondPeer = 0, g_s6CondNotOurs = 0, g_s6SpkPeer = 0, g_s6SpkNotOurs = 0, g_s6LeadSeen = 0, g_s6LeadOrdered = 0,
                g_s6LeadNotOurs = 0, g_s6LeadNoTarget = 0, g_s6LeadFault = 0, g_s6LeadNoAddr = 0, g_s6ChainTgt = 0;
/* P26s6 fold 1: noUid - the speaking NPC has no uid (the other game has no copy of it; was counted as notOurs); spkActsRefused - S6-2
   withheld the other player's character from a line that carries actions / items / relation effects; spkNoDlg - that character had
   no Dialogue; leaderNotOurs / leaderNoUid - the squad leader the order would go to is not this game's; flagFault - the +0x264
   write faulted after the order went out; orderMix - a TALK_TO_LEADER line that also carries another order-giving action (6, 16,
   17, 29, 34, 40, 52 - 67fad0 cases that call issueOrder, Read); viewLead - orders given from the router's NPC-side run. */
volatile LONG64 g_s6NoUid = 0, g_s6SpkActsRefused = 0, g_s6SpkNoDlg = 0, g_s6LeadLeaderNotOurs = 0, g_s6LeadLeaderNoUid = 0,
                g_s6LeadFlagFault = 0, g_s6OrderMix = 0, g_s6ViewLead = 0;

/* The other player's purse as its game last reported it (REQUEST, ANSWER REPLY, ACT_RESULT's purse after the act), moved by an ACT's
   money actions when this game sends it (TkPurseActSent) and dropped when this game's side of the talk ends (TkPurseTalkEnded): the engine's money
   condition reads only THIS game's player's purse, so an NPC this game drives judges that player's money replies by this table.
   Written on the MAIN THREAD, read from any thread (detour_s6CheckCond): a slot's uid and purse travel together in one interlocked
   64-bit value (uid high half, purse low half, 0 = empty); peer and when are main-thread only (when: the slot reused is the oldest). */
const int kTkPurseSlots = 8;
struct TkPurseSlot { volatile LONG64 v; unsigned int peer; unsigned long when; };
TkPurseSlot g_tkPurse[kTkPurseSlots];
volatile LONG64 g_s6MoneyAnswered = 0, g_s6MoneyNoPurse = 0;
volatile LONG64 g_s6MoneyDiffers = 0;   /* answers by the talker's purse that differ from the engine's own (this game's player's purse) */
long long g_tkPurseSet = 0, g_tkPurseCleared = 0;   /* MAIN THREAD */
long long g_tkPurseActMoved = 0;   /* MAIN THREAD: saved purses moved by an ACT's money actions when it was sent */

/* MAIN THREAD: the uid's purse; purse < 0 (unknown) forgets the uid's value. */
void TkPurseNote(unsigned int uid, int purse, unsigned int peer)
{
    if (uid == 0) return;
    int at = -1, empty = -1, oldest = 0;
    for (int i = 0; i < kTkPurseSlots; ++i)
    {
        const LONG64 v = ::InterlockedCompareExchange64(&g_tkPurse[i].v, 0, 0);
        if (v != 0 && (unsigned int)((unsigned long long)v >> 32) == uid) { at = i; break; }
        if (v == 0 && empty < 0) empty = i;
        if ((long)(g_tkPurse[i].when - g_tkPurse[oldest].when) < 0) oldest = i;
    }
    if (purse < 0)
    {
        if (at >= 0) { ::InterlockedExchange64(&g_tkPurse[at].v, 0); ++g_tkPurseCleared; }
        return;
    }
    if (at < 0) at = (empty >= 0) ? empty : oldest;
    g_tkPurse[at].peer = peer;
    g_tkPurse[at].when = ::GetTickCount();
    ::InterlockedExchange64(&g_tkPurse[at].v, (LONG64)(((unsigned long long)uid << 32) | (unsigned long long)(unsigned int)purse));
    ++g_tkPurseSet;
}

/* ANY THREAD: 1 and *out = the uid's last reported purse, 0 none. No C++ object. */
int TkPurseFind(unsigned int uid, int* out)
{
    if (uid == 0) return 0;
    for (int i = 0; i < kTkPurseSlots; ++i)
    {
        const LONG64 v = ::InterlockedCompareExchange64(&g_tkPurse[i].v, 0, 0);
        if (v != 0 && (unsigned int)((unsigned long long)v >> 32) == uid)
        {
            *out = (int)(unsigned int)((unsigned long long)v & 0xFFFFFFFFull);
            return 1;
        }
    }
    return 0;
}

/* MAIN THREAD: 1 = a forwarded conversation this game runs is open with the character uid. */
int TkTalkerInConv(unsigned int uid)
{
    if (uid == 0) return 0;
    for (std::map<void*, TkConv>::const_iterator it = g_tkConvs.begin(); it != g_tkConvs.end(); ++it)
        if (it->second.targetUid == uid) return 1;
    return 0;
}

/* MAIN THREAD: this game's side of a talk with uid ended (or a REQUEST started nothing): its saved purse is dropped unless another
   conversation with that character is still open here - with no entry the engine's own answer applies. */
void TkPurseTalkEnded(unsigned int uid)
{
    if (uid == 0 || TkTalkerInConv(uid)) return;
    TkPurseNote(uid, -1, 0);
}

/* MAIN THREAD: an ACT with money actions just went to the talker's game, which applies them before its window shows the next line,
   while this game's engine may choose that line's money replies before the ACT_RESULT comes back: the talker's saved purse moves
   by the line's money now (TalkPurseAfterAct); the ACT_RESULT's purse after the act replaces it. Only a saved purse is moved.
   1 moved (*before / *after), 0 not. */
int TkPurseActSent(unsigned int uid, const std::vector<int>& types, const std::vector<int>& values, int* before, int* after)
{
    if (uid == 0) return 0;
    for (int i = 0; i < kTkPurseSlots; ++i)
    {
        const LONG64 v = ::InterlockedCompareExchange64(&g_tkPurse[i].v, 0, 0);
        if (v == 0 || (unsigned int)((unsigned long long)v >> 32) != uid) continue;
        *before = (int)(unsigned int)((unsigned long long)v & 0xFFFFFFFFull);
        *after = cooptalk::TalkPurseAfterAct(*before, types, values);
        if (*after == *before) return 0;
        TkPurseNote(uid, *after, g_tkPurse[i].peer);
        ++g_tkPurseActMoved;
        return 1;
    }
    return 0;
}

/* A reply-list mismatch's two id lists, for the first kTkIdsLogCap mismatches (then nothing). MAIN THREAD. */
const int kTkIdsLogCap = 16;
int g_tkIdsLogged = 0;
std::string TkIdsMismatch(const std::vector<std::string>& here, const std::vector<std::string>& prompt)
{
    if (g_tkIdsLogged >= kTkIdsLogCap) return std::string();
    ++g_tkIdsLogged;
    std::string s = "; ids here [";
    for (size_t k = 0; k < here.size(); ++k) { if (k) s += ","; s += here[k]; }
    s += "] in the PROMPT [";
    for (size_t k = 0; k < prompt.size(); ++k) { if (k) s += ","; s += prompt[k]; }
    s += "]";
    return s;
}

/* P26s6: 1 = the Dialogue's speaker (+0x150) is a replicated character THIS game drives. ANY THREAD: one guarded read, the uid index
   (FindSpawnedUid - address compares, spawn.h) and net::IsUidMineAnyThread (the owned mirror; "not mine" while it may miss a uid -
   stage 6 then does nothing). No C++ object. */
/* P26s6 fold 1: 1 = a character this game drives, 0 = one the other game drives (or unreadable), -1 = no uid (not replicated: the other
   game has no copy). ANY THREAD, same reads as above. */
int TkS6Owner(void* c)
{
    if (c == 0) return 0;
    const unsigned int u = FindSpawnedUid(c);
    if (u == 0) return -1;
    return net::IsUidMineAnyThread(u) ? 1 : 0;
}

int TkS6NpcOurs(void* dlg)
{
    return TkS6Owner(SayReadPtr(dlg, kDlgMe));
}

/* P26s6 fold 1: 1 = the line carries something to run - actions (+0x210 count), givesItem (+0xB0), factionRelationEffects (map size
   +0x2F0) - the fields TkLineActsPod / TkRunViewPod use; unreadable = 1 (fails closed). No C++ object. NOT covered (re-check 2026-09-30, LOW): _doActions also reads campaignTriggers +0x28, crowdTrigger +0x2C8, playerInterruptionDialog +0x310, line locks +0x288/+0x2A0/+0x2B8, +0x230 and the squad's said-line note +0x1D8 - a speaker-2 line carrying only those still goes to B's copy and runs them on A against the copy's Dialogue (layouts unread; see the P26 row). */
int TkS6LineHasActs(const void* line)
{
    if (line == 0) return 0;
    __try
    {
        const char* L = (const char*)line;
        return (*(const unsigned int*)(L + 0x210) != 0 || *(const int*)(L + 0xB0) != 0 || *(const unsigned long long*)(L + 0x2F0) != 0) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

/* The money condition toward the other player's character (the conversation target first, then the target), asked by an NPC this
   game drives: answered with that player's purse as its game last reported it (TkPurseFind); none reported = the engine's answer.
   ANY THREAD: guarded reads, the uid index, the interlocked table, Interlocked counters. No C++ object. */
bool TkS6MoneyCond(void* dlg, int cmp, int val, void* target, void* convTarget, bool engine)
{
    void* const who = TkPeerTarget(convTarget) ? convTarget : (TkPeerTarget(target) ? target : 0);
    if (who == 0 || TkS6NpcOurs(dlg) != 1) return engine;
    const unsigned int uid = FindSpawnedUid(who);
    int purse = -1;
    if (!TkPurseFind(uid, &purse)) { ::InterlockedIncrement64(&g_s6MoneyNoPurse); return engine; }
    ::InterlockedIncrement64(&g_s6MoneyAnswered);
    const bool ans = cooptalk::TalkMoneyCond(cmp, val, purse);
    if (ans != engine)
    {   /* the first 20, then every 100th; DebugLog takes the log's own lock, so a line from any thread is safe */
        const LONG64 n = ::InterlockedIncrement64(&g_s6MoneyDiffers);
        if (n <= 20 || n % 100 == 0)
        {
            char b[160];
            std::sprintf(b, "[TALK] money check uid=%u price=%d purse=%d engine=%d answered=%d", uid, val, purse, engine ? 1 : 0, ans ? 1 : 0);
            DebugLog(b);
        }
    }
    return ans;
}

/* P26s6 S6-1 (any thread, hot): the original always runs with the same arguments; two conditions asked by an NPC this game drives
   toward the other player's character are answered for that player: DC_IS_PLAYER (as for a player) and the money condition (by
   that player's saved purse, TkS6MoneyCond). */
bool detour_s6CheckCond(void* dlg, int cond, int cmp, int val, void* target, void* convTarget)
{
    const bool r = orig_s6CheckCond(dlg, cond, cmp, val, target, convTarget);
    if (cond == kS6DcMoney && g_s6On) return TkS6MoneyCond(dlg, cmp, val, target, convTarget, r);
    if (cond != kS6DcIsPlayer || !g_s6On || !TkPeerTarget(target)) return r;
    const int own = TkS6NpcOurs(dlg);   /* P26s6 fold 1: -1 no uid, counted apart */
    if (own != 1) { ::InterlockedIncrement64(own < 0 ? &g_s6NoUid : &g_s6CondNotOurs); return r; }
    ::InterlockedIncrement64(&g_s6CondPeer);
    return 0 < val;   /* the engine's answer for a player target: (isPlayer != 0) == (0 < val) (675ec0:66, :412) */
}

/* P26s6 S6-2 (any thread): T_TARGET_IF_PLAYER that found no player resolves to the other player's character when the conversation
   target (T_TARGET, the same hand +0x158) is that character and the NPC is this game's. */
void* detour_s6GetSpeaker(void* dlg, int who, void* line, char forWordswaps)
{
    void* const r = orig_s6GetSpeaker(dlg, who, line, forWordswaps);
    if (r != 0 || who != kS6TalkerTargetIfPlayer || !g_s6On || !StandInAnyAnyThread()) return r;   /* P26s6 fold 1: no stand-in, no retry */
    void* const t = orig_s6GetSpeaker(dlg, kS6TalkerTarget, line, 0);
    if (t == 0 || !TkPeerTarget(t)) return r;
    const int own = TkS6NpcOurs(dlg);
    if (own != 1) { ::InterlockedIncrement64(own < 0 ? &g_s6NoUid : &g_s6SpkNotOurs); return r; }
    /* P26s6 fold 1: the line is then said on THAT character's own Dialogue (triggerNextLine 0x683C90 -> sayLine on getSpeaker's
       +0x280) - never marked, so its _doActions would run here, unforwarded, against the copy. A line with anything to run is not
       given the copy: the engine gets 0, as before stage 6 (the spoken chain ends, 683c90). Word swaps are answered as before. */
    if (TkDialogueOf(t) == 0) { ::InterlockedIncrement64(&g_s6SpkNoDlg); return r; }
    if (!forWordswaps && TkS6LineHasActs(line)) { ::InterlockedIncrement64(&g_s6SpkActsRefused); return r; }
    ::InterlockedIncrement64(&g_s6SpkPeer);
    return t;
}

const char* TalkHookState(int s)
{
    return s == 1 ? "installed" : (s == -1 ? "FAILED" : (s == -2 ? "noAddress" : "notInstalled"));
}

void InstallTalkStarters()
{
    /* P26 stage 0 fold 1: the off-thread event slot, allocated before InstallSpeech hooks sendEvent */
    if (g_talkTls == TLS_OUT_OF_INDEXES) g_talkTls = ::TlsAlloc();
    if (g_p25GateTls == TLS_OUT_OF_INDEXES) g_p25GateTls = ::TlsAlloc();   /* P25: the scoped camera gate's per-thread context */
    if (g_talkTls == TLS_OUT_OF_INDEXES)
        ErrorLog("[TALK] P26 stage 0 fold 1: TlsAlloc failed - an off-thread startPlayerConversation logs ev=direct");
    if (kDialogueStartConvRva == 0) g_startConvHook = -2;
    else g_startConvHook = (coop::AddHook((void*)(g_base + kDialogueStartConvRva), (void*)&detour_startConv,
                                          (void**)&orig_startConv) == coop::SUCCESS) ? 1 : -1;
    if (kDialogueStartPlayerConvRva == 0) g_startPlayerConvHook = -2;
    else g_startPlayerConvHook = (coop::AddHook((void*)(g_base + kDialogueStartPlayerConvRva), (void*)&detour_startPlayerConv,
                                                (void**)&orig_startPlayerConv) == coop::SUCCESS) ? 1 : -1;
    char b[320];
    std::sprintf(b, "[TALK] P26 stage 0 hooks (log only - the original always runs): startConversation 0x683500=%s"
                 " startPlayerConversation 0x683890=%s", TalkHookState(g_startConvHook), TalkHookState(g_startPlayerConvHook));
    if (g_startConvHook == 1 && g_startPlayerConvHook == 1) DebugLog(std::string(b)); else ErrorLog(std::string(b));
    /* P26 stages 1-3 */
    if (kDialogueSetInDialogRva == 0) g_setInDialogHook = -2;
    else g_setInDialogHook = (coop::AddHook((void*)(g_base + kDialogueSetInDialogRva), (void*)&detour_setInDialog,
                                            (void**)&orig_setInDialog) == coop::SUCCESS) ? 1 : -1;
    if (kTkEndDialogueRva == 0) g_endDialogueHook = -2;
    else g_endDialogueHook = (coop::AddHook((void*)(g_base + kTkEndDialogueRva), (void*)&detour_endDialogue,
                                            (void**)&orig_endDialogue) == coop::SUCCESS) ? 1 : -1;
    if (kDialogueReplyClickedRva == 0) g_replyClickedHook = -2;
    else g_replyClickedHook = (coop::AddHook((void*)(g_base + kDialogueReplyClickedRva), (void*)&detour_replyClicked,
                                             (void**)&orig_replyClicked) == coop::SUCCESS) ? 1 : -1;
    /* P26s1 fold 1 H2 */
    if (kDlgSetConvReplyGuiRva == 0) g_convReplyGuiHook = -2;
    else g_convReplyGuiHook = (coop::AddHook((void*)(g_base + kDlgSetConvReplyGuiRva), (void*)&detour_setConvReplyGui,
                                             (void**)&orig_setConvReplyGui) == coop::SUCCESS) ? 1 : -1;
    if (kDlgSetResponsesGuiRva == 0) g_responsesGuiHook = -2;
    else g_responsesGuiHook = (coop::AddHook((void*)(g_base + kDlgSetResponsesGuiRva), (void*)&detour_setResponsesGui,
                                             (void**)&orig_setResponsesGui) == coop::SUCCESS) ? 1 : -1;
    g_tkOn = (g_startPlayerConvHook == 1 && g_setInDialogHook == 1 && g_endDialogueHook == 1
              && g_convReplyGuiHook == 1 && g_responsesGuiHook == 1) ? 1 : 0;
    {
        const std::string f1 = std::string("[TALK] P26s1 fold 1 hooks: setConversationReplyGUI 0x6736E0=") + TalkHookState(g_convReplyGuiHook)
            + " setResponesGUI 0x6735E0=" + TalkHookState(g_responsesGuiHook) + " dialogue window 0x2132770="
            + (kDialogueWindowRva != 0 ? "row" : "noAddress") + " (forwarding needs both hooks; a PROMPT is shown only with the window row"
            + " and hire.cpp's _doActions hook)";
        if (g_convReplyGuiHook == 1 && g_responsesGuiHook == 1 && kDialogueWindowRva != 0) DebugLog(f1); else ErrorLog(f1);
    }
    std::sprintf(b, "[TALK] P26 stages 1-3 hooks: setInDialog 0x673C10=%s endDialogue 0x673DA0=%s replyClicked 0x683360=%s"
                 " getData 0x6AE090=%s - forwarding %s", TalkHookState(g_setInDialogHook), TalkHookState(g_endDialogueHook),
                 TalkHookState(g_replyClickedHook), kDialogDataGetDataRva != 0 ? "row" : "noAddress",
                 g_tkOn ? "ON" : "OFF (an NPC's conversation with the other player's character opens THIS game's window, as before)");
    if (g_tkOn && g_replyClickedHook == 1 && kDialogDataGetDataRva != 0) DebugLog(std::string(b)); else ErrorLog(std::string(b));
    {   /* P26 stage 4 */
        const std::string s4 = std::string("[TALK] P26 stage 4: A applies an ANSWER through replyClicked 0x683360 (") + TalkHookState(g_replyClickedHook)
            + "), next-line PROMPTs, no answer deadline (decision 229; a held end waits 30 s for its ACT_RESULT); B shows a next line through sayLine 0x682F10 (" + (kDialogueSayLineRva != 0 ? "row" : "noAddress")
            + "); target-side actions (money 8/9/10, knockout 57, hire 3/18) of a forwarded conversation are deferred to stage 5";
        if (g_replyClickedHook == 1 && kDialogueSayLineRva != 0) DebugLog(s4); else ErrorLog(s4);
    }
    /* P26s6: S6-1 / S6-2 hooks; S6-3 rides hire.cpp's _doActions hook */
    if (kS6CheckCondRva == 0) g_s6CondHook = -2;
    else g_s6CondHook = (coop::AddHook((void*)(g_base + kS6CheckCondRva), (void*)&detour_s6CheckCond,
                                       (void**)&orig_s6CheckCond) == coop::SUCCESS) ? 1 : -1;
    if (kS6GetSpeakerRva == 0) g_s6SpkHook = -2;
    else g_s6SpkHook = (coop::AddHook((void*)(g_base + kS6GetSpeakerRva), (void*)&detour_s6GetSpeaker,
                                      (void**)&orig_s6GetSpeaker) == coop::SUCCESS) ? 1 : -1;
    g_s6On = (g_tkOn && g_s6CondHook == 1 && g_s6SpkHook == 1) ? 1 : 0;
    {
        const int rows = (kS6SquadLeaderRva != 0 && kS6GetPlatoonRva != 0 && kS6AddOrderRva != 0 && kS6AddOrderArgRva != 0) ? 1 : 0;
        const std::string s6 = std::string("[TALK] P26s6 hooks: _checkCondition 0x675EC0=") + TalkHookState(g_s6CondHook)
            + " getSpeaker 0x672160=" + TalkHookState(g_s6SpkHook) + " TALK_TO_LEADER order rows=" + (rows ? "row" : "noAddress")
            + " - the other player's character counts as a player for DC_IS_PLAYER / T_TARGET_IF_PLAYER / TALK_TO_LEADER toward this"
            + " game's NPCs: " + (g_s6On ? "ON" : "OFF");
        if (g_s6On && rows) DebugLog(s6); else ErrorLog(s6);
    }
    /* P25: the view-point hook (A's camera gate bypass for a REQUEST's own start) */
    if (kP25ViewPointRva == 0) g_p25ViewHook = -2;
    else g_p25ViewHook = (coop::AddHook((void*)(g_base + kP25ViewPointRva), (void*)&detour_p25ViewPoint,
                                        (void**)&orig_p25ViewPoint) == coop::SUCCESS) ? 1 : -1;
    g_p25On = g_tkOn;
    {
        const int rows = (kP25ViewRetRva != 0 && kP25GateDistRva != 0 && kP25PlayerIfaceRva != 0 && kP25TradeInstRva != 0
                          && kP25TradeOpenRva != 0 && kP25CharEditorRva != 0) ? 1 : 0;
        const std::string p25 = std::string("[TALK] P25: view point 0x7F1DC0=") + TalkHookState(g_p25ViewHook) + " rows="
            + (rows ? "row" : "noAddress") + " - a click on an NPC the other game owns becomes a REQUEST and runs on that game;"
            + " a REQUEST for this game's NPC starts it here; the camera gate of a marked start (this game's NPC toward the other"
            + " player's character: P26 walk-over arrivals and P25 requests) is answered as the NPC: "
            + (g_p25On ? "ON" : "OFF (the P26 road is off)");
        if (g_p25On && g_p25ViewHook == 1 && rows) DebugLog(p25); else ErrorLog(p25);
    }
}

}   /* anonymous namespace */

void InstallSpeech()
{
    g_base = (uintptr_t)::GetModuleHandleA(0);
    InstallTalkStarters();   /* P26 stage 0: log-only starters, independent of the say / sendEvent hooks below */
    if (kDialogueSayRva == 0)
    {
        ErrorLog("[SAY] the address table gave Dialogue::say no address - NPC speech bubbles are NOT carried between"
                 " the games, and a copy keeps saying its own lines");
        return;
    }
    coop::HookStatus h = coop::AddHook((void*)(g_base + kDialogueSayRva), (void*)&detour_say, (void**)&orig_say);
    if (h != coop::SUCCESS)
    {
        orig_say = 0;
        g_sayHook = -1;
        ErrorLog("[SAY] AddHook Dialogue::say 0x67F2F0 FAILED - NPC speech bubbles are NOT sent from the game driving"
                 " a character, and a copy keeps saying its own lines");
        return;
    }
    g_sayHook = 1;
    DebugLog("[SAY] hook installed: Dialogue::say 0x67F2F0 (owned speakers send MSG_SAY, copies show only what their owner said)");
    if (kDialogueSendEventRva == 0)
    {
        ErrorLog("[SAY] the address table gave Dialogue::sendEvent no address - a copy can still start its own chat"
                 " (its lines are dropped, and its currentLine cleared)");
        return;
    }
    h = coop::AddHook((void*)(g_base + kDialogueSendEventRva), (void*)&detour_sendEvent, (void**)&orig_sendEvent);
    if (h != coop::SUCCESS)
    {
        orig_sendEvent = 0;
        g_sendEventHook = -1;
        ErrorLog("[SAY] AddHook Dialogue::sendEvent 0x683F00 FAILED - a copy can still start its own chat (its lines"
                 " are dropped, and its currentLine cleared)");
        return;
    }
    g_sendEventHook = 1;
    DebugLog("[SAY] hook installed: Dialogue::sendEvent 0x683F00 (a copy starts no chat of its own; the player talking to it still does)");
}

void ApplyRemoteSay(unsigned int uid, const std::string& text, unsigned int fromPeer)
{
    ::Character* c = FindSpawned(uid);
    if (c == 0 || !net::UidOwnedByPeer(uid, fromPeer)) { ::InterlockedIncrement64(&g_sayUnknownUid); return; }
    void* dlg = SayReadPtr(c, kCharDialogue);
    if (dlg == 0 || SayPlausiblePtr(dlg) == 0 || SayReadPtr(dlg, kDlgMe) != (void*)c)
    {
        ::InterlockedIncrement64(&g_sayNoDialogue);
        return;
    }
    SayFn fn = orig_say;
    /* No detour (no address or AddHook failed): the engine's own entry IS the original. */
    if (fn == 0 && g_sayHook != 1 && kDialogueSayRva != 0 && g_base != 0) fn = (SayFn)(g_base + kDialogueSayRva);
    if (fn == 0) { ::InterlockedIncrement64(&g_sayNoDialogue); return; }
    ::InterlockedExchange(&g_sayApplying, 1);
    fn(dlg, text, 0);
    ::InterlockedExchange(&g_sayApplying, 0);
    SayMarkIfParked(dlg, text);   /* P3-b: if it parked, its replay is ours */
    ::InterlockedIncrement64(&g_sayApplied);
    SayLog("<-", uid, text);
}

/* recruit1: the refused hirer's copy says the refusal line - ApplyRemoteSay's call without its ownership gate. */
bool SayOnCopy(::Character* c, const std::string& text)
{
    if (c == 0) return false;
    void* dlg = SayReadPtr(c, kCharDialogue);
    if (dlg == 0 || SayPlausiblePtr(dlg) == 0 || SayReadPtr(dlg, kDlgMe) != (void*)c) return false;
    SayFn fn = orig_say;
    if (fn == 0 && g_sayHook != 1 && kDialogueSayRva != 0 && g_base != 0) fn = (SayFn)(g_base + kDialogueSayRva);
    if (fn == 0) return false;
    ::InterlockedExchange(&g_sayApplying, 1);
    fn(dlg, text, 0);
    ::InterlockedExchange(&g_sayApplying, 0);
    SayMarkIfParked(dlg, text);
    return true;
}

void SayNoteMalformed() { ::InterlockedIncrement64(&g_sayMalformed); }
void SayNoteDroppedBlocked() { ::InterlockedIncrement64(&g_sayDroppedBlocked); }

// crimetest: the verb (MAIN THREAD) arms one request; the K2 safe point drains it.
/* crime7: an optional victim-faction filter (a faction stringID). T281's victims were all of a faction whose guards assign no
   bounty (51646-Dialogue.mod), so a run could not repeat T280's bounty; `crimetest steal defaultEmpireFactionSID` picks the
   nearest victim of that faction. MAIN THREAD only (armed here, read at the safe point, which is also the main thread). */
static std::string g_crimeTestFactionSid;

std::string CrimeTestArm(const std::string& arg)
{
    /* crime3: `stealnear` also takes a victim this game drives - on the townspeople's owner it is the positive control */
    std::string kind = arg, sid;
    const size_t sp = arg.find(' ');
    if (sp != std::string::npos)
    {
        kind = arg.substr(0, sp);
        sid = arg.substr(sp + 1);
        while (!sid.empty() && sid[0] == ' ') sid.erase(0, 1);
    }
    /* pvp1: `stealplayer` - the victim is the nearest character of the OTHER PLAYER (coop-peer); the setCrime hook should
       refuse it (crime stays 0, `[CRIME] REPORT ... pvp[refused]` +1). */
    /* pvp1 (review-pvp1 1): `attackplayer` - this game's first own player character attacks the nearest character of the
       other player (TASK_MELEE_FOCUSED through AttackLocal), so a run can show that being attacked still brings the
       victim's and its squad's own combat response while no crime is recorded. */
    /* rel3: `attacknear <faction sid>` - the same attacker and the same TASK_MELEE_FOCUSED as attackplayer, against the
       nearest living, conscious, non-player, non-peer character of that faction (a townsperson), so a run can see whether an
       assault moves a faction standing and whose. The faction is required. */
    /* arrest3: `sentence <hours>` - TEST-ONLY: shorten the sentence of the copies this game's guard jailed (spawn.cpp) */
    if (kind == "sentence")
    {
        const float h = (float)std::atof(sid.c_str());
        if (!(h >= 0.01f && h <= 100.0f)) return "error crimetest: usage crimetest sentence <hours 0.01-100>";
        const int n = PrisonTestSentence(h);
        char nb[96];
        std::sprintf(nb, "ok crimetest sentence %.2f h on %d copies", h, n);
        DebugLog(std::string("[CRIME] ") + nb);
        return nb;
    }
    /* par20: `bountyclear <uid>` - TEST-ONLY: erase every non-player bounty on character <uid> as this game sees it (crime.cpp) */
    if (kind == "bountyclear")
    {
        const unsigned long v = std::strtoul(sid.c_str(), 0, 10);
        if (v == 0 || v > 0xFFFFFFFFUL) return "error crimetest: usage crimetest bountyclear <uid>";
        return BountyTestClearArm((unsigned int)v);
    }
    /* `bountyset <personName> <lawNpcName> <amount>` - TEST-ONLY (the carried hand-in fixture): on the person's OWNER, a bounty of
       <amount> on that person under the key the law NPC's faction reads, so a carried hand-in has a bounty to pay (crime.cpp) */
    if (kind == "bountyset")
    {
        std::string pk, lk;
        int amount = 0;
        if (!coopsay::BountySetParse(sid, &pk, &lk, &amount))
            return "error crimetest: usage crimetest bountyset <personName> <lawNpcName> <amount 1..1000000> ('_' = a space in a name)";
        return BountyTestSetArm(pk, lk, amount);
    }
    if (kind != "steal" && kind != "stealnear" && kind != "stealplayer" && kind != "attackplayer" && kind != "attacknear")
        return "error crimetest: usage crimetest steal|stealnear|stealplayer|attackplayer|attacknear|sentence [<victim faction stringID> | <hours>]";
    if (kind == "attacknear" && sid.empty()) return "error crimetest: usage crimetest attacknear <victim faction stringID>";
    g_crimeTestFactionSid = sid;
    ::InterlockedExchange(&g_crimeTestPending, kind == "stealnear" ? 2 : (kind == "stealplayer" ? 3 : (kind == "attackplayer" ? 4 : (kind == "attacknear" ? 5 : 1))));
    ++g_crimeTestArmed;
    DebugLog("[CRIME] crimetest " + kind + (sid.empty() ? std::string() : " (victim faction " + sid + ")")
             + " ARMED - applied at the next safe point (threadSafeRagdollUpdates)");
    return "ok crimetest armed";
}

/* rel3: one skip line - attacknear's reads "[CRIME] crimetest attacknear NOT ORDERED: <why>", the other kinds' are unchanged */
static void CrimeTestSkip(LONG kind, const std::string& why)
{
    DebugLog(std::string(kind == 5 ? "[CRIME] crimetest attacknear NOT ORDERED: " : "[CRIME] crimetest skipped: ") + why);
}

// MAIN THREAD, worker paused (called from combat.cpp detour_tsRagdollUpdates, before the engine's own drain).
void CrimeTestDrain()
{
    const LONG kind = ::InterlockedExchange(&g_crimeTestPending, 0);
    if (kind == 0) return;
    if (EngineWritesBlocked()) { ++g_crimeTestBlocked; CrimeTestSkip(kind, "engine writes are blocked (world loading or tearing down)"); return; }
    if (kNotifyCrimeWitnessedRva == 0) { ++g_crimeTestNoAddr; CrimeTestSkip(kind, "NotifyCrimeWitnessed is not in the address table"); return; }
    ::Character* offender = 0; unsigned int ouid = 0; Ogre::Vector3 opos;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap && offender == 0; ++i)
    {
        unsigned int u = 0; ::Character* c = 0; ::Faction* f = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || !net::IsUidMine(u)) continue;
        if (!CrimeFactionOf(c, &f) || f == 0 || !IsPlayerFaction(f)) continue;
        if (!SafeReadPosition(c, &opos)) continue;
        offender = c; ouid = u;
    }
    if (offender == 0) { ++g_crimeTestNoOffender; CrimeTestSkip(kind, "no own player-faction character with a readable position"); return; }
    const float maxDist = g_crimeTestFactionSid.empty() ? kCrimeVictimMaxDist : kCrimeVictimMaxDistFiltered;
    ::Character* victim = 0; unsigned int vuid = 0; ::Faction* vf = 0; float best = maxDist;
    long long nSeen = 0, nPlayer = 0, nWrongFac = 0, nDead = 0, nDeadFault = 0, nDowned = 0, nUnread = 0, nFar = 0, nNoUid = 0;
    int walkRan = 1;
    if (kind == 5)
    {
        /* rel3 (T317): EVERY character the engine is updating - own, copies and unregistered alike - the list `census`
           and `roster full` walk (GameWorld+0x750). T315: the mirror walk found no target on either game. Each filter
           counts what it rejected, so the NOT ORDERED line names the filter. The other kinds keep the mirror walk below. */
        if (coop::GameWorldPtr() == 0) walkRan = 0;
        else
        {
            const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
            if (all.size() > 20000) walkRan = 0;
            else for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
            {
                ::Character* c = *it; ::Faction* f = 0; Ogre::Vector3 q;
                ++nSeen;
                if (SayPlausiblePtr(c) == 0 || !CrimeFactionOf(c, &f) || f == 0) { ++nUnread; continue; }
                if (IsPlayerFaction(f) || IsPeerFaction(f)) { ++nPlayer; continue; }
                if (RelationsWireSid(f) != g_crimeTestFactionSid) { ++nWrongFac; continue; }
                const int dead = CrimeIsDead(c);
                if (dead < 0) { ++nDeadFault; continue; }
                if (dead > 0) { ++nDead; continue; }
                if (IsDownedCharacter(c)) { ++nDowned; continue; }
                if (!SafeReadPosition(c, &q)) { ++nUnread; continue; }
                const float dx = q.x - opos.x, dz = q.z - opos.z;
                const float d = sqrtf(dx * dx + dz * dz);
                if (d > maxDist) { ++nFar; continue; }
                const unsigned int u = FindSpawnedUid(c);
                if (u == 0) { ++nNoUid; continue; }   /* AttackLocal orders by uid */
                if (d < best) { best = d; victim = c; vuid = u; vf = f; }
            }
        }
    }
    else for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0; ::Faction* f = 0; Ogre::Vector3 q;
        if (!MirrorSlot(i, &u, &c) || u == 0 || ((kind == 1 || kind == 3 || kind == 4) && net::IsUidMine(u))) continue;
        if (!CrimeFactionOf(c, &f) || f == 0) continue;
        if (kind == 3 || kind == 4) { if (!IsPeerFaction(f)) continue; }         /* pvp1: only the other player's characters */
        else if (IsPlayerFaction(f) || IsPeerFaction(f)) continue;
        if (!g_crimeTestFactionSid.empty() && RelationsWireSid(f) != g_crimeTestFactionSid) continue;   /* crime7 */
        if (!SafeReadPosition(c, &q)) continue;
        const float dx = q.x - opos.x, dz = q.z - opos.z;
        const float d = sqrtf(dx * dx + dz * dz);
        if (d < best) { best = d; victim = c; vuid = u; vf = f; }
    }
    if (victim == 0 && kind == 5)
    {
        /* rel3 (T317): which filter rejected them */
        ++g_crimeTestNoVictim;
        char nb[480];
        std::sprintf(nb, "no target of faction %.60s within %.0f u (seen=%lld wrongFaction=%lld player=%lld dead=%lld downed=%lld"
                     " unreadable=%lld deadUnreadable=%lld far=%lld noUid=%lld) - walk: the engine's character update list%s",
                     g_crimeTestFactionSid.c_str(), maxDist, nSeen, nWrongFac, nPlayer, nDead, nDowned, nUnread, nDeadFault, nFar, nNoUid,
                     walkRan ? "" : " DID NOT RUN (no GameWorld or an implausible list size)");
        CrimeTestSkip(kind, std::string(nb));
        return;
    }
    if (victim == 0)
    {
        ++g_crimeTestNoVictim;
        char nb[200];
        std::sprintf(nb, "no %s character%s%.60s within %.0f u of the offender",
                     kind == 1 ? "copy of a non-player" : ((kind == 3 || kind == 4) ? "copy of the other player's" : (kind == 5 ? "living, conscious non-player" : "non-player")),
                     g_crimeTestFactionSid.empty() ? "" : " of faction ", g_crimeTestFactionSid.c_str(), maxDist);
        CrimeTestSkip(kind, std::string(nb));
        return;
    }
    if (kind == 4 || kind == 5)
    {
        /* pvp1: no crime call - the attack itself; AttackLocal logs its own order (and, refused, its reason as an [M3] line) */
        const bool ok4 = AttackLocal(ouid, vuid, 5);
        char ab[320];
        if (kind == 4)
            std::sprintf(ab, "[CRIME] crimetest attackplayer %s: attacker uid=%u victim uid=%u (the other player's) dist=%.1f (TASK_MELEE_FOCUSED)",
                         ok4 ? "ORDERED" : "FAILED", ouid, vuid, best);
        else   /* rel3 */
            std::sprintf(ab, "[CRIME] crimetest attacknear %s: attacker uid=%u victim uid=%u (%s) victim faction %.60s dist=%.1f (TASK_MELEE_FOCUSED)%s",
                         ok4 ? "ORDERED" : "NOT ORDERED", ouid, vuid, net::IsUidMine(vuid) ? "mine" : "copy", RelationsWireSid(vf).c_str(), best,
                         ok4 ? "" : " - AttackLocal refused the order (its [M3] line says why)");
        DebugLog(std::string(ab));
        if (ok4) ++g_crimeTestDone; else ++g_crimeTestFaulted;
        return;
    }
    unsigned int w[7];
    if (!CrimeReadHandFields(victim, w)) { ++g_crimeTestFaulted; DebugLog("[CRIME] crimetest skipped: the victim's hand could not be read"); return; }
    const hand h(w[5] /*index*/, w[6] /*serial*/, (itemType)w[2] /*type*/, w[3] /*container*/, w[4] /*containerStamp*/);
    int before = -1, after = -1; float expBefore = -1.0f, expAfter = -1.0f;
    CrimeReadBack(offender, &before, &expBefore);
    const NotifyCrimeFn fn = (NotifyCrimeFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kNotifyCrimeWitnessedRva);
    const int ok = CrimeCall(fn, offender, vf, &h);
    CrimeReadBack(offender, &after, &expAfter);
    if (ok) ++g_crimeTestDone; else ++g_crimeTestFaulted;
    /* review-crime2 item 4: the engine refuses silently (an unconscious or animal offender, 851f40:10-13; a victim faction equal
       to the offender's own or its squad's, 851db0:12-15) - so "called" is not "committed". Effective = the read-back shows
       STEALING with an expiry at least 19 (it was just set to 20). */
    const bool effective = ok && after == kCrimeStealing && expAfter >= (float)(kCrimeExpirySec - 1);
    if (effective) ++g_crimeTestEffective;
    char b[400];
    std::sprintf(b, "[CRIME] crimetest %s %s effective=%d: offender uid=%u victim uid=%u (%s) dist=%.1f victimFaction=%p hand[type=%u index=%u serial=%u]"
                 " crime %d->%d expiry %.1f->%.1f (notifyCrimeWitnessed 0x851F40, expiry %d, STEALING)",
                 kind == 2 ? "stealnear" : (kind == 3 ? "stealplayer" : "steal"), ok ? "CALLED" : "FAULTED", effective ? 1 : 0, ouid, vuid, net::IsUidMine(vuid) ? "mine" : "copy",
                 best, (void*)vf, w[2], w[5], w[6], before, after, expBefore, expAfter, kCrimeExpirySec);
    DebugLog(std::string(b));
}

std::string CrimeTestCounts()
{
    char b[200];
    std::sprintf(b, "crimetest[armed,done,effective,noOffender,noVictim,noAddr,faulted,blocked]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                 g_crimeTestArmed, g_crimeTestDone, g_crimeTestEffective, g_crimeTestNoOffender, g_crimeTestNoVictim, g_crimeTestNoAddr,
                 g_crimeTestFaulted, g_crimeTestBlocked);
    return std::string(b);
}

// crime11: the lever's own REPORT line (P077 removed - H-crime-owner answered, F894/F896).
void ReportCrimeTest()
{
    DebugLog("[CRIMETEST] REPORT " + CrimeTestCounts());
}

// ===========================================================================================
// TALKTEST LEVER (P26 stage 0) - a TEST-ONLY dev verb (class b), not player behaviour.
// `talktest [npcUid|near] [targetUid|near] [event]` arms one request; the K2 safe point (combat.cpp detour_tsRagdollUpdates,
// worker paused) drains it: the engine's own event entry Dialogue::sendEvent 0x683F00 (through its hook, so the starters log
// what it starts) is called on the NPC's Dialogue (Character +0x280) with the target as `who` and the event (default 1
// EV_PLAYER_TALK_TO_ME - the only event that can open the reply window; the file's Q1 step 3). near target = the nearest
// character of the other player (a copy here, coop-peer faction) within 3000 u of this game's first own player character;
// near NPC = the nearest living, conscious, non-player, non-peer character with a Dialogue within 3000 u of the target
// (the engine's character update list, as crimetest attacknear). A uid names either one directly (a positive control: the
// target uid of this game's own character). It logs its inputs, sendEvent's result and the starts during the call. Nothing
// else: no order, no message, no write.
// ===========================================================================================
static volatile LONG g_talkTestPending = 0;
static unsigned int g_talkTestNpcUid = 0, g_talkTestTargetUid = 0;
static int g_talkTestEvent = 1;
static int g_talkTestNearCopy = 0;   /* P25: `talktest nearcopy ...` - the near NPC must be a copy the other game drives */
static long long g_talkTestArmed = 0, g_talkTestRan = 0, g_talkTestSkipped = 0, g_talkTestFaulted = 0;

std::string TalkTestArm(const std::string& arg)
{
    unsigned int n = 0, t = 0; int ev = 1, nc = 0;
    if (!coopsay::TalkTestParse(arg, &n, &t, &ev, &nc))   /* P25: nearcopy */
        return "error talktest: usage talktest [npcUid|near|nearcopy] [targetUid|near] [event 0..255, default 1]";
    g_talkTestNpcUid = n; g_talkTestTargetUid = t; g_talkTestEvent = ev; g_talkTestNearCopy = nc;
    ::InterlockedExchange(&g_talkTestPending, 1);
    ++g_talkTestArmed;
    char b[200];
    std::sprintf(b, "[TALK] talktest npc=%u target=%u (0 = near) ev=0x%X nearcopy=%d ARMED - runs at the next safe point", n, t, (unsigned int)ev, nc);   /* P25 */
    DebugLog(std::string(b));
    return "ok talktest armed";
}

static void TalkTestSkip(const std::string& why)
{
    ++g_talkTestSkipped;
    DebugLog("[TALK] talktest skipped: " + why);
}

/* The character's Dialogue when it reads as one (me == c), else 0. */
static void* TalkDialogueOf(::Character* c)
{
    if (c == 0 || SayPlausiblePtr(c) == 0) return 0;
    void* dlg = SayReadPtr(c, kCharDialogue);
    if (dlg == 0 || SayPlausiblePtr(dlg) == 0 || SayReadPtr(dlg, kDlgMe) != (void*)c) return 0;
    return dlg;
}

/* 1 called (*result = sendEvent's bool), 0 faulted. No C++ object here (C2712). */
static int TalkSendPod(SendEventFn fn, void* dlg, void* who, int ev, int* result)
{
    __try { *result = fn(dlg, who, ev) ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* P26 stage 0 fold 1: the drain's live-character checks. MAIN THREAD. No read of the pointer itself. */
static TalkOffItem g_talkOffBatch[kTalkOffRing];     /* one drain's entries */
static void* g_talkOffUnknown[2 * kTalkOffRing];     /* pointers the registry does not know, sorted */
static char g_talkOffFound[2 * kTalkOffRing];        /* ... 1 = a member of GameWorld's character update list */

/* 1 = the uid registry knows p and still maps that uid back to p (address compares only). */
static int TalkOffInRegistry(void* p)
{
    const unsigned int u = FindSpawnedUid(p);
    return (u != 0 && FindSpawned(u) == (::Character*)p) ? 1 : 0;
}

/* Marks which of the sorted u[0..n) are in GameWorld's character update list. 0 = the list could not be walked. */
static int TalkOffMarkUpdateList(void* const* u, int n)
{
    for (int i = 0; i < n; ++i) g_talkOffFound[i] = 0;
    if (n == 0) return 1;
    if (coop::GameWorldPtr() == 0) return 0;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return 0;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        void* const c = (void*)*it;
        void* const* hit = std::lower_bound(u, u + n, c);
        if (hit != u + n && *hit == c) g_talkOffFound[hit - u] = 1;
    }
    return 1;
}

/* 1 = p is a live character at this drain: the registry round trip, or a member of the update list walked this drain. */
static int TalkOffLive(void* p, int blocked, int walked, int nu)
{
    if (p == 0 || blocked) return 0;
    if (TalkOffInRegistry(p)) return 1;
    if (!walked) return 0;
    void* const* hit = std::lower_bound((void* const*)g_talkOffUnknown, (void* const*)g_talkOffUnknown + nu, p);
    return (hit != (void* const*)g_talkOffUnknown + nu && *hit == p && g_talkOffFound[hit - (void* const*)g_talkOffUnknown]) ? 1 : 0;
}

static const char* TalkOffKindName(int k) { return k == kTalkOffKinds - 1 ? "stale" : coopsay::TalkTargetName(k); }

/* P26 stage 0 fold 1: drain the off-thread starts. MAIN THREAD, worker paused (the K2 safe point, via TalkTestDrain). A pointer is
   dereferenced (TalkKindOf / TalkName / TalkDialogueOf) only after TalkOffLive says it is a live character. */
static void TalkOffDrain()
{
    if (g_talkOffTid != 0 && g_talkOffTidLogged == 0)
    {
        g_talkOffTidLogged = 1;
        char tb[240];
        std::sprintf(tb, "[TALK] off-thread conversation starters run on thread %lu (%s); the main thread is %lu",
                     (unsigned long)g_talkOffTid, EngineThreadNameOf((unsigned long)g_talkOffTid), StoreMainThreadId());
        DebugLog(std::string(tb));
    }
    if (g_talkOffPending == 0) return;
    int n = 0;
    for (LONG i = 0; i < kTalkOffRing; ++i)
    {
        TalkOffEntry& e = g_talkOffRing[i];
        if (::InterlockedCompareExchange(&e.state, 2, 2) != 2) continue;
        TalkOffItem& b = g_talkOffBatch[n++];
        b.ev = e.ev; b.player = e.player; b.tid = e.tid; b.dlg = e.dlg; b.me = e.me; b.who = e.who; b.line = e.line;
        ::InterlockedExchange(&e.state, 0);
    }
    ::InterlockedExchangeAdd(&g_talkOffPending, -(LONG)n);
    if (n == 0) return;
    g_talkOffDrained += n;
    const int blocked = EngineWritesBlocked() ? 1 : 0;   /* loading / tearing down: nothing is read, every pointer is stale */
    int nu = 0;
    if (!blocked)
        for (int k = 0; k < n; ++k)
        {
            if (g_talkOffBatch[k].me != 0 && !TalkOffInRegistry(g_talkOffBatch[k].me)) g_talkOffUnknown[nu++] = g_talkOffBatch[k].me;
            if (g_talkOffBatch[k].who != 0 && !TalkOffInRegistry(g_talkOffBatch[k].who)) g_talkOffUnknown[nu++] = g_talkOffBatch[k].who;
        }
    std::sort(g_talkOffUnknown, g_talkOffUnknown + nu);
    const int walked = blocked ? 0 : TalkOffMarkUpdateList(g_talkOffUnknown, nu);
    for (int k = 0; k < n; ++k)
    {
        const TalkOffItem& b = g_talkOffBatch[k];
        const int stale = kTalkOffKinds - 1;
        int sk = coopsay::kTalkTargetNone, tk = coopsay::kTalkTargetNone;
        unsigned int suid = 0, tuid = 0;
        if (b.me != 0)
            sk = (TalkOffLive(b.me, blocked, walked, nu) && TalkDialogueOf((::Character*)b.me) == b.dlg)
                 ? TalkKindOf((::Character*)b.me, &suid) : stale;
        if (b.who != 0)
            tk = TalkOffLive(b.who, blocked, walked, nu) ? TalkKindOf((::Character*)b.who, &tuid) : stale;
        if (sk == stale || tk == stale) ++g_talkOffStale;
        ++g_talkOffBySpeaker[sk];
        ++g_talkOffByTarget[tk];
        if (b.player) TkFromRing(b.dlg, b.who, b.ev, (b.me != 0 && sk != stale && tk != stale) ? 1 : 0);   /* P26 stages 1-3 */
        if (g_talkOffStartLogged >= kTalkOffStartLogLimit) continue;
        ++g_talkOffStartLogged;
        char sn[48], tn[48], evs[16], lb[640];
        if (sk == stale) std::strcpy(sn, "stale"); else TalkName((::Character*)b.me, sn);
        if (tk == stale) std::strcpy(tn, "stale"); else TalkName((::Character*)b.who, tn);
        if (b.ev < 0) std::strcpy(evs, "direct"); else std::sprintf(evs, "0x%X", (unsigned int)b.ev);
        std::sprintf(lb, "[TALK] %s STARTED offThread=1 tid=%lu ev=%s speaker uid=%u %s '%s' -> target uid=%u %s '%s' line=%p"
                     " replies=n/a host=%d", b.player ? "startPlayerConversation" : "startConversation", b.tid, evs,
                     suid, TalkOffKindName(sk), sn, tuid, TalkOffKindName(tk), tn, b.line, net::SessionIsHost() ? 1 : 0);
        DebugLog(std::string(lb));
    }
}

// ===========================================================================================
// P26 STAGES 1-3 - THE K2 SAFE POINT (MAIN THREAD, worker paused): A's not-forwarded / off-thread-ended / orphaned conversations,
// B's off-thread window end, every arrived MSG_TALK, and the talkprompt lever. Held while the engine's writes are blocked.
// ===========================================================================================
/* the engine calls the safe point makes, each fault-guarded. No C++ object here (C2712). */
static void* TkGetLinePod(DlgGetDataFn fn, void* gd)
{
    __try { return fn(gd); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int TkStartPod(StartPlayerConvFn fn, void* dlg, void* who, void* line, int* started)
{
    __try { *started = (fn(dlg, who, line) & 0xFF) != 0 ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static void TkRunEndLater()
{
    std::vector<TkLater> el;
    el.swap(g_tkEndLater);
    for (size_t k = 0; k < el.size(); ++k)
    {
        const int i = TkMarkFind(el[k].dlg);
        if (i < 0 || g_tkMarks[i].state != 1) { ++g_tkEndLaterSkipped; continue; }   /* P26s1 fold 1: released / ended already - not ours to end */
        ++g_tkEndedLater;
        TkEndOwn(el[k].dlg, el[k].me, 0, "not forwarded", 1);
    }
}

/* P26 stage 4: the engine calls of the apply, each fault-guarded. No C++ object here (C2712). */
static int TkSayLinePod(SayLineFn fn, void* dlg, void* line, int* said)
{
    __try { *said = (fn(dlg, line) & 0xFF) != 0 ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int TkApplyReplyPod(ReplyClickedFn fn, void* dlg, int index)
{
    __try { fn(dlg, index); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* P26 stage 4: B - the NPC's NEXT line (a PROMPT with the mirrored convId). The copy's Dialogue says it through the engine's own
   Dialogue::sayLine 0x682F10 - the call triggerNextLine 0x683C90 makes for a local player (Read decomp_683c90) - under the apply
   flag, so the line's _doActions is skipped (A ran it). Its GUI branch (+0x190 set by the start, event +0x188 = 1) writes the text
   and the reply list into this game's window (Read decomp_682f10: setConversationReplyGUI, listPlayerReplies -> setResponesGUI):
   the window updates in place and B may answer again. The copy is gone, the line does not resolve, or the engine ends it: ANSWER
   SHOW_FAILED and the window closes. MAIN THREAD, K2 safe point. */
static void TkApplyNextPrompt(const cooptalk::TalkMsg& m, unsigned int peer)
{
    ++g_tkNextRecv;
    const TkMirror mi = g_tkMirror;
    std::string why;
    void* line = 0;
    if (!TkMirrorLive(mi)) why = "the mirrored NPC copy is gone or its Dialogue moved";
    else if (kDialogueSayLineRva == 0 || kDialogDataGetDataRva == 0 || g_base == 0)
        why = "sayLine 0x682F10 / DialogDataManager::getData is not in this game's address table";
    else if (HireDoActionsHookState() != 1)
        why = "hire.cpp's Dialogue::_doActions hook is not installed - the NPC line's actions would run a second time here";
    else
    {
        ::GameData* gd = (coop::GameWorldPtr() != 0) ? coop::GameWorldPtr()->gamedata.getData(m.lineSid) : 0;
        if (gd == 0) why = "the line " + m.lineSid + " is not in this game's data";
        else
        {
            line = TkGetLinePod((DlgGetDataFn)(g_base + (uintptr_t)kDialogDataGetDataRva), gd);
            if (line == 0) why = "the line " + m.lineSid + " has no dialogue line object here";
        }
    }
    int called = 0, said = 0, replies = 0;
    void* cur = 0;
    if (why.empty())
    {
        g_tkApplyDlg = mi.dlg;
        g_tkApplyStart = 1;   /* TalkSkipDoActions: the line's _doActions ran on the NPC's game */
        called = TkSayLinePod((SayLineFn)(g_base + (uintptr_t)kDialogueSayLineRva), mi.dlg, line, &said);
        g_tkApplyStart = 0;
        cur = SayReadPtr(mi.dlg, kDlgCurLine);
        replies = TalkReplyCount(mi.dlg);
        const int ended = TkReadBytePod(mi.dlg, kDlgEnded);
        if (!called || !said || cur == 0 || replies <= 0 || ended != 0)
            why = "the engine did not say the line with replies (called=" + TkI(called) + " said=" + TkI(said) + " line="
                  + (cur != 0 ? "set" : "none") + " replies=" + TkI(replies) + " ended=" + TkI(ended) + ")";
        else   /* P26s4 fold 1 M3: shown = this game's window shows the mirrored Dialogue with the PROMPT's reply ids */
        {
            void* const win = TkWindowDlg();
            std::vector<std::string> nids;
            TkReadStrVec(mi.dlg, kDlgReplyBegin, kDlgReplyEnd, &nids, (int)cooptalk::kTalkMaxReplies, (int)cooptalk::kTalkMaxId);
            if (win != mi.dlg)
                why = std::string("this game's dialogue window does not show the mirrored conversation after the line (")
                      + (win == 0 ? "no conversation" : "another conversation") + ")";
            else if (nids != m.replyIds)
                why = "the window's reply ids are not the PROMPT's (" + TkI((long long)nids.size()) + " here, "
                      + TkI((long long)m.replyIds.size()) + " in the PROMPT" + TkIdsMismatch(nids, m.replyIds) + ")";
        }
        g_tkApplyDlg = 0;
    }
    if (!why.empty())
    {
        TkMirrorOff();   /* first: the end below is ours, not the player's CLOSED */
        int closed = 0;
        if (TkMirrorLive(mi) && TkDlgBusy(mi.dlg))
        {
            g_tkApplyDlg = mi.dlg;
            closed = TkEndPod(mi.dlg);
            g_tkApplyDlg = 0;
        }
        cooptalk::TalkMsg a;
        a.kind = cooptalk::kTalkAnswer; a.convId = m.convId; a.npcUid = m.npcUid; a.targetUid = m.targetUid;
        a.result = cooptalk::kTalkAnsShowFailed; a.index = -1;
        const int sent = net::SendTalk(a) ? 1 : 0;
        ++g_tkNextFailed;
        TkLogFail("[TALK] B next PROMPT conv=" + TkU(m.convId) + " from peer " + TkU(peer) + " line=" + m.lineSid + " NOT shown: " + why
                  + " - window closed (endDialogue=" + TkI(closed) + "), ANSWER SHOW_FAILED " + (sent ? "sent" : "FAILED (link down)"));
        return;
    }
    g_tkMirror.answered = 0;   /* B may answer this line */
    std::vector<std::string> ids;
    TkReadStrVec(mi.dlg, kDlgReplyBegin, kDlgReplyEnd, &ids, (int)cooptalk::kTalkMaxReplies, (int)cooptalk::kTalkMaxId);
    const int idsMatch = (ids == m.replyIds) ? 1 : 0;
    const int sameLine = (cur == line) ? 1 : 0;
    if (!idsMatch || !sameLine) ++g_tkMismatch;
    ++g_tkNextShown;
    TkLog("[TALK] B next PROMPT shown conv=" + TkU(m.convId) + " line=" + m.lineSid + " sameLine=" + TkI(sameLine) + " replies=" + TkI(replies)
          + " (prompt " + TkI((long long)m.replyIds.size()) + ") idsMatch=" + TkI(idsMatch) + " text='" + m.npcText.substr(0, 120)
          + "' - the window updated in place; "
          + (m.deadlineMs == 0 ? std::string("no answer deadline") : "answer due within " + TkU(m.deadlineMs / 1000) + " s"));   /* P26s5 fold 5 */
}

/* B: the PROMPT - show the NPC copy's line to this game's own character in this game's own window, or answer why not. */
static void TkApplyPrompt(const cooptalk::TalkMsg& m, unsigned int peer)
{
    ++g_tkPromptRecv;
    int reqMatched = 0;   /* P25 fold 1 L1 [p25f1-L1a] */
    if (m.reqId != 0 && g_p25Pend.active && g_p25Pend.reqId == m.reqId && cooplive::SamePlayer(g_p25Pend.peer, peer, coop::LinkPeerSlot()))   /* P25: the NPC's game started it (fold re-check: the owner record is the slot key once PEER_SLOT is known, the PROMPT's sender the raw link id - compared as one player) */
    {
        reqMatched = 1;
        g_p25Pend.active = 0;
        ++g_p25Prompted;
        TkLog("[TALK] P25 req=" + TkU(m.reqId) + " answered: the NPC's game started conv=" + TkU(m.convId) + " npc uid=" + TkU(m.npcUid)
              + " line=" + m.lineSid + " - shown here as its mirror");
    }
    /* P25 fold 1 L1 [p25f1-L1b]: a PROMPT answering a REQUEST that no longer waits here (given up after 30 s, or another one waits) is
       never opened - ANSWER CLOSED ends the NPC's game's side of it. The mirror already showing that conversation is left as before. */
    if (m.reqId != 0 && !reqMatched && !(g_tkMirror.active && cooplive::SamePlayer(g_tkMirror.peer, peer, coop::LinkPeerSlot()) && g_tkMirror.convId == m.convId))
    {
        cooptalk::TalkMsg c;
        c.kind = cooptalk::kTalkAnswer; c.convId = m.convId; c.npcUid = m.npcUid; c.targetUid = m.targetUid;
        c.result = cooptalk::kTalkAnsClosed; c.index = -1;
        const int csent = net::SendTalk(c) ? 1 : 0;
        if (csent) ++g_tkClosedSent;
        ++g_p25StalePrompt;
        TkLogFail("[TALK] P25 PROMPT req=" + TkU(m.reqId) + " conv=" + TkU(m.convId) + " from peer " + TkU(peer) + " line=" + m.lineSid
                  + " answers no request waiting here (given up after 30 s, or another one waits) - NOT opened: ANSWER CLOSED "
                  + (csent ? "sent" : "FAILED (link down)"));
        return;
    }
    if (m.convId != 0 && g_tkClosedConv == m.convId && cooplive::SamePlayer(g_tkClosedPeer, peer, coop::LinkPeerSlot())
        && !(g_tkMirror.active && cooplive::SamePlayer(g_tkMirror.peer, peer, coop::LinkPeerSlot()) && g_tkMirror.convId == m.convId))   /* P26s4 fold 1 M1: never re-opened */
    {
        cooptalk::TalkMsg c;
        c.kind = cooptalk::kTalkAnswer; c.convId = m.convId; c.npcUid = m.npcUid; c.targetUid = m.targetUid;
        c.result = cooptalk::kTalkAnsClosed; c.index = -1;
        const int csent = net::SendTalk(c) ? 1 : 0;
        if (csent) ++g_tkClosedSent;
        TkLogFail("[TALK] B PROMPT conv=" + TkU(m.convId) + " from peer " + TkU(peer) + " line=" + m.lineSid
                  + " names a window this game's player closed - NOT re-opened: ANSWER CLOSED " + (csent ? "sent" : "FAILED (link down)"));
        return;
    }
    int res = cooptalk::kTalkAnsShowFailed;
    std::string why;
    if (g_tkMirror.active)
    {
        if (cooplive::SamePlayer(g_tkMirror.peer, peer, coop::LinkPeerSlot()) && g_tkMirror.convId == m.convId)
        {
            TkApplyNextPrompt(m, peer);   /* P26 stage 4: the NPC's next line - the window updates in place */
            return;
        }
        res = cooptalk::kTalkAnsBusy; why = "another mirrored conversation is shown here";
    }
    ::Character* npc = FindSpawned(m.npcUid);
    ::Character* tgt = FindSpawned(m.targetUid);
    void* dlg = 0;
    void* line = 0;
    void* win = 0;
    void* tdlg = 0;
    if (why.empty())
    {
        if (npc == 0 || !net::UidOwnedByPeer(m.npcUid, peer)) { res = cooptalk::kTalkAnsNoChar; why = "no copy of that NPC from the sending game here"; }
        else if (tgt == 0 || !net::IsUidMine(m.targetUid)) { res = cooptalk::kTalkAnsNoChar; why = "the addressed character is not this game's"; }
        else if ((dlg = TkDialogueOf(npc)) == 0) { res = cooptalk::kTalkAnsNoChar; why = "the NPC copy's Dialogue does not read as one"; }
        /* P26s1 fold 1 H3: never over a conversation of this game's own */
        else if ((win = TkWindowDlg()) != 0 && win != (void*)1
                 && !(g_tkMirror.active && win == g_tkMirror.dlg && cooplive::SamePlayer(g_tkMirror.peer, peer, coop::LinkPeerSlot()) && g_tkMirror.convId == m.convId))   /* P26s1 fold 2: stage 4's next-line PROMPT */
        { res = cooptalk::kTalkAnsBusy; why = "this game's dialogue window already shows a conversation"; }
        else if (TkDlgBusy(dlg))
        {
            res = cooptalk::kTalkAnsBusy;
            why = "the NPC copy is already in a conversation here (currentLine " + std::string(SayReadPtr(dlg, kDlgCurLine) != 0 ? "set" : "none")
                  + ", event +0x188=" + TkI(TkReadIntPod(dlg, kDlgConvEvent)) + ")";
        }
        else if ((tdlg = TkDialogueOf(tgt)) != 0 && TkDlgBusy(tdlg))
        {
            res = cooptalk::kTalkAnsBusy;
            why = "the addressed character is already in a conversation here (currentLine " + std::string(SayReadPtr(tdlg, kDlgCurLine) != 0 ? "set" : "none")
                  + ", event +0x188=" + TkI(TkReadIntPod(tdlg, kDlgConvEvent)) + ")";
        }
    }
    if (why.empty() && (kDialogDataGetDataRva == 0 || kDialogueStartPlayerConvRva == 0 || g_replyClickedHook != 1 || g_base == 0))
        why = "DialogDataManager::getData / startPlayerConversation / the replyClicked hook is not available here";
    if (why.empty() && kDialogueWindowRva == 0)   /* P26s1 fold 1 H3 */
        why = "the dialogue window (0x2132770) is not in this game's address table - whether it shows a conversation cannot be told";
    if (why.empty() && HireDoActionsHookState() != 1)   /* P26s1 fold 1 M3 */
        why = "hire.cpp's Dialogue::_doActions hook is not installed - the NPC line's actions would run a second time here";
    if (why.empty())
    {
        ::GameData* gd = (coop::GameWorldPtr() != 0) ? coop::GameWorldPtr()->gamedata.getData(m.lineSid) : 0;
        if (gd == 0) why = "the line " + m.lineSid + " is not in this game's data";
        else
        {
            const DlgGetDataFn fn = (DlgGetDataFn)(g_base + (uintptr_t)kDialogDataGetDataRva);
            line = TkGetLinePod(fn, gd);
            if (line == 0) why = "the line " + m.lineSid + " has no dialogue line object here";
        }
    }
    int called = 0, started = 0, replies = 0;
    void* cur = 0;
    if (why.empty())
    {
        const StartPlayerConvFn fn = (orig_startPlayerConv != 0) ? orig_startPlayerConv
                                   : (StartPlayerConvFn)(g_base + (uintptr_t)kDialogueStartPlayerConvRva);
        g_tkApplyDlg = dlg;
        g_tkApplyStart = 1;
        called = TkStartPod(fn, dlg, tgt, line, &started);
        g_tkApplyStart = 0;
        cur = SayReadPtr(dlg, kDlgCurLine);
        replies = TalkReplyCount(dlg);
        if (!called || !started || cur == 0 || replies <= 0)
        {
            if (cur != 0) TkEndPod(dlg);
            why = "the engine did not open the window (called=" + TkI(called) + " started=" + TkI(started) + " line="
                  + (cur != 0 ? "set" : "none") + " replies=" + TkI(replies) + ")";
        }
        g_tkApplyDlg = 0;
    }
    if (!why.empty())
    {
        cooptalk::TalkMsg a;
        a.kind = cooptalk::kTalkAnswer; a.convId = m.convId; a.npcUid = m.npcUid; a.targetUid = m.targetUid; a.result = res; a.index = -1;
        const int sent = net::SendTalk(a) ? 1 : 0;
        if (res == cooptalk::kTalkAnsBusy) ++g_tkBusy; else if (res == cooptalk::kTalkAnsNoChar) ++g_tkNoChar; else ++g_tkShowFailed;
        TkLogFail("[TALK] B PROMPT conv=" + TkU(m.convId) + " npc uid=" + TkU(m.npcUid) + " -> my uid=" + TkU(m.targetUid) + " line="
              + m.lineSid + " NOT shown: " + why + " - ANSWER " + cooptalk::TalkAnswerName(res) + (sent ? " sent" : " FAILED (link down)"));
        return;
    }
    g_tkMirror.active = 1; g_tkMirror.answered = 0; g_tkMirror.peer = peer; g_tkMirror.convId = m.convId;
    g_tkMirror.npcUid = m.npcUid; g_tkMirror.targetUid = m.targetUid; g_tkMirror.dlg = dlg; g_tkMirror.me = npc;
    g_tkMirror.seen = (TkWindowDlg() == dlg) ? 1 : 0;   /* P26s1 fold 1 M5 */
    ::InterlockedExchangePointer((PVOID volatile*)&g_tkMirrorDlgAny, dlg);
    std::vector<std::string> ids;
    TkReadStrVec(dlg, kDlgReplyBegin, kDlgReplyEnd, &ids, (int)cooptalk::kTalkMaxReplies, (int)cooptalk::kTalkMaxId);
    const int idsMatch = (ids == m.replyIds) ? 1 : 0;
    const int sameLine = (cur == line) ? 1 : 0;
    if (!idsMatch || !sameLine) ++g_tkMismatch;
    ++g_tkShown;
    TkLog("[TALK] B PROMPT shown conv=" + TkU(m.convId) + " npc uid=" + TkU(m.npcUid) + " '" + TkNameStr(npc) + "' -> my uid="
          + TkU(m.targetUid) + " '" + TkNameStr(tgt) + "' line=" + m.lineSid + " sameLine=" + TkI(sameLine) + " replies=" + TkI(replies)
          + " (prompt " + TkI((long long)m.replyIds.size()) + ") idsMatch=" + TkI(idsMatch) + " - this game's own dialogue window is open"
          + (idsMatch ? std::string() : TkIdsMismatch(ids, m.replyIds)));
}

/* P26 stage 4: A, after replyClicked returned - the engine's next line goes out as the next PROMPT (same convId), or this side ends.
   An end inside the call already ran TkEndOwn from the endDialogue hook (END ANSWERED). MAIN THREAD. */
static void TkAfterApply(void* dlg, const TkConv& c, int called)
{
    std::map<void*, TkConv>::iterator it = g_tkConvs.find(dlg);
    const int gone = (it == g_tkConvs.end() || it->second.convId != c.convId) ? 1 : 0;
    if (!called)   /* P26s4 fold 1 L4: a fault is a fault, also after the conversation ended inside the call */
    {
        ++g_tkApplyFault;
        if (gone)
        {
            TkLogFail("[TALK] A conv=" + TkU(c.convId) + " seq=" + TkU(c.seq) + " replyClicked faulted (caught) after the conversation ended inside it (END already sent)");
            return;
        }
        TkEndOwn(dlg, c.me, cooptalk::kTalkEndEngine, "replyClicked faulted (caught)", 1);
        return;
    }
    if (gone)
    {
        ++g_tkApplied;
        TkLog("[TALK] A conv=" + TkU(c.convId) + " seq=" + TkU(c.seq) + " reply applied - the conversation ended inside it (END already sent: the endDialogue hook or a held line)");
        return;
    }
    ++g_tkApplied;
    {   /* P26s5 fold 2: the engine's end inside the reply is held for a held line's NPC-side part - no next PROMPT */
        unsigned int parts = 0;
        const int hh = TkHoldLast(dlg, c.convId, &parts);
        if (hh >= 0 && g_tkActHolds[(size_t)hh].endHeld)
        {
            TkLog("[TALK] A conv=" + TkU(c.convId) + " seq=" + TkU(c.seq) + " reply applied - the conversation's end is held until the answer to ACT seq="
                  + TkU(g_tkActHolds[(size_t)hh].seq) + " (no next PROMPT)");
            return;
        }
    }
    if (it->second.engineEnded)   /* P26s5 fold 3: the engine ended it inside the reply; its closing line (if any) went through the router */
    {
        const char* whyA = "the engine ended it while applying the answer (endDialogue)";
        if (TkHoldEndFor(dlg, cooptalk::kTalkEndAnswered, whyA, "the engine's end inside the applied answer (a closing line's NPC-side part waits)")) return;
        ++g_tkEndAnswered;
        TkEndOwn(dlg, c.me, cooptalk::kTalkEndAnswered, whyA, 0);
        return;
    }
    const int i = TkMarkFind(dlg);
    if (i < 0 || g_tkMarks[i].state != 2)
    {
        TkEndOwn(dlg, c.me, cooptalk::kTalkEndEngine, "the Dialogue's mark left 'open' while the reply ran", 0);
        return;
    }
    const TkConv n = it->second;
    cooptalk::TalkMsg p;
    const int built = TkBuildPrompt(dlg, n, n.ev, &p);
    const char* why = 0;
    int reason = cooptalk::kTalkEndEngine;
    if (built == 0) why = "no next line after the reply";
    else if (built == -1) why = "the next line has no reply the target could give";
    else if (built == -2) { why = "the next line's string id is unreadable or longer than 128 bytes"; reason = cooptalk::kTalkEndNotForwardable; }
    else if (built == -3) { why = "a next reply's string id is longer than 128 bytes"; reason = cooptalk::kTalkEndNotForwardable; }
    else if (!net::SendTalk(p)) { why = "the next PROMPT could not be sent (link down)"; reason = 0; }
    if (why != 0)
    {
        ++g_tkNextUnfwd;
        TkLog("[TALK] A conv=" + TkU(c.convId) + " seq=" + TkU(c.seq) + " reply applied; next line NOT forwarded: " + why + " - this game ends its side");
        if (g_tkConvs.find(dlg) != g_tkConvs.end()
            && (reason == 0 || !TkHoldEndFor(dlg, reason, why, "this game's end (the next line is not forwarded)")))   /* P26s5 fold 2: held unless the link is down */
            TkEndOwn(dlg, c.me, reason, why, 1);
        return;
    }
    it = g_tkConvs.find(dlg);   /* the send may not keep the map as it was */
    if (it == g_tkConvs.end() || it->second.convId != c.convId) return;
    it->second.line = SayReadPtr(dlg, kDlgCurLine);
    it->second.seq = c.seq + 1;
    it->second.sentTick = ::GetTickCount();
    ++g_tkNextPrompt;
    std::string rs;
    for (size_t k = 0; k < p.replyIds.size(); ++k) rs += (k ? " | " : "") + p.replyIds[k] + ":'" + p.replyTexts[k].substr(0, 60) + "'";
    TkLog("[TALK] A next PROMPT sent conv=" + TkU(c.convId) + " seq=" + TkU(c.seq + 1) + " line=" + p.lineSid + " replies="
          + TkI((long long)p.replyIds.size()) + " text='" + p.npcText.substr(0, 120) + "' [" + rs + "] - no answer deadline");   /* P26s5 fold 5 */
}

/* A: the ANSWER. P26 stage 4: a REPLY is applied by the engine's own reply path on THIS game's conversation - Dialogue::replyClicked(int)
   0x683360 (Read build/decomp_683360: the reply text as the target's speech, then replyClicked(string id) 0x683170 = the reply line's
   _doActions 0x67FAD0 and sayLine 0x682F10 of the next NPC line chosen among its children, decomp_683170 / decomp_683c90) with the
   index of B's reply id in this game's reply list (+0x238): the NPC answers exactly as for a local player. A reply id that is not in
   the list (0x683170 would go on from the CURRENT line - Read) or a current line that is no longer the prompted one ends this side,
   END NOT_FORWARDABLE. CLOSED / BUSY / NO_CHAR / SHOW_FAILED end this side (no END: B's side is gone already). MAIN THREAD, K2 safe
   point, the world not blocked. */
static void TkApplyAnswer(const cooptalk::TalkMsg& m, unsigned int peer)
{
    std::map<void*, TkConv>::iterator it = g_tkConvs.begin();
    for (; it != g_tkConvs.end(); ++it) if (it->second.convId == m.convId && cooplive::SamePlayer(it->second.peer, peer, coop::LinkPeerSlot())) break;
    if (it == g_tkConvs.end())
    {
        ++g_tkAnsUnknown;
        TkLog("[TALK] A ANSWER " + std::string(cooptalk::TalkAnswerName(m.result)) + " conv=" + TkU(m.convId) + " from peer " + TkU(peer)
              + " names no open conversation here (already ended) - ignored");
        return;
    }
    void* dlg = it->first;
    const TkConv c = it->second;
    {   /* P26s5 fold 2: the conversation's end is held for a line's NPC-side part - it ends once the ACT_RESULT came */
        unsigned int parts = 0;
        const int hh = TkHoldLast(dlg, c.convId, &parts);
        if (hh >= 0 && g_tkActHolds[(size_t)hh].endHeld)
        {
            ++g_tkAnsWhileHeld;
            TkLog("[TALK] A ANSWER " + std::string(cooptalk::TalkAnswerName(m.result)) + " conv=" + TkU(c.convId)
                  + " ignored: the conversation's end is held until the answer to ACT seq=" + TkU(g_tkActHolds[(size_t)hh].seq));
            return;
        }
    }
    if (m.result != cooptalk::kTalkAnsReply)
    {
        if (m.result == cooptalk::kTalkAnsClosed) ++g_tkAnsClosed; else ++g_tkAnsOther;
        TkLog("[TALK] A ANSWER " + std::string(cooptalk::TalkAnswerName(m.result)) + " conv=" + TkU(c.convId) + " seq=" + TkU(c.seq) + " - this game ends its side");
        /* P26s4 fold 1 M2: SHOW_FAILED for a NEXT line (seq > 1: the other game had the window open) also sends END, so that
           window closes even where its game could not close it (an END for a window already closed is ignored there) */
        const int endB = (m.result == cooptalk::kTalkAnsShowFailed && c.seq > 1) ? cooptalk::kTalkEndEngine : 0;
        TkEndOwn(dlg, c.me, endB, cooptalk::TalkAnswerName(m.result), 1);
        return;
    }
    ++g_tkAnsReply;
    const int live = TkLiveOwner(dlg, c.me);
    void* cur = live ? SayReadPtr(dlg, kDlgCurLine) : 0;
    int mapped = -1;
    if (live)
    {
        std::vector<std::string> ids;
        TkReadStrVec(dlg, kDlgReplyBegin, kDlgReplyEnd, &ids, 256, (int)cooptalk::kTalkMaxId);
        for (size_t k = 0; k < ids.size(); ++k) if (!m.replyId.empty() && ids[k] == m.replyId) { mapped = (int)k; break; }
    }
    if (mapped >= 0) ++g_tkAnsMapped;
    ReplyClickedFn fn = orig_replyClicked;   /* the trampoline (the detour passes A's Dialogue through anyway) */
    if (fn == 0 && kDialogueReplyClickedRva != 0 && g_base != 0) fn = (ReplyClickedFn)(g_base + (uintptr_t)kDialogueReplyClickedRva);
    const char* why = 0;
    if (!live) why = "the NPC no longer owns its Dialogue";
    else if (cur == 0 || cur != c.line) why = "the NPC's current line is no longer the prompted one";
    else if (mapped < 0) why = "the reply id is not in this game's reply list for that line";
    else if (fn == 0) why = "replyClicked 0x683360 is not in the address table";
    const std::string head = "[TALK] A ANSWER REPLY conv=" + TkU(c.convId) + " seq=" + TkU(c.seq) + " index=" + TkI(m.index) + " id=" + m.replyId
                             + " mapsTo=" + TkI(mapped);
    if (why != 0)
    {
        ++g_tkUnmapped;
        TkLog(head + " NOT applied: " + why + " - this game ends its side");
        TkEndOwn(dlg, c.me, cooptalk::kTalkEndNotForwardable, why, 1);
        return;
    }
    TkPurseNote(c.targetUid, m.purse, peer);   /* the next line's money replies are judged by the talker's purse */
    TkLog(head + " - applied through the engine's replyClicked (the NPC answers as for a local player)");
    g_tkApplyA = dlg;
    const int called = TkApplyReplyPod(fn, dlg, mapped);
    g_tkApplyA = 0;
    TkAfterApply(dlg, c, called);
}

/* P26 stage 5: one line's router view (no C++ object; C2712) - its actions (lektor +0x208: count +0x210, +0x218 pointers to
   {type, value} - 67fad0:712-715), its givesItem (lektor +0xA8: count +0xB0, +0xB8 array of {GameData*, int}, 16 bytes -
   67fad0:604-613) and its factionRelationEffects (the map +0x2D0: size +0x2F0, first node *(+0x308)[+0x2E8], node {next, ?,
   GameData* key, int value} - 67fad0:371-381). The action pointers are kept for the view. -1 unreadable or over the wire's caps. */
struct TkLineView
{
    int n; int types[32]; int values[32]; void* acts[32];
    int nItems; void* itemGd[16]; int itemVal[16];
    int nRels; void* relGd[16]; int relVal[16];
};

static int TkLineViewPod(const void* line, TkLineView* v)
{
    v->n = 0; v->nItems = 0; v->nRels = 0;
    __try
    {
        const char* L = (const char*)line;
        const unsigned int n = *(const unsigned int*)(L + 0x210);
        void* const* acts = *(void* const* const*)(L + 0x218);
        if (n > cooptalk::kTalkMaxActs) return -1;
        for (unsigned int i = 0; i < n; ++i)
        {
            const int* a = (const int*)acts[i];
            if (a == 0 || SayPlausiblePtr(a) == 0) return -1;
            v->types[v->n] = a[0]; v->values[v->n] = a[1]; v->acts[v->n] = acts[i]; ++v->n;
        }
        const unsigned int ni = *(const unsigned int*)(L + 0xB0);
        if (ni > cooptalk::kTalkMaxActItems) return -1;
        const char* items = ni != 0 ? *(const char* const*)(L + 0xB8) : 0;
        for (unsigned int i = 0; i < ni; ++i)
        {
            v->itemGd[i] = *(void* const*)(items + 16 * i);
            v->itemVal[i] = *(const int*)(items + 16 * i + 8);
            ++v->nItems;
        }
        const unsigned long long nr = *(const unsigned long long*)(L + 0x2F0);
        if (nr > cooptalk::kTalkMaxActRels) return -1;
        if (nr != 0)
        {
            const unsigned long long first = *(const unsigned long long*)(L + 0x2E8);
            void* const* node = *(void* const* const*)(*(const char* const*)(L + 0x308) + first * 8);
            while (node != 0)
            {
                if (v->nRels >= (int)cooptalk::kTalkMaxActRels) return -1;
                v->relGd[v->nRels] = node[2];
                v->relVal[v->nRels] = *(const int*)&node[3];
                ++v->nRels;
                node = (void* const*)node[0];
            }
        }
        return v->n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* P26 stage 5: a GameData's stringID (TkLineSidPod's read on the GameData itself). Its length, -1 unreadable / over cap. */
static int TkGdSidPod(const void* gd, char* out, int cap)
{
    if (gd == 0) return -1;
    __try
    {
        const std::string& s = ((const ::GameData*)gd)->stringID;
        const size_t n = s.size();
        if (n > (size_t)cap) return -1;
        if (n > 0) std::memcpy(out, s.data(), n);
        return (int)n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static std::string TkGdSid(const void* gd)
{
    char b[cooptalk::kTalkMaxId + 1];
    const int n = TkGdSidPod(gd, b, (int)cooptalk::kTalkMaxId);
    return n > 0 ? std::string(b, (size_t)n) : std::string();
}

/* P26 stage 5: the engine's own _doActions (hire.cpp HireCallDoActions - the trampoline) on a VIEW of the line: its action list
   swapped for keep[0..nKeep) and the other side's pre-loop parts emptied.
   P26s5 fold 1: the view is a PRIVATE COPY of the line (g_tkLineCopy, kTkLineSize) - the shared line is never written. _doActions
   only READS its line (Read: build/decomp_67fad0.txt - the 19 offsets +0x18 (its address, 698: the campaignTriggers FitnessSelector)
   +0x28 +0xB0 +0xB8 +0x1D8 +0x210 +0x218 +0x230 +0x288 +0x290 +0x2A0 +0x2A8 +0x2B8 +0x2C0 +0x2C8 +0x2E8 +0x2F0 +0x308 +0x310; the
   reloads 689 / 694 / 776 / 779 / 1525 only reload local_4a8 = the line pointer), keeps no pointer to it and never compares it, so
   the copy stands for the line; the AI thread's own _doActions (hire.cpp detour_doActions runs the original off the main thread)
   meanwhile reads the untouched shared line. Pointers inside the copy (lists, strings, the relation table) still point at the
   line's own storage - read only, never freed here.
     side 0 (A, the NPC's owner): givesItem (+0xB0) and factionRelationEffects (+0x2F0) emptied - they are the target's.
     side 1 (B, the target's owner): the dialogue's own parts emptied - locks / unlocks (+0x288 / +0x2A0 / +0x2B8, 67fad0:158-283),
       campaignTriggers (+0x28, 67fad0:675), crowdTrigger (+0x2C8, 67fad0:577) and playerInterruptionDialog (+0x310, 67fad0:367);
       A ran them. The tail call (the NPC squad's note of the said line, +0x1D8, 67fad0:1786) runs on B's copy of the NPC too
       (Inferred harmless: the copy's AI is A's to decide).
   MAIN THREAD. 1 ran, 0 no trampoline, -1 the engine faulted, -2 the line did not read / nesting too deep. */
static int TkRunViewPod(void* dlg, void* line, void** keep, unsigned int nKeep, int side)
{
    if (line == 0 || g_tkViewDepth < 0 || g_tkViewDepth >= kTkViewDepthMax) return -2;
    char* C = g_tkLineCopy[g_tkViewDepth];
    int copied = 0;
    __try
    {
        std::memcpy(C, line, kTkLineSize);
        *(unsigned int*)(C + 0x210) = nKeep;
        *(void**)(C + 0x218) = (void*)keep;
        if (side == 0) { *(unsigned int*)(C + 0xB0) = 0; *(unsigned long long*)(C + 0x2F0) = 0; }
        else
        {
            *(unsigned int*)(C + 0x288) = 0; *(unsigned int*)(C + 0x2A0) = 0; *(unsigned int*)(C + 0x2B8) = 0;
            *(void**)(C + 0x2C8) = 0; *(void**)(C + 0x310) = 0; *(unsigned long long*)(C + 0x28) = 0;
        }
        copied = 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { copied = 0; }
    if (!copied) return -2;
    /* P26s6 fold 1: HireCallDoActions is the trampoline (hire.cpp's detour, and so S6-3, is not re-entered): the NPC-side run of a
       forwarded line gives the TALK_TO_LEADER order here, after the NPC part ran - now, or when a held part runs at its ACT_RESULT;
       never for a refused line. The view's own action list (C +0x210 / +0x218 = keep) is what is scanned. */
    void* const s6Target = (side == 0) ? TalkS6LeaderTarget(dlg, (void*)C) : 0;
    ++g_tkViewDepth;
    const int ran = HireCallDoActions(dlg, (void*)C);
    --g_tkViewDepth;
    if (s6Target != 0 && ran == 1) { ::InterlockedIncrement64(&g_s6ViewLead); TalkS6LeaderOrder(dlg, s6Target); }
    return ran;
}

/* P26s5 fold 1 (28, 67fad0:1279-1287): 1 = the character's faction (RootObject +0x10, hire.cpp kCharFactionOff) is a player
   faction (Faction +0x250 isPlayer, items.cpp kFactionIsPlayer), 0 not, -1 unreadable. */
static int TkFactionIsPlayerPod(void* c)
{
    if (c == 0) return -1;
    __try
    {
        const char* f = *(const char* const*)((const char*)c + kRootFaction);
        if (f == 0) return -1;
        return (*(void* const*)(f + 0x250) != 0) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* P26s5 fold 1: WHAT THE PLAYER IS TOLD of a refused / unavailable line - owner decision 224 (2026-09-30): exactly what single
   player shows for the same cause, otherwise nothing but the log. No reason here has a single-player display to copy:
     NO_MONEY - single player never refuses a dialogue payment: TAKE_MONEY calls Ownerships::takeMoney (vtable slot 0) and then
       shows 'Paid c.{1}' through GameWorld 0x723EF0 whenever the amount is <= 100000 (67fad0:1687-1692; DAT_14169aea8 = 100000.0f
       in the exe), with no test of the purse, and the dialogue conditions have no money test; the
       'Paid' notice would claim a payment that did not happen. The hire road already refuses a short purse silently (hire.cpp).
     LINE_DIFFERS / NO_CONV / NO_CHAR / NO_LINE / FAULT / NOT_HERE / HIRE_NOT_ASKED / UNSUPPORTED and TRADE / CHARACTER_EDITOR
       with another player's character (kTkNoticeNotAvailable) - no single-player counterpart.
   So this only logs; it is the one place a future approved notice would go. reason: an ACT_RESULT result, or kTkNoticeNotAvailable. */
static void TkRefusalNotice(int reason, int detail, const std::string& where)
{
    ++g_tkNotices;
    TkLog("[TALK] player notice: none (no single-player equivalent - decision 224) reason="
          + std::string(reason == kTkNoticeNotAvailable ? "NOT_AVAILABLE" : cooptalk::TalkActResultName(reason))
          + " detail=" + TkI(detail) + " - " + where);
}

static std::string TkActsStr(const std::vector<int>& ty, const std::vector<int>& va)
{
    std::string s;
    for (size_t i = 0; i < ty.size() && i < va.size(); ++i) s += (i ? "," : "") + TkI(ty[i]) + ":" + TkI(va[i]);
    return s;
}

static std::string TkPairsStr(const std::vector<std::string>& sid, const std::vector<int>& va)
{
    std::string s;
    for (size_t i = 0; i < sid.size() && i < va.size(); ++i) s += (i ? "," : "") + sid[i] + ":" + TkI(va[i]);
    return s;
}

/* P26 stage 5: B - an ACT: the TARGET-side part of one line of the mirrored conversation, applied to this game's own character with
   the engine's own code - _doActions on THIS game's copy of the same line (looked up by its string id), viewed down to exactly the
   ACT's parts (TkRunViewPod side 1), on the NPC copy's Dialogue, whose conversation target is this game's character while the
   mirrored window is open (TkHandIs) - and hire.cpp's owner-applied hire road for 3 / 18. Answered with ACT_RESULT.
   Refused (nothing applied): the NPC copy or the character is not there / not ours (NO_CHAR), no mirrored window of that
   conversation with that character (NO_CONV), the line is not in this game's data (NO_LINE), its target-side part differs from the
   ACT's (LINE_DIFFERS), the line's TAKE_MONEY total - a hire's price - is more than this game's purse (NO_MONEY: the engine's own
   takeMoney would refuse silently while _doActions still says 'Paid'). Money and items are made only here (A never ran them), so
   nothing is made twice; a refusal makes nothing. MAIN THREAD (the K2 drain). */
static void TkApplyAct(const cooptalk::TalkMsg& m, unsigned int peer)
{
    ++g_tkActRecv;
    cooptalk::TalkMsg r;
    r.kind = cooptalk::kTalkActResult; r.convId = m.convId; r.npcUid = m.npcUid; r.targetUid = m.targetUid; r.seq = m.seq;
    r.result = cooptalk::kTalkActApplied;
    std::string why;
    ::Character* npc = FindSpawned(m.npcUid);
    ::Character* tgt = FindSpawned(m.targetUid);
    void* dlg = 0;
    void* line = 0;
    if (npc == 0 || !net::UidOwnedByPeer(m.npcUid, peer)) { r.result = cooptalk::kTalkActNoChar; why = "no copy of that NPC from the sending game here"; }
    else if (tgt == 0 || !net::IsUidMine(m.targetUid)) { r.result = cooptalk::kTalkActNoChar; why = "the addressed character is not this game's"; }
    else if (!g_tkMirror.active || !cooplive::SamePlayer(g_tkMirror.peer, peer, coop::LinkPeerSlot()) || g_tkMirror.convId != m.convId || g_tkMirror.me != (void*)npc
             || (dlg = TkDialogueOf(npc)) == 0 || dlg != g_tkMirror.dlg || !TkHandIs(dlg, tgt))
    { r.result = cooptalk::kTalkActNoConv; why = "no mirrored window of that conversation is open here with that character"; }
    else if (HireDoActionsHookState() != 1 || kDialogDataGetDataRva == 0 || g_base == 0)
    { r.result = cooptalk::kTalkActNotHere; why = "hire.cpp's _doActions hook or DialogDataManager::getData is not available here"; }
    if (why.empty())
    {
        ::GameData* gd = (coop::GameWorldPtr() != 0) ? coop::GameWorldPtr()->gamedata.getData(m.lineSid) : 0;
        if (gd != 0) line = TkGetLinePod((DlgGetDataFn)(g_base + (uintptr_t)kDialogDataGetDataRva), gd);
        if (line == 0) { r.result = cooptalk::kTalkActNoLine; why = "the line " + m.lineSid + " is not in this game's dialogue data"; }
    }
    TkLineView v;
    v.n = 0; v.nItems = 0; v.nRels = 0;
    if (why.empty() && TkLineViewPod(line, &v) < 0) { r.result = cooptalk::kTalkActNoLine; why = "this game's copy of the line does not read"; }
    int hireType = 0, take = 0, unsupported = 0, firstUnsup = 0, carried = 0, noCarry = 0, carriedType = 0, carriedEngine = 0;
    unsigned int nKeep = 0, ownCarried = 0;
    if (why.empty())
    {
        std::vector<int> ty, va, iv, rv;
        std::vector<std::string> is, rs;
        for (int i = 0; i < v.n; ++i)
        {
            const int side = cooptalk::TalkActSide(v.types[i]);
            if (side == cooptalk::kTalkSideNpc || cooptalk::TalkActEndsTalk(v.types[i])) continue;   /* 1 / 11: never sent (P26s5 fold 1) */
            ty.push_back(v.types[i]); va.push_back(v.values[i]);
            if (side == cooptalk::kTalkSideHire && hireType == 0) hireType = v.types[i];
        }
        for (int i = 0; i < v.nItems; ++i) { is.push_back(TkGdSid(v.itemGd[i])); iv.push_back(v.itemVal[i]); }
        for (int i = 0; i < v.nRels; ++i) { rs.push_back(TkGdSid(v.relGd[i])); rv.push_back(v.relVal[i]); }
        if (ty != m.actTypes || va != m.actValues || is != m.itemSids || iv != m.itemValues || rs != m.relSids || rv != m.relValues)
        {
            r.result = cooptalk::kTalkActLineDiffers;
            why = "this game's copy of the line has another target-side part: acts=[" + TkActsStr(ty, va) + "] items=[" + TkPairsStr(is, iv)
                  + "] rels=[" + TkPairsStr(rs, rv) + "]";
        }
    }
    if (why.empty())
    {
        for (int i = 0; i < v.n; ++i)
        {
            const int side = cooptalk::TalkActSide(v.types[i]);
            if (side == cooptalk::kTalkSideNpc || cooptalk::TalkActEndsTalk(v.types[i])) continue;   /* P26s5 fold 1 */
            if (v.types[i] == 8) { take += v.values[i]; if (hireType != 0) continue; }   /* with a hire in the line it is the price (HoldLine's rule) */
            if (side == cooptalk::kTalkSideApply) g_tkActView[nKeep++] = v.acts[i];
            else if (side == cooptalk::kTalkSideRefuse) { ++unsupported; if (firstUnsup == 0) firstUnsup = v.types[i]; }
            else if (side == cooptalk::kTalkSideCarried)
            {   /* the person our own character carries must be the one the NPC's game saw it carry */
                if (carriedType == 0) { carriedType = v.types[i]; ownCarried = CarryingUidOf(m.targetUid); }
                if (!cooptalk::TalkCarriedApplyOk(m.carriedUid, ownCarried)) { noCarry = 1; continue; }
                ++carried;
                if (cooptalk::TalkCarriedTalkerEngine(v.types[i], net::IsUidMine(ownCarried) ? 1 : 0))
                { g_tkActView[nKeep++] = v.acts[i]; ++carriedEngine; }
            }
        }
        int money = 0;
        if (noCarry)
        {
            r.result = cooptalk::kTalkActNoCarry; r.detail = carriedType;
            why = "the NPC's game saw our character carry uid " + TkU(m.carriedUid) + ", here it carries " + (ownCarried ? "uid " + TkU(ownCarried) : std::string("nobody"));
        }
        else if (!TkPurseMoneyPod(tgt, &money)) { r.result = cooptalk::kTalkActNotHere; why = "this game's purse does not read"; }
        else
        {
            r.moneyBefore = money;
            if (take > money)
            {
                r.result = cooptalk::kTalkActNoMoney; r.detail = 8;
                why = "the line takes " + TkI(take) + " cats and this game's purse holds " + TkI(money);
            }
        }
    }
    int ran = 0, hired = 0;
    std::string hireWhy;
    if (why.empty())
    {
        if (nKeep > 0 || v.nItems > 0 || v.nRels > 0)
        {
            ran = TkRunViewPod(dlg, line, g_tkActView, nKeep, 1);
            if (ran != 1)
            {
                r.result = cooptalk::kTalkActFault;
                why = std::string("the engine's _doActions ") + (ran == -1 ? "faulted" : (ran == 0 ? "has no trampoline" : "could not view the line"));
            }
            else ++g_tkActLinesRun;
        }
        if (why.empty() && hireType != 0)
        {
            hired = HireForTalk(m.npcUid, m.targetUid, take, hireType, &hireWhy);
            if (!hired) { r.result = cooptalk::kTalkActHireNotAsked; r.detail = hireType; why = "the hire was not asked: " + hireWhy; }
        }
        if (why.empty() && nKeep == 0 && v.nItems == 0 && v.nRels == 0 && hireType == 0 && carried == 0)
        {
            r.result = cooptalk::kTalkActUnsupported; r.detail = firstUnsup;
            why = "no part of the line is one the router applies (type " + TkI(firstUnsup) + ")";
        }
        if (why.empty() && carried > 0)
        {   /* our character gives the person to the NPC: it lets go when the NPC's own game says the NPC carries it */
            CarryHandOverNote(ownCarried, m.targetUid, m.npcUid);
            ++g_tkCarriedApplied;
            g_tkCarriedTalkerEngine += carriedEngine;
        }
    }
    if (noCarry) ++g_tkCarriedNoCarry;
    int after = 0;
    if (r.moneyBefore >= 0 && TkPurseMoneyPod(tgt, &after)) r.moneyAfter = after;
    const int parts = (ran == 1 ? (int)nKeep - carriedEngine + v.nItems + v.nRels : 0) + hired + (why.empty() ? carried : 0);
    r.anyApplied = (ran == -1) ? cooptalk::kTalkAnyUnknown : (parts > 0 ? cooptalk::kTalkAnyYes : cooptalk::kTalkAnyNone);   /* P26s5 fold 1 */
    r.applied = parts > 255 ? 255 : parts;
    r.unsupported = unsupported > 255 ? 255 : unsupported;
    if (r.result == cooptalk::kTalkActApplied) ++g_tkActApplied; else ++g_tkActRefused;
    if (r.result == cooptalk::kTalkActFault) ++g_tkActFault;
    if (hired) ++g_tkActHires;
    g_tkActUnsupported += unsupported;
    const int sent = net::SendTalk(r) ? 1 : 0;
    TkLog("[TALK] B ACT seq=" + TkU(m.seq) + " conv=" + TkU(m.convId) + " line=" + m.lineSid + " acts=[" + TkActsStr(m.actTypes, m.actValues)
          + "] items=[" + TkPairsStr(m.itemSids, m.itemValues) + "] rels=[" + TkPairsStr(m.relSids, m.relValues) + "] -> "
          + cooptalk::TalkActResultName(r.result) + (why.empty() ? std::string("") : " - " + why)
          + " applied=" + TkI(r.applied) + " unsupported=" + TkI(unsupported) + (firstUnsup ? " (first type " + TkI(firstUnsup) + ")" : std::string(""))
          + " hire=" + TkI(hired) + (carried ? " carried person uid=" + TkU(ownCarried) + " handed to the NPC (engine case run here: "
                                               + TkI(carriedEngine) + ")" : std::string(""))
          + " anyApplied=" + TkI(r.anyApplied) + " purse " + TkI(r.moneyBefore) + " -> " + TkI(r.moneyAfter)
          + " char=" + (tgt != 0 ? TkNameStr(tgt) : std::string("?")) + "; ACT_RESULT " + (sent ? "sent" : "NOT sent (link down)"));
    if (r.result != cooptalk::kTalkActApplied)   /* P26s5 fold 1: this game's player is the one in the conversation */
        TkRefusalNotice(r.result, r.detail, "B: the other player's NPC's line " + m.lineSid + " was refused here");
}

/* A, a carried-person line whose engine case does not run here: the NPC's two orders from that case (67fad0:1675-1682) - take the
   person (0xD5, sixth argument 1), then cage it (0x70, sixth argument 0), both with issueOrder's own seventh argument
   (*AddOrderArgGlobal, as at 0x6823DB) - on this game's view of the person the other player's character carries. No C++ object
   (C2712). 1 given, 0 faulted. */
const int kTkTaskTakeCarried = 0xD5;
const int kTkTaskCagePerson = 0x70;
static int TkTakeOrdersPod(void* npc, void* person)
{
    const S6AddOrderFn ao = (S6AddOrderFn)(g_base + (uintptr_t)kS6AddOrderRva);
    __try
    {
        const unsigned long long a7 = *(const unsigned long long*)(g_base + (uintptr_t)kS6AddOrderArgRva);
        ao(npc, 0, kTkTaskTakeCarried, person, 0, 1, a7);
        ao(npc, 0, kTkTaskCagePerson, person, 0, 0, a7);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* A: the take of a carried-person line, after the other game answered APPLIED (it paid its player and marked the hand-over). The
   NPC must still be this game's; the person is whoever this game knows by the uid the ACT named. MAIN THREAD (the K2 drain). */
static void TkTakeCarried(unsigned int npcUid, unsigned int personUid, unsigned int convId, unsigned int seq)
{
    std::string why;
    ::Character* const npc = FindSpawned(npcUid);
    ::Character* const person = FindSpawned(personUid);
    if (g_base == 0 || kS6AddOrderRva == 0 || kS6AddOrderArgRva == 0) why = "issueOrder is not in this game's address table";
    else if (npc == 0 || !net::IsUidMine(npcUid)) why = "the NPC is not this game's any more";
    else if (person == 0) why = "the carried person is not loaded here";
    else if (!TkTakeOrdersPod(npc, person)) { ++g_tkTakeFault; why = "the engine's issueOrder faulted"; }
    if (why.empty()) ++g_tkTakeOrders; else ++g_tkTakeRefused;
    TkLog("[TALK] A conv=" + TkU(convId) + " ACT seq=" + TkU(seq) + " carried person uid=" + TkU(personUid) + " NPC uid=" + TkU(npcUid)
          + (why.empty() ? std::string(" - the NPC is ordered to take the person and cage it (the engine's own two orders)")
                         : " - NO take orders: " + why));
}

/* P26 stage 5: A - the other game's answer to an ACT.
   P26s5 fold 1: the line's NPC-side part was HELD (TalkDeferTargetActs) until this answer. APPLIED runs it now through the engine's
   own _doActions (TkRunViewPod side 0) - and a line with TRADE / CHARACTER_EDITOR (1 / 11) then ends the conversation; ANY refusal
   runs none of it and ends the conversation on both games (END ENGINE): never an NPC half done (a bribe: no money taken, the guard
   does not stand down either). A conversation already over when the answer comes runs nothing (logged). MAIN THREAD (K2 drain). */
static void TkApplyActResult(const cooptalk::TalkMsg& m, unsigned int peer)
{
    if (m.result == cooptalk::kTalkActApplied) ++g_tkActResOk; else ++g_tkActResRefused;
    ::Character* copy = FindSpawned(m.targetUid);
    int here = -1;
    if (copy != 0 && !TkPurseMoneyPod(copy, &here)) here = -1;
    if (m.moneyAfter >= 0 && net::UidOwnedByPeer(m.targetUid, peer) && TkTalkerInConv(m.targetUid))
        TkPurseNote(m.targetUid, m.moneyAfter, peer);   /* the talker's purse after the act; a talk already over keeps none */
    int hi = -1;
    for (size_t k = 0; k < g_tkActHolds.size(); ++k)
        if (g_tkActHolds[k].seq == m.seq && cooplive::SamePlayer(g_tkActHolds[k].peer, peer, coop::LinkPeerSlot()) && g_tkActHolds[k].convId == m.convId) { hi = (int)k; break; }
    TkActHold h;
    std::memset(&h, 0, sizeof(h));
    if (hi >= 0) { h = g_tkActHolds[(size_t)hi]; g_tkActHolds.erase(g_tkActHolds.begin() + hi); }
    std::map<void*, TkConv>::iterator it = (hi >= 0) ? g_tkConvs.find(h.dlg) : g_tkConvs.end();
    const int live = (it != g_tkConvs.end() && it->second.convId == h.convId && cooplive::SamePlayer(it->second.peer, peer, coop::LinkPeerSlot()) && TkLiveOwner(h.dlg, h.me)) ? 1 : 0;
    const int applied = (m.result == cooptalk::kTalkActApplied) ? 1 : 0;
    std::string what;
    if (hi < 0) what = "names no held line here (dropped at the cap, or the link was lost) - nothing runs";
    else if (!applied)
    {
        ++g_tkActHoldRefused;
        what = std::string("the line's NPC-side part does NOT run here")
             + (live ? "; the conversation ends on both games (END ENGINE)" : " (the conversation is already over)")
             + (m.anyApplied == cooptalk::kTalkAnyYes ? " - NOTE the other game says part of it was ALREADY applied there"
               : (m.anyApplied == cooptalk::kTalkAnyUnknown ? " - NOTE the other game's engine faulted part-way: part of it may be applied there" : ""));
    }
    else if (!live) { ++g_tkActHoldLost; what = "the conversation ended before the answer came - the line's NPC-side part did not run (the other game's part did)"; }
    else
    {
        const int ran = TkRunViewPod(h.dlg, h.line, h.acts, h.n, 0);
        if (ran == 1) ++g_tkActNpcRan; else ++g_tkActNpcFault;
        what = "the held NPC-side part ran here=" + TkI(ran)
             + (h.endsTalk ? std::string("; TRADE / CHARACTER_EDITOR in the line: the conversation ends; the other game opens that window itself (P25)")
                           : (h.endHeld ? std::string(" - then the conversation's held end runs")   /* P26s5 fold 2 */
                                        : std::string(" - the conversation goes on")));
    }
    /* A carried person the other game has already been paid for, or handed over, is taken even when the conversation ended before
       the answer: the take belongs to the NPC, not to the conversation, and the other game's character is waiting to let go. */
    if (hi >= 0 && applied && h.takeOrders) TkTakeCarried(h.npcUid, h.carriedUid, h.convId, h.seq);
    TkLog("[TALK] A ACT_RESULT seq=" + TkU(m.seq) + " conv=" + TkU(m.convId) + " from peer " + TkU(peer) + " " + cooptalk::TalkActResultName(m.result)
          + " detail=" + TkI(m.detail) + " applied=" + TkI(m.applied) + " anyApplied=" + TkI(m.anyApplied) + " unsupported=" + TkI(m.unsupported)
          + " their purse " + TkI(m.moneyBefore) + " -> " + TkI(m.moneyAfter) + "; the copy's purse here " + TkI(here) + " - " + what);
    if (hi >= 0 && h.endHeld && (!live || !applied))   /* P26s5 fold 2 */
    {
        ++g_tkEndHeldNotRun;
        TkLog("[TALK] A conv=" + TkU(h.convId) + " ACT seq=" + TkU(h.seq) + " held end: " + (live
              ? std::string("runs now with nothing of the line run (the other game refused)")
              : std::string("already taken by another end (the held end's 30 s ACT_RESULT clock, link loss, the NPC gone, a restart or an off-thread end) - nothing of the line ran")));
    }
    if (hi < 0 || !live) return;
    if (!applied) { TkEndOwn(h.dlg, h.me, cooptalk::kTalkEndEngine, "the other game refused the line's target-side part", 1); return; }
    /* P26s5 fold 4: endHeldRan is counted below, where the held end itself runs */
    if (h.endsTalk && g_tkConvs.find(h.dlg) != g_tkConvs.end())   /* not already ended by the NPC part itself */
    {
        ++g_tkActEndsTalk;   /* P25 step 5 (manager decision 2): the END carries the type - the other game opens its OWN window */
        TkEndOwn(h.dlg, h.me, cooptalk::kTalkEndEngine, "a TRADE / CHARACTER_EDITOR line ends the conversation (the other game opens that window)", 1, h.endsType);
    }
    if (h.endHeld && g_tkConvs.find(h.dlg) != g_tkConvs.end())   /* P26s5 fold 2: not already ended by the NPC part or 1 / 11 */
    {
        ++g_tkEndHeldRan;   /* P26s5 fold 4 */
        if (h.endReason == cooptalk::kTalkEndAnswered) ++g_tkEndAnswered;
        TkLog("[TALK] A conv=" + TkU(h.convId) + " ACT seq=" + TkU(h.seq) + " held NPC-side part ran - the held end runs now (END "
              + std::string(cooptalk::TalkEndName(h.endReason)) + "): single-player order, both halves then the end");
        TkEndOwn(h.dlg, h.me, h.endReason, h.endWhy != 0 ? h.endWhy : "the held end", 1);
    }
}

/* P25 step 5: B - the engine's own window opens, each call fault-guarded. No C++ object here (C2712). 1 opened, 0 not. */
static int TkP25TradePod(void* npc, void* own)
{
    __try
    {
        void* const inst = ((P25TradeInstFn)(g_base + (uintptr_t)kP25TradeInstRva))();
        if (inst == 0) return 0;
        ((P25TradeOpenFn)(g_base + (uintptr_t)kP25TradeOpenRva))(inst, npc, own, 1);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int TkP25EditorPod(void* own)
{
    __try
    {
        void* const pi = *(void* const*)(g_base + (uintptr_t)kP25PlayerIfaceRva);
        if (pi == 0) return 0;
        ((P25CharEditorFn)(g_base + (uintptr_t)kP25CharEditorRva))(pi, own);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* P25 step 5 (manager decision 2): B - the NPC's game ended the conversation on a TRADE / CHARACTER_EDITOR line (END endsType 1 / 11):
   this game opens its OWN trade window over the NPC's copy with its own character (0x951620 on the 0x4FDE00 singleton, 67fad0:1567-
   1570) or the editor for its own character (0x7F2AD0 on the PlayerInterface, 1721-1722) - the engine's own calls, after the window
   closed (its order). The goods ride the shop / trade roads. MAIN THREAD (the K2 drain). */
static void TkP25OpenLocal(const TkMirror& mi, int endsType)
{
    const int kind = cooptalk::TalkEndsLocal(endsType);
    if (kind == cooptalk::kTalkLocalNone) return;
    ::Character* const npc = FindSpawned(mi.npcUid);
    ::Character* const own = FindSpawned(mi.targetUid);
    std::string why;
    if (npc == 0 || (void*)npc != mi.me) why = "the NPC copy is gone";
    else if (own == 0 || !net::IsUidMine(mi.targetUid)) why = "the addressed character is not this game's";
    else if (g_base == 0 || (kind == cooptalk::kTalkLocalTrade && (kP25TradeInstRva == 0 || kP25TradeOpenRva == 0))
             || (kind == cooptalk::kTalkLocalEditor && (kP25CharEditorRva == 0 || kP25PlayerIfaceRva == 0)))
        why = "the window's calls are not in this game's address table";
    if (why.empty())
    {
        const int ok = (kind == cooptalk::kTalkLocalTrade) ? TkP25TradePod(npc, own) : TkP25EditorPod(own);
        if (!ok) { ++g_p25LocalFault; why = "the engine's call faulted or had no window"; }
        else if (kind == cooptalk::kTalkLocalTrade) ++g_p25TradeLocal;
        else ++g_p25EditorLocal;
    }
    TkLog("[TALK] P25 B END endsType=" + TkI(endsType) + " conv=" + TkU(mi.convId) + " npc uid=" + TkU(mi.npcUid) + " -> "
          + (kind == cooptalk::kTalkLocalTrade ? "trade window" : "character editor")
          + (why.empty() ? std::string(" OPENED here (this game's own window, own character)") : " NOT opened: " + why));
}

/* B: the END - the mirrored window closes. */
static void TkApplyEnd(const cooptalk::TalkMsg& m, unsigned int peer)
{
    if (m.reason == cooptalk::kTalkEndNotStarted)   /* P25: the NPC's game did not start what this game's click asked for */
    {
        if (g_p25Pend.active && g_p25Pend.reqId == m.reqId && cooplive::SamePlayer(g_p25Pend.peer, peer, coop::LinkPeerSlot()))   /* fold re-check: raw link id vs slot key, one player */
        {
            g_p25Pend.active = 0;
            ++g_p25WhyB[(m.why > 0 && m.why < cooptalk::kTalkWhyCount) ? m.why : 0];
            TkLogFail("[TALK] P25 req=" + TkU(m.reqId) + " npc uid=" + TkU(m.npcUid) + " NOT started on the NPC's game: "
                      + cooptalk::TalkWhyName(m.why) + " - the click does nothing here (one world: no local-only conversation)");
        }
        else
        {
            ++g_p25EndUnknown;
            TkLog("[TALK] P25 END NOT_STARTED req=" + TkU(m.reqId) + " from peer " + TkU(peer) + " names no request waiting here - ignored");
        }
        return;
    }
    if (!g_tkMirror.active || !cooplive::SamePlayer(g_tkMirror.peer, peer, coop::LinkPeerSlot()) || g_tkMirror.convId != m.convId)
    {
        ++g_tkEndUnknown;
        TkLog("[TALK] B END " + std::string(cooptalk::TalkEndName(m.reason)) + " conv=" + TkU(m.convId) + " names no mirrored window here - ignored");
        return;
    }
    const TkMirror mi = g_tkMirror;
    TkMirrorOff();
    int ended = 0;
    if (FindSpawned(mi.npcUid) == (::Character*)mi.me && TkDialogueOf(mi.me) == mi.dlg)
    {
        g_tkApplyDlg = mi.dlg;
        ended = TkEndPod(mi.dlg);
        g_tkApplyDlg = 0;
    }
    ++g_tkEndRecv;
    TkLog("[TALK] B END " + std::string(cooptalk::TalkEndName(m.reason)) + " conv=" + TkU(m.convId) + " - window closed (endDialogue=" + TkI(ended) + ")");
    if (m.endsType != 0) TkP25OpenLocal(mi, m.endsType);   /* P25 step 5: this game's own trade window / editor */
}

/* P25: A - END NOT_STARTED for a REQUEST (counted by why, logged). MAIN THREAD. */
static void TkP25NotStarted(const cooptalk::TalkMsg& m, unsigned int peer, int why, const std::string& detail)
{
    ++g_p25WhyA[(why > 0 && why < cooptalk::kTalkWhyCount) ? why : 0];
    cooptalk::TalkMsg e;
    e.kind = cooptalk::kTalkEnd; e.convId = 0; e.npcUid = m.npcUid; e.targetUid = m.targetUid;
    e.reason = cooptalk::kTalkEndNotStarted; e.reqId = m.reqId; e.why = why;
    const int sent = net::SendTalk(e) ? 1 : 0;
    TkLogFail("[TALK] P25 request req=" + TkU(m.reqId) + " npc uid=" + TkU(m.npcUid) + " <- talker uid=" + TkU(m.targetUid) + " from peer "
              + TkU(peer) + " NOT started: " + cooptalk::TalkWhyName(why) + " (" + detail + ") - END NOT_STARTED " + (sent ? "sent" : "FAILED (link down)"));
}

/* P25: A - the other game's player clicked this game's NPC (its copy there): start the conversation HERE on the real NPC with this
   game's copy of the talker, through the engine's own player-talk entry Dialogue::sendEvent(npc, copy, 1) - its hook, the P26
   starters' mark and forwarding (PROMPT with the reqId), the camera gate answered as the NPC (the view-point hook, armed for this
   call only), the start's first line held and routed after the PROMPT. MAIN THREAD (the K2 drain). */
static void TkApplyRequest(const cooptalk::TalkMsg& m, unsigned int peer)
{
    ++g_p25ReqRecv;
    ::Character* const npc = FindSpawned(m.npcUid);
    ::Character* const cp = FindSpawned(m.targetUid);
    void* const dlg = (npc != 0) ? TkDialogueOf(npc) : 0;
    void* const cdlg = (cp != 0) ? TkDialogueOf(cp) : 0;
    ::Faction* nf = 0;
    const int npcOurs = (npc != 0 && dlg != 0 && net::IsUidMine(m.npcUid) && !TkPeerTarget(npc) && CrimeFactionOf(npc, &nf) && nf != 0
                         && !IsPlayerFaction(nf)) ? 1 : 0;
    const int down = (npcOurs && (CrimeIsDead(npc) != 0 || IsDownedCharacter(npc))) ? 1 : 0;
    const int copyOk = (cp != 0 && cdlg != 0 && net::UidOwnedByPeer(m.targetUid, peer) && TkPeerTarget(cp)) ? 1 : 0;
    void* const win = TkWindowDlg();
    const int busy = ((dlg != 0 && (TkDlgBusy(dlg) || win == dlg || TkMarkFind(dlg) >= 0)) || (cdlg != 0 && TkDlgBusy(cdlg))) ? 1 : 0;
    Ogre::Vector3 np, cq;
    float dist = -1.0f;
    if (npcOurs && copyOk && SafeReadPosition(npc, &np) && SafeReadPosition(cp, &cq))
    {
        const float dx = np.x - cq.x, dy = np.y - cq.y, dz = np.z - cq.z;
        dist = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    const float gate = (g_base != 0 && kP25GateDistRva != 0) ? *(const float*)(g_base + (uintptr_t)kP25GateDistRva) : 0.0f;
    const int nearNpc = (dist >= 0.0f && gate > 0.0f && dist <= gate) ? 1 : 0;
    const int on = (g_p25On && kDialogueSendEventRva != 0 && g_base != 0) ? 1 : 0;
    char d[320];
    std::sprintf(d, "npcOurs=%d down=%d copyOk=%d busy=%d dist=%.0f gate=%.0f event=%d viewHook=%s", npcOurs, down, copyOk, busy, dist,
                 gate, m.event, TalkHookState(g_p25ViewHook));
    const int why0 = cooptalk::TalkP25RequestWhy(on, npcOurs, down, copyOk, busy, nearNpc, m.event == 1 ? 1 : 0);
    if (why0 != 0) { TkP25NotStarted(m, peer, why0, d); return; }
    const long long promptBefore = g_tkPromptSent;
    g_p25StartDlg = dlg; g_p25ReqIdNow = m.reqId; g_p25FirstLine = 0; g_p25CamDist = -1.0f;
    g_p25StartSeen = 0; g_p25StartHadLine = 0;   /* P25 fold 2 [p25f2-b2] */
    const long long gateRef0 = (long long)g_p25GateRefused;
    int res = 0;
    TkPurseNote(m.targetUid, m.purse, peer);   /* the first line's money replies are judged by the talker's purse */
    const int called = TalkSendPod((SendEventFn)(g_base + (uintptr_t)kDialogueSendEventRva), dlg, cp, 1, &res);   /* its start is scoped (detour_startPlayerConv) */
    g_p25StartDlg = 0; g_p25ReqIdNow = 0;
    g_talkCurEvent = -1;   /* a fault inside would have skipped detour_sendEvent's restore */
    void* const first = g_p25FirstLine;
    g_p25FirstLine = 0;
    const int subst = (g_p25CamDist >= 0.0f) ? 1 : 0;   /* this start's own substitution (main thread) */
    std::map<void*, TkConv>::iterator it = g_tkConvs.find(dlg);
    const int started = (called && g_tkPromptSent > promptBefore && it != g_tkConvs.end() && it->second.targetUid == m.targetUid) ? 1 : 0;
    char vb[200];
    std::sprintf(vb, "view point answered as the NPC=%d (the camera was %.0f u from the talker; gate %.0f)", subst, g_p25CamDist, gate);
    if (!started)
    {
        int endLater = 0;
        for (size_t k = 0; k < g_tkEndLater.size(); ++k) if (g_tkEndLater[k].dlg == dlg) endLater = 1;
        /* P25 fold 2 [p25f2-b3]: 'engine' split - noLine (startPlayerConversation ran with no line: the NPC has nothing to say to
           the talker), engine (sendEvent returned before it: one of its own early exits), gate (a start with a line refused) */
        const int gateNo = ((!subst && g_p25ViewHook != 1) || (long long)g_p25GateRefused > gateRef0) ? 1 : 0;
        const int why = cooptalk::TalkP25NotStartedWhy(called, endLater, g_p25StartSeen, g_p25StartHadLine, gateNo);
        if (first != 0) ++g_p25FirstLineDropped;
        TkPurseTalkEnded(m.targetUid);   /* nothing started: the purse saved for it is not kept */
        TkP25NotStarted(m, peer, why, std::string(d) + "; sendEvent called=" + TkI(called) + " result=" + TkI(res)
                        + "; startPlayerConversation reached=" + TkI(g_p25StartSeen) + " with a line=" + TkI(g_p25StartHadLine) + "; " + vb
                        + (first != 0 ? "; its held first line never ran" : ""));
        return;
    }
    ++g_p25StartedA;
    const unsigned int conv = it->second.convId;
    TkLog("[TALK] P25 request req=" + TkU(m.reqId) + " npc uid=" + TkU(m.npcUid) + " '" + TkNameStr(npc) + "' <- talker copy uid="
          + TkU(m.targetUid) + " '" + TkNameStr(cp) + "' from peer " + TkU(peer) + " -> started conv=" + TkU(conv) + "; " + vb
          + (first != 0 ? "; its first line was held and goes to the router now" : ""));
    if (first != 0)
    {
        const bool routed = TalkDeferTargetActs(dlg, first);
        if (routed) ++g_p25FirstLineRouted; else ++g_p25FirstLineDropped;
        TkLog("[TALK] P25 conv=" + TkU(conv) + " first line " + TkLineSid(first) + (routed
              ? std::string(" routed after the PROMPT: target-side part as ACT, NPC-side part held until the answer")
              : std::string(" NOT routed (the conversation is no longer forwarded) - none of it ran")));
    }
}

/* P25: B - the clicks the any-thread intercept queued: re-checked with the main-thread tables, then sent as REQUEST (one waiting at a
   time). A click whose NPC is this game's after all runs the engine's own start now. Anything else: the click does nothing - counted,
   logged (decision 1). MAIN THREAD (the K2 drain), the world not blocked. */
static void TkP25Drain()
{
    /* P25 fold 1 L2 [p25f1-L2b]: the main-thread recheck of the last talker the any-thread owned mirror did not list */
    const unsigned int missUid = (unsigned int)::InterlockedExchange(&g_p25MissUid, 0);
    if (missUid != 0 && net::IsUidMine(missUid))
    {
        ++g_p25MineMissConfirmed;
        TkLogFail("[TALK] P25 my uid=" + TkU(missUid) + " clicked an NPC copy the other game drives while the any-thread owned mirror did not list"
                  " that uid - the conversation ran HERE, locally (no REQUEST)");
    }
    TkReqSlot got[kTkReqSlots];
    int n = 0;
    for (int i = 0; i < kTkReqSlots; ++i)
    {
        if (::InterlockedCompareExchange(&g_tkReqSlots[i].state, 2, 2) != 2) continue;
        got[n].dlg = g_tkReqSlots[i].dlg; got[n].me = g_tkReqSlots[i].me; got[n].who = g_tkReqSlots[i].who; got[n].line = g_tkReqSlots[i].line;
        ++n;
        ::InterlockedExchange(&g_tkReqSlots[i].state, 0);
    }
    for (int k = 0; k < n; ++k)
    {
        void* const dlg = got[k].dlg;
        void* const me = got[k].me;
        void* const who = got[k].who;
        int why = 0;
        std::string detail;
        unsigned int nuid = 0, tuid = 0, peer = 0;
        if (!TkLiveChar(me) || TkDialogueOf(me) != dlg || !TkLiveChar(who)) { why = cooptalk::kTalkWhyNoCopy; detail = "the NPC copy or the talker is gone"; }
        else
        {
            nuid = FindSpawnedUid(me);
            tuid = FindSpawnedUid(who);
            if (nuid != 0 && net::IsUidMine(nuid))
            {
                int st = 0;
                g_p25NoIntercept = 1;
                const int called = (kDialogueStartPlayerConvRva != 0 && g_base != 0)
                    ? TkStartPod((StartPlayerConvFn)(g_base + (uintptr_t)kDialogueStartPlayerConvRva), dlg, who, got[k].line, &st) : 0;
                g_p25NoIntercept = 0;
                ++g_p25Relocal;
                TkLog("[TALK] P25 click on npc uid=" + TkU(nuid) + " '" + TkNameStr(me) + "': the NPC is this game's after all - the engine's own"
                      " start runs here (called=" + TkI(called) + " started=" + TkI(st) + ")");
                continue;
            }
            if (tuid == 0 || !net::IsUidMine(tuid)) { why = cooptalk::kTalkWhyNoCopy; detail = "the talker is not this game's"; }
            else if (nuid == 0 || !net::UidOwnerPeer(nuid, &peer)) { why = cooptalk::kTalkWhyNotOurs; detail = "no game has claimed the NPC"; }
            else if (coop::StorePeerHere(peer) == 0)   /* a REQUEST (SendTalk) goes to the NPC's owner: that player must be in the world here */
            {
                why = cooptalk::kTalkWhyLink; if (!net::OwnerIsSessionPeer(peer)) ++g_p25ThirdOwner;
                detail = "the NPC's game (owner key " + TkU(peer) + ") is not in the world here";
            }
            else if (g_p25Pend.active) { why = cooptalk::kTalkWhyBusy; detail = "request req=" + TkU(g_p25Pend.reqId) + " still waits for its answer"; }
            else if (g_tkMirror.active) { why = cooptalk::kTalkWhyBusy; detail = "a mirrored conversation is shown here"; }
        }
        if (why == 0)
        {
            cooptalk::TalkMsg q;
            q.kind = cooptalk::kTalkRequest; q.convId = 0; q.npcUid = nuid; q.targetUid = tuid; q.event = 1;
            {   /* the talker's purse: the NPC's game judges the first line's money replies by it */
                int pv = -1;
                q.purse = TkPurseMoneyPod(who, &pv) ? pv : -1;
            }
            q.reqId = ++g_p25NextReq;
            if (q.reqId == 0) q.reqId = ++g_p25NextReq;
            if (!net::SendTalk(q)) { why = cooptalk::kTalkWhyLink; detail = "the REQUEST could not be sent (link down)"; }
            else
            {
                g_p25Pend.active = 1; g_p25Pend.reqId = q.reqId; g_p25Pend.peer = peer; g_p25Pend.npcUid = nuid; g_p25Pend.targetUid = tuid;
                g_p25Pend.tick = ::GetTickCount();
                ++g_p25ReqSent;
                if (got[k].line == 0) ++g_p25ReqNoLine;   /* P25 fold 2 [p25f2-b4] */
                const std::string copyLine = (got[k].line != 0) ? TkLineSid(got[k].line) : std::string();
                TkLog("[TALK] P25 request sent req=" + TkU(q.reqId) + " npc uid=" + TkU(nuid) + " '" + TkNameStr(me) + "' (a copy peer " + TkU(peer)
                      + " drives) <- my uid=" + TkU(tuid) + " '" + TkNameStr(who) + "' - no local window: the NPC's game runs the conversation"
                      + "; the copy's line here=" + (got[k].line == 0 ? std::string("none (the NPC's game decides)")
                                                     : (copyLine.empty() ? std::string("? (unreadable)") : copyLine)));
                continue;
            }
        }
        ++g_p25WhyB[why];
        TkLogFail("[TALK] P25 click on npc uid=" + TkU(nuid) + " by my uid=" + TkU(tuid) + " NOT requested: " + cooptalk::TalkWhyName(why) + " ("
                  + detail + ") - the click does nothing (one world: no local-only conversation)");
    }
}

/* P25: B - a REQUEST with no PROMPT / END within 30 s is given up: nothing opens (the NPC's game ignored it). MAIN THREAD. */
static void TkP25Timeouts()
{
    if (!g_p25Pend.active || (unsigned long)(::GetTickCount() - g_p25Pend.tick) < (unsigned long)cooptalk::kTalkReqWaitMs) return;
    g_p25Pend.active = 0;
    ++g_p25WhyB[cooptalk::kTalkWhyNoAnswer];
    TkLogFail("[TALK] P25 req=" + TkU(g_p25Pend.reqId) + " npc uid=" + TkU(g_p25Pend.npcUid) + " got no answer from the NPC's game within 30 s"
              " - the click does nothing");
}

/* TEST-ONLY talkprompt lever state (armed on the main thread by the verb, run at the safe point). */
static volatile LONG g_tkLeverPending = 0;
static int g_tkLeverMode = 0, g_tkLeverIndex = -1;
static unsigned int g_tkLeverNpc = 0, g_tkLeverTarget = 0;
static std::string g_tkLeverSid;
static long long g_tkLeverArmed = 0, g_tkLeverRan = 0, g_tkLeverSkipped = 0;

static void TkLeverSkip(const std::string& why) { ++g_tkLeverSkipped; DebugLog("[TALK] talkprompt skipped: " + why); }

/* P26f3: why the lever may not start a line (kTkLnOk = it may). */
enum { kTkLnOk = 0, kTkLnUnreadable, kTkLnSpeaker, kTkLnInterject, kTkLnCond, kTkLnAction, kTkLnGive, kTkLnNoText, kTkLnNoReply, kTkLnReasons };
static const char* const kTkLnReasonName[kTkLnReasons] = { "ok", "unreadable", "speakerNotMe", "interjection", "conditions", "actions",
                                                           "givesItem", "noText", "noUnconditionalReply" };

/* P26f3: kTkLnOk = a line the lever may start: the NPC speaks it (speaker +0x31C T_ME), not an interjection (+0x318), no condition
   (+0x1F8), no action (+0x210) and no item hand-over (+0xB0 - nothing runs on this game when it is said), at least one text
   (+0x220), and at least one child (+0x1E8, a DialogChoiceList: count +8, array +0x10) with no condition - getPlayerReplies
   0x679070 lists the children whose conditions pass, so B's window has a reply. +0x1E1 is NOT tested: the constructor 0x67A5E0
   sets it to 1 and reads "monologue" into it only for a DIALOGUE item (0x12), so it was 1 on every line (T653 usable 0). No C++ object. */
static int TkLineRejectPod(const void* line)
{
    if (line == 0) return kTkLnUnreadable;
    __try
    {
        const char* l = (const char*)line;
        if (*(const int*)(l + 0x31C) != 0) return kTkLnSpeaker;
        if (*(const char*)(l + 0x318) != 0) return kTkLnInterject;
        if (*(const int*)(l + 0x1F8) != 0) return kTkLnCond;
        if (*(const int*)(l + 0x210) != 0) return kTkLnAction;
        if (*(const int*)(l + 0xB0) != 0) return kTkLnGive;
        if (*(const int*)(l + 0x220) <= 0) return kTkLnNoText;
        const char* ch = *(const char* const*)(l + 0x1E8);
        if (ch == 0) return kTkLnNoReply;
        const unsigned int n = *(const unsigned int*)(ch + 8);
        const char* const* arr = *(const char* const* const*)(ch + 0x10);
        if (n == 0 || n > 256 || arr == 0) return kTkLnNoReply;
        for (unsigned int k = 0; k < n; ++k)
            if (arr[k] != 0 && *(const int*)(arr[k] + 0x1F8) == 0) return kTkLnOk;
        return kTkLnNoReply;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return kTkLnUnreadable; }
}

/* P26 stage 4 (lever only): 1 = one of line's unconditional replies (+0x1E8 children, condition count +0x1F8 == 0) leads to an NPC
   line (speaker +0x31C T_ME, unconditional) that has an unconditional reply itself - B's answer then brings a second NPC line with
   replies. TkLineRejectPod's reads, one and two levels down. No C++ object. */
static int TkLineTwoStepPod(const void* line)
{
    if (line == 0) return 0;
    __try
    {
        const char* ch = *(const char* const*)((const char*)line + 0x1E8);
        if (ch == 0) return 0;
        const unsigned int n = *(const unsigned int*)(ch + 8);
        const char* const* arr = *(const char* const* const*)(ch + 0x10);
        if (n == 0 || n > 256 || arr == 0) return 0;
        for (unsigned int a = 0; a < n; ++a)
        {
            const char* r = arr[a];
            if (r == 0 || *(const int*)(r + 0x1F8) != 0) continue;
            const char* rc = *(const char* const*)(r + 0x1E8);
            if (rc == 0) continue;
            const unsigned int m = *(const unsigned int*)(rc + 8);
            const char* const* ra = *(const char* const* const*)(rc + 0x10);
            if (m == 0 || m > 256 || ra == 0) continue;
            for (unsigned int b = 0; b < m; ++b)
            {
                const char* nl = ra[b];
                if (nl == 0 || *(const int*)(nl + 0x31C) != 0 || *(const int*)(nl + 0x1F8) != 0) continue;
                const char* nc = *(const char* const*)(nl + 0x1E8);
                if (nc == 0) continue;
                const unsigned int q = *(const unsigned int*)(nc + 8);
                const char* const* na = *(const char* const* const*)(nc + 0x10);
                if (q == 0 || q > 256 || na == 0) continue;
                for (unsigned int k = 0; k < q; ++k) if (na[k] != 0 && *(const int*)(na[k] + 0x1F8) == 0) return 1;
            }
        }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int TkReplyPod(ReplyClickedFn fn, void* dlg, int index)
{
    __try { fn(dlg, index); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int TkEndEntryPod(TkEndDialogueFn fn, void* dlg)
{
    __try { fn(dlg, true); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static bool TkLineLess(const std::pair<std::string, void*>& a, const std::pair<std::string, void*>& b) { return a.first < b.first; }

/* P26f3: the lever's own reads of the engine's dialogue objects (Read: build/decomp_6a3e70 / 671c30 / 679630 / 683890 / 67a5e0). */
const size_t kDlgConvMap = 0x1A0;       /* Dialogue: std::map<int event, DialogChoiceList*> (getConversationList 0x671C30); head +0x8, size +0x10 (= +0x1B0, startPlayerConversation's "has conversations" test) */
const size_t kTkGdType = 0x50;          /* GameData: itemType (_createData 0x6ADFF0) */
const int kTkEvPlayerTalk = 1;          /* EventTriggerEnum: the player talks to this NPC (sendEvent 0x683F00: event 1 -> startPlayerConversation) */
const int kTkTypeDialogue = 0x12, kTkTypeDialogueLine = 0x13;   /* itemType DIALOGUE / DIALOGUE_LINE (DialogDataManager::initialise 0x67D340) */
const int kTkLeverNpcCap = 8, kTkLeverTriesPerNpc = 6, kTkLeverTryCap = 12;

/* the Dialogue's conversation list for event ev, READ ONLY (the engine's lookup 0x6A3E70 inserts a missing key): an MSVC map
   node is left +0, parent +8, right +0x10, key +0x18, value +0x20, isnil +0x29. *mapSize = entries (-1 unreadable). No C++ object. */
static void* TkConvListPod(const void* dlg, int ev, long long* mapSize)
{
    *mapSize = -1;
    if (dlg == 0) return 0;
    __try
    {
        const char* d = (const char*)dlg;
        *mapSize = (long long)*(const unsigned long long*)(d + kDlgConvMap + 0x10);
        const char* head = *(const char* const*)(d + kDlgConvMap + 0x8);
        if (head == 0) return 0;
        const char* node = *(const char* const*)(head + 0x8);
        const char* res = head;
        for (int guard = 0; node != 0 && *(node + 0x29) == 0 && guard < 64; ++guard)
        {
            if (*(const int*)(node + 0x18) < ev) node = *(const char* const*)(node + 0x10);
            else { res = node; node = *(const char* const*)node; }
        }
        if (res == head || *(const int*)(res + 0x18) != ev) return 0;
        return *(void* const*)(res + 0x20);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *mapSize = -1; return 0; }
}

/* a DialogChoiceList (lektor<DialogLineData*>: count +8, array +0x10): its size (0 = none / unreadable) and element k. No C++ object. */
static int TkChoiceCountPod(const void* list)
{
    if (list == 0) return 0;
    __try
    {
        const unsigned int n = *(const unsigned int*)((const char*)list + 8);
        if (n > 4096 || (n > 0 && *(void* const*)((const char*)list + 0x10) == 0)) return 0;
        return (int)n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static void* TkChoiceAtPod(const void* list, int k)
{
    __try { return (*(void* const* const*)((const char*)list + 0x10))[k]; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* a conversation-list entry: 1 readable. *type = its GameData's itemType, *mono = a DIALOGUE's "monologue" (+0x1E1; 0 for any
   other item - the constructor reads it only for 0x12), *conds = its conditions (+0x1F8), *kids = its children (+0x1E8). No C++ object. */
static int TkRootPod(const void* root, int* type, int* mono, int* conds, void** kids)
{
    *type = -1; *mono = 0; *conds = 0; *kids = 0;
    if (root == 0) return 0;
    __try
    {
        const char* r = (const char*)root;
        const char* gd = *(const char* const*)(r + kLineGameData);
        if (gd == 0) return 0;
        *type = *(const int*)(gd + kTkGdType);
        *mono = (*type == kTkTypeDialogue && *(const char*)(r + 0x1E1) != 0) ? 1 : 0;
        *conds = *(const int*)(r + 0x1F8);
        *kids = *(void* const*)(r + 0x1E8);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

struct TkLineTally { int seen, noSid, usable; int rej[kTkLnReasons]; };
static void TkTallyZero(TkLineTally* t) { t->seen = 0; t->noSid = 0; t->usable = 0; for (int i = 0; i < kTkLnReasons; ++i) t->rej[i] = 0; }

static std::string TkTallyStr(const TkLineTally& t)
{
    std::string s = "seen=" + TkI(t.seen) + " rejected[";
    for (int i = 1; i < kTkLnReasons; ++i) s += std::string(i > 1 ? " " : "") + kTkLnReasonName[i] + "=" + TkI(t.rej[i]);
    return s + "] noSid=" + TkI(t.noSid) + " usable=" + TkI(t.usable);
}

/* one line seen by the search: tallied, and kept (once) when the lever may start it. */
static void TkLeverTakeLine(void* line, TkLineTally* t, std::vector<std::pair<std::string, void*> >* out)
{
    ++t->seen;
    const int r = TkLineRejectPod(line);
    if (r != kTkLnOk) { ++t->rej[r]; return; }
    const std::string sid = TkLineSid(line);
    if (sid.empty()) { ++t->noSid; return; }
    for (size_t k = 0; k < out->size(); ++k) if ((*out)[k].second == line) return;   /* the same line under two conversations */
    ++t->usable;
    out->push_back(std::make_pair(sid, line));
}

/* (a) the NPC's OWN lines the way the engine picks for a player talk: event 1's conversation roots in list order (score order,
   DialogChoiceList::add 0x6AE1C0), unconditional roots first; a root that is a DIALOGUE (an empty node) gives its children,
   any other entry is a line itself; a monologue gives nothing. */
static void TkLeverOwnLines(void* evList, TkLineTally* t, int* roots, int* mono, int* condRoots, std::vector<std::pair<std::string, void*> >* out)
{
    const int n = TkChoiceCountPod(evList);
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < n; ++k)
        {
            void* root = TkChoiceAtPod(evList, k);
            int ty = -1, m = 0, c = 0; void* kids = 0;
            const int ok = TkRootPod(root, &ty, &m, &c, &kids);
            if (pass == 0) { ++*roots; if (ok && m) ++*mono; if (ok && c) ++*condRoots; }
            if (!ok) { if (pass == 0) { ++t->seen; ++t->rej[kTkLnUnreadable]; } continue; }
            if (m != 0 || (c != 0) != (pass == 1)) continue;
            if (ty != kTkTypeDialogue) { TkLeverTakeLine(root, t, out); continue; }
            const int nk = TkChoiceCountPod(kids);
            for (int j = 0; j < nk; ++j) TkLeverTakeLine(TkChoiceAtPod(kids, j), t, out);
        }
}

/* (a) the fallback: every DIALOGUE_LINE in the game data (the container DialogDataManager::initialise 0x67D340 walks), in
   string-id order. *listed = items listed, *implausible = unreadable GameData, *noObj = no DialogLineData (getData 0). */
static void TkLeverGlobalLines(DlgGetDataFn gfn, TkLineTally* t, int* listed, int* implausible, int* noObj, std::vector<std::pair<std::string, void*> >* out)
{
    lektor<GameData*> list;
    coop::GameWorldPtr()->gamedata.listRecordsOfType(list, (itemType)kTkTypeDialogueLine);
    *listed = (int)list.size();
    for (lektor<GameData*>::iterator it = list.begin(); it != list.end(); ++it)
    {
        ::GameData* gd = *it;
        if (gd == 0 || SayPlausiblePtr(gd) == 0) { ++*implausible; continue; }
        void* line = TkGetLinePod(gfn, gd);
        if (line == 0) { ++*noObj; continue; }
        TkLeverTakeLine(line, t, out);
    }
    std::sort(out->begin(), out->end(), TkLineLess);
}

struct TkLeverNpc { float d; ::Character* c; unsigned int u; };
static bool TkLeverNpcLess(const TkLeverNpc& a, const TkLeverNpc& b) { return a.d < b.d || (a.d == b.d && a.u < b.u); }

/* the lever, start: the engine's startPlayerConversation ENTRY (through its hook - the stage-2 intercept) for an NPC this game
   owns toward the other player's character. P26f3: `near` tries up to 8 non-animal NPCs nearest first; `auto` takes the NPC's own
   player-talk lines (else the global list); up to 6 tries per NPC, 12 in all, until a PROMPT goes out. */
static void TkLeverStart()
{
    if (!g_tkOn || kDialogueStartPlayerConvRva == 0) { TkLeverSkip("the P26 hooks are not installed (forwarding OFF)"); return; }
    ::Character* ref = 0; Ogre::Vector3 rpos;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap && ref == 0; ++i)
    {
        unsigned int u = 0; ::Character* c = 0; ::Faction* f = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || !net::IsUidMine(u)) continue;
        if (!CrimeFactionOf(c, &f) || f == 0 || !IsPlayerFaction(f)) continue;
        if (!SafeReadPosition(c, &rpos)) continue;
        ref = c;
    }
    ::Character* target = 0; unsigned int tuid = 0; float tdist = -1.0f;
    if (g_tkLeverTarget != 0)
    {
        target = FindSpawned(g_tkLeverTarget);
        if (target == 0 || SayPlausiblePtr(target) == 0 || net::IsUidMine(g_tkLeverTarget)) { TkLeverSkip("target uid " + TkU(g_tkLeverTarget) + " is not the other player's character here"); return; }
        tuid = g_tkLeverTarget;
    }
    else
    {
        if (ref == 0) { TkLeverSkip("no own player-faction character with a readable position to search near"); return; }
        float best = kTalkNearMax;
        for (int i = 0; i < cap; ++i)
        {
            unsigned int u = 0; ::Character* c = 0; ::Faction* f = 0; Ogre::Vector3 q;
            if (!MirrorSlot(i, &u, &c) || u == 0 || net::IsUidMine(u)) continue;
            if (!CrimeFactionOf(c, &f) || f == 0 || !IsPeerFaction(f)) continue;
            if (!SafeReadPosition(c, &q)) continue;
            const float dx = q.x - rpos.x, dz = q.z - rpos.z, d = sqrtf(dx * dx + dz * dz);
            if (d < best || (d == best && target != 0 && u < tuid)) { best = d; target = c; tuid = u; }
        }
        if (target == 0) { TkLeverSkip("no character of the other player within 3000 u of this game's first own player character"); return; }
        tdist = best;
    }
    Ogre::Vector3 tpos;
    if (!SafeReadPosition(target, &tpos)) { TkLeverSkip("the target's position is unreadable"); return; }
    /* P26f3 (b): the NPCs to try. An explicit uid is taken as given; `near` ranks every living, conscious non-player character this
       game owns with a Dialogue within 3000 u of the target, animals skipped (GroundAnimalAppPod == 1), nearest first, at most 8. */
    std::vector<TkLeverNpc> pool;
    int inRange = 0, animals = 0;
    if (g_tkLeverNpc != 0)
    {
        ::Character* one = FindSpawned(g_tkLeverNpc);
        if (one == 0 || SayPlausiblePtr(one) == 0 || !net::IsUidMine(g_tkLeverNpc)) { TkLeverSkip("NPC uid " + TkU(g_tkLeverNpc) + " is not a character this game owns"); return; }
        TkLeverNpc e; e.d = -1.0f; e.c = one; e.u = g_tkLeverNpc;
        pool.push_back(e);
    }
    else
    {
        if (coop::GameWorldPtr() == 0) { TkLeverSkip("no GameWorld to walk"); return; }
        const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
        if (all.size() > 20000) { TkLeverSkip("the character update list has an implausible size"); return; }
        for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
        {
            ::Character* c = *it; ::Faction* f = 0; Ogre::Vector3 q;
            if (c == target || SayPlausiblePtr(c) == 0 || !CrimeFactionOf(c, &f) || f == 0) continue;
            if (IsPlayerFaction(f) || IsPeerFaction(f)) continue;
            if (CrimeIsDead(c) != 0 || IsDownedCharacter(c)) continue;
            const unsigned int u = FindSpawnedUid(c);
            if (u == 0 || !net::IsUidMine(u)) continue;   /* the other game must have a copy, and this game must own it */
            if (TkDialogueOf(c) == 0 || !SafeReadPosition(c, &q)) continue;
            const float dx = q.x - tpos.x, dz = q.z - tpos.z, d = sqrtf(dx * dx + dz * dz);
            if (!(d < kTalkNearMax)) continue;
            ++inRange;
            if (GroundAnimalAppPod(c) == 1) { ++animals; continue; }   /* P26f3 (b): T653 picked 'Bonedog' */
            TkLeverNpc e; e.d = d; e.c = c; e.u = u;
            pool.push_back(e);
        }
        std::sort(pool.begin(), pool.end(), TkLeverNpcLess);
        if (pool.size() > (size_t)kTkLeverNpcCap) pool.resize((size_t)kTkLeverNpcCap);
        if (pool.empty())
        {
            TkLeverSkip("no living, conscious, non-animal non-player character this game owns with a Dialogue within 3000 u of the target ("
                        + TkI(inRange) + " in range, " + TkI(animals) + " animals skipped)");
            return;
        }
    }
    DebugLog("[TALK] talkprompt pool=" + TkI((long long)pool.size()) + (g_tkLeverNpc != 0 ? std::string(" (explicit uid)") : (" (in range " + TkI(inRange)
             + ", animals skipped " + TkI(animals) + ", cap " + TkI(kTkLeverNpcCap) + ")")) + " target uid=" + TkU(tuid) + " '" + TkNameStr(target) + "'");
    if (kDialogDataGetDataRva == 0 || coop::GameWorldPtr() == 0) { TkLeverSkip("DialogDataManager::getData is not in the address table"); return; }
    const DlgGetDataFn gfn = (DlgGetDataFn)(g_base + (uintptr_t)kDialogDataGetDataRva);
    void* sidLine = 0;
    if (!g_tkLeverSid.empty())
    {
        ::GameData* gd = coop::GameWorldPtr()->gamedata.getData(g_tkLeverSid);
        sidLine = (gd != 0) ? TkGetLinePod(gfn, gd) : 0;
        if (sidLine == 0) { TkLeverSkip("line " + g_tkLeverSid + " has no dialogue line object in this game"); return; }
    }
    const StartPlayerConvFn entry = (StartPlayerConvFn)(g_base + (uintptr_t)kDialogueStartPlayerConvRva);   /* the engine's entry (hooked) */
    const long long p0 = g_tkPromptSent;
    std::vector<std::pair<std::string, void*> > global;   /* (a) computed once, only when an NPC's own conversations give no usable line */
    int globalDone = 0;
    int tries = 0, npcsTried = 0, lastCands = 0, lastUsable = 0;
    ::Character* npc = pool[0].c; unsigned int nuid = pool[0].u; float ndist = pool[0].d;
    std::string src = "none";
    for (size_t ni = 0; ni < pool.size() && g_tkPromptSent == p0 && tries < kTkLeverTryCap; ++ni)
    {
        npc = pool[ni].c; nuid = pool[ni].u; ndist = pool[ni].d;
        ++npcsTried;
        void* dlg = TkDialogueOf(npc);
        long long mapSize = -1;
        void* evList = (dlg != 0) ? TkConvListPod(dlg, kTkEvPlayerTalk, &mapSize) : 0;
        std::vector<std::pair<std::string, void*> > own;
        TkLineTally ot; TkTallyZero(&ot);
        int roots = 0, mono = 0, condRoots = 0;
        if (evList != 0) TkLeverOwnLines(evList, &ot, &roots, &mono, &condRoots, &own);
        const std::vector<std::pair<std::string, void*> >* cands = &own;
        std::string next;
        if (dlg == 0) { cands = 0; next = "the NPC's Dialogue does not read as one - next NPC"; }
        else if (sidLine != 0) { src = "sid"; next = "explicit line " + g_tkLeverSid; }
        else if (mapSize <= 0) { cands = 0; next = "no conversations (Dialogue+0x1B0 = " + TkI(mapSize) + ": startPlayerConversation 0x683890 ends at once) - next NPC"; }
        else if (!own.empty()) { src = "own"; next = "trying own lines"; }
        else
        {
            if (!globalDone)
            {
                globalDone = 1;
                TkLineTally gt; TkTallyZero(&gt);
                int listed = 0, implausible = 0, noObj = 0;
                TkLeverGlobalLines(gfn, &gt, &listed, &implausible, &noObj, &global);
                DebugLog("[TALK] talkprompt global DIALOGUE_LINE(19) listed=" + TkI(listed) + " implausible=" + TkI(implausible)
                         + " noLineObject=" + TkI(noObj) + " " + TkTallyStr(gt));
            }
            cands = &global;
            src = "global";
            next = global.empty() ? std::string("no own usable line and no global one - next NPC") : ("no own usable line: global fallback (" + TkI((long long)global.size()) + " usable)");
        }
        char nb[200];
        std::sprintf(nb, "[TALK] talkprompt npc #%d uid=%u dist=%.1f convMap=%lld ev1List=%s roots=%d (monologue %d, conditional %d) own: ",
                     npcsTried, nuid, ndist, mapSize, evList != 0 ? "yes" : "none", roots, mono, condRoots);
        DebugLog(std::string(nb) + TkTallyStr(ot) + " -> " + next + " npc='" + TkNameStr(npc) + "'");
        if (cands == 0) continue;
        std::vector<std::pair<std::string, void*> > one;
        if (sidLine != 0) { one.push_back(std::make_pair(g_tkLeverSid, sidLine)); cands = &one; }
        std::vector<std::pair<std::string, void*> > ordered;   /* P26 stage 4: two-step lines first (a second NPC line to answer) */
        for (size_t k = 0; k < cands->size(); ++k) if (TkLineTwoStepPod((*cands)[k].second)) ordered.push_back((*cands)[k]);
        for (size_t k = 0; k < cands->size(); ++k) if (!TkLineTwoStepPod((*cands)[k].second)) ordered.push_back((*cands)[k]);
        cands = &ordered;
        lastCands = (int)cands->size();
        lastUsable = (sidLine != 0) ? 1 : (src == "own" ? ot.usable : (int)global.size());
        int t = 0;
        for (size_t k = 0; k < cands->size() && t < kTkLeverTriesPerNpc && tries < kTkLeverTryCap; ++k)
        {
            ++t; ++tries;
            const long long before = g_tkPromptSent;
            int started = 0;
            const int called = TkStartPod(entry, dlg, target, (*cands)[k].second, &started);
            TkRunEndLater();   /* a try that was not forwarded ends now, so the next try starts clean */
            DebugLog("[TALK] talkprompt try " + TkI(tries) + " npc uid=" + TkU(nuid) + " src=" + src + " line=" + (*cands)[k].first + " called="
                     + TkI(called) + " started=" + TkI(started) + " promptSent=" + TkI(g_tkPromptSent - before)
                     + " twoStep=" + TkI(TkLineTwoStepPod((*cands)[k].second)));   /* P26 stage 4 */
            if (g_tkPromptSent > before) break;
        }
    }
    ++g_tkLeverRan;
    char b[400];
    std::sprintf(b, "[TALK] talkprompt npc uid=%u dist=%.1f -> target uid=%u dist=%.1f candidates=%d (usable %d) tries=%d promptSent=%lld host=%d"
                 " src=%s npcsTried=%d pool=%d animalsSkipped=%d",
                 nuid, ndist, tuid, tdist, lastCands, lastUsable, tries, g_tkPromptSent - p0, net::SessionIsHost() ? 1 : 0,
                 src.c_str(), npcsTried, (int)pool.size(), animals);
    DebugLog(std::string(b) + " npc='" + TkNameStr(npc) + "' target='" + TkNameStr(target) + "'");
}

static int TkAnswerActPick(void* dlg, int act, int* pick, std::string* replies);   /* talkprompt answer act - defined with the TALKSIGHT scan helpers */

static void TkLeverDrain()
{
    if (::InterlockedExchange(&g_tkLeverPending, 0) == 0) return;
    if (g_tkLeverMode == cooptalk::kTalkLeverStart) { TkLeverStart(); return; }
    if (!g_tkMirror.active) { TkLeverSkip("no mirrored conversation is open on this game"); return; }
    if (!TkMirrorLive(g_tkMirror)) { TkLeverSkip("the mirrored NPC copy is gone or its Dialogue moved (TkApplyEnd's liveness check)"); return; }   /* P26s1 fold 1 M4 */
    if (g_tkLeverMode == cooptalk::kTalkLeverAnswerAct)
    {
        /* answer act <type>: the reply whose line (or a line up to 3 below it) carries the action, read from this game's copy of
           the mirrored line - then the same click as `answer <k>` */
        int k = -1;
        std::string replies;
        TkAnswerActPick(g_tkMirror.dlg, g_tkLeverIndex, &k, &replies);
        if (k < 0)
        {
            ++g_tkLeverSkipped;
            DebugLog("[TALK] talkprompt answer act " + TkI(g_tkLeverIndex) + ": no reply carries it - replies [" + replies + "]");
            return;
        }
        if (kDialogueReplyClickedRva == 0) { TkLeverSkip("replyClicked is not in the address table"); return; }
        const ReplyClickedFn fn = (ReplyClickedFn)(g_base + (uintptr_t)kDialogueReplyClickedRva);   /* the engine's entry (hooked) */
        const int called = TkReplyPod(fn, g_tkMirror.dlg, k);
        ++g_tkLeverRan;
        DebugLog("[TALK] talkprompt answer act " + TkI(g_tkLeverIndex) + ": reply " + TkI(k) + " carries it - replies [" + replies
                 + "] called=" + TkI(called) + " (through the replyClicked hook)");
        return;
    }
    if (g_tkLeverMode == cooptalk::kTalkLeverAnswer)
    {
        if (kDialogueReplyClickedRva == 0) { TkLeverSkip("replyClicked is not in the address table"); return; }
        const ReplyClickedFn fn = (ReplyClickedFn)(g_base + (uintptr_t)kDialogueReplyClickedRva);   /* the engine's entry (hooked) */
        const int called = TkReplyPod(fn, g_tkMirror.dlg, g_tkLeverIndex);
        ++g_tkLeverRan;
        DebugLog("[TALK] talkprompt answer " + TkI(g_tkLeverIndex) + " called=" + TkI(called) + " (through the replyClicked hook)");
        return;
    }
    if (kTkEndDialogueRva == 0) { TkLeverSkip("endDialogue is not in the address table"); return; }
    const TkEndDialogueFn fn = (TkEndDialogueFn)(g_base + (uintptr_t)kTkEndDialogueRva);   /* the engine's entry (hooked) */
    const int called = TkEndEntryPod(fn, g_tkMirror.dlg);
    ++g_tkLeverRan;
    DebugLog("[TALK] talkprompt close called=" + TkI(called) + " (through the endDialogue hook)");
}

/* P26s1 fold 1 L4: a PROMPT held too long (the world was blocked) is not shown - SHOW_FAILED, so the NPC's game ends its side.
   P26s4 fold 1 M2: a NEXT-line PROMPT of the mirrored conversation also turns the mirror off and ends the copy's conversation,
   as TkApplyNextPrompt's failure path does (mirror off first: the end is ours, not the player's CLOSED). MAIN THREAD, the world
   not blocked (TkSafePoint). */
static void TkPromptAged(const cooptalk::TalkMsg& m, unsigned int peer, LONG age)
{
    ++g_tkPromptAged;
    int closed = -1;
    if (g_tkMirror.active && cooplive::SamePlayer(g_tkMirror.peer, peer, coop::LinkPeerSlot()) && g_tkMirror.convId == m.convId)
    {
        const TkMirror mi = g_tkMirror;
        TkMirrorOff();
        closed = 0;
        if (TkMirrorLive(mi) && TkDlgBusy(mi.dlg))
        {
            g_tkApplyDlg = mi.dlg;
            closed = TkEndPod(mi.dlg);
            g_tkApplyDlg = 0;
        }
    }
    cooptalk::TalkMsg a;
    a.kind = cooptalk::kTalkAnswer; a.convId = m.convId; a.npcUid = m.npcUid; a.targetUid = m.targetUid;
    a.result = cooptalk::kTalkAnsShowFailed; a.index = -1;
    const int sent = net::SendTalk(a) ? 1 : 0;
    TkLogFail("[TALK] B PROMPT conv=" + TkU(m.convId) + " from peer " + TkU(peer) + " waited " + TkI(age)
              + " safe points (the world was blocked) - NOT shown: ANSWER SHOW_FAILED " + (sent ? "sent" : "FAILED (link down)")
              + (closed < 0 ? std::string() : " - the mirrored window closed (endDialogue=" + TkI(closed) + ")"));
}

/* P26s1 fold 1 M2 / M5: A - an open forwarded conversation whose NPC no longer owns its Dialogue ends (END ENGINE; nothing to call),
   an open mark with no conversation is dropped. B - the mirror must still be the copy's Dialogue, and once this game's window has
   shown it, the window must still show it: otherwise CLOSED (and the copy's conversation ends, which leaves another window alone -
   DialogueWindow::hide only hides its own Dialogue). MAIN THREAD, the world not blocked. */
static void TkSweep()
{
    std::vector<TkLater> gone;
    for (std::map<void*, TkConv>::iterator it = g_tkConvs.begin(); it != g_tkConvs.end(); ++it)
        if (!TkLiveOwner(it->first, it->second.me)) { TkLater l; l.dlg = it->first; l.me = it->second.me; gone.push_back(l); }
    for (size_t k = 0; k < gone.size(); ++k)
    {
        ++g_tkSweptConv;
        TkEndOwn(gone[k].dlg, gone[k].me, cooptalk::kTalkEndEngine, "its NPC is gone or its Dialogue moved (liveness sweep)", 0);
    }
    for (int i = 0; i < kTkMarks; ++i)
    {
        void* d = g_tkMarks[i].dlg;
        if (d != 0 && g_tkMarks[i].state == 2 && g_tkConvs.find(d) == g_tkConvs.end()) { ++g_tkSweptConv; TkMarkDrop(i); }
    }
    if (!g_tkMirror.active) return;
    if (!TkMirrorLive(g_tkMirror))
    {
        ++g_tkSweptMirror;
        TkMirrorClosed(g_tkMirror.dlg, "the NPC copy is gone or its Dialogue moved (liveness sweep)");
        return;
    }
    void* w = TkWindowDlg();
    if (w == g_tkMirror.dlg) { g_tkMirror.seen = 1; return; }
    if (w == (void*)1 || (w == 0 && !g_tkMirror.seen)) return;   /* no window row / not shown yet (a queued show) */
    const TkMirror mi = g_tkMirror;
    ++g_tkWinOther;
    TkMirrorClosed(mi.dlg, w != 0 ? "this game's window now shows another conversation" : "this game's window was closed");
    g_tkApplyDlg = mi.dlg;
    const int ended = TkEndPod(mi.dlg);
    g_tkApplyDlg = 0;
    TkLog("[TALK] B mirrored conversation conv=" + TkU(mi.convId) + " ended on the copy (endDialogue=" + TkI(ended) + ")");
}

/* P26s5 fold 5 (owner decision 229): a forwarded conversation waits for the player's ANSWER with no time limit, as in single player.
   The one bound left: a HELD END (fold 2 / 3) waiting for the other game's ACT_RESULT - a machine reply, answered at that game's
   next safe point - ends kTkHeldEndWaitMs after it was held: the engine's endDialogue (not again when the engine already ended it)
   and END TIMEOUT, nothing of the held part run (a late ACT_RESULT then finds no conversation). The clock is on the conversation
   (endClock), so dropping its hold at the 16-cap cannot lose it. MAIN THREAD, the world not blocked. */
static void TkTimeouts()
{
    const unsigned long now = ::GetTickCount();
    std::vector<TkLater> late;
    for (std::map<void*, TkConv>::iterator it = g_tkConvs.begin(); it != g_tkConvs.end(); ++it)
        if (it->second.endClock && (unsigned long)(now - it->second.endTick) >= (unsigned long)kTkHeldEndWaitMs)
        { TkLater l; l.dlg = it->first; l.me = it->second.me; late.push_back(l); }
    for (size_t k = 0; k < late.size(); ++k)
    {
        ++g_tkTimeouts;
        TkEndOwn(late[k].dlg, late[k].me, cooptalk::kTalkEndTimeout,
                 "no ACT_RESULT within 30 s for the held end (a lost answer from the other game) - nothing of the held part ran", 1);
    }
}

/* P26 stage 4: a link lost while the world was blocked - A ends the conversations whose marks it kept (the window stayed shut), B
   closes the mirrored window, now that the engine may be called. A Dialogue whose mark moved on (released, restarted) or a copy that
   is gone is left alone. MAIN THREAD, the world not blocked. */
static void TkLinkLaterRun()
{
    std::vector<TkLater> a;
    a.swap(g_tkLinkLaterA);
    for (size_t k = 0; k < a.size(); ++k)
    {
        const int i = TkMarkFind(a[k].dlg);
        if (i < 0 || (g_tkMarks[i].state != 1 && g_tkMarks[i].state != 2)) continue;
        TkEndOwn(a[k].dlg, a[k].me, 0, "the link to the other game was lost (ended once the world unblocked)", 1);
    }
    if (!g_tkLinkLaterB.active) return;
    const TkMirror mi = g_tkLinkLaterB;
    g_tkLinkLaterB.active = 0;
    if (g_tkMirror.active && g_tkMirror.dlg == mi.dlg) return;   /* a new mirror owns that Dialogue now */
    if (!TkMirrorLive(mi) || (TkWindowDlg() != mi.dlg && !TkDlgBusy(mi.dlg))) return;
    g_tkApplyDlg = mi.dlg;
    const int ended = TkEndPod(mi.dlg);
    g_tkApplyDlg = 0;
    TkLog("[TALK] B mirrored window conv=" + TkU(mi.convId) + " closed once the world unblocked: the link to the NPC's game was lost (endDialogue="
          + TkI(ended) + ")");
}

static void TkSafePoint()
{
    ::InterlockedIncrement(&g_tkGen);
    if (EngineWritesBlocked()) return;   /* held: the inbound queue and the lever wait for the world */
    TkLinkLaterRun();   /* P26 stage 4 */
    TkRunEndLater();
    for (int i = 0; i < kTkMarks; ++i)
    {
        void* d = g_tkMarks[i].dlg;
        if (d == 0) continue;
        const LONG st = g_tkMarks[i].state;
        if (st == 5)   /* P26s1 fold 1 H1: released off the main thread */
        {
            /* P26s1 fold 2 N6: counted once, as releasedOff (TkRelease's off-thread flip) - not again as released */
            TkCancelEndLater(d);
            TkEndOwn(d, g_tkMarks[i].me, cooptalk::kTalkEndRestarted, "the NPC started a conversation with a character that is not the other player's (off the main thread)", 0);
        }
        else if (st == 3)
        {
            std::map<void*, TkConv>::iterator e3 = g_tkConvs.find(d);   /* P26s5 fold 4: the engine had ended it already (fold 3) - its later end off the main thread is not this side's */
            if (e3 != g_tkConvs.end() && e3->second.engineEnded && ::InterlockedCompareExchange(&g_tkMarks[i].state, 2, 3) == 3) ++g_tkEndReEnd;
            else TkEndOwn(d, g_tkMarks[i].me, cooptalk::kTalkEndEngine, "the engine ended it off the main thread", 0);
        }
        else if (st == 1 && g_tkGen - g_tkMarks[i].gen > 2 && g_tkConvs.find(d) == g_tkConvs.end())
        {
            ++g_tkOrphans;
            TkEndOwn(d, g_tkMarks[i].me, 0, "an off-thread start whose ring entry was lost", 1);
        }
    }
    if (::InterlockedExchange(&g_tkMirrorClosedOff, 0) != 0 && g_tkMirror.active) TkMirrorClosed(g_tkMirror.dlg, "an off-thread end");
    TkSweep();   /* P26s1 fold 1 M2 / M5 */
    std::vector<TkIn> in;
    in.swap(g_tkInbound);
    for (size_t k = 0; k < in.size(); ++k)
    {
        const cooptalk::TalkMsg& m = in[k].m;
        if (m.kind == cooptalk::kTalkPrompt && g_tkGen - in[k].gen > kTkPromptMaxAge) { TkPromptAged(m, in[k].peer, g_tkGen - in[k].gen); continue; }   /* P26s1 fold 1 L4 */
        if (m.kind == cooptalk::kTalkPrompt) TkApplyPrompt(m, in[k].peer);
        else if (m.kind == cooptalk::kTalkAnswer) TkApplyAnswer(m, in[k].peer);
        else if (m.kind == cooptalk::kTalkEnd) TkApplyEnd(m, in[k].peer);
        else if (m.kind == cooptalk::kTalkAct) TkApplyAct(m, in[k].peer);               /* P26 stage 5: B */
        else if (m.kind == cooptalk::kTalkActResult) TkApplyActResult(m, in[k].peer);   /* P26 stage 5: A */
        else if (m.kind == cooptalk::kTalkRequest) TkApplyRequest(m, in[k].peer);       /* P25: A */
        else ++g_tkBad;
    }
    TkP25Drain();      /* P25: B - this game's clicks on the other game's NPCs */
    TkP25Timeouts();
    TkTimeouts();   /* P26 stage 4: after the arrived ANSWERs */
    TkLeverDrain();
}

static std::string TkCountsString()
{
    char b[1200];
    std::sprintf(b, " talkP26[on,promptSent,noReply,notOurs,notStarted,sendFailed,restarted,endSent,endedLater,orphans,stale,markFull,marked,"
                 "suppressOn,suppressOff,endingOff,offNotStarted]=%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP26A[ansReply,ansMapped,ansClosed,ansOther,ansUnknown,linkEnded]=%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP26B[promptRecv,shown,showFailed,busy,noChar,ansSent,closedSent,endRecv,endUnknown,actsSkipped,clicks,mismatch]="
                 "%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld talkP26bad=%lld"
                 " talkP26Hooks=setInDialog:%s,endDialogue:%s,replyClicked:%s,getData:%s talkprompt[armed,ran,skipped]=%lld,%lld,%lld",
                 g_tkOn, g_tkPromptSent, g_tkPromptNoReply, g_tkNotOurs, g_tkNotStarted, g_tkSendFailed, g_tkRestarted, g_tkEndSent,
                 g_tkEndedLater, g_tkOrphans, g_tkStale, (long long)g_tkMarkFull, (long long)g_tkMarked, (long long)g_tkSuppressOn,
                 (long long)g_tkSuppressOff, (long long)g_tkEndingOff, (long long)g_tkOffNotStarted,
                 g_tkAnsReply, g_tkAnsMapped, g_tkAnsClosed, g_tkAnsOther, g_tkAnsUnknown, g_tkLinkEnded,
                 g_tkPromptRecv, g_tkShown, g_tkShowFailed, g_tkBusy, g_tkNoChar, g_tkAnsSent, g_tkClosedSent, g_tkEndRecv, g_tkEndUnknown,
                 g_tkActsSkipped, g_tkClicks, g_tkMismatch, g_tkBad,
                 TalkHookState(g_setInDialogHook), TalkHookState(g_endDialogueHook), TalkHookState(g_replyClickedHook),
                 kDialogDataGetDataRva != 0 ? "row" : "noAddress", g_tkLeverArmed, g_tkLeverRan, g_tkLeverSkipped);
    char f[700];   /* P26s1 fold 1 */
    std::sprintf(f, " talkP26f1[released,releasedOff,relRefused,notCreator,notCreatorOff,guiSkipped,sweptConv,sweptMirror,windowOther,"
                 "endLaterSkipped,inDropped,promptAged,handMismatch,idTooLong]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP26f1Hooks=setConvReplyGUI:%s,setResponesGUI:%s,window:%s,doActions:%d",
                 g_tkReleased, (long long)g_tkReleasedOff, (long long)g_tkRelRefused, g_tkNotCreator, (long long)g_tkNotCreatorOff,
                 (long long)g_tkGuiSkipped, g_tkSweptConv, g_tkSweptMirror, g_tkWinOther, g_tkEndLaterSkipped, g_tkInDropped, g_tkPromptAged,
                 g_tkHandMismatch, g_tkIdTooLong, TalkHookState(g_convReplyGuiHook), TalkHookState(g_responsesGuiHook),
                 kDialogueWindowRva != 0 ? "row" : "noAddress", HireDoActionsHookState());
    char g[160];   /* P26s1 fold 2 */
    std::sprintf(g, " talkP26f2[ownWindowRefused,mirrorNewStart]=%lld,%lld", (long long)g_tkOwnWinRefused, (long long)g_tkMirrorNewStart);
    char s4[640];   /* P26 stage 4: ~220 literal bytes + 14 numbers of at most 20 */
    /* P26s5 fold 5: 'timeouts' counts only held ends that got no ACT_RESULT (decision 229: no answer deadline) */
    std::sprintf(s4, " talkP26s4A[applied,applyFault,nextPrompt,nextUnforwardable,unmapped,heldEndTimeouts,endAnswered,linesDeferred,actsDeferred,"
                 "itemLines,linkLater]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld talkP26s4B[nextRecv,nextShown,nextFailed]=%lld,%lld,%lld"
                 " talkP26s4Hooks=sayLine:%s",
                 g_tkApplied, g_tkApplyFault, g_tkNextPrompt, g_tkNextUnfwd, g_tkUnmapped, g_tkTimeouts, g_tkEndAnswered, g_tkLinesDeferred,
                 g_tkActsDeferred, g_tkItemLines, g_tkLinkLater, g_tkNextRecv, g_tkNextShown, g_tkNextFailed,
                 kDialogueSayLineRva != 0 ? "row" : "noAddress");
    char s5[520];   /* P26 stage 5: ~200 literal bytes + 15 numbers of at most 20 */
    std::sprintf(s5, " talkP26s5A[actSent,actSendFail,npcRan,npcFault,resApplied,resRefused]=%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP26s5B[actRecv,applied,refused,fault,hires,unsupported,linesRun]=%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                 g_tkActSent, g_tkActSendFail, g_tkActNpcRan, g_tkActNpcFault, g_tkActResOk, g_tkActResRefused,
                 g_tkActRecv, g_tkActApplied, g_tkActRefused, g_tkActFault, g_tkActHires, g_tkActUnsupported, g_tkActLinesRun);
    char s5f2[640];   /* P26s5 folds 1-3: ~200 literal bytes + 15 numbers of at most 20 */
    std::sprintf(s5f2, " talkP26s5f1[held,holdRefused,holdLost,holdDropped,carriedNone,refused28,endsTalk,notices]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP26s5f2[endHeld,endHeldRan,endHeldNotRun,endHeldRepeat,answerWhileHeld,endKept,endReEnd]=%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                 g_tkActHeld, g_tkActHoldRefused, g_tkActHoldLost, g_tkActHoldDropped, g_tkActCarriedNone, g_tkAct28Refused, g_tkActEndsTalk,
                 g_tkNotices, g_tkEndHeld, g_tkEndHeldRan, g_tkEndHeldNotRun, g_tkEndHeldRepeat, g_tkAnsWhileHeld, g_tkEndKept, g_tkEndReEnd);
    char car[300];   /* ~150 literal bytes + 8 numbers of at most 20 */
    std::sprintf(car, " talkCarriedA[sent,npcEngine,takeOrders,takeRefused,takeFault]=%lld,%lld,%lld,%lld,%lld"
                 " talkCarriedB[applied,talkerEngine,noCarry]=%lld,%lld,%lld",
                 g_tkCarriedSent, g_tkCarriedNpcEngine, g_tkTakeOrders, g_tkTakeRefused, g_tkTakeFault,
                 g_tkCarriedApplied, g_tkCarriedTalkerEngine, g_tkCarriedNoCarry);
    char s5f4[64];   /* P26s5 fold 4 */
    std::sprintf(s5f4, " talkP26s5f4[endReused]=%lld", (long long)g_tkEndReused);
    char s6[1200];   /* ~370 literal bytes + 25 numbers of at most 20 + two hook states of at most 12 */
    std::sprintf(s6, " talkP26s6[condPeer,condNotOurs,spkPeer,spkNotOurs,leadSeen,leadOrdered,leadNotOurs,leadNoTarget,leadFault,leadNoAddr,chainTgt]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP26s6f1[noUid,spkActsRefused,spkNoDlg,leaderNotOurs,leaderNoUid,flagFault,orderMix,viewLead]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP26s6money[answered,noPurse,set,cleared,actMoved,differs]=%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP26s6Hooks=cond:%s,spk:%s,on:%d",
                 (long long)g_s6CondPeer, (long long)g_s6CondNotOurs, (long long)g_s6SpkPeer, (long long)g_s6SpkNotOurs,
                 (long long)g_s6LeadSeen, (long long)g_s6LeadOrdered, (long long)g_s6LeadNotOurs, (long long)g_s6LeadNoTarget,
                 (long long)g_s6LeadFault, (long long)g_s6LeadNoAddr, (long long)g_s6ChainTgt,
                 (long long)g_s6NoUid, (long long)g_s6SpkActsRefused, (long long)g_s6SpkNoDlg, (long long)g_s6LeadLeaderNotOurs,
                 (long long)g_s6LeadLeaderNoUid, (long long)g_s6LeadFlagFault, (long long)g_s6OrderMix, (long long)g_s6ViewLead,
                 (long long)g_s6MoneyAnswered, (long long)g_s6MoneyNoPurse, g_tkPurseSet, g_tkPurseCleared,
                 g_tkPurseActMoved, (long long)g_s6MoneyDiffers,
                 TalkHookState(g_s6CondHook), TalkHookState(g_s6SpkHook), g_s6On);
    char p25[2100];   /* P25: ~520 literal bytes + 42 numbers of at most 20 + a hook state; fold 1 [p25f1-rep]: ~100 + 6 numbers */
    const long long* wa = g_p25WhyA;
    const long long* wb = g_p25WhyB;
    std::sprintf(p25, " talkP25B[localHeld,reqSent,prompted,relocal,ringFull,dup,gateLocal,gateUnread,endUnknown,tradeLocal,editorLocal,localFault,reqNoLine]"
                 "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP25A[reqRecv,started,firstLineHeld,firstLineRouted,firstLineDropped,gateSubst]=%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP25Gate[scoped,bypassed,refused]=%lld,%lld,%lld"
                 " talkP25notStartedA[busy,notOurs,far,gate,noCopy,link,down,engine,notForwarded,off,noAnswer,noLine]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP25notStartedB[busy,notOurs,far,gate,noCopy,link,down,engine,notForwarded,off,noAnswer,noLine]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkP25f1[mineMiss,mineMissConfirmed,stalePrompt,thirdOwner,startUnwound,nestedCleared]=%lld,%lld,%lld,%lld,%lld,%lld"   /* [p25f1-repf] */
                 " talkP25Hooks=view:%s,on:%d",
                 (long long)g_p25LocalHeld, g_p25ReqSent, g_p25Prompted, g_p25Relocal, (long long)g_p25RingFull, (long long)g_p25Dup,
                 (long long)g_p25GateLocal, (long long)g_p25GateUnread, g_p25EndUnknown, g_p25TradeLocal, g_p25EditorLocal, g_p25LocalFault, g_p25ReqNoLine,
                 g_p25ReqRecv, g_p25StartedA, g_p25FirstLineHeld, g_p25FirstLineRouted, g_p25FirstLineDropped, (long long)g_p25GateSubst,
                 (long long)g_p25GateScoped, (long long)g_p25GateBypassed, (long long)g_p25GateRefused,
                 wa[1], wa[2], wa[3], wa[4], wa[5], wa[6], wa[7], wa[8], wa[9], wa[10], wa[11], wa[12],
                 wb[1], wb[2], wb[3], wb[4], wb[5], wb[6], wb[7], wb[8], wb[9], wb[10], wb[11], wb[12],
                 (long long)g_p25MineMiss, g_p25MineMissConfirmed, g_p25StalePrompt, g_p25ThirdOwner,   /* [p25f1-repa] */
                 (long long)g_p25StartUnwound, (long long)g_p25NestedCleared,
                 TalkHookState(g_p25ViewHook), g_p25On);
    return std::string(b) + f + g + s4 + s5 + s5f2 + car + s5f4 + s6 + p25;
}

/* M7b slice 4 fold 1 (review 2026-10-02 F1): a stored TALK sender is the id it arrived with (the session link's raw id, or the
   notebook's 0x80000000 | slot); every comparison with it - the mirror, a conversation, an ACT hold, the closed mark, the queue and
   TalkForgetPeer's cleanup - goes through cooplive::SamePlayer, so a road switch mid-conversation is still the same player. */
void TalkNoteRecv(const cooptalk::TalkMsg& m, unsigned int fromPeer)
{
    /* P26s1 fold 1 L4: the queue waits while the world is blocked - a PROMPT beyond 64 queued, anything beyond 256, is dropped */
    if (g_tkInbound.size() >= kTkInboundCap || (m.kind == cooptalk::kTalkPrompt && g_tkInbound.size() >= kTkInboundPromptCap))
    {
        ++g_tkInDropped;
        TkLogFail("[TALK] " + std::string(cooptalk::TalkKindName(m.kind)) + " conv=" + TkU(m.convId) + " from peer " + TkU(fromPeer)
                  + " dropped: " + TkI((long long)g_tkInbound.size()) + " messages already wait for the safe point");
        return;
    }
    TkIn in;
    in.m = m;
    in.peer = fromPeer;
    in.gen = g_tkGen;
    g_tkInbound.push_back(in);
}

void TalkNoteBad() { ++g_tkBad; }

bool TalkSkipDoActions(void* dlg)
{
    if (!g_tkApplyStart || dlg == 0 || dlg != g_tkApplyDlg || SayOnMainThread() == 0) return false;
    ++g_tkActsSkipped;
    return true;
}

/* P26 stage 4: a line's actions {type, value} (DialogLineData +0x210 count, +0x218 array of pointers - hire.cpp ReadLinePod's reads,
   Read 67fad0:712-715) and its item hand-over count (+0xB0, the lever's read). The actions read, -1 unreadable. No C++ object. */
static int TkLineActsPod(const void* line, int* types, int* values, int cap, int* items)
{
    __try
    {
        const unsigned int n = *(const unsigned int*)((const char*)line + 0x210);
        const int* const* acts = *(const int* const* const*)((const char*)line + 0x218);
        *items = *(const int*)((const char*)line + 0xB0);
        if (n > 256) return -1;
        int k = 0;
        for (unsigned int i = 0; i < n && k < cap; ++i)
        {
            const int* a = acts[i];
            if (a == 0 || SayPlausiblePtr(a) == 0) continue;
            types[k] = a[0]; values[k] = a[1]; ++k;
        }
        return k;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

/* P26s6 S6-3 - hire.cpp detour_doActions, ANY THREAD (the thread the engine runs the line on), BEFORE the original: the character a
   TALK_TO_LEADER (2) on this line would send this game's NPC's squad leader to, when that is the other player's character; else 0.
   The target is the one _doActions acts on (67fad0:280-365: the conversation hand +0x158, resolved as T_TARGET through the engine's
   own getSpeaker; when that is the speaker itself the engine takes the original speaker +0x2A0 - never the other player's
   character for an NPC this game drives). Also counts (log-only, S6-6 deferred) a line with a target-side part on a Dialogue
   stages 1-5 do not forward (no mark) toward that character: it runs as vanilla here (chainTgt). No C++ object. */
void* TalkS6LeaderTarget(void* dlg, void* line)
{
    if (!g_s6On || dlg == 0 || line == 0 || kS6GetSpeakerRva == 0) return 0;
    int types[kS6ActsCap], values[kS6ActsCap], items = 0;
    const int n = TkLineActsPod(line, types, values, kS6ActsCap, &items);
    if (n < 0) return 0;
    int has2 = 0, hasTgt = (items > 0) ? 1 : 0, orders = 0;
    for (int i = 0; i < n; ++i)
    {
        const int ty = types[i];
        if (ty == kS6ActTalkToLeader) { has2 = 1; continue; }
        if (ty == 6 || ty == 16 || ty == 17 || ty == 29 || ty == 34 || ty == 40 || ty == 52) orders = 1;   /* P26s6 fold 1: orderMix */
        if (cooptalk::TalkActSide(ty) != cooptalk::kTalkSideNpc) hasTgt = 1;
    }
    if (!has2 && !hasTgt) return 0;
    const S6GetSpeakerFn gs = (orig_s6GetSpeaker != 0) ? orig_s6GetSpeaker : (S6GetSpeakerFn)(g_base + (uintptr_t)kS6GetSpeakerRva);
    void* t = 0;
    __try { t = gs(dlg, kS6TalkerTarget, line, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { t = 0; }
    if (t == 0) { if (has2) ::InterlockedIncrement64(&g_s6LeadNoTarget); return 0; }
    if (!TkPeerTarget(t)) return 0;
    const int own = TkS6NpcOurs(dlg);   /* P26s6 fold 1: -1 no uid, counted apart */
    if (own != 1) { if (has2) ::InterlockedIncrement64(own < 0 ? &g_s6NoUid : &g_s6LeadNotOurs); return 0; }
    if (hasTgt && TkMarkFind(dlg) < 0) ::InterlockedIncrement64(&g_s6ChainTgt);
    if (!has2) return 0;
    ::InterlockedIncrement64(&g_s6LeadSeen);
    if (orders) ::InterlockedIncrement64(&g_s6OrderMix);
    return t;
}

/* P26s6 S6-3 - AFTER the original, same thread: the order the engine gives a player target (0x6823C4-0x682411, Confirmed bytes).
   The engine calls without null checks; here a missing platoon or leader gives nothing (leadFault). No C++ object. */
void TalkS6LeaderOrder(void* dlg, void* target)
{
    if (dlg == 0 || target == 0) return;
    if (g_base == 0 || kS6GetPlatoonRva == 0 || kS6SquadLeaderRva == 0 || kS6AddOrderRva == 0 || kS6AddOrderArgRva == 0)
    { ::InterlockedIncrement64(&g_s6LeadNoAddr); return; }
    const S6GetPlatoonFn gp = (S6GetPlatoonFn)(g_base + (uintptr_t)kS6GetPlatoonRva);
    const S6SquadLeaderFn sl = (S6SquadLeaderFn)(g_base + (uintptr_t)kS6SquadLeaderRva);
    const S6AddOrderFn ao = (S6AddOrderFn)(g_base + (uintptr_t)kS6AddOrderRva);
    void* leader = 0;
    __try
    {
        void* const me = *(void* const*)((const char*)dlg + kDlgMe);
        void* const pl = (me != 0) ? gp(me) : 0;
        leader = (pl != 0) ? sl(pl) : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { leader = 0; }
    if (leader == 0) { ::InterlockedIncrement64(&g_s6LeadFault); return; }
    /* P26s6 fold 1: the leader who walks over must be this game's too - its arrival starts the conversation that stages 1-5 forward */
    const int lown = TkS6Owner(leader);
    if (lown != 1) { ::InterlockedIncrement64(lown < 0 ? &g_s6LeadLeaderNoUid : &g_s6LeadLeaderNotOurs); return; }
    int ordered = 0;
    __try
    {
        const unsigned long long a7 = *(const unsigned long long*)(g_base + (uintptr_t)kS6AddOrderArgRva);
        ao(leader, 0, kS6TaskSeekTalk, target, 0, 1, a7);
        ordered = 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { ordered = 0; }
    if (!ordered) { ::InterlockedIncrement64(&g_s6LeadFault); return; }
    ::InterlockedIncrement64(&g_s6LeadOrdered);   /* P26s6 fold 1: counted when the order went out */
    __try
    {
        void* const me2 = *(void* const*)((const char*)dlg + kDlgMe);   /* the engine re-reads it (0x6823FF) */
        char* const ai = *(char* const*)((const char*)me2 + kS6CharAi);
        char* const sub = *(char* const*)(ai + kS6AiSub);
        *(int*)(sub + kS6TalkFlag) = 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { ::InterlockedIncrement64(&g_s6LeadFlagFault); }
}

/* P25: 1 = the line has a part the P26 router does not run on this game as it stands - a target-side action (APPLY / HIRE / REFUSE /
   CARRIED, incl. 1 / 11 / 10 / 30 / 40), a givesItem or a factionRelationEffects entry; unreadable or over the caps = 1 (held: the router then
   fails closed). MAIN THREAD. */
static int TkP25LineHasTargetPart(const void* line)
{
    TkLineView v;
    if (TkLineViewPod(line, &v) < 0) return 1;
    if (v.nItems > 0 || v.nRels > 0) return 1;
    for (int i = 0; i < v.n; ++i) if (cooptalk::TalkActSide(v.types[i]) != cooptalk::kTalkSideNpc) return 1;
    return 0;
}

/* P26 stage 5 (hire.cpp detour_doActions, MAIN THREAD): THE ROUTER. A line said or answered in a conversation this game FORWARDS
   to the other player is split by cooptalk::TalkActSide (the per-action table in src/common/talkwire.h): its NPC-side part runs HERE
   through the engine's own _doActions on a view of the line (TkRunViewPod side 0); its TARGET-side part - the actions the other
   game applies, hires or refuses, the givesItem hand-over and the factionRelationEffects - goes to the addressed character's owner
   as MSG_TALK ACT. None of the target-side part runs here (on this game the target is only a copy): money and items are made on
   the target's owner alone. A line with no target-side part runs unchanged. Fails closed - the stage-4 hold: nothing of the line
   runs and the conversation ends on both games (TkEndOwn, END ENGINE) - when the line does not read, is over the wire's caps, or
   its ACT cannot be encoded. Never the first line of a start still inside startPlayerConversation (g_tkStartingDlg - not
   forwarded yet: it runs as vanilla, a LEFTOVER), never a start not forwarded, never this game's own player's conversation (no
   mark). true = handled (the original must NOT run).
   P26s5 fold 1:
     - the NPC-side part of a line with a target-side part is HELD (g_tkActHolds) until the other game answers APPLIED
       (TkApplyActResult runs it then); a refusal, or an ACT that did not go out, runs none of it and ends the conversation.
     - 10 / 30 / 40 act on the person the other player's character carries: they go in the ACT with that person's uid as this
       game sees it carried. The engine's case runs here only for 30 / 40 when this game owns the person; otherwise this game's
       share is the NPC's take and cage orders, given after APPLIED (TkTakeCarried). When the other player's character carries
       nobody here, the WHOLE line is refused: nothing runs, nothing is sent, the conversation ends (END ENGINE).
     - 1 / 11 (TalkActEndsTalk) are never sent and never run: after the NPC part the conversation ends (END ENGINE).
     - 28 is left out when the NPC's faction is a player faction (it would act on the other player's character: 67fad0:1279-1287).
     - what the player is told: TkRefusalNotice (decision 224 - no single-player equivalent for any reason, so the log only). */
bool TalkDeferTargetActs(void* dlg, void* line)
{
    /* P25: a REQUEST's own start - its first line runs inside startPlayerConversation, before the conversation is forwarded. With a
       target-side part (Seto's / agnu's instant hire, 'Esata talk to' relations) it is HELD here and routed by TkApplyRequest right
       after the PROMPT went out; without one it runs as vanilla (NPC-side only, on the NPC's own game). */
    if (g_p25StartDlg != 0 && dlg == g_p25StartDlg && dlg == g_tkStartingDlg && line != 0 && SayOnMainThread() != 0)
    {
        if (g_p25FirstLine == 0 && TkP25LineHasTargetPart(line)) { g_p25FirstLine = line; ++g_p25FirstLineHeld; return true; }
        return false;
    }
    if (!g_tkOn || dlg == 0 || line == 0 || SayOnMainThread() == 0 || dlg == g_tkStartingDlg || !TkMarkSuppresses(dlg)) return false;
    std::map<void*, TkConv>::iterator it = g_tkConvs.find(dlg);
    if (it == g_tkConvs.end()) return false;   /* P26s4 fold 1 M4: not a forwarded conversation - the engine runs the line */
    const TkConv c = it->second;
    TkLineView v;
    const int k = TkLineViewPod(line, &v);
    cooptalk::TalkMsg a;
    std::string why, npcActs, ends, whole;
    void* keep[cooptalk::kTalkMaxActs];
    unsigned int nKeep = 0;
    int endsType = 0, wholeType = 0, refused28 = 0, npcPlayer = -2, carriedSeen = 0, takeOrders = 0, carriedMine = 0;
    unsigned int carried = 0;
    if (k < 0) why = "the line's actions / items / relation effects do not read or are over the router's caps";   /* fails closed */
    else
    {
        for (int i = 0; i < v.n; ++i)
        {
            const int ty = v.types[i];
            if (cooptalk::TalkActCarried(ty))
            {   /* target-side: in the ACT; the NPC's share runs here as the engine's case or as the take orders */
                if (!carriedSeen) { carriedSeen = 1; carried = CarryingUidOf(c.targetUid); carriedMine = (carried != 0 && net::IsUidMine(carried)) ? 1 : 0; }
                if (!cooptalk::TalkCarriedMaySend(carried)) { if (wholeType == 0) wholeType = ty; whole += (whole.empty() ? "" : ",") + TkI(ty); continue; }
                a.actTypes.push_back(ty); a.actValues.push_back(v.values[i]);
                if (cooptalk::TalkCarriedNpcEngine(ty, carriedMine))
                {
                    if (nKeep < cooptalk::kTalkMaxActs) keep[nKeep++] = v.acts[i];
                    npcActs += (npcActs.empty() ? "" : ",") + TkI(ty) + ":" + TkI(v.values[i]);
                }
                else takeOrders = 1;
            }
            else if (cooptalk::TalkActEndsTalk(ty)) { if (endsType == 0) endsType = ty; ends += (ends.empty() ? "" : ",") + TkI(ty); }
            else if (cooptalk::TalkActSide(ty) == cooptalk::kTalkSideNpc)
            {
                if (ty == 28)
                {
                    if (npcPlayer == -2) npcPlayer = TkFactionIsPlayerPod(c.me);
                    if (npcPlayer != 0) { ++refused28; continue; }   /* a player faction (or unreadable): it would act on the target */
                }
                if (nKeep < cooptalk::kTalkMaxActs) keep[nKeep++] = v.acts[i];
                npcActs += (npcActs.empty() ? "" : ",") + TkI(ty) + ":" + TkI(v.values[i]);
            }
            else { a.actTypes.push_back(ty); a.actValues.push_back(v.values[i]); }
        }
        if (whole.empty() && ends.empty() && refused28 == 0 && a.actTypes.empty() && v.nItems == 0 && v.nRels == 0)
            return false;   /* no target-side part: the engine runs the line unchanged */
        for (int i = 0; i < v.nItems; ++i)
        {
            const std::string sid = TkGdSid(v.itemGd[i]);
            if (sid.empty() && why.empty()) why = "a givesItem entry's item has no readable string id";
            a.itemSids.push_back(sid); a.itemValues.push_back(v.itemVal[i]);
        }
        for (int i = 0; i < v.nRels; ++i)
        {
            const std::string sid = TkGdSid(v.relGd[i]);
            if (sid.empty() && why.empty()) why = "a factionRelationEffects entry's faction has no readable string id";
            a.relSids.push_back(sid); a.relValues.push_back(v.relVal[i]);
        }
    }
    const int hasTarget = (!a.actTypes.empty() || v.nItems > 0 || v.nRels > 0) ? 1 : 0;
    if (why.empty() && whole.empty() && hasTarget)
    {
        a.kind = cooptalk::kTalkAct; a.convId = c.convId; a.npcUid = c.npcUid; a.targetUid = c.targetUid;
        a.carriedUid = carriedSeen ? carried : 0u;
        a.seq = ++g_tkActSeq;
        if (a.seq == 0) a.seq = ++g_tkActSeq;
        a.lineSid = TkLineSid(line);
        std::vector<char> probe;
        if (a.lineSid.empty()) why = "the line has no string id the other game can look it up by";
        else if (!cooptalk::EncodeTalk(&probe, a)) why = "the ACT does not fit the wire (a string id over 128 bytes)";
    }
    if (!why.empty())
    {
        ++g_tkLinesDeferred;
        g_tkActsDeferred += (long long)a.actTypes.size();
        TkLog("[TALK] A conv=" + TkU(c.convId) + " line=" + TkLineSid(line) + " HELD - " + why
              + ": none of the line's actions run on this game and the conversation ends (the router fails closed)");
        TkEndOwn(dlg, c.me, cooptalk::kTalkEndEngine, "a line the router could not split ends the conversation", 1);
        return true;
    }
    if (!whole.empty())
    {
        ++g_tkActCarriedNone;
        TkLog("[TALK] A conv=" + TkU(c.convId) + " line=" + TkLineSid(line) + " REFUSED whole - carried-person action(s) [" + whole
              + "] but the other player's character carries nobody on this game (uid " + TkU(c.targetUid) + ")"
              + ": none of the line's actions run on either game and the conversation ends");
        TkRefusalNotice(cooptalk::kTalkActNoCarry, wholeType, "A: a carried-person line while the other player's character carries nobody here");
        TkEndOwn(dlg, c.me, cooptalk::kTalkEndEngine, "a carried-person line (10 / 30 / 40) with nobody carried here ends the conversation", 1);
        return true;
    }
    g_tkAct28Refused += refused28;
    const std::string note28 = refused28 ? std::string(" (28 left out: the NPC's faction is a player faction - it would act on the other player's character)") : std::string("");
    if (!hasTarget)
    {
        const int ran = TkRunViewPod(dlg, line, keep, nKeep, 0);
        if (ran == 1) ++g_tkActNpcRan; else ++g_tkActNpcFault;
        TkLog("[TALK] A conv=" + TkU(c.convId) + " line=" + TkLineSid(line) + " no target-side part to send; NPC-side [" + npcActs + "] ran here=" + TkI(ran) + note28
              + (ends.empty() ? std::string(" - the conversation goes on")
                              : "; TRADE / CHARACTER_EDITOR [" + ends + "]: the conversation ends; the other game opens that window itself (P25)"));
        if (!ends.empty() && g_tkConvs.find(dlg) != g_tkConvs.end())
        {
            ++g_tkActEndsTalk;   /* P25 step 5 (manager decision 2): the END carries the type - the other game opens its OWN window */
            TkEndOwn(dlg, c.me, cooptalk::kTalkEndEngine, "a TRADE / CHARACTER_EDITOR line ends the conversation (the other game opens that window)", 1, endsType);
        }
        return true;
    }
    const int sent = net::SendTalk(a) ? 1 : 0;
    if (!sent)
    {
        ++g_tkActSendFail;
        TkLog("[TALK] A ACT seq=" + TkU(a.seq) + " conv=" + TkU(c.convId) + " line=" + a.lineSid
              + " NOT sent (link down): none of the line's actions run on either game and the conversation ends");
        TkEndOwn(dlg, c.me, cooptalk::kTalkEndEngine, "the line's ACT did not go out (link down)", 1);
        return true;
    }
    ++g_tkActSent;
    {   /* the talker's game applies the line's money before showing the next line; this game may choose that line's money replies first */
        int before = -1, after = -1;
        if (TkPurseActSent(c.targetUid, a.actTypes, a.actValues, &before, &after))
            TkLog("[TALK] A ACT seq=" + TkU(a.seq) + " conv=" + TkU(c.convId) + " the talker's saved purse " + TkI(before) + " -> " + TkI(after)
                  + " (the line's TAKE_MONEY / GIVE_MONEY, for the next line's money replies; the ACT_RESULT's purse replaces it)");
    }
    TkActHold h;
    std::memset(&h, 0, sizeof(h));
    h.seq = a.seq; h.convId = c.convId; h.peer = c.peer; h.n = nKeep; h.dlg = dlg; h.me = c.me; h.line = line;
    h.takeOrders = takeOrders; h.npcUid = c.npcUid; h.carriedUid = a.carriedUid;
    for (unsigned int i = 0; i < nKeep; ++i) h.acts[i] = keep[i];
    h.endsTalk = ends.empty() ? 0 : 1; h.endsType = endsType;
    {   /* P26s5 fold 2: a held end moves to the conversation's newest held line - it runs after the last NPC-side part (fold 4: before the cap drop) */
        unsigned int parts = 0;
        const int hh = TkHoldLast(dlg, c.convId, &parts);
        if (hh >= 0 && g_tkActHolds[(size_t)hh].endHeld)
        {
            h.endHeld = 1; h.endReason = g_tkActHolds[(size_t)hh].endReason; h.endWhy = g_tkActHolds[(size_t)hh].endWhy;
            g_tkActHolds[(size_t)hh].endHeld = 0;
        }
    }
    std::string dropped;
    if (g_tkActHolds.size() >= kTkActHoldCap)
    {
        ++g_tkActHoldDropped;
        dropped = " (the oldest held line, seq=" + TkU(g_tkActHolds[0].seq) + ", dropped at the cap: its NPC-side part never runs)";
        if (g_tkActHolds[0].endHeld)   /* P26s5 fold 4: another conversation's held end - that conversation ends on its 30 s clock */
        {
            ++g_tkEndHeldNotRun;
            dropped += " (it held conv=" + TkU(g_tkActHolds[0].convId) + "'s end: that conversation ends on its 30 s clock, nothing run)";
        }
        g_tkActHolds.erase(g_tkActHolds.begin());
    }
    g_tkActHolds.push_back(h);
    ++g_tkActHeld;
    if (carriedSeen)
    {
        ++g_tkCarriedSent;
        if (!takeOrders) ++g_tkCarriedNpcEngine;
    }
    TkLog("[TALK] A ACT seq=" + TkU(a.seq) + " conv=" + TkU(c.convId) + " line=" + a.lineSid + " target=[" + TkActsStr(a.actTypes, a.actValues)
          + "] items=[" + TkPairsStr(a.itemSids, a.itemValues) + "] rels=[" + TkPairsStr(a.relSids, a.relValues) + "] sent to peer " + TkU(c.peer)
          + (carriedSeen ? " carried person uid=" + TkU(carried) + (carriedMine ? " (ours)" : " (not ours)")
                           + (takeOrders ? ": the NPC's take orders follow APPLIED" : ": the engine's case runs here after APPLIED") : std::string(""))
          + "; NPC-side [" + npcActs + "] HELD until the other game answers APPLIED" + note28
          + (ends.empty() ? std::string("") : "; TRADE / CHARACTER_EDITOR [" + ends + "]: the conversation ends after it") + dropped);
    return true;
}

void TalkForgetPeer(unsigned int peer)
{
    /* P26s1 fold 1 L7: only that peer's conversations, mirror and queued messages - kTalkAllPeers (OnPeerGone: the link itself is
       gone) also ends the not-forwarded ones still waiting */
    const int blocked = EngineWritesBlocked() ? 1 : 0;
    const int all = (peer == kTalkAllPeers) ? 1 : 0;
    for (int i = 0; i < kTkPurseSlots; ++i)   /* that player's reported purses are not used again */
        if (::InterlockedCompareExchange64(&g_tkPurse[i].v, 0, 0) != 0
            && (all || cooplive::SamePlayer(g_tkPurse[i].peer, peer, coop::LinkPeerSlot())))
        {
            ::InterlockedExchange64(&g_tkPurse[i].v, 0);
            ++g_tkPurseCleared;
        }
    if (g_p25Pend.active && (all || cooplive::SamePlayer(g_p25Pend.peer, peer, coop::LinkPeerSlot())))   /* P25: the waiting REQUEST opens nothing */
    {
        g_p25Pend.active = 0;
        ++g_p25WhyB[cooptalk::kTalkWhyLink];
        TkLogFail("[TALK] P25 req=" + TkU(g_p25Pend.reqId) + " npc uid=" + TkU(g_p25Pend.npcUid) + " dropped: the link to the NPC's game was lost - the click does nothing");
    }
    {   /* P26s5 fold 1: that peer's held NPC-side parts never run (its conversations end below) */
        std::vector<TkActHold> keepH;
        for (size_t k = 0; k < g_tkActHolds.size(); ++k)
        {
            if (!all && !cooplive::SamePlayer(g_tkActHolds[k].peer, peer, coop::LinkPeerSlot())) keepH.push_back(g_tkActHolds[k]);
            else if (g_tkActHolds[k].endHeld) ++g_tkEndHeldNotRun;   /* P26s5 fold 2: its conversation ends below, nothing run */
        }
        g_tkActHoldLost += (long long)(g_tkActHolds.size() - keepH.size());
        g_tkActHolds.swap(keepH);
    }
    std::vector<TkLater> own;
    for (std::map<void*, TkConv>::iterator it = g_tkConvs.begin(); it != g_tkConvs.end(); ++it)
    {
        if (!all && !cooplive::SamePlayer(it->second.peer, peer, coop::LinkPeerSlot())) continue;
        TkLater l; l.dlg = it->first; l.me = it->second.me;
        own.push_back(l);
    }
    if (all)
    {
        for (size_t k = 0; k < g_tkEndLater.size(); ++k)
        {
            const int i = TkMarkFind(g_tkEndLater[k].dlg);
            if (i >= 0 && g_tkMarks[i].state == 1) own.push_back(g_tkEndLater[k]);
        }
        g_tkEndLater.clear();
    }
    for (size_t k = 0; k < own.size(); ++k)
    {
        ++g_tkLinkEnded;
        std::map<void*, TkConv>::iterator lk = g_tkConvs.find(own[k].dlg);
        const int engEnded = (lk != g_tkConvs.end() && lk->second.engineEnded) ? 1 : 0;   /* P26s5 fold 4: nothing to call - TkEndOwn never calls the engine again */
        if (blocked && !engEnded)
        {   /* the mark stays (window shut) and TkLinkLaterRun ends it; the talk is gone from the table now, so its purse goes now */
            const unsigned int tuid = lk != g_tkConvs.end() ? lk->second.targetUid : 0;
            g_tkConvs.erase(own[k].dlg); g_tkLinkLaterA.push_back(own[k]); ++g_tkLinkLater;
            if (tuid != 0) TkPurseTalkEnded(tuid);
        }
        else TkEndOwn(own[k].dlg, own[k].me, 0, "the link to the other game was lost", 1);
    }
    if (g_tkMirror.active && (all || cooplive::SamePlayer(g_tkMirror.peer, peer, coop::LinkPeerSlot())))
    {
        const TkMirror mi = g_tkMirror;
        TkMirrorOff();
        int ended = 0;
        if (!blocked && FindSpawned(mi.npcUid) == (::Character*)mi.me && TkDialogueOf(mi.me) == mi.dlg)
        {
            g_tkApplyDlg = mi.dlg;
            ended = TkEndPod(mi.dlg);
            g_tkApplyDlg = 0;
        }
        if (blocked) { g_tkLinkLaterB = mi; g_tkLinkLaterB.active = 1; ++g_tkLinkLater; }   /* P26 stage 4: TkLinkLaterRun closes the window */
        ++g_tkLinkEnded;
        TkLog("[TALK] B mirrored window conv=" + TkU(mi.convId) + " closed: the link to the NPC's game was lost (endDialogue=" + TkI(ended) + ")");
    }
    if (all || cooplive::SamePlayer(g_tkClosedPeer, peer, coop::LinkPeerSlot())) { g_tkClosedPeer = 0; g_tkClosedConv = 0; }   /* P26s4 fold 1 M1: a new link numbers its conversations afresh */
    if (all) g_tkInbound.clear();
    else
    {
        std::vector<TkIn> keep;
        for (size_t k = 0; k < g_tkInbound.size(); ++k) if (!cooplive::SamePlayer(g_tkInbound[k].peer, peer, coop::LinkPeerSlot())) keep.push_back(g_tkInbound[k]);
        g_tkInbound.swap(keep);
    }
}

std::string TalkPromptArm(const std::string& arg)
{
    int mode = 0, idx = -1;
    unsigned int n = 0, t = 0;
    std::string sid;
    if (!cooptalk::TalkPromptParse(arg, &mode, &n, &t, &sid, &idx))
        return "error talkprompt: usage talkprompt [npcUid|near] [targetUid|near] [lineSid|auto] | talkprompt answer <0..9> | talkprompt answer act <type 0..255> | talkprompt close";
    g_tkLeverMode = mode; g_tkLeverIndex = idx; g_tkLeverNpc = n; g_tkLeverTarget = t; g_tkLeverSid = sid;
    ::InterlockedExchange(&g_tkLeverPending, 1);
    ++g_tkLeverArmed;
    DebugLog("[TALK] talkprompt " + std::string(mode == cooptalk::kTalkLeverStart ? "start" : (mode == cooptalk::kTalkLeverAnswer ? "answer" : (mode == cooptalk::kTalkLeverAnswerAct ? "answer act" : "close")))
             + " npc=" + TkU(n) + " target=" + TkU(t) + " (0 = near) line=" + (sid.empty() ? std::string("auto") : sid) + " index=" + TkI(idx)
             + " ARMED - runs at the next safe point");
    return "ok talkprompt armed";
}

// ===========================================================================================
// TALKSIGHT LEVER (P26 stage 6, owner decision 233 (a), 2026-09-30) - a TEST-ONLY dev verb (class b), not player behaviour.
// `talksight <npcUid | nearest <dialogueName>> <targetUid> [seconds] [walkover]`: the NPC "notices" the target through the SAME
// entry the engine's sight check sends its event through - Dialogue::sendEvent 0x683F00, event 3 (8587b0:212-266, Read) - once per
// K2 safe point (MAIN THREAD, worker paused), until a conversation starts (with `walkover`: until one whose said line carries
// TALK_TO_LEADER; other starts are logged and the lever goes on) or `seconds` (default 120) run out. sendEvent's own repeat timer
// (Dialogue +0xD0 per event: read first, stamped 1.0 only after a start, 683f00) is never touched; the line choice with its
// conditions and rolls (_chooseDialog 0x679630), the walk-over order and the arrival are the engine's own. NOT re-run: the sight
// trigger's gates BEFORE its call (not an ally, not the NPC's faction, not a slave, not committing a crime, NPC squad not busy -
// 8587b0:212-266); the dialogue's own conditions (IS_ALLY, IS_SLAVE, IN_COMBAT, ...) still decide. Refused, in words: no world; the
// NPC not this game's (npcMine); the target on another floor (the engine's own same-floor test, vt+0x60 getFloor, 675ec0:180-195)
// or out of talk range (3D distance^2 >= 67600, same lines).
// `talkcarriers <x> <z> <radius>` (read-only): living non-player characters within radius of (x,z) whose event-3 conversation list
// holds a walk-over dialogue (a line under it carries TALK_TO_LEADER) - owner, name, uid, dialogue, player-gated, filler line.
// `playerteleport near <uid | nearest <dialogueName>> <units>`: the point <units> from that (outdoor) character on the line toward
// this game's first own player character; command_channel.cpp moves the player there with PlayerTeleportAbs (handoff.cpp).
// ===========================================================================================
const int kTsEvSight = 3;                          /* the sight check's event (8587b0: ...(npc, seen, 3)) */
const float kTsTalkRangeSq = 67600.0f;             /* 675ec0:180-195: TARGET_IN_TALKING_RANGE - 260 units, 3D */
const size_t kTsCharClassPtr = 0x1A0, kTsClass = 0xDC;   /* Character +0x1A0 -> +0xDC: the class a line's "target is type" compares (6784a0:428-442) */
const size_t kTsLineForType = 0x1E4, kTsLineKids = 0x1E8, kTsLineCondN = 0x1F8, kTsLineConds = 0x200, kTsLineParent = 0x230;   /* DialogLineData (67a5e0) */
const size_t kTsLineActN = 0x210, kTsLineActs = 0x218;   /* actions lektor: count, array of {type, value}* (TkLineActsPod's reads) */
const int kTsScanNodeCap = 512, kTsScanDepthCap = 12, kTsDlgCap = 8, kTsCarrierLogCap = 40;

static int g_tsActive = 0;                         /* MAIN THREAD */
static unsigned int g_tsNpcUid = 0, g_tsTargetUid = 0;
static int g_tsSeconds = 0, g_tsUntilWalk = 0;
static DWORD g_tsStartMs = 0;
static long long g_tsRunTries = 0, g_tsRunStarted = 0, g_tsRunWalk = 0;
static std::string g_tsLastLine = "-";
static long long g_tsArmed = 0, g_tsRefused = 0, g_tsTries = 0, g_tsStarted = 0, g_tsWalkOver = 0, g_tsTimedOut = 0, g_tsEnded = 0,
                 g_tsFaulted = 0, g_tsCarrierRuns = 0, g_tsNearOk = 0, g_tsNearRefused = 0;
/* P26 T718 fixture waits (TEST-ONLY levers; events over timers). T714 moved the player to a town and asked 9 s later - the town's
   people had not loaded yet. Now `playerteleport near` and `talksight` WAIT when the character is not loaded yet (the uid is not
   here / no carrier within 3000 units) instead of refusing at once: ONE pending wait per lever, and TalkWaitTick (MAIN THREAD,
   CommandChannelTick - the same thread and place the commands run) re-runs the same check every 500 ms until it passes, the check
   refuses for another reason, the deadline passes (near 60 s; talksight the command's own seconds) or the world goes away. A
   re-check is quiet: the refusal counters count once, at the end. Every other refusal is immediate, as before. */
const unsigned long kTwPeriodMs = 500, kTwNearMs = 60000;
static int g_tsQuiet = 0;           /* MAIN THREAD: 1 while TalkWaitTick re-runs the talksight check - no counter, no REFUSED line */
static int g_tsNotLoaded = 0;       /* the last check failed ONLY because the character is not loaded yet */
static std::string g_tsLastWhy;     /* the last talksight check's refusal reason */
struct TsWait { int on; std::string arg; DWORD startMs; DWORD waitMs; DWORD lastTryMs; };
static TsWait g_twNear, g_twSight, g_twName;  /* MAIN THREAD; g_twName: P25 T729 talktest nearestname */
static long long g_twNearArmed = 0, g_twNearDone = 0, g_twNearTimedOut = 0, g_twSightArmed = 0, g_twSightDone = 0, g_twSightTimedOut = 0;

/* the character's floor through its own vt+0x60 (RootObjectBase::getFloor - the call 675ec0:182-183 makes). 1 read. No C++ object. */
typedef int (*TsFloorFn)(const void* self);
static int TsFloorPod(const void* ch, int* floor)
{
    *floor = -1;
    if (ch == 0) return 0;
    __try
    {
        const char* vt = *(const char* const*)ch;
        if (vt == 0) return 0;
        const TsFloorFn fn = *(const TsFloorFn*)(vt + 0x60);
        if (fn == 0) return 0;
        *floor = fn(ch);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *floor = -1; return 0; }
}

/* the character's class (CharacterTypeEnum; 9 = OT_ADVENTURER) as _checkLine compares it. 1 read. No C++ object. */
static int TsClassPod(const void* ch, int* cls)
{
    *cls = -1;
    if (ch == 0) return 0;
    __try
    {
        const char* p = *(const char* const*)((const char*)ch + kTsCharClassPtr);
        if (p == 0) return 0;
        *cls = *(const int*)(p + kTsClass);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *cls = -1; return 0; }
}

/* one DialogLineData: *type = its GameData's itemType, *forType = "target is type", *kids = its children, *conds = its condition
   count, *gate = 1 when a condition wants the target to be a player (DC_IS_PLAYER, who T_TARGET / T_TARGET_IF_PLAYER, equal, value
   != 0), *walk = 1 when an action is TALK_TO_LEADER (2). 1 readable. No C++ object. */
static int TsLinePod(const void* line, int* type, int* forType, int* gate, int* walk, int* conds, void** kids)
{
    *type = -1; *forType = 0; *gate = 0; *walk = 0; *conds = 0; *kids = 0;
    if (line == 0) return 0;
    __try
    {
        const char* L = (const char*)line;
        const char* gd = *(const char* const*)(L + kLineGameData);
        if (gd == 0) return 0;
        *type = *(const int*)(gd + kTkGdType);
        *forType = *(const int*)(L + kTsLineForType);
        *kids = *(void* const*)(L + kTsLineKids);
        const unsigned int nc = *(const unsigned int*)(L + kTsLineCondN);
        const unsigned int na = *(const unsigned int*)(L + kTsLineActN);
        if (nc > 256 || na > 256) return 0;
        *conds = (int)nc;
        const int* const* cs = *(const int* const* const*)(L + kTsLineConds);
        for (unsigned int i = 0; i < nc; ++i)
        {
            const int* k = cs[i];   /* DialogCondition {key, compareBy, who, value} */
            if (k != 0 && k[0] == kS6DcIsPlayer && k[1] == 0 && (k[2] == kS6TalkerTarget || k[2] == kS6TalkerTargetIfPlayer) && k[3] != 0) *gate = 1;
        }
        const int* const* as = *(const int* const* const*)(L + kTsLineActs);
        for (unsigned int i = 0; i < na; ++i)
        {
            const int* a = as[i];   /* DialogAction {key, value} */
            if (a != 0 && a[0] == kS6ActTalkToLeader) *walk = 1;
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* the GameData name of a line (+0x1D8 -> name) into out[cap], cut to cap-1. No C++ object here (C2712). */
static void TsGdNamePod(const void* line, char* out, int cap)
{
    out[0] = '-'; out[1] = 0;
    if (line == 0) return;
    __try
    {
        const ::GameData* gd = *(::GameData* const*)((const char*)line + kLineGameData);
        if (gd == 0) return;
        const std::string& s = gd->name;
        size_t n = s.size();
        if (n > (size_t)(cap - 1)) n = (size_t)(cap - 1);
        if (n > 0) std::memcpy(out, s.data(), n);
        out[n] = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = '?'; out[1] = 0; }
}

static std::string TsName(const void* line)
{
    char b[96];
    TsGdNamePod(line, b, (int)sizeof(b));
    return std::string(b);
}

struct TsScan { int nodes; int walkLines; int gated; int types[4]; int nTypes; void* firstWalk; };
struct TsDlgInfo { void* root; int rootConds; int rootGate; int filler; TsScan scan; };

/* 1 = the subtree under line (inclusive) holds a TALK_TO_LEADER line. gatedAbove: a player gate on the path above it. */
static int TsScanLine(void* line, int depth, int gatedAbove, TsScan* s)
{
    if (line == 0 || depth > kTsScanDepthCap || s->nodes >= kTsScanNodeCap) return 0;
    ++s->nodes;
    int ty = -1, ft = 0, g = 0, w = 0, nc = 0; void* kids = 0;
    if (!TsLinePod(line, &ty, &ft, &g, &w, &nc, &kids)) return 0;
    const int gated = (gatedAbove || g) ? 1 : 0;
    int has = 0;
    if (w)
    {
        has = 1;
        ++s->walkLines;
        if (gated) ++s->gated;
        if (s->firstWalk == 0) s->firstWalk = line;
        int known = 0;
        for (int i = 0; i < s->nTypes; ++i) if (s->types[i] == ft) known = 1;
        if (!known && s->nTypes < 4) s->types[s->nTypes++] = ft;
    }
    const int nk = TkChoiceCountPod(kids);
    for (int j = 0; j < nk; ++j) if (TsScanLine(TkChoiceAtPod(kids, j), depth + 1, gated, s)) has = 1;
    return has;
}

/* one conversation root (a DIALOGUE node, or a bare line): 1 = it walks over. filler = a direct child line whose subtree does not. */
static int TsScanRoot(void* root, TsDlgInfo* d)
{
    std::memset(d, 0, sizeof(*d));
    d->root = root;
    int ty = -1, ft = 0, g = 0, w = 0, nc = 0; void* kids = 0;
    if (!TsLinePod(root, &ty, &ft, &g, &w, &nc, &kids)) return 0;
    d->rootConds = nc;
    d->rootGate = g;
    if (ty != kTkTypeDialogue) return TsScanLine(root, 0, 0, &d->scan);
    int any = 0;
    const int nk = TkChoiceCountPod(kids);
    for (int j = 0; j < nk; ++j)
    {
        if (TsScanLine(TkChoiceAtPod(kids, j), 1, g, &d->scan)) any = 1;
        else d->filler = 1;
    }
    return any;
}

/* the walk-over dialogues in the character's event-3 conversation list, at most cap; key "" = any, else TalkNameKey of the
   dialogue's name or of its string id must equal key. */
static int TsWalkOvers(::Character* ch, const std::string& key, TsDlgInfo* out, int cap)
{
    void* dlg = TalkDialogueOf(ch);
    if (dlg == 0) return 0;
    long long mapSize = 0;
    void* list = TkConvListPod(dlg, kTsEvSight, &mapSize);
    const int n = TkChoiceCountPod(list);
    int k = 0;
    for (int i = 0; i < n && k < cap; ++i)
    {
        void* root = TkChoiceAtPod(list, i);
        if (!TsScanRoot(root, &out[k])) continue;
        if (!key.empty() && coopsay::TalkNameKey(TsName(root)) != key && coopsay::TalkNameKey(TkLineSid(root)) != key) continue;
        ++k;
    }
    return k;
}

/* a living, conscious non-player character with a Dialogue (talktest near's filters). */
static int TsCandidate(::Character* ch)
{
    ::Faction* f = 0;
    if (ch == 0 || SayPlausiblePtr(ch) == 0 || !CrimeFactionOf(ch, &f) || f == 0) return 0;
    if (IsPlayerFaction(f) || IsPeerFaction(f)) return 0;
    if (CrimeIsDead(ch) != 0 || IsDownedCharacter(ch)) return 0;
    return TalkDialogueOf(ch) != 0 ? 1 : 0;
}

/* the nearest candidate within kTalkNearMax (2D) of (x,z) carrying the walk-over dialogue `key`; 0 none. */
static ::Character* TsNearestCarrier(const std::string& key, float x, float z, float* dist)
{
    *dist = -1.0f;
    if (coop::GameWorldPtr() == 0) return 0;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return 0;
    ::Character* best = 0; float bd = kTalkNearMax;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* ch = *it; Ogre::Vector3 q;
        if (!TsCandidate(ch) || !SafeReadPosition(ch, &q)) continue;
        const float dx = q.x - x, dz = q.z - z, d = sqrtf(dx * dx + dz * dz);
        if (d >= bd) continue;
        TsDlgInfo one[1];
        if (TsWalkOvers(ch, key, one, 1) == 0) continue;
        bd = d; best = ch;
    }
    if (best != 0) *dist = bd;
    return best;
}

static const char* TsOwnerName(int own) { return own == 1 ? "mine" : (own == 0 ? "copy" : "noUid"); }

static std::string TsTypes(const TsScan& s)
{
    std::string r;
    for (int i = 0; i < s.nTypes; ++i) { char b[16]; std::sprintf(b, "%s%d", i ? "," : "", s.types[i]); r += b; }
    return r.empty() ? std::string("-") : r;
}

/* one dialogue's summary: name, sid, gates, walk lines, their "target is type" values, filler. */
static std::string TsDlgString(const TsDlgInfo& d)
{
    char b[200];
    std::sprintf(b, " rootConds=%d rootPlayerGate=%d walkLines=%d playerGated=%d walkTypes=", d.rootConds, d.rootGate, d.scan.walkLines, d.scan.gated);
    return " dialogue='" + TsName(d.root) + "' key=" + coopsay::TalkNameKey(TsName(d.root)) + " sid=" + TkLineSid(d.root) + b + TsTypes(d.scan)
           + " filler=" + (d.filler ? "1" : "0");
}

/* talkcarriers ev/act and talkprompt answer act: one line's children (kTsLineKids) and whether its own actions (kTsLineActN /
   kTsLineActs) hold type act. 1 readable. No C++ object. */
static int TsLineActKidsPod(const void* line, int act, int* has, void** kids)
{
    *has = 0; *kids = 0;
    if (line == 0) return 0;
    __try
    {
        const char* L = (const char*)line;
        *kids = *(void* const*)(L + kTsLineKids);
        const unsigned int na = *(const unsigned int*)(L + kTsLineActN);
        if (na > 256) return 0;
        const int* const* as = *(const int* const* const*)(L + kTsLineActs);
        for (unsigned int i = 0; i < na; ++i)
        {
            const int* a = as[i];   /* DialogAction {key, value} */
            if (a != 0 && a[0] == act) *has = 1;
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *has = 0; *kids = 0; return 0; }
}

/* talkcarriers ev/act: the first line in the subtree under line (inclusive) whose actions hold type act, 0 none - within the
   walk-over scan's depth and node caps (*nodes counts the lines read). */
static void* TsScanAct(void* line, int depth, int act, int* nodes)
{
    if (line == 0 || depth > kTsScanDepthCap || *nodes >= kTsScanNodeCap) return 0;
    ++*nodes;
    int has = 0; void* kids = 0;
    if (!TsLineActKidsPod(line, act, &has, &kids)) return 0;
    if (has) return line;
    const int nk = TkChoiceCountPod(kids);
    for (int j = 0; j < nk; ++j)
    {
        void* hit = TsScanAct(TkChoiceAtPod(kids, j), depth + 1, act, nodes);
        if (hit != 0) return hit;
    }
    return 0;
}

/* talkprompt answer act: 1 = line, or a line up to 3 levels under it, carries action act (TkLineViewPod's action read). */
static int TkLineCarriesAct(void* line, int depth, int act, int* nodes)
{
    if (line == 0 || depth > 3 || *nodes >= kTsScanNodeCap) return 0;
    ++*nodes;
    TkLineView v;
    if (TkLineViewPod(line, &v) > 0)
        for (int i = 0; i < v.n; ++i) if (v.types[i] == act) return 1;
    int has = 0; void* kids = 0;
    if (!TsLineActKidsPod(line, act, &has, &kids)) return 0;
    const int nk = TkChoiceCountPod(kids);
    for (int j = 0; j < nk; ++j) if (TkLineCarriesAct(TkChoiceAtPod(kids, j), depth + 1, act, nodes)) return 1;
    return 0;
}

/* talkprompt answer act (the talker's game, MAIN THREAD, K2 safe point): the mirrored conversation's current line (kDlgCurLine),
   its child lines matched to the reply ids (kDlgReplyBegin) by string id; *pick = the first reply k whose line carries act
   (TkLineCarriesAct), -1 none. *replies = "k=id:1" (carries), ":0" (does not) or ":noLine" (no child line has that id), per reply. */
static int TkAnswerActPick(void* dlg, int act, int* pick, std::string* replies)
{
    *pick = -1;
    replies->clear();
    std::vector<std::string> ids;
    TkReadStrVec(dlg, kDlgReplyBegin, kDlgReplyEnd, &ids, (int)cooptalk::kTalkMaxReplies, (int)cooptalk::kTalkMaxId);
    void* cur = SayReadPtr(dlg, kDlgCurLine);
    int has = 0; void* kids = 0;
    if (cur == 0 || !TsLineActKidsPod(cur, act, &has, &kids)) kids = 0;
    const int nk = TkChoiceCountPod(kids);
    for (size_t k = 0; k < ids.size(); ++k)
    {
        void* line = 0;
        for (int j = 0; j < nk && line == 0; ++j)
        {
            void* c = TkChoiceAtPod(kids, j);
            if (c != 0 && TkLineSid(c) == ids[k]) line = c;
        }
        int nodes = 0;
        const int carries = (line != 0) ? TkLineCarriesAct(line, 0, act, &nodes) : 0;
        *replies += std::string(k ? "," : "") + TkI((long long)k) + "=" + ids[k] + (line == 0 ? ":noLine" : (carries ? ":1" : ":0"));
        if (carries && *pick < 0) *pick = (int)k;
    }
    if (cur == 0) *replies += " (no current line)";
    return *pick >= 0 ? 1 : 0;
}

// P26s6 ARRIVAL WATCH (T714, log-only). MAIN THREAD (K2 safe point). Armed by TalkSightTick when a walk-over line starts.
static int g_awActive = 0, g_awSeconds = 0;
static unsigned int g_awSpeakerUid = 0, g_awLeaderUid = 0, g_awTargetUid = 0;
static DWORD g_awStartMs = 0, g_awNextMs = 0;
static LONG g_awSeqSeen = 0;
static float g_awLastX = 0, g_awLastZ = 0;
static int g_awHaveLast = 0;
static long long g_awWatches = 0, g_awLines = 0, g_awArrivals = 0, g_awEndedEarly = 0;
const size_t kArrDlgParked = 0xC8;    /* Dialogue: the parked playerInterruptionDialog line (67fad0:367-369; 683f00 event 1 reads it) */

/* ANY THREAD. SEH-guarded reads, no C++ object. 1 read, 0 unreadable. */
static int TkArrIntPod(const void* p, size_t off, int* out)
{
    if (p == 0) return 0;
    __try { *out = *(const int*)((const char*)p + off); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int TkArrBytePod(const void* p, size_t off, int* out)
{
    if (p == 0) return 0;
    __try { *out = (int)*(const unsigned char*)((const char*)p + off); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* the engine's own lookups (the S6-3 rows): ActivePlatoon::getSquadLeader(Character::getSquad(c)). 0 = none / unreadable. */
static ::Character* TkArrLeaderOf(::Character* c)
{
    if (c == 0 || g_base == 0 || kS6GetPlatoonRva == 0 || kS6SquadLeaderRva == 0) return 0;
    const S6GetPlatoonFn gp = (S6GetPlatoonFn)(g_base + (uintptr_t)kS6GetPlatoonRva);
    const S6SquadLeaderFn sl = (S6SquadLeaderFn)(g_base + (uintptr_t)kS6SquadLeaderRva);
    void* lead = 0;
    __try { void* const pl = gp(c); lead = (pl != 0) ? sl(pl) : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { lead = 0; }
    return (::Character*)lead;
}
/* the talk flag the arrival turns into its event: Character +0x650 (AI) -> +0x20 (OrdersReceiver) -> +0x264. -1 unreadable. */
static int TkArrFlagOf(::Character* c)
{
    const void* ai = SayReadPtr(c, kS6CharAi);
    const void* orders = (ai != 0) ? SayReadPtr(ai, kS6AiSub) : 0;
    int v = -1;
    if (orders == 0 || !TkArrIntPod(orders, kS6TalkFlag, &v)) return -1;
    return v;
}
/* " parked=%d ended=%d convEv=%d line=%d" for one Dialogue (-1 = unreadable) */
static std::string TkArrDlgState(void* dlg)
{
    if (dlg == 0) return " dlg=none";
    int ended = -1, ev = -1;
    TkArrBytePod(dlg, kDlgEnded, &ended);
    TkArrIntPod(dlg, kDlgConvEvent, &ev);
    const void* parked = SayReadPtr(dlg, kArrDlgParked);
    const void* line = SayReadPtr(dlg, kDlgCurLine);
    char b[120];
    std::sprintf(b, " parked=%d ended=%d convEv=%d line=%d", parked != 0 ? 1 : 0, ended, ev, line != 0 ? 1 : 0);
    return std::string(b);
}
static std::string TkArrEvCounts()
{
    char b[200];
    std::sprintf(b, " tpEv[1,9,0x11,0x3C]=%lld/%lld,%lld/%lld,%lld/%lld,%lld/%lld",
                 (long long)g_arrCalls[0], (long long)g_arrTrue[0], (long long)g_arrCalls[1], (long long)g_arrTrue[1],
                 (long long)g_arrCalls[2], (long long)g_arrTrue[2], (long long)g_arrCalls[3], (long long)g_arrTrue[3]);
    return std::string(b);
}
static std::string TkArrCounts()
{
    char b[160];
    std::sprintf(b, " talkArrive[watches,lines,arrivals,endedEarly]=%lld,%lld,%lld,%lld", g_awWatches, g_awLines, g_awArrivals, g_awEndedEarly);
    return std::string(b) + TkArrEvCounts();
}
static void TkArrWatchEnd(const std::string& why, int early)
{
    if (!g_awActive) return;
    g_awActive = 0;
    if (early) ++g_awEndedEarly;
    char b[260];
    std::sprintf(b, "[TALKARRIVE] END leader=%u speaker=%u target=%u after %.1f s: arrivals=%lld lines=%lld - ",
                 g_awLeaderUid, g_awSpeakerUid, g_awTargetUid, (double)(DWORD)(::GetTickCount() - g_awStartMs) / 1000.0,
                 g_awArrivals, g_awLines);
    DebugLog(std::string(b) + why + TkArrCounts());
}
/* MAIN THREAD. Called by TalkSightTick when a walk-over line has just started on npc toward target. */
static void TkArrWatchArm(::Character* npc, ::Character* target)
{
    if (g_awActive) TkArrWatchEnd("replaced by a new walk-over", 1);
    ::Character* lead = TkArrLeaderOf(npc);
    g_awSpeakerUid = FindSpawnedUid(npc);
    g_awLeaderUid = (lead != 0) ? FindSpawnedUid(lead) : 0;
    g_awTargetUid = FindSpawnedUid(target);
    g_awSeconds = (g_tsSeconds > 0) ? g_tsSeconds : 120;
    g_awStartMs = ::GetTickCount();
    g_awNextMs = g_awStartMs;          /* the first tick logs at once */
    g_awSeqSeen = g_arrSeq;
    g_awHaveLast = 0;
    g_awActive = 1;
    ++g_awWatches;
    char b[300];
    std::sprintf(b, "[TALKARRIVE] ARMED speaker=%u leader=%u (%s) target=%u seconds=%d flagSpeaker=%d flagLeader=%d",
                 g_awSpeakerUid, g_awLeaderUid, lead == 0 ? "no leader read" : (lead == npc ? "the speaker is the leader" : "ANOTHER character"),
                 g_awTargetUid, g_awSeconds, TkArrFlagOf(npc), lead != 0 ? TkArrFlagOf(lead) : -1);
    DebugLog(std::string(b) + " speakerDlg" + TkArrDlgState(TalkDialogueOf(npc)));
}
/* MAIN THREAD, worker paused (TalkTestDrain). Every 2 s, and at once when an event-1 sendEvent landed. */
static void TkArrWatchTick()
{
    if (!g_awActive) return;
    if (EngineWritesBlocked()) { TkArrWatchEnd("the world is loading or tearing down", 1); return; }
    const DWORD now = ::GetTickCount();
    if ((DWORD)(now - g_awStartMs) >= (DWORD)g_awSeconds * 1000u) { TkArrWatchEnd("watch time over", 0); return; }
    const LONG seq = g_arrSeq;
    const int fresh = (seq != g_awSeqSeen) ? 1 : 0;
    if (!fresh && (LONG)(now - g_awNextMs) < 0) return;
    g_awNextMs = now + 2000;
    ::Character* speaker = FindSpawned(g_awSpeakerUid);
    ::Character* lead = (g_awLeaderUid != 0) ? FindSpawned(g_awLeaderUid) : 0;
    ::Character* target = FindSpawned(g_awTargetUid);
    if (lead == 0 || target == 0 || SayPlausiblePtr(lead) == 0 || SayPlausiblePtr(target) == 0)
    { TkArrWatchEnd("the leader or the target is no longer here", 1); return; }
    void* const leadDlg = TalkDialogueOf(lead);
    void* const spkDlg = (speaker != 0 && SayPlausiblePtr(speaker) != 0) ? TalkDialogueOf(speaker) : 0;
    Ogre::Vector3 lp, tp;
    float d = -1.0f, moved = -1.0f;
    const int lok = SafeReadPosition(lead, &lp) ? 1 : 0;
    float horiz = -1.0f, dy = 0.0f;   /* P26lvl: the arrival test's two parts (horizontal < radius + 21; height gap < 10, or < 35 on one floor) */
    const int tgtOk = (lok && SafeReadPosition(target, &tp)) ? 1 : 0;
    if (tgtOk)
    {
        d = sqrtf((lp.x - tp.x) * (lp.x - tp.x) + (lp.y - tp.y) * (lp.y - tp.y) + (lp.z - tp.z) * (lp.z - tp.z));
        horiz = sqrtf((lp.x - tp.x) * (lp.x - tp.x) + (lp.z - tp.z) * (lp.z - tp.z));
        dy = lp.y - tp.y;
    }
    if (lok && g_awHaveLast) moved = sqrtf((lp.x - g_awLastX) * (lp.x - g_awLastX) + (lp.z - g_awLastZ) * (lp.z - g_awLastZ));
    if (lok) { g_awLastX = lp.x; g_awLastZ = lp.z; g_awHaveLast = 1; }
    std::string arr;
    if (fresh)
    {
        g_awSeqSeen = seq;
        ArrSlot s; s.dlg = 0; s.who = 0; s.retRva = 0; s.result = -1; s.tid = 0;
        if (::InterlockedCompareExchange(&g_arrBusy, 1, 0) == 0) { s = g_arrLast; ::InterlockedExchange(&g_arrBusy, 0); }
        const int onLeader = (s.dlg != 0 && s.dlg == leadDlg) ? 1 : 0;
        const int toTarget = (s.who != 0 && s.who == (void*)target) ? 1 : 0;
        if (onLeader && toTarget) ++g_awArrivals;
        char a[260];
        std::sprintf(a, " | %s ev1 dlg=%s who=%s result=%d retRva=0x%llX tid=%lu(%s) calls-since=%ld",
                     (onLeader && toTarget) ? "ARRIVAL" : "OTHER", onLeader ? "leader" : (s.dlg != 0 && s.dlg == spkDlg ? "speaker" : "other"),
                     toTarget ? "target" : "other", s.result, s.retRva, s.tid, EngineThreadNameOf(s.tid), (long)seq);
        arr = a;
    }
    ++g_awLines;
    char b[360];
    std::sprintf(b, "[TALKARRIVE] t=%.1f leader=%u%s at %.0f,%.0f dist=%.1f horiz=%.1f dy=%.1f targetAt=%.0f,%.0f moved=%.1f flag=%d |",
                 (double)(DWORD)(now - g_awStartMs) / 1000.0, g_awLeaderUid, g_awLeaderUid == g_awSpeakerUid ? "(speaker)" : "",
                 lok ? lp.x : 0.0f, lok ? lp.z : 0.0f, d, horiz, dy, tgtOk ? tp.x : 0.0f, tgtOk ? tp.z : 0.0f, moved, TkArrFlagOf(lead));
    std::string line = std::string(b) + " leaderDlg" + TkArrDlgState(leadDlg);
    if (spkDlg != 0 && spkDlg != leadDlg) line += " | speakerDlg" + TkArrDlgState(spkDlg);
    DebugLog(line + TkArrEvCounts() + arr);
}

std::string TalkSightCounts()
{
    char b[320];
    std::sprintf(b, " talksight[armed,refused,tries,started,walkOver,timedOut,ended,faulted]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " talkcarriers=%lld teleportNear[ok,refused]=%lld,%lld",
                 g_tsArmed, g_tsRefused, g_tsTries, g_tsStarted, g_tsWalkOver, g_tsTimedOut, g_tsEnded, g_tsFaulted,
                 g_tsCarrierRuns, g_tsNearOk, g_tsNearRefused);
    char wb[160];   /* T718 fixture waits */
    std::sprintf(wb, " wait[nearArmed,nearDone,nearTimedOut,sightArmed,sightDone,sightTimedOut]=%lld,%lld,%lld,%lld,%lld,%lld",
                 g_twNearArmed, g_twNearDone, g_twNearTimedOut, g_twSightArmed, g_twSightDone, g_twSightTimedOut);
    return std::string(b) + TkArrCounts() + std::string(wb);   /* P26s6 arrival probe; T718 waits last */
}

static std::string TsRefuse(const std::string& why)
{
    g_tsLastWhy = why;
    if (g_tsQuiet) return "error talksight: " + why;   /* T718: a waiting re-check - TalkWaitTick decides, logs and counts */
    ++g_tsRefused;
    DebugLog("[TALKSIGHT] REFUSED: " + why);
    return "error talksight: " + why;
}

/* T718: the check failed only because the NPC is not loaded yet - TalkSightArm arms a wait (no counter, no REFUSED line). */
static std::string TsNotLoaded(const std::string& why)
{
    g_tsNotLoaded = 1;
    g_tsLastWhy = why;
    return "error talksight: " + why;
}

static void TsEnd(const std::string& why)
{
    if (!g_tsActive) return;
    g_tsActive = 0;
    ++g_tsEnded;
    char b[360];
    std::sprintf(b, "[TALKSIGHT] END npc=%u target=%u after %.1f s: tries=%lld started=%lld walkOver=%lld line=",
                 g_tsNpcUid, g_tsTargetUid, (double)(DWORD)(::GetTickCount() - g_tsStartMs) / 1000.0, g_tsRunTries, g_tsRunStarted, g_tsRunWalk);
    DebugLog(std::string(b) + g_tsLastLine + " - " + why);
    DebugLog("[TALKSIGHT] REPORT" + TalkSightCounts());
}

// MAIN THREAD (the command channel through TalkSightArm, and TalkWaitTick's re-checks).
static std::string TsSightCheck(const std::string& arg)
{
    unsigned int nu = 0, tu = 0; std::string key; int secs = 0, walk = 0;
    if (!coopsay::TalkSightParse(arg, &nu, &key, &tu, &secs, &walk))
        return "error talksight: usage talksight <npcUid | nearest <dialogueName>> <targetUid> [seconds 1..3600, default 120] [walkover]";
    if (!g_tsQuiet) ++g_tsArmed;   /* T718: a command, not a re-check */
    if (coop::GameWorldPtr() == 0 || EngineWritesBlocked()) return TsRefuse("no world (none loaded, or it is loading / tearing down)");
    if (kDialogueSendEventRva == 0 || g_base == 0) return TsRefuse("Dialogue::sendEvent is not in the address table");
    char nb[200];
    ::Character* target = FindSpawned(tu);
    if (target == 0 || SayPlausiblePtr(target) == 0) { std::sprintf(nb, "the target uid %u is not here", tu); return TsRefuse(nb); }
    Ogre::Vector3 tp;
    if (!SafeReadPosition(target, &tp)) return TsRefuse("the target's position is unreadable");
    ::Character* npc = 0; float nd = -1.0f;
    if (nu != 0)
    {
        npc = FindSpawned(nu);
        if (npc == 0 || SayPlausiblePtr(npc) == 0) { std::sprintf(nb, "the NPC uid %u is not here", nu); return TsNotLoaded(nb); }
    }
    else
    {
        npc = TsNearestCarrier(key, tp.x, tp.z, &nd);
        if (npc == 0) return TsNotLoaded("no living non-player character within 3000 units of the target carries a walk-over dialogue keyed '" + key + "'");
    }
    const unsigned int npcUid = FindSpawnedUid(npc);
    const int own = TkS6Owner(npc);
    if (own != 1)
    {
        std::sprintf(nb, "the NPC (uid %u) is %s", npcUid, own == 0 ? "the other game's character (a copy here), not this game's own (npcMine)"
                                                                   : "not replicated (no uid), not this game's own (npcMine)");
        return TsRefuse(nb);
    }
    void* dlg = TalkDialogueOf(npc);
    if (dlg == 0) return TsRefuse("the NPC's Dialogue does not read as one");
    Ogre::Vector3 np;
    if (!SafeReadPosition(npc, &np)) return TsRefuse("the NPC's position is unreadable");
    int nf = -1, tf = -1;
    if (!TsFloorPod(npc, &nf) || !TsFloorPod(target, &tf)) return TsRefuse("a floor (indoor / outdoor state) is unreadable");
    const float dx = np.x - tp.x, dy = np.y - tp.y, dz = np.z - tp.z, d2 = dx * dx + dy * dy + dz * dz;
    if (nf != tf)
    {
        std::sprintf(nb, "the target is in a different indoor / outdoor state: NPC floor %d, target floor %d (%s)", nf, tf,
                     tf == 0 ? "the target is outdoors, the NPC indoors" : (nf == 0 ? "the target is indoors, the NPC outdoors" : "different floors"));
        return TsRefuse(nb);
    }
    if (d2 >= kTsTalkRangeSq)
    {
        std::sprintf(nb, "the target is out of talk range: %.0f units apart (the engine's range is under 260)", sqrtf(d2));
        return TsRefuse(nb);
    }
    if (g_tsActive) TsEnd("replaced by a new talksight");
    TsDlgInfo dl[kTsDlgCap];
    const int ndl = TsWalkOvers(npc, key, dl, kTsDlgCap);
    int cls = -1;
    TsClassPod(target, &cls);
    unsigned int tu2 = 0;
    const int tk = TalkKindOf(target, &tu2);
    char nn[48], tn[48];
    TalkName(npc, nn);
    TalkName(target, tn);
    g_tsNpcUid = npcUid; g_tsTargetUid = tu; g_tsSeconds = secs; g_tsUntilWalk = walk;
    g_tsStartMs = ::GetTickCount();
    g_tsRunTries = 0; g_tsRunStarted = 0; g_tsRunWalk = 0; g_tsLastLine = "-";
    g_tsActive = 1;
    char b[520];
    std::sprintf(b, "[TALKSIGHT] ARMED npc uid=%u npcMine '%s' (%s) -> target uid=%u %s '%s' class=%d dist=%.1f floor=%d seconds=%d until=%s"
                 " walkOverDialogues=%d host=%d",
                 npcUid, nn, nu != 0 ? "by uid" : "the nearest carrier to the target", tu, coopsay::TalkTargetName(tk), tn, cls,
                 sqrtf(d2), nf, secs, walk ? "a walk-over start" : "any start", ndl, net::SessionIsHost() ? 1 : 0);
    DebugLog(std::string(b));
    for (int i = 0; i < ndl; ++i) DebugLog("[TALKSIGHT]   npc carries" + TsDlgString(dl[i]));
    {   /* P26s6 arrival probe, decision 236 fixture: a TALK_TO_LEADER order goes to this NPC's squad leader (0x6823C4) */
        ::Character* lead = TkArrLeaderOf(npc);
        char lb[200];
        std::sprintf(lb, "[TALKSIGHT]   npc squad leader uid=%u npcIsLeader=%d leaderOwner=%s flagNpc=%d",
                     lead != 0 ? FindSpawnedUid(lead) : 0u, (lead != 0 && lead == npc) ? 1 : 0,
                     lead != 0 ? TsOwnerName(TkS6Owner(lead)) : "none", TkArrFlagOf(npc));
        DebugLog(std::string(lb));
    }
    return "ok talksight armed";
}

// MAIN THREAD, worker paused (TalkTestDrain at the K2 safe point): one try per safe point.
static void TalkSightTick()
{
    if (!g_tsActive) return;
    if (EngineWritesBlocked()) { TsEnd("the world is loading or tearing down"); return; }
    if ((DWORD)(::GetTickCount() - g_tsStartMs) >= (DWORD)g_tsSeconds * 1000u) { ++g_tsTimedOut; TsEnd("timed out"); return; }
    ::Character* npc = FindSpawned(g_tsNpcUid);
    ::Character* target = FindSpawned(g_tsTargetUid);
    if (npc == 0 || target == 0 || SayPlausiblePtr(npc) == 0 || SayPlausiblePtr(target) == 0) { TsEnd("the NPC or the target is no longer here"); return; }
    void* dlg = TalkDialogueOf(npc);
    if (dlg == 0) { TsEnd("the NPC's Dialogue no longer reads as one"); return; }
    const SendEventFn fn = (SendEventFn)(g_base + (uintptr_t)kDialogueSendEventRva);   /* the engine's entry (hooked: counted like any call) */
    const long long ls0 = (long long)g_s6LeadSeen, lo0 = (long long)g_s6LeadOrdered;
    int result = 0;
    g_talkSightCall = 1;
    const int called = TalkSendPod(fn, dlg, target, kTsEvSight, &result);
    g_talkSightCall = 0;
    g_talkCurEvent = -1;   /* a fault inside would have skipped detour_sendEvent's restore */
    ++g_tsTries; ++g_tsRunTries;
    if (!called) { ++g_tsFaulted; TsEnd("sendEvent FAULTED"); return; }
    if (!result) return;
    ++g_tsStarted; ++g_tsRunStarted;
    const void* line = SayReadPtr(dlg, kDlgCurLine);
    int ty = -1, ft = 0, g = 0, w = 0, nc = 0; void* kids = 0;
    TsLinePod(line, &ty, &ft, &g, &w, &nc, &kids);
    const void* root = line;
    for (int i = 0; i < 16 && root != 0; ++i)
    {
        const void* p = SayReadPtr(root, kTsLineParent);
        if (p == 0 || SayPlausiblePtr(p) == 0) break;
        root = p;
    }
    if (w) { ++g_tsWalkOver; ++g_tsRunWalk; }
    g_tsLastLine = TkLineSid(line) + " '" + TsName(line) + "' dialogue='" + TsName(root) + "' walkOver=" + (w ? "1" : "0");
    Ogre::Vector3 np, tp;
    float d = -1.0f;
    if (SafeReadPosition(npc, &np) && SafeReadPosition(target, &tp))
        d = sqrtf((np.x - tp.x) * (np.x - tp.x) + (np.y - tp.y) * (np.y - tp.y) + (np.z - tp.z) * (np.z - tp.z));
    char b[360];
    std::sprintf(b, "[TALKSIGHT] STARTED try=%lld after %.1f s npc=%u -> target=%u dist=%.1f forType=%d leadSeen+=%lld leadOrdered+=%lld line=",
                 g_tsRunTries, (double)(DWORD)(::GetTickCount() - g_tsStartMs) / 1000.0, g_tsNpcUid, g_tsTargetUid, d, ft,
                 (long long)g_s6LeadSeen - ls0, (long long)g_s6LeadOrdered - lo0);
    DebugLog(std::string(b) + g_tsLastLine);
    if (w) TkArrWatchArm(npc, target);   /* P26s6 arrival probe: follow the leader until the arrival or the watch time */
    if (w || !g_tsUntilWalk) TsEnd(w ? "a walk-over line started" : "a conversation started");
}

// MAIN THREAD (the command channel). Read-only.
std::string TalkCarriersCommand(float x, float z, float radius)
{
    ++g_tsCarrierRuns;
    if (coop::GameWorldPtr() == 0 || EngineWritesBlocked()) { DebugLog("[TALKSIGHT] talkcarriers: no world"); return "error talkcarriers: no world"; }
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return "error talkcarriers: the character update list has an implausible size";
    int nearN = 0, carriers = 0, dialogues = 0, logged = 0;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* ch = *it; Ogre::Vector3 q;
        if (!TsCandidate(ch) || !SafeReadPosition(ch, &q)) continue;
        const float dx = q.x - x, dz = q.z - z, d = sqrtf(dx * dx + dz * dz);
        if (d > radius) continue;
        ++nearN;
        TsDlgInfo dl[kTsDlgCap];
        const int n = TsWalkOvers(ch, "", dl, kTsDlgCap);
        if (n == 0) continue;
        ++carriers; dialogues += n;
        const unsigned int u = FindSpawnedUid(ch);
        int fl = -1;
        TsFloorPod(ch, &fl);
        char nn[48], b[200];
        TalkName(ch, nn);
        ::Character* const cl = TkArrLeaderOf(ch);   /* P26s6 arrival probe, decision 236 fixture */
        std::sprintf(b, "[TALKSIGHT] carrier uid=%u %s '%s' at %.0f,%.0f dist=%.0f floor=%d squadLeader=%d", u, TsOwnerName(TkS6Owner(ch)), nn,
                     q.x, q.z, d, fl, cl == 0 ? -1 : (cl == ch ? 1 : 0));
        for (int i = 0; i < n && logged < kTsCarrierLogCap; ++i, ++logged) DebugLog(std::string(b) + TsDlgString(dl[i]));
    }
    char s[240];
    std::sprintf(s, "[TALKSIGHT] talkcarriers x=%.0f z=%.0f r=%.0f: nearNpcs=%d carriers=%d walkOverDialogues=%d logged=%d host=%d",
                 x, z, radius, nearN, carriers, dialogues, logged, net::SessionIsHost() ? 1 : 0);
    DebugLog(std::string(s));
    return "ok talkcarriers";
}

// MAIN THREAD (the command channel). Read-only. TEST-ONLY: which NPCs near (x,z) offer dialogue action `act` through event `ev` -
// e.g. the bounty office's people with PAY_BOUNTY (10) for a carried hand-in. The walk-over scan's caps (kTsScanNodeCap and
// kTsScanDepthCap per conversation, kTsDlgCap per NPC, kTsCarrierLogCap lines).
std::string TalkCarriersActCommand(float x, float z, float radius, int ev, int act)
{
    ++g_tsCarrierRuns;
    if (coop::GameWorldPtr() == 0 || EngineWritesBlocked()) { DebugLog("[TALKSIGHT] talkcarriers: no world"); return "error talkcarriers: no world"; }
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return "error talkcarriers: the character update list has an implausible size";
    int nearN = 0, carriers = 0, dialogues = 0, logged = 0;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* ch = *it; Ogre::Vector3 q;
        if (!TsCandidate(ch) || !SafeReadPosition(ch, &q)) continue;
        const float dx = q.x - x, dz = q.z - z, d = sqrtf(dx * dx + dz * dz);
        if (d > radius) continue;
        ++nearN;
        long long mapSize = 0;
        void* list = TkConvListPod(TalkDialogueOf(ch), ev, &mapSize);
        const int n = TkChoiceCountPod(list);
        int found = 0;
        char nn[48];
        TalkName(ch, nn);
        const unsigned int u = FindSpawnedUid(ch);
        for (int i = 0; i < n && found < kTsDlgCap; ++i)
        {
            void* root = TkChoiceAtPod(list, i);
            int nodes = 0;
            void* hit = TsScanAct(root, 0, act, &nodes);
            if (hit == 0) continue;
            ++found; ++dialogues;
            if (logged >= kTsCarrierLogCap) continue;
            ++logged;
            char b[240];
            std::sprintf(b, "[TALKSIGHT] carrier ev=%d act=%d uid=%u %s '%s' at %.0f,%.0f dist=%.0f nodes=%d", ev, act, u,
                         TsOwnerName(TkS6Owner(ch)), nn, q.x, q.z, d, nodes);
            DebugLog(std::string(b) + " dialogue='" + TsName(root) + "' sid=" + TkLineSid(root) + " line=" + TkLineSid(hit));
        }
        if (found) ++carriers;
    }
    char s[260];
    std::sprintf(s, "[TALKSIGHT] talkcarriers x=%.0f z=%.0f r=%.0f ev=%d act=%d: nearNpcs=%d carriers=%d dialogues=%d logged=%d host=%d",
                 x, z, radius, ev, act, nearN, carriers, dialogues, logged, net::SessionIsHost() ? 1 : 0);
    DebugLog(std::string(s));
    return "ok talkcarriers";
}

/* this game's first own player-faction character with a readable position (talktest's reference), 0 none. */
static ::Character* TsOwnPlayer(Ogre::Vector3* pos)
{
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* ch = 0; ::Faction* f = 0;
        if (!MirrorSlot(i, &u, &ch) || u == 0 || !net::IsUidMine(u)) continue;
        if (!CrimeFactionOf(ch, &f) || f == 0 || !IsPlayerFaction(f)) continue;
        if (SafeReadPosition(ch, pos)) return ch;
    }
    return 0;
}

/* P25 T729 (TEST-ONLY name levers): the nearest candidate (TsCandidate: living, conscious, non-player, with a Dialogue) that has a
   uid and whose display name keys as nameKey (coopsay::TalkPersonKey), within maxDist (2D) of (x,z); 0 none. *count = how many
   loaded candidates carry the name at any distance. P25 T729b: own = 1 only this game's own, 0 only a copy the other game drives,
   -1 any (TkS6Owner). MAIN THREAD. */
static ::Character* TsNearestNamed(const std::string& nameKey, float x, float z, float maxDist, int own, float* dist, int* count)
{
    *dist = -1.0f; *count = 0;
    if (coop::GameWorldPtr() == 0 || nameKey.empty()) return 0;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return 0;
    ::Character* best = 0; float bd = maxDist;
    char nn[48];
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* ch = *it; Ogre::Vector3 q;
        if (!TsCandidate(ch) || FindSpawnedUid(ch) == 0 || (own >= 0 && TkS6Owner(ch) != own)) continue;   /* P25 T729b: own */
        TalkName(ch, nn);
        if (coopsay::TalkPersonKey(nn) != nameKey || !SafeReadPosition(ch, &q)) continue;
        ++*count;
        const float dx = q.x - x, dz = q.z - z, d = sqrtf(dx * dx + dz * dz);
        if (d >= bd) continue;
        bd = d; best = ch;
    }
    if (best != 0) *dist = bd;
    return best;
}

/* TEST-ONLY (talktest nearestfaction): as TsNearestNamed, matched on the candidate's faction name (its TalkPersonKey) instead of its
   own name - bar recruits carry random names. MAIN THREAD. */
static ::Character* TsNearestFaction(const std::string& facKey, float x, float z, float maxDist, int own, float* dist, int* count)
{
    *dist = -1.0f; *count = 0;
    if (coop::GameWorldPtr() == 0 || facKey.empty()) return 0;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return 0;
    ::Character* best = 0; float bd = maxDist;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* ch = *it; Ogre::Vector3 q; ::Faction* f = 0;
        if (!TsCandidate(ch) || FindSpawnedUid(ch) == 0 || (own >= 0 && TkS6Owner(ch) != own)) continue;
        if (!CrimeFactionOf(ch, &f) || f == 0 || SayPlausiblePtr(f) == 0) continue;
        if (coopsay::TalkPersonKey(f->getName()) != facKey || !SafeReadPosition(ch, &q)) continue;
        ++*count;
        const float dx = q.x - x, dz = q.z - z, d = sqrtf(dx * dx + dz * dz);
        if (d >= bd) continue;
        bd = d; best = ch;
    }
    if (best != 0) *dist = bd;
    return best;
}

/* The carried hand-in fixture's TEST-ONLY name pick (crimetest bountyset, koself name, capturetest carry name) - see speech.h.
   MAIN THREAD. */
::Character* LeverNearestNamed(const std::string& nameKey, int pick, ::Character* from, float maxDist, unsigned int* uid, float* dist,
                               std::string* name, std::string* why)
{
    *uid = 0; *dist = -1.0f; name->clear(); why->clear();
    if (coop::GameWorldPtr() == 0 || EngineWritesBlocked()) { *why = "no world"; return 0; }
    if (nameKey.empty()) { *why = "an empty name"; return 0; }
    Ogre::Vector3 o;
    if (from != 0) { if (!SafeReadPosition(from, &o)) { *why = "the reference character's position does not read"; return 0; } }
    else if (TsOwnPlayer(&o) == 0) { *why = "this game has no own player character with a readable position"; return 0; }
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) { *why = "the character update list has an implausible size"; return 0; }
    ::Character* best = 0; float bd = maxDist; int named = 0;
    char nn[48];
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* ch = *it; Ogre::Vector3 q;
        if (ch == 0 || ch == from || SayPlausiblePtr(ch) == 0) continue;
        TalkName(ch, nn);
        if (coopsay::TalkPersonKey(nn) != nameKey) continue;
        if (pick == kLeverPickOwnLiving)
        {
            const unsigned int u = FindSpawnedUid(ch);
            ::Faction* f = 0;
            if (u == 0 || !net::IsUidMine(u) || CrimeIsDead(ch) != 0) continue;
            if (!CrimeFactionOf(ch, &f) || f == 0 || IsPlayerFaction(f) || IsPeerFaction(f)) continue;
        }
        else if (pick == kLeverPickDowned)
        {
            if (CrimeIsDead(ch) != 0 || !IsDownedCharacter(ch)) continue;
        }
        if (!SafeReadPosition(ch, &q)) continue;
        ++named;
        const float dx = q.x - o.x, dz = q.z - o.z, d = sqrtf(dx * dx + dz * dz);
        if (d >= bd) continue;
        bd = d; best = ch;
    }
    if (best == 0)
    {
        char dt[40], b[260];
        if (maxDist > 1.0e9f) std::strcpy(dt, "anywhere");
        else std::sprintf(dt, "within %.0f units", maxDist);
        std::sprintf(b, "no %s character named '%.60s' %s (%d such loaded at any distance)",
                     pick == kLeverPickOwnLiving ? "own living non-player" : (pick == kLeverPickDowned ? "down (knocked-out)" : "loaded"),
                     nameKey.c_str(), dt, named);
        *why = b;
        return 0;
    }
    *uid = FindSpawnedUid(best);
    *dist = bd;
    TalkName(best, nn);
    *name = nn;
    return best;
}

// MAIN THREAD (the command channel through TalkNearPoint, and TalkWaitTick's re-checks). Counts no refusal (the callers do);
// g_tsNotLoaded = 1 when it failed only because the character is not loaded yet.
static bool TsNearCheck(const std::string& arg, float* x, float* z, std::string* why)
{
    unsigned int uid = 0; std::string key, nameKey; int units = 0, maxdy = -1;
    const int byName = coopsay::TalkTeleportNearNameParse(arg, &nameKey, &units, &maxdy);   /* P25 T729: near name <npc name> */
    if (!byName && !coopsay::TalkTeleportNearParseDy(arg, &uid, &key, &units, &maxdy)) { *why = "usage playerteleport near <uid | nearest <dialogueName> | name <npc name, '_' = a space>> <units 1..2000> [maxdy <1..500>]"; return false; }
    if (coop::GameWorldPtr() == 0 || EngineWritesBlocked()) { *why = "no world"; return false; }
    Ogre::Vector3 pp;
    ::Character* me = TsOwnPlayer(&pp);
    if (me == 0) { *why = "this game has no own player character with a readable position"; return false; }
    ::Character* ch = 0; float nd = -1.0f;
    char nb[200];
    if (uid != 0)
    {
        ch = FindSpawned(uid);
        if (ch == 0 || SayPlausiblePtr(ch) == 0) { std::sprintf(nb, "uid %u is not here", uid); *why = nb; g_tsNotLoaded = 1; return false; }
    }
    else if (byName)
    {
        /* P25 T729: the nearest character with that name within the carrier search's radius; none -> wait (not loaded yet) */
        int named = 0;
        ch = TsNearestNamed(nameKey, pp.x, pp.z, kTalkNearMax, -1, &nd, &named);
        if (ch == 0)
        {
            std::sprintf(nb, " (%d loaded with that name farther away)", named);
            *why = "no living, conscious non-player character named '" + nameKey + "' within 3000 units of this game's player" + nb;
            g_tsNotLoaded = 1;
            return false;
        }
        uid = FindSpawnedUid(ch);
    }
    else
    {
        ch = TsNearestCarrier(key, pp.x, pp.z, &nd);
        if (ch == 0) { *why = "no living non-player character within 3000 units of this game's player carries a walk-over dialogue keyed '" + key + "'"; g_tsNotLoaded = 1; return false; }
        uid = FindSpawnedUid(ch);
    }
    Ogre::Vector3 cp;
    int cf = -1, pf = -1;
    if (!SafeReadPosition(ch, &cp) || !TsFloorPod(ch, &cf)) { *why = "the character's position or floor is unreadable"; return false; }
    TsFloorPod(me, &pf);
    if (cf != 0) { std::sprintf(nb, "the character (uid %u) is indoors (floor %d): only an outdoor character is a teleport anchor", uid, cf); *why = nb; return false; }
    float ux = pp.x - cp.x, uz = pp.z - cp.z;
    const float len = sqrtf(ux * ux + uz * uz);
    if (len < 0.01f) { ux = 1.0f; uz = 0.0f; } else { ux /= len; uz /= len; }
    *x = cp.x + ux * (float)units;
    *z = cp.z + uz * (float)units;
    char nn[48], b[400], db[260];
    TalkName(ch, nn);
    std::sprintf(db, " anchorFeetY=%.1f", cp.y);
    if (maxdy > 0)
    {
        /* P26lvl maxdy: the walk-over arrives only when the height gap is < 10 (or < 35 on the same floor; isWithinChoppingRange
           0x59BC40) - T718 stood 44-55 units off. The destination's terrain must be within maxdy of the anchor's feet (cp.y):
           point 0 = the straight line toward the player, then 1..15 around the circle of radius units, 22.5 degrees apart. */
        const int kDirs = 16;
        const float a0 = atan2f(uz, ux);
        int pick = -1, noTerrain = 0, failed = 0;
        float pickY = 0.0f, bestGap = -1.0f;
        for (int i = 0; i < kDirs && pick < 0; ++i)
        {
            const float a = a0 + 6.2831853f * (float)i / (float)kDirs;
            const float px = (i == 0) ? *x : cp.x + cosf(a) * (float)units;
            const float pz = (i == 0) ? *z : cp.z + sinf(a) * (float)units;
            float th = 0.0f;
            const int r = GroundTerrainHeightAt(px, pz, &th);
            if (r == 0) { ++noTerrain; continue; }
            if (r < 0) { ++failed; continue; }
            const float gap = fabsf(th - cp.y);
            if (bestGap < 0.0f || gap < bestGap) bestGap = gap;
            if (gap <= (float)maxdy) { pick = i; pickY = th; *x = px; *z = pz; }
        }
        if (pick < 0)
        {
            std::sprintf(b, "no point at %d units within %d height of uid %u '%s' (feet y=%.1f at %.0f,%.0f): %d points tried, smallest gap %.1f, no terrain %d, read failed %d",
                         units, maxdy, uid, nn, cp.y, cp.x, cp.z, kDirs, bestGap, noTerrain, failed);
            *why = b;
            return false;
        }
        std::sprintf(db, " maxdy=%d point=%d/16 (0 = the straight line) terrainY=%.1f anchorFeetY=%.1f gap=%.1f", maxdy, pick, pickY, cp.y, fabsf(pickY - cp.y));
    }
    ++g_tsNearOk;
    std::sprintf(b, "[TALKSIGHT] playerteleport near: to %.0f,%.0f = %d units from uid=%u %s '%s' at %.0f,%.0f (floor %d, %s) on the line toward the player at %.0f,%.0f (floor %d)",
                 *x, *z, units, uid, TsOwnerName(TkS6Owner(ch)), nn, cp.x, cp.z, cf, byName ? "nearest named" : (key.empty() ? "by uid" : "nearest carrier"), pp.x, pp.z, pf);   /* P25 T729 */
    DebugLog(std::string(b) + (key.empty() ? std::string() : " key=" + key) + (byName ? " name='" + nameKey + "'" : std::string()) + db);   /* P25 T729 */   /* P26lvl: the chosen point's height and the anchor's feet */
    return true;
}

/* MAIN THREAD (the command channel). T718: 1 = the point is in *x, *z; 0 = refused (*why, counted); 2 = the character is not
   loaded yet - a wait is armed (logged WAITING) and TalkWaitTick finishes it. A new call replaces a pending wait (logged). */
int TalkNearPoint(const std::string& arg, float* x, float* z, std::string* why)
{
    if (g_twNear.on)
    {
        g_twNear.on = 0;
        DebugLog("[TALKSIGHT] playerteleport near: the pending wait for '" + g_twNear.arg + "' is REPLACED by a new playerteleport near");
    }
    g_tsNotLoaded = 0;
    if (TsNearCheck(arg, x, z, why)) return 1;
    if (!g_tsNotLoaded) { ++g_tsNearRefused; return 0; }
    const DWORD now = ::GetTickCount();
    g_twNear.on = 1; g_twNear.arg = arg; g_twNear.startMs = now; g_twNear.waitMs = kTwNearMs; g_twNear.lastTryMs = now;
    ++g_twNearArmed;
    DebugLog("[TALKSIGHT] playerteleport near WAITING: " + *why + " - re-checking every 500 ms for up to 60 s");
    return 2;
}

/* The lever's verb as the command named it: nearestfaction, else nearestname. */
static const char* TsVerb(const std::string& arg)
{
    const size_t at = arg.find_first_not_of(" \t");
    return (at != std::string::npos && arg.compare(at, 14, "nearestfaction") == 0) ? "nearestfaction" : "nearestname";
}

/* P25 T729 (TEST-ONLY): `talktest nearestname <npc name> <targetUid> [event]` - the check. 1 = picked: *armArg = "<uid> <target>
   <event>" for TalkTestArm and the pick (uid, owner, where, how far from the target) is logged; 0 = refused (*why); g_tsNotLoaded
   = 1 when no such character is loaded yet. No distance limit (talktest by uid has none). MAIN THREAD (the command channel). */
static int TsNameCheck(const std::string& arg, std::string* armArg, std::string* why)
{
    std::string nameKey; unsigned int tu = 0; int ev = 1, own = -1;
    const bool byFac = coopsay::TalkTestFactionParse(arg, &nameKey, &tu, &ev, &own) != 0;   /* nearestfaction: the same pick by faction name */
    if (!byFac && !coopsay::TalkTestNameParse(arg, &nameKey, &tu, &ev, &own))   /* P25 T729b: [copy | mine] */
    { *why = "usage talktest nearestname|nearestfaction <npc name | faction name: the words up to the first number, '_' = a space> <targetUid> [event 0..255, default 1] [copy | mine]"; return 0; }
    if (coop::GameWorldPtr() == 0 || EngineWritesBlocked()) { *why = "no world"; return 0; }
    char b[400];
    ::Character* tc = FindSpawned(tu);
    Ogre::Vector3 tp;
    if (tc == 0 || SayPlausiblePtr(tc) == 0 || !SafeReadPosition(tc, &tp))
    { std::sprintf(b, "the target uid %u is not here (or its position is unreadable)", tu); *why = b; return 0; }
    float d = -1.0f; int named = 0;
    ::Character* ch = byFac ? TsNearestFaction(nameKey, tp.x, tp.z, 1.0e30f, own, &d, &named)
                            : TsNearestNamed(nameKey, tp.x, tp.z, 1.0e30f, own, &d, &named);
    if (ch == 0)
    {
        *why = std::string("no living, conscious non-player character ") + (byFac ? "of the faction '" : "named '") + nameKey + "'" + (own == 0 ? " that is a copy the other game drives" : (own == 1 ? " that is this game's own" : "")) + " is loaded";   /* P25 T729b */
        g_tsNotLoaded = 1;
        return 0;
    }
    const unsigned int uid = FindSpawnedUid(ch);
    Ogre::Vector3 cp;
    cp.x = 0; cp.y = 0; cp.z = 0;
    SafeReadPosition(ch, &cp);
    char nn[48];
    TalkName(ch, nn);
    std::sprintf(b, "[TALK] talktest %s: picked uid=%u %s '%s' at %.0f,%.0f = %.0f units from target uid=%u at %.0f,%.0f (%d loaded with that %s) ev=0x%X",
                 byFac ? "nearestfaction" : "nearestname", uid, TsOwnerName(TkS6Owner(ch)), nn, cp.x, cp.z, d, tu, tp.x, tp.z, named,
                 byFac ? "faction" : "name", (unsigned int)ev);
    DebugLog(std::string(b) + (byFac ? " faction='" : " name='") + nameKey + "'" + (own == 0 ? " only=copy" : (own == 1 ? " only=mine" : "")));   /* P25 T729b */
    std::sprintf(b, "%u %u %d", uid, tu, ev);
    *armArg = b;
    return 1;
}

/* P25 T729: MAIN THREAD (the command channel). Picks the NPC and arms `talktest <uid> <target> <event>` (TalkTestArm, its own ARMED
   line); when no such NPC is loaded yet it WAITS (re-checked every 500 ms by TalkWaitTick, up to 60 s), then refuses in words. A
   new call replaces a pending wait (logged). */
std::string TalkTestNameArm(const std::string& arg)
{
    const std::string verb = TsVerb(arg);
    if (g_twName.on)
    {
        g_twName.on = 0;
        DebugLog("[TALK] talktest " + std::string(TsVerb(g_twName.arg)) + ": the pending wait for '" + g_twName.arg + "' is REPLACED by a new talktest " + verb);
    }
    g_tsNotLoaded = 0;
    std::string armArg, why;
    if (TsNameCheck(arg, &armArg, &why)) return TalkTestArm(armArg);
    if (!g_tsNotLoaded)
    {
        DebugLog("[TALK] talktest " + verb + " REFUSED: " + why);
        return "error talktest " + verb + ": " + why;
    }
    const DWORD now = ::GetTickCount();
    g_twName.on = 1; g_twName.arg = arg; g_twName.startMs = now; g_twName.waitMs = kTwNearMs; g_twName.lastTryMs = now;
    DebugLog("[TALK] talktest " + verb + " WAITING: " + why + " - re-checking every 500 ms for up to 60 s");
    return "ok talktest " + verb + ": waiting up to 60 s for the NPC to load";
}

/* MAIN THREAD (the command channel). T718: the talksight check; when it fails only because the NPC is not loaded yet, a wait of
   up to the command's own seconds is armed (its talksight window starts when it finally arms, as before). */
std::string TalkSightArm(const std::string& arg)
{
    if (g_twSight.on)
    {
        g_twSight.on = 0;
        DebugLog("[TALKSIGHT] the pending talksight wait for '" + g_twSight.arg + "' is REPLACED by a new talksight");
    }
    g_tsNotLoaded = 0;
    const std::string r = TsSightCheck(arg);
    if (!g_tsNotLoaded) return r;
    unsigned int nu = 0, tu = 0; std::string key; int secs = 0, walk = 0;
    coopsay::TalkSightParse(arg, &nu, &key, &tu, &secs, &walk);   /* parsed already inside TsSightCheck */
    const DWORD now = ::GetTickCount();
    g_twSight.on = 1; g_twSight.arg = arg; g_twSight.startMs = now; g_twSight.waitMs = (DWORD)secs * 1000u; g_twSight.lastTryMs = now;
    ++g_twSightArmed;
    char b[120];
    std::sprintf(b, " - re-checking every 500 ms for up to %d s", secs);
    DebugLog("[TALKSIGHT] WAITING: " + g_tsLastWhy + b);
    std::sprintf(b, "ok talksight: waiting up to %d s for the NPC to load", secs);
    return std::string(b);
}

static void TwLogEnd(const char* head, DWORD waited, const std::string& why)
{
    char b[160];
    std::sprintf(b, "%s after %lu ms of waiting: ", head, (unsigned long)waited);
    DebugLog(std::string(b) + why);
}

/* MAIN THREAD - CommandChannelTick, the same thread and place the commands run (never the K2 safe point: no teleport or sendEvent
   from TalkTestDrain). While a wait is pending, every 500 ms: the same check, quietly. Returns 1 when the near wait's check passed:
   the point is in *x, *z and the wait in *waitedMs - the caller moves the player with PlayerTeleportAbs exactly as the command does
   and logs DONE. A talksight wait that passes is armed exactly as the command arms (its ARMED lines) plus "ARMED after". */
int TalkWaitTick(float* x, float* z, unsigned long* waitedMs)
{
    if (!g_twNear.on && !g_twSight.on && !g_twName.on) return 0;   /* P25 T729: g_twName */
    const DWORD now = ::GetTickCount();
    if (coop::GameWorldPtr() == 0 || EngineWritesBlocked())
    {
        if (g_twNear.on) { g_twNear.on = 0; TwLogEnd("[TALKSIGHT] playerteleport near: the wait ENDED", (DWORD)(now - g_twNear.startMs), "the world is gone, loading or tearing down"); }
        if (g_twSight.on) { g_twSight.on = 0; TwLogEnd("[TALKSIGHT] talksight: the wait ENDED", (DWORD)(now - g_twSight.startMs), "the world is gone, loading or tearing down"); }
        if (g_twName.on) { g_twName.on = 0; TwLogEnd(("[TALK] talktest " + std::string(TsVerb(g_twName.arg)) + ": the wait ENDED").c_str(), (DWORD)(now - g_twName.startMs), "the world is gone, loading or tearing down"); }   /* P25 T729 */
        return 0;
    }
    int rc = 0;
    if (g_twNear.on && (DWORD)(now - g_twNear.lastTryMs) >= kTwPeriodMs)
    {
        g_twNear.lastTryMs = now;
        const DWORD waited = (DWORD)(now - g_twNear.startMs);
        std::string why;
        g_tsNotLoaded = 0;
        if (TsNearCheck(g_twNear.arg, x, z, &why)) { g_twNear.on = 0; ++g_twNearDone; *waitedMs = (unsigned long)waited; rc = 1; }
        else if (!g_tsNotLoaded) { g_twNear.on = 0; ++g_tsNearRefused; TwLogEnd("[TALKSIGHT] playerteleport near REFUSED", waited, why); }
        else if (waited >= g_twNear.waitMs)
        {
            g_twNear.on = 0; ++g_twNearTimedOut; ++g_tsNearRefused;
            TwLogEnd("[TALKSIGHT] playerteleport near REFUSED", waited, why + " (the 60 s wait ran out)");
        }
    }
    if (g_twSight.on && (DWORD)(now - g_twSight.lastTryMs) >= kTwPeriodMs)
    {
        g_twSight.lastTryMs = now;
        const DWORD waited = (DWORD)(now - g_twSight.startMs);
        g_tsNotLoaded = 0;
        g_tsQuiet = 1;
        const std::string r = TsSightCheck(g_twSight.arg);
        g_tsQuiet = 0;
        if (r.compare(0, 2, "ok") == 0)
        {
            g_twSight.on = 0; ++g_twSightDone;
            char b[96];
            std::sprintf(b, "[TALKSIGHT] ARMED after %lu ms of waiting", (unsigned long)waited);
            DebugLog(std::string(b));
        }
        else if (!g_tsNotLoaded) { g_twSight.on = 0; ++g_tsRefused; TwLogEnd("[TALKSIGHT] talksight REFUSED", waited, g_tsLastWhy); }
        else if (waited >= g_twSight.waitMs)
        {
            g_twSight.on = 0; ++g_twSightTimedOut; ++g_tsRefused;
            TwLogEnd("[TALKSIGHT] talksight REFUSED", waited, g_tsLastWhy + " (the command's wait ran out)");
        }
    }
    if (g_twName.on && (DWORD)(now - g_twName.lastTryMs) >= kTwPeriodMs)   /* P25 T729: talktest nearestname's wait */
    {
        g_twName.lastTryMs = now;
        const DWORD waited = (DWORD)(now - g_twName.startMs);
        std::string armArg, why;
        g_tsNotLoaded = 0;
        if (TsNameCheck(g_twName.arg, &armArg, &why))
        {
            g_twName.on = 0;
            char b[96];
            std::sprintf(b, "[TALK] talktest %s: picked after %lu ms of waiting", TsVerb(g_twName.arg), (unsigned long)waited);
            DebugLog(std::string(b));
            TalkTestArm(armArg);
        }
        else if (!g_tsNotLoaded) { g_twName.on = 0; TwLogEnd(("[TALK] talktest " + std::string(TsVerb(g_twName.arg)) + " REFUSED").c_str(), waited, why); }
        else if (waited >= g_twName.waitMs)
        {
            g_twName.on = 0;
            TwLogEnd(("[TALK] talktest " + std::string(TsVerb(g_twName.arg)) + " REFUSED").c_str(), waited, why + " (the 60 s wait ran out)");
        }
    }
    return rc;
}

// MAIN THREAD, worker paused (combat.cpp detour_tsRagdollUpdates).
/* PROBE-START: P114 - Dialogue::sendEvent's refusal gates, read just before talktest's call (report-only).
   Offsets from build/decomp_683f00.txt (Steam 1.0.65 0x683F00; 1.0.68 0x684990, same bytes per the P25 refusal read).
   Every read guarded; POD only (C2712). The only engine calls are the two pure hand lookups sendEvent itself makes:
   hand::getCharacter 0x7974F0 (:29) and hand::asBuilding 0x791D70 (= 0x9F8050 on HandleManager+0x20, as :81). NOT called:
   the repeat-timer lookup 0x6A3C10 (:18, it inserts), hasDied 0x620B20, the always-list 0x670860, 0x9C3CC0 (war campaign).
   P114 ext (gate 4 normal-or-not): + the NPC's 'where am I' (vt+0x1D8, 1.0.68 0x5CE020) inputs read as fields (+0x3D4, +0x2F8,
   +0x308) - the virtual itself is NOT called; the talker's building (same hand path); the interior's +0x08/+0x28 and building
   +0x1A1; the owner class via items.cpp BuildingOwnerClass (one more engine call: the building's getOwnerFaction getter). */
static int P114Rd(const void* base, size_t off, void* out, size_t n)
{
    if (base == 0) return 0;
    __try { std::memcpy(out, (const char*)base + off, n); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void* P114HandChar(const void* h)
{
    __try { return (void*)((const ::hand*)h)->getCharacter(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void* P114HandBuilding(const void* h)
{
    __try { return (void*)((const ::hand*)h)->asBuilding(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* conversationHasEnded 0x6711D0 replayed on a Dialogue (decomp_6711d0.txt:9-27). 1 ended, 0 running, -1 unreadable. */
static int P114Ended(const void* d)
{
    unsigned char he = 0; unsigned long long p228 = 0, b = 0, e = 0; int t2c8 = 0;
    if (!P114Rd(d, 0x148, &he, 1) || !P114Rd(d, 0x228, &p228, 8) || !P114Rd(d, 0x2C8, &t2c8, 4)) return -1;
    if (he != 0 && p228 == 0 && t2c8 == 0xB) return 1;
    if (!P114Rd(d, 0x80, &b, 8) || !P114Rd(d, 0x88, &e, 8)) return -1;
    if (e < b || e - b > 4 * 256) return -1;
    for (unsigned long long q = b; q < e; q += 4) { int v = 0; if (!P114Rd((const void*)q, 0, &v, 4)) return -1; if (v == 1) return 1; }
    return 0;
}
/* P114 ext (gate 4 normal-or-not): items.cpp's owner class (coopown::kOwn*: 1 = this game's player faction, 2 = another player's,
   0 none, 3 shared, 4 unreadable record, -1 unreadable building). Declared here so the probe block stays self-contained; the
   definition is items.cpp's (items.h:423). Its only engine call is the building's own getOwnerFaction virtual, SEH-guarded. */
int BuildingOwnerClass(void* bld, int* slot);
/* The 'where am I' building by the same path the gate-4 read uses: the hand at +0x80, type at +0x88 (0 = building),
   hand::asBuilding (row HandGetBuilding). *type = the hand type (-1 unread). 0 = not in a building / unreadable. */
static void* P114WhereBld(void* ch, int* type)
{
    *type = -1;
    if (ch == 0 || !P114Rd(ch, 0x88, type, 4) || *type != 0) return 0;
    return P114HandBuilding((const char*)ch + 0x80);
}
/* One line, named after the gate each value feeds. out >= 2048 bytes. talker = sendEvent's second argument. */
static void P114GateLine(void* dlg, void* npc, void* talker, unsigned int nuid, int ev, char* out)
{
    void* me = 0; void* cur = 0; void* intr = 0; void* lead = 0; void* leadDlg = 0;
    int leadType = -1, leadEnded = -2, inType = -1, rdOk = 0;
    unsigned char ko = 0xFF, dead = 0xFF;
    void* bld = 0; void* b1f0 = 0; void* b10 = 0;
    /* P114 ext: what 0x5CE020 (Character vt+0x1D8, 'where am I') branches on, the talker's building, the building's interior */
    unsigned int carried = 0xFFFFFFFFu; unsigned long long f2f8 = 0xFFFFFFFFFFFFFFFFull; int h300Type = -1;
    int tType = -1; void* tBld = 0;
    void* i08 = 0; float i28 = -1.0f; unsigned int i28bits = 0xFFFFFFFFu; int i28ok = 0; int b1a1 = -1;
    int ownCls = -9, ownSlot = -1;
    rdOk += P114Rd(dlg, 0x150, &me, 8);    /* :20/:63 me */
    rdOk += P114Rd(dlg, 0x190, &cur, 8);   /* :25 currentConversation */
    rdOk += P114Rd(dlg, 0xC8, &intr, 8);   /* :28/:108 parked interrupt (param_1 + 200) */
    rdOk += P114Rd(dlg, 0x2A8, &leadType, 4);   /* :29 conversationMaster hand (+0x2A0), its type at +8 */
    lead = P114HandChar((const char*)dlg + 0x2A0);   /* :29 the same lookup sendEvent makes */
    if (lead != 0 && P114Rd(lead, 0x280, &leadDlg, 8) && leadDlg != 0) leadEnded = P114Ended(leadDlg);   /* :30 */
    if (me != 0)
    {
        P114Rd(me, 0x5B9, &ko, 1);     /* :63 unconscious */
        P114Rd(me, 0x5BC, &dead, 1);   /* :64 dead */
        if (P114Rd(me, 0x88, &inType, 4) && inType == 0)   /* :77/:79 isIndoors() hand type, read as the field (RootObject +0x80) */
        {
            bld = P114HandBuilding((const char*)me + 0x80);   /* :81 */
            if (bld != 0 && P114Rd(bld, 0x1F0, &b1f0, 8) && b1f0 != 0) P114Rd(b1f0, 0x10, &b10, 8);   /* :83-84 */
        }
        { unsigned char cb = 0; if (P114Rd(me, 0x3D4, &cb, 1)) carried = cb; }        /* P114 ext: 0x5CE020's carried test - a BYTE (review fold 1) */
        { unsigned int d = 0; if (P114Rd(me, 0x2F8, &d, 4)) f2f8 = d; }              /* P114 ext: 0x5CE020's +0x2F8 test - a DWORD (review fold 1) */
        P114Rd(me, 0x308, &h300Type, 4);   /* P114 ext: the hand at +0x300, its type at +8 */
    }
    tBld = P114WhereBld(talker, &tType);   /* P114 ext: the talker's 'where am I' building, the same path */
    if (bld != 0)
    {
        unsigned char c1a1 = 0;
        if (P114Rd(bld, 0x1A1, &c1a1, 1)) b1a1 = (int)c1a1;   /* P114 ext */
        if (b1f0 != 0)
        {
            P114Rd(b1f0, 0x08, &i08, 8);   /* P114 ext */
            if (P114Rd(b1f0, 0x28, &i28bits, 4)) { std::memcpy(&i28, &i28bits, 4); i28ok = 1; }   /* P114 ext: the 10 s timer */
        }
        ownCls = BuildingOwnerClass(bld, &ownSlot);   /* P114 ext: MAIN THREAD (this line runs on it) */
    }
    const int g2 = (cur == 0 && (intr == 0 || ev != 1) && lead != 0 && leadEnded == 0) ? 1 : 0;
    const int g3 = (ko == 1 || dead == 1) ? 1 : 0;
    const int g4 = (inType != 0xB && ev != 0x27 && ev != 6 && ev != 0xB && (inType != 0 || bld == 0 || b1f0 == 0 || b10 == 0)) ? 1 : 0;
    std::sprintf(out, "[TALK] P114 gates ev=0x%X npc uid=%u dlg=%p me=%p meIsNpc=%d reads=%d/4 | g1 repeatTimer(+0xD0)=unread (its lookup 0x6A3C10 inserts)"
                 " | g2 currentConv(+0x190)=%p parkedInterrupt(+0xC8)=%p lead(+0x2A0)=%p leadType=%d leadIsMe=%d leadEnded=%d -> g2refuse=%d (ev assumed outside the always-list)"
                 " | g3 unconscious(+0x5B9)=%d dead(+0x5BC)=%d -> g3refuse=%d (KO-exempt mask unread)"
                 " | g4 indoorsType(+0x88)=%d building=%p bld+0x1F0=%p ->+0x10=%p -> g4refuse=%d"
                 " | g5 parkedInterrupt=%d warCampaign=unread (0x9C3CC0 is a call, not decompiled)"
                 " | g4x npcWhere carried(+0x3D4)=0x%08X f2f8(+0x2F8)=0x%llX h300Type(+0x308)=%d"
                 " talker=%p talkerType(+0x88)=%d talkerBld=%p sameBld=%d"
                 " bld+0x1A1=%d int(+0x1F0)=%p int+0x08=%p int+0x10=%p int+0x28=%.3f(bits 0x%08X read=%d)"
                 " owner cls=%d slot=%d ownerIsMyPlayerFaction=%d",
                 (unsigned int)ev, nuid, dlg, me, (me == npc) ? 1 : 0, rdOk, cur, intr, lead, leadType, (lead != 0 && lead == me) ? 1 : 0, leadEnded, g2,
                 (int)ko, (int)dead, g3, inType, bld, b1f0, b10, g4, intr != 0 ? 1 : 0,
                 carried, f2f8, h300Type,
                 talker, tType, tBld, (tBld != 0 && tBld == bld) ? 1 : 0,
                 b1a1, b1f0, i08, b10, (double)i28, i28bits, i28ok,
                 ownCls, ownSlot, (ownCls == 1) ? 1 : 0);
}
/* PROBE-END: P114 */
void TalkTestDrain()
{
    TalkOffDrain();   /* P26 stage 0 fold 1: the off-thread starts, every K2 safe point */
    TkSafePoint();    /* P26 stages 1-3: every K2 safe point */
    TalkSightTick();  /* P26 stage 6 talksight: one try per K2 safe point while armed */
    TkArrWatchTick(); /* P26s6 arrival probe: the leader's walk and the event-1 arrival */
    if (::InterlockedExchange(&g_talkTestPending, 0) == 0) return;
    if (EngineWritesBlocked()) { TalkTestSkip("engine writes are blocked (world loading or tearing down)"); return; }
    if (kDialogueSendEventRva == 0 || g_base == 0) { TalkTestSkip("DialogueSendEvent is not in the address table"); return; }
    ::Character* ref = 0; Ogre::Vector3 rpos;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap && ref == 0; ++i)
    {
        unsigned int u = 0; ::Character* c = 0; ::Faction* f = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || !net::IsUidMine(u)) continue;
        if (!CrimeFactionOf(c, &f) || f == 0 || !IsPlayerFaction(f)) continue;
        if (!SafeReadPosition(c, &rpos)) continue;
        ref = c;
    }
    ::Character* target = 0; unsigned int tuid = 0; float tdist = -1.0f;
    if (g_talkTestTargetUid != 0)
    {
        target = FindSpawned(g_talkTestTargetUid);
        if (target == 0 || SayPlausiblePtr(target) == 0) { char nb[80]; std::sprintf(nb, "target uid %u is not here", g_talkTestTargetUid); TalkTestSkip(nb); return; }
        tuid = g_talkTestTargetUid;
    }
    else
    {
        if (ref == 0) { TalkTestSkip("no own player-faction character with a readable position to search near"); return; }
        float best = kTalkNearMax;
        for (int i = 0; i < cap; ++i)
        {
            unsigned int u = 0; ::Character* c = 0; ::Faction* f = 0; Ogre::Vector3 q;
            if (!MirrorSlot(i, &u, &c) || u == 0 || net::IsUidMine(u)) continue;
            if (!CrimeFactionOf(c, &f) || f == 0 || !IsPeerFaction(f)) continue;
            if (!SafeReadPosition(c, &q)) continue;
            const float dx = q.x - rpos.x, dz = q.z - rpos.z, d = sqrtf(dx * dx + dz * dz);
            if (d < best || (d == best && target != 0 && u < tuid)) { best = d; target = c; tuid = u; }
        }
        if (target == 0) { TalkTestSkip("no character of the other player within 3000 u of this game's first own player character"); return; }
        tdist = best;
    }
    Ogre::Vector3 tpos;
    if (!SafeReadPosition(target, &tpos)) { TalkTestSkip("the target's position is unreadable"); return; }
    ::Character* npc = 0; float ndist = -1.0f;
    if (g_talkTestNpcUid != 0)
    {
        npc = FindSpawned(g_talkTestNpcUid);
        if (npc == 0 || SayPlausiblePtr(npc) == 0) { char nb[80]; std::sprintf(nb, "NPC uid %u is not here", g_talkTestNpcUid); TalkTestSkip(nb); return; }
    }
    else
    {
        if (coop::GameWorldPtr() == 0) { TalkTestSkip("no GameWorld to walk"); return; }
        const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
        if (all.size() > 20000) { TalkTestSkip("the character update list has an implausible size"); return; }
        float best = kTalkNearMax;
        for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
        {
            ::Character* c = *it; ::Faction* f = 0; Ogre::Vector3 q;
            if (c == target || SayPlausiblePtr(c) == 0 || !CrimeFactionOf(c, &f) || f == 0) continue;
            if (IsPlayerFaction(f) || IsPeerFaction(f)) continue;
            if (g_talkTestNearCopy) { const unsigned int cu = FindSpawnedUid(c); if (cu == 0 || net::IsUidMine(cu)) continue; }   /* P25: a copy the other game drives */
            if (CrimeIsDead(c) != 0 || IsDownedCharacter(c)) continue;
            if (TalkDialogueOf(c) == 0 || !SafeReadPosition(c, &q)) continue;
            const float dx = q.x - tpos.x, dz = q.z - tpos.z, d = sqrtf(dx * dx + dz * dz);
            if (d < best) { best = d; npc = c; }
        }
        if (npc == 0) { TalkTestSkip("no living, conscious non-player character with a Dialogue within 3000 u of the target"); return; }
        ndist = best;
    }
    void* dlg = TalkDialogueOf(npc);
    if (dlg == 0) { TalkTestSkip("the NPC's Dialogue does not read as one"); return; }
    unsigned int nuid = 0, tu2 = 0;
    const int nk = TalkKindOf(npc, &nuid);
    const int tk = TalkKindOf(target, &tu2);
    char nn[48], tn[48];
    TalkName(npc, nn);
    TalkName(target, tn);
    const long long s0 = g_talkStarted, p0 = g_talkPlayerStarts, w0 = g_talkWindow, c0 = g_talkCalls;
    const SendEventFn fn = (SendEventFn)(g_base + (uintptr_t)kDialogueSendEventRva);   /* the engine's entry (hooked: counted like any call) */
    { char p114[2048]; P114GateLine(dlg, npc, target, nuid, (int)g_talkTestEvent, p114); DebugLog(std::string(p114)); }   /* PROBE P114 */
    int result = 0;
    g_talkLever = 1;
    const int called = TalkSendPod(fn, dlg, target, g_talkTestEvent, &result);
    g_talkLever = 0;
    g_talkCurEvent = -1;   /* a fault inside would have skipped detour_sendEvent's restore */
    ++g_talkTestRan;
    if (!called) ++g_talkTestFaulted;
    char b[640];
    std::sprintf(b, "[TALK] talktest %s ev=0x%X npc uid=%u %s '%s' dist=%.1f -> target uid=%u %s '%s' dist=%.1f: sendEvent=%d;"
                 " during the call starterCalls=%lld started=%lld playerStarted=%lld windowWouldOpen=%lld host=%d",
                 called ? "SENT" : "FAULTED", (unsigned int)g_talkTestEvent, nuid, coopsay::TalkTargetName(nk), nn, ndist,
                 tuid, coopsay::TalkTargetName(tk), tn, tdist, result, g_talkCalls - c0, g_talkStarted - s0,
                 g_talkPlayerStarts - p0, g_talkWindow - w0, net::SessionIsHost() ? 1 : 0);
    DebugLog(std::string(b));
}

static std::string TalkByEventString(const long long* counts)
{
    std::string s;
    for (int i = 0; i < coopsay::kTalkEventBuckets; ++i)
    {
        if (counts[i] == 0) continue;
        char b[48];
        if (i == coopsay::kTalkEventBuckets - 1) std::sprintf(b, "%sother:%lld", s.empty() ? "" : ",", counts[i]);
        else std::sprintf(b, "%s0x%X:%lld", s.empty() ? "" : ",", (unsigned int)i, counts[i]);
        s += b;
    }
    return s;
}

/* P26 stage 0 fold 1: the off-thread part of the REPORT line. */
static std::string TalkOffCountsString()
{
    long long se[coopsay::kTalkEventBuckets], ne[coopsay::kTalkEventBuckets];
    for (int i = 0; i < coopsay::kTalkEventBuckets; ++i)
    {
        se[i] = (long long)g_talkOffStartedByEvent[i];
        ne[i] = (long long)g_talkOffNotStartedByEvent[i];
    }
    const long long* t = g_talkOffByTarget;
    const long long* s = g_talkOffBySpeaker;
    char b[900];
    std::sprintf(b, " offThread[started,notStarted,drained,stale,dropped]=%lld,%lld,%lld,%lld,%lld"
                 " offThreadMore[noLine,whoNull,playerCalls,otherTid,startedDirect,notStartedDirect]=%lld,%lld,%lld,%lld,%lld,%lld"
                 " offThreadTid=%lu(%s)"
                 " offStartedByTarget[none,mine,partnerCopy,npcMine,npcCopy,unread,stale]=%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " offStartedBySpeaker[none,mine,partnerCopy,npcMine,npcCopy,unread,stale]=%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                 (long long)g_talkOffStarted, (long long)g_talkOffNotStarted, g_talkOffDrained, g_talkOffStale,
                 (long long)g_talkOffDropped, (long long)g_talkOffNoLine, (long long)g_talkOffWhoNull,
                 (long long)g_talkOffPlayerCalls, (long long)g_talkOffOtherTid, (long long)g_talkOffStartedDirect,
                 (long long)g_talkOffNotStartedDirect, (unsigned long)g_talkOffTid, EngineThreadNameOf((unsigned long)g_talkOffTid),
                 t[0], t[1], t[2], t[3], t[4], t[5], t[6], s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
    return std::string(b) + " offStartedByEvent=[" + TalkByEventString(se) + "] offNotStartedByEvent=[" + TalkByEventString(ne) + "]";
}

std::string TalkCountsString()
{
    char b[640];
    std::sprintf(b, "talk[started,withCopy,withMine,windowWouldOpen,withNpc,noTarget,bySpeakerCopy,playerStarted,direct,"
                 "calls,notStarted,noLine,offThread,leverStarted]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                 g_talkStarted, g_talkWithCopy, g_talkWithMine, g_talkWindow, g_talkWithNpc, g_talkNoTarget, g_talkBySpeakerCopy,
                 g_talkPlayerStarts, g_talkDirect, g_talkCalls, g_talkNotStarted, g_talkNoLine, (long long)g_talkOffThread,
                 g_talkLeverStarts);
    char h[200];
    std::sprintf(h, " talkHooks=conv:%s,player:%s talktest[armed,ran,skipped,faulted]=%lld,%lld,%lld,%lld",
                 TalkHookState(g_startConvHook), TalkHookState(g_startPlayerConvHook),
                 g_talkTestArmed, g_talkTestRan, g_talkTestSkipped, g_talkTestFaulted);
    return std::string(b) + " talkByEvent=[" + TalkByEventString(g_talkByEvent) + "] sendEventWouldBlockByEvent=["
           + TalkByEventString(g_copyEvByEvent) + "]" + h + TalkOffCountsString() + TkCountsString() + TalkSightCounts()   /* P26 stage 0 fold 1; P26 stages 1-3; P26 stage 6 talksight */
           + " " + InteriorKeepToken();   /* P25 interior keep (replicate.cpp) */
}

std::string SayCountsString()
{
    char b[512];
    std::sprintf(b, "%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld sayHook=%s sendEventHook=%s",
                 (long long)g_saySent, (long long)g_sayTooLong, (long long)g_sayPassOffThread,
                 (long long)g_sayPassNoUid, (long long)g_sayPassReplay,
                 (long long)g_sayLocalPuppetDropped, (long long)g_sayApplied, (long long)g_sayUnknownUid,
                 (long long)g_sayNoDialogue, (long long)g_sayMalformed,
                 (long long)g_saySendEventBlocked, (long long)g_sayDropClearedLine,
                 (long long)g_sayReplaySent, (long long)g_sayReplayDropped, (long long)g_sayDroppedBlocked,
                 g_sayHook == 1 ? "installed" : (g_sayHook == -1 ? "FAILED" : "notInstalled"),
                 g_sendEventHook == 1 ? "installed" : (g_sendEventHook == -1 ? "FAILED" : "notInstalled"));
    return std::string(b);
}

}   /* namespace coop */
