/* setupstock.h - A SHOP'S SET-UP RESTOCK WHEN ITS COUNTERS ALREADY HOLD THE WORLD SERVER'S STOCK. Pure rules: no engine, no state.
 *
 * The engine's squad builder (RootObjectFactory::createRandomSquad 0x582F80) restocks a trader squad with force=1 when it makes the
 * squad's people - among others a sleeping keeper squad that wakes on the game that has just become the holder of its shop's area.
 * The restock (ActivePlatoon::refreshInventory 0x4FEB50) clears every counter, fills it with new stock and SETS the keeper's money,
 * so stock the world server stores for that shop, already put into the counters at this load, would be replaced by goods made from
 * nothing and the keeper given a second money pot. The timed restock passes force=0 and keeps its rule.
 */
#pragma once
#include "boxrec.h"   /* the box records' apply states (coopbrec::kBas*) */

namespace shopsetup {

/* THE SET-UP RESTOCK. force: the restock's force argument (0 = the timed restock); ownCommand: the mod's own test command made the
   call; wanderer: a wandering trader (no home building - its packs have their own rule, wanderrestock.h); stockHeld: at least one
   of the shop's counters holds the world server's stock applied at this load (unread = 0). Refused only when forced, not the mod's
   own command, not a wanderer and the stock is held; every other call runs the engine's restock. */
const int kSetupRestockRefuse = 0, kSetupRestockRun = 1;
inline int SetupRestockDecide(int force, int ownCommand, int wanderer, int stockHeld)
{
    if (force == 0 || ownCommand != 0 || wanderer != 0) return kSetupRestockRun;
    return (stockHeld != 0) ? kSetupRestockRefuse : kSetupRestockRun;
}

/* THE APPLY STATES IN WHICH THE BOX'S STOCK IS THE WORLD'S CURRENT STOCK (coopbrec::kBas*, one box's apply at this load): done (the
   record put in, or the box found already to be it or newer), another game writes the box (its copy follows that writer's), and
   refused (this game's box stands and is written as the next generation above the record). Not counted: none and wait (not decided
   yet - the restock gate already holds the restock for them) and no-box (the record's box is not here). */
inline int SetupApplyHolds(int applyState)
{
    return (applyState == coopbrec::kBasDone || applyState == coopbrec::kBasNotWriter || applyState == coopbrec::kBasRefused) ? 1 : 0;
}
/* ONE COUNTER: 1 = it holds the world server's stock here - its kept box record is a shop record with at least one item and that
   record's apply at this load is in a state SetupApplyHolds counts. No record, another kind, an empty record (a sold-out shop) or
   an apply not decided = 0. */
inline int SetupPieceHeld(int haveRecord, int isShopKind, long long items, int applyState)
{
    return (haveRecord != 0 && isShopKind != 0 && items > 0 && SetupApplyHolds(applyState) != 0) ? 1 : 0;
}

/* THE DEADLINE THE ENGINE'S RESTOCK WRITES (ActivePlatoon::refreshInventory 0x4FEB50, its tail): Platoon +0x1F8 = now + the squad's
   "vendors refresh time" float field when it is above 0, else the game's default (a missing field reads 0 there). A refused
   set-up restock writes the same value, so the engine's timed check (deadline <= now) does not restock on its next pass. */
inline double RestockDeadlineAsEngine(double now, float squadField, float defaultIv)
{
    const float iv = (squadField > 0.0f) ? squadField : defaultIv;
    return (double)iv + now;
}

/* THE KEEPER REPAIR FOR A KEEPER WHOSE MONEY IS NOT KNOWN (squadlead.h KeeperRepairWhy's kKeeperRepairMoneyUnknown). Counters that do
   not hold the world server's stock: the engine's restock is asked (the squad's refresh deadline moved into the past). Counters
   that hold it: no restock (it would replace
   that stock) - the money comes from the squad's record when the record carries it, else it stays not known. */
const int kKeeperMoneyRestock = 0, kKeeperMoneyFromRecord = 1, kKeeperMoneyLeftUnknown = 2;
inline int KeeperUnknownMoneyRoad(int stockHeld, int recordCarriesMoney)
{
    if (stockHeld == 0) return kKeeperMoneyRestock;
    return (recordCarriesMoney != 0) ? kKeeperMoneyFromRecord : kKeeperMoneyLeftUnknown;
}

}   /* namespace shopsetup */
