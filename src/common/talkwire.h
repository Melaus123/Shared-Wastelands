/* src/common/talkwire.h - P26 stages 1-3 (.modding/investigations/p26-npc-dialogue.md Q3): THE MSG_TALK PAYLOAD (59, RELIABLE).
 *
 * An NPC's conversation with the OTHER player's character opens THAT player's own dialogue window. The NPC's owner runs the
 * conversation and sends PROMPT; the addressed character's owner shows its own window and sends ANSWER (the clicked reply, or
 * CLOSED / BUSY / NO_CHAR / SHOW_FAILED); the NPC's owner sends END when its side ends (speech.cpp 'P26 STAGES 1-3').
 *
 *   u8 kind | u32 convId | u32 npcUid | u32 targetUid | the kind's fields:
 *     1 PROMPT  u8 event | s16 lineSid | s16 npcText | u8 n (<= 10) | n x (s16 replyId | s16 replyText) | u32 deadlineMs
 *     2 ANSWER  u8 result | i32 index | s16 replyId | i32 purse (the talker's purse on its own game, -1 unknown)
 *     3 END     u8 reason   (... P25 fields; protocol 194, T-858: u8 count | count x u32 unrun ACT seq, only when count > 0 - talkrefund.h)
 *     4 ACT, 5 ACT_RESULT - P26 stage 5 (protocol 103), below the END reasons
 *     6 REQUEST - P25 (protocol 109), below; P25 also appends u32 reqId to PROMPT and u32 reqId | u8 why | u8 endsType to END;
 *       T-478 step 3 (protocol 199) appends s16 lineSid to END after endsType, before T-858's optional unrun list (the line whose 43 BAIL
 *       the talker's game runs; empty otherwise)
 *     7 CURSOR_ASK, 8 CURSOR_ANSWER - T-737, the talk cursor (talkcursor.h), below REQUEST
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

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "talkrefund.h"   /* T-858: kTalkMaxUnrun, the END's unrun list */

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
/* T-858 (T1914): the talker's game ended the conversation first (its ANSWER CLOSED / BUSY / NO_CHAR / SHOW_FAILED for the first
   line) - its window is gone already, so this END closes nothing there: it carries the unrun list, so the talker's game refunds what
   it took for paid lines whose NPC-side part never ran. A receiver that has no mirrored window by that convId ignores the rest. */
