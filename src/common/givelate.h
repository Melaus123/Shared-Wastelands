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

/* T-853: SILENCE MEANS ONE THING ON BOTH GAMES. The giver's request lapses after kGiverLapseMs and its item is put back; the
   owner's GIVE row stands (the give is taken as done) only after kOwnerGiveStandMs of the giver's time in the world (T-889:
   OwnerPendAgeMs below - a giver that dropped is not a giver that agreed). The giver sends a REVOKE the moment its
   request lapses, so the owner's window must be clearly longer than the giver's or that REVOKE would find a row already
   ended and the item would exist on both games. Twice the giver's lapse leaves a whole lapse of time for the REVOKE to land.
   A TAKE keeps its own timing (the escrow road). */
const unsigned int kGiverLapseMs = 10000;       /* items.cpp kPendTimeoutMs is defined from this one constant */
const unsigned int kOwnerGiveStandMs = 2u * kGiverLapseMs;   /* twice the giver's lapse, by construction - the order cannot invert */
inline bool GiveStandOutlivesLapse(unsigned int standMs, unsigned int lapseMs) { return standMs >= 2u * lapseMs; }
/* The owner's expiry window for one pending row: a GIVE's stand window, a TAKE's own timeout. */
inline unsigned int OwnerPendWindowMs(int dir, unsigned int takeMs, unsigned int giveStandMs) { return (dir != 0) ? giveStandMs : takeMs; }
/* T-889: the age of one owner row that coopesc::ExpireAction judges. A GIVE row's age is hereMs - a clock that runs only while its
   giver is in the world, stepped like the escrow's quiet clock (coopesc::QuietStep: held while the giver is away, a long frame
   counts as the cap) - so a giver that dropped or is reconnecting still finds its row when its REVOKE comes. A TAKE keeps the wall
   time since the grant. */
inline unsigned int OwnerPendAgeMs(int dir, unsigned int wallMs, unsigned int hereMs) { return (dir != 0) ? hereMs : wallMs; }
/* T-889: does this owner row answer a REVOKE for request `id`? Only a GIVE row (dir 1) with that request id. samePeerId: the
   REVOKE came from the connection id the request came from (kGmExact - preferred). samePlayer: it came from the same PLAYER
   (coopesc::SameSender of the player key kept at the grant and the sender's) - a reconnected giver has a new connection id
   (kGmPlayer). Anything else: not this row (kGmNone). */
const int kGmNone = 0, kGmExact = 1, kGmPlayer = 2;
inline int GiveRowMatch(int rowDir, unsigned int rowId, unsigned int id, int samePeerId, int samePlayer)
{
    if (rowDir != 1 || id == 0 || rowId != id) return kGmNone;
    if (samePeerId != 0) return kGmExact;
    return (samePlayer != 0) ? kGmPlayer : kGmNone;
}
/* T-889: a clock held because the other game is away is logged once per row / kept REVOKE: 1 = say it now. */
inline int AwayHoldFirst(int here, int saidBefore) { return (here == 0 && saidBefore == 0) ? 1 : 0; }

/* T-853: which lapsed requests the giver revokes at once. A GIVE (dir 1) that is not a take-one consume - a plain give, and a
   sale whose lapse refund really moved the cats back (the owner's revoke puts the shopkeeper's cats back too, so both games
   agree the sale did not happen). A sale whose refund did NOT move money keeps the old road: the owner's give stands and the
   late yes removes our copy, so the engine's local credit is matched by a completed sale. A GIVE whose put-back did not end
   somewhere real (backReal 0: kept alive in no inventory, still parked for our box, a faulted put-back) keeps the old road too:
   a REVOKE then would leave the item on neither game. */
