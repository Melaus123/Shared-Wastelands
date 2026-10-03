/* T-164 B4-1 - THE REAL CHARGE OF A PLAYER TRADE, and the keeper lever's affordable pick. Pure arithmetic, no engine types.
 *
 * The plugin reads the buyer's purse (the player faction's one global Ownerships, ItPurseRead) immediately before and after
 * the engine's own purchase/sale step - InventoryGUI::RClickAutoTrade 0x713150 (right-click) or placeItemFromMouse 0x714990
 * (drag) - and keeps the difference as the CHARGE:
 *
 *   charge = purseBefore - purseAfter      a BUY is > 0 (cats left the purse), a SALE is < 0 (cats came in), 0 = nothing moved
 *
 * The undo of a charge goes through ItPursePay, which REMOVES its amount, so the exact undo is ItPursePay(-charge): a refused
 * buy of charge 500 pays -500 (gives 500 back), a refused sale of charge -300 pays 300 (takes the 300 back). B4-2 uses this.
 *
 * WHICH PENDING RECORD A CHARGE BELONGS TO. Every ring entry carries the ticket it was rung under (seq = ticket before the
 * increment). The detour samples the ticket before and after the engine call, so the entries that call rang are exactly the
 * seqs in [lo, hi). The drain's ItCrossTransfer holds the two halves it paired; if either half's seq is in the window, the
 * pending record it creates is the one that call charged. Unsigned arithmetic, so the comparison survives the wrap. */
#ifndef COOP_TRADECHARGE_H
#define COOP_TRADECHARGE_H

