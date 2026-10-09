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
   character's area asks: there the guards and the slave owner's people are run, so its engine's judgement (a slave leaving the
   camp is ESCAPING, a freed one EX_SLAVE) is the area's own; a game whose copy only mirrors another game's area asks nothing.
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
inline int SlaveAskSenderDecide(bool copy, bool inBail, int cur, int want, int runnerSlot, int mySlot)
{
    if (!copy) return kAskNotCopy;
    if (cur < 0 || cur > kSlaveStateMax || want < 0 || want > kSlaveStateMax) return kAskBad;
    if (cur == want) return kAskSame;
    if (inBail) return kAskBail;
    if (cur == 0 || want == 1) return kAskEnslave;
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
   (an ask made on a copy that had not yet heard the owner's last change is stale), it is not an enslavement, and the asking game
   is the one that runs the character's area as this game's area map says (-1 unknown: refused). */
const int kAskApply          = 0;
const int kAskNotOwner       = 1;
const int kAskOwnerBad       = 2;   /* a state outside 0..kSlaveStateMax, or this game's read failed (cur < 0) */
const int kAskAlready        = 3;   /* ours is already the wanted state */
const int kAskStale          = 4;   /* ours is not the state the copy had */
const int kAskOwnerEnslave   = 5;   /* from NOT_SLAVE or to IS_SLAVE: not taken from this road */
const int kAskHolderUnknown  = 6;   /* this game does not know who runs the area, or the sender's slot */
const int kAskSenderNotHolder = 7;  /* the sender does not run the area */
inline int SlaveAskOwnerDecide(bool mine, int cur, int from, int want, int holderSlot, int senderSlot)
{
    if (!mine) return kAskNotOwner;
    if (cur < 0 || cur > kSlaveStateMax || from < 0 || from > kSlaveStateMax || want < 0 || want > kSlaveStateMax || from == want)
        return kAskOwnerBad;
    if (cur == want) return kAskAlready;
    if (cur != from) return kAskStale;
    if (from == 0 || want == 1) return kAskOwnerEnslave;
    if (holderSlot < 0 || senderSlot < 0) return kAskHolderUnknown;
    if (holderSlot != senderSlot) return kAskSenderNotHolder;
    return kAskApply;
}
/* While another game runs the area of one of OUR characters, that game's engine is the only one deciding its slave state: this
   game's own StateBroadcastData::periodicUpdate (its setter call and its direct writes) is held back for it, and the runner's
   periodic changes on its copy are asked like its setter changes. Two engines deciding the same character would undo each other
   (one writes EX_SLAVE, the other asks ESCAPING again) without end; one decider settles. -1 (not known) leaves this game's own. */
inline bool SlaveOwnEngineDefers(int runnerSlot, int mySlot)
{
    return runnerSlot >= 0 && mySlot >= 0 && runnerSlot != mySlot;
}

inline const char* SlaveAskWhy(int d, bool owner)
{
    if (owner)
        switch (d)
        {
        case kAskApply: return "applied"; case kAskNotOwner: return "not ours"; case kAskOwnerBad: return "bad state or unreadable";
        case kAskAlready: return "already so"; case kAskStale: return "stale (ours is not the copy's state)";
        case kAskOwnerEnslave: return "an enslavement (the capture road's)"; case kAskHolderUnknown: return "who runs the area is not known";
        case kAskSenderNotHolder: return "the sender does not run the area"; default: return "?";
        }
    switch (d)
    {
    case kAskSend: return "asked"; case kAskNotCopy: return "own character"; case kAskBad: return "bad state";
    case kAskSame: return "no change"; case kAskBail: return "inside the bail"; case kAskEnslave: return "an enslavement (the capture road's)";
    case kAskNoMap: return "who runs the area is not known"; case kAskNotRunner: return "another game runs the area"; default: return "?";
    }
}

} // namespace coopslave
