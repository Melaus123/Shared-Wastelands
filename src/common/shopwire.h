/* T-164 B4-4 (intercept design, owner 184-186) - A SHOP TRADE ASKED OF THE STOCK HOLDER AT THE TRADE CALL. No engine types; the
 * plugin (items.cpp, net/session.cpp) and the offline suite compile this.
 *
 * THE ROAD (.modding/investigations/t1-b44-intercept-design.md): the non-holder's right-click trade (InventoryGUI::RClickAutoTrade
 * 0x713150) is stopped BEFORE anything moves. The requester reserves the money (a buy) or holds the item in escrow (a sale),
 * sends MSG_ITEM_REQUEST with the shop block below, and settles on the holder's MSG_ITEM_CONFIRM, whose shop block names the
 * outcome, the units, the price and the pieces the holder touched. A refusal, or no answer before the asked game leaves or the road
 * to it is lost, gives the reservation / escrow back.
 *
 *   Gate          - which road a trade call takes on this game (the original here, the intercept, or a local refusal).
 *   CaravanHolder - T-1 B5: which game holds a travelling trader's packs (the keeper's uid, every backing pack's wearer and - fold 1 F2 -
 *                   the squad's formal leader; a single-player game, or nothing adopted, is this game - the engine's own trade).
 *   PreCheck      - the original's own cheap pre-checks, answered with the original's own TradeResult.
 *   Units         - one unit, or the whole stack with shift ('all', owner 186) - ONE request either way.
 *   TotalPrice    - unit price x units, refused on overflow.
 *   SameClick     - the dedupe of one click reaching the hook twice in one frame (KeyAfter: remembered only once sent).
 *   BuyFit        - the buyer's room for ALL n units, asked before anything moves (the engine's "No room", result 2);
 *                   T-450: CountPlacementsCapped / CellUnits / CellRoom / RoomAdd count it (free cells under the section's
 *                   item-count cap, x the stack limit - one unit at a limit of 1 or less - plus matching stacks' room).
 *   Desc/SameDesc - the clicked variant's full description; the holder takes only stacks that match it exactly.
 *   LateAnswer    - an ok after the requester settled the request with no answer (its lapse): the holder is asked to undo its halves (UndoOutcome counts it).
 *   Reserve       - the buyer's reservation answer (ItPursePay) in words.
 *   Settle        - what the requester does on the answer (a bit set).
 *   TakePlan      - which stacks, in the pieces' chain order, the holder takes n units from (0x951BA0's first-holder rule).
 *   HolderBuy/HolderSell - the holder's verdicts; ShowResult maps them to the ENGINE's own TradeResult message (0x70EC10), 0 = none.
 *   Encode/Decode - the two tagged blocks ('SHR3' on the request - T-1 B5, 'SHR2' + the stock kind; 'SHR2' is still read as a
 *                   home's - and 'SHC1' on the confirm), after the trade block.
 *   StackMax/StackRoom - fold 2 (MED 1): the engine's stack limit (FUN_14075D550's rule) and what a matching stack can still take.
 *   Tail          - fold 2 (MED 2): 'SHK1' after MSG_ITEM_REVOKE's id - kind 0 a caravan's revoke, 1 the SHOP ACK, 2 the CANCEL to
 *                   the area's holder, 3 its answer (CancelHolderDecide / CancelAskerOnAnswer / CancelSendDue).
 *   SettleOnlyDue / SettleOnlyDecide - a lapsed home-shop trade asked of the area's current holder settle-only ('SHS1').
 *   Record*       - fold 2 (MED 2): the holder's applied record, kept until the ack (no expiry, never evicted; full = kVBusy).
 *   MakeEpoch/SameRecord - fold 2 (item 12): the requester's per-process epoch keys the record, so ids that restart never match. */
#ifndef COOP_SHOPWIRE_H
#define COOP_SHOPWIRE_H

#include <string>
#include <vector>
#include <cstring>

