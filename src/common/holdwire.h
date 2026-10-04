#pragma once
/* P105 build 2 (row P105; .modding/investigations/p105-boxmove-cursor-design-2026-10-02.md sections 3-5, 15.3-15.5): AN ITEM LIFTED
   OUT OF A CONTAINER ANOTHER GAME WRITES IS HELD THERE, AND PUT DOWN THERE.
     HOLD  (ITEM_REQUEST dir 2) "the item at square S of that container is on my cursor": the writer lifts it out of its own copy into
           a hold row (alive, in no inventory) - the other players see it gone and cannot take it.
     LAND  (ITEM_REQUEST dir 3) "the item I hold under hold H went down at square T of the same container, or of ANOTHER BOX the same
           game writes" (the taker box key names that box - T-159, protocol 138): the writer puts its kept object there (placed,
           merged onto the stack there, back onto the stack it came off, or swapped in).
     A move inside one container (piece A), or from one box to another box the same game writes, is a HOLD followed by a LAND. A
     drop anywhere else is a LAND back onto the item's own square, sent first, and then the road the drop always took (the link is
     ordered, so the writer has the item back in its square when that road's request arrives).
   Every publication the writer makes for a HOLD / LAND carries a request tag 'RQT1' {request id, the asker's slot}; the asker never
   applies a publication tagged with its own slot - it already shows that change (the absorb rule, design 3.4).

   THE WIRE (protocol 126; 138: a LAND's taker box key may name another box). Every block below is a fixed-size TRAILER, the last bytes of its message, announced by the message's own
   leading byte so no reader ever has to guess whether it is there:
     ITEM_REQUEST  dir byte 2 (HOLD) / 3 (LAND)  ->  'HLD1' {u32 tag, u32 hold id, u8 how, u32 asker slot}         13 bytes
     ITEM_CONFIRM  ok byte 2 (= ok, with where)   ->  'LND1' {u32 tag, u8 where, u32 x, u32 y}                      13 bytes
     ITEM_MOVE     op byte | 0x80                 ->  'RQT1' {u32 tag, u32 request id, u32 asker slot}              12 bytes
   'HLD1' how: a LAND's kHow*; a HOLD's 1 = the whole item at that square (whatever count the writer's square holds - its answer says
   the count), 0 = exactly `quantity` units off the stack. The asker's slot is copied by the writer into every 'RQT1' it publishes.
   A refusal of a HOLD / LAND says why in the existing 'GND1' reason block (1 not the writer, 2 gone, 3 busy, 4 the access policy).
   Integers are little-endian, as everywhere on this wire.
   Header-only, no engine types; shared by items.cpp, session.cpp and the offline suite (coop-test). C++03. */

#include <cstddef>
#include <vector>

