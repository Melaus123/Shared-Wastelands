/* src/common/talkwire.h - P26 stages 1-3 (.modding/investigations/p26-npc-dialogue.md Q3): THE MSG_TALK PAYLOAD (59, RELIABLE).
 *
 * An NPC's conversation with the OTHER player's character opens THAT player's own dialogue window. The NPC's owner runs the
 * conversation and sends PROMPT; the addressed character's owner shows its own window and sends ANSWER (the clicked reply, or
 * CLOSED / BUSY / NO_CHAR / SHOW_FAILED); the NPC's owner sends END when its side ends (speech.cpp 'P26 STAGES 1-3').
 *
 *   u8 kind | u32 convId | u32 npcUid | u32 targetUid | the kind's fields:
 *     1 PROMPT  u8 event | s16 lineSid | s16 npcText | u8 n (<= 10) | n x (s16 replyId | s16 replyText) | u32 deadlineMs
 *     2 ANSWER  u8 result | i32 index | s16 replyId
 *     3 END     u8 reason
 *     4 ACT, 5 ACT_RESULT - P26 stage 5 (protocol 103), below the END reasons
 *     6 REQUEST - P25 (protocol 109), below; P25 also appends u32 reqId to PROMPT and u32 reqId | u8 why | u8 endsType to END
 *   s16 = u16 length + that many bytes: a string id (line, reply) at most 128 bytes, a text at most 512 bytes.
 *
 * convId is the PROMPT sender's own number (never 0); the receiver keys a conversation on {peer, convId}. event 255 = the start
 * ran outside any Dialogue::sendEvent. deadlineMs = the answer deadline the sender holds (0 = none; P26s5 fold 5, owner decision
 * 229: the sender sends 0 - a conversation waits for the player's answer with no limit). A conversation's next NPC line is a new PROMPT with
 * the same convId (P26 stage 4). The length test on decode is a MINIMUM: bytes after the last field are ignored, so a later build may
 * append a field without a protocol bump.
 *
 * Built and parsed here only, so the offline suite (src/coop-test/test_main.cpp) hits the SAME bytes net/session.cpp sends and
 * reads (lesson 11). C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace cooptalk {

const int kTalkPrompt = 1;
const int kTalkAnswer = 2;
const int kTalkEnd    = 3;

const unsigned int kTalkMaxText    = 512;   /* bytes of an NPC text or a reply text */
const unsigned int kTalkMaxId      = 128;   /* bytes of a line / reply string id */
const unsigned int kTalkMaxReplies = 10;    /* replies in one PROMPT (the engine's own reply lektor starts at 10) */

/* ANSWER results */
const int kTalkAnsReply      = 0;   /* index + replyId: the reply the addressed player clicked */
const int kTalkAnsClosed     = 1;   /* the window ended without a reply */
const int kTalkAnsBusy       = 2;   /* the addressed game already shows a mirrored conversation */
const int kTalkAnsNoChar     = 3;   /* no copy of the NPC from the sender there, or the addressed character is not that game's */
const int kTalkAnsShowFailed = 4;   /* the line did not resolve there, or that engine did not open the window */

/* END reasons */
const int kTalkEndEngine    = 1;    /* the NPC's owner's engine ended the conversation (attack, knock-out, out of range, no line) */
const int kTalkEndAnswered  = 2;    /* P26 stage 4: the NPC's owner applied the answer and its engine ended the conversation inside it */
const int kTalkEndRestarted = 3;    /* the NPC started another conversation on the same Dialogue */
const int kTalkEndLink      = 4;    /* never sent: the log's name for a link loss */
/* P26 stage 4. New reason VALUES only (no new kind, no new field): DecodeTalk stores the reason byte unchecked and a receiver of any
   build closes its window on any END, so no protocol bump. */
const int kTalkEndTimeout        = 5;   /* the NPC's owner ended its side on a clock - P26s5 fold 5: only a held end that got no ACT_RESULT within 30 s */
const int kTalkEndNotForwardable = 6;   /* the answer's reply id is not in the NPC's own reply list, or the next line cannot go on the wire */
const int kTalkEndNotStarted     = 7;   /* P25 (protocol 109): the NPC's owner did not start a REQUESTed conversation - END convId 0, reqId, why */

/* P26 stage 5 (protocol 103): THE ROUTER'S TWO KINDS.
 *   4 ACT         u32 seq | s16 lineSid | u8 nActs (<= 32) | nActs x (u16 type | i32 value)
 *                 | u8 nItems (<= 16) | nItems x (s16 itemSid | i32 value) | u8 nRels (<= 16) | nRels x (s16 factionSid | i32 amount)
 *                 | u32 carriedUid (the person the addressed character carries on the NPC's owner's game; 0 = the line has no
 *                   carried-person action)
 *   5 ACT_RESULT  u32 seq | u8 result | u16 detail | i32 moneyBefore | i32 moneyAfter | u8 applied | u8 unsupported
 *                 | u8 anyApplied (P26s5 fold 1: 0 nothing, 1 some part applied, 2 unknown - the engine faulted part-way)
 * ACT: the NPC's owner sends the TARGET-side part of one line of a forwarded conversation - its DialogActionEnum actions {type, value}
 * as the line holds them (DialogLineData::actions +0x208), its givesItem hand-over (+0xA8: {item stringID, value}) and its
 * factionRelationEffects (+0x2D0: {faction stringID, amount} - relations of that faction with THE PLAYER's faction, 67fad0:371-574).
 * It ran the line's NPC-side part itself. The addressed character's owner applies the ACT to its own character and answers
 * ACT_RESULT with the ACT's seq: result (below), detail (the action type the refusal is about, 0 none), its purse before / after
 * (-1 unread), how many parts it applied and how many it does not apply (kTalkSideRefuse). */
