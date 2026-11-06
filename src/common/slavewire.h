/* src/common/slavewire.h - slave1 (T337/T341, Confirmed: a slave owned by game A read SlaveStateEnum 1 on A all run while
 * B's copy switched to 2 ESCAPING on its own). MSG_SLAVE (52, reliable) carries the owner's slave state for one character -
 * after every SPAWN the owner sends, and when the owner's engine changes it (StateBroadcastData::setSlaveState 0x5A3EB0 and
 * the direct writes in StateBroadcastData::periodicUpdate 0x5A44C0) - and the other game sets it on its copy.
 *
 *   uid u32 | state u8 (SlaveStateEnum: 0 NOT_SLAVE, 1 IS_SLAVE, 2 ESCAPING_SLAVE, 3 EX_SLAVE) | ownerUid u32
 *
 * P11 (protocol 101): ownerUid = the replicated uid of the character the slave-owner hand (Character +0x328) names;
 * 0 = none, kSlaveOwnerUnknown = an owner that is not a replicated character (the receiver leaves its copy's hand alone).
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <vector>

namespace coopslave {

const int    kSlaveStateMax = 3;   /* EX_SLAVE */
const size_t kSlaveWireSize = 9;   /* P11 (protocol 101): + u32 ownerUid */
const unsigned int kSlaveOwnerUnknown = 0xFFFFFFFFu;

const int kSlaveDecodeOk       = 0;
const int kSlaveDecodeTooShort = 1;
const int kSlaveDecodeBadState = 2;   /* state byte above kSlaveStateMax */

/* false (nothing appended) for a state outside 0..kSlaveStateMax. */
inline bool EncodeSlave(std::vector<char>* b, unsigned int uid, int state, unsigned int ownerUid)
{
    if (b == 0 || state < 0 || state > kSlaveStateMax) return false;
    const size_t at = b->size();
    b->resize(at + kSlaveWireSize);
    char* p = &(*b)[at];
    std::memcpy(p, &uid, 4);
    p[4] = (char)(unsigned char)state;
    std::memcpy(p + 5, &ownerUid, 4);
    return true;
}

/* Nothing is written on a refusal. */
inline int DecodeSlave(const char* p, size_t size, unsigned int* uid, int* state, unsigned int* ownerUid)
{
    if (p == 0 || uid == 0 || state == 0 || ownerUid == 0 || size < kSlaveWireSize) return kSlaveDecodeTooShort;
    const int s = (int)(unsigned char)p[4];
    if (s > kSlaveStateMax) return kSlaveDecodeBadState;
    std::memcpy(uid, p, 4);
    *state = s;
    std::memcpy(ownerUid, p + 5, 4);
    return kSlaveDecodeOk;
}

/* ---- a slave-state change made on a COPY reaches the character's owner (MSG_PRISON kind kPrisonSlaveAsk) ----
   The copy's engine still never changes the copy itself (slave1's refusal stands); the change is ASKED of the owner, who makes it
   on its own character through the engine's setter, and its MSG_SLAVE then brings every copy to it. Only the game that runs the
   character's area asks: there the guards and the slave owner's people are run, so its engine's judgement of a freeing (1 -> 3,
   1 -> 0, 2 -> 0) and of an ex-slave's end (3 -> 0) is the area's own; a game whose copy only mirrors another game's area asks
   nothing.
   T-1225 (run T2019, Confirmed): ESCAPING is never asked here, nor taken by the owner from this road. The engine judges 'escaping'
   from the slave owner's presence and distance, and a copy's engine can lack the owner (its copy not yet made after a load, put
   away, or an NPC of the owner's game that no game shares - T2019's 'Slaver Guard' uid 0): A's copy went 1 -> 2 with the owner
   hand naming nobody, B applied it with its owner 7 units away, and the town's guards chased the group.
   T-1225 (design C): an escape's end (2 -> 3) is not asked either. StateBroadcastData::periodicUpdate 0x5A44C0 makes it 12 game-clock
   units after the escape's own stamp (+8), and only the slave's own game made that escape and holds its stamp (Read,
   decomp_5a44c0.txt); its engine makes 2 -> 3 itself (SlaveOwnEscapeRefused never holds it) and MSG_SLAVE carries it.
   Making someone a slave is never asked here - neither 0 -> anything nor anything -> IS_SLAVE (re-enslaving an escaping or freed
   character): the engine sets IS_SLAVE inside a slaver's processing (task 182 body 0x35A9C0, its setSlaveState call), which is the
   capture road's (P11 MSG_CAPTURE), with its checks. */
