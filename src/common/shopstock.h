/* T-164 B4-4 - THE SHOP-STOCK LIST'S PURE DECISIONS. No engine types; the plugin (items.cpp) and the offline suite compile this.
 *
 * WHAT A SHOP IS (F626 corrected, Read): the trade window is keeper -> getOwnerships 0x794B00 -> home building (+0x38) -> interior
 * -> every piece with an inventory. The old test (UseableStuff::shopOwner +0x360, "a counter") binds at most one building per trader
 * squad and trade never reads it, so it never matched a real shop. The list B4-3 keeps (items.cpp ItThRegisterPieces: trader squad
 * -> formal leader's home -> InteriorPieceIds -> pieces with an inventory) is the list; its squad is the keeper.
 *
 *   PieceRecognised    - one piece, from this game's own list: registered AND its squad answered recently AND the squad still
 *                        names the same home. The holder re-checks a request with this; the requester asks it first.
 *   (RequesterVerdict, the drain's trade test, was removed by the B4-4 intercept - coopshop::Gate in shopwire.h decides now.)
 *   PickPieceByRecord  - which piece a window removal came out of: the first piece (chain order) that holds the item's base record
 *                        - the engine's own rule at _removeItemFromInventories 0x951BA0; only an UNREADABLE record falls back to the
 *                        first piece; a readable record that no piece holds is no piece (the old any-section arm is gone).
 *   KeeperPayRoute     - where the keeper's half of a trade is paid: the keeper squad's own money (Ownerships +0x88 through the
 *                        keeper's getOwnerships - where the engine's window trade puts it, R-a), or nowhere; never a piece's own
 *                        takeMoney (UseableStuff::takeMoney 0x54DAD0 falls back to the building's FACTION's purse). */
#ifndef COOP_SHOPSTOCK_H
#define COOP_SHOPSTOCK_H

namespace shopstock {

/* registered: the piece key is in the list. squadFound: the list's squad row exists. squadHome: that row names a home.
   ageMs / freshMs: how long ago the row was answered, and the bound. homeSame: the row's home key is the piece's home key
   (a recycled squad pointer answering for another home is not this shop). 1 = a shop piece whose keeper is that squad. */
inline int PieceRecognised(int registered, int squadFound, int squadHome, unsigned long ageMs, unsigned long freshMs, int homeSame)
{
    if (registered == 0 || squadFound == 0 || squadHome == 0) return 0;
    if (ageMs >= freshMs) return 0;
    return (homeSame != 0) ? 1 : 0;
}

/* T-164 B4-4 intercept: RequesterVerdict (the drain's 'window home == piece home' shop test) is REMOVED - the non-holder's shop
   trade is stopped at the trade call (src/common/shopwire.h: coopshop::Gate) and the drain no longer recognises shop trades. */

/* n candidate sections in chain order; isPiece[i] = a registered shop piece; holds[i] = it holds the wanted base record.
   *byRecord = 1 when the answer is a holder, 0 when it is the unreadable-record fallback. -1 = no piece. */
inline int PickPieceByRecord(const int* isPiece, const int* holds, int n, int recordReadable, int* byRecord)
{
    *byRecord = 0;
    int first = -1;
    for (int i = 0; i < n; ++i)
    {
        if (isPiece[i] == 0) continue;
        if (first < 0) first = i;
        if (recordReadable != 0 && holds[i] != 0) { *byRecord = 1; return i; }
    }
    return (recordReadable != 0) ? -1 : first;
}

const int kPaySquad = 0;           /* the keeper squad's own money */
const int kPayNoKeeper = 1;        /* the piece names no live keeper: nothing is paid */
const int kPayNoSquadPot = 2;      /* the keeper has no squad pot (a player faction's global purse, a stand-in, no squad) */

inline int KeeperPayRoute(int keeperResolved, int squadPotReadable)
{
    if (keeperResolved == 0) return kPayNoKeeper;
    return (squadPotReadable != 0) ? kPaySquad : kPayNoSquadPot;
}

}   /* namespace shopstock */
#endif