const int kTalkAct       = 4;
const int kTalkActResult = 5;
const unsigned int kTalkMaxActs     = 32;   /* actions of one line (the engine's own list has no cap; A holds a longer line, fails closed) */
const unsigned int kTalkMaxActItems = 16;   /* givesItem entries */
const unsigned int kTalkMaxActRels  = 16;   /* factionRelationEffects entries */

/* ACT_RESULT results */
const int kTalkActApplied      = 0;   /* applied (unsupported > 0: those parts were not) */
const int kTalkActNoChar       = 1;   /* no copy of the NPC from the sender there, or the addressed character is not that game's */
const int kTalkActNoConv       = 2;   /* no mirrored window of that conversation is open there with that character */
const int kTalkActNoLine       = 3;   /* the line's string id is not in that game's dialogue data, or its copy does not read */
const int kTalkActLineDiffers  = 4;   /* that game's copy of the line has another target-side part than the ACT (other mods / data) */
const int kTalkActNoMoney      = 5;   /* the line's TAKE_MONEY total (a hire's price) is more than that game's purse */
const int kTalkActNotHere      = 6;   /* the _doActions hook / getData / the purse is not available there */
const int kTalkActFault        = 7;   /* the engine's _doActions faulted there */
const int kTalkActUnsupported  = 8;   /* every target-side part is one the router does not apply (kTalkSideRefuse) */
const int kTalkActHireNotAsked = 9;   /* the hire road did not send its request (already pending, link down, not that game's) */
const int kTalkActNoCarry      = 10;  /* a carried-person line: the addressed character does not carry the ACT's carriedUid there */

/* THE PER-ACTION TABLE (P26 stage 5). Who runs each DialogActionEnum type (0 DA_NONE .. 58 DA_KNOCKOUT)
   of a line of a forwarded conversation, read from _doActions 0x67FAD0 (build/decomp_67fad0.txt; param_1 +0x150 = the NPC 'me',
   plVar13 = the conversation target, lVar33 = the target's faction via vtable +0x58):
     APPLY  (the addressed character's owner runs the engine's own case on its own character):
       4 AFFECT_RELATIONS / 5 AFFECT_REPUTATION / 13 DECLARE_WAR / 14 END_WAR (the NPC's faction's relations object +0x78, vt +0x20 /
       +0x48 / +0x40 / +0x38, WITH THE TARGET'S FACTION: 67fad0:1243-1261), 8 TAKE_MONEY (the target faction's Ownerships +0x80,
       takeMoney vt slot 0: 1687), 9 GIVE_MONEY (target faction Ownerships +0x80 money +0x88 += value: 1635), 23 MAKE_TARGET_RUN_FASTER
       (target +0x640 obj +0x20 = 2: 1734), 25 TAG_ESCAPED_SLAVE / 53 REMOVE_SLAVE_STATUS (the target's slave state: 1533, 1325-1350),
       32 ASSIGN_BOUNTY / 49 CLEAR_BOUNTY (the target's bounty list +0xF0: 1433, 1352), 35 INCREASE_FACTION_RANK (the player's rank
       kept on the NPC's faction relations +0x78 +0x10: 1271 - Inferred the player's).
     HIRE   3 JOIN_SQUAD_WITH_EDIT / 18 JOIN_SQUAD_FAST (1210-1241: PlayerInterface::recruit into THIS game's squad, or the NPC joins
       the target's faction) - hire.cpp's owner-applied hire road on the target's owner; a TAKE_MONEY in the same line is its price.
     REFUSE (target-side, not applied by the router - the target's owner answers UNSUPPORTED, the NPC's owner does not run it):
       1 TRADE (endDialogue + the trade window for the target: 1567), 11 CHARACTER_EDITOR (the editor for the target: 1721),
       24 GIVE_TARGET_MY_SLAVES (the NPC squad's slaves handed to the target: 1737), 41-48 CHOOSE_* (selection windows over the
       player's squad: 712-1159).
     NPC    everything else: the NPC, its squad, its faction's own state, the dialogue's own state - 2 TALK_TO_LEADER, 6 / 55 / 57
       attacks, 7 GO_HOME, 12, 15, 16, 17, 19 REMEMBER, 20 / 21 temp ally / enemy (the NPC squad's memory), 28, 29 ARREST_TARGET,
       33 CRIME_ALARM (it writes no bounty itself: the guards that answer its alarm assign one to the copy, and crime.cpp sends a
       copy's bounty additions to the character's owner), 34 RUN_AWAY, 36-39, 52, 54, 56, and 58 DA_KNOCKOUT - which
       knocks out THE NPC ITSELF (param_1 +0x150 +0x4F8 knockoutClock = value: 1321), not the target. Types with no case (0, 22,
       26, 27, 31, 50, 51) and unknown types do nothing in _doActions.
   Special cases:
     CARRIED 10 PAY_BOUNTY, 30 ARREST_TARGETS_CARRIED_PERSON and 40 ENSLAVE_TARGETS_CARRIED_PERSON act on the person the TARGET
       CARRIES (the hand at target +0x380: 1640-1643, 1366, 1397). 10: if that person's bounty for the NPC's law faction is not
       claimed yet, its amount (halved for a dead body) goes into the target faction's purse, the entry is marked claimed and
       'Received c.N reward.' is shown (1644-1672); then the NPC is ordered to take the person (0xD5) and cage it (0x70) and the
       person remembers both (1673-1682). 30 (in the NPC's own town) / 40: the person is split into a squad of its own (0x5CDA50),
       the same two orders and memories, and a unique person's imprisoned state (0x34ADC0) (1365-1403). Each part runs on the
       game that owns its state - see TalkCarriedNpcEngine / TalkCarriedTalkerEngine below; the carry itself passes from the
       talker's character to the NPC on every game through the carry roads (spawn.cpp K1, arrest1) and the hand-over mark
       (TalkHandOverLetsGo).
     1 TRADE / 11 CHARACTER_EDITOR end the conversation in the engine (1567, 1721): TalkActEndsTalk - never sent in an ACT, left
       out of the target's owner's compare; the NPC's owner runs the NPC part, then ends the conversation.
     28 (1279-1287) acts on the TARGET when the NPC's faction is a player faction (+0x250 isPlayer): the NPC's owner leaves it
       out then. */
