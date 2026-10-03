#pragma once
/* give1 (2026-09-26): the pure decisions of the player-to-player item handover's late and cancelled paths. Header-only, no
   engine types, shared by the plugin (items.cpp) and the offline suite (coop-test). */

namespace coopgive {

/* A CONFIRM whose request our own 10 s timeout already expired (ApplyItemConfirm's late branch).
   kLateGiveRemove: a GIVE (trade 0, dir 1) the owner says it KEPT - the owner's own expiry lets a give STAND, so the item is
   on both games until the giver removes its copy. kLateSell / kLateBuyNote: the P7q arms, unchanged (they need the refund to
   have moved: `refunded`). Anything else: nothing is done. */
const int kLateNone = 0, kLateGiveRemove = 1, kLateSell = 2, kLateBuyNote = 3;
inline int LateConfirmAction(int ok, int trade, int dir, int refunded)
{
    if (ok == 0) return kLateNone;
    if (trade == 0 && dir == 1) return kLateGiveRemove;
    /* review-give1 M2: a SALE whose timeout refund did not move money (price unknown, character not spawned) kept its credit
       and the owner kept the give - the item is on both games exactly as a late give is, so it is removed the same way */
    if (trade == 2 && dir == 1 && refunded == 0) return kLateGiveRemove;
    if (refunded == 0) return kLateNone;
    return trade == 2 ? kLateSell : kLateBuyNote;
}

/* The late GIVE's removal. A pack whose rows were sent is removed only if it is still the same object holding the same rows
   (bagSame); otherwise nothing is removed and it counts "changed". removedExact is ItTakerRemoveExact's answer:
   1 removed, 0 nothing there / not the same kind, -1 a different object at the slot. */
const int kGiveRemoved = 1, kGiveGone = 2, kGiveChanged = 3;
inline int LateGiveOutcome(bool bagSent, bool bagSame, int removedExact)
{
    if (bagSent && !bagSame) return kGiveChanged;
    if (removedExact == 1) return kGiveRemoved;
    if (removedExact < 0) return kGiveChanged;
    return kGiveGone;
}

/* A request the owner ANSWERED ok but that this game cancels at confirm (its own copy could not be removed / placed, or its
   character's section could not be resolved). The owner undoes its half and puts the shopkeeper's cats back (the revoke's
   ItRollbackKeeper for a sale, ItRollbackTake's for a purchase); the engine's own local purse move must be undone here too,
   or money is created (a sale's credit) or destroyed (a purchase's debit). A plain give/take moves no money. */
const int kCancelNone = 0, kCancelSale = 1, kCancelBuy = 2;
inline int CancelRefundOwed(int dir, int trade)
{
    if (dir == 1 && trade == 2) return kCancelSale;
    if (dir == 0 && trade == 1) return kCancelBuy;
    return kCancelNone;
}

/* TEST-ONLY `itemtest confirmdelay <seconds>`: 0..30, digits only (at most three), 0 = off. */
inline bool ConfirmDelayParse(const char* s, int* out)
{
    if (s == 0 || s[0] == 0) return false;
    int v = 0;
    for (int i = 0; s[i] != 0; ++i)
    {
        if (i >= 3 || s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    if (v > 30) return false;
    *out = v;
    return true;
}

} // namespace coopgive