const int kTalkEndTalkerClosed   = 8;

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
       kept on the NPC's faction relations +0x78 +0x10: 1271 - Inferred the player's), 47 SURRENDER (T-478 step 4: TalkActChainsTalkers).
     HIRE   3 JOIN_SQUAD_WITH_EDIT / 18 JOIN_SQUAD_FAST (1210-1241: PlayerInterface::recruit into THIS game's squad, or the NPC joins
       the target's faction) - hire.cpp's owner-applied hire road on the target's owner; a TAKE_MONEY in the same line is its price.
     REFUSE (target-side, not applied by the router - the target's owner answers UNSUPPORTED, the NPC's owner does not run it):
       1 TRADE (endDialogue + the trade window for the target: 1567), 11 CHARACTER_EDITOR (the editor for the target: 1721),
       24 GIVE_TARGET_MY_SLAVES (the NPC squad's slaves handed to the target: 1737), 41-48 CHOOSE_* (selection windows over the
       player's squad: 712-1159) - but 43 BAIL (959-1052), T-478 step 3: kTalkSideEnds (TalkActEndsTalk below), and 47 SURRENDER
       (722-760), T-478 step 4: APPLY - it opens no window.
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
const int kTalkSideEnds    = 5;   /* T-478 step 3: 43 BAIL - never sent in an ACT and never run on the NPC's game; the NPC's game ends
                                     the conversation (END endsType 43, the line's id with it) and the talker's game runs the engine's
                                     own case on its own mirrored conversation just before that window closes (TalkActEndsTalk).
                                     T-478 step 5: 41 SELL SLAVES the same way (END endsType 41, TalkEndsLocal kTalkLocalSell) */
inline int TalkActSide(int type)
{
    switch (type)
    {
    case 4: case 5: case 8: case 9: case 13: case 14: case 23: case 25: case 32: case 35: case 47: case 49: case 53:
        return kTalkSideApply;   /* 47 SURRENDER: T-478 step 4 (TalkActChainsTalkers) */
    case 3: case 18:
        return kTalkSideHire;
    case 10: case 30: case 40:
        return kTalkSideCarried;
    case 41: case 42: case 43: case 48:
        return kTalkSideEnds;   /* T-478 step 3 (43 BAIL), step 5 (41 SELL SLAVES), step 6 (42 BUY SLAVES, 48 BUY ANIMALS) */
    case 1: case 11: case 24: case 44: case 45: case 46:
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

/* P26s5 fold 1: an action that ends the conversation in the engine - never sent, never run; the NPC's owner ends it (END ENGINE).
   T-478 step 3 (manager 2026-11-03: the bail window opens on the TALKER's game, as TRADE): 43 BAIL joins them. The engine's own case
   (67fad0:959-1052) does not end the conversation; it lists the prisoners of the NPC's town that the target may bail and opens the
   bail window (0x6AC080) for the target - on the NPC's game that would be the other player's COPY there, paying from the wrong purse.
   So the NPC's game ends the conversation with END endsType 43 and the line's id, and the talker's game runs that one case on its
   own mirrored conversation (its own character, its own copy of the NPC and town) before closing it (speech.cpp TkApplyEnd). The
   confirm (0x6AE960) then charges the ticked rows' prices once on that game (its own player money, 6ae960:64-66 / 136-137) and frees
   them there; a freed copy of another game's character goes to its owner as "@bail" (spawn.cpp detour_bailConfirm). */
/* T-478 step 5 (design read 2026-11-04, D1): 41 SELL SLAVES joins them. The engine's case (67fad0:897-951, Read) lists the TALKER's
   own squad members (getSquad 0x790F70, +0x58 / +0x60) that are alive (0x620B20) and closer than 300 units (_DAT_14168cc94, read in
   the 1.0.65 exe) to the NPC, and opens the selling window (0x6A7740, CharacterTrading_SlaveSelling) with the NPC as the buyer. Its
   confirm (vtable entry 0, 0x6A7F30) pays each ticked row's price into the LOCAL player's purse and chains each row's character to
   the NPC (setChainedMode 0x32E550, as surrender); the slaver's purse is never read. On the NPC's game every row would be a copy
   and the money the wrong purse, so the NPC's game ends the conversation (END endsType 41 + the line's id) and the seller's game
   runs the case on its own mirrored conversation (speech.cpp TkSellOpenLocal) and its own confirm (spawn.cpp detour_sellConfirm);
   each sold character's MSG_SLAVE carries the slave state and the slaver as owner to the other game. No new message. */
/* T-478 step 6 (design C, 2026-11-05): 42 BUY SLAVES / 48 BUY ANIMALS join them. The engine's case opens the slave buying window
   (0x6A8160, vtable 0x16FE930) or the animal buying window (0x6A9C30, vtable 0x16FE9A8) listing the NPC's slaves / animals (67fad0:783,
   :1066, Read). Each confirm (vtable entry 0: 0x6AE510 slaves, 0x6AB410 animals) charges every ticked row's price from the LOCAL
   player's purse (global 0x2133630), even for a row whose character is missing, and pays nobody; the animal one first checks the
   character limit (a refusal returns 0 and keeps the window open) and recruits each animal (0x693010); the slave one clears the
   slave's owner (0x32E3F0), gives it a squad of its own (0x5CDA50), clears its prison mode (0x3305C0), sends talk event 0x33, gives
   order 0x85 and makes two relation changes (Read, decomp_6ae510 / 6ab410). On the NPC's game the buyer would be the other player's
   copy and the money this game's purse, so the NPC's game ends the conversation (END endsType 42 / 48 + the line's id) and the
   buyer's game runs the case on its own mirrored conversation (speech.cpp TkBuyOpenLocal). Its confirm is held (hire.cpp
   BuyConfirm): every ticked row whose character another game runs is asked of that game on the hire road (join type 42 / 48); only
   when every owner said yes does this game take the characters and run the engine's own confirm, once (TalkBuyConfirmGate). */
inline int TalkActEndsTalk(int type) { return (type == 1 || type == 11 || type == 41 || type == 42 || type == 43 || type == 48) ? 1 : 0; }
/* The END names the line for the window types whose case the talker's game runs from that line (41 SELL SLAVES, 42 / 48 BUY, 43 BAIL). */
inline int TalkEndCarriesLine(int endsType) { return (endsType == 41 || endsType == 42 || endsType == 43 || endsType == 48) ? 1 : 0; }
inline int TalkActBuys(int type) { return (type == 42 || type == 48) ? 1 : 0; }
/* T-478 step 4: 47 SURRENDER (67fad0:722-760, Read) opens no window and changes only the TARGET's side: it lists every character of
   the target's faction (target vt +0x58, that faction's vt +8 list) and, for each one that is not held / carried (vt +0x248 == 0),
   within the case's distance of the NPC and passing the case's other two checks (0x5C8CC0 / 0x854A90 for a downed one, 0x42E930),
   chains it with the NPC as owner (setChainedMode 0x32E550, on = 1, the NPC's hand +0x58) and gives it a memory of the NPC
   (0x672EA0 on its AI +0x98). setChainedMode also adds the slave to the OWNER's slave list and sets its owner faction
   (decomp_32e550:50-58, Read) - the one write the case makes on the NPC's side, and it lands on the game that runs the case. The
   checks (67fad0:740-756): a sneaking character (0x5C8CC0 = not in melee 0x436270 and the sneaking byte Character +0xD4) is taken
   only when 0x854A90 finds it in the NPC's own memory (0x854840); vt +0x248 must be 0 (not held); the case's range; the race test
   0x42E930. So it is APPLY: the talker's game runs the engine's own case on its own mirrored conversation (its own characters, its
   copy of the NPC as their owner - that copy's slave list gets the entry there, and that copy's memory answers the sneaking test);
   each chained own character goes through the capture hook's own-character pass (items.cpp detour_capChain: run, MSG_SLAVE marked),
   and MSG_SLAVE brings slave state 1 and ownerUid = the NPC to every copy - on the NPC's game changeSlaveOwner puts each copy in the
   real NPC's slave list (CaptureApplyOwnerToCopy), the state the capture road's CHAIN request leaves there. A forwarded line's 47 is
   never run on the NPC's game. When the router did NOT forward the conversation, the engine runs the case there over that game's
   view of the target's faction: a copy's chain becomes a CHAIN request its owner refuses as NOT_DOWN (items.cpp, the downed rule) -
   counted (talkSurrender notForwardedA), nothing changes. Known limits: a third game that has no copy of the NPC cannot apply the
   owner (kept pending until the copy exists); the case's memory of the NPC stays on the talker's game only. */
inline int TalkActChainsTalkers(int type) { return type == 47 ? 1 : 0; }
/* T-858 TEST-ONLY (`talktest nearestcarrier <act>`): 1 = a line whose own actions (types[0..n)) hold `act` together with at least one
   action the NPC's own game runs (TalkActSide kTalkSideNpc, not 0 NONE) and no hire (3 / 18) or carried-person (10 / 30 / 40)
   action - for TAKE_MONEY (8) a paid line the NPC's game HOLDS until the talker's ACT_RESULT, so an END that leaves it unrun lists
   it (speech.cpp TkUnrunFor: a hold with no NPC-side part is never listed; a hire's price is taken later, outside the ACT). */
inline int TalkCarrierLineFits(const int* types, int n, int act)
{
    int has = 0, npc = 0;
    for (int i = 0; i < n; ++i)
    {
        const int s = TalkActSide(types[i]);
        if (s == kTalkSideHire || s == kTalkSideCarried) return 0;
        if (types[i] == act) has = 1;
        if (s == kTalkSideNpc && types[i] != 0 && types[i] != act) npc = 1;
    }
    return (has && npc) ? 1 : 0;
}

/* P25 (protocol 109; .modding/investigations/p25-dialogue-actions-2026-09-30.md): A PLAYER'S CONVERSATION WITH AN NPC THE OTHER GAME
   OWNS. The talker's game (B) does not run it: its click on the NPC's copy becomes a REQUEST, and the NPC's owner (A) starts the
   conversation on its REAL NPC with its copy of B's character - the P26 road from there (PROMPT / ANSWER / ACT / END), so the NPC's
   orders, locks, memory and campaigns change on A and B's own consequences come back to B as ACT.
     6 REQUEST  u32 reqId | u8 event | i32 purse   (header: convId 0, npcUid = the NPC, targetUid = the talker - B's own character;
                purse: the talker's purse on its own game, -1 unknown - the NPC's game answers its money condition with it)
   PROMPT gains a last field u32 reqId (the REQUEST it answers; 0 = a conversation the NPC's game started itself). END gains
   u32 reqId | u8 why | u8 endsType: reason 7 NOT_STARTED (convId 0) names the REQUEST and why it was not started (kTalkWhy*);
   endsType 1 TRADE / 11 CHARACTER_EDITOR = the line that ended the conversation opens that window, which the talker's game opens
   ITSELF (manager decision 2026-09-30 (2)). The new fields are required: a 107 / 108 game never links (HELLO refuses).
   T-478 step 3 (protocol 199): endsType 43 BAIL, and END's s16 lineSid (after endsType) names that line (TalkActEndsTalk).
   T-478 step 5 (protocol 208): endsType 41 SELL SLAVES names its line the same way (TalkEndCarriesLine). */
const int kTalkRequest = 6;
const unsigned int kTalkReqWaitMs = 30000;   /* B: a REQUEST with no PROMPT / END within this is given up (noAnswer; nothing opens) */

/* T-737 - THE TALK CURSOR (talkcursor.h): the hover on an NPC another game drives asks that game whether its NPC would talk to the
   asker's character now; the answer decides the cursor and the click's talk order there.
     7 CURSOR_ASK     u32 askId | i32 purse   (header: convId 0, npcUid = the NPC, targetUid = the talker - the asker's own character;
                      purse: the talker's purse on its own game, -1 unknown - the NPC's game answers its money condition with it)
     8 CURSOR_ANSWER  u32 askId | u8 answer   (header: convId 0, npcUid and targetUid as the ASK's; answer kTalkCursor* below)
   askId (TalkMsg::reqId) is the asker's own number, never 0; the answer rides TalkMsg::result. */
const int kTalkCursorAsk    = 7;
const int kTalkCursorAnswer = 8;
const int kTalkCursorNo      = 0;   /* nothing to say: the ordinary cursor, and the click gives no talk order */
const int kTalkCursorTalk    = 1;   /* a line was found: the talk cursor */
const int kTalkCursorParked  = 2;   /* the NPC holds a parked interrupt line: the s_talk cursor */
const int kTalkCursorUnknown = 3;   /* the NPC or the talker's copy is not there */
const int kTalkCursorAnswerMax = 3;

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
const int kTalkLocalNone = 0, kTalkLocalTrade = 1, kTalkLocalEditor = 2, kTalkLocalBail = 3, kTalkLocalSell = 4, kTalkLocalBuy = 5;   /* 3: T-478 step 3; 4: step 5; 5: step 6 */
inline int TalkEndsLocal(int endsType)
{
    return endsType == 1 ? kTalkLocalTrade : (endsType == 11 ? kTalkLocalEditor
         : (endsType == 43 ? kTalkLocalBail : (endsType == 41 ? kTalkLocalSell : (TalkActBuys(endsType) ? kTalkLocalBuy : kTalkLocalNone))));
}
/* T-478 steps 3 / 5, B: the bail (43) or selling (41) window opens only when the END names the line (the case is run from that
   line) and this game still mirrors that very conversation with its own character in it (the engine's case reads the
   conversation's target). */
inline int TalkEndsOpenOk(int endsType, const std::string& lineSid, int mirrorLive, int handIsOwn)
{
    return (TalkEndCarriesLine(endsType) && !lineSid.empty() && mirrorLive && handIsOwn) ? 1 : 0;
}
/* T-478 step 5, B (the seller's game), before the selling case runs: the engine lists every squad member near the slaver and the
   confirm sells every ticked row with no way to drop one here, so the WHOLE window is refused when any member it could list is not
   this game's own character (another game's copy - selling it would chain a stand-in and pay this purse for a character this game
   does not own - or a member with no uid). This far (x, z) from the slaver counts as listed: twice the engine's 300 units, so a
   member at the edge is never missed (too far only refuses more windows, never sells a copy). */
const float kTalkSellRowNearUnits = 600.0f;
inline int TalkSellWindowOk(int notOwnNear) { return notOwnNear == 0 ? 1 : 0; }
/* T-478 step 5 (T2017: B's confirm stayed disabled): the selling window's refresh (0x727690) enables its confirm only when a row is
   ticked AND the ticked rows' total is no more than the buyer's money - the slaver's squad pot (Character vtable +0x1B8 = 0x790AC0,
   the value KeeperPot reads). B's END line names that pot as this game's copy reads it and the cheapest listed row's price (the
   handler's rows: +0x20 array, +0x28 count, 0x68 a row, the price at +0x20). TalkSellCheapest: the lowest price of n rows (a
   negative one is not a price; -1 = none). TalkSellMoneyWord: "covers" = the pot pays the cheapest row (ticking it enables the
   confirm), "short" = it does not (the confirm stays disabled), "unread" = the pot or every price is unknown. */
inline int TalkSellCheapest(const int* prices, int n)
{
    int best = -1;
    for (int i = 0; i < n; ++i) if (prices[i] >= 0 && (best < 0 || prices[i] < best)) best = prices[i];
    return best;
}
inline const char* TalkSellMoneyWord(int haveMoney, int money, int cheapest)
{
    if (haveMoney == 0 || cheapest < 0) return "unread";
    return (money >= cheapest) ? "covers" : "short";
}
/* T-478 step 5, B: the selling confirm (0x6A7F30) of a window this game opened for a slaver another game runs.
   The note (spawn.cpp g_sellArm) names that window: its handler (the confirm's own object, CharacterTradingWindow +0xD0) and the
   window itself, which the engine keeps as its ONE current trading window (row TradingWindowCurrent: set by the window's
   constructor 0x729D50, which first destroys any older one, and cleared by its destructor 0x727550 - Read 2026-11-04). Every end of
   the window - its confirm, its cancel button, a hide, a newer trading window, the world's end - runs that destructor (725ca0 /
   9e03 -> 725d00 -> 73ddc0 -> 727550, Read), so the note is cleared when the window is no longer the current one (TalkSellNoteEnded,
   SlaveTick) and a confirm counts as the armed window's only while it still is (TalkSellArmedWindow, read live at the confirm).
   The link's drop does NOT clear the note: the window stays open and its confirm must still be refused (kTalkSellNoLink).
   Re-check fold (2026-11-04): there is no fall-back to the buyer's address. Right after the case ran, the note is tied to the window
   (TalkSellBindOutcome): TIED - the current window's handler sells to this slaver's copy; NO WINDOW - no current window, or the
   current one's handler is missing or sells to another buyer, so no window sells to this copy: the note is cleared and the window is
   refused as never opened; UNTIED - a read of the window, its handler or its buyer faulted (or the note names another slaver): it
   cannot be told which window sells to this copy, so the note is kept REFUSING - every confirm while that window is current (or,
   with no window read, until the note ends) is refused, nothing of it runs. Returned 1 = the tied window's confirm, 2 = a confirm the
   refusing note covers, 0 = neither (the engine's own sale). */
const int kTalkSellBindNoWindow = 0, kTalkSellBindTied = 1, kTalkSellBindUntied = 2;
inline int TalkSellBindOutcome(int winOk, unsigned long long win, int handlerOk, unsigned long long handler, int buyerOk, unsigned long long buyer,
                               unsigned long long npc, unsigned long long armNpc)
{
    if (!winOk || npc == 0 || armNpc != npc) return kTalkSellBindUntied;
    if (win == 0) return kTalkSellBindNoWindow;
    if (!handlerOk) return kTalkSellBindUntied;
    if (handler == 0) return kTalkSellBindNoWindow;
    if (!buyerOk) return kTalkSellBindUntied;
    return buyer == npc ? kTalkSellBindTied : kTalkSellBindNoWindow;
}
inline int TalkSellArmedWindow(unsigned long long armWin, unsigned long long armHandler, unsigned long long winNow, unsigned long long self,
                               unsigned long long armNpc, int armRefusing)
{
    if (armNpc == 0) return 0;
    if (armRefusing) return (armWin == 0 || winNow == armWin) ? 2 : 0;
    return (armWin != 0 && armHandler != 0 && self == armHandler && winNow == armWin) ? 1 : 0;
}
inline int TalkSellNoteEnded(unsigned long long armNpc, unsigned long long armWin, unsigned long long winNow, int worldGone)
{
    if (armNpc == 0) return 0;
    return (worldGone || (armWin != 0 && winNow != armWin)) ? 1 : 0;
}
/* armedWindow: TalkSellArmedWindow; 0 = a sale to an NPC this game runs (single player's road, unchanged). armedNpc / slaver: the
   slaver copy's address recorded at the open and the handler's buyer (+8) now, compared as addresses only (it may point at a
   retired copy, class F459). The armed sale runs only when the registry still maps that address to the slaver's uid (the copy was
   not retired: no dereference), the slaver is still another game's (slaverOurs: read live at the confirm - a slaver this game now
   runs is the engine's own sale, kTalkSellNowOurs) and the session is up (the direct link or the world road); else nothing of it
   runs - no money, no one chained - and the engine's own caller destroys the handler and closes the window (725ca0:8-16, Read).
   armedWindow 2 (a refusing note's window, re-check fold): refused whatever the slaver's state (kTalkSellUntied). */
const int kTalkSellNotOurs = 0, kTalkSellRun = 1, kTalkSellCopyGone = 2, kTalkSellNoLink = 3, kTalkSellNowOurs = 4, kTalkSellUntied = 5;
inline int TalkSellConfirmGate(int armedWindow, unsigned long long armedNpc, unsigned long long slaver, unsigned int armedUid, unsigned int uidNow,
                               int slaverOurs, int linked)
{
    if (!armedWindow) return kTalkSellNotOurs;
    if (armedWindow == 2) return kTalkSellUntied;
    if (armedNpc == 0 || slaver != armedNpc) return kTalkSellCopyGone;
    if (armedUid == 0 || uidNow != armedUid) return kTalkSellCopyGone;
    if (slaverOurs) return kTalkSellNowOurs;
    if (!linked) return kTalkSellNoLink;
    return kTalkSellRun;
}
inline const char* TalkSellGateName(int g)
{
    return g == kTalkSellRun ? "runs" : (g == kTalkSellCopyGone ? "the slaver's copy here was retired (its game left the area or the session) or is not the window's buyer"
         : (g == kTalkSellNoLink ? "the session is down (no direct link and no world road)"
         : (g == kTalkSellNowOurs ? "the slaver is now run by this game (the engine's own sale)"
         : (g == kTalkSellUntied ? "the window could not be tied to the sale note when it opened (refused as never opened)" : "not a window opened for another game's slaver"))));
}

/* T-478 step 6 (design C), the buyer's game: the slave / animal buying confirm (hire.cpp BuyConfirm, the hook on vtable entry 0).
   Each ticked row (the handler's rows: +0x20 array, +0x28 count, 0x68 a row - a hand at +0, the price at +0x20, ticked at +0x24, as
   0x725690 reads them) is classed by its character as this game reads it now: OWN (this game runs it, no other game's request holds
   it), OTHER (another game runs it), RESERVED (this game runs it but another game's request holds it - that game is buying it), NO UID
   (a character the other games do not know) or GONE (the row's character does not resolve - the engine would still charge it). */
const int kTalkBuyRowOwn = 0, kTalkBuyRowOther = 1, kTalkBuyRowReserved = 2, kTalkBuyRowNoUid = 3, kTalkBuyRowGone = 4;
inline int TalkBuyRowClass(int found, unsigned int uid, int mine, int reservedHere)
{
    if (!found) return kTalkBuyRowGone;
    if (uid == 0) return kTalkBuyRowNoUid;
    if (mine) return reservedHere ? kTalkBuyRowReserved : kTalkBuyRowOwn;
    return kTalkBuyRowOther;
}
/* The confirm's verdict, checked in this order:
     spent    - this very window's purchase already ran (its close did not take): returned 1 so the engine's caller closes the window;
                nothing is charged again;
     asking   - this window's owners are being asked: a second click asks nobody again and charges nothing;
     rows     - the ticked rows do not read: refused;
     reserved - a row this game runs is promised to another game's purchase: refused (the other buyer was first);
     unread   - in a session (session: the link or a dial is up, or a ticked row has a uid), a row with no uid or no character:
                refused, before the own-rows verdict - the engine would charge a missing row's price for nobody (B's copy of another
                game's slave removed while B's window lists it, alone or beside B's own rows), and a row with no uid cannot be asked
                of anyone; single player (no session, no uids) keeps the engine's own confirm unchanged;
     no OTHER - every row is this game's own (single player, or the NPC's own game): the engine's own confirm, unchanged;
     no link  - no road to the other games: refused;
     untied   - the confirm's handler is not the engine's current trading window's (+0xD0): refused (the window could not be closed);
     no hirer - this game has no own living player character to name as the buyer: refused;
     no room  - the animal window's ticked rows would pass the game's character limit (room 0; -1 = unread, left to the engine): refused.
   Otherwise ASK: the owners are asked on the hire road and the confirm is held (returned 0: the window stays open). Every refusal
   charges nothing, moves nobody and keeps the window open (0), as the engine's own limit refusal does. */
const int kTalkBuyEngine = 0, kTalkBuyAsk = 1, kTalkBuyAsking = 2, kTalkBuySpent = 3, kTalkBuyRowsUnread = 4, kTalkBuyReserved = 5,
          kTalkBuyUnread = 6, kTalkBuyNoLink = 7, kTalkBuyUntied = 8, kTalkBuyNoHirer = 9, kTalkBuyNoRoom = 10;
inline int TalkBuyConfirmGate(int spent, int asking, int rowsRead, int nOther, int nReserved, int nNoUid, int nGone, int linked, int tied,
                              int hirer, int room, int session)
{
    if (spent) return kTalkBuySpent;
    if (asking) return kTalkBuyAsking;
    if (!rowsRead) return kTalkBuyRowsUnread;
    if (nReserved > 0) return kTalkBuyReserved;
    if (nGone > 0) return kTalkBuyUnread;   /* a ticked row with no character: the engine would charge it for nobody, in a session or not */
    if ((session || nOther > 0) && nNoUid > 0) return kTalkBuyUnread;
    if (nOther <= 0) return kTalkBuyEngine;
    if (!linked) return kTalkBuyNoLink;
    if (!tied) return kTalkBuyUntied;
    if (!hirer) return kTalkBuyNoHirer;
    if (room == 0) return kTalkBuyNoRoom;
    return kTalkBuyAsk;
}
/* A mixed batch's OWN row (one this game runs), after the engine's confirm ran inside the own-road depth (hire.cpp BuyRun): the recruit
   guard sent no DONE for it, so BuyRun sends the one the guard would have sent (EngineRecruitSettle's road: DONE taken 0 joined 1 to
   every game, each re-files its copy) - when the row joined this game's player faction in the confirm (not in it before, in it after).
   An animal joins by the engine's recruit; a slave by the slave confirm, read the same way. */
inline int TalkBuyOwnRowDone(int cls, int wasInMine, int nowInMine)
{
    return (cls == kTalkBuyRowOwn && !wasInMine && nowInMine) ? 1 : 0;
}
inline const char* TalkBuyGateName(int g)
{
    switch (g)
    {
    case kTalkBuyEngine:     return "every row is this game's own (the engine's own purchase)";
    case kTalkBuyAsk:        return "asks each row's owner game";
    case kTalkBuyAsking:     return "this window's owners are already being asked (a second click asks nobody again)";
    case kTalkBuySpent:      return "this window's purchase already ran (nothing is charged again; the window closes)";
    case kTalkBuyRowsUnread: return "the window's ticked rows do not read";
    case kTalkBuyReserved:   return "a row this game runs is promised to another game's purchase";
    case kTalkBuyUnread:     return "a ticked row has no character here or no uid (it cannot be asked of its owner; the engine would charge it for nobody)";
    case kTalkBuyNoLink:     return "no road to the other games (the link is down)";
    case kTalkBuyUntied:     return "the confirm is not the engine's current trading window's";
    case kTalkBuyNoHirer:    return "this game has no own living player character to buy for";
    case kTalkBuyNoRoom:     return "the ticked animals would pass the game's character limit";
    default:                 return "?";
    }
}
/* The asked owners' answers (hire.cpp BuyDrain): every row OK = all taken may run; any NO, or an ask that ended unanswered (its
   owner's game left: HireForgetPeer dropped the request), = refused; otherwise wait. Nothing asked = refused. */
const int kTalkBuyWait = 0, kTalkBuyAllOk = 1, kTalkBuyFailed = 2;
inline int TalkBuyBatchVerdict(int asked, int ok, int no, int lost)
{
    if (asked <= 0 || no > 0 || lost > 0) return kTalkBuyFailed;
    return ok >= asked ? kTalkBuyAllOk : kTalkBuyWait;
}
/* hire.cpp's buying batch (T-478 step 6). The constructor and Reset() set every field by hand: under VS2010 `TalkBuyBatch()`
   of a struct with a vector member and no constructor left the plain fields as they were (run T2051: a reset batch kept
   active=1 and a junk buyer uid, and BuyDrain refused it about 25 500 times). */
struct TalkBuyRow { unsigned int uid; unsigned int reqId; int price; int state; unsigned int gen; unsigned int peer; int noReason; };   /* state 0 asked, 1 OK, 2 NO, 3 lost */
struct TalkBuyBatch
{
    int active; int joinType; void* handler; void* win; unsigned int hirerUid; int nRows; std::vector<TalkBuyRow> rows;
    TalkBuyBatch() : active(0), joinType(0), handler(0), win(0), hirerUid(0), nRows(0), rows() {}
    void Reset() { active = 0; joinType = 0; handler = 0; win = 0; hirerUid = 0; nRows = 0; rows.clear(); }
};
/* The same set of uids, whatever the order (the ticked OTHER rows now against the rows asked). */
inline int TalkBuyUidsSame(std::vector<unsigned int> a, std::vector<unsigned int> b)
{
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b ? 1 : 0;
}
/* Just before the take (every owner said yes): the window is still the one asked for, its ticked rows are the very rows asked (and
   nothing else another game runs or promised), the road is open, every asked character is up and nobody's player character, and
   (animals) the limit has room. Otherwise nothing is taken: each owner's promise is cleared (DONE taken 0) and the window stays. */
const int kTalkBuyRunGo = 0, kTalkBuyRunWindowGone = 1, kTalkBuyRunRowsChanged = 2, kTalkBuyRunNoLink = 3, kTalkBuyRunDown = 4, kTalkBuyRunNoRoom = 5;
inline int TalkBuyRunGate(int windowSame, int rowsSame, int linked, int allUp, int room)
{
    if (!windowSame) return kTalkBuyRunWindowGone;
    if (!rowsSame) return kTalkBuyRunRowsChanged;
    if (!linked) return kTalkBuyRunNoLink;
    if (!allUp) return kTalkBuyRunDown;
    if (room == 0) return kTalkBuyRunNoRoom;
    return kTalkBuyRunGo;
}
inline const char* TalkBuyRunName(int r)
{
    return r == kTalkBuyRunGo ? "runs" : (r == kTalkBuyRunWindowGone ? "the window was closed or replaced while the owners were asked"
         : (r == kTalkBuyRunRowsChanged ? "the ticked rows changed while the owners were asked (confirm again)"
         : (r == kTalkBuyRunNoLink ? "the road to the other games closed" : (r == kTalkBuyRunDown ? "an asked character is down, gone or now a player's"
         : (r == kTalkBuyRunNoRoom ? "the ticked animals would now pass the game's character limit" : "?")))));
}

/* The engine's money condition (DialogConditionEnum 2) as Dialogue's condition check answers it (0x670AA0, Read
   build/decomp_670aa0.txt): compare 1 (less than) = purse < val; any other compare = val <= purse (an equal purse passes). */
inline bool TalkMoneyCond(int cmp, int val, int purse)
{
    return cmp == 1 ? (purse < val) : (val <= purse);
}

/* The talker's purse after its game applies an ACT's actions {types, values} (speech.cpp TkApplyAct): that game refuses the whole
   ACT when the line's TAKE_MONEY (8) total - a hire's price included - is more than its purse (NO_MONEY: unchanged); otherwise
   TAKE_MONEY comes off and GIVE_MONEY (9) goes on. A TAKE_MONEY in a line with a hire (3 / 18) is the hire's price: it comes off
   later on the hire road, only when the NPC's owner agrees - not counted here. 10 PAY_BOUNTY's reward is the carried person's
   bounty, which the sender does not know - not counted. A result below 0 = -1 (unknown); above the int range = its top.
   purse < 0 (unknown) is returned as it is. */
inline int TalkPurseAfterAct(int purse, const std::vector<int>& types, const std::vector<int>& values)
{
    if (purse < 0) return purse;
    const size_t n = types.size() < values.size() ? types.size() : values.size();
    int hire = 0;
    for (size_t i = 0; i < n; ++i) if (TalkActSide(types[i]) == kTalkSideHire) hire = 1;
    long long take = 0, give = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (types[i] == 8) take += values[i];
        else if (types[i] == 9) give += values[i];
    }
    if (take > (long long)purse) return purse;
    const long long r = (long long)purse + give - (hire ? 0 : take);
    if (r < 0) return -1;
    if (r > (long long)0x7FFFFFFF) return 0x7FFFFFFF;
    return (int)r;
}

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
const int kTalkDecodeBadValue = 6;  /* T-737: a CURSOR_ASK / CURSOR_ANSWER with askId 0 or no NPC / talker uid, or an answer over 3 */

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
    int purse;                                  /* REQUEST / ANSWER: the talker's purse on its own game, -1 unknown - the NPC's game answers its money condition with it */
    std::vector<unsigned int> unrun;            /* END (game protocol 194, T-858 - talkrefund.h): the ACT seqs whose NPC-side part this end
                                                   leaves unrun; the talker's game refunds what it took for them. <= kTalkMaxUnrun */
    TalkMsg() : kind(0), convId(0), npcUid(0), targetUid(0), event(0), deadlineMs(0), result(0), index(-1), reason(0),
                seq(0), detail(0), moneyBefore(-1), moneyAfter(-1), applied(0), unsupported(0), anyApplied(0),
                reqId(0), why(0), endsType(0), carriedUid(0), purse(-1) {}
};

inline const char* TalkKindName(int k)
{
    return k == kTalkPrompt ? "PROMPT" : (k == kTalkAnswer ? "ANSWER" : (k == kTalkEnd ? "END"
         : (k == kTalkAct ? "ACT" : (k == kTalkActResult ? "ACT_RESULT" : (k == kTalkRequest ? "REQUEST"
         : (k == kTalkCursorAsk ? "CURSOR_ASK" : (k == kTalkCursorAnswer ? "CURSOR_ANSWER" : "?")))))));   /* ACT / ACT_RESULT: P26 stage 5; REQUEST: P25; CURSOR_*: T-737 */
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
    case kTalkEndTalkerClosed: return "TALKER_CLOSED";   /* T-858 (T1914) */
    default:                return "?";
    }
}

/* T-858 (T1914): A - the END reason for an ANSWER that ends this side (every result but REPLY, which goes on). Before, CLOSED / BUSY /
   NO_CHAR / SHOW_FAILED for the first line sent no END (the talker's window was gone), so a paid line whose NPC-side part never ran
   (its ACT_RESULT lost) was never listed and never refunded - T1914 lost 200 cats. Now each sends END with the unrun list:
   TALKER_CLOSED; SHOW_FAILED for a NEXT line (seq > 1) stays ENGINE_ENDED (P26s4 fold 1 M2: that window may still be open there
   and must close). 0 = not an end (REPLY). */
inline int TalkEndForAnswer(int result, unsigned int seq)
{
    if (result == kTalkAnsReply) return 0;
    if (result == kTalkAnsShowFailed && seq > 1) return kTalkEndEngine;
    return kTalkEndTalkerClosed;
}

/* T-858 (T1914): B - 1 = an END that names no mirrored window here is expected, not unknown: TALKER_CLOSED follows this game's own
   close (or its refusal to open the window), so its window is gone by design; only its unrun list matters. */
inline int TalkEndExpectsNoWindow(int reason) { return reason == kTalkEndTalkerClosed ? 1 : 0; }

/* T-1187: A - what a forwarded conversation's end does while lines of it are held (TkHoldEndFor). held = its held lines (waiting for
   their ACT_RESULT), already = its end is held already. 0 = no held line: the end runs now; 1 = the end is held now on the newest
   held line; 2 = already held (asked again). The NPC-side actions the held lines keep are NOT an input: the engine's _doActions ends
   EVERY line with 0x2732F0 (Read, build/decomp_2732f0.txt: the line's 'change AI' and 'AI contract' references put on the NPC's
   squad, the talker as the contract's party - a hire), which the held run (TkRunViewPod side 0) makes even with no action kept; an
   end let through before it (T1979: a hire line, nothing NPC-side) took the money and nobody joined. */
inline int TalkHoldEndDecide(int held, int already) { return held <= 0 ? 0 : (already ? 2 : 1); }

/* T-1187 review fold 1: A - may a held line be listed as NOT RUN on its conversation's END (the talker's game then refunds the money
   it took for it, talkrefund.h)? Only when the line has an NPC-side part this game would lose: npcActs = its held NPC-side actions,
   aiRef = its GameData's 'change AI' / 'AI contract' reference lists (the lists the end call 0x2732F0 / 0x272700 reads, F1051):
   1 one is not empty (a hire's contract), 0 both empty, -1 unread (listed: the part cannot be ruled out). A line whose whole effect
   lands on the talker (an item handed over for money) is never listed: the talker's game applies it whenever its answer comes, so a
   refund would leave it the item AND the money. 1 = may be listed. */
inline int TalkHoldMayListUnrun(unsigned int npcActs, int aiRef) { return (npcActs > 0 || aiRef != 0) ? 1 : 0; }

/* T-1187 review fold 2: A - after APPLIED, the held run (TkRunViewPod side 0) answered ran (1 ran, 0 no trampoline, -1 the engine
   faulted, -2 the line did not read). 1 = the line counts as NOT RUN: its seq goes on the conversation's next END as unrun (the
   dropped-line note road, no new message) so the talker's game refunds it - only when it did not run, has an NPC-side part to lose
   (TalkHoldMayListUnrun) and is no carried-person line (takeOrders: the take still follows APPLIED, never both refunded and taken). */
inline int TalkHeldRunUnrun(int ran, unsigned int npcActs, int aiRef, int takeOrders)
{
    return (ran != 1 && takeOrders == 0 && TalkHoldMayListUnrun(npcActs, aiRef)) ? 1 : 0;
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
    if (out == 0 || m.kind < kTalkPrompt || m.kind > kTalkCursorAnswer) return false;   /* 1..8 (4 / 5: P26 stage 5; 6 REQUEST: P25; 7 / 8: T-737) */
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
        TalkPut32(&b, (unsigned int)m.purse);
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
        TalkPut32(&b, (unsigned int)m.purse);
    }
    else if (m.kind == kTalkCursorAsk || m.kind == kTalkCursorAnswer)   /* T-737 */
    {
        if (m.reqId == 0 || m.npcUid == 0 || m.targetUid == 0) return false;
        if (m.kind == kTalkCursorAnswer && (m.result < 0 || m.result > kTalkCursorAnswerMax)) return false;
        TalkPut32(&b, m.reqId);
        if (m.kind == kTalkCursorAsk) TalkPut32(&b, (unsigned int)m.purse);
        else TalkPut8(&b, (unsigned int)m.result);
    }
    else
    {
        if (m.why < 0 || m.why > 255 || m.endsType < 0 || m.endsType > 255) return false;   /* P25 */
        TalkPut8(&b, (unsigned int)m.reason);
        TalkPut32(&b, m.reqId);   /* P25: END - reqId | why | endsType */
        TalkPut8(&b, (unsigned int)m.why);
        TalkPut8(&b, (unsigned int)m.endsType);
        if (!TalkPutStr(&b, m.lineSid, kTalkMaxId)) return false;   /* T-478 step 3 (protocol 199): END - the line's id (empty: none), before the unrun list */
        if (m.unrun.size() > (size_t)kTalkMaxUnrun) return false;   /* T-858: u8 count | count x u32 seq - only when there is one, so */
        if (!m.unrun.empty())                                        /* an END with nothing unrun ends at the line id */
        {
            TalkPut8(&b, (unsigned int)m.unrun.size());
            for (size_t i = 0; i < m.unrun.size(); ++i) TalkPut32(&b, m.unrun[i]);
        }
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
    if (kind < (unsigned int)kTalkPrompt || kind > (unsigned int)kTalkCursorAnswer) return kTalkDecodeBadKind;   /* 1..8 (4 / 5: P26 stage 5; 6: P25; 7 / 8: T-737) */
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
        if (!TalkGet32(&r, &v)) return r.err;
        t.purse = (int)v;
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
        if (!TalkGet32(&r, &v)) return r.err;
        t.purse = (int)v;
    }
    else if (t.kind == kTalkCursorAsk || t.kind == kTalkCursorAnswer)   /* T-737 */
    {
        if (!TalkGet32(&r, &t.reqId)) return r.err;
        if (t.kind == kTalkCursorAsk) { if (!TalkGet32(&r, &v)) return r.err; t.purse = (int)v; }
        else { if (!TalkGet8(&r, &v)) return r.err; t.result = (int)v; }
        if (t.reqId == 0 || t.npcUid == 0 || t.targetUid == 0 || (t.kind == kTalkCursorAnswer && t.result > kTalkCursorAnswerMax))
            return kTalkDecodeBadValue;
    }
    else
    {
        unsigned int w = 0, et = 0;   /* P25: END - reqId | why | endsType */
        if (!TalkGet8(&r, &v) || !TalkGet32(&r, &t.reqId) || !TalkGet8(&r, &w) || !TalkGet8(&r, &et)
            || !TalkGetStr(&r, &t.lineSid, kTalkMaxId)) return r.err;   /* T-478 step 3 (protocol 199): END's line id */
        t.reason = (int)v; t.why = (int)w; t.endsType = (int)et;
        if (r.at < r.n)   /* T-858 (protocol 194): the unrun ACT seqs; an END without them names none */
        {
            unsigned int cnt = 0;
            if (!TalkGet8(&r, &cnt)) return r.err;
            if (cnt > kTalkMaxUnrun) return kTalkDecodeTooMany;
            for (unsigned int i = 0; i < cnt; ++i)
            {
                unsigned int s = 0;
                if (!TalkGet32(&r, &s)) return r.err;
                t.unrun.push_back(s);
            }
        }
    }
    *m = t;
    return kTalkDecodeOk;
}