const int kTalkSideNpc     = 0;
const int kTalkSideApply   = 1;
const int kTalkSideHire    = 2;
const int kTalkSideRefuse  = 3;
const int kTalkSideCarried = 4;   /* sent in the ACT; each game runs its own share (below) */
inline int TalkActSide(int type)
{
    switch (type)
    {
    case 4: case 5: case 8: case 9: case 13: case 14: case 23: case 25: case 32: case 35: case 49: case 53:
        return kTalkSideApply;
    case 3: case 18:
        return kTalkSideHire;
    case 10: case 30: case 40:
        return kTalkSideCarried;
    case 1: case 11: case 24: case 41: case 42: case 43: case 44: case 45: case 46: case 47: case 48:
        return kTalkSideRefuse;
    default:
        return kTalkSideNpc;
    }
}

/* A CARRIED-PERSON ACTION BETWEEN GAMES. The NPC's owner (A) runs the conversation with its copy of the talker's character, which
   carries A's view of the person there. Who runs what:
     - the talker's game (B) runs the engine's own case on its own character for 10 always: the reward goes into B's purse, the
       notice shows on B's screen, and B marks its view of the person's bounty claimed (its own list when B owns the person,
       else the claim travels to the person's owner as a bounty clear). For 30 / 40 B runs the engine's case only when B owns the
       person (the squad split, the memories and the unique state are the person's owner's); otherwise nothing of the engine.
     - the NPC's owner (A) runs the engine's own case for 30 / 40 when A owns the person (no money in it). Otherwise - always
       for 10, whose engine case would pay the copy's stand-in faction and show the notice on A's screen - A gives its NPC the
       case's two orders itself: take the person (0xD5), then cage it (0x70), the engine's own arguments (67fad0:1675-1682).
     - B keeps a hand-over mark: when the NPC's copy on B is told to carry the person, B's own character lets go of it.
   personMine: the person is the asking game's own (net::IsUidMine on the carried uid). 1 = run the engine's case there. */
inline int TalkActCarried(int type) { return (type == 10 || type == 30 || type == 40) ? 1 : 0; }
inline int TalkCarriedNpcEngine(int type, int personMine) { return ((type == 30 || type == 40) && personMine) ? 1 : 0; }
inline int TalkCarriedTalkerEngine(int type, int personMine) { return (type == 10 || ((type == 30 || type == 40) && personMine)) ? 1 : 0; }
/* A, sending: the line goes out only when its copy of the talker carries a person here (carriedHere = that person's uid, 0 none). */
inline int TalkCarriedMaySend(unsigned int carriedHere) { return carriedHere != 0 ? 1 : 0; }
/* B, applying: the talker's own character must carry the very person the ACT names. */
inline int TalkCarriedApplyOk(unsigned int actCarried, unsigned int carriedHere) { return (actCarried != 0 && carriedHere == actCarried) ? 1 : 0; }

/* B's hand-over mark: its own `carrier` handed `body` to the NPC `taker` in a conversation; for kTalkHandOverMs a wish (from the NPC's
   owner's STATE) that `taker`'s copy carry `body` makes this game's `carrier` let go of it - the one exception to "a carrier this
   game drives never lets go on the other game's word". The window bounds how long B waits for the NPC, already within talking
   range, to come and take the person on its own game. */
const unsigned int kTalkHandOverMs = 60000;
const unsigned int kTalkHandOverCap = 8;
struct TalkHandOver { unsigned int body, carrier, taker; unsigned long ms; };
inline int TalkHandOverLetsGo(const TalkHandOver& h, unsigned int body, unsigned int heldBy, unsigned int taker, unsigned long nowMs)
{
    if (h.body == 0 || h.body != body || h.carrier != heldBy || h.taker != taker) return 0;
    return (unsigned long)(nowMs - h.ms) < (unsigned long)kTalkHandOverMs ? 1 : 0;
}
inline int TalkHandOverExpired(const TalkHandOver& h, unsigned long nowMs) { return (unsigned long)(nowMs - h.ms) >= (unsigned long)kTalkHandOverMs ? 1 : 0; }

/* P26s5 fold 1: an action that ends the conversation in the engine - never sent, never run; the NPC's owner ends it (END ENGINE) */
inline int TalkActEndsTalk(int type) { return (type == 1 || type == 11) ? 1 : 0; }