namespace tradecharge {

const int kRouteRClick = 0;
const int kRouteDrag = 1;

inline int Charge(int purseBefore, int purseAfter) { return purseBefore - purseAfter; }

/* What ItPursePay must be handed to put the purse back exactly (it REMOVES its amount). */
inline int UndoPay(int charge) { return -charge; }

/* 1 = a ring entry stamped `seq` was rung inside the call window [lo, hi). An empty window (hi == lo) holds nothing. */
inline int SeqInWindow(unsigned long seq, unsigned long lo, unsigned long hi)
{
    return ((seq - lo) < (hi - lo)) ? 1 : 0;
}

/* B4-1 fold: 1 = a recorded charge belongs to a pending record. Either half's seq must lie in the call's window [lo, hi), AND the
   charge's buyer must be the record's own character (a move rung on another thread inside the window is not this trade), AND
   the record must be a trade (trade 1 buy / 2 sell). A charge with no buyer never fits. */
inline int ClaimFits(unsigned long srcSeq, unsigned long dstSeq, unsigned long lo, unsigned long hi,
                     unsigned int buyerUid, unsigned int mineUid, int trade)
{
    if (buyerUid == 0 || buyerUid != mineUid || trade == 0) return 0;
    /* T-164 B4-2 (B4-1 review note d): a half whose seq is 0 is a CLEARED entry (ItClear - ItLootRouteGive's), not a ticket the
       call rang; a window that contains 0 (the wrap, or the first tickets of a session) must not hand it the charge. */
    const int srcIn = (srcSeq != 0 && SeqInWindow(srcSeq, lo, hi) != 0) ? 1 : 0;
    const int dstIn = (dstSeq != 0 && SeqInWindow(dstSeq, lo, hi) != 0) ? 1 : 0;
    return (srcIn != 0 || dstIn != 0) ? 1 : 0;
}

/* 1 = the call is recorded as a trade: the purse was read on both sides and either it moved, or (right-click) the engine's
   own isTradingForMoney said this pair trades for money (a trade the buyer could not afford is still a trade: charge 0). */
inline int IsTrade(int haveBefore, int haveAfter, int charge, int forMoney)
{
    if (haveBefore == 0 || haveAfter == 0) return 0;
    return (charge != 0 || forMoney == 1) ? 1 : 0;
}

/* The keeper lever's pick: 1 = this candidate replaces the best so far. Only a value the purse covers is a candidate, and the
   cheaper one wins (the first of equals stays). An unreadable value or purse is never a candidate - the caller falls back to
   its first qualifying item. A FREE item (value 0) is never a candidate either: T555 picked 0-value items, which trade for
   nothing and so cannot show a charge. */
inline int CheaperAffordable(int haveValue, int value, int havePurse, int purse, int haveBest, int bestValue)
{
    if (haveValue == 0 || value <= 0 || havePurse == 0 || value > purse) return 0;
    return (haveBest == 0 || value < bestValue) ? 1 : 0;
}

/* `cats=<n>`: 1 and *out = n for a whole non-negative number up to 100000000, 0 for anything else (including the bare word). */
inline int ParseCats(const char* tok, int* out)
{
    *out = -1;
    if (tok == 0) return 0;
    const char* k = "cats=";
    int i = 0;
    for (; k[i] != 0; ++i) if (tok[i] != k[i]) return 0;
    if (tok[i] == 0) return 0;
    long long v = 0;
    for (; tok[i] != 0; ++i)
    {
        if (tok[i] < '0' || tok[i] > '9') return 0;
        v = v * 10 + (tok[i] - '0');
        if (v > 100000000LL) return 0;
    }
    *out = (int)v;
    return 1;
}

/* ---- T-164 B4-2 / T-199: WHAT A REFUSED TRADE GIVES BACK ----
   A refusal on this game after ItCrossTransfer's early put-back, a holder refusal, a lapse and a cancel at confirm all undo a
   move whose money the local engine ALREADY moved. What goes back is decided here, from the charge the pending record holds:
     a MEASURED charge (have 1, measured 1)   -> exactly UndoPay(charged); a measured 0 pays nothing (the engine moved nothing)
     a right-click kept UNMEASURED (have 1, measured 0: a purse side could not be read) that the engine's own
       isTradingForMoney called a money trade (forMoney 1), with a carried price > 0
                                              -> the carried price (the one case with evidence of a charge and no amount)
     anything else - no record at all         -> NOTHING (kRefundNoRecord, logged by the caller). A drag that cost 0 is never
       recorded, so "no record" cannot be told from "free"; a price is never paid without evidence. Money is never invented.
   *pay is what ItPursePay must be handed (it REMOVES its amount): a buy gives back (< 0), a sale takes back (> 0). */
const int kRefundNothing = 0;
const int kRefundCharge = 1;
const int kRefundPrice = 2;
const int kRefundNoRecord = 3;
inline int RefundPick(int trade, int have, int measured, int charged, int route, int forMoney, int price, int* pay)
{
    *pay = 0;
    if (trade != 1 && trade != 2) return kRefundNothing;
    if (have != 0 && measured != 0)
    {
        *pay = UndoPay(charged);
        return (charged != 0) ? kRefundCharge : kRefundNothing;
    }
    if (have != 0 && route == kRouteRClick && forMoney == 1 && price > 0)
    {
        *pay = (trade == 1) ? -price : price;
        return kRefundPrice;
    }
    return kRefundNoRecord;
}

/* T-164 B4-2: 1 = a call IsTrade did not record is still kept, UNMEASURED: a right-click the engine's own isTradingForMoney
   called a money trade whose purse could not be read on one side. Without it that trade would leave no record at all and its
   refusal could give back nothing; with it RefundPick may use the carried price. A drag is never kept this way. */
inline int KeepUnmeasured(int route, int haveBefore, int haveAfter, int forMoney)
{
    return (route == kRouteRClick && forMoney == 1 && (haveBefore == 0 || haveAfter == 0)) ? 1 : 0;
}

/* T-164 B4-2: the refusal exits ItCrossTransfer can take AFTER its early put-back - the `reason=` of the refund line and the
   gate the TEST-ONLY lever `itemtest refusehere <gate>` forces. */
const int kGateTakerBox = 0;      /* our side of the move is a box */
const int kGateCaravanNotIntercepted = 1;   /* T-1 B5 (was kGateWandering, the retired wandering refusal): MARKS a caravan move the drain's shopNotIntercepted exit already refuses - it cannot force it (fold 1 F5) */
const int kGateNoNotebook = 2;    /* decision 44: the notebook is down past its grace */
const int kGateNoAnswer = 3;      /* no area map for the counter's sector */
const int kGateUnloading = 4;     /* the counter's zone is unloading / gone */
const int kGateNoHolder = 5;      /* a fresh map names no holder */
const int kGateUnresolved = 6;    /* the counter's building could not be resolved */
const int kGatePolicy = 7;        /* E36 access policy */
const int kGateTableFull = 8;     /* no free pending row */
const int kGateNoItemName = 9;    /* the item's record could not be read */
const int kGateBag = 10;          /* a given pack's contents could not be read */
const int kGateSendFailed = 11;   /* the request could not be sent */
const int kGateCount = 12;
inline const char* GateName(int g)
{
    static const char* const names[kGateCount] = { "takerBox", "caravanNotIntercepted", "noNotebook", "noAnswer", "unloading", "noHolder",
                                                   "unresolved", "policy", "tableFull", "noItemName", "bagUnreadable", "sendFailed" };
    return (g >= 0 && g < kGateCount) ? names[g] : "?";
}
/* the gate named `s`, or -1 */
inline int GateIndex(const char* s)
{
    if (s == 0 || s[0] == 0) return -1;
    for (int g = 0; g < kGateCount; ++g)
    {
        const char* n = GateName(g);
        int i = 0;
        while (n[i] != 0 && s[i] == n[i]) ++i;
        if (n[i] == 0 && s[i] == 0) return g;
    }
    return -1;
}

/* T-164 B4-2 fold (T-215): THE FIRST TICKET the items ring hands out (g_ticket's start). Not 0: ClaimFits reads a seq of 0 as a
   CLEARED entry, so a process whose first move rang ticket 0 could never claim that move's charge. */
const long kFirstTicket = 1;

/* T-164 B4-2 fold (T-215): THE KEEPER'S HALF FOLLOWS THE REFUND THE PURSE REALLY GOT. `pay` is what ItPursePay moved (it REMOVES
   its amount). 1 = the counter keeper's pot is undone by exactly |pay|, in the direction the purse moved the other way:
   *keeperTrade 1 (the purse got cats back, pay < 0 - the pot gives back what a purchase put in) or 2 (the purse gave cats back,
   pay > 0 - the pot gets back what a sale paid out). 0 = nothing moved, nothing to follow. NOT gated on the carried price: a
   price-0 request whose engine step really moved cats is followed by exactly that amount (reviewer finding, T-215 item 1). */
inline int KeeperFollow(int pay, int* keeperTrade, int* amount)
{
    *keeperTrade = 0; *amount = 0;
    if (pay == 0 || pay < -2147483647) return 0;   /* -INT_MIN has no int */
    *keeperTrade = (pay < 0) ? 1 : 2;
    *amount = (pay < 0) ? -pay : pay;
    return 1;
}
/* T-215 B4-2 (money re-check 2a-2c): WHAT THIS GAME KNEW OF THE COUNTER'S KEEPER WHEN ITS ENGINE MOVED THE MONEY. Decided at the
   START of the move, from the building ItCrossTransfer resolved at its gate (before the put-back) - never by re-resolving the box
   key at a refusal, where the very miss that refused the move made the keeper look unknown.
     kKeeperRunner    (i)   the keeper's uid is this game's: its engine moved the REAL pot, which this game announces.
     kKeeperLocalOnly (ii)  the keeper resolved but carries no uid (an unregistered orphan, a twin whose adoption was refused):
                            a local pot nobody announces and nobody overwrites - this game's engine moved it and nothing else will.
     kKeeperCopy      (iii) another game runs the keeper: this pot is a copy its runner's announcement overwrites.
     kKeeperUnknown   (iv)  a counter trade whose keeper could not be resolved at all.
   kKeeperNone: not a counter trade - there is no keeper side. */
const int kKeeperNone = 0;
const int kKeeperRunner = 1;
const int kKeeperLocalOnly = 2;
const int kKeeperCopy = 3;
const int kKeeperUnknown = 4;
inline int KeeperClassAtMove(int counterTrade, int keeperResolved, unsigned int uid, int uidMine)
{
    if (counterTrade == 0) return kKeeperNone;
    if (keeperResolved == 0) return kKeeperUnknown;
    if (uid == 0) return kKeeperLocalOnly;
    return (uidMine != 0) ? kKeeperRunner : kKeeperCopy;
}
/* T-215 B4-2 (re-check 2a/2b): A REFUND IS NEVER WITHHELD - the purse is always paid what RefundPick says. Withholding was the
   wrong side in the common case: the engine's takeMoney moved a LOCAL keeper pot, which is normally a copy the runner's
   announcement overwrites, so withheld cats were destroyed rather than parked. What the keeper's side then does, once the purse
   really moved (purseMoved 0 = nothing to follow):
     kSideRunnerUndo   (i)   ItKeeperUndoOwn - this game's engine moved the real pot; undone by the refunded amount.
     kSideLocalUndo    (ii)  the local-only pot undone directly by the refunded amount, through the engine's own route the trade used.
     kSideLeftToRunner (iii) nothing here: the pot is announced by its runner, and the runner never saw the charge (the trade was
                             refused before anything reached it), so no money is created.
     kSideUnknownPaid  (iv)  paid, keeper side not undone - logged and counted (refundKeeperUnknown), so runs can see it. */
const int kSideNothing = 0;
const int kSideRunnerUndo = 1;
const int kSideLocalUndo = 2;
const int kSideLeftToRunner = 3;
const int kSideUnknownPaid = 4;
inline int KeeperSideAfterRefund(int keeperClass, int purseMoved)
{
    if (purseMoved == 0) return kSideNothing;
    if (keeperClass == kKeeperRunner) return kSideRunnerUndo;
    if (keeperClass == kKeeperLocalOnly) return kSideLocalUndo;
    if (keeperClass == kKeeperCopy) return kSideLeftToRunner;
    if (keeperClass == kKeeperUnknown) return kSideUnknownPaid;
    return kSideNothing;
}
/* T-215 B4-2 (re-check 1b): the pot change this game's engine made on the keeper for a refund of `pay` - the same basis as the
   undo (KeeperFollow of RefundPick's pay), so what KeeperPotAnnounced nets out is exactly what a refusal would undo. */
inline int KeeperPotMoved(int pay)
{
    int kt = 0, amt = 0;
    if (KeeperFollow(pay, &kt, &amt) == 0) return 0;
    return (kt == 1) ? amt : -amt;
}

/* The ItPursePay amount that takes a purse of `current` to `target` (it REMOVES its amount). */
inline int PayToSet(int current, int target) { return current - target; }

}   /* namespace tradecharge */

#endif