const int kAskSend      = 0;   /* sender: ask the owner */
const int kAskNotCopy   = 1;   /* this game's own character: the setter ran, slave1 sends MSG_SLAVE */
const int kAskBad       = 2;   /* a state outside 0..kSlaveStateMax */
const int kAskSame      = 3;   /* no change */
const int kAskBail      = 4;   /* inside the bail confirm: its own release message carries the change (prisonwire.h "@bail") */
const int kAskEnslave   = 5;   /* from NOT_SLAVE or to IS_SLAVE: the capture road's */
const int kAskNoMap     = 6;   /* who runs the area is not known (no fresh area map, or no slot of this game) */
const int kAskNotRunner = 7;   /* another game runs the character's area */
const int kAskEscaping  = 8;   /* T-1225: to ESCAPING - never asked (the owner would refuse it: kAskOwnerEscaping) */
const int kAskEscapeEnd = 9;   /* T-1225 design C: ESCAPING -> EX_SLAVE - never asked (the owner would refuse it: kAskOwnerEscapeEnd) */
inline int SlaveAskSenderDecide(bool copy, bool inBail, int cur, int want, int runnerSlot, int mySlot)
{
    if (!copy) return kAskNotCopy;
    if (cur < 0 || cur > kSlaveStateMax || want < 0 || want > kSlaveStateMax) return kAskBad;
    if (cur == want) return kAskSame;
    if (inBail) return kAskBail;
    if (cur == 0 || want == 1) return kAskEnslave;
    if (want == 2) return kAskEscaping;
    if (cur == 2 && want == 3) return kAskEscapeEnd;
    if (runnerSlot < 0 || mySlot < 0) return kAskNoMap;
    if (runnerSlot != mySlot) return kAskNotRunner;
    return kAskSend;
}

/* The engine repeats a refused change every few frames: one ask per copy per wanted state, again only after kSlaveAskResendMs
   (the owner may have refused a stale one; the engine still wanting it is asked once more). */
const unsigned int kSlaveAskResendMs = 3000;
inline bool SlaveAskDue(bool askedBefore, int askedWant, unsigned int msSinceAsk, int want)
{
    return !askedBefore || askedWant != want || msSinceAsk >= kSlaveAskResendMs;
}

/* The owner, one ask about its own character: applied only when the character is ours, its state now is the one the copy had
   (an ask made on a copy that had not yet heard the owner's last change is stale), it is not an enslavement, it is not ESCAPING,
   and the asking game is the one that runs the character's area as this game's area map says (-1 unknown: refused).
   T-1225 (run T2019, Confirmed): ESCAPING (2) is never taken from this road - only the slave's own engine marks its slave
   escaping. The engine judges 'escaping' from the slave owner's presence and distance, and a copy's engine can lack the owner
   (its copy not yet made after a load, put away, or an NPC of the owner's game that no game shares - T2019's 'Slaver Guard'
   uid 0): A's copy went 1 -> 2 with the owner hand naming nobody, B applied it with its owner 7 units away, and the town's
   guards chased the group. Senders since T-1225 do not ask it (kAskEscaping); this refusal is for older peers.
   T-1225 (design C): nor an escape's end (2 -> 3): this game's own engine made the escape and ends it on its own clock
   (kAskEscapeEnd; this refusal, kAskOwnerEscapeEnd, is for older peers). Still taken from the runner: EX_SLAVE from IS_SLAVE
   (a freeing) and NOT_SLAVE (a freeing, an ex-slave's end). */