/* P25 (protocol 109; .modding/investigations/p25-dialogue-actions-2026-09-30.md): A PLAYER'S CONVERSATION WITH AN NPC THE OTHER GAME
   OWNS. The talker's game (B) does not run it: its click on the NPC's copy becomes a REQUEST, and the NPC's owner (A) starts the
   conversation on its REAL NPC with its copy of B's character - the P26 road from there (PROMPT / ANSWER / ACT / END), so the NPC's
   orders, locks, memory and campaigns change on A and B's own consequences come back to B as ACT.
     6 REQUEST  u32 reqId | u8 event        (header: convId 0, npcUid = the NPC, targetUid = the talker - B's own character)
   PROMPT gains a last field u32 reqId (the REQUEST it answers; 0 = a conversation the NPC's game started itself). END gains
   u32 reqId | u8 why | u8 endsType: reason 7 NOT_STARTED (convId 0) names the REQUEST and why it was not started (kTalkWhy*);
   endsType 1 TRADE / 11 CHARACTER_EDITOR = the line that ended the conversation opens that window, which the talker's game opens
   ITSELF (manager decision 2026-09-30 (2)). The new fields are required: a 107 / 108 game never links (HELLO refuses). */
const int kTalkRequest = 6;
const unsigned int kTalkReqWaitMs = 30000;   /* B: a REQUEST with no PROMPT / END within this is given up (noAnswer; nothing opens) */

/* Why a REQUEST did not start - END NOT_STARTED's why (A), and B's own refusals (link, busy, noAnswer). 0 = started. */
const int kTalkWhyBusy = 1, kTalkWhyNotOurs = 2, kTalkWhyFar = 3, kTalkWhyGate = 4, kTalkWhyNoCopy = 5, kTalkWhyLink = 6,
          kTalkWhyDown = 7, kTalkWhyEngine = 8, kTalkWhyNotForwarded = 9, kTalkWhyOff = 10, kTalkWhyNoAnswer = 11,
          kTalkWhyNoLine = 12;   /* P25 fold 2 [p25f2-why]: A - the NPC had nothing to say to the talker (appended) */
const int kTalkWhyCount = 13;
inline const char* TalkWhyName(int w)
{
    switch (w)
    {
    case kTalkWhyBusy:         return "busy";
    case kTalkWhyNotOurs:      return "notOurs";
    case kTalkWhyFar:          return "far";
    case kTalkWhyGate:         return "gate";
    case kTalkWhyNoCopy:       return "noCopy";
    case kTalkWhyLink:         return "link";
    case kTalkWhyDown:         return "down";
    case kTalkWhyEngine:       return "engine";
    case kTalkWhyNotForwarded: return "notForwarded";
    case kTalkWhyOff:          return "off";
    case kTalkWhyNoAnswer:     return "noAnswer";
    case kTalkWhyNoLine:       return "noLine";
    default:                   return "?";
    }
}

/* B, the click (Dialogue::startPlayerConversation on this game): 1 = REQUEST - the engine's start is NOT run here (no window, no
   line, no action); 0 = the engine runs it as before. speakerOwner: 1 this game drives the NPC, 0 the other game does, -1 no uid
   (not replicated - the other game has no copy). speakerPlayerCopy: the NPC is the other player's own character (a stand-in
   faction member - LEFTOVER: which package its copy offers is not traced). applying: our own apply of a PROMPT. */
inline int TalkP25Intercept(int on, int talkerOwnPlayer, int speakerOwner, int speakerPlayerCopy, int applying)
{
    return (on && talkerOwnPlayer && speakerOwner == 0 && !speakerPlayerCopy && !applying) ? 1 : 0;
}

/* P25 fold 2 [p25f2-click]: B, a start that reached the intercept - 1 = it may become a REQUEST. hasLine: this game's copy of the NPC
   found a line; ev: the event of the sendEvent the start runs inside (-1 none known). A start with a line is taken as before; a
   start with NO line only for the player's talk-to event (1): the copy's choice (its AI off, its own town / lock state) must not
   veto a conversation the real NPC on its owner's game would have - the owner decides. Any other no-line start is the engine's. */
inline int TalkP25ClickEvent(int hasLine, int ev)
{
    return (hasLine || ev == 1) ? 1 : 0;
}

/* A, a REQUEST: 0 = start it, else the FIRST reason it is not, in this order. nearNpc: the talker's copy is within the engine's
   talk gate (9216 u) of the NPC - the view-point substitute of the camera gate (speech.cpp P25). */
inline int TalkP25RequestWhy(int on, int npcOurs, int npcDown, int copyOk, int busy, int nearNpc, int eventOk)
{
    if (!on) return kTalkWhyOff;
    if (!npcOurs) return kTalkWhyNotOurs;
    if (npcDown) return kTalkWhyDown;
    if (!copyOk) return kTalkWhyNoCopy;
    if (!eventOk) return kTalkWhyEngine;
    if (busy) return kTalkWhyBusy;
    if (!nearNpc) return kTalkWhyFar;
    return 0;
}

/* P25 fold 2 [p25f2-nsw]: A, a REQUEST's start that opened nothing - the reason, in this order. called: sendEvent ran (no fault);
   endLater: the start was not forwarded; reached: startPlayerConversation ran on the NPC's Dialogue inside that sendEvent; hadLine:
   _chooseDialog handed it a line; gateRefused: the talk gate refused it. Not reached = one of sendEvent's own early exits (engine);
   no line = the NPC has nothing to say to the talker (the normal outcome, single player too). */
inline int TalkP25NotStartedWhy(int called, int endLater, int reached, int hadLine, int gateRefused)
{
    if (!called) return kTalkWhyEngine;
    if (endLater) return kTalkWhyNotForwarded;
    if (!reached) return kTalkWhyEngine;
    if (!hadLine) return kTalkWhyNoLine;
    if (gateRefused) return kTalkWhyGate;
    return kTalkWhyEngine;
}