inline int LapseRevoke(int dir, int trade, int refunded, int consume, int backReal)
{
    if (dir != 1 || consume != 0) return 0;
    if (trade == 2 && refunded == 0) return 0;
    if (backReal == 0) return 0;
    return 1;
}
/* A late ok for a GIVE already revoked at its lapse: the ok proves the owner's row exists now, and the first REVOKE may have
   reached the owner before that row did (the request and the REVOKE can travel different roads) and found nothing - so the
   REVOKE is sent again. A late no needs nothing: the owner built nothing. */
inline int LateYesReRevoke(int revokedAtLapse, int ok) { return (revokedAtLapse != 0 && ok != 0) ? 1 : 0; }

/* T-853: a late yes whose recorded object is no longer at the recorded cell (the lapse's put-back made a new object). The item
   at that cell is our copy when it has the recorded base sid and EXACTLY the quantity the request carried - a merge into that
   cell since (a different count) is not ours to take. The quality and the level must be the ones the request sent (another
   item of the same record put there since is not ours). A pack must also still digest to the rows that were sent. */
inline bool LateGiveSquareMatch(bool sidSame, int haveQty, int wantQty, bool bagSent, bool bagDigestSame, bool qualitySame,
                                bool levelSame)
{
    if (!sidSame || wantQty <= 0 || haveQty != wantQty) return false;
    if (!qualitySame || !levelSame) return false;
    if (bagSent && !bagDigestSame) return false;
    return true;
}
/* Two quality readings are the same within a thousandth; a value that is not a number never matches. */
inline bool LateGiveQualitySame(float have, float want)
{
    const float d = have - want;
    return d < 0.001f && d > -0.001f;
}
/* Asked only when the exact test could not match by object: a different object at the cell (-1), or a pack whose object
   changed (bagSent and the object test failed). */
inline bool LateGiveTrySquare(int removedExact, bool bagSent, bool bagSameByObject)
{
    if (bagSent && !bagSameByObject) return true;
    return removedExact < 0;
}

/* T-853: a REVOKE that could not be handed to the link is kept and tried again on a recurring check - every kRevokeRetryMs,
   for at most kRevokeKeepMs (the owner's GIVE row stands after its own window, so a REVOKE older than that can only find a
   row already ended). kRvKeep: try again later; kRvDone: handed to the link; kRvGiveUp: kept too long, dropped and counted.
   T-889: ageMs counts only while the game the REVOKE goes to is reachable (items.cpp steps it with coopesc::QuietStep): the
   owner's row counts only while the giver is in its world, so a long drop must not run this clock out while that row waits. */
const int kRevokeListCap = 32;
const unsigned int kRevokeRetryMs = 1000;
const unsigned int kRevokeKeepMs = 60000;
const int kRvKeep = 0, kRvDone = 1, kRvGiveUp = 2;
inline int RevokeRetryAction(int sentNow, unsigned int ageMs, unsigned int keepMs)
{
    if (sentNow != 0) return kRvDone;
    return (ageMs >= keepMs) ? kRvGiveUp : kRvKeep;
}

/* T-853: the OWNER's keeper half when a GIVE it granted (a sale to its counter) is revoked - each sale's money undone exactly once.
   kRkNone: no cats moved at the grant. kRkKept: the object the owner built was NOT removed - it stays at the counter, so the sale
   stands on this game and the keeper's payment stands with it (refunding it too would leave the keeper the item for free).
   kRkRunner: the keeper is a copy here, run by another game - the real pot is that game's to undo (the giver's own refund
   undoes it when the giver runs the keeper); a write on this copy moves no real pot, and would be a second payback the moment
   this game came to run the keeper. kRkUndo: this game runs the keeper (or it is a local-only keeper) - undone here. */
const int kRkNone = 0, kRkUndo = 1, kRkRunner = 2, kRkKept = 3;
inline int RevokeKeeperAction(int traded, int removed, int keeperCopyHere)
{
    if (traded == 0) return kRkNone;
    if (removed == 0) return kRkKept;
    if (keeperCopyHere != 0) return kRkRunner;
    return kRkUndo;
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