const int kAskApply          = 0;
const int kAskNotOwner       = 1;
const int kAskOwnerBad       = 2;   /* a state outside 0..kSlaveStateMax, or this game's read failed (cur < 0) */
const int kAskAlready        = 3;   /* ours is already the wanted state */
const int kAskStale          = 4;   /* ours is not the state the copy had */
const int kAskOwnerEnslave   = 5;   /* from NOT_SLAVE or to IS_SLAVE: not taken from this road */
const int kAskHolderUnknown  = 6;   /* this game does not know who runs the area, or the sender's slot */
const int kAskSenderNotHolder = 7;  /* the sender does not run the area */
const int kAskOwnerEscaping  = 8;   /* T-1225: to ESCAPING - only this game's own engine decides it */
const int kAskOwnerEscapeEnd = 9;   /* T-1225 design C: ESCAPING -> EX_SLAVE - only this game's own engine decides it */
const int kAskCodeCount      = 10;  /* counter arrays: sender and owner codes are below this */
inline int SlaveAskOwnerDecide(bool mine, int cur, int from, int want, int holderSlot, int senderSlot)
{
    if (!mine) return kAskNotOwner;
    if (cur < 0 || cur > kSlaveStateMax || from < 0 || from > kSlaveStateMax || want < 0 || want > kSlaveStateMax || from == want)
        return kAskOwnerBad;
    if (cur == want) return kAskAlready;
    if (cur != from) return kAskStale;
    if (from == 0 || want == 1) return kAskOwnerEnslave;
    if (want == 2) return kAskOwnerEscaping;
    if (from == 2 && want == 3) return kAskOwnerEscapeEnd;
    if (holderSlot < 0 || senderSlot < 0) return kAskHolderUnknown;
    if (holderSlot != senderSlot) return kAskSenderNotHolder;
    return kAskApply;
}
/* This game is KNOWN to run the area of one of OUR characters: a fresh area map names this game's slot. Every other answer -
   another game's slot, -1 (no map, a map older than 5 s, a row naming nobody, off the grid; zones.cpp AreaHolderSlotAgeTS) and
   coopdrop::kAreaFrozen -2 (the holder is a player not in the world) - is not, and so is an unknown slot of this game. */
inline bool SlaveAreaKnownHere(int runnerSlot, int mySlot)
{
    return runnerSlot >= 0 && mySlot >= 0 && runnerSlot == mySlot;
}
/* The slave-owner hand (Character +0x328, its type at +0x330) and the two engine lookups the escape routines make from it.
   Hand types (Read): 1 a character, 0x22 (34) a squad, 0x5b (91) the one other type both routines resolve, 11 the empty hand. */
const int kSlaveHandChar  = 1;
const int kSlaveHandSquad = 0x22;
const int kSlaveHandOther = 0x5b;
const int kSlaveHandNone  = 11;
/* ownerHere answers */
const int kOwnerHere        = 1;    /* the engine resolves the owner (or its squad) on this game */
const int kOwnerEmpty       = 0;    /* the empty hand */
const int kOwnerNotHere     = -1;   /* the lookups ran and found nobody, or a hand type neither routine resolves */
const int kOwnerUnreadable  = -2;   /* the hand's type could not be read */
const int kOwnerLookupFault = -3;   /* a type the engine resolves, but this game's lookup could not run (no address, or it faulted) */
/* What each routine resolves (Read, 1.0.65): hand::getCharacter 0x7974F0 (decomp_7974f0.txt) resolves type 1 by the object
   lookups and type 0x5b by the same singleton call 0x5A44C0 makes for it (0x790A60, decomp_790a60.txt), every other type to
   nobody. StateBroadcastData::periodicUpdate 0x5A44C0 resolves types 1 and 0x5b to a character (its type-1 fallback 0x796080
   where getCharacter has 0x7CC8A0) and calls the setter only when that character stands too far, or when it resolved nobody and
   0x4FDC50 does not know the hand either (the empty hand never). AI::updateStateBroadcast 0x5A4050 resolves the owner's SQUAD
   (hand::asActiveSquad 0x791D20, types 1 and 0x22 only) and calls the setter when no squad resolves for 45 s, and when one
   resolves but is down or its owner gone (decomp_5a4050.txt:83-158). So the owner is here when getCharacter resolves (types 1,
   0x5b) or asActiveSquad does (types 1, 0x22): with either, the change is the engine's own judgement from an owner standing on
   this game. charRes / squadRes: 1 found, 0 none, -1 the lookup could not run. handType -1: unreadable. */