/* B, an END's endsType: the window this game opens ITSELF after closing the mirrored one (the engine's own order: endDialogue, then
   the window - 67fad0:1567-1570 / 1721-1722). */
const int kTalkLocalNone = 0, kTalkLocalTrade = 1, kTalkLocalEditor = 2;
inline int TalkEndsLocal(int endsType) { return endsType == 1 ? kTalkLocalTrade : (endsType == 11 ? kTalkLocalEditor : kTalkLocalNone); }

/* P26s5 fold 1: ACT_RESULT anyApplied */
const int kTalkAnyNone    = 0;   /* nothing of the ACT was applied there */
const int kTalkAnyYes     = 1;   /* some part was applied there (whatever the result) */
const int kTalkAnyUnknown = 2;   /* the engine faulted part-way: some part may be applied there */

const int kTalkDecodeOk       = 0;
const int kTalkDecodeTooShort = 1;  /* fewer than the 13 fixed bytes */
const int kTalkDecodeBadKind  = 2;  /* kind is not 1..6 (4 ACT / 5 ACT_RESULT: P26 stage 5; 6 REQUEST: P25) */
const int kTalkDecodeTooLong  = 3;  /* a string's length field is over its cap */
const int kTalkDecodeTrunc    = 4;  /* the payload ends inside a field */
const int kTalkDecodeTooMany  = 5;  /* a PROMPT names more than 10 replies; an ACT more than 32 actions / 16 items / 16 relation effects */

struct TalkMsg
{
    int kind;
    unsigned int convId, npcUid, targetUid;
    int event;                                  /* PROMPT */
    std::string lineSid, npcText;               /* PROMPT */
    std::vector<std::string> replyIds, replyTexts;   /* PROMPT, the same count */
    unsigned int deadlineMs;                    /* PROMPT */
    int result;                                 /* ANSWER */
    int index;                                  /* ANSWER: the clicked reply's index, -1 none */
    std::string replyId;                        /* ANSWER */
    int reason;                                 /* END */
    unsigned int seq;                           /* ACT / ACT_RESULT (P26 stage 5): the ACT sender's number, never 0; lineSid too */
    std::vector<int> actTypes, actValues;       /* ACT: the target-side actions {type, value}, the same count */
    std::vector<std::string> itemSids;          /* ACT: givesItem, the item's stringID ... */
    std::vector<int> itemValues;                /* ... and its value, the same count */
    std::vector<std::string> relSids;           /* ACT: factionRelationEffects, the faction's stringID ... */
    std::vector<int> relValues;                 /* ... and the amount, the same count */
    int detail, moneyBefore, moneyAfter, applied, unsupported;   /* ACT_RESULT (result above) */
    int anyApplied;                             /* ACT_RESULT (P26s5 fold 1): kTalkAny* - apart from result */
    unsigned int reqId;                         /* P25: REQUEST / PROMPT / END - the talker's game's request number (0 none) */
    int why;                                    /* P25: END NOT_STARTED - kTalkWhy* */
    int endsType;                               /* P25: END - 1 TRADE / 11 CHARACTER_EDITOR: the talker's game opens that window */
    unsigned int carriedUid;                    /* ACT: the person the addressed character carries on the sender's game, 0 none */
    TalkMsg() : kind(0), convId(0), npcUid(0), targetUid(0), event(0), deadlineMs(0), result(0), index(-1), reason(0),
                seq(0), detail(0), moneyBefore(-1), moneyAfter(-1), applied(0), unsupported(0), anyApplied(0),
                reqId(0), why(0), endsType(0), carriedUid(0) {}
};

inline const char* TalkKindName(int k)
{
    return k == kTalkPrompt ? "PROMPT" : (k == kTalkAnswer ? "ANSWER" : (k == kTalkEnd ? "END"
         : (k == kTalkAct ? "ACT" : (k == kTalkActResult ? "ACT_RESULT" : (k == kTalkRequest ? "REQUEST" : "?")))));   /* ACT / ACT_RESULT: P26 stage 5; REQUEST: P25 */
}

inline const char* TalkAnswerName(int r)
{
    switch (r)
    {
    case kTalkAnsReply:      return "REPLY";
    case kTalkAnsClosed:     return "CLOSED";
    case kTalkAnsBusy:       return "BUSY";
    case kTalkAnsNoChar:     return "NO_CHAR";
    case kTalkAnsShowFailed: return "SHOW_FAILED";
    default:                 return "?";
    }
}

inline const char* TalkEndName(int r)
{
    switch (r)
    {
    case kTalkEndEngine:    return "ENGINE_ENDED";
    case kTalkEndAnswered:  return "ANSWERED";
    case kTalkEndRestarted: return "RESTARTED";
    case kTalkEndLink:      return "LINK";
    case kTalkEndTimeout:   return "TIMEOUT";            /* P26 stage 4 */
    case kTalkEndNotForwardable: return "NOT_FORWARDABLE";
    case kTalkEndNotStarted: return "NOT_STARTED";   /* P25 */
    default:                return "?";
    }
}

/* P26 stage 5: an ACT_RESULT's result as the log shows it */
inline const char* TalkActResultName(int r)
{
    switch (r)
    {
    case kTalkActApplied:      return "APPLIED";
    case kTalkActNoChar:       return "NO_CHAR";
    case kTalkActNoConv:       return "NO_CONV";
    case kTalkActNoLine:       return "NO_LINE";
    case kTalkActLineDiffers:  return "LINE_DIFFERS";
    case kTalkActNoMoney:      return "NO_MONEY";
    case kTalkActNotHere:      return "NOT_HERE";
    case kTalkActFault:        return "FAULT";
    case kTalkActUnsupported:  return "UNSUPPORTED";
    case kTalkActHireNotAsked: return "HIRE_NOT_ASKED";
    case kTalkActNoCarry:      return "NO_CARRY";
    default:                   return "?";
    }
}

