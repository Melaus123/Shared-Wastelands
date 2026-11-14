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
#include "buildwire.h"   /* T-1130 re-check: coopbuild::kBuildDeadHandleWait - the dead-handle wait the build copy gate (T-903) uses */

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
/* ONE COUNTER (one of the shop's home's pieces, walked live from its home building): 1 = it holds the world server's stock here -
   its kept box record is not a player's box, has at least one item, and that record's apply at this load is in a state
   SetupApplyHolds counts. T-1130: the record's kind is not tested - it is each game's label from its own shop list, filled some
   seconds after a load, so a counter's record may carry either kind. No record, a player's box, an empty record (a sold-out shop)
   or an apply not decided = 0. */
inline int SetupPieceHeld(int haveRecord, int kind, int ownerSlot, long long items, int applyState)
{
    return (haveRecord != 0 && coopbrec::BoxIsPlayerRec(kind, ownerSlot) == 0 && items > 0 && SetupApplyHolds(applyState) != 0) ? 1 : 0;
}

/* T-1130: THE RESTOCK HOLD OF ONE SHOP, judged over its OWN home's pieces walked live from its home building at the restock - not
   every shop record of the map section, so a counter of another shop left undecided there no longer holds this shop. One piece
   holds when this game keeps a record of it that is not a player's box, whatever its kind (the kind is each game's label), and that
   record's apply at this load is not decided yet (coopbrec::BoxApplyHoldsRestock: a restock before the apply would be undone by it). */
struct HomePiece { int haveRecord, kind, ownerSlot, applyState; };
inline int HomePieceHoldsRestock(const HomePiece& p)
{
    return (p.haveRecord != 0 && coopbrec::BoxIsPlayerRec(p.kind, p.ownerSlot) == 0 && coopbrec::BoxApplyHoldsRestock(p.applyState) != 0) ? 1 : 0;
}
inline int HomePiecesHolding(const std::vector<HomePiece>& pieces)
{
    int n = 0;
    for (size_t i = 0; i < pieces.size(); ++i) n += HomePieceHoldsRestock(pieces[i]);
    return n;
}
/* THE HOME WALK (items.cpp ItHomePieces, its *walkOut): torn = a read fault mid-walk, or a building its interior lists that does not
   resolve yet (HomeWalkUnresolved below) - a later pass may read it whole; read = every listed piece read, or every one that did not
   resolve is a dead handle HomeWalkUnresolved counts as no piece; no interior = the home has no interior to walk (no home, or its
   interior pointer missing or not plausible) - it has no counters whose stock a restock could undo, and nothing would ever release a
   hold on it (KlPiecesOf counts the same case as a real one). */
const int kHomeWalkTorn = 0, kHomeWalkRead = 1, kHomeWalkNoInterior = 2;
/* T-1130 re-check (F1026, Confirmed T1226 / T1327: an interior list can keep entries that answer null from both engine lookups for
   300 s and more and are never pruned - a walk torn by them would hold the shop's restock for the whole session). THE WALK FROM ITS
   LISTED BUILDINGS THAT DID NOT RESOLVE, the build copy gate's idea (coopbuild::BuildDeadHandlesIgnore, T-903): other = handles not
   resolved for any other reason (a faulted lookup, hand::asObjectBase answering an object, unreadable or different ids) - the walk
   stays torn; dead = handles hand::asBuilding and hand::asObjectBase both answered null, neither faulted. Dead handles count as no
   piece (the walk reads whole) only when the home's area reads fully loaded here (areaLoaded) and the same dead set
   (coopbuild::BuildDeadSetSig) has lasted at least coopbuild::kBuildDeadHandleWait safe points (setAge, coopbuild::BuildDeadSetStep
   over a per-home memory: -1 = new or changed now); until then the walk stays torn. No handle unresolved: read. */
inline int HomeWalkUnresolved(int other, int dead, int areaLoaded, long long setAge)
{
    if (other != 0) return kHomeWalkTorn;
    if (dead <= 0) return kHomeWalkRead;
    if (areaLoaded == 0) return kHomeWalkTorn;
    if (setAge < coopbuild::kBuildDeadHandleWait) return kHomeWalkTorn;   /* -1 fails too */
    return kHomeWalkRead;
}
/* THE SHOP: a torn walk holds (kHomeHoldUnread) - the engine asks for the restock again on its next pass; a walk read whole holds
   while at least one of its pieces holds (piecesHolding > 0); a home read with no pieces, or a home with no interior, never holds. */
const int kHomeHoldNone = 0, kHomeHoldPiece = 1, kHomeHoldUnread = 2;
inline int HomeRestockHold(int walk, int piecesHolding)
{
    if (walk == kHomeWalkNoInterior) return kHomeHoldNone;
    if (walk != kHomeWalkRead) return kHomeHoldUnread;
    return (piecesHolding > 0) ? kHomeHoldPiece : kHomeHoldNone;
}
/* THE HOLD FROM THE WALK (items.cpp BoxRecHomeApplyPending): each walked piece's key -> its record id (coopbrec::MakeBoxRecId: a
   piece no player owns has no height in its id) -> this game's kept record (`kept`: id -> a value with .h.kind and .h.ownerSlot)
   and that record's apply at this load (`applies`: id -> a value with .applyState; a kept record with no apply row reads not decided
   while the table has room, decided once it holds appliesCap rows - a full table never holds a restock). A key with no id or no
   kept record is not a counter with stored stock. Returns HomeRestockHold's answer. */
template <class KeptMap, class ApplyMap>
inline int HomeApplyHold(int walk, const std::vector<std::string>& keys, const KeptMap& kept, const ApplyMap& applies, size_t appliesCap)
{
    std::vector<HomePiece> v;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const std::string id = coopbrec::MakeBoxRecId(keys[i]);
        if (id.empty()) continue;
        typename KeptMap::const_iterator it = kept.find(id);
        if (it == kept.end()) continue;
        typename ApplyMap::const_iterator wi = applies.find(id);
        HomePiece p;
        p.haveRecord = 1; p.kind = it->second.h.kind; p.ownerSlot = it->second.h.ownerSlot;
        p.applyState = (wi != applies.end()) ? wi->second.applyState : (applies.size() < appliesCap ? coopbrec::kBasNone : coopbrec::kBasDone);
        v.push_back(p);
    }
    return HomeRestockHold(walk, HomePiecesHolding(v));
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