namespace coopshop {

const unsigned int kReqTag = 0x33524853u;   /* 'SHR3' little-endian (T-1 B5, protocol 121: 'SHR2' + u8 stock kind after the epoch) */
const unsigned int kReqTagV4 = 0x34524853u; /* 'SHR4' (T-619): 'SHR3' + the requester's culture and local price multipliers (2 x f32) */
const unsigned int kReqTagV2 = 0x32524853u; /* 'SHR2' (fold 2, protocol 96: + u32 epoch after entry; 'SHR1' was 92-94) - still decoded, as a home's stock */
const unsigned int kReqTagSettle = 0x31534853u; /* 'SHS1': the 'SHR3' layout, a SETTLE-ONLY request (no price legs) */
const unsigned int kCfTag = 0x31434853u;    /* 'SHC1' little-endian */
const int kMaxStr = 96;                     /* a box key or a record sid; longer is refused */
const int kMaxPieces = 32;                  /* piece keys a confirm may name */
const int kMaxUnits = 100000;

const int kEntryOne = 0;   /* right-click, one unit (or a stack of one) */
const int kEntryAll = 1;   /* right-click with shift: the whole stack (owner 186) */
const int kEntryDrag = 2;  /* a drag drop (I3, T-164 part 2) */
/* T-1 B5: WHICH STOCK a request names. Home: the trader's home building's pieces, named by the home's box key. Caravan: a travelling
   trader's packs - the packs its squad's members wear - named by the keeper's uid alone (the home key is empty). */
const int kStockHome = 0;
const int kStockCaravan = 1;
/* T-608 (owner decision 971 b, protocol 168): an NPC's purchase from a PLAYER's shop counter, made on a game that does not write
   that counter. The home key is the COUNTER's box key, no keeper (town NPCs often carry no uid - the buyer stays on the buying game),
   one unit, entry kEntryOne. The counter owner's game makes the sale (ApplyNpcBuyRequest) and answers; an older build refuses the
   kind (StockKeyOk). */
const int kStockNpcBuy = 2;

struct ShopReq
{
    int has;
    unsigned int keeperUid;   /* the trader the window shows */
    std::string homeKey;      /* its home building's box key - the holder's branch is keyed on it; empty for a caravan (T-1 B5) */
    std::string sid;          /* the item's base record */
    int n;                    /* units */
    int unitPrice;            /* cats per unit, as the requester's engine priced it */
    int entry;                /* kEntry* */
    unsigned int epoch;       /* fold 2 (item 12): the requester's per-process epoch (never 0) - the holder's record key */
    int stock;                /* T-1 B5: kStock* (an 'SHR2' block decodes as kStockHome) */
    float cult;               /* T-619: the culture multiplier the requester's price used (0 = not sent: an 'SHR3' block) */
    float local;              /* T-619: the local multiplier the requester's price used (0 = not sent) */
    int settleOnly;           /* 1 = SETTLE-ONLY ('SHS1'): answered from the shop record only, never applied fresh (SettleOnlyDue) */
    ShopReq() : has(0), keeperUid(0), n(0), unitPrice(0), entry(0), epoch(0), stock(kStockHome), cult(0.0f), local(0.0f), settleOnly(0) {}
};
struct ShopCf
{
    int has;
    int verdict;              /* kV* (0 = applied) */
    int show;                 /* the engine TradeResult to show on the requester (0x70EC10), 0 = no message */
    int n;                    /* units moved */
    int price;                /* cats the keeper moved */
    std::vector<std::string> pieces;   /* the holder's piece keys it wrote (the requester re-asks each) */
    ShopCf() : has(0), verdict(0), show(0), n(0), price(0) {}
};

/* ---------------- the gate ---------------- */
const int kGateOriginal = 0;    /* not a shop window, or a trader with no home and no packs behind its window (nothing to trade) */
const int kGateHeldHere = 1;    /* this game holds the stock (the home, or the caravan's packs): the original runs (B4-1 measures it) */
const int kGateIntercept = 2;   /* the other game holds it: intercept, request */
const int kGateRefuse = 3;      /* a shop whose holder is not known (no key, no answer, unowned, mixed packs): refused here, nothing runs */
/* isShop: one window is a ShopTrader stand-in. hasHome: its trader's home building read. homeKeyOk: that home has a box key.
   holder: 1 this game, 2 the other game, anything else unknown. caravan (T-1 B5): the trader has no home and its stand-in is
   backed by packs (a travelling trader) - holder is then CaravanHolder's answer, and the road is a town shop's. */
inline int Gate(int isShop, int hasHome, int homeKeyOk, int holder, int caravan = 0)
{
    if (isShop == 0) return kGateOriginal;
    if (hasHome == 0 && caravan == 0) return kGateOriginal;
    if (hasHome != 0 && homeKeyOk == 0) return kGateRefuse;
    if (holder == 1) return kGateHeldHere;
    if (holder == 2) return kGateIntercept;
    return kGateRefuse;
}
/* T-1 B5 (manager decision 2, 2026-10-02): WHICH GAME HOLDS A CARAVAN'S PACKS - the game that runs the trader's squad, the rule B1
   uses: the keeper's uid is that game's AND every pack used is worn by a member that game runs AND (fold 1 F2) the squad's FORMAL
   LEADER is run by that game too (the squad pot is the leader's, and only the leader's game announces it - SquadCatsAnnounceNow).
   multiplayer: this game is in a multiplayer world (not a single-player game) - 1 whether or not another player is in that world
   now. keeperUid: the keeper carries a uid. keeperMine: it is this game's. leader: the
   formal leader - 1 this game runs it, 2 the other game, 0 no uid (or not read). packs: the backing packs found. wMine / wOther /
   wUnknown: of them, worn by a member this game runs / the other game runs / no uid (or no wearer found). wearerUid: some worn pack
   of the squad has a wearer with a uid, or the squad could not be shown to have none (not read, or wider than the view).
   A SINGLE-PLAYER GAME - held here: the engine's own trade. In a multiplayer world the character numbers decide on every player
   count: a caravan another player's game runs is that game's even while it is away (the request then gets no answer and is
   settled as a refusal when that game leaves or the road to it is lost). NOTHING ADOPTED (the keeper, the leader and every wearer carry no uid) -
   held here too: the engine's own trade, as before B5 for an unadopted caravan. Otherwise 1 this game, 2 the other game, 0 not
   known (the keeper, the leader or a wearer with no uid), kCaravanMixed the keeper's game does not run every wearer and the leader
   (a squad hand-over in flight, or a split squad) - Gate refuses both 0 and kCaravanMixed: nothing runs. */
const int kCaravanMixed = 3;
inline int CaravanHolder(int multiplayer, int keeperUid, int keeperMine, int leader, int packs, int wMine, int wOther, int wUnknown, int wearerUid)
{
    if (multiplayer == 0) return 1;
    if (packs <= 0) return 0;
    if (keeperUid == 0 && leader == 0 && wearerUid == 0 && wMine == 0 && wOther == 0) return 1;
    if (keeperUid == 0 || leader == 0 || wUnknown != 0) return 0;
    if (keeperMine != 0) return (wOther == 0 && leader == 1) ? 1 : kCaravanMixed;
    return (wMine == 0 && leader == 2) ? 2 : kCaravanMixed;
}

/* T-1007 (J): A PARTLY NUMBERED CARAVAN. Each member that carries no number - the keeper (keeperUid 0), the formal leader read with
   no number (leaderUnnum 1) and each pack worn by a member found with no number (wUnnum) - takes ONE answer from the area rule at
   the squad's decision position (the numbering sweep's rule): area kCarAreaMine counts it as this game's, kCarAreaOther as another
   game's, kCarAreaUnknown leaves it unknown. Numbered members keep their numbers; wUnknown (a pack whose wearer was not found, a
   squad wider than the view) stays unknown. A single-player game and NOTHING ADOPTED are CaravanHolder's, unchanged; the rest is
   CaravanHolder over the answered members. */
const int kCarAreaUnknown = 0, kCarAreaMine = 1, kCarAreaOther = 2;
inline int CaravanHolderArea(int multiplayer, int keeperUid, int keeperMine, int leader, int leaderUnnum, int packs, int wMine, int wOther,
                             int wUnnum, int wUnknown, int wearerUid, int area)
{
    if (multiplayer == 0) return 1;
    if (packs <= 0) return 0;
    if (keeperUid == 0 && leader == 0 && wearerUid == 0 && wMine == 0 && wOther == 0) return 1;
    if (area == kCarAreaMine || area == kCarAreaOther)
    {
        const int side = (area == kCarAreaMine) ? 1 : 2;
        if (keeperUid == 0) { keeperUid = 1; keeperMine = (side == 1) ? 1 : 0; }
        if (leader == 0 && leaderUnnum != 0) leader = side;
        if (side == 1) wMine += wUnnum; else wOther += wUnnum;
    }
    else wUnknown += wUnnum;
    return CaravanHolder(1, keeperUid, keeperMine, leader, packs, wMine, wOther, wUnknown, wearerUid);
}
/* T-1007 (J2): THE ANSWER WAIT'S PURE RULES.
   PendOnGone - which pending request a leave or a link loss settles. linkLoss 0 (PLAYER_GONE): one asked of goneSlot (kPendAskedGone).
     linkLoss 1 (the session link went down): one asked over the session link (askedSession 1) and sent before the link's latest
     edge (sentBeforeEdge 1) - kPendLinkLost; one asked of a game over the world server stays (its leave or the road check ends it).
   RepeatRoad - the holder, at a request whose {peer, epoch, id} it answered already (the requester re-sends a pending request once a
     second): the applied record's confirm (kept until the ack), else the answer memory's; kRepeatFresh = never answered: judged now.
   DupAnswer - the requester, at a further answer to a request it settled already: an ok to one settled as a trade is acked again (a
     lost ack is made good), an ok to one settled as a refusal is revoked (the holder undoes its halves), a refusal changes nothing.
   RefusalShows - whether a refused caravan trade shows "That's not for sale." now: a click or a drop (perClick 1) once per frame; a
     pending request only when it is the one the player waits on (its id is the latest click's). */
const int kPendKeep = 0, kPendAskedGone = 1, kPendLinkLost = 2;
inline int PendOnGone(int askedSlot, int askedSession, int sentBeforeEdge, int goneSlot, int linkLoss)
{
    if (linkLoss == 0) return (goneSlot >= 0 && askedSlot == goneSlot) ? kPendAskedGone : kPendKeep;
    return (askedSession != 0 && sentBeforeEdge != 0) ? kPendLinkLost : kPendKeep;
}
/* T-1007 (J3): inAcked - the request is on the holder's acked list (acked or revoked: finished) - kRepeatAcked: nothing is sent and
   nothing applied. Order: an unacked record's confirm, the acked list, the answer memory, then a fresh judgement. */
const int kRepeatFresh = 0, kRepeatFromRecord = 1, kRepeatFromMemory = 2, kRepeatAcked = 3;
inline int RepeatRoad(int inRecord, int inMemory, int inAcked = 0)
{
    if (inRecord != 0) return kRepeatFromRecord;
    if (inAcked != 0) return kRepeatAcked;
    return (inMemory != 0) ? kRepeatFromMemory : kRepeatFresh;
}
/* T-1007 (J3): WHO ASKED, as the holder keys its records and answers: the asker's player slot when it is known (whichever road the
   message came on), else the raw sender key marked by the top bit (never equal to a slot). */
inline unsigned int AskerKey(int slot, unsigned int rawKey) { return (slot >= 0) ? (unsigned int)slot : (0x80000000u | rawKey); }
/* T-1007 (J4): the session peer's slot became known - a key filed while it was not (top bit set: a raw link key, which only the
   session peer's messages carry) is re-filed under that slot; a slot key, or no slot known, is kept. */
inline unsigned int AskerRefile(unsigned int asker, int slot) { return ((asker & 0x80000000u) != 0 && slot >= 0) ? (unsigned int)slot : asker; }
inline int AskerIsSlot(unsigned int asker) { return ((asker & 0x80000000u) == 0) ? 1 : 0; }
const int kDupNothing = 0, kDupAck = 1, kDupRevoke = 2;
inline int DupAnswer(int settledOk, int answerOk)
{
    if (answerOk == 0) return kDupNothing;
    return (settledOk != 0) ? kDupAck : kDupRevoke;
}
inline int RefusalShows(int perClick, int sameFrame, unsigned int id, unsigned int latestId)
{
    if (perClick != 0) return (sameFrame != 0) ? 0 : 1;
    return (id != 0 && id == latestId) ? 1 : 0;
}

/* ---------------- the original's cheap pre-checks (decomp_713150 :82-127) ---------------- */
const int kTrLocked = 7, kTrOutOfRange = 1, kTrNoRoom = 2, kTrCantAfford = 3, kTrKeeperBroke = 4, kTrQuiet = 11;
/* 0 = pass; otherwise the TradeResult the original would have answered (locked 7, out of range 1), or kTrQuiet for an item this
   road does not carry (money - its first branch destroys it, a stack count that did not read). */
inline int PreCheck(int locked, int inRange, int isMoney, int qtyRead)
{
    if (isMoney != 0 || qtyRead == 0) return kTrQuiet;
    if (locked != 0) return kTrLocked;
    if (inRange == 0) return kTrOutOfRange;
    return 0;
}

/* ---------------- amount and price ---------------- */
/* The original: a stack under 2 moves whole, else one unit (clone 0x57B5A0). Shift ('all', owner 186): the whole stack as ONE request. */
inline int Units(int stackQty, int allFlag)
{
    if (stackQty <= 0) return 0;
    if (stackQty < 2 || allFlag != 0) return (stackQty > kMaxUnits) ? 0 : stackQty;
    return 1;
}
inline int TotalPrice(int unitPrice, int n)
{
    if (unitPrice < 0 || n <= 0) return -1;
    const long long t = (long long)unitPrice * (long long)n;
    return (t > 0x7FFFFFFFLL) ? -1 : (int)t;
}

/* ---------------- the drag (T-164 part 2, design I2-I4) ---------------- */
/* I2: a pick-up from a shop stand-in runs under ShopTraderInventory::Updating when the other game holds the shop or its holder is
   not known - the stand-in callbacks then touch no piece: the cursor carries a COPY and only the window lost it. */
inline int PickupGuarded(int gate) { return (gate == kGateIntercept || gate == kGateRefuse) ? 1 : 0; }
/* I3: what a drop does. */
const int kDropOriginal = 0;        /* not this road's: the original, unchanged */
const int kDropGuardedOriginal = 1; /* a shop copy dropped back on its own shop: the original under the guard */
const int kDropBuy = 2;             /* a shop copy on a window of ours, a money trade, the other game holds the shop: back, request */
const int kDropSell = 3;            /* an item of ours dropped on a shop the other game holds, a money trade: back, sale request */
const int kDropReturnGuarded = 4;   /* a shop copy anywhere else (or its shop's holder not / no longer the other game): back, guarded */
const int kDropReturnPlain = 5;     /* anything else dropped on a shop not held here (holder unknown, not ours, not for money): back */
/* copy: the cursor item was picked from a stand-in under the guard. sameShop: the drop window shows that stand-in. copyGate: that
   shop's gate at the drop. dstGate: the drop window's shop gate (kGateOriginal when it is no shop). dstMine / srcMine: the drop /
   pick-up window is a character of ours. forMoney: isTradingForMoney 0x70F340 on (source window, drop window), 1 = yes. */
inline int DropRoute(int copy, int sameShop, int copyGate, int dstGate, int dstMine, int srcMine, int forMoney)
{
    if (copy != 0)
    {
        if (sameShop != 0) return kDropGuardedOriginal;
        if (copyGate == kGateIntercept && dstGate == kGateOriginal && dstMine != 0 && forMoney == 1) return kDropBuy;
        return kDropReturnGuarded;
    }
    if (dstGate == kGateOriginal || dstGate == kGateHeldHere) return kDropOriginal;
    if (dstGate == kGateIntercept && srcMine != 0 && forMoney == 1) return kDropSell;
    return kDropReturnPlain;
}
/* How many units a drop moves (decomp_714990 :452-460): the cursor stack, clamped to the engine's stack limit in the drop section
   (0x75D550's rule) and, on a buy, to what the purse affords (item slot 0x298). A limit that did not read (< 1) does not clamp; an
   affordable count that did not read (< 0) does not clamp (the reservation still refuses a short purse); 0 affordable = 0. */
inline int DragUnits(int cursorQty, int stackMax, int affordable)
{
    if (cursorQty <= 0 || cursorQty > kMaxUnits) return 0;
    int n = cursorQty;
    if (stackMax > 0 && stackMax < n) n = stackMax;
    if (affordable >= 0 && affordable < n) n = affordable;
    return n;
}
/* T-164 part 2 fold (H1): what the tracked shop copy becomes after an engine call that may have changed the cursor. curHeld: an item is
   on the cursor; curIsTracked: it is the tracked copy; curFromShop: the cursor's source window (MouseInventory +0x68) still shows the
   copy's stand-in. A different item from that stand-in (0x714990's swap 0x712990 + 0x748EE0, or its clamp leftover) left it under the
   guard - it is a copy too and is ADOPTED: a shop-sourced cursor item is never forgotten. An empty cursor forgets. */
const int kCurKeep = 0, kCurAdopt = 1, kCurForget = 2;
inline int CursorAfter(int curHeld, int curIsTracked, int curFromShop)
{
    if (curHeld == 0) return kCurForget;
    if (curIsTracked != 0) return kCurKeep;
    return (curFromShop != 0) ? kCurAdopt : kCurForget;
}
/* T-164 part 2 fold (H2): the put-back 0x70CC90 (an item into a window's inventory - every return of a cursor item ends there) runs
   under the guard when the item is the tracked copy, or the window shows a stand-in whose shop is not held here (the pick-up's rule).
   inGuard: already inside the guard (a guarded drop or return calls it) - as is. */
const int kPbAsIs = 0, kPbGuard = 1;
inline int PutBackGuard(int inGuard, int trackedCopy, int standIn, int gate)
{
    if (inGuard != 0 || standIn == 0) return kPbAsIs;   /* fold 2 (LOW): the tracked copy too is guarded only into a stand-in */
    if (trackedCopy != 0) return kPbGuard;
    return (PickupGuarded(gate) != 0) ? kPbGuard : kPbAsIs;
}
/* T-164 part 2 fold (M2): the engine's answer to a drop the destination cannot afford one unit of (decomp_714990 :472-476): the
   source window record's side flag + 3 - 3 "You can't afford that." (a buy), 4 "The shopkeeper can't afford that." (a sale). */
inline int DragShortResult(int srcSide) { return (srcSide != 0) ? kTrKeeperBroke : kTrCantAfford; }
/* T-164 part 2 fold 2 (HIGH): 0x7118C0 - the cursor item released over a world object (a character or container under the mouse that
   is not the window's owner, 0x712AD0's first branch) goes straight into that object's inventory (decomp_7118c0: target +0x160 ->
   slot +0x18) and 0x712AD0 then only clears the cursor (0x70CE50): no put-back, no guard. The tracked shop copy, or an item whose
   source window shows a stand-in of a shop not held here (the pick-up's rule), is never given: the original does not run and 0 is
   answered, so 0x712AD0 runs 0x70D000 - the put-back 0x70CC90 under the guard, which also ends the tracking. */
const int kGiveAsIs = 0, kGiveRefuse = 1;
inline int GiveRefused(int trackedCopy, int srcStandIn, int gate)
{
    if (trackedCopy != 0) return kGiveRefuse;
    return (srcStandIn != 0 && PickupGuarded(gate) != 0) ? kGiveRefuse : kGiveAsIs;
}

/* ---------------- dedupe ---------------- */
/* B4-4 part 1 fold (review MED 8). 0x713E50 retries a trade the first window did not take with its SECOND window (decomp_713e50
   :143-190): any result but 0 / 8 / 9 / 15 - our quiet 11 included - calls the trade again with the other window, the same
   click in the same frame. The key is therefore remembered only once a request was actually SENT (KeyAfter): a first window
   refused here (none of our characters in it, a pre-check, "No room") no longer swallows the real retry. The destination is
   deliberately NOT in the key: after a request was sent the retry must be the duplicate it is - with the destination in the key a
   retry into a second window of ours would send the same click twice (a double buy / a double sale). The code wins. */
struct ClickKey { unsigned long long window; unsigned int secHash; int x, y; unsigned int frame; };
inline unsigned int SecHash(const char* s)
{
    unsigned int h = 2166136261u;
    for (; s != 0 && *s != 0; ++s) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}
inline int SameClick(const ClickKey& a, const ClickKey& b)
{
    return (a.window == b.window && a.secHash == b.secHash && a.x == b.x && a.y == b.y && a.frame == b.frame) ? 1 : 0;
}

/* The key remembered after a trade call: the call's own when a request was sent, else the one before (a refusal marks nothing). */
inline ClickKey KeyAfter(const ClickKey& last, const ClickKey& now, int sent) { return (sent != 0) ? now : last; }

/* ---------------- the engine's stack limit (fold 2, MED 1) ---------------- */
/* FUN_14075D550(item, section) (decomp_75d550; 0x74BD70 asks it before merging onto a stack, 0x75D6E0 clamps a merged stack to it):
   1 for the record types 2, 3, 0x2E, 0x66, 0x6B, 0x6F (GameData +0x50); else the record's intFields "stackable" (GameData +0x178)
   through the section's rule 0x745B70 (decomp_745b70): the section's int at +0x98 != 0 -> 1, raised to its int at +0x88, times its
   float at +0x8C, truncated. */
inline int StackMax(int dataType, int stackable, int secNoStack, int secMin, float secMult)
{
    if (dataType == 2 || dataType == 3 || dataType == 0x2E || dataType == 0x66 || dataType == 0x6B || dataType == 0x6F) return 1;
    int s = (secNoStack != 0) ? 1 : stackable;
    if (s < secMin) s = secMin;
    return (int)((float)s * secMult);
}
/* What a matching stack of qty can still take under that limit (never below 0). */
inline int StackRoom(int max, int qty) { return (max > qty) ? max - qty : 0; }

/* ---------------- the buyer's room (review HIGH 1, T-450) ---------------- */
/* 0x713150 answers result 2 ("No room") when no add into the destination succeeded: it tries the destination's sections in turn -
   a merge onto a matching stack, else InventorySection::addItem (vt +0x10) at the first cell that takes it - and a failed try
   of every section is result 2 (decomp_713150 :380-470). There is no separate test there: the add IS the test. The requester
   holds no copy of the item it buys, so it asks of the shop's display copy WITHOUT adding whether ALL n units fit (T-450: run
   T812 passed a 27-unit stack on ONE free cell and the pack took 8 - a stack above the per-cell limit takes SEVERAL cells):
   cellRoom - the free placements of the item in every section of the buyer's inventory (CountPlacements over the engine's own cell
   test, ItCellAccepts - cell1's 0x74BD70 condition), each times that section's stack limit (CellRoom of StackMax), summed (RoomAdd);
   stackRoom - fold 2 (MED 1) / T-450: what EVERY stack of exactly that item in the buyer's inventory can still take under the
   engine's limit (StackRoom of StackMax), summed - a FULL matching stack is 0 (T-236 (1): a one-unit stack of a stackable record
   has room); n - the units bought (shift-all included); read - the buyer's sections read at all. T-450: an unreadable inventory,
   section or limit is NO room (the plugin counts it 0): the money is never taken for units that may not fit. */
const int kFitSend = 0, kFitNoRoom = 1;
const int kRoomCap = 0x3FFFFFFF;   /* room saturates here - never overflows into a negative */
inline int RoomClamp(long long v) { return (v < 0) ? 0 : ((v > (long long)kRoomCap) ? kRoomCap : (int)v); }
/* two rooms summed; a negative (unread) side counts 0 */
inline int RoomAdd(int a, int b) { return RoomClamp((long long)(a > 0 ? a : 0) + (long long)(b > 0 ? b : 0)); }
/* T-450 fold 1 (review 1): THE UNITS ONE PLACED CELL HOLDS, once the stack limit READ. The engine places ONE unit per cell when
   the limit is 1 or less (decomp_74bd70:129-145), so a limit read as 0 (or truncated to 0 or 1) is one unit per cell, never no
   room. Only a FAILED read (the plugin's read answering 0, not a computed value) is no room - the caller decides that, not this. */
inline int CellUnits(int stackMax) { return (stackMax > 1) ? stackMax : 1; }
/* the units `placements` free cells take at `stackMax` units each; an unread count or limit (<= 0) is no room */
inline int CellRoom(int placements, int stackMax)
{
    return (placements > 0 && stackMax > 0) ? RoomClamp((long long)placements * (long long)stackMax) : 0;
}
inline int BuyFit(int read, int cellRoom, int stackRoom, int n)
{
    if (read == 0) return kFitNoRoom;   /* T-450: fail safe - never take money for what may not fit */
    const int need = (n > 0) ? n : 1;   /* the engine moves at least one unit */
    return (RoomAdd(cellRoom, stackRoom) >= need) ? kFitSend : kFitNoRoom;
}
/* T-450: THE CELLS A SECTION GIVES A STACK. A stack the section's existing stacks do not absorb is placed as split-off clones of at
   most the stack limit, each at the first cell the engine's own test accepts (0x74BD70: y outer, x inner, from 0 - the order of
   coopkeep::KitFirstFreeCell), the clones placed before it occupying their cells. This counts those placements WITHOUT placing
   anything: the same scan, each accepted footprint claimed on a local copy of the occupancy so no later placement overlaps it. Every
   position the scan passes was refused (or overlaps a claimed footprint) and stays so as more is placed. WHAT IS MODELLED: the
   engine's cell condition (taken - the plugin passes ItCellAccepts' answer, 0x74BD70's test) in the engine's scan order, and the
   footprints this count has claimed. WHAT IS NOT: the section's item-count cap 0x719B80 that the engine re-checks after every
   placement (CountPlacementsCapped below applies it), and any other check the engine's add makes beyond those two - so the count
   is the engine's sequence of cells only as far as the cell test and that cap decide it. taken(ctx, x, y): non-zero = an item's
   footprint at x,y is refused now. Stops at `want`. -1 = bad sizes or a grid above kPlaceGridCap cells (the caller counts no
   room). */
typedef int (*CellTaken)(void* ctx, int x, int y);
const int kPlaceGridCap = 4096;
inline int CountPlacements(int sw, int sh, int iw, int ih, CellTaken taken, void* ctx, int want)
{
    if (taken == 0 || sw < 1 || sh < 1 || iw < 1 || ih < 1 || sw > kPlaceGridCap || sh > kPlaceGridCap || sw * sh > kPlaceGridCap) return -1;
    if (iw > sw || ih > sh || want < 1) return 0;
    unsigned char claimed[kPlaceGridCap];
    std::memset(claimed, 0, (size_t)(sw * sh));
    int count = 0;
    for (int cy = 0; cy <= sh - ih; ++cy)
        for (int cx = 0; cx <= sw - iw; ++cx)
        {
            int clash = 0;
            for (int j = 0; j < ih && clash == 0; ++j)
                for (int i = 0; i < iw && clash == 0; ++i)
                    if (claimed[(cy + j) * sw + (cx + i)] != 0) clash = 1;
            if (clash != 0 || taken(ctx, cx, cy) != 0) continue;
            for (int j = 0; j < ih; ++j)
                for (int i = 0; i < iw; ++i) claimed[(cy + j) * sw + (cx + i)] = 1;
            if (++count >= want) return count;
        }
    return count;
}
/* T-450 fold 1 (review 2): THE SECTION'S ITEM-COUNT CAP. After every placement the engine asks 0x719B80 (0x74BD70:114): the
   section's int at +0xA8 above 0 and its item list's size at or above it = the section is full. The placements left under it:
   no cap (0 or less) -> kPlaceGridCap (the grid bounds the count); else the cap less the items already there, never below 0. */
inline int PlaceCapLeft(int itemCap, int itemCount)
{
    if (itemCap <= 0) return kPlaceGridCap;
    const int have = (itemCount > 0) ? itemCount : 0;
    return (have >= itemCap) ? 0 : itemCap - have;
}
/* CountPlacements stopped at the section's item-count cap as well as at `want` (bad sizes are still -1, a full section 0) */
inline int CountPlacementsCapped(int sw, int sh, int iw, int ih, CellTaken taken, void* ctx, int want, int itemCap, int itemCount)
{
    const int left = PlaceCapLeft(itemCap, itemCount);
    return CountPlacements(sw, sh, iw, ih, taken, ctx, (want < left) ? want : left);
}

/* ---------------- the item a buy names (review MED 4) ---------------- */
/* The clicked variant's full description - the same fields the sale's request carries (maker, material, colour, quality,
   charges, function, level, unique) - and the holder takes units only from stacks that match it EXACTLY: the price was the
   clicked variant's, so a stack of another maker or grade is not the item that was priced. */
struct Desc
{
    std::string baseSid, companySid, materialSid, colorSid;
    float quality, charges;
    int functionKind, level, unique;
    Desc() : quality(0.0f), charges(0.0f), functionKind(0), level(0), unique(0) {}
};
inline int SameDesc(const Desc& a, const Desc& b)
{
    return (a.baseSid == b.baseSid && a.companySid == b.companySid && a.materialSid == b.materialSid && a.colorSid == b.colorSid
            && a.quality == b.quality && a.charges == b.charges && a.functionKind == b.functionKind && a.level == b.level
            && a.unique == b.unique) ? 1 : 0;
}


/* ---------------- the buyer's reservation (ItPursePay +price) ---------------- */
const int kResOk = 0, kResShort = 1, kResFailed = 2;
inline int Reserve(int payResult) { return (payResult == 1) ? kResOk : ((payResult == 0) ? kResShort : kResFailed); }

/* ---------------- the requester's settlement ---------------- */
const int kActCreate = 1;         /* buy ok: create the item in the pack */
const int kActRefund = 2;         /* buy refused / lapsed: the reservation back (a gift) */
const int kActCredit = 4;         /* sale ok: the price into the purse */
const int kActDestroyEscrow = 8;  /* sale ok: the escrowed object goes */
const int kActReturnEscrow = 16;  /* sale refused / lapsed: the escrowed object back into the pack */
const int kActRebuild = 32;       /* every answer: the window re-mirrors the pieces */
/* dir 0 buy, 1 sale. ok: the holder applied it. held: the reservation was taken (buy) / the escrow is held (sale). */
inline int Settle(int dir, int ok, int held)
{
    int a = kActRebuild;
    if (dir == 0) { if (ok != 0) a |= kActCreate; else if (held != 0) a |= kActRefund; }
    else { if (ok != 0) a |= kActCredit | (held != 0 ? kActDestroyEscrow : 0); else if (held != 0) a |= kActReturnEscrow; }
    return a;
}

/* ---------------- the holder ---------------- */
const int kVOk = 0;
const int kVNoHome = 1;        /* the stock (home or caravan) does not resolve here: no home for the key / no keeper, squad or worn pack */
const int kVNotHolder = 2;     /* this game does not hold that stock (home or caravan) */
const int kVKeeper = 3;        /* the keeper uid is not that stock's trader here (a caravan's keeper has a home), or has no squad pot */
const int kVPolicy = 4;        /* E36 access policy */
const int kVRefuseNext = 5;    /* TEST-ONLY lever */
const int kVSoldOut = 6;       /* fewer units of the record across the home's pieces than asked */
const int kVKeeperBroke = 7;   /* a sale the keeper cannot pay */
const int kVNoRoom = 8;        /* a sale no piece accepts */
const int kVFault = 9;         /* an engine call failed */
const int kVMismatch = 10;     /* a malformed shop block (n / price / dir) */
const int kVBusy = 11;         /* the holder's applied-record table is full (unacked records) - refused before anything moves */
const int kVBuyerShort = 12;   /* an NPC purchase whose unit price is above the NPC's budget - refused, nothing moves */
const int kVNotApplied = 13;   /* a settle-only request no holder applied (its cancelled marker is stored), or one no record can answer - refused for good */
inline int HolderCommon(int homeFound, int heldHere, int keeperOk, int policyOk, int refuseNext, int blockOk)
{
    if (blockOk == 0) return kVMismatch;
    if (homeFound == 0) return kVNoHome;
    if (heldHere == 0) return kVNotHolder;
    if (keeperOk == 0) return kVKeeper;
    if (policyOk == 0) return kVPolicy;
    if (refuseNext != 0) return kVRefuseNext;
    return kVOk;
}
inline int HolderBuy(int common, int unitsHeld, int n) { if (common != kVOk) return common; return (unitsHeld >= n) ? kVOk : kVSoldOut; }
/* keeperPay: ItKeeperPaySquad(+price) - 1 paid, 0 short, -1 failed. placed: the item was filed in a piece. */
inline int HolderSell(int common, int keeperPay, int placed)
{
    if (common != kVOk) return common;
    if (keeperPay == 0) return kVKeeperBroke;
    if (keeperPay != 1) return kVFault;
    return (placed != 0) ? kVOk : kVNoRoom;
}
/* T-447 (review 7): THE HOLDER'S SPLIT OF A SALE ITS PIECES TOOK ONLY IN PART. n / price = the request (price = unitPrice x n, checked
   before anything moves); got = the units the shop's pieces took. *soldN / *soldPrice = what the trade moved, *payBack = the cats the
   keeper takes back (the unsold units' price). got >= n: the whole sale, nothing back; got 0: nothing sold, everything back. */
inline void HolderSellSplit(int n, int unitPrice, int price, int got, int* soldN, int* soldPrice, int* payBack)
{
    if (got < 0) got = 0;
    if (got >= n) { *soldN = n; *soldPrice = price; *payBack = 0; return; }
    const int keep = (got > 0) ? TotalPrice(unitPrice, got) : 0;
    *soldN = got;
    *soldPrice = (keep > 0) ? keep : 0;
    *payBack = (price > *soldPrice) ? price - *soldPrice : 0;
}
/* T-447 (review 4 / 7): THE SELLER'S CHECK OF A CONFIRMED SALE. asked / unitPrice / askedPrice = its own request; cfN / cfPrice = the
   holder's numbers. A full sale (cfN == asked) carries the asked price, a PARTIAL one (0 < cfN < asked) the unit price times cfN, and
   no sale names more units than asked. 1 consistent, 0 a mismatch - COUNTED, NEVER REFUSED: by the time the seller reads it the holder
   has filed the units and paid (or been paid back by) its keeper, and a refusal here would leave both games one trade apart with no undo
   asked for - the holder's numbers settle it, as a full sale's price mismatch always has. */
inline int SellerConfirmOk(int asked, int unitPrice, int askedPrice, int cfN, int cfPrice)
{
    if (cfN <= 0 || cfN > asked) return 0;
    if (cfN == asked) return (cfPrice == askedPrice) ? 1 : 0;
    return (cfPrice == TotalPrice(unitPrice, cfN)) ? 1 : 0;
}
/* T-447 (review 7): the units of the seller's escrow that go back to its pack - what the escrow holds beyond what the holder sold
   (0 = all of it is sold; a confirm naming no units sells the whole escrow, as before partial sales) */
inline int SellerRest(int held, int cfN) { return (cfN > 0 && held > cfN) ? held - cfN : 0; }
/* TEST-ONLY `itemtest refusenext [n] [reason]` (T620 fold): the verdict the holder answers with is the named reason's own shop
   verdict, so the requester's message path for it (ShowResult) is reached. No reason ("generic") and a name with no shop analogue
   keep kVRefuseNext. */
inline int RefuseNextVerdict(const char* reason)
{
    if (reason == 0) return kVRefuseNext;
    const std::string r(reason);
    if (r == "keeperBroke") return kVKeeperBroke;
    if (r == "place") return kVNoRoom;
    if (r == "notOwned") return kVNotHolder;
    if (r == "policy") return kVPolicy;
    if (r == "tradeMismatch") return kVMismatch;
    if (r == "notACounter" || r == "keeperIsPlayer" || r == "keeperNoSquadPot" || r == "keeperPurseUnreadable") return kVKeeper;
    if (r == "noItem" || r == "short") return kVSoldOut;
    if (r == "fault") return kVFault;
    return kVRefuseNext;
}
/* The ENGINE's own message for a verdict (0x70EC10 TradeResult): 2 "No room", 4 "The shopkeeper can't afford". 0 = none. */
inline int ShowResult(int verdict)
{
    if (verdict == kVNoRoom) return kTrNoRoom;
    if (verdict == kVKeeperBroke) return kTrKeeperBroke;
    return 0;
}
/* The holder's take: stacks in chain order (qty per stack of the record), n wanted -> takes[i] units from each; returns the units
   planned (== n when enough). A stack is taken whole before the next is touched - 0x951BA0 removes from the first holder. */
inline int TakePlan(const int* qty, int m, int n, int* takes)
{
    int left = n;
    for (int i = 0; i < m; ++i)
    {
        takes[i] = 0;
        if (left <= 0 || qty[i] <= 0) continue;
        takes[i] = (qty[i] < left) ? qty[i] : left;
        left -= takes[i];
    }
    return n - (left > 0 ? left : 0);
}

/* ---------------- T-608: an NPC purchase from a player's counter (stock kind 2) ---------------- */
/* THE OWNER'S GAME. blockOk: one unit, a buy, the sid agrees; counterFound: the key resolves to a live counter with an inventory;
   ownerWrites: the owner rung says this game's own player owns it and is loaded there (the writer by owner, mine); refuseNext:
   the TEST-ONLY lever's verdict (0 = none); tableRoom: the applied-record table has room (every applied row waits for an ack);
   stackFound: a stack of exactly the asked item is on the counter; removed: one unit came off it. Nothing moves before kVOk. */
inline int NpcBuyHolder(int blockOk, int counterFound, int ownerWrites, int refuseNext, int tableRoom, int stackFound)
{
    if (blockOk == 0) return kVMismatch;
    if (counterFound == 0) return kVNoHome;
    if (ownerWrites == 0) return kVNotHolder;
    if (refuseNext != 0) return refuseNext;
    if (tableRoom == 0) return kVBusy;
    if (stackFound == 0) return kVSoldOut;
    return kVOk;
}
/* fold 2 (review H1 / M1): the unit's price (ShopUnitPricePod - the engine prices a ONE-UNIT clone) against the NPC's budget (the
   NPC's money, sent in the request's trade price): above it -> kVBuyerShort, as buyItem's "I can't afford that." - nothing moves. */
inline int NpcBuyAfford(int unitPrice, int budget) { return (unitPrice >= 0 && budget >= 0 && unitPrice <= budget) ? kVOk : kVBuyerShort; }
/* How many units ShopRemovePod takes for the one unit sold: a stack of one leaves whole (-1, as buyItem's 0x748EE0), else 1. */
inline int NpcBuyTakeCount(int stackQty) { return (stackQty <= 1) ? -1 : 1; }
/* fold 2 (review L2): THE OWNER'S PAYMENT. payResult: the counter's takeMoney(-price) (1 paid; a negative amount is a gift and is
   refused only when the call cannot be made); putBack: after a failed payment the removed unit went back on the counter.
   kNbOwnPaid: the sale stands at its price; kNbOwnRefuse: the unit is back - refused (kVFault), no record; kNbOwnFree: the unit
   could not be put back - the sale stands at PRICE 0 (counted), so the buyer is never charged for a sale the owner was not paid for. */
const int kNbOwnPaid = 0, kNbOwnRefuse = 1, kNbOwnFree = 2;
inline int NpcBuyOwnerPaid(int payResult, int putBack)
{
    if (payResult == 1) return kNbOwnPaid;
    return (putBack != 0) ? kNbOwnRefuse : kNbOwnFree;
}
/* THE BUYING GAME, on an answer to one of its NPC purchases. ok / hasShop / verdict: the answer; npcLive: the buyer still exists
   here. An ok is ALWAYS acknowledged (the owner keeps its record until the ack - 64 unacked make it refuse every shop trade), also
   when late (after the 10 s lapse) and also when the NPC is gone (then only the NPC side is skipped: the owner has already sold
   it - the accepted residual). A refusal moved nothing: no ack (no record was kept), the counter is re-asked. */
const int kNbGive = 1, kNbPay = 2, kNbAck = 4, kNbReask = 8;
inline int NpcBuyAnswer(int ok, int hasShop, int verdict, int npcLive)
{
    if (ok != 0 && hasShop != 0 && verdict == kVOk) return kNbAck | (npcLive != 0 ? (kNbGive | kNbPay) : 0);
    return kNbReask;
}
/* fold 2 (review M2 b): an answer to an NPC purchase whose pending row is gone (dropped after 5 minutes, or cleared with the world):
   an OK is still ACKNOWLEDGED (the owner's record must clear - 64 stuck records refuse every shop trade); nothing else is done. */
inline int NpcBuyOrphanAnswer(int forgotten, int ok, int hasShop, int verdict)
{
    if (forgotten == 0) return 0;
    return (ok != 0 && hasShop != 0 && verdict == kVOk) ? kNbAck : 0;
}
/* The buying game's pending row by age (wrap-safe ticks): kNbKeep; kNbLapse - lapseMs passed with no answer: the NPC is freed
   for another purchase, nothing is done (a late ok is still applied and acked); kNbForget - forgetMs passed since the send: the
   row is dropped (counted - an ok after that would find no row and its record would wait on the owner). */
const int kNbKeep = 0, kNbLapse = 1, kNbForget = 2;
inline int NpcBuyAge(unsigned int sentAt, unsigned int now, int lapsed, unsigned int lapseMs, unsigned int forgetMs)
{
    const unsigned int age = now - sentAt;
    if (age >= forgetMs) return kNbForget;
    if (lapsed == 0 && age >= lapseMs) return kNbLapse;
    return kNbKeep;
}

/* ---------------- a late answer (review HIGH 2) ---------------- */
/* The requester settled a lapsed row as a refusal (reservation back / escrow back). An ok that arrives after that means the
   holder applied BOTH of its halves (buy: units out of the pieces, keeper paid; sale: item filed, keeper paid out), so the two
   games are one trade apart. The requester sends MSG_ITEM_REVOKE for that id and the HOLDER undoes its own halves from its
   record of the applied row (ShopUndoApplied): buy - the units filed back, the price taken back from the keeper; sale - the
   filed units removed, the keeper refunded. Each half is undone on its own (items and money are two separate totals).
   cfOk / hasShop / verdict: the late answer; lapsedShop: the id is one of our lapsed shop rows. */
/* fold 2 (LOW 11): giveBackFailed - the lapse's give-back (the reservation refund / the escrow back) FAILED, so nothing was given
   back: the late ok SETTLES the trade (kLateSettle: create the item / credit the sale) instead of a revoke. */
const int kLateNothing = 0, kLateRevoke = 1, kLateSettle = 2;
inline int LateAnswer(int lapsedShop, int cfOk, int hasShop, int verdict, int giveBackFailed = 0)
{
    if (lapsedShop == 0) return kLateNothing;
    if (cfOk != 0 && hasShop != 0 && verdict == kVOk) return (giveBackFailed != 0) ? kLateSettle : kLateRevoke;
    return kLateNothing;
}
/* The holder's undo, counted: 2 both halves undone, 1 one half, 0 none. itemDone / moneyDone: each half's own answer. */
inline int UndoOutcome(int itemDone, int moneyDone) { return (itemDone != 0 ? 1 : 0) + (moneyDone != 0 ? 1 : 0); }

/* ---------------- wire ---------------- */
inline void PutU32(std::vector<char>* b, unsigned int v)
{ for (int i = 0; i < 4; ++i) b->push_back((char)(unsigned char)((v >> (8 * i)) & 0xFF)); }
inline int GetU32(const char* p, size_t size, size_t* at, unsigned int* v)
{
    if (*at + 4 > size) return 0;
    *v = 0;
    for (int i = 0; i < 4; ++i) *v |= ((unsigned int)(unsigned char)p[*at + i]) << (8 * i);
    *at += 4;
    return 1;
}
inline int PutS(std::vector<char>* b, const std::string& s)
{
    if (s.size() > (size_t)kMaxStr) return 0;
    b->push_back((char)(unsigned char)s.size());
    b->insert(b->end(), s.begin(), s.end());
    return 1;
}
inline int GetS(const char* p, size_t size, size_t* at, std::string* s)
{
    if (*at + 1 > size) return 0;
    const size_t n = (unsigned char)p[*at];
    if (n > (size_t)kMaxStr || *at + 1 + n > size) return 0;
    s->assign(p + *at + 1, n);
    *at += 1 + n;
    return 1;
}
inline int TagAt(const char* p, size_t size, size_t at, unsigned int tag)
{
    size_t a = at; unsigned int v = 0;
    return (p != 0 && GetU32(p, size, &a, &v) != 0 && v == tag) ? 1 : 0;
}
/* T-1 B5: a stock kind and its home key agree - a home names its key, a caravan names none (it is named by the keeper's uid). */
inline int StockKeyOk(int stock, const std::string& homeKey)
{
    if (stock == kStockHome) return homeKey.empty() ? 0 : 1;
    if (stock == kStockCaravan) return homeKey.empty() ? 1 : 0;
    if (stock == kStockNpcBuy) return homeKey.empty() ? 0 : 1;   /* T-608: the counter's key */
    return 0;
}
/* T-608: an NPC purchase names no keeper, ONE unit, a right-click entry and a buy (dir 0, trade 1 - checked by the holder) */
inline int NpcBuyShapeOk(int stock, unsigned int keeperUid, int n, int entry)
{
    if (stock != kStockNpcBuy) return 1;
    return (keeperUid == 0 && n == 1 && entry == kEntryOne) ? 1 : 0;
}
/* T-1 B5: a request block of either form starts at `at` ('SHR3', or the older 'SHR2'). */
inline int ReqTagAt(const char* p, size_t size, size_t at) { return (TagAt(p, size, at, kReqTag) != 0 || TagAt(p, size, at, kReqTagV2) != 0 || TagAt(p, size, at, kReqTagV4) != 0 || TagAt(p, size, at, kReqTagSettle) != 0) ? 1 : 0; }
inline int EncodeReq(std::vector<char>* b, const ShopReq& r)
{
    if (r.n <= 0 || r.n > kMaxUnits || r.unitPrice < 0 || r.entry < 0 || r.entry > kEntryDrag || StockKeyOk(r.stock, r.homeKey) == 0 || r.sid.empty() || r.epoch == 0) return 0;
    if (NpcBuyShapeOk(r.stock, r.keeperUid, r.n, r.entry) == 0) return 0;   /* T-608 */
    /* T-619: an 'SHR4' block carries the two price legs when both are numbers above 0 and below 100; a missing, out-of-range or NaN
       leg sends the 'SHR3' block - the trade goes on and the holder counts its legs unsent */
    const int legs = (r.settleOnly == 0 && r.cult > 0.0f && r.cult < 100.0f && r.local > 0.0f && r.local < 100.0f) ? 1 : 0;
    std::vector<char> t;
    PutU32(&t, (r.settleOnly != 0) ? kReqTagSettle : ((legs != 0) ? kReqTagV4 : kReqTag)); PutU32(&t, r.keeperUid);
    if (!PutS(&t, r.homeKey) || !PutS(&t, r.sid)) return 0;
    PutU32(&t, (unsigned int)r.n); PutU32(&t, (unsigned int)r.unitPrice); t.push_back((char)(unsigned char)r.entry); PutU32(&t, r.epoch);
    t.push_back((char)(unsigned char)r.stock);   /* T-1 B5 ('SHR3'): the stock kind */
    if (legs != 0) { unsigned int cb = 0, lb = 0; std::memcpy(&cb, &r.cult, 4); std::memcpy(&lb, &r.local, 4); PutU32(&t, cb); PutU32(&t, lb); }   /* T-619 ('SHR4') */
    b->insert(b->end(), t.begin(), t.end());
    return 1;
}
/* 1 = decoded (at moved past it); 0 = cut or out of range (the message is refused whole). Call only when ReqTagAt says so. An 'SHR2'
   block has no stock byte and is a home's; an 'SHR3' block's kind must agree with its home key (StockKeyOk). */
inline int DecodeReq(const char* p, size_t size, size_t* at, ShopReq* r)
{
    size_t a = *at; unsigned int tag = 0, n = 0, up = 0;
    if (!GetU32(p, size, &a, &tag) || (tag != kReqTag && tag != kReqTagV2 && tag != kReqTagV4 && tag != kReqTagSettle) || !GetU32(p, size, &a, &r->keeperUid)) return 0;
    if (!GetS(p, size, &a, &r->homeKey) || !GetS(p, size, &a, &r->sid) || r->sid.empty()) return 0;
    if (!GetU32(p, size, &a, &n) || !GetU32(p, size, &a, &up) || a + 1 > size) return 0;
    r->entry = (unsigned char)p[a]; ++a;
    if (!GetU32(p, size, &a, &r->epoch) || r->epoch == 0) return 0;   /* fold 2 (item 12): the epoch, never 0 */
    r->stock = kStockHome; r->cult = 0.0f; r->local = 0.0f; r->settleOnly = (tag == kReqTagSettle) ? 1 : 0;
    if (tag == kReqTag || tag == kReqTagV4 || tag == kReqTagSettle) { if (a + 1 > size) return 0; r->stock = (unsigned char)p[a]; ++a; }
    if (tag == kReqTagV4)
    {   /* T-619: the requester's two price legs, each a number above 0 and below 100 */
        unsigned int cb = 0, lb = 0;
        if (!GetU32(p, size, &a, &cb) || !GetU32(p, size, &a, &lb)) return 0;
        std::memcpy(&r->cult, &cb, 4); std::memcpy(&r->local, &lb, 4);
        if (!(r->cult > 0.0f && r->cult < 100.0f) || !(r->local > 0.0f && r->local < 100.0f)) return 0;
    }
    if (StockKeyOk(r->stock, r->homeKey) == 0) return 0;   /* T-1 B5: an empty home key only for a caravan */
    if (n == 0 || n > (unsigned int)kMaxUnits || up > 0x7FFFFFFFu || r->entry > kEntryDrag) return 0;
    if (NpcBuyShapeOk(r->stock, r->keeperUid, (int)n, r->entry) == 0) return 0;   /* T-608: an NPC purchase is one unit, no keeper */
    r->n = (int)n; r->unitPrice = (int)up; r->has = 1;
    *at = a;
    return 1;
}
inline int EncodeCf(std::vector<char>* b, const ShopCf& c)
{
    if (c.pieces.size() > (size_t)kMaxPieces || c.n < 0 || c.price < 0 || c.verdict < 0 || c.verdict > 255 || c.show < 0 || c.show > 255) return 0;
    std::vector<char> t;
    PutU32(&t, kCfTag);
    t.push_back((char)(unsigned char)c.verdict); t.push_back((char)(unsigned char)c.show);
    PutU32(&t, (unsigned int)c.n); PutU32(&t, (unsigned int)c.price);
    t.push_back((char)(unsigned char)c.pieces.size());
    for (size_t i = 0; i < c.pieces.size(); ++i) if (!PutS(&t, c.pieces[i])) return 0;
    b->insert(b->end(), t.begin(), t.end());
    return 1;
}
inline int DecodeCf(const char* p, size_t size, size_t* at, ShopCf* c)
{
    size_t a = *at; unsigned int tag = 0, n = 0, pr = 0;
    if (!GetU32(p, size, &a, &tag) || tag != kCfTag || a + 2 > size) return 0;
    c->verdict = (unsigned char)p[a]; c->show = (unsigned char)p[a + 1]; a += 2;
    if (!GetU32(p, size, &a, &n) || !GetU32(p, size, &a, &pr) || a + 1 > size) return 0;
    if (n > (unsigned int)kMaxUnits || pr > 0x7FFFFFFFu) return 0;
    const size_t k = (unsigned char)p[a]; ++a;
    if (k > (size_t)kMaxPieces) return 0;
    c->pieces.clear();
    for (size_t i = 0; i < k; ++i) { std::string s; if (!GetS(p, size, &a, &s)) return 0; c->pieces.push_back(s); }
    c->n = (int)n; c->price = (int)pr; c->has = 1;
    *at = a;
    return 1;
}

/* ---------------- fold 2 (MED 2): the shop tail, the ack and the holder's record ---------------- */
/* MSG_ITEM_REVOKE {u32 id} may carry 'SHK1' {u32 tag, u32 epoch, u8 kind} after the id: kind 0 = the SHOP REVOKE (the requester's
   answer to a late ok from a CARAVAN's holder - that game undoes its halves), kind 1 = the SHOP ACK (the requester settled that ok -
   the holder drops its record), kind 2 = the CANCEL of a HOME shop's trade, to the game holding the shop's area now (the cancel body
   below follows), kind 3 = that holder's CANCEL ANSWER (u8 kCa* follows). A revoke with no tail is the E22b give revoke, unchanged.
   epoch = the requester's (never 0); any other kind is refused. */
const unsigned int kTailTag = 0x314B4853u;   /* 'SHK1' little-endian */
const int kTailRevoke = 0, kTailAck = 1, kTailCancel = 2, kTailCancelAnswer = 3;
/* THE CANCEL (kind 2). A late or repeated ok to a home shop's trade this game settled as a refusal goes to the game the area map names
   as the holder of the shop's area NOW - never to the game first asked, which may no longer hold it - with what that holder needs to
   undo a trade it may never have applied itself (an earlier holder applied it; the shop record names its units and money):
   u8 stock | str home key | u32 keeper uid | u8 dir (0 buy, 1 sale) | u32 n | u32 price | str base sid | str company sid |
   str material sid | str colour sid | f32 quality | f32 charges | u32 function kind | u32 level | u32 unique. */
struct ShopCancel
{
    int stock;
    std::string homeKey;
    unsigned int keeperUid;
    int dir, n, price;
    Desc desc;
    ShopCancel() : stock(kStockHome), keeperUid(0), dir(0), n(0), price(0) {}
};
struct ShopTail { int has; unsigned int epoch; int kind; ShopCancel cancel; int answer; ShopTail() : has(0), epoch(0), kind(0), answer(0) {} };
/* THE HOLDER'S ANSWER (kind 3). kCaUndone: the record's entry was applied and its cancelled mark is in a STORED record (this game's
   write echoed, or a record from the world server carries it); goods and money were undone here. kCaNotApplied: no holder applied
   it - a cancelled marker is in a stored record, so no holder applies it later. kCaNotHolder: this game does not hold the shop's area
   (or the shop does not resolve here). kCaUnknown: nothing can tell whether it was applied (below the asker's floor in the record, or
   no record carries the keeper and no applied row is kept here). kCaRefused: an NPC purchase or a caravan - never undone this way.
   kCaWriteRefused: the world server refused this game's write of the mark (it lost the area), or its copy is under a refusal. */
const int kCaUndone = 0, kCaNotApplied = 1, kCaNotHolder = 2, kCaUnknown = 3, kCaRefused = 4, kCaWriteRefused = 5, kCaMax = 5;
inline const char* CancelAnswerName(int a)
{
    return a == kCaUndone ? "undone" : a == kCaNotApplied ? "not-applied" : a == kCaNotHolder ? "not-holder" : a == kCaUnknown ? "unknown"
         : a == kCaRefused ? "refused" : a == kCaWriteRefused ? "write-refused" : "?";
}
inline int CancelBodyOk(const ShopCancel& c)
{
    if (StockKeyOk(c.stock, c.homeKey) == 0 || c.dir < 0 || c.dir > 1 || c.n < 0 || c.n > kMaxUnits || c.price < 0) return 0;
    if (c.desc.baseSid.empty() || !(c.desc.quality == c.desc.quality) || !(c.desc.charges == c.desc.charges)) return 0;
    return 1;
}
inline void EncodeTail(std::vector<char>* b, unsigned int epoch, int kind)
{ PutU32(b, kTailTag); PutU32(b, epoch); b->push_back((char)(unsigned char)kind); }
/* 1 = the kind-2 tail went on; 0 = nothing went on (epoch 0, a body out of range, a string over kMaxStr) */
inline int EncodeCancelTail(std::vector<char>* b, unsigned int epoch, const ShopCancel& c)
{
    if (epoch == 0 || CancelBodyOk(c) == 0) return 0;
    std::vector<char> t;
    EncodeTail(&t, epoch, kTailCancel);
    t.push_back((char)(unsigned char)c.stock);
    if (!PutS(&t, c.homeKey)) return 0;
    PutU32(&t, c.keeperUid); t.push_back((char)(unsigned char)c.dir); PutU32(&t, (unsigned int)c.n); PutU32(&t, (unsigned int)c.price);
    if (!PutS(&t, c.desc.baseSid) || !PutS(&t, c.desc.companySid) || !PutS(&t, c.desc.materialSid) || !PutS(&t, c.desc.colorSid)) return 0;
    unsigned int q = 0, ch = 0;
    std::memcpy(&q, &c.desc.quality, 4); std::memcpy(&ch, &c.desc.charges, 4);
    PutU32(&t, q); PutU32(&t, ch); PutU32(&t, (unsigned int)c.desc.functionKind); PutU32(&t, (unsigned int)c.desc.level); PutU32(&t, (unsigned int)c.desc.unique);
    b->insert(b->end(), t.begin(), t.end());
    return 1;
}
inline int EncodeCancelAnswerTail(std::vector<char>* b, unsigned int epoch, int answer)
{
    if (epoch == 0 || answer < 0 || answer > kCaMax) return 0;
    EncodeTail(b, epoch, kTailCancelAnswer);
    b->push_back((char)(unsigned char)answer);
    return 1;
}
inline int DecodeCancelBody(const char* p, size_t size, size_t* at, ShopCancel* c)
{
    size_t a = *at;
    unsigned int ku = 0, n = 0, pr = 0, q = 0, ch = 0, fk = 0, lv = 0, un = 0;
    if (a + 1 > size) return 0;
    c->stock = (unsigned char)p[a]; ++a;
    if (!GetS(p, size, &a, &c->homeKey) || !GetU32(p, size, &a, &ku) || a + 1 > size) return 0;
    c->dir = (unsigned char)p[a]; ++a;
    if (!GetU32(p, size, &a, &n) || !GetU32(p, size, &a, &pr)) return 0;
    if (!GetS(p, size, &a, &c->desc.baseSid) || !GetS(p, size, &a, &c->desc.companySid) || !GetS(p, size, &a, &c->desc.materialSid)
        || !GetS(p, size, &a, &c->desc.colorSid)) return 0;
    if (!GetU32(p, size, &a, &q) || !GetU32(p, size, &a, &ch) || !GetU32(p, size, &a, &fk) || !GetU32(p, size, &a, &lv) || !GetU32(p, size, &a, &un)) return 0;
    if (n > (unsigned int)kMaxUnits || pr > 0x7FFFFFFFu) return 0;
    c->keeperUid = ku; c->n = (int)n; c->price = (int)pr;
    std::memcpy(&c->desc.quality, &q, 4); std::memcpy(&c->desc.charges, &ch, 4);
    c->desc.functionKind = (int)fk; c->desc.level = (int)lv; c->desc.unique = (int)un;
    if (CancelBodyOk(*c) == 0) return 0;
    *at = a;
    return 1;
}
inline int DecodeTail(const char* p, size_t size, size_t* at, ShopTail* t)
{
    size_t a = *at; unsigned int tag = 0;
    if (!GetU32(p, size, &a, &tag) || tag != kTailTag || !GetU32(p, size, &a, &t->epoch) || a + 1 > size) return 0;
    t->kind = (unsigned char)p[a]; ++a;
    if (t->kind > kTailCancelAnswer || t->epoch == 0) return 0;
    if (t->kind == kTailCancel && DecodeCancelBody(p, size, &a, &t->cancel) == 0) return 0;
    if (t->kind == kTailCancelAnswer)
    {
        if (a + 1 > size) return 0;
        t->answer = (unsigned char)p[a]; ++a;
        if (t->answer > kCaMax) return 0;
    }
    t->has = 1; *at = a;
    return 1;
}
/* THE HOLDER'S STEP ON A CANCEL. npcBuy / caravan: the stock kind named; stockOk: the stock resolves here and this game holds it
   (ShopStockResolve); exact: the current area map names this game as the holder of the shop's area; carried: a kept shop record names
   the keeper's squad; known: no record will carry it (a record is kept here without the squad, or the world server's records of the
   area have all arrived and none is there); stale: this game's copy is under a refusal; entry: the request's entry (shoprec kRs*: 0
   none, 1 applied, 2 undone, 3 below the asker's floor, 4 cancelled with its undo owed, 5 its undo claimed, 6 the cancel refused - the
   trade stands: answered kCaRefused once stored); marker: the undone
   entry moved nothing; stored: the entry in the world server's last record here (0 applied or none, 1 owed or claimed, 2 undone); appliedHere: this game keeps the
   applied row (it applied it); descOk: the cancel names this record's shop and no fewer units than the entry moved (always 1 with the
   applied row here).
   kChAnswerNow: *answer goes back at once. kChMarkOwed: mark the applied entry owed - nothing is undone here; the exact holder that
   reads the owed mark stored claims the undo, applies it once its claim is stored and marks it undone (the owed sweep) - the answer
   kCaUndone waits for that. kChMarker: write a marker, answer kCaNotApplied once it is stored. kChUndoLocal: no record carries
   the keeper - undo from the applied row kept here, answer kCaUndone at once. kChWaitStored: a mark in this game's copy only - the
   answer waits until it is stored. kChNoAnswer: not answerable yet (the shop's record has not arrived here, or the copy is under a
   refusal) - nothing is answered; the asker's repeat asks again. */
const int kChAnswerNow = 0, kChMarkOwed = 1, kChMarker = 2, kChUndoLocal = 3, kChWaitStored = 4, kChNoAnswer = 5;
inline int CancelHolderDecide(int npcBuy, int caravan, int stockOk, int exact, int carried, int known, int stale, int entry, int marker,
                              int stored, int appliedHere, int descOk, int* answer)
{
    *answer = kCaUnknown;
    if (npcBuy != 0 || caravan != 0) { *answer = kCaRefused; return kChAnswerNow; }
    if (stockOk == 0 || exact == 0) { *answer = kCaNotHolder; return kChAnswerNow; }
    if (carried == 0)
    {
        if (appliedHere != 0) return kChUndoLocal;
        return (known != 0) ? kChAnswerNow : kChNoAnswer;
    }
    if (stale != 0) return kChNoAnswer;
    if (entry == 3) return kChAnswerNow;
    if (entry == 6) { *answer = kCaRefused; return (stored >= 2) ? kChAnswerNow : kChWaitStored; }
    if (entry == 2)
    {
        *answer = (marker != 0) ? kCaNotApplied : kCaUndone;
        return (stored >= 2) ? kChAnswerNow : kChWaitStored;
    }
    if (entry == 1 || entry == 4 || entry == 5)
    {
        if (descOk == 0) { *answer = kCaRefused; return kChAnswerNow; }
        *answer = kCaUndone;
        return (entry == 1) ? kChMarkOwed : kChWaitStored;
    }
    return kChMarker;
}
/* THE ASKER'S ENDING PER ANSWER: undone, not applied, refused and unknown end the cancel (unknown is counted: the two games may be one
   trade apart); not holder and write refused keep it. */
const int kCeEnd = 0, kCeAwaitHolderChange = 1;
inline int CancelAskerOnAnswer(int answer) { return (answer == kCaNotHolder || answer == kCaWriteRefused) ? kCeAwaitHolderChange : kCeEnd; }
/* WHICH ANSWER THE ASKER TAKES: only one from the game the cancel was last sent to (an earlier holder's answer may describe a record
   that has moved on). kTakeIgnored; kTakeEnded; kTakeKept (not holder / write refused: that game is not asked again for kRefusedHoldMs
   unless the map names another holder). */
const int kTakeIgnored = 0, kTakeEnded = 1, kTakeKept = 2;
inline int CancelAnswerTake(int sentSlot, int fromSlot, int answer)
{
    if (sentSlot < 0 || fromSlot != sentSlot) return kTakeIgnored;
    return (CancelAskerOnAnswer(answer) == kCeEnd) ? kTakeEnded : kTakeKept;
}
/* WHEN THE ASKER SENDS A KEPT CANCEL - its once-a-second step, until an ending answer: no event marks a cancel or an answer lost on
   the way, so an unanswered cancel goes again each second, as a pending request does, and the holder answers a repeat from its record.
   holder: the slot the area map names as the holder of the shop's area now (-1 not known, -2 frozen: its holder left the world);
   mySlot: this game's; refusedBy / refusedAgeMs: the slot that answered not-holder / write-refused and how long ago - not asked again
   until the map names another holder or kRefusedHoldMs passed (a holder briefly not exact, in its load window, is asked again).
   kCsLocal: this game holds the area - its own holder step answers it here. */
const unsigned int kRefusedHoldMs = 10000u;
const int kCsWait = 0, kCsSend = 1, kCsLocal = 2;
inline int CancelSendDue(int holder, int mySlot, int refusedBy, unsigned int refusedAgeMs)
{
    if (holder < 0) return kCsWait;
    if (refusedBy >= 0 && holder == refusedBy && refusedAgeMs < kRefusedHoldMs) return kCsWait;
    return (mySlot >= 0 && holder == mySlot) ? kCsLocal : kCsSend;
}
/* A PENDING HOME-SHOP TRADE WHOSE ASKED GAME CANNOT ANSWER, or whose settle-only holder could not answer from a record (kVNotHolder).
   homeStock: a home shop with a known area square; holder: as CancelSendDue; refusedBy / refusedAgeMs: the game that could not answer.
   kLrSettleOnly: the SAME request (same id, same process epoch) goes to that holder flagged settle-only - answered from the shop record
   only, never applied fresh; kLrLocal: this game holds the area - its own record decides (the settle-only decision run here); kLrWait:
   no holder known (or only the game that could not answer, within kRefusedHoldMs) - the trade stays pending and is asked once the map
   names a holder (world teardown forgets it, as any pending trade); kLrLapse: a caravan, or a shop with no area square - settled here
   as a refusal. */
const int kLrLapse = 0, kLrSettleOnly = 1, kLrLocal = 2, kLrWait = 3;
inline int SettleOnlyDue(int homeStock, int holder, int mySlot, int refusedBy, unsigned int refusedAgeMs)
{
    if (homeStock == 0) return kLrLapse;
    const int s = CancelSendDue(holder, mySlot, refusedBy, refusedAgeMs);
    return (s == kCsWait) ? kLrWait : ((s == kCsLocal) ? kLrLocal : kLrSettleOnly);
}
/* A PENDING HOME-SHOP TRADE WAITING FOR A HOLDER (soWait) AROUND A GAME SAVE: what it holds out of the save goes back in for it and
   is taken out again when the save returns - a buy's reserved price into the purse (kPsPurse), a sale's escrowed item into the
   seller's inventory (kPsEscrow); kPsNone: nothing held, or not waiting (a trade asked of a game is answered within seconds). World
   teardown drops a waiting trade with the world (its characters are gone by then), counted. */
const int kPsNone = 0, kPsPurse = 1, kPsEscrow = 2;
inline int PendSaveHold(int used, int soWait, int dir, int held, int haveEscrow)
{
    if (used == 0 || soWait == 0) return kPsNone;
    if (dir == 0) return (held != 0) ? kPsPurse : kPsNone;
    return (haveEscrow != 0) ? kPsEscrow : kPsNone;
}
/* WHAT ONE PENDING ROW NEEDS AROUND A SAVE (phase 0 before it; 1 after it, and each tick, answer or leave): a hold still put back
   is taken out FIRST, whatever the phase (a save that did not finish left it) - never put back over it; only then, before a save,
   does a waiting trade put its hold back (PendSaveHold). kPaNone, kPaTakeOut, kPaPurse, kPaEscrow. */
const int kPaNone = 0, kPaTakeOut = 1, kPaPurse = 2, kPaEscrow = 3;
inline int PendSaveAct(int phase, int saveHeld, int used, int soWait, int dir, int held, int haveEscrow)
{
    if (saveHeld != kPsNone) return kPaTakeOut;
    if (phase != 0) return kPaNone;
    const int h = PendSaveHold(used, soWait, dir, held, haveEscrow);
    return (h == kPsPurse) ? kPaPurse : ((h == kPsEscrow) ? kPaEscrow : kPaNone);
}
/* A HOLD TAKEN OUT AGAIN - after the save, or a save that did not finish (taken out first at the next save's start, each tick, an answer
   or a leave, before anything reads the row): tookOut 0 ends the trade as a refusal with the give-back already in place - *held 0 (a
   buy's price stays with the buyer: no refund follows), *dropEscrow 1 (a sale's item stays with the seller: nothing is destroyed or
   given back) - and a later ok from a holder is answered by a cancel. 1 = end the trade. */
inline int PendSaveTakeEnd(int saveHeld, int tookOut, int* held, int* dropEscrow)
{
    *dropEscrow = 0;
    if (saveHeld == kPsNone || tookOut != 0) return 0;
    if (saveHeld == kPsPurse) *held = 0; else *dropEscrow = 1;
    return 1;
}
/* THE HOLDER'S ANSWER TO A SETTLE-ONLY REQUEST, from the shop record only. keeperFound: the keeper resolves here; exact / carried /
   known / stale / entry / stored: as CancelHolderDecide. kSoFromRecord: a holder applied it - ok from the entry, nothing moves;
   kSoMarker: no entry - a cancelled marker is written and the refusal waits until it is stored (a refused write decides again: an
   applied entry stored meanwhile is answered from); kSoRefuse: refused for good (kVNotApplied: cancelled and stored, below the asker's
   floor, or no record will carry the keeper); kSoNotHolder: not answerable here now (not the exact holder, the record not arrived, a
   refused copy) - refused with kVNotHolder, the asker asks again; kSoWaitStored: cancelled in this game's copy only - waits. */
const int kSoFromRecord = 0, kSoMarker = 1, kSoRefuse = 2, kSoNotHolder = 3, kSoWaitStored = 4;
inline int SettleOnlyDecide(int keeperFound, int exact, int carried, int known, int stale, int entry, int stored)
{
    if (keeperFound == 0 || exact == 0) return kSoNotHolder;
    if (carried == 0) return (known != 0) ? kSoRefuse : kSoNotHolder;
    if (stale != 0) return kSoNotHolder;
    if (entry == 1) return kSoFromRecord;
    if (entry == 0) return kSoMarker;
    if (entry == 2) return (stored != 0) ? kSoRefuse : kSoWaitStored;
    return kSoRefuse;
}
/* The holder's applied record: kept until the requester's ACK - no time expiry, never evicted. Keyed {peer, epoch, id}: request ids
   restart with the requester's process, the epoch does not (item 12). */
inline int SameRecord(unsigned int peerA, unsigned int epochA, unsigned int idA, unsigned int peerB, unsigned int epochB, unsigned int idB)
{ return (peerA == peerB && epochA == epochB && idA == idB) ? 1 : 0; }
/* A NEW shop trade is admitted only while the table has room: a full table refuses it before anything moves (kVBusy). */
inline int RecordAdmit(size_t size, size_t cap) { return (size < cap) ? 1 : 0; }
/* A record of this peer under an older epoch belongs to a requester process that has ended (its link went down and its world left):
   it can never be acked or revoked - dropped when that peer's request with the new epoch arrives. */
inline int RecordStale(int samePeer, unsigned int recEpoch, unsigned int nowEpoch) { return (samePeer != 0 && recEpoch != nowEpoch) ? 1 : 0; }
const int kRecAckDrop = 1, kRecUndo = 2, kRecAckMissed = 3, kRecRevokeMissed = 4;
inline int RecordOnTail(int found, int kind)
{
    if (kind == kTailAck) return (found != 0) ? kRecAckDrop : kRecAckMissed;
    return (found != 0) ? kRecUndo : kRecRevokeMissed;
}
/* The per-process epoch, mixed (FNV-1a) from three readings taken once at the first shop request; never 0 (0 = none on the wire). */
inline unsigned int MakeEpoch(unsigned int a, unsigned int b, unsigned int c)
{
    unsigned int h = 2166136261u;
    const unsigned int v[3] = { a, b, c };
    for (int i = 0; i < 3; ++i) for (int k = 0; k < 4; ++k) { h ^= (v[i] >> (8 * k)) & 0xFFu; h *= 16777619u; }
    return (h == 0) ? 1u : h;
}

}   /* namespace coopshop */
#endif