/* s cut to at most cap bytes (the sender clips; the receiver's own engine shows its own text, the wire copy is for the log). */
inline std::string TalkClip(const std::string& s, unsigned int cap) { return s.size() > cap ? s.substr(0, cap) : s; }

inline void TalkPut8(std::vector<char>* b, unsigned int v) { b->push_back((char)(unsigned char)(v & 0xFFu)); }
inline void TalkPut16(std::vector<char>* b, unsigned int v)
{
    const unsigned short s = (unsigned short)v;
    const size_t at = b->size();
    b->resize(at + 2);
    std::memcpy(&(*b)[at], &s, 2);
}
inline void TalkPut32(std::vector<char>* b, unsigned int v)
{
    const size_t at = b->size();
    b->resize(at + 4);
    std::memcpy(&(*b)[at], &v, 4);
}
inline bool TalkPutStr(std::vector<char>* b, const std::string& s, unsigned int cap)
{
    if (s.size() > cap) return false;
    TalkPut16(b, (unsigned int)s.size());
    b->insert(b->end(), s.begin(), s.end());
    return true;
}

/* false (and nothing appended) when the kind is unknown, a string is over its cap, a PROMPT has more than 10 replies, or its
   id and text lists differ in length. */
inline bool EncodeTalk(std::vector<char>* out, const TalkMsg& m)
{
    if (out == 0 || m.kind < kTalkPrompt || m.kind > kTalkRequest) return false;   /* 1..6 (4 / 5: P26 stage 5; 6 REQUEST: P25) */
    std::vector<char> b;
    TalkPut8(&b, (unsigned int)m.kind);
    TalkPut32(&b, m.convId);
    TalkPut32(&b, m.npcUid);
    TalkPut32(&b, m.targetUid);
    if (m.kind == kTalkPrompt)
    {
        if (m.replyIds.size() != m.replyTexts.size() || m.replyIds.size() > (size_t)kTalkMaxReplies) return false;
        TalkPut8(&b, (unsigned int)m.event);
        if (!TalkPutStr(&b, m.lineSid, kTalkMaxId) || !TalkPutStr(&b, m.npcText, kTalkMaxText)) return false;
        TalkPut8(&b, (unsigned int)m.replyIds.size());
        for (size_t i = 0; i < m.replyIds.size(); ++i)
            if (!TalkPutStr(&b, m.replyIds[i], kTalkMaxId) || !TalkPutStr(&b, m.replyTexts[i], kTalkMaxText)) return false;
        TalkPut32(&b, m.deadlineMs);
        TalkPut32(&b, m.reqId);   /* P25: the REQUEST this PROMPT answers, 0 none */
    }
    else if (m.kind == kTalkAnswer)
    {
        TalkPut8(&b, (unsigned int)m.result);
        TalkPut32(&b, (unsigned int)m.index);
        if (!TalkPutStr(&b, m.replyId, kTalkMaxId)) return false;
    }
    else if (m.kind == kTalkAct)   /* P26 stage 5 */
    {
        if (m.actTypes.size() != m.actValues.size() || m.actTypes.size() > (size_t)kTalkMaxActs
            || m.itemSids.size() != m.itemValues.size() || m.itemSids.size() > (size_t)kTalkMaxActItems
            || m.relSids.size() != m.relValues.size() || m.relSids.size() > (size_t)kTalkMaxActRels) return false;
        TalkPut32(&b, m.seq);
        if (!TalkPutStr(&b, m.lineSid, kTalkMaxId)) return false;
        TalkPut8(&b, (unsigned int)m.actTypes.size());
        for (size_t i = 0; i < m.actTypes.size(); ++i)
        {
            if (m.actTypes[i] < 0 || m.actTypes[i] > 0xFFFF) return false;
            TalkPut16(&b, (unsigned int)m.actTypes[i]);
            TalkPut32(&b, (unsigned int)m.actValues[i]);
        }
        TalkPut8(&b, (unsigned int)m.itemSids.size());
        for (size_t i = 0; i < m.itemSids.size(); ++i)
        {
            if (!TalkPutStr(&b, m.itemSids[i], kTalkMaxId)) return false;
            TalkPut32(&b, (unsigned int)m.itemValues[i]);
        }
        TalkPut8(&b, (unsigned int)m.relSids.size());
        for (size_t i = 0; i < m.relSids.size(); ++i)
        {
            if (!TalkPutStr(&b, m.relSids[i], kTalkMaxId)) return false;
            TalkPut32(&b, (unsigned int)m.relValues[i]);
        }
        TalkPut32(&b, m.carriedUid);
    }
    else if (m.kind == kTalkActResult)   /* P26 stage 5 */
    {
        if (m.result < 0 || m.result > 255 || m.detail < 0 || m.detail > 0xFFFF || m.applied < 0 || m.applied > 255
            || m.unsupported < 0 || m.unsupported > 255 || m.anyApplied < 0 || m.anyApplied > 255) return false;   /* anyApplied: P26s5 fold 1 */
        TalkPut32(&b, m.seq);
        TalkPut8(&b, (unsigned int)m.result);
        TalkPut16(&b, (unsigned int)m.detail);
        TalkPut32(&b, (unsigned int)m.moneyBefore);
        TalkPut32(&b, (unsigned int)m.moneyAfter);
        TalkPut8(&b, (unsigned int)m.applied);
        TalkPut8(&b, (unsigned int)m.unsupported);
        TalkPut8(&b, (unsigned int)m.anyApplied);   /* P26s5 fold 1 */
    }
    else if (m.kind == kTalkRequest)   /* P25 */
    {
        if (m.event < 0 || m.event > 255) return false;
        TalkPut32(&b, m.reqId);
        TalkPut8(&b, (unsigned int)m.event);
    }
    else
    {
        if (m.why < 0 || m.why > 255 || m.endsType < 0 || m.endsType > 255) return false;   /* P25 */
        TalkPut8(&b, (unsigned int)m.reason);
        TalkPut32(&b, m.reqId);   /* P25: END - reqId | why | endsType */
        TalkPut8(&b, (unsigned int)m.why);
        TalkPut8(&b, (unsigned int)m.endsType);
    }
    out->insert(out->end(), b.begin(), b.end());
    return true;
}