inline int SlaveOwnerHereOf(int handType, int charRes, int squadRes)
{
    if (handType < 0) return kOwnerUnreadable;
    if (handType == kSlaveHandNone) return kOwnerEmpty;
    const bool byChar = handType == kSlaveHandChar || handType == kSlaveHandOther;
    const bool bySquad = handType == kSlaveHandChar || handType == kSlaveHandSquad;
    if ((byChar && charRes == 1) || (bySquad && squadRes == 1)) return kOwnerHere;
    if ((byChar && charRes < 0) || (bySquad && squadRes < 0)) return kOwnerLookupFault;
    return kOwnerNotHere;
}
/* T-1225 (design C): the slave's own game decides its slave state wherever it stands. Its engine's own changes - 1 -> 2,
   2 -> 3 and 3 -> 0 (StateBroadcastData::periodicUpdate 0x5A44C0) and 1 -> 2 (AI::updateStateBroadcast 0x5A4050) - are made
   and sent by MSG_SLAVE as when no other game runs the area. Two engines cannot undo each other: the area's game asks only
   1 -> 3, 1 -> 0, 2 -> 0 and 3 -> 0 (never to ESCAPING, never 2 -> 3, never to IS_SLAVE), and the engine never goes 3 -> 2 or
   0 -> 2 (Read, decomp_5a44c0.txt / decomp_5a4050.txt).
   One change is refused: 1 -> 2 while this game is not KNOWN to run the area (another game runs it, or the area map is unknown,
   stale or frozen - the other player's loading screen, a map gap, a reload before the first map) and the slave-owner hand does
   not resolve to its owner on this game by the engine's own lookups (SlaveOwnerHereOf; no uid: an NPC slaver that no game
   shares but that stands on this game resolves, and its slave's real escape goes through). Refused for kOwnerEmpty,
   kOwnerNotHere and kOwnerUnreadable; never for kOwnerLookupFault (a type the engine resolves: the engine's judgement stands).
   Both routines reach it through the setter 0x5A3EB0. 0x5A44C0 goes 1 -> 2 at once when the owner record names no known
   character and 0x5A4050 after no owner squad resolved for 45 s, so on a game that lacks the owner (another game's NPC that no
   game shares, a copy not yet made or put away) they mark a slave escaping whose owner stands beside it in the area's game, and
   the town's guards chase the group. The empty hand is refused too: the capture road leaves it empty when the slaver's copy is
   not on this game (items.cpp g_capOwnerNoSlaver), and 0x5A44C0 then goes 1 -> 2 at once - the same missing owner. Once the
   owner is known, or the map names this game, the engine's next 1 -> 2 goes through (both routines repeat it: 0x5A44C0 every
   periodic update, 0x5A4050 every frame). The cost: while the map is unknown, a real escape from an owner that is truly gone
   waits for the map.
   Known remaining case (Inferred, not folded): a copy of the owner counts as present even when it is stale, downed or alone on
   this game, so its engine can still judge a false escape by distance or by a downed squad; the area's game is not asked. */
inline bool SlaveOwnEscapeRefused(int cur, int want, bool areaKnownHere, int ownerHere)
{
    return cur == 1 && want == 2 && !areaKnownHere
        && (ownerHere == kOwnerEmpty || ownerHere == kOwnerNotHere || ownerHere == kOwnerUnreadable);
}
/* The decision detour_setSlaveState makes on one of OUR characters, from the facts it reads at the moment of the change. */
inline bool SlaveOwnEscapeDecide(int cur, int want, int runnerSlot, int mySlot, int handType, int charRes, int squadRes, int* ownerHereOut)
{
    const int ownerHere = SlaveOwnerHereOf(handType, charRes, squadRes);
    if (ownerHereOut != 0) *ownerHereOut = ownerHere;
    return SlaveOwnEscapeRefused(cur, want, SlaveAreaKnownHere(runnerSlot, mySlot), ownerHere);
}

inline const char* SlaveAskWhy(int d, bool owner)
{
    if (owner)
        switch (d)
        {
        case kAskApply: return "applied"; case kAskNotOwner: return "not ours"; case kAskOwnerBad: return "bad state or unreadable";
        case kAskAlready: return "already so"; case kAskStale: return "stale (ours is not the copy's state)";
        case kAskOwnerEnslave: return "an enslavement (the capture road's)"; case kAskHolderUnknown: return "who runs the area is not known";
        case kAskSenderNotHolder: return "the sender does not run the area";
        case kAskOwnerEscaping: return "escaping is decided by this game's own engine only (the asker's engine judged it on its copy)";
        case kAskOwnerEscapeEnd: return "an escape's end is decided by this game's own engine only (it made the escape and keeps its clock)";
        default: return "?";
        }
    switch (d)
    {
    case kAskSend: return "asked"; case kAskNotCopy: return "own character"; case kAskBad: return "bad state";
    case kAskSame: return "no change"; case kAskBail: return "inside the bail"; case kAskEnslave: return "an enslavement (the capture road's)";
    case kAskNoMap: return "who runs the area is not known"; case kAskNotRunner: return "another game runs the area";
    case kAskEscaping: return "escaping is decided by the slave's own game only (this engine judged it on its copy)";
    case kAskEscapeEnd: return "an escape's end is decided by the slave's own game only (its engine made the escape and keeps its clock)";
    default: return "?";
    }
}

} // namespace coopslave