namespace coophold {

const int kDirHold = 2, kDirLand = 3;
const unsigned int kTagHold = 0x31444C48u;   /* "HLD1" */
const unsigned int kTagLand = 0x31444E4Cu;   /* "LND1" */
const unsigned int kTagReq  = 0x31545152u;   /* "RQT1" */
const size_t kHoldTailLen = 13, kLandTailLen = 13, kReqTagLen = 12;
const int kMoveTagFlag = 0x80;   /* ITEM_MOVE: the op byte's high bit = an 'RQT1' trailer */
const int kConfirmOkLand = 2;    /* ITEM_CONFIRM: the ok byte 2 = ok, with an 'LND1' trailer */

/* how the item went down (LAND) */
const int kHowPlaced = 0, kHowMerged = 1, kHowBack = 2, kHowSwap = 3, kHowCount = 4;
/* where the writer actually put it (LND1) */
const int kWhereExact = 0, kWhereOld = 1, kWhereFree = 2, kWhereGround = 3, kWhereKept = 4, kWhereCount = 5;
/* why a HOLD / LAND was refused (the GND1 reason values) */
const int kReasonNotWriter = 1, kReasonGone = 2, kReasonBusy = 3, kReasonPolicy = 4;

inline void PutU32Le(std::vector<char>* b, unsigned int v)
{
    b->push_back((char)(unsigned char)(v & 0xFFu)); b->push_back((char)(unsigned char)((v >> 8) & 0xFFu));
    b->push_back((char)(unsigned char)((v >> 16) & 0xFFu)); b->push_back((char)(unsigned char)((v >> 24) & 0xFFu));
}
inline unsigned int GetU32Le(const char* p)
{
    const unsigned char* u = (const unsigned char*)p;
    return (unsigned int)u[0] | ((unsigned int)u[1] << 8) | ((unsigned int)u[2] << 16) | ((unsigned int)u[3] << 24);
}

struct HoldTail { unsigned int holdId; int how; int slot; HoldTail() : holdId(0u), how(0), slot(-1) {} };
struct LandTail { int where; int x, y; LandTail() : where(0), x(0), y(0) {} };
struct ReqTag { unsigned int reqId; int slot; ReqTag() : reqId(0u), slot(-1) {} };

inline void PutHoldTail(std::vector<char>* b, const HoldTail& t)
{
    PutU32Le(b, kTagHold); PutU32Le(b, t.holdId); b->push_back((char)(unsigned char)t.how); PutU32Le(b, (unsigned int)t.slot);
}
/* The last kHoldTailLen bytes of a message of n bytes. 1 = read; 0 = too short, another tag, or a `how` out of range. */
inline int TakeHoldTail(const char* p, size_t n, HoldTail* t)
{
    if (p == 0 || n < kHoldTailLen) return 0;
    const char* q = p + (n - kHoldTailLen);
    if (GetU32Le(q) != kTagHold) return 0;
    const int how = (int)(unsigned char)q[8];
    if (how < 0 || how >= kHowCount) return 0;
    t->holdId = GetU32Le(q + 4); t->how = how; t->slot = (int)GetU32Le(q + 9);
    return 1;
}
inline void PutLandTail(std::vector<char>* b, const LandTail& t)
{
    PutU32Le(b, kTagLand); b->push_back((char)(unsigned char)t.where); PutU32Le(b, (unsigned int)t.x); PutU32Le(b, (unsigned int)t.y);
}
inline int TakeLandTail(const char* p, size_t n, LandTail* t)
{
    if (p == 0 || n < kLandTailLen) return 0;
    const char* q = p + (n - kLandTailLen);
    if (GetU32Le(q) != kTagLand) return 0;
    const int w = (int)(unsigned char)q[4];
    if (w < 0 || w >= kWhereCount) return 0;
    t->where = w; t->x = (int)GetU32Le(q + 5); t->y = (int)GetU32Le(q + 9);
    return 1;
}
inline void PutReqTag(std::vector<char>* b, const ReqTag& t)
{
    PutU32Le(b, kTagReq); PutU32Le(b, t.reqId); PutU32Le(b, (unsigned int)t.slot);
}
inline int TakeReqTag(const char* p, size_t n, ReqTag* t)
{
    if (p == 0 || n < kReqTagLen) return 0;
    const char* q = p + (n - kReqTagLen);
    if (GetU32Le(q) != kTagReq) return 0;
    t->reqId = GetU32Le(q + 4); t->slot = (int)GetU32Le(q + 8);
    return 1;
}

/* THE ABSORB RULE (design 3.4): a publication tagged with THIS game's own slot is the writer's effect of a request this game made and
   already shows - never applied. Every other game applies it as ordinary truth. -1 (no notebook slot) is a slot like any other: two
   games with no slot are only ever the asker and the writer, and the writer never receives its own publication. */
inline int AbsorbTagged(int tagHas, int tagSlot, int mySlot)
{
    return (tagHas != 0 && tagSlot == mySlot) ? 1 : 0;
}

/* THE WRITER'S HOLD STEP, in order (design 4.2): a repeat of a hold it granted is answered again from its row (nothing moves); then
   authority, then the box's access policy, then a test lever's refusal, then room in the hold table. -1 = answer the repeat, 0 = take
   the item, else the refusal reason. */
const int kHsRepeat = -1, kHsTake = 0;
inline int HoldServeStep(int repeatRow, int isWriter, int accessOk, int refuseNext, int tableFull)
{
    if (repeatRow != 0) return kHsRepeat;
    if (isWriter == 0) return kReasonNotWriter;
    if (accessOk == 0) return kReasonPolicy;
    if (refuseNext == kReasonGone || refuseNext == kReasonBusy || refuseNext == kReasonPolicy) return refuseNext;
    if (tableFull != 0) return kReasonBusy;
    return kHsTake;
}

/* WHERE A LAND GOES on the writer (design 4.3 + 15.3), from live reads at the moment it is applied:
   cellFits  the target square is empty and the kept object fits there; mergeOk  the target holds a stack of the same record with room
   for the units (the stack limit read first); oldFits  the hold's own square takes it; freeFound  some other square of the section
   takes it. A placed / swapped item goes into the square asked for; merged / back units onto the stack there. Anything else is the
   writer's own player having changed the box meanwhile: its old square, then (units off a stack - mergeOldOk: the stack they came
   off is still there with room) back onto that stack, then any square, else it is KEPT (parked, placed when room appears) - every one
   of those is answered where != exact, and the asker re-asks the box. freeTargetFirst (T-159, a LAND into another box whose free
   square is in that box - LandFreeFrom kLfTarget): the player chose that box, so a free square there comes before the way home. */
const int kLpPlaceExact = 0, kLpMergeExact = 1, kLpOld = 2, kLpFree = 3, kLpKeep = 4, kLpMergeOld = 5;
inline int LandPlan(int how, int cellFits, int mergeOk, int oldFits, int freeFound, int mergeOldOk = 0, int freeTargetFirst = 0)
{
    if ((how == kHowPlaced || how == kHowSwap) && cellFits != 0) return kLpPlaceExact;
    if ((how == kHowMerged || how == kHowBack) && mergeOk != 0) return kLpMergeExact;
    if (freeTargetFirst != 0 && freeFound != 0) return kLpFree;
    if (oldFits != 0) return kLpOld;
    if (mergeOldOk != 0) return kLpMergeOld;
    if (freeFound != 0) return kLpFree;
    return kLpKeep;
}
inline int LandWhereOf(int plan)
{
    if (plan == kLpPlaceExact || plan == kLpMergeExact) return kWhereExact;
    if (plan == kLpOld || plan == kLpMergeOld) return kWhereOld;
    if (plan == kLpFree) return kWhereFree;
    return kWhereKept;
}

/* WHAT THE ASKER DOES WITH AN ANSWER (design 5.3). kind 0 HOLD, 1 LAND. onCursor: the held object is still on this game's cursor. */
const int kAnNone = 0, kAnReaskBox = 1, kAnPutBackReaskBox = 2, kAnAskAgain = 3;
inline int AnswerAction(int kind, int ok, int reason, int where, int onCursor)
{
    if (ok != 0) return (kind == 1 && where != kWhereExact) ? kAnReaskBox : kAnNone;
    if (reason == kReasonNotWriter || reason == kReasonBusy) return kAnAskAgain;   /* never guessed: the same id, asked again */
    if (kind == 0 && onCursor != 0) return kAnPutBackReaskBox;                      /* gone / policy: the item goes back, the box is re-read */
    return kAnReaskBox;
}

/* HOW THE ASKER NAMES ITS DROP (design 5.2). sameContainer: the drop's square is in the container the hold came out of;
   mergedThere: the engine merged the item onto a stack at that square (that stack's own removal rang at the same square in the same
   drain); displacedThere: a different item was lifted off that square onto the cursor (a swap); backOnOwnStack: a split's units went
   back onto the stack they came off. -1 = not a LAND into the same container: the hold is landed BACK on its own square first. */
inline int HowAtDrop(int sameContainer, int mergedThere, int displacedThere, int backOnOwnStack)
{
    if (sameContainer == 0) return -1;
    if (backOnOwnStack != 0) return kHowBack;
    if (mergedThere != 0) return kHowMerged;
    if (displacedThere != 0) return kHowSwap;
    return kHowPlaced;
}

/* FOLD 1 (review HIGH-1 / MED-1, 2026-10-02): THE ASKER'S HOLD ROW AGAINST ITS LAND. A HOLD row is done once its LAND is answered
   for good, granted or not: a hold never granted (refused busy / not the writer and waiting, its send lost, or still on the way) has
   nothing left to wait for once the item is down - before, such a row stayed open and kept its box from every resync (HdBoxPending).
   A granted hold is done as soon as its LAND goes out (unchanged). */
inline int HoldRowDone(int granted, int landed, int landAnswered)
{
    return ((granted != 0 && landed != 0) || landAnswered != 0) ? 1 : 0;
}
/* An answer to a HOLD row already done. A GRANT for a hold never granted before, whose LAND was REFUSED, means the LAND overtook the
   HOLD (the road switched between the two sends, or a re-ask was granted after the drop): the writer now keeps the item for nobody -
   the LAND is sent again under a new id (the writer caches the old id's refusal). Anything else is swallowed. */
const int kLaSwallow = 0, kLaResendLand = 1;
inline int HoldLateAnswer(int ok, int wasGranted, int landAnswered, int landRefused)
{
    return (ok != 0 && wasGranted == 0 && landAnswered != 0 && landRefused != 0) ? kLaResendLand : kLaSwallow;
}
/* FOLD 1 (review LOW): a LAND is never refused for table space. A new HOLD is opened only while the rows free to take (unused or
   done) cover it, its own LAND and one LAND for every open hold not yet landed. */
inline int HoldRoomOk(int available, int openHolds)
{
    return (available >= openHolds + 2) ? 1 : 0;
}

/* FOLD 2 (H1 / H2, 2026-10-02): A HOLD'S ITEM ON THE GROUND AT ITS BOX. When this game stops writing a box with a hold open on it,
   or a zone write finds no room, the kept item is dropped at the box on the one drop road (announced to every game). d: the drop's
   answer - 1 on the ground: the hold ends; -1 FAULTED: the hold ends too (the item may be on the ground already - never added into
   the box again, T-164's rule); 0 not placed (no position read, no ground code, the engine did not place it): the put-back. */
const int kGdEnded = 0, kGdEndedFault = 1, kGdPutBack = 2;
inline int GroundDropNext(int d)
{
    if (d == 1) return kGdEnded;
    if (d < 0) return kGdEndedFault;
    return kGdPutBack;
}
/* FOLD 2 (H3): WHICH ANSWERS THE WRITER REMEMBERS (kind 0 HOLD, 1 LAND). A grant and a refusal "gone" are final, and for a HOLD the
   access policy's refusal too: a repeat of that id is answered from the record, nothing moved. "Not the writer" and "busy" are asked
   again under the same id and decided afresh - never remembered. */
inline int AnswerFinal(int kind, int ok, int reason)
{
    if (ok != 0 || reason == kReasonGone) return 1;
    return (kind == 0 && reason == kReasonPolicy) ? 1 : 0;
}
/* FOLD 2 (H3 / H4): THE WRITER'S FIRST LOOK AT A HOLD, before HoldServeStep. An open row for the id: the ordinary step (its repeat
   answer). No row but a remembered final answer for the id: that answer again, nothing moved (a duplicate HOLD after its hold ended).
   No row, no record, but a LAND for this hold id already refused "gone": the LAND overtook the HOLD (the road switched between the two
   sends, or the asker's edge sent a LAND back for a hold not answered yet) - the HOLD is refused "gone" and nothing is taken, so the
   item stays on its own square, where a hold landed back would put it. */
const int kHpStep = 0, kHpRecord = 1, kHpLandFirst = 2;
inline int HoldPreStep(int openRow, int recordFound, int landRefusedFound)
{
    if (openRow != 0) return kHpStep;
    if (recordFound != 0) return kHpRecord;
    if (landRefusedFound != 0) return kHpLandFirst;
    return kHpStep;
}

/* T-159: A DROP INTO ANOTHER BOX THE SAME GAME WRITES. An item lifted out of box X that another game writes (held, or a one-tick move)
   and put down in box Y is a LAND into Y when: X and Y are both boxes; Y is not inside a pack and not a shop facade's half; this game
   does not write Y but knows who does (the area verdict says another game holds it) and Y has a key; and the game that writes Y is the
   game that writes X - the slots this game's requests for the two boxes would go to are equal (-1 included: with no slot known for
   either, both requests go to the one linked game). 1 = a LAND into Y; 0 = the road a drop elsewhere always took (a LAND back onto
   X's own square first). */
inline int LandIntoOtherBox(int srcIsBox, int dstIsBox, int dstMine, int dstHeld, int dstKeyHas, int dstInBag, int dstFacade,
                            int srcWriterSlot, int dstWriterSlot)
{
    if (srcIsBox == 0 || dstIsBox == 0 || dstMine != 0 || dstHeld == 0 || dstKeyHas == 0 || dstInBag != 0 || dstFacade != 0) return 0;
    return (srcWriterSlot == dstWriterSlot) ? 1 : 0;
}
/* T-159: THE WRITER'S LOOK AT A LAND INTO ANOTHER BOX. The other box takes the item only when this game writes it too (read live),
   its access policy lets that player in, and it is not a registered shop piece (never held for, or filled by, another player's
   cursor). Otherwise the target is BARRED and the LAND is planned with no target square: the item goes back to its own square in
   its own box, then any square there, else it is kept and placed when room appears - never dropped, never doubled; the answer says
   where (not exact), so the asker re-reads both boxes. A LAND inside one container is never barred here. */
inline int LandTargetOpen(int otherBox, int targetWriter, int targetAccess, int targetShopPiece)
{
    if (otherBox == 0) return 1;
    return (targetWriter != 0 && targetAccess != 0 && targetShopPiece == 0) ? 1 : 0;
}
/* T-159: WHICH CONTAINER A LAND'S "any free square" comes from. Inside one container: that container (1). Into another box: a free
   square of the target box first (1), else a free square of the item's own box (2) - a full target sends the item home, it never
   waits while its own box has room. 0 = none (the item is kept). */
const int kLfNone = 0, kLfTarget = 1, kLfOwn = 2;
inline int LandFreeFrom(int otherBox, int targetOpen, int freeInTarget, int freeInOwn)
{
    if (otherBox == 0) return (freeInTarget != 0) ? kLfTarget : kLfNone;
    if (targetOpen != 0 && freeInTarget != 0) return kLfTarget;
    return (freeInOwn != 0) ? kLfOwn : kLfNone;
}
/* T-159: HOW THE ASKER RE-READS THE TARGET BOX after a LAND into another box that it must re-read (reask: the answer was not exact,
   or the LAND was refused). The engine here already showed the item in that box at the drop. When this game does not write the box,
   the ordinary ask (its writer's copy replaces this one). When this game WRITES it now (that box's area was handed to this game while
   the LAND was in flight), its own copy - with the item shown at the drop - is what counts here and an ordinary ask is answered
   "not the holder"; the box is PULLED instead: the previous writer's copy (the one the LAND was served against) goes in over this
   one, so the item is in the box the writer put it in and nowhere else, however it was dropped (placed, merged, swapped or split). */
const int kRrNone = 0, kRrAsk = 1, kRrPull = 2;
inline int LandTargetReread(int reask, int otherBox, int targetMineNow)
{
    if (reask == 0 || otherBox == 0) return kRrNone;
    return (targetMineNow != 0) ? kRrPull : kRrAsk;
}

}   /* namespace coophold */