struct TalkReader { const char* p; size_t n; size_t at; int err; };

inline bool TalkGet8(TalkReader* r, unsigned int* v)
{
    if (r->at + 1 > r->n) { r->err = kTalkDecodeTrunc; return false; }
    *v = (unsigned int)(unsigned char)r->p[r->at];
    r->at += 1;
    return true;
}
inline bool TalkGet16(TalkReader* r, unsigned int* v)
{
    if (r->at + 2 > r->n) { r->err = kTalkDecodeTrunc; return false; }
    unsigned short s = 0;
    std::memcpy(&s, r->p + r->at, 2);
    *v = s;
    r->at += 2;
    return true;
}
inline bool TalkGet32(TalkReader* r, unsigned int* v)
{
    if (r->at + 4 > r->n) { r->err = kTalkDecodeTrunc; return false; }
    std::memcpy(v, r->p + r->at, 4);
    r->at += 4;
    return true;
}
inline bool TalkGetStr(TalkReader* r, std::string* s, unsigned int cap)
{
    unsigned int len = 0;
    if (!TalkGet16(r, &len)) return false;
    if (len > cap) { r->err = kTalkDecodeTooLong; return false; }
    if (r->at + len > r->n) { r->err = kTalkDecodeTrunc; return false; }
    s->assign(r->p + r->at, (size_t)len);
    r->at += len;
    return true;
}

/* kTalkDecodeOk, or why not (and *m untouched). */
inline int DecodeTalk(const char* p, size_t n, TalkMsg* m)
{
    if (p == 0 || m == 0 || n < 13) return kTalkDecodeTooShort;
    TalkReader r;
    r.p = p; r.n = n; r.at = 0; r.err = kTalkDecodeOk;
    TalkMsg t;
    unsigned int kind = 0, v = 0;
    TalkGet8(&r, &kind);
    if (kind < (unsigned int)kTalkPrompt || kind > (unsigned int)kTalkRequest) return kTalkDecodeBadKind;   /* 1..6 (4 / 5: P26 stage 5; 6: P25) */
    t.kind = (int)kind;
    TalkGet32(&r, &t.convId);
    TalkGet32(&r, &t.npcUid);
    TalkGet32(&r, &t.targetUid);
    if (t.kind == kTalkPrompt)
    {
        if (!TalkGet8(&r, &v)) return r.err;
        t.event = (int)v;
        if (!TalkGetStr(&r, &t.lineSid, kTalkMaxId) || !TalkGetStr(&r, &t.npcText, kTalkMaxText)) return r.err;
        unsigned int cnt = 0;
        if (!TalkGet8(&r, &cnt)) return r.err;
        if (cnt > kTalkMaxReplies) return kTalkDecodeTooMany;
        for (unsigned int i = 0; i < cnt; ++i)
        {
            std::string id, text;
            if (!TalkGetStr(&r, &id, kTalkMaxId) || !TalkGetStr(&r, &text, kTalkMaxText)) return r.err;
            t.replyIds.push_back(id);
            t.replyTexts.push_back(text);
        }
        if (!TalkGet32(&r, &t.deadlineMs)) return r.err;
        if (!TalkGet32(&r, &t.reqId)) return r.err;   /* P25 */
    }
    else if (t.kind == kTalkAnswer)
    {
        if (!TalkGet8(&r, &v)) return r.err;
        t.result = (int)v;
        if (!TalkGet32(&r, &v)) return r.err;
        t.index = (int)v;
        if (!TalkGetStr(&r, &t.replyId, kTalkMaxId)) return r.err;
    }
    else if (t.kind == kTalkAct)   /* P26 stage 5 */
    {
        if (!TalkGet32(&r, &t.seq) || !TalkGetStr(&r, &t.lineSid, kTalkMaxId)) return r.err;
        unsigned int cnt = 0;
        if (!TalkGet8(&r, &cnt)) return r.err;
        if (cnt > kTalkMaxActs) return kTalkDecodeTooMany;
        for (unsigned int i = 0; i < cnt; ++i)
        {
            unsigned int ty = 0, va = 0;
            if (!TalkGet16(&r, &ty) || !TalkGet32(&r, &va)) return r.err;
            t.actTypes.push_back((int)ty);
            t.actValues.push_back((int)va);
        }
        if (!TalkGet8(&r, &cnt)) return r.err;
        if (cnt > kTalkMaxActItems) return kTalkDecodeTooMany;
        for (unsigned int i = 0; i < cnt; ++i)
        {
            std::string sid;
            unsigned int va = 0;
            if (!TalkGetStr(&r, &sid, kTalkMaxId) || !TalkGet32(&r, &va)) return r.err;
            t.itemSids.push_back(sid);
            t.itemValues.push_back((int)va);
        }
        if (!TalkGet8(&r, &cnt)) return r.err;
        if (cnt > kTalkMaxActRels) return kTalkDecodeTooMany;
        for (unsigned int i = 0; i < cnt; ++i)
        {
            std::string sid;
            unsigned int va = 0;
            if (!TalkGetStr(&r, &sid, kTalkMaxId) || !TalkGet32(&r, &va)) return r.err;
            t.relSids.push_back(sid);
            t.relValues.push_back((int)va);
        }
        if (!TalkGet32(&r, &t.carriedUid)) return r.err;
    }
    else if (t.kind == kTalkActResult)   /* P26 stage 5 */
    {
        unsigned int a8 = 0, d16 = 0, mb = 0, ma = 0, ap = 0, un = 0, aa = 0;   /* aa: P26s5 fold 1 anyApplied */
        if (!TalkGet32(&r, &t.seq) || !TalkGet8(&r, &a8) || !TalkGet16(&r, &d16) || !TalkGet32(&r, &mb) || !TalkGet32(&r, &ma)
            || !TalkGet8(&r, &ap) || !TalkGet8(&r, &un) || !TalkGet8(&r, &aa)) return r.err;
        t.anyApplied = (int)aa;
        t.result = (int)a8; t.detail = (int)d16; t.moneyBefore = (int)mb; t.moneyAfter = (int)ma; t.applied = (int)ap; t.unsupported = (int)un;
    }
    else if (t.kind == kTalkRequest)   /* P25 */
    {
        if (!TalkGet32(&r, &t.reqId) || !TalkGet8(&r, &v)) return r.err;
        t.event = (int)v;
    }
    else
    {
        unsigned int w = 0, et = 0;   /* P25: END - reqId | why | endsType */
        if (!TalkGet8(&r, &v) || !TalkGet32(&r, &t.reqId) || !TalkGet8(&r, &w) || !TalkGet8(&r, &et)) return r.err;
        t.reason = (int)v; t.why = (int)w; t.endsType = (int)et;
    }
    *m = t;
    return kTalkDecodeOk;
}