/* THE talkprompt LEVER (TEST-ONLY, speech.cpp): the argument shapes. */
const int kTalkLeverStart  = 0;   /* talkprompt [npcUid|near|name:<Name>[#k]] [targetUid|near] [lineSid|auto] (name: TalkPromptNameForm) */
const int kTalkLeverAnswer = 1;   /* talkprompt answer <0..9> */
const int kTalkLeverClose  = 2;   /* talkprompt close */
const int kTalkLeverAnswerAct = 3;   /* talkprompt answer act <type 0..255>: *index carries the dialogue action type */
const int kTalkLeverJoinAct = 4;   /* talkprompt joinact <type 0..255>: *index carries the dialogue action type */

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
    if (!tok.empty() && tok[0] == "joinact")
    {
        /* the line to run is the one in the conversation's whole line tree whose actions hold this dialogue action (speech.cpp) */
        if (tok.size() != 2 || tok[1].empty() || tok[1].size() > 3) return 0;
        int v = 0;
        for (size_t k = 0; k < tok[1].size(); ++k)
        {
            if (tok[1][k] < '0' || tok[1][k] > '9') return 0;
            v = v * 10 + (tok[1][k] - '0');
        }
        if (v > 255) return 0;
        *mode = kTalkLeverJoinAct;
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

/* T-478 (TEST-ONLY): `talkprompt name:<Name>[#k] [targetUid|near] [lineSid|auto]` - the NPC by its display name ('_' = a space;
   `#k` = the k-th nearest), because a trader's uid is made during a run and `near` takes whoever stands nearest. 0 = not the name
   form (the first word does not start with `name:`); 1 = *name = the text after `name:` (raw, `#k` kept; speech.cpp keys it and
   picks the character), *rest = the words after it, which parse as talkprompt's start form; -1 = malformed (an empty name, an
   unresolved harness placeholder holding '{' or '}', or a rest that is not a start form). */
inline int TalkPromptNameForm(const std::string& arg, std::string* name, std::string* rest)
{
    name->clear(); rest->clear();
    size_t i = 0;
    while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t')) ++i;
    if (arg.compare(i, 5, "name:") != 0) return 0;
    size_t j = i + 5;
    while (j < arg.size() && arg[j] != ' ' && arg[j] != '\t') ++j;
    const std::string nm = arg.substr(i + 5, j - i - 5);
    if (nm.empty() || nm[0] == '#' || nm.find('{') != std::string::npos || nm.find('}') != std::string::npos) return -1;
    while (j < arg.size() && (arg[j] == ' ' || arg[j] == '\t')) ++j;
    const std::string r = arg.substr(j);
    int mode = 0, idx = -1;
    unsigned int n = 0, t = 0;
    std::string sid;
    if (!TalkPromptParse("1 " + r, &mode, &n, &t, &sid, &idx) || mode != kTalkLeverStart) return -1;
    *name = nm; *rest = r;
    return 1;
}

/* T-726: what became of one talk order this game's player gave (speech.cpp's talk-order ring, TalkOrderDrain). set = an outcome the
   click's own road wrote (taken for the NPC's game as a REQUEST, the click ring full, the same click already waiting, this game's
   camera gate refused it; 0 none); reached = startPlayerConversation ran on the NPC's Dialogue inside that sendEvent; hadLine =
   _chooseDialog handed it a line; started = it returned true. Not reached = the engine refused before startPlayerConversation: sendEvent goes on to it for
   event 1 whether or not the NPC's Dialogue is busy, so a busy Dialogue is never the cause by itself (the caller shows it beside
   the refusal). kTalkOrderPlayerSpeaker = startPlayerConversation was reached and refused because the speaker is another player's
   character. kTalkOrderWindowBusy (T-978 B2, owner 1445 a) = it was reached and refused because this game's conversation window
   shows another live conversation. The kinds stay on this game: the talk-order ring and its log lines only, never a message. */
const int kTalkOrderNotReached = 0, kTalkOrderStarted = 1, kTalkOrderNoLine = 2, kTalkOrderIntercepted = 3, kTalkOrderGate = 4,
          kTalkOrderRingFull = 5, kTalkOrderDup = 6, kTalkOrderLineNotStarted = 7, kTalkOrderPlayerSpeaker = 8, kTalkOrderWindowBusy = 9,
          kTalkOrderKinds = 10;
inline int TalkOrderOutcome(int set, int reached, int hadLine, int started)
{
    if (set != 0) return set;
    if (!reached) return kTalkOrderNotReached;
    if (started) return kTalkOrderStarted;
    if (!hadLine) return kTalkOrderNoLine;
    return kTalkOrderLineNotStarted;
}

/* T-726: 1 = write this talk order's line. The first 5 lines of a session always; after that one per NPC per 10 s. lastMs = when
   this NPC's last line was written (0 = never - the caller stores 1 for a tick of 0); nLogged = the lines written so far. The caller
   counts the orders it skipped and writes "(n more since)" on the next line. Wrap-safe. */
const unsigned int kTalkOrderLogFirst = 5, kTalkOrderLogGapMs = 10000;
inline int TalkOrderLogDue(unsigned lastMs, unsigned nowMs, unsigned nLogged)
{
    if (nLogged < kTalkOrderLogFirst) return 1;
    if (lastMs == 0) return 1;
    return (unsigned)(nowMs - lastMs) >= kTalkOrderLogGapMs ? 1 : 0;
}

/* T-719 / T-726: 1 = the n-th line of one kind is written (n counts from 1): the first 5, then every 100th. */
inline int TalkLogNthDue(long long n) { return (n >= 1 && (n <= 5 || n % 100 == 0)) ? 1 : 0; }

/* Which conversation `talkprompt answer <k>` / `answer act <type>` / `close` acts on (the talker's game, at the safe point):
   the mirrored one while it is open; otherwise, for answer, answer act, joinact and close, the one this game's own dialogue
   window shows (ownShown 1) when its owner is a live character whose Dialogue is still that one (ownOwnerLive 1). close on the
   own window is a test lever: it ends this game's own conversation (e.g. the one left open over the character editor after
   joinact ran a line's actions). *why names a refusal. */
const int kLeverWinNone = 0, kLeverWinMirror = 1, kLeverWinOwn = 2;
inline int LeverWindowPick(int mode, int mirrorActive, int ownShown, int ownOwnerLive, const char** why)
{
    *why = "";
    if (mirrorActive != 0)
    {
        /* joinact runs a line's actions on this game's own conversation only - a mirrored copy's actions belong to the NPC's game */
        if (mode == kTalkLeverJoinAct) { *why = "joinact runs on this game's own window only and a mirrored conversation is open"; return kLeverWinNone; }
        return kLeverWinMirror;
    }
    if (mode != kTalkLeverAnswer && mode != kTalkLeverAnswerAct && mode != kTalkLeverJoinAct && mode != kTalkLeverClose) { *why = "no mirrored conversation is open on this game"; return kLeverWinNone; }
    if (ownShown != 1) { *why = "no mirrored conversation is open and this game's own window shows no conversation"; return kLeverWinNone; }
    if (ownOwnerLive != 1) { *why = "no mirrored conversation is open and the own window's conversation has no live owner"; return kLeverWinNone; }
    return kLeverWinOwn;
}

}   /* namespace cooptalk */