/* THE talkprompt LEVER (TEST-ONLY, speech.cpp): the argument shapes. */
const int kTalkLeverStart  = 0;   /* talkprompt [npcUid|near] [targetUid|near] [lineSid|auto] */
const int kTalkLeverAnswer = 1;   /* talkprompt answer <0..9> */
const int kTalkLeverClose  = 2;   /* talkprompt close */
const int kTalkLeverAnswerAct = 3;   /* talkprompt answer act <type 0..255>: *index carries the dialogue action type */

/* "near" -> 0; a decimal or 0x-hex uid 1..0xFFFFFFFF. 1 parsed, 0 refused. */
inline int TalkLeverUid(const std::string& s, unsigned int* out)
{
    if (s == "near") { *out = 0; return 1; }
    size_t i = 0;
    unsigned int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; i = 2; }
    if (i >= s.size()) return 0;
    unsigned long long v = 0;
    for (; i < s.size(); ++i)
    {
        const char c = s[i];
        unsigned int d = 0;
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
        else return 0;
        v = v * base + d;
        if (v > 0xFFFFFFFFull) return 0;
    }
    if (v == 0) return 0;
    *out = (unsigned int)v;
    return 1;
}

/* 1 parsed, 0 malformed. start: defaults near near auto (lineSid empty = auto); a line sid is at most 128 bytes. */
inline int TalkPromptParse(const std::string& arg, int* mode, unsigned int* npcUid, unsigned int* targetUid, std::string* lineSid, int* index)
{
    *mode = kTalkLeverStart; *npcUid = 0; *targetUid = 0; lineSid->clear(); *index = -1;
    std::vector<std::string> tok;
    size_t i = 0;
    while (i < arg.size())
    {
        while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t')) ++i;
        if (i >= arg.size()) break;
        size_t j = i;
        while (j < arg.size() && arg[j] != ' ' && arg[j] != '\t') ++j;
        tok.push_back(arg.substr(i, j - i));
        i = j;
    }
    if (tok.size() == 3 && tok[0] == "answer" && tok[1] == "act")
    {
        /* the reply to click is the one whose line carries this dialogue action (speech.cpp picks it on the mirrored window) */
        if (tok[2].empty() || tok[2].size() > 3) return 0;
        int v = 0;
        for (size_t k = 0; k < tok[2].size(); ++k)
        {
            if (tok[2][k] < '0' || tok[2][k] > '9') return 0;
            v = v * 10 + (tok[2][k] - '0');
        }
        if (v > 255) return 0;
        *mode = kTalkLeverAnswerAct;
        *index = v;
        return 1;
    }
    if (!tok.empty() && tok[0] == "answer")
    {
        if (tok.size() != 2 || tok[1].size() != 1 || tok[1][0] < '0' || tok[1][0] > '9') return 0;
        *mode = kTalkLeverAnswer;
        *index = tok[1][0] - '0';
        return 1;
    }
    if (!tok.empty() && tok[0] == "close")
    {
        if (tok.size() != 1) return 0;
        *mode = kTalkLeverClose;
        return 1;
    }
    if (tok.size() > 3) return 0;
    if (tok.size() >= 1 && !TalkLeverUid(tok[0], npcUid)) return 0;
    if (tok.size() >= 2 && !TalkLeverUid(tok[1], targetUid)) return 0;
    if (tok.size() == 3 && tok[2] != "auto")
    {
        if (tok[2].size() > (size_t)kTalkMaxId) return 0;
        *lineSid = tok[2];
    }
    return 1;
}

}   /* namespace cooptalk */
