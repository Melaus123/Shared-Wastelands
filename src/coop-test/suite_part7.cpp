/* src/coop-test/suite_part7.cpp - THE OFFLINE SUITE, part 7 of 7 (owner decision 1204).
 *
 * Older, settled tests moved here unchanged from test_main.cpp, in their old order, so that cl /MP8 compiles the
 * suite's files side by side. Each test keeps its place in the run: the order is kTests in test_main.cpp. What more
 * than one file uses is in suite.h. New tests go in test_main.cpp.
 */
#include <limits>
#include "suite.h"
#include "../common/bookshare.h"   /* T-1242 */

namespace suite {

/* a key's height reads back, and the into lever's offset */
void t_key_height_into_offset()
{
    const char* const sid22 = "1234567-Some Names.mod";   /* 22 characters: a 48-character key */
    char kLow[64];
    if (poskey::ComposeOrHash(kLow, 64, sid22, 18, 33, -604241, 87679, 12928, 0, 0) != 1) Fail("key height: the key did not compose");
    char sid[poskey::kSidRoom];
    int sx = 0, sy = 0;
    long x10 = 0, z10 = 0, y10 = 0;
    if (poskey::ParseKeySpotH(kLow, sid, poskey::kSidRoom, &sx, &sy, &x10, &z10, &y10) != 1 || y10 != 12928 || x10 != -604241 || z10 != 87679)
        Fail("key height: the key's height does not read back");
    float dx = 0.0f, dz = 0.0f;
    if (coopbuild::BuildIntoOffsetParse("25,25", &dx, &dz) != 1 || dx != 25.0f || dz != 25.0f) Fail("into offset: 25,25 does not read");
    if (coopbuild::BuildIntoOffsetParse("-10.5,3", &dx, &dz) != 1 || dx != -10.5f || dz != 3.0f) Fail("into offset: -10.5,3 does not read");
    if (coopbuild::BuildIntoOffsetParse("indoors", &dx, &dz) != 0 || coopbuild::BuildIntoOffsetParse("1,2,3", &dx, &dz) != 0 || coopbuild::BuildIntoOffsetParse(",", &dx, &dz) != 0
        || coopbuild::BuildIntoOffsetParse("1,", &dx, &dz) != 0 || coopbuild::BuildIntoOffsetParse(",1", &dx, &dz) != 0 || coopbuild::BuildIntoOffsetParse("1e9,0", &dx, &dz) != 0
        || coopbuild::BuildIntoOffsetParse("nan,0", &dx, &dz) != 0 || coopbuild::BuildIntoOffsetParse("", &dx, &dz) != 0 || coopbuild::BuildIntoOffsetParse("1x,2", &dx, &dz) != 0)
        Fail("into offset: a malformed offset reads");
    /* T-899: `buildtest town` names a row by a key substring, else by its exact sid */
    if (coopbuild::BuildTownArgMatch("abc", "x_abc_1", "zz") != 1 || coopbuild::BuildTownArgMatch("zz", "x_abc_1", "zz") != 2)
        Fail("town lever: a key substring or the exact sid does not name its row");
    if (coopbuild::BuildTownArgMatch("z", "x_abc_1", "zz") != 0 || coopbuild::BuildTownArgMatch("", "x_abc_1", "") != 0
        || coopbuild::BuildTownArgMatch("zzz", "x_abc_1", "zz") != 0)
        Fail("town lever: a part of a sid, or an empty argument, names a row");
    /* T-609 U: `buildtest upgrade|downgrade` calls the engine only on an own, readable, whole, complete piece with a target record */
    if (coopbuild::BuildUpgradeLeverVerdict(1, 0, 1, 1, 0, 0, 1, 1) != coopbuild::kBuildUpgradeWall
        || coopbuild::BuildUpgradeLeverVerdict(0, 0, 1, 1, 0, 0, 1, 1) != coopbuild::kBuildUpgradeNotOwn)
        Fail("upgrade lever: a wall is not refused, or refused before the own check");
    if (coopbuild::BuildUpgradeLeverVerdict(1, 0, 1, 1, 0, 0, 1, 0) != coopbuild::kBuildUpgradeGo)
        Fail("upgrade lever: an own complete piece with an 'upgrades to' record is refused");
    if (coopbuild::BuildUpgradeLeverVerdict(0, 0, 1, 1, 0, 0, 1, 0) != coopbuild::kBuildUpgradeNotOwn
        || coopbuild::BuildUpgradeLeverVerdict(1, 1, 1, 1, 0, 0, 1, 0) != coopbuild::kBuildUpgradeNotOwn)
        Fail("upgrade lever: a piece that is not own (or a copy) is not refused as not own");
    if (coopbuild::BuildUpgradeLeverVerdict(1, 0, 0, 1, 0, 0, 1, 0) != coopbuild::kBuildUpgradeNoState)
        Fail("upgrade lever: an unreadable state is not refused");
    if (coopbuild::BuildUpgradeLeverVerdict(1, 0, 1, 1, 1, 0, 1, 0) != coopbuild::kBuildUpgradeDismantled
        || coopbuild::BuildUpgradeLeverVerdict(1, 0, 1, 1, 0, 1, 1, 0) != coopbuild::kBuildUpgradeRuin)
        Fail("upgrade lever: a dismantled piece or a ruin is not refused");
    if (coopbuild::BuildUpgradeLeverVerdict(1, 0, 1, 0, 0, 0, 1, 0) != coopbuild::kBuildUpgradeNotComplete)
        Fail("upgrade lever: an unfinished piece is not refused");
    if (coopbuild::BuildUpgradeLeverVerdict(1, 0, 1, 1, 0, 0, 0, 0) != coopbuild::kBuildUpgradeNoRecord)
        Fail("upgrade lever: a piece with no target record is not refused");
    if (coopbuild::BuildUpgradeLeverVerdict(0, 1, 0, 0, 1, 1, 0, 0) != coopbuild::kBuildUpgradeNotOwn)
        Fail("upgrade lever: not own is not the first reason");
    if (std::string(coopbuild::BuildUpgradeLeverWhy(coopbuild::kBuildUpgradeNoRecord, 1)).find("downgrades to") == std::string::npos
        || std::string(coopbuild::BuildUpgradeLeverWhy(coopbuild::kBuildUpgradeNoRecord, 0)).find("upgrades to") == std::string::npos
        || std::string(coopbuild::BuildUpgradeLeverWhy(99, 0)) != "unknown verdict")
        Fail("upgrade lever: a reason does not name its case");
}
/* T-609 U fix: what the main thread does with an upgrade / downgrade hook event (args: keysOk, sameKey, sameWorld, rowExists, own,
   copyHere, removed, newRow) - every verdict, the order they are judged in, the dropped ones and the REMOVE's word */
void t_t609u_rekey_act()
{
    using namespace coopbuild;
    if (BuildRekeyAct(1, 0, 1, 1, 1, 0, 0, kBuildRekeyNewNone) != kBuildRekeyOwn) Fail("rekey: an own live row with a free new key is not moved");
    if (BuildRekeyAct(0, 0, 1, 1, 1, 0, 0, kBuildRekeyNewNone) != kBuildRekeyNoKey || BuildRekeyAct(0, 1, 0, 0, 0, 1, 1, kBuildRekeyNewCopy) != kBuildRekeyNoKey)
        Fail("rekey: an unreadable key is not the first reason");
    if (BuildRekeyAct(1, 1, 1, 1, 1, 0, 0, kBuildRekeyNewNone) != kBuildRekeySame || BuildRekeyAct(1, 1, 0, 0, 0, 1, 1, kBuildRekeyNewCopy) != kBuildRekeySame)
        Fail("rekey: an unchanged key does something");
    if (BuildRekeyAct(1, 0, 0, 1, 1, 0, 0, kBuildRekeyNewNone) != kBuildRekeyStale || BuildRekeyAct(1, 0, 0, 0, 0, 0, 0, kBuildRekeyNewNone) != kBuildRekeyStale)
        Fail("rekey: a pair queued in an earlier world is not dropped first");
    if (BuildRekeyAct(1, 0, 1, 0, 0, 0, 0, kBuildRekeyNewNone) != kBuildRekeyNoRow || BuildRekeyAct(1, 0, 1, 0, 1, 1, 1, kBuildRekeyNewOwn) != kBuildRekeyNoRow)
        Fail("rekey: a piece with no row is not left alone");
    if (BuildRekeyAct(1, 0, 1, 1, 1, 1, 0, kBuildRekeyNewNone) != kBuildRekeyCopy || BuildRekeyAct(1, 0, 1, 1, 0, 0, 0, kBuildRekeyNewNone) != kBuildRekeyCopy
        || BuildRekeyAct(1, 0, 1, 1, 1, 1, 1, kBuildRekeyNewCopy) != kBuildRekeyCopy)
        Fail("rekey: a copy (or a row this game does not own) is re-keyed here");
    if (BuildRekeyAct(1, 0, 1, 1, 1, 0, 1, kBuildRekeyNewNone) != kBuildRekeyGone || BuildRekeyAct(1, 0, 1, 1, 1, 0, 1, kBuildRekeyNewCopy) != kBuildRekeyGone)
        Fail("rekey: a removed row is moved, or judged after the new key");
    if (BuildRekeyAct(1, 0, 1, 1, 1, 0, 0, kBuildRekeyNewCopy) != kBuildRekeyOverCopy) Fail("rekey: a copy row at the new key is overwritten");
    if (BuildRekeyAct(1, 0, 1, 1, 1, 0, 0, kBuildRekeyNewOwn) != kBuildRekeyRemoveOnly) Fail("rekey: a live own row at the new key is not kept (REMOVE only)");
    if (BuildRekeyDropped(kBuildRekeyNoKey) != 1 || BuildRekeyDropped(kBuildRekeyStale) != 1 || BuildRekeyDropped(kBuildRekeyOverCopy) != 1)
        Fail("rekey: a dropped verdict is not counted dropped");
    if (BuildRekeyDropped(kBuildRekeySame) != 0 || BuildRekeyDropped(kBuildRekeyNoRow) != 0 || BuildRekeyDropped(kBuildRekeyCopy) != 0
        || BuildRekeyDropped(kBuildRekeyGone) != 0 || BuildRekeyDropped(kBuildRekeyRemoveOnly) != 0 || BuildRekeyDropped(kBuildRekeyOwn) != 0)
        Fail("rekey: a verdict with nothing to move is counted dropped");
    if (BuildRekeyRemoveOutcome(1, 1, 1) != kBuildRekeyRmSent || BuildRekeyRemoveOutcome(1, 0, 0) != kBuildRekeyRmSent
        || BuildRekeyRemoveOutcome(0, 1, 1) != kBuildRekeyRmOwed || BuildRekeyRemoveOutcome(0, 0, 0) != kBuildRekeyRmNotShared
        || BuildRekeyRemoveOutcome(0, 0, 1) != kBuildRekeyRmFailed)
        Fail("rekey: the old key's REMOVE outcome is misread");
    if (std::string(BuildRekeyRemoveWord(kBuildRekeyRmSent)) != "removeSent" || std::string(BuildRekeyRemoveWord(kBuildRekeyRmOwed)) != "removeOwed"
        || std::string(BuildRekeyRemoveWord(kBuildRekeyRmNotShared)) != "removeNotShared" || std::string(BuildRekeyRemoveWord(kBuildRekeyRmFailed)) != "removeFailed"
        || std::string(BuildRekeyRemoveWord(99)) != "remove?")
        Fail("rekey: a REMOVE outcome does not say its word");
}
/* T-609 U fix: the off-main re-key queue - filled in order up to its cap, a push past it (or with a bad queue or count) refused unchanged */
struct T609uRec { char k[8]; int down; };
void t_t609u_rekey_queue()
{
    using namespace coopbuild;
    T609uRec q[3];
    std::memset(q, 0, sizeof q);
    int n = 0;
    T609uRec r;
    std::memset(&r, 0, sizeof r);
    for (int i = 0; i < 3; ++i)
    {
        r.k[0] = (char)('a' + i); r.down = i;
        if (BuildRekeyQueuePush(q, &n, 3, r) != 1 || n != i + 1) Fail("rekey queue: a push below the cap is refused");
    }
    if (q[0].k[0] != 'a' || q[1].k[0] != 'b' || q[2].k[0] != 'c' || q[2].down != 2) Fail("rekey queue: the records are not kept in order");
    r.k[0] = 'z';
    if (BuildRekeyQueuePush(q, &n, 3, r) != 0 || n != 3 || q[2].k[0] != 'c') Fail("rekey queue: a push past the cap is taken or changes the queue");
    int bad = -1;
    if (BuildRekeyQueuePush(q, &bad, 3, r) != 0 || bad != -1) Fail("rekey queue: a negative count is pushed to");
    int m = 0;
    if (BuildRekeyQueuePush((T609uRec*)0, &m, 3, r) != 0 || m != 0) Fail("rekey queue: a missing queue is pushed to");
    if (BuildRekeyQueuePush(q, (int*)0, 3, r) != 0) Fail("rekey queue: a missing count is pushed to");
    if (BuildRekeyQueuePush(q, &m, 0, r) != 0 || m != 0) Fail("rekey queue: a zero cap takes a record");
}
/* T-936: the wake queue - one entry per town (by handle words, else by the founding's address), in order, up to its cap; an entry
   taken out keeps the rest in order */
void t_t936_wake_queue()
{
    using namespace townwake;
    Entry q[3];
    std::memset(q, 0, sizeof q);
    int n = 0;
    Entry e;
    std::memset(&e, 0, sizeof e);
    e.hand[0] = 7; e.hand[4] = 1; e.ptr = 0x1000;
    if (Push(q, &n, 3, e) != kPushQueued || n != 1) Fail("wake queue: a new town is not queued");
    if (Push(q, &n, 3, e) != kPushAgain || n != 1) Fail("wake queue: the same town is queued twice");
    Entry f = e; f.ptr = 0x2000;
    if (Push(q, &n, 3, f) != kPushAgain || n != 1) Fail("wake queue: the same handle at another address is a second town");
    Entry g; std::memset(&g, 0, sizeof g); g.ptr = 0x3000;   /* no handle read: matched by its address */
    if (Push(q, &n, 3, g) != kPushQueued || n != 2) Fail("wake queue: a town with no handle read is refused");
    if (Push(q, &n, 3, g) != kPushAgain || n != 2) Fail("wake queue: a town with no handle read is queued twice by its address");
    Entry h; std::memset(&h, 0, sizeof h); h.hand[0] = 8; h.ptr = 0x4000;
    if (Push(q, &n, 3, h) != kPushQueued || n != 3) Fail("wake queue: a third town below the cap is refused");
    Entry z; std::memset(&z, 0, sizeof z); z.hand[0] = 9; z.ptr = 0x5000;
    if (Push(q, &n, 3, z) != kPushFull || n != 3 || q[2].hand[0] != 8) Fail("wake queue: a push past the cap is taken or changes the queue");
    RemoveAt(q, &n, 0);
    if (n != 2 || q[0].ptr != 0x3000 || q[1].hand[0] != 8) Fail("wake queue: taking the first out does not keep the rest in order");
    RemoveAt(q, &n, 5);
    if (n != 2) Fail("wake queue: an index past the end takes an entry out");
    int bad = -1;
    if (Push(q, &bad, 3, z) != kPushFull || bad != -1) Fail("wake queue: a negative count is pushed to");
    if (Push((Entry*)0, &n, 3, z) != kPushFull || Push(q, (int*)0, 3, z) != kPushFull || Push(q, &n, 0, z) != kPushFull) Fail("wake queue: a missing queue / count or a zero cap takes a town");
    Entry a0; std::memset(&a0, 0, sizeof a0); Entry b0 = a0;
    if (SameTown(a0, b0)) Fail("wake queue: two entries with no handle and no address are the same town");
}
/* T-936: one look at a queued town - woken only when found, asleep, on the grid, its area loaded and the located area record naming
   that area; kept while it is not found or its area is not loaded, dropped after 60 s or in another world */
void t_t936_wake_verdict()
{
    using namespace townwake;
    if (Verdict(1, 1, 0, 1, 1, 0, 1) != kWake) Fail("wake verdict: a found, asleep town in a loaded, matching area is not woken");
    if (Verdict(1, 1, 1, 1, 1, 0, 1) != kAlready) Fail("wake verdict: an awake town is called");
    if (Verdict(1, 1, 0, 0, 0, 1000, 1) != kWaitArea || Verdict(1, 1, 0, -1, 0, 1000, 1) != kWaitArea) Fail("wake verdict: a town whose area is not loaded (or unread) is not kept");
    if (Verdict(1, 1, 0, 0, 0, kGiveUpMs, 1) != kDropTimeout) Fail("wake verdict: a town waiting 60 s for its area is kept");
    if (Verdict(0, 0, 0, 0, 0, 10, 1) != kWaitTown || Verdict(0, 0, 0, 0, 0, kGiveUpMs + 1, 1) != kDropTimeout) Fail("wake verdict: a town not found is not kept, or kept past 60 s");
    if (Verdict(1, 1, 0, 1, 0, 0, 1) != kAreaMismatch) Fail("wake verdict: an area record naming another area is called");
    if (Verdict(1, 0, 0, 1, 1, 0, 1) != kNoArea) Fail("wake verdict: a town off the grid is called");
    if (Verdict(1, 1, 0, 1, 1, 0, 0) != kDropWorld) Fail("wake verdict: a town of another world is called");
    if (TakesOut(kWaitArea) != 0 || TakesOut(kWaitTown) != 0 || TakesOut(kWake) != 1 || TakesOut(kAlready) != 1 || TakesOut(kDropTimeout) != 1 || TakesOut(kAreaMismatch) != 1)
        Fail("wake verdict: the wait / take-out split is wrong");
    if (AreaRecordMatches(1, 3, 4, 3, 4) != 1 || AreaRecordMatches(1, 3, 4, 4, 3) != 0 || AreaRecordMatches(0, 3, 4, 3, 4) != 0 || AreaRecordMatches(-1, 3, 4, 3, 4) != 0)
        Fail("wake verdict: the area-record check is wrong");
    int sx = -1, sy = -1;
    if (SectorOfXZ(0.0f, 0.0f, &sx, &sy) != 1 || sx != 32 || sy != 32) Fail("wake verdict: the world centre is not area 32,32");
    if (SectorOfXZ(-147456.0f, 147455.0f, &sx, &sy) != 1 || sx != 0 || sy != 63) Fail("wake verdict: the grid's corners are not areas 0 / 63");
    if (SectorOfXZ(-147457.0f, 0.0f, &sx, &sy) != 0 || sx != -1) Fail("wake verdict: a position west of the grid is an area (or clamped)");
    if (SectorOfXZ(1e12f, 0.0f, &sx, &sy) != 0) Fail("wake verdict: a garbage position is an area");
}
/* T-799 D1: the money-known mark - set by a restock here or the other game's written value, never unset by a later event; an
   announcement carries the pot only when it was read AND its money is known. */
void t_t799_money_known()
{
    using namespace coopsquad;
    if (MoneyKnownAfter(0, kMoneyEvNone) != 0) Fail("a pot nobody set stays unknown");
    if (MoneyKnownAfter(0, kMoneyEvRestockHere) != 1) Fail("a restock here makes it known");
    if (MoneyKnownAfter(0, kMoneyEvPeerWritten) != 1) Fail("the other game's written value makes it known");
    if (MoneyKnownAfter(1, kMoneyEvNone) != 1) Fail("known stays known");
    if (MoneyKnownAfter(0, 99) != 0) Fail("an unknown event marks nothing");
    if (AnnounceCarriesMoney(1, 1) != 1) Fail("read and known: carried");
    if (AnnounceCarriesMoney(1, 0) != 0) Fail("read but unknown (a loaded 0): no money - the copies keep theirs");
    if (AnnounceCarriesMoney(0, 1) != 0) Fail("unread: no money");
}
/* T-799 D3: which keeper is restocked now (owner 793 a + clarification) - runner only, once per load, after any due adoption; money
   unknown whatever the counters hold; known only at 0 money AND 0 goods; the area holder only. */
void t_t799_keeper_repair()
{
    using namespace coopsquad;
    if (KeeperRepairWhy(1, 0, 0, 1, 1, 0, 0, 5) != kKeeperRepairMoneyUnknown) Fail("money unknown, counters hold goods: repaired (793 a)");
    if (KeeperRepairWhy(1, 0, 0, 1, 1, 0, 0, -1) != kKeeperRepairMoneyUnknown) Fail("money unknown, counters unread: repaired");
    if (KeeperRepairWhy(1, 0, 0, 1, 1, 1, 0, 0) != kKeeperRepairEmpty) Fail("0 money and 0 goods: repaired at once");
    if (KeeperRepairWhy(1, 0, 0, 1, 1, 1, 0, 3) != kKeeperRepairNone) Fail("0 money with goods, known: not repaired");
    if (KeeperRepairWhy(1, 0, 0, 1, 1, 1, 400, 0) != kKeeperRepairNone) Fail("money with 0 goods, known: not repaired");
    if (KeeperRepairWhy(1, 0, 0, 1, 1, 1, 0, -1) != kKeeperRepairNone) Fail("0 money, counters unread: asked again, not repaired");
    if (KeeperRepairWhy(0, 0, 0, 1, 1, 0, 0, 0) != kKeeperRepairNone) Fail("a copy (another game runs it): never repaired here");
    if (KeeperRepairWhy(1, 1, 0, 1, 1, 0, 0, 0) != kKeeperRepairNone) Fail("once per load per group");
    if (KeeperRepairWhy(1, 0, 1, 1, 1, 0, 0, 0) != kKeeperRepairNone) Fail("an adoption of the other game's value is due: it waits");
    if (KeeperRepairWhy(1, 0, 0, 0, 1, 0, 0, 0) != kKeeperRepairNone) Fail("the pot unread: nothing");
    if (KeeperRepairWhy(1, 0, 0, 1, 0, 0, 0, 5) != kKeeperRepairNone) Fail("money unknown but never decided here (loaded single player, or its load not yet decided): not repaired");
    if (KeeperRepairWhy(1, 0, 0, 1, 0, 1, 0, 0) != kKeeperRepairEmpty) Fail("known 0 money and 0 goods: repaired, decided or not");
    if (KeeperRepairGo(kKeeperRepairEmpty, 1) != kKeeperRepairEmpty) Fail("the area holder repairs");
    if (KeeperRepairGo(kKeeperRepairMoneyUnknown, 0) != kKeeperRepairNone) Fail("not the area holder: nothing");
}
/* T-801 D5: a sale no counter has room for is taken (keeper pays, item destroyed, verdict OK) only when counters resolved and nothing
   faulted; no counter resolved, a fault, an unread quantity, no item or a caravan stays refused (kVNoRoom). */
void t_t801_sell_no_room()
{
    using namespace coopsquad;
    if (SellNoRoomTaken(2, 0, 0, 0, 1, 0) != 1) Fail("counters resolved, none took it: taken");
    if (coopshop::HolderSell(coopshop::kVOk, 1, SellNoRoomTaken(2, 0, 0, 0, 1, 0)) != coopshop::kVOk) Fail("the taken sale's verdict is OK");
    if (SellNoRoomTaken(0, 0, 0, 0, 1, 0) != 0) Fail("no counter resolved: refused");
    if (coopshop::HolderSell(coopshop::kVOk, 1, SellNoRoomTaken(0, 0, 0, 0, 1, 0)) != coopshop::kVNoRoom) Fail("no counter resolved: verdict 8");
    if (SellNoRoomTaken(2, 0, 1, 0, 1, 0) != 0) Fail("a faulted add: refused");
    if (SellNoRoomTaken(2, 0, 0, 1, 1, 0) != 0) Fail("quantity unread: refused");
    if (SellNoRoomTaken(2, 0, 0, 0, 0, 0) != 0) Fail("no item made: refused");
    if (SellNoRoomTaken(2, 0, 0, 0, 1, 1) != 0) Fail("a caravan's packs: refused as before");
    if (SellNoRoomTaken(2, 3, 0, 0, 1, 0) != 0) Fail("a piece took units: the partial path, not this one");
}
/* T-799 D1: where a pot's money came from at a load - the engine's own load of a group it loaded by itself: known; a record that
   carries the money and went in: known; a record without it (an older record) or whose money did not go in: cleared, never "0
   known"; a context platoon or a later load: unchanged; a freed pot: cleared. */
void t_t799_money_load_rule()
{
    using namespace coopsquad;
    if (MoneyKnownAfter(0, kMoneyEvEngineLoaded) != 1) Fail("t799 load: the engine's own load makes it known");
    if (MoneyKnownAfter(0, kMoneyEvRecordApplied) != 1) Fail("t799 load: a record's money that went in makes it known");
    if (MoneyKnownAfter(1, kMoneyEvRecordNoMoney) != 0 || MoneyKnownAfter(0, kMoneyEvRecordNoMoney) != 0) Fail("t799 load: a record without the money clears the mark");
    if (MoneyKnownAfter(1, kMoneyEvGone) != 0) Fail("t799 load: a freed pot's mark goes");
    if (MoneyKnownAfter(1, kMoneyEvNone) != 1 || MoneyKnownAfter(1, kMoneyEvRestockHere) != 1 || MoneyKnownAfter(1, kMoneyEvPeerWritten) != 1) Fail("t799 load: known stays known otherwise");
    if (LoadMoneyEvent(0, 0, 0, 1, 0, 0) != kMoneyEvEngineLoaded) Fail("t799 load: the first load of a group the mod did not make is the engine's own");
    if (LoadMoneyEvent(0, 0, 0, 0, 0, 0) != kMoneyEvNone) Fail("t799 load: a later load, or a group the mod made, leaves the mark");
    if (LoadMoneyEvent(0, 0, 0, 1, 1, 0) != kMoneyEvNone) Fail("t799 load: a context platoon is not known by a load");
    if (LoadMoneyEvent(1, 1, 1, 1, 0, 0) != kMoneyEvRecordApplied || LoadMoneyEvent(1, 1, 1, 0, 0, 0) != kMoneyEvRecordApplied) Fail("t799 load: an override carrying the money that went in: known");
    if (LoadMoneyEvent(1, 0, 0, 1, 0, 0) != kMoneyEvRecordNoMoney || LoadMoneyEvent(1, 0, 0, 0, 0, 0) != kMoneyEvRecordNoMoney) Fail("t799 load: an override from an older record: not known");
    if (LoadMoneyEvent(1, 1, 0, 1, 0, 0) != kMoneyEvRecordNoMoney) Fail("t799 load: an override whose money did not go in: not known");
    if (LoadMoneyEvent(kLoadRoadOwnOverlay, 0, 0, 1, 0, 0) != kMoneyEvEngineLoaded) Fail("t799 load: the player's own record or a strip's copy read in: the money is the engine's own read of the save");
    if (LoadMoneyEvent(kLoadRoadOwnOverlay, 0, 0, 0, 0, 0) != kMoneyEvNone || LoadMoneyEvent(kLoadRoadOwnOverlay, 0, 0, 1, 1, 0) != kMoneyEvNone) Fail("t799 load: own road, a later load or a context platoon: unchanged");
    if (LoadMoneyEvent(kLoadRoadEngineFile, 0, 0, 1, 0, 1) != kMoneyEvSavedUnknown || LoadMoneyEvent(kLoadRoadOwnOverlay, 0, 0, 1, 0, 1) != kMoneyEvSavedUnknown) Fail("t799 load: a group listed unknown at the save: not known");
    if (LoadMoneyEvent(kLoadRoadEngineFile, 0, 0, 0, 0, 1) != kMoneyEvNone) Fail("t799 load: listed, but a later load: unchanged");
    if (LoadMoneyEvent(kLoadRoadRecord, 1, 1, 1, 0, 1) != kMoneyEvRecordApplied) Fail("t799 load: a record carrying the money wins over the list");
    if (MoneyKnownAfter(1, kMoneyEvSavedUnknown) != 0) Fail("t799 load: listed unknown clears the mark");
    if (MadeMoneyEvent(1, 1) != kMoneyEvRecordApplied || MadeMoneyEvent(0, 0) != kMoneyEvRecordNoMoney || MadeMoneyEvent(1, 0) != kMoneyEvRecordNoMoney) Fail("t799 load: a group made from a record is known only with its money in");
    if (RecordCarriesMoney(1, 1) != 1 || RecordCarriesMoney(1, 0) != 0 || RecordCarriesMoney(0, 1) != 0) Fail("t799 load: a record carries the money only when read and known");
}
/* T-799 D2: the record's money - written into the squad record and read back; an older record reads as not carried (never 0); a
   carried 0 is 0; a cut file is unread; the strip (T-802) takes a squad record that carries it and keeps it. */
void t_t799_record_money()
{
    using namespace coopown;
    int m = -1;
    std::vector<char> withM = T802Squad(true, true, -1, false, true, 4), old = T802Squad(true, true, -1, false, true);
    if (RecordMoneyOf(&withM[0], withM.size(), &m) != kRecMoneyCarried || m != 2500) Fail("t799 record: the money written reads back");
    if (RecordMoneyOf(&old[0], old.size(), &m) != kRecMoneyNone || m != 0) Fail("t799 record: an older record reads as money not carried");
    std::vector<char> zero;
    {
        std::vector<std::string> none;
        std::vector<std::vector<std::string> > noSt;
        const int z = 0;
        RecPutI32(&zero, 15); RecPutI32(&zero, 99); RecPutI32(&zero, 1);
        T802Rec(&zero, 0x1E, "sq", 0, 0, none, none, noSt, 0, 0, &z);
    }
    if (RecordMoneyOf(&zero[0], zero.size(), &m) != kRecMoneyCarried || m != 0) Fail("t799 record: a carried 0 is carried");
    std::vector<char> cut(withM.begin(), withM.begin() + withM.size() / 2);
    if (RecordMoneyOf(&cut[0], cut.size(), &m) != kRecMoneyUnread) Fail("t799 record: a cut file is unread");
    std::vector<char> out;
    if (GdcStripChar(&withM[0], withM.size(), HandId(11, 1), &out) != kStripOk) Fail("t799 record: the strip takes a squad record that carries money");
    else if (out != T802Squad(false, true, -1, false, true, 4)) Fail("t799 record: the strip keeps the money, every other byte as the engine's");
    else if (RecordMoneyOf(&out[0], out.size(), &m) != kRecMoneyCarried || m != 2500) Fail("t799 record: the stripped copy still carries the money");
    if (GdcStripChar(&old[0], old.size(), HandId(11, 1), &out) != kStripOk) Fail("t799 record: an older record still strips");
}
/* T-799 D4: the area holder's restock reaches the game that runs the keeper - sent only for another game's leader; written by the
   runner, held while priced requests or an adoption are due, dropped when it no longer runs the leader; the wire round trip. */
void t_t799_holder_restock()
{
    using namespace coopsquad;
    if (HolderRestockSend(1, 0, 1) != 1) Fail("t799 D4: the holder's restock of another game's keeper is sent");
    if (HolderRestockSend(1, 1, 1) != 0) Fail("t799 D4: this game runs the keeper: nothing to send");
    if (HolderRestockSend(1, 0, 0) != 0) Fail("t799 D4: the leader's uid unknown: nothing to send");
    if (HolderRestockSend(0, 0, 1) != 0) Fail("t799 D4: no restock here: nothing");
    if (HolderRestockApply(1, 1) != kHrApply) Fail("t799 D4: the runner writes it");
    if (HolderRestockApply(0, 1) != kHrNotRunner || HolderRestockApply(0, 0) != kHrNotRunner) Fail("t799 D4: not the runner: sent on");
    if (HolderRestockApply(1, 0) != kHrUnread) Fail("t799 D4: the pot unread: dropped");
    if (HolderRestockResend(0) != 1 || HolderRestockResend(kHolderRestockResendCap - 1) != 1 || HolderRestockResend(kHolderRestockResendCap) != 0) Fail("t799 D4: sent on a limited number of times");
    {   /* the change taken at arrival: pot 1000 with a 300-cat sale in flight predicted (announced 1300); restock to 5000 */
        const long long d = HolderRestockDelta(5000, 1300);
        int want = 0;
        if (d != 3700) Fail("t799 D4: the change owed is the restocked money less the announced pot");
        if (!HolderRestockWant(1000, d, &want) || want != 4700) Fail("t799 D4: the request still in flight: its prediction stays (the holder applies it after the restock)");
        if (!HolderRestockWant(1300, d, &want) || want != 5000) Fail("t799 D4: refused since the arrival (undone): the restocked money");
        if (!HolderRestockWant(1100, d, &want) || want != 4800) Fail("t799 D4: a trade made after the arrival stays in the pot");
        if (HolderRestockWant(2147483000, 1000000LL, &want) != 0) Fail("t799 D4: out of range: nothing written");
    }
    KeeperRestockMsg a, b;
    a.leader = 4242; a.cats = 12500;
    std::vector<char> w;
    if (!EncodeKeeperRestock(&w, a) || w.size() != kKeeperRestockSize || !DecodeKeeperRestock(&w[0], w.size(), &b) || b.leader != 4242 || b.cats != 12500) Fail("t799 D4: KEEPER_RESTOCK round trip");
    a.cats = 0;
    if (!EncodeKeeperRestock(&w, a) || !DecodeKeeperRestock(&w[0], w.size(), &b) || b.cats != 0) Fail("t799 D4: a restock to 0 is carried as 0");
    if (DecodeKeeperRestock(&w[0], w.size() - 1, &b)) Fail("t799 D4: a short KEEPER_RESTOCK is refused");
    w.push_back(0);
    if (DecodeKeeperRestock(&w[0], w.size(), &b)) Fail("t799 D4: a long KEEPER_RESTOCK is refused");
    a.leader = 0;
    if (EncodeKeeperRestock(&w, a)) Fail("t799 D4: no leader: not encoded");
    const char zeros[8] = { 0, 0, 0, 0, 1, 0, 0, 0 };
    if (DecodeKeeperRestock(zeros, 8, &b)) Fail("t799 D4: leader 0 is not read");
}
/* T-799: the list beside a save of the groups whose money was not known at the save, and what a record's save does with the money
   field a container already had */
void t_t799_money_list()
{
    using namespace coopsquad;
    if (MoneyUnknownAtSave(1, 0, 0) != 1 || MoneyUnknownAtSave(1, 0, 1) != 1) Fail("t799 list: decided and unknown: listed");
    if (MoneyUnknownAtSave(1, 1, 1) != 0) Fail("t799 list: decided and known now (restocked, written): not listed");
    if (MoneyUnknownAtSave(0, 0, 1) != 1 || MoneyUnknownAtSave(0, 0, 0) != 0) Fail("t799 list: never loaded this world: as the loaded save's list had it");
    std::set<std::string> a, b;
    a.insert("100-gamedata.base"); a.insert("7-x.mod"); a.insert(""); a.insert("bad\nid");
    const std::string s = EncodeMoneyList(a);
    if (!DecodeMoneyList(s, &b) || b.size() != 2 || b.count("100-gamedata.base") != 1 || b.count("7-x.mod") != 1) Fail("t799 list: round trip (empty and broken ids left out)");
    const std::string crlf = std::string(kMoneyListHead) + "\r\n100-gamedata.base\r\n";
    if (!DecodeMoneyList(crlf, &b) || b.size() != 1 || b.count("100-gamedata.base") != 1) Fail("t799 list: CRLF lines read");
    if (!DecodeMoneyList(EncodeMoneyList(std::set<std::string>()), &b) || !b.empty()) Fail("t799 list: an empty list reads as empty");
    if (DecodeMoneyList("pending\n100\n", &b) || !b.empty() || DecodeMoneyList("", &b)) Fail("t799 list: no head: not a list");
    if (RecMoneyFieldAtSave(1, 0) != kRecFieldSet || RecMoneyFieldAtSave(1, 1) != kRecFieldSet) Fail("t799 field: carried: put");
    if (RecMoneyFieldAtSave(0, 1) != kRecFieldErase) Fail("t799 field: not carried, an older value there: taken out for the save");
    if (RecMoneyFieldAtSave(0, 0) != kRecFieldKeep) Fail("t799 field: not carried, none there: untouched");
    if (RecMoneyFieldAfterSave(kRecFieldSet, 1) != kRecFieldRestore || RecMoneyFieldAfterSave(kRecFieldErase, 1) != kRecFieldRestore) Fail("t799 field: the container's own value given back");
    if (RecMoneyFieldAfterSave(kRecFieldSet, 0) != kRecFieldErase || RecMoneyFieldAfterSave(kRecFieldKeep, 0) != kRecFieldKeep) Fail("t799 field: none before: none after");
}
/* T-805: who may dismantle another player's piece - a player of another faction never, a member by asking the owner's game (refused
   when that game is out of reach); nothing else is touched */
void t_t805_dismantle_gate()
{
    using namespace coopbuild;
    if (BuildDismantleOrderVerdict(kBuildTaskDismantle, 1, 0, 1, 1) != kBuildDmOrderRefusedOther || BuildDismantleOrderVerdict(kBuildTaskDismantle, 1, 0, 0, 0) != kBuildDmOrderRefusedOther)
        Fail("t805: a non-member's dismantle of another player's piece was not refused");
    if (BuildDismantleOrderVerdict(kBuildTaskDismantle, 1, 1, 1, 1) != kBuildDmOrderAsk) Fail("t805: a member's dismantle with the owner reachable is not an ask");
    if (BuildDismantleOrderVerdict(kBuildTaskDismantle, 1, 1, 1, 0) != kBuildDmOrderOwnerAway) Fail("t805: a member's dismantle with the owner away was not refused at the click");
    if (BuildDismantleOrderVerdict(kBuildTaskDismantle, 1, 1, 0, 1) != kBuildDmOrderRefusedMember) Fail("t805: a member's dismantle went on while members may not dismantle");
    if (BuildDismantleOrderVerdict(kBuildTaskDismantle, 0, 0, 1, 0) != kBuildDmOrderGoes || BuildDismantleOrderVerdict(kBuildTaskDismantle, 0, 1, 1, 1) != kBuildDmOrderGoes)
        Fail("t805: a dismantle of this game's own piece was taken by the rule");
    const int others[4] = { kBuildTaskBuild, kBuildTaskAddMaterials, kBuildTaskRepair, 0 };
    for (int i = 0; i < 4; ++i)
        if (BuildDismantleOrderVerdict(others[i], 1, 0, 1, 1) != kBuildDmOrderGoes || BuildDismantleOrderVerdict(others[i], 1, 1, 1, 1) != kBuildDmOrderGoes)
            Fail("t805: a task other than dismantle was taken by the dismantle rule");
    if (kBuildTaskDismantle != 96) Fail("t805: the dismantle TaskType is not 96");
    /* the owner's answer */
    if (BuildDismantleAskVerdict(1, 1, 1, 0, 0, 0) != kBuildDmDone) Fail("t805: a member's ask on the owner's whole own piece is not done");
    if (BuildDismantleAskVerdict(0, 1, 1, 0, 0, 0) != kBuildDmNotMember) Fail("t805: an ask from a player outside the team was not refused");
    if (BuildDismantleAskVerdict(0, 0, 0, 0, 1, 1) != kBuildDmNotMember) Fail("t805: a non-member's ask was not refused first");
    if (BuildDismantleAskVerdict(1, 0, 1, 0, 0, 0) != kBuildDmGone || BuildDismantleAskVerdict(1, 1, 1, 1, 0, 0) != kBuildDmGone) Fail("t805: an ask on a missing or dismantled piece is not 'gone'");
    if (BuildDismantleAskVerdict(1, 1, 0, 0, 0, 0) != kBuildDmNotOwn) Fail("t805: an ask on a piece that is not the owner's own was not refused");
    if (BuildDismantleAskVerdict(1, 1, 1, 0, 1, 0) != kBuildDmWall || BuildDismantleAskVerdict(1, 1, 1, 0, 1, 1) != kBuildDmWall) Fail("t805: an ask on a wall (or an unreadable class) was not refused");
    if (BuildDismantleAskVerdict(1, 1, 1, 0, 0, 1) != kBuildDmBlocked) Fail("t805: an ask on a piece with furniture or a research bench was not refused");
    /* the message: ask and answer round trip; a bad verdict is refused */
    for (int v = 0; v <= 7; ++v)
    {
        BuildMsg a; a.kind = kBuildDismantleAsk; a.key = "Shack@43,11@538000,-945000,21000"; a.reason = (unsigned char)v;
        std::vector<char> b;
        BuildMsg o;
        if (!EncodeBuild(&b, a) || DecodeBuild(&b[0], b.size(), &o) != kBuildDecodeOk || o.kind != kBuildDismantleAsk || o.key != a.key || o.reason != a.reason)
            Fail("t805: a DISMANTLE_ASK does not round-trip");
    }
    {
        BuildMsg a; a.kind = kBuildDismantleAsk; a.key = "Shack@43,11@538000,-945000,21000"; a.reason = 8;
        std::vector<char> b;
        if (BuildEncodable(a)) Fail("t805: a DISMANTLE_ASK verdict 8 was encodable");
        a.reason = 0;
        EncodeBuild(&b, a);
        b.back() = (char)8;
        BuildMsg o;
        if (DecodeBuild(&b[0], b.size(), &o) != kBuildDecodeBadValue) Fail("t805: a DISMANTLE_ASK verdict 8 decoded");
        b.push_back(0);
        if (DecodeBuild(&b[0], b.size(), &o) == kBuildDecodeOk) Fail("t805: a DISMANTLE_ASK with a trailing byte decoded");
        a.key.clear();
        if (BuildEncodable(a)) Fail("t805: a DISMANTLE_ASK without a key was encodable");
    }
    for (int v = 0; v <= 7; ++v) if (std::string(BuildDmWord(v)) == "?") Fail("t805: a verdict has no word");
}
/* ---- T-798: A TOWN'S SET-ASIDE RESIDENTS ARE HANDED BACK (owedpop.h, townreoffer.h, townpending.h, worldreset.h) ---- */
void t_t798_has_reasons()
{
    const int codes[5] = { owedpop::kHasListUnread, owedpop::kHasGroupUnread, owedpop::kHasKeyless, owedpop::kHasHandUnread, owedpop::kHasNoBuilding };
    std::set<std::string> names; std::set<int> slots;
    for (int i = 0; i < 5; ++i)
    {
        if (codes[i] >= 0) Fail("a could-not-tell code is not negative");
        if (owedpop::HasSlot(codes[i]) < 0 || owedpop::HasSlot(codes[i]) >= owedpop::kHasReasons) Fail("a could-not-tell code has no counter");
        names.insert(owedpop::HasName(codes[i])); slots.insert(owedpop::HasSlot(codes[i]));
        if (owedpop::Commit(1, 1, codes[i], 0, (int)owedpop::kClaimNone) != owedpop::kActWait) Fail("a could-not-tell code did not wait");
        if (owedpop::Commit(1, 1, codes[i], 1, (int)owedpop::kClaimYou) != owedpop::kActWait) Fail("a could-not-tell code made a held row");
    }
    if (names.size() != 5 || slots.size() != 5) Fail("two could-not-tell reasons share a name or a counter");
    if (owedpop::HasSlot(1) != -1 || owedpop::HasSlot(0) != -1 || owedpop::HasSlot(-6) != -1) Fail("an answer or an unknown code was counted as a reason");
    if (std::string(owedpop::HasName(0)) != "none" || std::string(owedpop::HasName(1)) != "has-residents") Fail("an answer is misnamed");
    if (owedpop::WhyLogStep(0, owedpop::kHasKeyless) != 1) Fail("a building's first wait line was not written");
    if (owedpop::WhyLogStep(owedpop::kHasKeyless, owedpop::kHasKeyless) != 0) Fail("an unchanged reason was written again");
    if (owedpop::WhyLogStep(owedpop::kHasKeyless, owedpop::kHasGroupUnread) != 1) Fail("a changed reason was not written");
    if (owedpop::WhyLogStep(owedpop::kHasKeyless, 0) != 0 || owedpop::WhyLogStep(0, 1) != 0) Fail("an answer wrote a wait line");
}
void t_t798_keyless_live()
{
    /* live, tmplRead, groupTmpl, bldTmpl, bldPosRead, live dx, dz, recPosKnown, record dx, dz, radius */
    if (townreoffer::KeylessHoldsLive(0, 1, (void*)8, (void*)8, 1, 0.0f, 0.0f, 1, 10.0f, 10.0f, 30.0f) != 1) Fail("an asleep keyless group whose record places it at the building did not hold it");
    if (townreoffer::KeylessHoldsLive(0, 1, (void*)8, (void*)8, 1, 0.0f, 0.0f, 0, 900.0f, 0.0f, 30.0f) != 1) Fail("an asleep keyless group of the template with no record position did not hold it");
    if (townreoffer::KeylessHoldsLive(0, 1, (void*)8, (void*)8, 1, 0.0f, 0.0f, 1, 40.0f, 0.0f, 30.0f) != 0) Fail("an asleep keyless group whose record is away from the building held it");
    if (townreoffer::KeylessHoldsLive(0, 0, (void*)0, (void*)8, 1, 0.0f, 0.0f, 1, 900.0f, 0.0f, 30.0f) != 1) Fail("an asleep keyless group whose template could not be read did not hold");
    if (townreoffer::KeylessHoldsLive(-1, 1, (void*)8, (void*)8, 1, 900.0f, 0.0f, 1, 900.0f, 0.0f, 30.0f) != 1) Fail("a loaded keyless group that could not be read did not hold (it is read again next check-up)");
    if (townreoffer::KeylessHoldsLive(1, 1, (void*)8, (void*)8, 1, 10.0f, 10.0f, 1, 900.0f, 0.0f, 30.0f) != 1) Fail("a loaded keyless group of the template standing at the building did not hold it");
    if (townreoffer::KeylessHoldsLive(1, 1, (void*)8, (void*)8, 1, 40.0f, 0.0f, 1, 5.0f, 0.0f, 30.0f) != 1) Fail("a loaded keyless group wandering away from the building its record places it at did not hold it");
    if (townreoffer::KeylessHoldsLive(1, 1, (void*)8, (void*)8, 1, 40.0f, 0.0f, 1, 40.0f, 0.0f, 30.0f) != 0) Fail("a loaded keyless group away from the building, live and on record, held it");
    if (townreoffer::KeylessHoldsLive(1, 1, (void*)16, (void*)8, 1, 0.0f, 0.0f, 1, 0.0f, 0.0f, 30.0f) != 0) Fail("a loaded keyless group of another template held it");
    if (townreoffer::KeylessHoldsLive(1, 1, (void*)8, (void*)0, 1, 0.0f, 0.0f, 1, 0.0f, 0.0f, 30.0f) != 0) Fail("a building with no resident template was held");
}
void t_t798_note_holds()
{
    /* testNote, noteHome, bldKey, recHere, tmplKnown, tmplMatch, posKnown, dx, dz, radius */
    if (townpending::NoteHoldsBuilding(0, "K1", "K1", 0, 0, 0, 0, 0.0f, 0.0f, 30.0f) != 1) Fail("a note homed at the building did not hold it");
    if (townpending::NoteHoldsBuilding(0, "K2", "K1", 1, 1, 1, 1, 0.0f, 0.0f, 30.0f) != 0) Fail("a note homed at another building held this one (T-698)");
    if (townpending::NoteHoldsBuilding(0, "", "K1", 1, 1, 1, 1, 10.0f, 10.0f, 30.0f) != 1) Fail("a keyless note of the template at the building did not hold it");
    if (townpending::NoteHoldsBuilding(0, "", "K1", 1, 1, 0, 1, 0.0f, 0.0f, 30.0f) != 0) Fail("a keyless note of another template held it");
    if (townpending::NoteHoldsBuilding(0, "", "K1", 1, 1, 1, 1, 40.0f, 0.0f, 30.0f) != 0) Fail("a keyless note away from the building held it");
    if (townpending::NoteHoldsBuilding(0, "", "K1", 1, 1, 1, 0, 0.0f, 0.0f, 30.0f) != 1) Fail("a keyless note with no position (cannot be told) did not hold the building");
    if (townpending::NoteHoldsBuilding(0, "", "K1", 0, 0, 0, 0, 0.0f, 0.0f, 30.0f) != 1) Fail("a note with no record here (cannot be told) did not hold the building");
    if (townpending::NoteHoldsBuilding(0, "", "K1", 1, 0, 0, 1, 0.0f, 0.0f, 30.0f) != 1) Fail("a keyless note whose template is not known (cannot be told) did not hold the building");
    if (townpending::NoteHoldsBuilding(1, "", "K1", 0, 0, 0, 0, 0.0f, 0.0f, 30.0f) != 1) Fail("a TEST-ONLY pendnote note did not hold the building");
    if (townpending::NoteHoldsBuilding(0, "K1", "", 1, 1, 1, 1, 0.0f, 0.0f, 30.0f) != 0) Fail("a note was matched to a building with no key");
}
void t_t798_repair()
{
    /* heldLive, populatable, has, owedRow, entry, recordsHome, noteHolds, townLoss, repairedBefore, townAsideBefore[, repairOldWorlds] */
    if (owedpop::kT392RepairOldWorlds != 1) Fail("the shipped repair setting is not 'every town'");
    if (owedpop::T392MayRepairEmpty(1, 1, 0, 0, 0, 0, 0, 0, 0, 0) != 1) Fail("an empty building with nothing owed, in a town never set aside, was not repaired by default");
    if (owedpop::T392MayRepairEmpty(1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0) != 0) Fail("'only towns set aside before' repaired a town never set aside");
    if (owedpop::T392MayRepairEmpty(1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0) != 1) Fail("'only towns set aside before' did not repair a town set aside before");
    if (owedpop::T392MayRepairEmpty(0, 1, 0, 0, 0, 0, 0, 0, 0, 1) != 0) Fail("a building whose area this game does not hold live was repaired");
    if (owedpop::T392MayRepairEmpty(1, 0, 0, 0, 0, 0, 0, 0, 0, 1) != 0) Fail("a building Kenshi does not fill (or a player's) was repaired");
    if (owedpop::T392MayRepairEmpty(1, 1, 1, 0, 0, 0, 0, 0, 0, 1) != 0) Fail("a building with residents was repaired");
    for (int c = -5; c < 0; ++c) if (owedpop::T392MayRepairEmpty(1, 1, c, 0, 0, 0, 0, 0, 0, 1) != 0) Fail("a building whose residents check could not tell was repaired");
    if (owedpop::T392MayRepairEmpty(1, 1, 0, 1, 0, 0, 0, 0, 0, 1) != 0) Fail("a building with an owed row was repaired");
    if (owedpop::T392MayRepairEmpty(1, 1, 0, 0, 1, 0, 0, 0, 0, 1) != 0) Fail("a building already set aside here was repaired");
    if (owedpop::T392MayRepairEmpty(1, 1, 0, 0, 0, 1, 0, 0, 0, 1) != 0) Fail("a building a living record names as home was repaired");
    if (owedpop::T392MayRepairEmpty(1, 1, 0, 0, 0, 0, 1, 0, 0, 1) != 0) Fail("a building a note holds was repaired");
    if (owedpop::T392MayRepairEmpty(1, 1, 0, 0, 0, 0, 0, 1, 0, 1) != 0) Fail("a building of a town with recorded losses was repaired");
    if (owedpop::T392MayRepairEmpty(1, 1, 0, 0, 0, 0, 0, 0, 1, 1) != 0) Fail("a building repaired (or given up) this session was repaired again");
}
void t_t798_town_gate()
{
    if (townrefill::HoldSettled(1, 30000u, 30000u) != 1) Fail("a hold of the settle time was not settled");
    if (townrefill::HoldSettled(1, 29999u, 30000u) != 0) Fail("a hold shorter than the settle time was settled");
    if (townrefill::HoldSettled(0, 90000u, 30000u) != 0) Fail("a town not held was settled");
    if (townrefill::TownGateOpen(1, 1, 1) != 1) Fail("the town gate was shut while held, settled and ready");
    if (townrefill::TownGateOpen(0, 1, 1) != 0 || townrefill::TownGateOpen(1, 0, 1) != 0 || townrefill::TownGateOpen(1, 1, 0) != 0)
        Fail("the town gate opened while not held, not settled, or before the push / snapshot / link were in");
}
void t_t798_group_fault()
{
    if (owedpop::GroupFaultHolds(0, owedpop::kGroupFaultBound) != 1) Fail("a group with no fault run did not hold");
    if (owedpop::GroupFaultHolds(1, owedpop::kGroupFaultBound) != 1) Fail("a group with one faulted check-up stopped holding");
    if (owedpop::GroupFaultHolds(owedpop::kGroupFaultBound - 1, owedpop::kGroupFaultBound) != 1) Fail("a group stopped holding before the bound");
    if (owedpop::GroupFaultHolds(owedpop::kGroupFaultBound, owedpop::kGroupFaultBound) != 0) Fail("a group faulting for the whole bound still held");
    if (owedpop::kGroupFaultBound < 2) Fail("the group fault bound does not allow a transient fault");
}
void t_t798_owed_files()
{
    owedpop::Table t;
    const owedpop::Row r = T581Row(owedpop::kKindResidents, "B1", "Mongrel");
    const std::string tk = owedpop::TableKey(owedpop::kKindResidents, "B1");
    if (owedpop::TableAddFrom(&t, r, 2, 0) != 1) Fail("setup: the row was not stored");
    if (owedpop::TableClaim(&t, owedpop::kKindResidents, "B1", 2, 0.0) != owedpop::kClaimGranted || owedpop::TableDone(&t, owedpop::kKindResidents, "B1", 2, owedpop::kWhyFault, 100.0) != owedpop::kDoneCounted) Fail("setup: the fault was not counted");
    const std::string line = owedpop::MetaLine(t[tk], 160.0);   /* spacing to 400: 240 s left */
    owedpop::Table u; owedpop::TableAdd(&u, r);
    if (owedpop::ParseMetaLine(line, &u, 1000.0) != 1) Fail("a meta line did not read back");
    if (u[tk].adder != 2 || u[tk].faults != 1 || u[tk].gones != 0 || u[tk].empties != 0) Fail("the retry counts or the row's owner did not survive the file");
    if (u[tk].retryAt < 1239.0 || u[tk].retryAt > 1241.0) Fail("the retry spacing left did not survive the file");
    if (owedpop::TableClaim(&u, owedpop::kKindResidents, "B1", 3, 1100.0) != owedpop::kClaimLater) Fail("a reloaded row was given before its spacing ran out");
    if (owedpop::TableClaim(&u, owedpop::kKindResidents, "B1", 3, 1300.0) != owedpop::kClaimGranted) Fail("a reloaded row was not given after its spacing");
    if (owedpop::TableDone(&u, owedpop::kKindResidents, "B1", 3, owedpop::kWhyFault, 1300.0) != owedpop::kDoneCounted || owedpop::TableClaim(&u, owedpop::kKindResidents, "B1", 3, 1700.0) != owedpop::kClaimGranted
        || owedpop::TableDone(&u, owedpop::kKindResidents, "B1", 3, owedpop::kWhyFault, 1700.0) != owedpop::kDoneRemoved) Fail("the count read back did not carry to the third fault");
    owedpop::Table none;
    if (owedpop::ParseMetaLine(line, &none, 0.0) != 0) Fail("a meta line for no row was applied");
    if (owedpop::ParseMetaLine(owedpop::Line(r), &u, 0.0) != 0) Fail("a v1 row line read as a meta line");
    if (owedpop::ParseLine(line, &none) != 0 || !none.empty()) Fail("a meta line read as a v1 row");
    {   /* a fresh row's spacing: none left */
        owedpop::Table v; owedpop::TableAdd(&v, r); owedpop::Table w; owedpop::TableAdd(&w, r);
        if (owedpop::ParseMetaLine(owedpop::MetaLine(v[tk], 50.0), &w, 9.0) != 1 || w[tk].retryAt != 0.0) Fail("a row with no spacing read back with one");
    }
    owedpop::DoneSet d;
    if (owedpop::DoneKept(owedpop::kKindResidents, owedpop::kWhyMade) != 1 || owedpop::DoneKept(owedpop::kKindResidents, owedpop::kWhyHas) != 1) Fail("a filled building was not kept");
    if (owedpop::DoneKept(owedpop::kKindResidents, owedpop::kWhyGone) != 0 || owedpop::DoneKept(owedpop::kKindResidents, owedpop::kWhyFault) != 0
        || owedpop::DoneKept(owedpop::kKindResidents, owedpop::kWhyEmpty) != 0 || owedpop::DoneKept(owedpop::kKindBar, owedpop::kWhyMade) != 0) Fail("a building not filled (or a bar) was kept as filled");
    const std::string dl = owedpop::DoneLineOf(tk);
    if (owedpop::ParseDoneLine(dl, &d) != 1 || owedpop::AddIsDone(d, owedpop::kKindResidents, "B1") != 1) Fail("a done line did not read back");
    if (owedpop::AddIsDone(d, owedpop::kKindBar, "B1") != 0 || owedpop::AddIsDone(d, owedpop::kKindResidents, "B2") != 0) Fail("another key read as filled");
    if (owedpop::ParseDoneLine("d1\t9\tB1\n", &d) != 0 || owedpop::ParseDoneLine("d1\t1\n", &d) != 0 || owedpop::ParseDoneLine(owedpop::Line(r), &d) != 0 || d.size() != 1) Fail("an unusable done line was read");
    if (owedpop::ParseLine(dl, &none) != 0) Fail("a done line read as a v1 row");
    if (!owedpop::DoneLineOf("x").empty()) Fail("a malformed table key made a done line");
    if (worldreset::ResetClassify("owed_done.txt", 0) != worldreset::kResetMove || worldreset::ResetClassify("owed_meta.txt", 0) != worldreset::kResetMove) Fail("a world reset does not move the owed companions with the world");
}
/* the world server's REOPEN verdict (owedpop::ReopenDecide): holder = the slot + 1 the area map names, who = the sender's */
void t_t1090_reopen_decide()
{
    if (owedpop::ReopenDecide(2, 2, 1, 0, 0) != owedpop::kReopenAccepted) Fail("the area's holder, no town loss, no living home: accepted");
    if (owedpop::ReopenDecide(1, 1, 1, 0, 0) != owedpop::kReopenAccepted) Fail("a player alone (slot 0) holding the area: accepted");
    if (owedpop::ReopenDecide(3, 2, 1, 0, 0) != owedpop::kReopenNotHolder) Fail("another game holds the area: not holder");
    if (owedpop::ReopenDecide(0, 2, 1, 0, 0) != owedpop::kReopenNotHolder || owedpop::ReopenDecide(0, 0, 1, 0, 0) != owedpop::kReopenNotHolder) Fail("nobody holds the area (or no sender): not holder");
    if (owedpop::ReopenDecide(3, 2, 0, 4, 2) != owedpop::kReopenNotHolder) Fail("not holder is decided before every other answer");
    if (owedpop::ReopenDecide(2, 2, 1, 1, 0) != owedpop::kReopenTownLoss || owedpop::ReopenDecide(2, 2, 1, 1, 3) != owedpop::kReopenTownLoss) Fail("a town with a recorded loss: town loss (before living home)");
    if (owedpop::ReopenDecide(2, 2, 1, 0, 1) != owedpop::kReopenLivingHome) Fail("a living record homed at the building: living home");
    if (owedpop::ReopenDecide(2, 2, 0, 0, 0) != owedpop::kReopenUnknown) Fail("the loss table unread: unknown");
    if (owedpop::ReopenDecide(2, 2, 1, 0, -1) != owedpop::kReopenUnknown) Fail("the homes not countable: unknown");
    /* each refusal answers GONE with its own reason; none of them is has (which would drop the work as made) or full (made locally) */
    const int ref[4] = { owedpop::kReopenNotHolder, owedpop::kReopenTownLoss, owedpop::kReopenLivingHome, owedpop::kReopenUnknown };
    const unsigned why[4] = { owedpop::kWhyReopenNotHolder, owedpop::kWhyReopenTownLoss, owedpop::kWhyReopenLivingHome, owedpop::kWhyReopenUnknown };
    for (int i = 0; i < 4; ++i)
    {
        const unsigned w = owedpop::ReopenRefusalWhy(ref[i]);
        if (w != why[i] || owedpop::ReopenRefused(w) != 1) Fail("a refused verdict does not answer its own reason");
        if (w == owedpop::kWhyHas || owedpop::GoneKeepsLocal(w) != 0 || owedpop::UpWhyOk(w) || owedpop::DoneKept(owedpop::kKindResidents, w) != 0) Fail("a refusal reason reads as has / full / an up reason / filled");
        if (std::strcmp(owedpop::WhyName(w), "?") == 0 || std::strcmp(owedpop::ReopenName(ref[i]), "?") == 0) Fail("a refusal has no name");
    }
    if (owedpop::ReopenRefusalWhy(owedpop::kReopenAccepted) != 0 || owedpop::ReopenRefused(owedpop::kWhyHas) != 0 || owedpop::ReopenRefused(owedpop::kWhyFull) != 0 || owedpop::ReopenRefused(0) != 0) Fail("accepted / has / full read as a refused REOPEN");
    /* accepted lifts the filled mark and the row is then stored as an ADD; a refusal leaves the mark */
    owedpop::DoneSet d; owedpop::Table t;
    const owedpop::Row r = T581Row(owedpop::kKindResidents, "B1", "Mongrel");
    d.insert(owedpop::TableKey(owedpop::kKindResidents, "B1"));
    for (int i = 0; i < 4; ++i) if (owedpop::ReopenApply(&d, owedpop::kKindResidents, "B1", ref[i]) != 0 || owedpop::AddIsDone(d, owedpop::kKindResidents, "B1") != 1) Fail("a refused REOPEN lifted the mark");
    if (owedpop::ReopenApply(&d, owedpop::kKindResidents, "B1", owedpop::kReopenAccepted) != 1 || owedpop::AddIsDone(d, owedpop::kKindResidents, "B1") != 0) Fail("an accepted REOPEN did not lift the mark");
    if (owedpop::ReopenApply(&d, owedpop::kKindResidents, "B1", owedpop::kReopenAccepted) != 0) Fail("a building not on the list was lifted twice");
    if (owedpop::TableAddFrom(&t, r, 2, owedpop::kRowsPerGame) != 1 || owedpop::TableClaim(&t, owedpop::kKindResidents, "B1", 2, 0.0) != owedpop::kClaimGranted) Fail("the reopened row was not stored and given to the holder");
    if (owedpop::TableDone(&t, owedpop::kKindResidents, "B1", 2, owedpop::kWhyMade, 0.0) != owedpop::kDoneRemoved || owedpop::DoneKept(owedpop::kKindResidents, owedpop::kWhyMade) != 1) Fail("the reopened row's make does not mark the building filled again");
}

/* ================= T-804: the dialogue window's portrait refresh (src/common/portraitdefer.h) =================
 * Called now only on the main thread with the AI worker not running; held one per slot (the newest wins); a refresh run at
 * once empties its slot; replayed only while the window shows the conversation it was held for, is visible, and the
 * character still exists; a held refresh with no character is dropped. */
void t_t804_portrait_hold()
{
    /* when the engine's refresh runs at once */
    if (cooppd::CallNow(1, 0) != 1) Fail("main thread, worker idle: not called now");
    if (cooppd::CallNow(1, -1) != 1) Fail("main thread, worker unreadable: not called now (it must run as the engine runs it)");
    if (cooppd::CallNow(1, 1) != 0) Fail("main thread, worker running: called now");
    if (cooppd::CallNow(0, 0) != 0 || cooppd::CallNow(0, -1) != 0 || cooppd::CallNow(0, 1) != 0) Fail("off the main thread: called now");
    /* the slot byte's entry */
    if (cooppd::SlotIndex(0) != 0 || cooppd::SlotIndex(1) != 1 || cooppd::SlotIndex(255) != 1) Fail("slot index");
    /* the table: one per slot, the newest wins, a take empties it */
    cooppd::HeldTable t;
    cooppd::Clear(&t);
    cooppd::Held a; a.used = 0; a.slot = 0; a.why = cooppd::kWhyOffThread; a.hadWho = 1; a.dlgAtHold = 0;
    for (int i = 0; i < 5; ++i) a.hand[i] = (unsigned int)(10 + i);
    cooppd::Held b = a; b.hand[3] = 99;
    cooppd::Held c = a; c.slot = 1; c.why = cooppd::kWhyWorkerBusy;
    if (cooppd::Put(&t, a) != 0) Fail("a first hold reported as a replacement");
    if (cooppd::Put(&t, c) != 0) Fail("slot 1 replaced slot 0's entry");
    if (cooppd::Put(&t, b) != 1) Fail("a second hold for slot 0 not reported as a replacement");
    cooppd::Held got;
    if (cooppd::Take(&t, 0, &got) != 1 || got.hand[3] != 99 || got.used != 1) Fail("slot 0 did not give back the newest hold");
    if (cooppd::Take(&t, 0, &got) != 0) Fail("a taken slot gave a hold again");
    if (cooppd::Take(&t, 1, &got) != 1 || got.slot != 1 || got.why != cooppd::kWhyWorkerBusy) Fail("slot 1's hold lost");
    if (cooppd::Take(&t, 2, &got) != 0 || cooppd::Take(&t, -1, &got) != 0) Fail("an index outside the table read");
    /* a refresh run at once empties its slot, so an older held one cannot paint over it at the safe point */
    cooppd::Put(&t, a);
    cooppd::Put(&t, c);
    if (cooppd::Drop(&t, 255) != 1) Fail("a direct refresh for slot 1 did not empty slot 1's held entry");
    if (cooppd::Take(&t, 1, &got) != 0) Fail("slot 1 still replays after a direct refresh");
    if (cooppd::Drop(&t, 1) != 0) Fail("an empty slot reported a dropped hold");
    if (cooppd::Take(&t, 0, &got) != 1 || got.slot != 0) Fail("a direct refresh for slot 1 emptied slot 0");
    /* the replay */
    int w = 0, d1 = 0, d2 = 0, ch = 0;
    if (cooppd::ReplayVerdict(&w, 1, &d1, &d1, 1, &ch) != cooppd::kApply) Fail("same conversation, character here: not applied");
    if (cooppd::ReplayVerdict(&w, 1, &d1, 0, 1, &ch) != cooppd::kApply) Fail("held with no conversation shown, one shown now: not applied");
    if (cooppd::ReplayVerdict(&w, 1, &d1, &d1, 0, 0) != cooppd::kDropChar) Fail("a refresh with no character: not dropped (the engine reads its name unchecked)");
    if (cooppd::ReplayVerdict(&w, 0, &d1, &d1, 1, &ch) != cooppd::kDropHidden) Fail("hidden window: not dropped");
    if (cooppd::ReplayVerdict(&w, -1, &d1, &d1, 1, &ch) != cooppd::kDropHidden) Fail("window visibility unreadable: not dropped");
    if (cooppd::ReplayVerdict(0, 1, &d1, &d1, 1, &ch) != cooppd::kDropWindow) Fail("no window: not dropped");
    if (cooppd::ReplayVerdict(&w, 1, 0, &d1, 1, &ch) != cooppd::kDropWindow) Fail("window shows nothing: not dropped");
    if (cooppd::ReplayVerdict(&w, 1, &d2, &d1, 1, &ch) != cooppd::kDropWindow) Fail("another conversation: not dropped");
    if (cooppd::ReplayVerdict(&w, 0, &d2, &d1, 1, &ch) != cooppd::kDropWindow) Fail("hidden window of another conversation: not counted as the window");
    if (cooppd::ReplayVerdict(&w, 1, &d1, &d1, 1, 0) != cooppd::kDropChar) Fail("character gone: not dropped");
    if (cooppd::ReplayVerdict(0, 0, 0, &d1, 1, 0) != cooppd::kDropWindow) Fail("window and character gone: not counted as the window");
}

/* talkprompt answer / answer act use the mirrored conversation while one is open, else this game's own window's conversation with
   a live owner; close and joinact too (joinact never on a mirror) (src/common/talkwire.h LeverWindowPick). */
void t809_lever_window_pick()
{
    using namespace cooptalk;
    const char* why = 0;
    if (LeverWindowPick(kTalkLeverAnswer, 1, 1, 1, &why) != kLeverWinMirror) Fail("answer with a mirror open: not the mirror");
    if (LeverWindowPick(kTalkLeverClose, 1, 0, 0, &why) != kLeverWinMirror) Fail("close with a mirror open: not the mirror");
    if (LeverWindowPick(kTalkLeverAnswer, 0, 1, 1, &why) != kLeverWinOwn) Fail("answer, no mirror, own window live: not the own window");
    if (LeverWindowPick(kTalkLeverAnswerAct, 0, 1, 1, &why) != kLeverWinOwn) Fail("answer act, no mirror, own window live: not the own window");
    if (LeverWindowPick(kTalkLeverClose, 0, 1, 1, &why) != kLeverWinOwn) Fail("close, no mirror, own window live: not the own window");
    if (LeverWindowPick(kTalkLeverClose, 0, 0, 0, &why) != kLeverWinNone || why == 0 || *why == 0) Fail("close, nothing shown: not refused with a reason");
    if (LeverWindowPick(kTalkLeverClose, 0, 1, 0, &why) != kLeverWinNone || why == 0 || *why == 0) Fail("close, owner not live: not refused with a reason");
    if (LeverWindowPick(kTalkLeverAnswer, 0, 0, 0, &why) != kLeverWinNone || why == 0 || *why == 0) Fail("answer, nothing shown: not refused with a reason");
    if (LeverWindowPick(kTalkLeverAnswerAct, 0, 1, 0, &why) != kLeverWinNone || why == 0 || *why == 0) Fail("answer act, owner not live: not refused with a reason");
    if (LeverWindowPick(kTalkLeverJoinAct, 0, 1, 1, &why) != kLeverWinOwn) Fail("joinact, no mirror, own window live: not the own window");
    if (LeverWindowPick(kTalkLeverJoinAct, 1, 1, 1, &why) != kLeverWinNone || why == 0 || *why == 0) Fail("joinact, mirror open: not refused with a reason");
    if (LeverWindowPick(kTalkLeverJoinAct, 0, 0, 0, &why) != kLeverWinNone) Fail("joinact, no window: not refused");
}

/* `spawncontainer new | 0 | 1`: the word read (src/common/spawnage.h SpawnContainerWord) - 0 and 1 as before, "new" the third. */
void t1170_spawncontainer_word()
{
    using namespace coopspawn;
    if (SpawnContainerWord("new") != kSpawnContainerNew) Fail("new: not the new-squad mode");
    if (SpawnContainerWord("0") != kSpawnContainerNull) Fail("0: not the null container");
    if (SpawnContainerWord("1") != kSpawnContainerKeep) Fail("1: not the reference platoon");
    if (SpawnContainerWord("2") != kSpawnContainerKeep) Fail("2: a non-zero integer is not the reference platoon");
    if (SpawnContainerWord("") != kSpawnContainerKeep) Fail("nothing: not the reference platoon (the default)");
    if (SpawnContainerWord("x") != kSpawnContainerKeep) Fail("a non-number: not the reference platoon (as before)");
    if (SpawnContainerWord("NEW") != kSpawnContainerKeep) Fail("NEW: the word is lower case only");
    if (kSpawnContainerNull == kSpawnContainerKeep || kSpawnContainerKeep == kSpawnContainerNew || kSpawnContainerNull == kSpawnContainerNew) Fail("the three modes are not distinct");
}

/* `editorstate [watch <N>]` and the editor's per-recruit body test (src/common/editorstate.h). */
void t809_editorstate()
{
    int n = -1;
    if (!coopeditor::EditorStateParse("", &n) || n != 0) Fail("editorstate: no argument is one line");
    if (!coopeditor::EditorStateParse("watch 30", &n) || n != 30) Fail("editorstate watch 30");
    if (!coopeditor::EditorStateParse("  watch   120 ", &n) || n != 120) Fail("editorstate watch 120 with spaces");
    if (coopeditor::EditorStateParse("watch 121", &n)) Fail("editorstate watch 121 must be refused");
    if (coopeditor::EditorStateParse("watch 0", &n)) Fail("editorstate watch 0 must be refused");
    if (coopeditor::EditorStateParse("watch", &n)) Fail("editorstate watch without N must be refused");
    if (coopeditor::EditorStateParse("watch 5x", &n)) Fail("editorstate watch 5x must be refused");
    if (coopeditor::EditorStateParse("watchx 5", &n)) Fail("editorstate watchx must be refused");
    if (coopeditor::EditorStateParse("now", &n)) Fail("editorstate now must be refused");
    if (coopeditor::EditorBodyReady(1, 1, 0) != 1) Fail("body with an entity: not ready");
    if (coopeditor::EditorBodyReady(0, 1, 1) != 0) Fail("no entity, one pending: ready");
    if (coopeditor::EditorBodyReady(0, 0, 0) != 0) Fail("no entity, none pending, not done: ready");
    if (coopeditor::EditorBodyReady(0, 0, 1) != 1) Fail("no entity, none pending, done: not ready");
    /* when the paused hire editor's recruits get the animation update call */
    const int lim = coopeditor::kBodyStepGiveUpFrames;
    if (coopeditor::EditorBodyStepDue(1, 1, 0, 0) != 1) Fail("paused hire editor, no recruit on show: not stepped");
    if (coopeditor::EditorBodyStepDue(0, 1, 0, 0) != 0) Fail("running game: stepped");
    if (coopeditor::EditorBodyStepDue(1, 0, 0, 0) != 0) Fail("editor not in the hire mode: stepped");
    if (coopeditor::EditorBodyStepDue(1, 1, 1, 0) != 0) Fail("a recruit on show: stepped");
    if (coopeditor::EditorBodyStepDue(1, 1, 0, lim - 1) != 1) Fail("one frame under the limit: not stepped");
    if (coopeditor::EditorBodyStepDue(1, 1, 0, lim) != 0) Fail("at the frame limit: stepped");
    if (coopeditor::EditorRecruitStepDue(1, 1, 0) != 1) Fail("skipped recruit, body not ready: not stepped");
    if (coopeditor::EditorRecruitStepDue(0, 1, 0) != 0) Fail("a recruit the engine updates itself: stepped");
    if (coopeditor::EditorRecruitStepDue(1, 0, 0) != 0) Fail("no body object: stepped");
    if (coopeditor::EditorRecruitStepDue(1, 1, 1) != 0) Fail("body ready: stepped");
    if (coopeditor::EditorBodyGiveUpDue(1, 1, 0, lim, 1) != 1) Fail("limit reached, a body missing: no give-up line");
    if (coopeditor::EditorBodyGiveUpDue(1, 1, 0, lim - 1, 1) != 0) Fail("under the limit: give-up line");
    if (coopeditor::EditorBodyGiveUpDue(1, 1, 0, lim, 0) != 0) Fail("all bodies ready: give-up line");
    if (coopeditor::EditorBodyGiveUpDue(0, 1, 0, lim, 1) != 0) Fail("unpaused: give-up line");
    if (coopeditor::EditorBodyGiveUpDue(1, 1, 1, lim, 1) != 0) Fail("a recruit on show: give-up line");
}

/* ================= T-1230: the trade window never calls through a freed trader (src/common/tradewinguard.h) =================
 * An open trade window is kept while its ShopTrader reads as a shop stand-in and its trader is a live address; otherwise it
 * closes, or waits for the safe point inside the engine's own window change. Before a destroy, only the window's own trader
 * closes it, and only on the main thread outside a window change. The cursor item's put-back road; the destroy's road; hands. */
void t_t1230_trade_window_guard()
{
    using namespace tradewinguard;
    /* the backstop verdict: every input combination */
    for (int f = -1; f <= 1; ++f)
        for (int l = 0; l <= 1; ++l)
            for (int b = 0; b <= 1; ++b)
            {
                if (Verdict(0, f, l, b) != kKeep) Fail("no window open: something done");
                const int v = Verdict(1, f, l, b);
                if (f == 1 && l == 1) { if (v != kKeep) Fail("open, shop reads, trader live: not kept"); }
                else if (b == 1) { if (v != kWait) Fail("open, trader gone or shop unreadable, inside a window change: not waited"); }
                else if (v != kClose) Fail("open, trader gone or shop unreadable: not closed");
            }
    if (Verdict(1, 1, 0, 0) != kClose) Fail("T1965's case (the trader freed under an open window): not closed");
    if (Verdict(1, 0, 1, 0) != kClose || Verdict(1, -1, 1, 0) != kClose) Fail("a ShopTrader that does not read as one: not closed");
    /* before a destroy: only the window's own trader, compared by address */
    int trader = 0, other = 0;
    if (BeforeDestroy(1, 1, &trader, &trader, 1, 0) != kClose) Fail("the trader doomed on the main thread: not closed");
    if (BeforeDestroy(1, 1, &other, &trader, 1, 0) != kKeep) Fail("another object doomed: the window closed");
    if (BeforeDestroy(1, 1, 0, &trader, 1, 0) != kKeep || BeforeDestroy(1, 1, &trader, 0, 1, 0) != kKeep) Fail("no object or no trader: acted");
    if (BeforeDestroy(0, 1, &trader, &trader, 1, 0) != kKeep) Fail("no window open: acted");
    if (BeforeDestroy(1, 0, &trader, &trader, 1, 0) != kKeep || BeforeDestroy(1, -1, &trader, &trader, 1, 0) != kKeep) Fail("a shop that does not read: acted on its stale trader");
    if (BeforeDestroy(1, 1, &trader, &trader, 0, 0) != kWait) Fail("the trader doomed off the main thread: not left to the backstop");
    if (BeforeDestroy(1, 1, &trader, &trader, 1, 1) != kWait) Fail("the trader doomed inside a window change: not left to the backstop");
    /* the cursor item's put-back road */
    int held = 0, copy = 0;
    if (CursorRoad(0, &copy) != kCurNone || CursorRoad(0, 0) != kCurNone) Fail("an empty cursor: a put-back");
    if (CursorRoad(&copy, &copy) != kCurShopCopy) Fail("the tracked shop copy: not put back under the guard");
    if (CursorRoad(&held, &copy) != kCurOwn || CursorRoad(&held, 0) != kCurOwn) Fail("another item: not put back plainly");
    /* the destroy's road */
    if (DestroyRoad(1, 0) != kRoadUnload) Fail("the engine's unload: not the unload road");
    if (DestroyRoad(0, 0) != kRoadEngineRemoval) Fail("another engine removal: wrong road");
    if (DestroyRoad(0, 1) != kRoadModRemoval || DestroyRoad(1, 1) != kRoadModRemoval) Fail("this mod's removal: wrong road");
    /* the hand compare (the record ends when the engine's close names the ShopTrader's hand) */
    const unsigned int h1[5] = { 0x5B, 2, 3, 4, 5 }, h2[5] = { 0x5B, 2, 3, 4, 5 }, h3[5] = { 0x5B, 2, 3, 4, 6 }, h4[5] = { 1, 2, 3, 4, 5 };
    if (SameHand(h1, h2) != 1) Fail("the same hand words: not the same");
    if (SameHand(h1, h3) != 0 || SameHand(h1, h4) != 0) Fail("different serial or type: the same");
    if (SameHand(h1, 0) != 0 || SameHand(0, h1) != 0) Fail("no hand: the same");
    /* review fold 1: the engine's close ends the record by the hand words read BEFORE the close. A close through the engine's own copy
       (gui +0x120) empties that copy before it returns (type kHandTypeNone, the rest 0): words read after it never match. */
    const unsigned int wiped[5] = { kHandTypeNone, 0, 0, 0, 0 };
    if (CloseEndsRecord(1, 0, 1, h2, h1) != 1) Fail("the trader window's hand read before the close: the record not ended");
    if (CloseEndsRecord(1, 0, 1, wiped, h1) != 0) Fail("an emptied hand (read after the engine's close) ended the record");
    if (CloseEndsRecord(1, 0, 1, h3, h1) != 0) Fail("another window's hand ended the record");
    if (CloseEndsRecord(1, 0, 0, h2, h1) != 0) Fail("unread hand words ended the record");
    if (CloseEndsRecord(1, 1, 0, 0, h1) != 1) Fail("closeAllInventories (every window): the record not ended");
    if (CloseEndsRecord(0, 1, 0, 0, h1) != 0 || CloseEndsRecord(0, 0, 1, h2, h1) != 0) Fail("no record open: ended");
}

/* ================= T-831: the dialogue window never calls through a removed character (src/common/dlgwinguard.h) =================
 * The shown conversation ends when its target or the window's stored character is gone; when its owner is gone nothing of the
 * Dialogue is called - the window closes through its own hide, or nothing is called without that row; the stored character counts as here only by address against the live owner, the resolved target or the
 * engine's character lists; before this mod removes a character the shown conversation names, it ends. */
void t_t831_window_guard()
{
    /* the verdict from the three liveness values */
    if (winguard::Verdict(1, 1, 1, 1) != winguard::kKeep) Fail("all here: not kept");
    if (winguard::Verdict(1, -1, -1, 1) != winguard::kKeep) Fail("owner here, no target named, nothing stored: not kept");
    if (winguard::Verdict(1, 1, -1, 1) != winguard::kKeep || winguard::Verdict(1, -1, 1, 1) != winguard::kKeep) Fail("nothing to check on one part: not kept");
    if (winguard::Verdict(1, 0, 1, 1) != winguard::kEnd) Fail("target gone: not ended");
    if (winguard::Verdict(1, 1, 0, 1) != winguard::kEnd) Fail("stored character gone: not ended");
    if (winguard::Verdict(1, 0, 0, 1) != winguard::kEnd || winguard::Verdict(1, -1, 0, 1) != winguard::kEnd) Fail("stored character gone with the target gone or unnamed: not ended");
    for (int t = -1; t <= 1; ++t)
        for (int s = -1; s <= 1; ++s)
        {
            if (winguard::Verdict(0, t, s, 1) != winguard::kHide) Fail("owner gone, hide row: the window not closed through hide");
            if (winguard::Verdict(-1, t, s, 1) != winguard::kHide) Fail("owner unreadable, hide row: the window not closed through hide");
            if (winguard::Verdict(0, t, s, 0) != winguard::kOwnerGone) Fail("owner gone, no hide row: something would be called");
            if (winguard::Verdict(-1, t, s, 0) != winguard::kOwnerGone) Fail("owner unreadable, no hide row: something would be called");
        }
    /* the hide row never changes a conversation whose owner is here */
    if (winguard::Verdict(1, 1, 1, 1) != winguard::kKeep || winguard::Verdict(1, 0, 1, 1) != winguard::kEnd
        || winguard::Verdict(1, 1, 0, 0) != winguard::kEnd) Fail("the hide row changed a live owner's verdict");
    /* the gone parts */
    if (winguard::GoneMask(1, 1, 1) != 0 || winguard::GoneMask(1, -1, -1) != 0) Fail("a mask with nothing gone");
    if (winguard::GoneMask(0, -1, 0) != (unsigned)(winguard::kGoneOwner | winguard::kGoneShown)) Fail("owner and stored character mask");
    if (winguard::GoneMask(1, 0, 1) != (unsigned)winguard::kGoneTarget) Fail("target mask");
    /* the stored character: compared, never trusted */
    int own = 0, tgt = 0, other = 0;
    if (winguard::ShownLive(0, &own, &tgt, 1) != -1) Fail("nothing stored: not 'nothing to check'");
    if (winguard::ShownLive(&own, &own, &tgt, 0) != 1) Fail("stored = the live owner: not here");
    if (winguard::ShownLive(&tgt, &own, &tgt, 0) != 1) Fail("stored = the resolved target: not here");
    if (winguard::ShownLive(&other, &own, &tgt, 1) != 1) Fail("stored, in the engine's lists: not here");
    if (winguard::ShownLive(&other, &own, &tgt, 0) != 0) Fail("stored, in no list and neither part: not gone");
    if (winguard::ShownLive(&tgt, &own, 0, 0) != 0) Fail("stored = a target the handle no longer resolves: not gone");
    if (winguard::ShownLive(&own, 0, &tgt, 0) != 0) Fail("stored = an owner that is not live: not gone");
    /* the stored character is cleared only while the window still shows the conversation and it is gone */
    if (winguard::ClearShown(1, 0) != 1) Fail("still shown, stored gone: not cleared");
    if (winguard::ClearShown(0, 0) != 0) Fail("window closed: cleared anyway");
    if (winguard::ClearShown(1, 1) != 0 || winguard::ClearShown(1, -1) != 0) Fail("stored here or none: cleared");
    /* before this mod removes a character */
    if (winguard::EndBeforeRemove(&own, &own, &tgt, &other) != 1) Fail("removing the owner: not ended first");
    if (winguard::EndBeforeRemove(&tgt, &own, &tgt, &other) != 1) Fail("removing the target: not ended first");
    if (winguard::EndBeforeRemove(&other, &own, &tgt, &other) != 1) Fail("removing the stored character: not ended first");
    int unrelated = 0;
    if (winguard::EndBeforeRemove(&unrelated, &own, &tgt, &other) != 0) Fail("removing an unrelated character: ended");
    if (winguard::EndBeforeRemove(0, 0, 0, 0) != 0) Fail("removing nothing: ended");
    if (winguard::RemoveVerdict(1, 1) != winguard::kEnd || winguard::RemoveVerdict(1, 0) != winguard::kEnd) Fail("owner here before a removal: not ended");
    if (winguard::RemoveVerdict(0, 1) != winguard::kHide) Fail("owner gone before a removal, hide row: not closed through hide");
    if (winguard::RemoveVerdict(0, 0) != winguard::kOwnerGone) Fail("owner gone before a removal, no hide row: something would be called");
    if (winguard::EndBeforeRemove(&unrelated, &own, 0, 0) != 0) Fail("no target, nothing stored: an unrelated character ended it");
}
/* ================= T-761: a save-born non-player twin no game drives here - put to sleep or taken (src/common/twinsleep.h) ================= */
int t761_KeepBox(void*, float x, float z) { return (x > -1000.0f && x < 1000.0f && z > -1000.0f && z < 1000.0f) ? 1 : 0; }
int t761_KeepAll(void*, float, float) { return 1; }
int t761_KeepUnread(void*, float, float) { return -1; }
void t_t761_twin_sleep()
{
    /* the member's own branch: not a twin, or a player / player-faction twin - unchanged whatever the kind and the keep */
    for (int cg = 0; cg <= 2; ++cg)
        for (int k = -1; k <= 1; ++k)
        {
            if (twinsleep::WithdrawBranch(0, 0, cg, k) != twinsleep::kUnchanged) Fail("not a twin: its withdrawal changed");
            if (twinsleep::WithdrawBranch(1, 1, cg, k) != twinsleep::kUnchanged) Fail("a player twin: its withdrawal changed");
            if (twinsleep::WithdrawBranch(0, 1, cg, k) != twinsleep::kUnchanged) Fail("a player copy that is no twin: its withdrawal changed");
        }
    if (twinsleep::WithdrawBranch(1, 0, twinsleep::kKindRunnerRuns, 1) == twinsleep::kTake || twinsleep::WithdrawBranch(1, 0, twinsleep::kKindNeverTake, 1) == twinsleep::kTake)
        Fail("a twin its runner runs, or a REVOKE's: taken on its own branch");
    if (twinsleep::WithdrawBranch(1, 0, twinsleep::kKindNobodyRuns, 1) != twinsleep::kTake) Fail("nobody runs it and this game keeps its spot: not taken");
    if (twinsleep::WithdrawBranch(1, 0, twinsleep::kKindNobodyRuns, 0) == twinsleep::kTake || twinsleep::WithdrawBranch(1, 0, twinsleep::kKindNobodyRuns, -1) == twinsleep::kTake)
        Fail("nobody runs it, the spot not kept or unreadable: taken (the hand-over's take test refuses an unreadable spot)");
    if (twinsleep::WithdrawBranch(1, 0, 7, 1) != twinsleep::kTake) Fail("an unknown kind: not read as nobody runs it");

    /* the squad's road: its runner still runs it */
    if (twinsleep::SquadMode(1, 0, 0, 1, 0, 1) != twinsleep::kModeRunnerSpot) Fail("runner: its spot not kept here - not put to sleep there");
    if (twinsleep::SquadMode(1, 0, 0, 1, 1, 0) != twinsleep::kModeWait || twinsleep::SquadMode(1, 0, 0, 1, -1, 0) != twinsleep::kModeWait
        || twinsleep::SquadMode(1, 0, 0, 0, -1, 0) != twinsleep::kModeWait) Fail("runner, early: its spot kept, unreadable or unknown - did not wait for the SPAWN");
    if (twinsleep::SquadMode(1, 0, 1, 1, 1, 1) != twinsleep::kModeStrip || twinsleep::SquadMode(1, 0, 1, 1, 0, 1) != twinsleep::kModeStrip)
        Fail("runner, late, its spot known: not the strip sleep toward it");
    if (twinsleep::SquadMode(1, 0, 1, 0, -1, 0) != twinsleep::kModeStrip) Fail("runner, late, no spot: not the bounded strip road");
    if (twinsleep::SquadMode(1, 0, 1, 0, -1, 1) != twinsleep::kModeStrip || twinsleep::SquadMode(1, 0, 1, 0, -1, -1) != twinsleep::kModeStrip)
        Fail("runner, late, no spot, where it stands kept or unreadable: not the strip sleep (a last-resort take instead?)");
    if (twinsleep::SquadMode(1, 1, 0, 1, 1, 0) != twinsleep::kModeWait) Fail("a squad with a runner's member and a REVOKE's: the runner's road did not lead");
    /* a REVOKE with no winner: never put to sleep where this engine keeps it (it would wake at once as this game's own) */
    if (twinsleep::SquadMode(0, 1, 0, 0, -1, 0) != twinsleep::kModeHere) Fail("REVOKE, where it stands not kept: not put to sleep there");
    if (twinsleep::SquadMode(0, 1, 0, 0, -1, 1) != twinsleep::kModeStrip || twinsleep::SquadMode(0, 1, 0, 0, -1, -1) != twinsleep::kModeStrip)
        Fail("REVOKE, where it stands kept or unreadable: put to sleep in place");
    if (twinsleep::SquadMode(0, 1, 1, 0, -1, 0) != twinsleep::kModeStrip) Fail("REVOKE, late: not the strip sleep");
    /* nobody runs it */
    if (twinsleep::SquadMode(0, 0, 0, 0, -1, 0) != twinsleep::kModeHere || twinsleep::SquadMode(0, 0, 1, 0, -1, 0) != twinsleep::kModeTake)
        Fail("nobody runs it: not put to sleep in place early, or not taken late");

    /* the shared squad timer: the OLDEST member's stamp, the tick count's wrap included */
    {
        const unsigned long at[3] = { 20000ul, 5000ul, 31000ul };
        if (twinsleep::SquadAgeSec(at, 3, 35000ul) != 30.0) Fail("squad timer: not the oldest member's age");
        if (twinsleep::SquadAgeSec(at, 3, 35000ul) < twinsleep::kAwaitSec) Fail("squad timer: a member queued later restarted its squad's wait");
        const unsigned long wrap[2] = { 0xFFFFF000ul, 1000ul };
        if (twinsleep::SquadAgeSec(wrap, 2, 2000ul) < 6.0 || twinsleep::SquadAgeSec(wrap, 2, 2000ul) > 6.2) Fail("squad timer: the tick count's wrap not followed");
        if (twinsleep::SquadAgeSec(at, 0, 35000ul) != 0.0) Fail("squad timer: an empty squad has an age");
    }

    /* the gate */
    if (twinsleep::SleepGate(1, 0, 0, 0) != twinsleep::kGateGo) Fail("an awake squad with nothing holding it: not put to sleep");
    for (int a = -1; a <= 1; ++a)
        for (int b = -1; b <= 1; ++b)
            for (int h = -1; h <= 1; ++h)
                if (twinsleep::SleepGate(0, a, b, h) != twinsleep::kGateDone) Fail("a squad not awake: something still to do");
    if (twinsleep::SleepGate(-1, 0, 0, 0) != twinsleep::kGateWait) Fail("an unreadable squad: did not wait");
    if (twinsleep::SleepGate(1, 1, 0, 0) != twinsleep::kGateWait) Fail("a member this game runs: put to sleep");
    if (twinsleep::SleepGate(1, 0, 1, 0) != twinsleep::kGateWait) Fail("a member still a puppet: put to sleep");
    if (twinsleep::SleepGate(1, 0, 0, 1) != twinsleep::kGateWait) Fail("a member carried: put to sleep");
    if (twinsleep::SleepGate(1, -1, 0, 0) != twinsleep::kGateWait || twinsleep::SleepGate(1, 0, -1, 0) != twinsleep::kGateWait
        || twinsleep::SleepGate(1, 0, 0, -1) != twinsleep::kGateWait) Fail("an unreadable member: put to sleep");

    /* the strip sleep's gate: a shut gate waits its full time, never an instant take; then the last resort */
    if (twinsleep::StripGate(twinsleep::kGateGo, 0.0) != twinsleep::kSgCall || twinsleep::StripGate(twinsleep::kGateGo, 100.0) != twinsleep::kSgCall) Fail("strip gate open: not called");
    if (twinsleep::StripGate(twinsleep::kGateWait, 0.0) != twinsleep::kSgWait || twinsleep::StripGate(twinsleep::kGateWait, 29.9) != twinsleep::kSgWait)
        Fail("strip gate shut: taken before its wait (an instant take)");
    if (twinsleep::StripGate(twinsleep::kGateWait, 30.0) != twinsleep::kSgLastResort || twinsleep::StripGate(twinsleep::kGateWait, 300.0) != twinsleep::kSgLastResort)
        Fail("strip gate shut past its wait: left frozen (no last resort)");

    /* after the call */
    if (twinsleep::AfterCall(0.0, 1) != twinsleep::kCallAwait || twinsleep::AfterCall(9.9, 1) != twinsleep::kCallAwait) Fail("first call: judged before its 10 s");
    if (twinsleep::AfterCall(10.0, 1) != twinsleep::kCallAgain || twinsleep::AfterCall(60.0, 1) != twinsleep::kCallAgain) Fail("first call, still awake after 10 s: not called again");
    if (twinsleep::AfterCall(9.9, 2) != twinsleep::kCallAwait) Fail("second call: judged before its 10 s");
    if (twinsleep::AfterCall(10.0, 2) != twinsleep::kCallGiveUp || twinsleep::AfterCall(10.0, 3) != twinsleep::kCallGiveUp) Fail("second call, still awake after 10 s: not given up");

    /* a member whose squad cannot be read: waits, bounded */
    if (twinsleep::NoSquadDecide(twinsleep::kKindNobodyRuns, 29.9) != twinsleep::kNsWait || twinsleep::NoSquadDecide(twinsleep::kKindNobodyRuns, 30.0) != twinsleep::kNsTake)
        Fail("no squad, nobody runs it: not taken at 30 s");
    for (int kd = 0; kd <= 2; kd += 2)
        if (twinsleep::NoSquadDecide(kd, 59.9) != twinsleep::kNsWait || twinsleep::NoSquadDecide(kd, 60.0) != twinsleep::kNsLastResort)
            Fail("no squad, a runner's or a REVOKE's: not waited 60 s, or left frozen after it");
    if (twinsleep::kBoundSec < twinsleep::kAwaitSec + twinsleep::kStripGateSec + twinsleep::kRecheckSec) Fail("the bound is shorter than its roads");

    /* the strip point: the nearest point outside the kept box (|x| < 1000, |z| < 1000) on the line toward the target, bounded */
    float ox = 0.0f, oz = 0.0f;
    if (twinsleep::StripPoint(0.0f, 0.0f, 500.0f, 0.0f, t761_KeepBox, 0, &ox, &oz) != 1 || !(ox >= 1000.0f && ox < 1000.0f + twinsleep::kStripStep) || oz != 0.0f)
        Fail("strip point: not the first point outside the kept box past the target");
    if (twinsleep::StripPoint(0.0f, 0.0f, 0.0f, -3000.0f, t761_KeepBox, 0, &ox, &oz) != 1 || !(oz <= -1000.0f && oz > -1000.0f - twinsleep::kStripStep))
        Fail("strip point: not the nearest point outside toward the target");
    if (twinsleep::StripPoint(10.0f, 10.0f, 10.5f, 10.0f, t761_KeepBox, 0, &ox, &oz) != 0) Fail("strip point: found with no direction");
    if (twinsleep::StripPoint(0.0f, 0.0f, 500.0f, 0.0f, t761_KeepAll, 0, &ox, &oz) != 0) Fail("strip point: found past the bound");
    if (twinsleep::StripPoint(0.0f, 0.0f, 500.0f, 0.0f, t761_KeepUnread, 0, &ox, &oz) != 0) Fail("strip point: an unreadable keep read as outside");
    /* with no direction: the nearest way out of the four */
    if (twinsleep::StripPointAround(900.0f, 0.0f, t761_KeepBox, 0, &ox, &oz) != 1 || !(ox >= 1000.0f && ox < 1000.0f + twinsleep::kStripStep) || oz != 0.0f)
        Fail("strip point around: not the nearest way out");
    if (twinsleep::StripPointAround(0.0f, -950.0f, t761_KeepBox, 0, &ox, &oz) != 1 || !(oz <= -1000.0f) || ox != 0.0f) Fail("strip point around: not the nearest way out (-z)");
    if (twinsleep::StripPointAround(0.0f, 0.0f, t761_KeepAll, 0, &ox, &oz) != 0) Fail("strip point around: found past the bound");
}

/* T-642: a counter's floor from the deleted marks of this game's own block - a deleted group's number is never handed out again
   (src/common/numberblock.h OwnMarkFloor) */
void t642_deleted_mark_floor()
{
    using namespace numblock;
    std::vector<char> b(65536u / 8u * 2u, 0);   /* blocks 0 and 1 */
    b[0] = (char)(1u << 3);   /* number 3, block 0 */
    const unsigned n1 = 65536u + 700u; b[n1 >> 3] = (char)((unsigned char)b[n1 >> 3] | (1u << (n1 & 7u)));   /* number 65,536 + 700, block 1 */
    if (OwnMarkFloor(&b[0], b.size(), 1, 0u) != n1 + 1u) Fail("mark floor: not one past the highest own-block mark");
    if (OwnMarkFloor(&b[0], b.size(), 0, 0u) != 4u) Fail("mark floor: block 0's highest mark");
    if (OwnMarkFloor(&b[0], b.size(), 1, 70000u) != 70000u) Fail("mark floor: lowered below the floor already held");
    if (OwnMarkFloor(&b[0], b.size(), 2, 0u) != 0u) Fail("mark floor: another block's mark raises");
    if (OwnMarkFloor(&b[0], b.size(), -1, 5u) != 5u || OwnMarkFloor(&b[0], b.size(), 16, 5u) != 5u) Fail("mark floor: no slot raises");
    if (OwnMarkFloor(&b[0], 10u, 1, 0u) != 0u) Fail("mark floor: read past the bitmap's end");
    int cap = -1;
    if (OwnMarkFloor(&b[0], b.size(), 1, 0u, &cap) != n1 + 1u || cap != 0) Fail("mark floor: a cap reported below the block's last number");
    std::vector<char> t(65536u / 8u, 0); t[t.size() - 1] = (char)0x80;   /* the block's last number */
    if (OwnMarkFloor(&t[0], t.size(), 0, 0u, &cap) != 65535u || cap != 1) Fail("mark floor: a mark on the block's last number put the floor in the next game's block, or the cap was not reported (T-642 review 4)");
}
/* T-642: a group whose 'another game writes this' mark comes off is marked changed (src/common/hbmath.h WriterLiftMarks), and a
   faction made after the block was applied - a stand-in, its counter at 0 - takes this game's block (src/common/numberblock.h
   BlockForNewFaction, CounterForBlock) */
void t642q_writer_lift_and_new_faction_block()
{
    if (coophb::WriterLiftMarks(1, 0) != 1) Fail("lift: a group this game now writes is not marked changed");
    if (coophb::WriterLiftMarks(0, 0) != 0) Fail("lift: a group that stays this game's is marked");
    if (coophb::WriterLiftMarks(1, 1) != 0) Fail("lift: a group another game still writes is marked");
    if (coophb::WriterLiftMarks(0, 1) != 0) Fail("lift: a group dropped to another game is marked");
    using namespace numblock;
    if (BlockForNewFaction(0, 1) != -1) Fail("new faction: a block taken before the block was applied (NumberBlockTick sets it then)");
    if (BlockForNewFaction(1, -1) != -1) Fail("new faction: a block taken with no slot");
    if (BlockForNewFaction(1, 16) != -1) Fail("new faction: a block taken past the 16-block cap");
    if (BlockForNewFaction(1, 2) != 2 || BlockForNewFaction(1, 0) != 0) Fail("new faction: the applied block not taken");
    int how = -1;
    if (CounterForBlock(0u, BlockForNewFaction(1, 2), 0, 0u, 0u, &how) != 131072u || how != kRaised) Fail("new faction: a stand-in's counter at 0 not raised into this game's block");
    if (CounterForBlock(0u, BlockForNewFaction(1, 0), 0, 0u, 0u, &how) != 0u || how != kKept) Fail("new faction: a counter at 0 moved in block 0");
    if (CounterForBlock(131080u, BlockForNewFaction(1, 2), 0, 0u, 0u, &how) != 131080u || how != kKept) Fail("new faction: a counter already in this game's block moved");
}
/* T-642: the block applied from the relay's slot or the remembered one, and a counter in another game's block moved down - never below
   a number this world may have handed out (src/common/numberblock.h SlotToApply, CounterForBlock) */
void t642_other_block_counter_moved()
{
    using namespace numblock;
    if (SlotToApply(1, 2, 1, 1) != 2 || SlotToApply(1, 2, 1, 0) != 2) Fail("slot: the relay's slot not used");
    if (SlotToApply(0, -1, 1, 1) != 1 || SlotToApply(1, -1, 1, 1) != 1) Fail("slot: the remembered slot not used without the relay's");
    if (SlotToApply(0, -1, 1, 0) != -1 || SlotToApply(1, -1, 1, 0) != -1) Fail("slot: a slot remembered from another world or relay applied (T-642 review 1)");
    if (SlotToApply(0, -1, -1, 1) != -1) Fail("slot: a slot guessed");
    int how = -1;
    if (CounterForBlock(65600u, 1, 1, 300u, 0u, &how) != 65600u || how != kKept) Fail("block: a counter inside its block moved");
    if (CounterForBlock(300u, 1, 1, 300u, 0u, &how) != 65536u || how != kRaised) Fail("block: not raised to the floor");
    if (CounterForBlock(300u, 1, 1, 300u, 65900u, &how) != 65900u || how != kRaised) Fail("block: raised below a number handed out there earlier");
    if (CounterForBlock(65536u, 0, 1, 300u, 0u, &how) != 300u || how != kMovedDown) Fail("block: not moved down to the load floor");
    if (CounterForBlock(131072u, 1, 1, 300u, 0u, &how) != 65536u || how != kMovedDown) Fail("block: not moved down to the block's floor");
    if (CounterForBlock(131072u, 1, 1, 300u, 65700u, &how) != 65700u || how != kMovedDown) Fail("block: moved below a number handed out there earlier");
    if (CounterForBlock(131072u, 1, 1, 65800u, 65700u, &how) != 65800u || how != kMovedDown) Fail("block: moved below the load floor");
    if (CounterForBlock(65536u + 64u, 0, 1, 300u, 0u, &how) != 65600u || how != kHeld) Fail("block: moved down out of a block it has handed out numbers in (T-642 review 2)");
    if (CounterForBlock(131072u + 1u, 1, 1, 300u, 65700u, &how) != 131073u || how != kHeld) Fail("block: moved down after handing out one number in the block it leaves");
    if (CounterForBlock(65536u, 0, 1, 65536u, 0u, &how) != 65536u || how != kHeld) Fail("block: moved below the save's own squads (the load left it above the block)");
    if (CounterForBlock(65536u, 0, 0, 0u, 0u, &how) != 65536u || how != kHeld) Fail("block: moved down with no load floor");
    if (CounterForBlock(5u, 16, 1, 0u, 0u, &how) != 5u || how != kKept || CounterForBlock(5u, -1, 1, 0u, 0u, &how) != 5u) Fail("block: a slot past the cap applied");
}
/* T-642: the load floor is kept under a faction's stringID - a faction renamed after the load keeps its floor (squadorigin.h FloorKey,
   FloorLookupKey) */
void t642_renamed_faction_keeps_floor()
{
    using namespace sqorigin;
    std::map<std::string, unsigned> floor; floor[FloorKey("10-gamedata.base", "Holy Nation")] = 1200u;
    const std::string after = FloorKey("10-gamedata.base", "Holy Sanctum");   /* the same faction, renamed */
    const std::string k = FloorLookupKey(1, after, 0, std::string());
    if (floor.find(k) == floor.end() || floor.find(k)->second != 1200u) Fail("floor key: a renamed faction lost its floor");
    if (FloorKey("", "My Crew") != "@My Crew" || FloorKey("@slot:1", "My Crew") != "@My Crew") Fail("floor key: an unusable stringID does not fall back to the name");
    if (FloorKey("", "10-gamedata.base") == FloorKey("10-gamedata.base", "x")) Fail("floor key: a name key equals a stringID key");
    if (FloorLookupKey(0, std::string(), 1, "10-gamedata.base") != "10-gamedata.base" || !FloorLookupKey(0, std::string(), 0, "x").empty()) Fail("floor key: the name index not used, or used when absent");
}
/* T-642: a player's faction showing an NPC faction's name keeps its own floor, and the shown-name index names neither (squadorigin.h
   FloorKey, NameIndexMerge) */
void t642_npc_name_clash()
{
    using namespace sqorigin;
    const std::string npc = FloorKey("17-gamedata.base", "Holy Nation"), player = FloorKey("204-gamedata.base", "Holy Nation");
    if (npc == player) Fail("name clash: a player's faction shares an NPC faction's floor key");
    std::map<std::string, unsigned> floor; floor[npc] = 900u; floor[player] = 3u;
    if (floor.find(FloorLookupKey(1, player, 0, std::string()))->second != 3u || floor.find(FloorLookupKey(1, npc, 0, std::string()))->second != 900u) Fail("name clash: a faction reads the other's floor");
    std::string idx = NameIndexMerge(0, std::string(), npc);
    if (idx != npc) Fail("name index: a name's first faction not kept");
    if (NameIndexMerge(1, idx, npc) != npc) Fail("name index: the same key twice made ambiguous");
    idx = NameIndexMerge(1, idx, player);
    if (!idx.empty()) Fail("name index: a name two keys show names one of them");
    if (!NameIndexMerge(1, idx, npc).empty()) Fail("name index: an ambiguous name named again");
}
/* a squad's origin slot: its number's block at or above the load floor; below it, the game that runs it now; the number's block when
   the floor or the runner is not known (src/common/squadorigin.h) */
void t850_squad_origin()
{
    using namespace sqorigin;
    int why = -1;
    /* at or above the floor: the number's block, whoever runs it */
    if (SquadOrigin(65540u, 1, 1200u, 1, 0, kByMembers, &why) != 1 || why != kByFloor) Fail("origin: above the floor is not the block");
    if (SquadOrigin(1200u, 1, 1200u, 0, 1, kByHolder, &why) != 0 || why != kByFloor) Fail("origin: at the floor is not the block");
    /* below the floor: the runner, by each source */
    if (SquadOrigin(5u, 1, 1200u, 0, 1, kByHandover, &why) != 1 || why != kByHandover) Fail("origin: below the floor, handed over - not the runner");
    if (SquadOrigin(5u, 1, 1200u, 0, 1, kByMembers, &why) != 1 || why != kByMembers) Fail("origin: below the floor, by members - not the runner");
    if (SquadOrigin(5u, 1, 1200u, 0, 2, kByHolder, &why) != 2 || why != kByHolder) Fail("origin: below the floor, by the area holder - not the runner");
    if (SquadOrigin(5u, 1, 1200u, 0, 1, kByRecord, &why) != 1 || why != kByRecord) Fail("origin: below the floor, by the record writer - not the runner");
    if (SquadOrigin(5u, 1, 1200u, 0, 0, kByMembers, &why) != 0 || why != kByMembers) Fail("origin: below the floor, run here - not this slot");
    /* the number's block when nothing better is known */
    if (SquadOrigin(5u, 0, 0u, 0, 1, kByMembers, &why) != 0 || why != kFallback) Fail("origin: no floor is not the block");
    if (SquadOrigin(5u, 1, 1200u, 0, -1, kFallback, &why) != 0 || why != kFallback) Fail("origin: no runner is not the block");
    if (SquadOrigin(5u, 1, 1200u, 0, 16, kByHolder, &why) != 0 || why != kFallback) Fail("origin: a runner past the slot cap is used");
    if (SquadOrigin(5u, 1, 1200u, 0, -2, kByHolder, &why) != 0 || why != kFallback) Fail("origin: a frozen area answer is used as a runner");
    if (SquadOrigin(0u, 1, 1200u, -1, 1, kByMembers, &why) != -1 || why != kFallback) Fail("origin: an id with no number has an answer");
    /* the runner sources in order */
    if (RunBy(2, 1, 0, 3, &why) != 2 || why != kByHandover) Fail("runner: the hand-over does not come first");
    if (RunBy(-1, 1, 0, 3, &why) != 1 || why != kByMembers) Fail("runner: the members do not come second");
    if (RunBy(-1, -1, 0, 3, &why) != 0 || why != kByHolder) Fail("runner: the area holder does not come third");
    if (RunBy(-1, -1, -2, 3, &why) != 3 || why != kByRecord) Fail("runner: a frozen area is not passed to the record writer");
    if (RunBy(-1, -1, -1, -1, &why) != -1 || why != kFallback) Fail("runner: nothing known has a runner");
    /* the members: a strict majority of those whose runner is known */
    { int c[kMaxSlots] = { 3, 1 }; if (MembersMajority(c, kMaxSlots) != 0) Fail("members: 3 of 4 is not a majority"); }
    { int c[kMaxSlots] = { 1, 1 }; if (MembersMajority(c, kMaxSlots) != -1) Fail("members: a tie has a runner"); }
    { int c[kMaxSlots] = { 0, 2 }; if (MembersMajority(c, kMaxSlots) != 1) Fail("members: all of another game's is not that game"); }
    { int c[kMaxSlots] = { 2, 1, 1 }; if (MembersMajority(c, kMaxSlots) != -1) Fail("members: 2 of 4 is a majority"); }
    { int c[kMaxSlots] = { 0 }; if (MembersMajority(c, kMaxSlots) != -1) Fail("members: none known has a runner"); }
    /* a faction name several factions share: the lowest counter */
    if (FloorMerge(0, 0u, 50u) != 50u || FloorMerge(1, 50u, 30u) != 30u || FloorMerge(1, 30u, 50u) != 30u) Fail("floor: a shared name does not keep the lowest counter");
    /* the origin line: the first 40, then every 50th */
    if (OriginLineDue(1) != 1 || OriginLineDue(40) != 1 || OriginLineDue(41) != 0 || OriginLineDue(50) != 1 || OriginLineDue(99) != 0 || OriginLineDue(100) != 1) Fail("origin line cap");
    if (WhyWord(kByHolder) == 0 || WhyWord(kFallback) == 0) Fail("origin: a source has no word");
    /* the same-id supersede on the world loads' ages: (why, this game's copy all its current load's pickups, this game's load age ms, the
       announcer's, this game's seat, the announcer's seat) */
    if (OwnIdLiftedByLoadAge(kByMembers, 1, 30000u, 3600000u, 1, 0) != kOwnIdLifted || OwnIdLiftedByLoadAge(kByMembers, 1, 30000u, 3600000u, 0, 1) != kOwnIdLifted) Fail("supersede: a copy from the younger load is not lifted");
    if (OwnIdLiftedByLoadAge(kByMembers, 0, 30000u, 3600000u, 1, 0) != kOwnIdKept) Fail("supersede: a copy holding a person from the session or an earlier load is lifted");
    if (OwnIdLiftedByLoadAge(kByFloor, 1, 30000u, 3600000u, 1, 0) != kOwnIdKept || OwnIdLiftedByLoadAge(kFallback, 1, 30000u, 3600000u, 1, 0) != kOwnIdKept) Fail("supersede: the number rule's refusal is lifted (the stale-reload guard)");
    if (OwnIdLiftedByLoadAge(kByHolder, 1, 30000u, 3600000u, 1, 0) != kOwnIdKept || OwnIdLiftedByLoadAge(kByHandover, 1, 30000u, 3600000u, 1, 0) != kOwnIdKept || OwnIdLiftedByLoadAge(kByRecord, 1, 30000u, 3600000u, 1, 0) != kOwnIdKept) Fail("supersede: a source other than the copy's own members is lifted");
    if (OwnIdLiftedByLoadAge(kByMembers, 1, 3600000u, 30000u, 1, 0) != kOwnIdKept) Fail("supersede: the older load's copy is lifted");
    if (OwnIdLiftedByLoadAge(kByMembers, 1, 60000u, 62000u, 1, 0) != kOwnIdLifted || OwnIdLiftedByLoadAge(kByMembers, 1, 62000u, 60000u, 0, 1) != kOwnIdKeptLowerSeat) Fail("supersede: within the tie the higher seat's copy does not go");
    if (OwnIdLiftedByLoadAge(kByMembers, 1, 30000u, 3600000u, -1, 0) != kOwnIdKept || OwnIdLiftedByLoadAge(kByMembers, 1, 30000u, 3600000u, 1, -1) != kOwnIdKept) Fail("supersede: a seat unknown lifts the refusal");
    if (OwnIdLiftedByLoadAge(kByMembers, 1, kLoadAgeUnknown, 3600000u, 1, 0) != kOwnIdKept || OwnIdLiftedByLoadAge(kByMembers, 1, 30000u, kLoadAgeUnknown, 1, 0) != kOwnIdKept) Fail("supersede: an unknown load age lifts the refusal");
    /* the sleep write's record source: a fresh copy is not decided by another game's record */
    if (SleepRecordSource(0, 1, 0) != kRecordNewest) Fail("sleep: a stale copy does not read the newest record");
    if (SleepRecordSource(1, 1, 0) != kRecordNone) Fail("sleep: a fresh copy is decided by another game's record");
    if (SleepRecordSource(1, 0, 0) != 0) Fail("sleep: a fresh copy loses this game's own record");
    if (SleepRecordSource(1, -1, 0) != kRecordNone || SleepRecordSource(1, 0, -1) != kRecordNone) Fail("sleep: no record / no slot names a writer");
}

/* T-850 (decision 826 a): two games answer each other's CONTEXT for one save squad. own: that game's copy is all its current load's
   sweep pickups; age: ms since that game's current world load finished. bit 0 = game A's own copy survives, bit 1 = game B's */
int t850_survivors(int aOwn, unsigned int aAge, int aSeat, int bOwn, unsigned int bAge, int bSeat)
{
    using namespace sqorigin;
    const int aLift = OwnIdLiftedByLoadAge(kByMembers, aOwn, aAge, bAge, aSeat, bSeat) == kOwnIdLifted ? 1 : 0;
    const int bLift = OwnIdLiftedByLoadAge(kByMembers, bOwn, bAge, aAge, bSeat, aSeat) == kOwnIdLifted ? 1 : 0;
    return (aLift != 0 ? 0 : 1) | (bLift != 0 ? 0 : 2);
}
/* the member half (spawn.cpp ReloadVerdict with the load test dropped): every session member this load's sweep pickup, slot known */
int t850_this_load(int thisGen, int earlier, int other) { return coopsp::ReloadRetireDecide(1, 1, 1, thisGen, earlier, other) == coopsp::kReloadRetire ? 1 : 0; }
void t850_load_age_lift()
{
    using namespace sqorigin;
    const unsigned int U = kLoadAgeUnknown, T = kLoadAgeTieMs;
    /* the lift, exhaustively over (why, own copy this load's, own age, announcer's age, my seat, announcer's seat) */
    const unsigned int ages[] = { 0u, 1000u, 1000u + T, 1001u + T, 7200000u, U - 1u, U };
    for (int w = 0; w <= kFallback; ++w) for (int o = 0; o < 2; ++o) for (int i = 0; i < 7; ++i) for (int j = 0; j < 7; ++j)
        for (int ms = -1; ms <= 2; ++ms) for (int ps = -1; ps <= 2; ++ps)
        {
            const unsigned int a = ages[i], b = ages[j];
            const int got = OwnIdLiftedByLoadAge(w, o, a, b, ms, ps);
            const int ok = (w == kByMembers && o == 1 && a != U && b != U && ms >= 0 && ps >= 0) ? 1 : 0;
            const int tie = (a > b ? a - b : b - a) <= T ? 1 : 0;
            const int lift = (ok == 1 && ((b > a && b - a > T) || (tie == 1 && ms > ps))) ? 1 : 0;
            const int lower = (ok == 1 && tie == 1 && ms < ps) ? 1 : 0;
            if ((got == kOwnIdLifted) != (lift == 1)) Fail("lift: lifted exactly when its own members decided it, own copy this load's, ages and seats known, and its load younger by more than the tie or within the tie and the higher seat");
            if ((got == kOwnIdKeptLowerSeat) != (lower == 1)) Fail("lift: withheld for the lower seat exactly when within the tie and this seat is lower");
        }
    /* the member half: retire-eligible only when every session member is this load's sweep pickup */
    if (t850_this_load(4, 0, 0) != 1 || t850_this_load(4, 1, 0) != 0 || t850_this_load(4, 0, 1) != 0 || t850_this_load(0, 0, 0) != 0) Fail("member half: this-load copy judged wrongly");
    /* (i) a fresh session start: both games load the save within seconds; exactly one copy, the lower seat's */
    if (t850_survivors(1, 40000u, 0, 1, 37000u, 1) != 1 || t850_survivors(1, 37000u, 0, 1, 40000u, 1) != 1) Fail("(i): not exactly the lower seat's copy survives");
    /* (ii) one game reloads in game while the other runs on: the reloaded copy goes, either seat */
    if (t850_survivors(1, 3600000u, 0, 1, 30000u, 1) != 1 || t850_survivors(1, 30000u, 0, 1, 3600000u, 1) != 2) Fail("(ii): the running game's copy is not the one survivor");
    /* (iii) both reload in game: the later loader's copy goes; within the tie the higher seat's */
    if (t850_survivors(1, 90000u, 0, 1, 20000u, 1) != 1 || t850_survivors(1, 20000u, 0, 1, 90000u, 1) != 2) Fail("(iii): the later loader's copy did not go");
    if (t850_survivors(1, 20000u, 0, 1, 23000u, 1) != 1 || t850_survivors(1, 23000u, 1, 1, 20000u, 0) != 2) Fail("(iii): within the tie, not the lower seat's copy survives");
    /* (iv) one game crashes, restarts and rejoins: its load is the youngest, its copy goes, either seat */
    if (t850_survivors(1, 45000u, 0, 1, 7200000u, 1) != 2 || t850_survivors(1, 7200000u, 0, 1, 45000u, 1) != 1) Fail("(iv): the restarted game's copy did not go");
    /* (v) neither reloaded: a copy holding people from the session (SPAWN / XFER) or from an earlier load is never lifted - both kept as before */
    {
        const int a = t850_this_load(3, 0, 2), b = t850_this_load(3, 1, 0);
        if (a != 0 || b != 0) Fail("(v): a copy holding a person from elsewhere reads as this load's");
        if (t850_survivors(a, 7200000u, 0, b, 7100000u, 1) != 3 || t850_survivors(a, 7100000u, 1, b, 7200000u, 0) != 3) Fail("(v): a game that did not reload lost its copy");
    }
    /* never a vanish: with both seats known and different, at least one copy survives whatever the ages and member halves */
    for (int ao = 0; ao < 2; ++ao) for (int bo = 0; bo < 2; ++bo) for (int i = 0; i < 7; ++i) for (int j = 0; j < 7; ++j)
        if (t850_survivors(ao, ages[i], 0, bo, ages[j], 1) == 0 || t850_survivors(ao, ages[i], 1, bo, ages[j], 0) == 0) Fail("lift: both copies retired");
    /* an unknown age or seat never lifts */
    if (t850_survivors(1, U, 0, 1, 30000u, 1) != 3 || t850_survivors(1, 30000u, 1, 1, 3600000u, -1) != 3) Fail("lift: an unknown age or seat lifted");
    /* the announcer's age brought forward to the moment of the decision */
    if (LoadAgeAdvance(U, 5u) != U || LoadAgeAdvance(1000u, 500u) != 1500u || LoadAgeAdvance(U - 10u, 100u) != U - 1u || LoadAgeAdvance(0u, U) != U - 1u) Fail("age: brought forward wrongly");
}
/* T-876: the heartbeat registry's writer, re-judged from who runs the squad's living people */
void t876_hb_writer()
{
    using namespace coopsquad;
    { int c[4] = { 0, 3, 0, 0 }; if (HbRunnerOf(5, c, 4) != 1) Fail("runner: 3 of 5 living is not a majority"); }
    { int c[4] = { 0, 3, 0, 0 }; if (HbRunnerOf(6, c, 4) != -1) Fail("runner: exactly half is a majority"); }
    { int c[4] = { 2, 2, 0, 0 }; if (HbRunnerOf(5, c, 4) != -1) Fail("runner: a split has a runner"); }
    { int c[4] = { 0, 4, 0, 0 }; if (HbRunnerOf(10, c, 4) != -1) Fail("runner: members with a session uid whose runner is unknown do not count against the majority"); }
    { int c[4] = { 0, 0, 0, 0 }; if (HbRunnerOf(0, c, 4) != -1 || HbRunnerOf(3, c, 4) != -1 || HbRunnerOf(3, 0, 4) != -1) Fail("runner: none known has a runner"); }
    /* a context platoon filed foreign whose people this game (slot 1) now runs - lifted at the next visit */
    if (HbForeignAfter(0, 1, 1, 1, 0, 0) != 0) Fail("writer: a squad this game runs stays foreign");
    if (HbForeignAfter(0, 0, 1, 0, 0, 1) != 1) Fail("writer: a squad another game runs is not dropped to foreign");
    if (HbForeignAfter(0, 1, 1, -1, 0, 0) != 1 || HbForeignAfter(0, 0, 1, -1, 1, 0) != 0) Fail("writer: an unknown runner changed the mark");
    if (HbForeignAfter(0, 1, 1, -1, 0, 1) != 0) Fail("writer: the area holder's tie-break did not lift");
    if (HbForeignAfter(1, 0, 1, 1, 1, 0) != 0 || HbForeignAfter(1, 0, 1, 0, 0, 1) != 1) Fail("filing: the runner does not decide");
    if (HbForeignAfter(1, 0, 1, -1, 1, 0) != 1 || HbForeignAfter(1, 0, 1, -1, 1, 1) != 0 || HbForeignAfter(1, 0, 1, -1, 0, 0) != 0) Fail("filing: an unknown runner is not decided by the holder, then the origin");
    if (HbForeignAfter(1, 1, -1, 0, 1, 0) != 0 || HbForeignAfter(0, 1, -1, 1, 0, 0) != 1 || HbForeignAfter(0, 0, -1, 0, 0, 0) != 0) Fail("writer: no slot of this game's own changed the mark");
    for (int f = 0; f < 2; ++f) for (int cur = 0; cur < 2; ++cur) for (int ms = -1; ms <= 2; ++ms) for (int r = -1; r <= 2; ++r) for (int bo = 0; bo < 2; ++bo) for (int h = 0; h < 2; ++h)
    {
        const int was = (f == 0 && cur != 0) ? 1 : 0;
        const int want = ms < 0 ? was : (r >= 0 ? (r == ms ? 0 : 1) : (h == 1 ? 0 : (f != 0 ? bo : was)));
        if (HbForeignAfter(f, cur, ms, r, bo, h) != want) Fail("writer: the mark is not the runner's, then the holder's, then the origin's at filing, else unchanged");
    }
    /* the stamp: this game (slot 1) is the writer by the runner rule while the newest record is slot 0's and newer than its copy */
    if (HbStampRunner(0, 1, 1, 0, 1, 0, 200, 1, 100) != 1 || HbStampRunner(0, 1, 1, 0, 1, 0, 200, 0, 0) != 1) Fail("stamp: the runner rule's writer left a stale copy refused");
    if (HbStampRunner(0, 1, 1, 0, 1, 0, 200, 1, 200) != 0 || HbStampRunner(0, 1, 1, 0, 1, 1, 300, 1, 100) != 0 || HbStampRunner(0, 1, 1, 0, 0, -1, 0, 0, 0) != 0)
        Fail("stamp: a copy NpcCopyFresh already passes was stamped");
    if (HbStampRunner(0, -1, 1, 0, 1, 0, 200, 1, 100) != 0) Fail("stamp: the area holder's tie-break stamped");
    if (HbStampRunner(1, 1, 1, 0, 1, 0, 200, 1, 100) != 0) Fail("stamp: a foreign entry stamped");
    if (HbStampRunner(0, 1, 1, 1, 1, 0, 200, 0, 0) != 0) Fail("stamp: a copy whose stamp the orphan purge forgot was stamped");
    if (HbStampRunner(0, 1, -1, 0, 1, 0, 200, 1, 100) != 0 || HbStampRunner(0, 2, 1, 0, 1, 0, 200, 1, 100) != 0) Fail("stamp: no slot of this game's own, or another runner, stamped");
    /* a late record from the old writer after the stamp: the next visit stamps again, with no change of writer */
    {
        long long stamp = 100; const long long rec1 = 200, rec2 = 250;
        if (HbStampRunner(0, 1, 1, 0, 1, 0, rec1, 1, stamp) == 1) stamp = rec1;
        if (NpcCopyFresh(1, 1, 0, rec2, 1, stamp) != 0) Fail("stamp: the late record did not make the copy stale (premise)");
        if (HbStampRunner(0, 1, 1, 0, 1, 0, rec2, 1, stamp) != 1) Fail("stamp: a late record from the old writer left this game's copy refused");
    }
    for (int f = 0; f < 2; ++f) for (int w = 0; w < 2; ++w) for (int r = -1; r <= 2; ++r) for (int e = 0; e < 2; ++e)
    {
        const int now = HbForeignAfter(f, w, 1, r, 1, 0);
        const long long rec = 200, stamp = 100;
        const int st = HbStampRunner(now, r, 1, e, 1, 0, rec, 1, stamp);
        const int want = (now == 0 && r == 1 && e == 0) ? 1 : 0;
        if (st != want) Fail("stamp: not exactly the runner rule's writer with a stamp of its own");
        if (st == 1 && NpcCopyFresh(1, 1, 0, rec, 1, rec) != 1) Fail("stamp: the stamped copy is still refused as stale");
    }
}
/* T-871: limbtest fit's level=<n> (0..kLimbTestLevelMax, plain digits, fit only); absent = -1. */
void t_t871_limbtest_level()
{
    unsigned int uid = 0; int limb = -1; std::string sid; int level = 7;
    if (cooplimb::LimbTestParse("fit 77 3 95747-Newwworld.mod level=85", &uid, &limb, &sid, &level) != cooplimb::kLimbTestFit
        || uid != 77 || limb != 3 || sid != "95747-Newwworld.mod" || level != 85)
        Fail("limbtest fit with level=85");
    level = 7;
    if (cooplimb::LimbTestParse("fit 77 3 95747-Newwworld.mod", &uid, &limb, &sid, &level) != cooplimb::kLimbTestFit || level != -1)
        Fail("limbtest fit without level= must give -1");
    level = 7;
    if (cooplimb::LimbTestParse("fit 77 1 x level=0", &uid, &limb, &sid, &level) != cooplimb::kLimbTestFit || level != 0)
        Fail("limbtest fit level=0");
    if (cooplimb::LimbTestParse("fit 77 1 x level=1000", &uid, &limb, &sid, &level) != cooplimb::kLimbTestFit || level != 1000)
        Fail("limbtest fit level=1000");
    if (cooplimb::LimbTestParse("fit 77 1 x level=0085", &uid, &limb, &sid, &level) != cooplimb::kLimbTestFit || level != 85)
        Fail("limbtest fit level=0085");
    level = 7;
    if (cooplimb::LimbTestParse("cut 77 2", &uid, &limb, &sid, &level) != cooplimb::kLimbTestCut || level != -1)
        Fail("limbtest cut gives level -1");
    if (cooplimb::LimbTestParse("fit 77 0 x level=5", &uid, &limb, &sid) != cooplimb::kLimbTestFit) Fail("level pointer may be absent");
    const char* bad[] = { "fit 5 1 x level=", "fit 5 1 x level=-1", "fit 5 1 x level=1001", "fit 5 1 x level=99999999999",
                          "fit 5 1 x level=8x", "fit 5 1 x level=+8", "fit 5 1 x lvl=8", "fit 5 1 x LEVEL=8", "fit 5 1 level=8",
                          "cut 5 1 level=3", "show 5 level=3", "fit 5 1 x level=8 extra", "fit 5 1 x level=8 level=9" };
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); ++k)
        if (cooplimb::LimbTestParse(bad[k], &uid, &limb, &sid, &level) != cooplimb::kLimbTestBad)
            Fail(std::string("limbtest must refuse '") + bad[k] + "'");
}

/* T-871: limb block version 2 carries each fitted limb's level; version 1 still reads (levels -1); an implausible level reads
   -1; a same-item level difference refits once both levels are known. */
void t_t871_limb_level_wire()
{
    cooplimb::LimbsWire w, r;
    cooplimb::LimbsClear(&w);
    if (w.level[0] != -1 || w.level[3] != -1) Fail("LimbsClear must set every level to -1");
    w.state[1] = cooplimb::kLimbReplaced; cooplimb::LimbSetSid(&w, 1, "abc"); w.quality[1] = 0.5f; w.level[1] = 85;
    w.state[2] = cooplimb::kLimbReplaced; cooplimb::LimbSetSid(&w, 2, "de"); w.quality[2] = 0.25f; w.level[2] = 0;
    w.state[0] = cooplimb::kLimbStump; w.level[0] = 40;   /* not fitted: not on the wire */
    std::vector<char> b;
    cooplimb::EncodeLimbs(&b, w);
    if (b.empty() || (unsigned char)b[0] != 2) Fail("the block this build writes must be version 2");
    if (b.size() != 5 + (1 + 3 + 4 + 4) + (1 + 2 + 4 + 4)) Fail("version 2: a fitted limb is sidLen + sid + quality + level");
    size_t end = 0;
    if (!cooplimb::DecodeLimbs(&b[0], b.size(), 0, &r, &end) || end != b.size()) { Fail("version 2 must decode"); return; }
    if (r.level[1] != 85 || r.level[2] != 0 || r.level[0] != -1 || r.level[3] != -1 || r.quality[1] != 0.5f
        || std::strcmp(r.sid[2], "de") != 0)
        Fail("version 2 levels / quality / sid came back wrong");
    if (cooplimb::DecodeLimbs(&b[0], b.size() - 1, 0, &r, 0)) Fail("a version 2 block cut inside a level must be refused");
    /* an implausible level is written and read as -1 */
    cooplimb::LimbsWire h(w);
    h.level[1] = cooplimb::kLimbLevelMax + 1;
    std::vector<char> hb;
    cooplimb::EncodeLimbs(&hb, h);
    if (!cooplimb::DecodeLimbs(&hb[0], hb.size(), 0, &r, 0) || r.level[1] != -1) Fail("a level over the cap must travel as -1");
    std::vector<char> hostile(b);
    const int neg = -7, big = 2000000;
    std::memcpy(&hostile[5 + 1 + 3 + 4], &neg, 4);
    std::memcpy(&hostile[5 + 12 + 1 + 2 + 4], &big, 4);
    if (!cooplimb::DecodeLimbs(&hostile[0], hostile.size(), 0, &r, 0) || r.level[1] != -1 || r.level[2] != -1)
        Fail("a negative or huge level on the wire must read -1, the block kept");
    /* version 1: no level bytes; every level -1 */
    std::vector<char> v1;
    v1.push_back((char)1); v1.push_back((char)0); v1.push_back((char)2); v1.push_back((char)0); v1.push_back((char)0);
    v1.push_back((char)3); v1.push_back('a'); v1.push_back('b'); v1.push_back('c');
    const float q1 = 0.75f;
    const size_t at = v1.size();
    v1.resize(at + 4);
    std::memcpy(&v1[at], &q1, 4);
    if (!cooplimb::DecodeLimbs(&v1[0], v1.size(), 0, &r, &end) || end != v1.size()) { Fail("version 1 must still decode"); return; }
    if (r.state[1] != cooplimb::kLimbReplaced || std::strcmp(r.sid[1], "abc") != 0 || r.quality[1] != 0.75f || r.level[1] != -1)
        Fail("version 1: sid / quality kept, level -1");
    if (cooplimb::DecodeLimbs(&v1[0], v1.size() - 1, 0, &r, 0)) Fail("a cut version 1 block must be refused");
    std::vector<char> v3(v1);
    v3[0] = (char)3;
    if (cooplimb::DecodeLimbs(&v3[0], v3.size(), 0, &r, 0)) Fail("version 3 must read as no block");
    /* LimbsEqual sees a level difference on a fitted limb only */
    cooplimb::LimbsWire e1(w), e2(w);
    e2.level[1] = 86;
    if (cooplimb::LimbsEqual(e1, e2)) Fail("LimbsEqual must see a fitted limb's level");
    e2 = w; e2.level[0] = 1;
    if (!cooplimb::LimbsEqual(e1, e2)) Fail("LimbsEqual must ignore a level on a limb that is not fitted");
    /* the plan */
    if (cooplimb::PlanLimbQL(2, "x", 0.5f, 10, 2, "x", 0.5f, 85) != cooplimb::kLimbStepRefit) Fail("same item, other level must refit");
    if (cooplimb::PlanLimbQL(2, "x", 0.5f, 85, 2, "x", 0.5f, 85) != cooplimb::kLimbStepNone) Fail("same item, same level is none");
    if (cooplimb::PlanLimbQL(2, "x", 0.5f, 10, 2, "x", 0.5f, -1) != cooplimb::kLimbStepNone) Fail("an unknown owner level never refits");
    if (cooplimb::PlanLimbQL(2, "x", 0.5f, -1, 2, "x", 0.5f, 85) != cooplimb::kLimbStepNone) Fail("an unreadable copy level never refits");
    if (cooplimb::PlanLimbQL(2, "x", 0.5f, 10, 2, "", 0.5f, 85) != cooplimb::kLimbStepNone) Fail("an unnamed owner item never refits on level");
    if (cooplimb::PlanLimbQL(2, "x", 0.5f, 85, 2, "x", 0.9f, 85) != cooplimb::kLimbStepRefit) Fail("PlanLimbQL must keep the quality refit");
    if (cooplimb::PlanLimbQL(1, "", -1.0f, -1, 2, "x", 0.5f, 85) != cooplimb::kLimbStepFit) Fail("PlanLimbQL must keep the fit step");
    if (cooplimb::PlanLimbQL(0, "", 0.0f, -1, 1, "", 0.0f, -1) != cooplimb::kLimbStepAmputate) Fail("PlanLimbQL must keep the amputate step");
}

/* ---- T-697: an area's ground items as one world-server record (src/common/groundrec.h): the id, the bytes both ways with a
   backpack, every truncation refused, the caps counted, the server's accept rule and its refusal message ---- */
coopgrec::GroundRow T697Row(const char* name, const char* sid, unsigned int qty, float x)
{
    coopgrec::GroundRow r;
    r.name = name; r.baseSid = sid; r.companySid = "maker"; r.materialSid = "steel"; r.colorSid = "";
    r.qty = qty; r.quality = 0.5f; r.charges = 3.0f; r.functionKind = 1; r.level = 2; r.unique = 1;
    r.pos[0] = x; r.pos[1] = 10.0f; r.pos[2] = -20.5f;
    r.rot[0] = 0.7071f; r.rot[1] = 0.0f; r.rot[2] = 0.7071f; r.rot[3] = 0.0f;
    return r;
}
bool T697RowSame(const coopgrec::GroundRow& a, const coopgrec::GroundRow& b)
{
    if (a.name != b.name || a.baseSid != b.baseSid || a.companySid != b.companySid || a.materialSid != b.materialSid || a.colorSid != b.colorSid) return false;
    if (a.qty != b.qty || a.quality != b.quality || a.charges != b.charges || a.functionKind != b.functionKind || a.level != b.level || a.unique != b.unique || a.made != b.made) return false;
    for (int i = 0; i < 3; ++i) if (a.pos[i] != b.pos[i]) return false;
    for (int i = 0; i < 4; ++i) if (a.rot[i] != b.rot[i]) return false;
    if (!coopmark::OwnerEqual(a.owner, b.owner) || !gearwire::GearEqual(a.gear, b.gear)) return false;
    if (a.bag.size() != b.bag.size() || coopbag::BagRowsDigest(a.bag) != coopbag::BagRowsDigest(b.bag)) return false;
    for (size_t i = 0; i < a.bag.size(); ++i)
        if (!coopmark::OwnerEqual(a.bag[i].owner, b.bag[i].owner) || !gearwire::GearEqual(a.bag[i].gear, b.bag[i].gear) || a.bag[i].parent != b.bag[i].parent) return false;
    return true;
}
/* ---- per-piece world-server records, fold 1: the server view of a game's own writes, the seed rule, build stopped ---- */
void t_t1045f1_piece_fold()
{
    using namespace piecerec;
    /* HIGH 1: after this game's seed over a removal mark is taken (RECORD_SEQ), the kept copy is still the removal mark (the server
       never sends a game its own writes back) - the next change is an update, never a second seed */
    PieceSrvView v;
    PieceTakenNote(&v, 5, 0);
    if (v.have != 1 || v.gen != 5 || v.removed != 0) Fail("a taken write is not the server view");
    if (PieceSeedDecide(1, 4, 1, v.have, v.gen, v.removed, 1) != 0) Fail("a change after this game's taken seed over a removal mark seeds again (the refusal loop)");
    if (PieceSeedDecide(1, 4, 1, 0, 0, 0, 1) != 1) Fail("a new placement over a kept removal mark is not a seed");
    if (PieceSeedDecide(0, 0, 0, 0, 0, 0, 0) != 1 || PieceSeedDecide(0, 0, 0, 0, 0, 0, 1) != 0) Fail("no record known: a seed, unless a write is on its way");
    if (PieceSeedDecide(1, 6, 0, 1, 5, 1, 1) != 0) Fail("an older server view overruled a newer kept copy");
    if (PieceSeedDecide(1, 4, 0, 1, 7, 1, 1) != 1) Fail("a newer removal mark in the server view does not make the next write a seed");
    PieceTakenNote(&v, 3, 1);
    if (v.gen != 5 || v.removed != 0) Fail("an older taken write overwrote the server view");
    {   /* a 'seed exists' refusal on a change teaches the live stored record: the next write is an update above it */
        PieceSrvView s;
        int hs = 1; unsigned int sent = 5;
        PieceRefuseLearn(kPrRefuseSeedExists, 6, &s);
        if (PieceRefuseNext(kPrRefuseSeedExists, 6, &hs, &sent) != kPwrRaise || sent != 6) Fail("a seed-exists refusal is not raised above the stored record");
        if (s.have != 1 || s.gen != 6 || s.removed != 0 || PieceSeedDecide(1, 4, 1, s.have, s.gen, s.removed, hs) != 0) Fail("a seed-exists refusal does not make the next write an update");
        unsigned int g = 0;
        if (PieceWriteGen(1, 6, hs, sent, &g) != 0 || g != 7) Fail("the update after a seed-exists refusal is not above the stored generation");
    }
    /* MED 2: an update refused over a removal mark is written again as a seed above the stored generation */
    {
        PieceSrvView s;
        int hs = 1; unsigned int sent = 5;
        PieceRefuseLearn(kPrRefuseOverRemoval, 9, &s);
        if (PieceRefuseNext(kPrRefuseOverRemoval, 9, &hs, &sent) != kPwrRaise || sent != 9) Fail("an over-removal refusal drops the piece's writes");
        if (s.have != 1 || s.gen != 9 || s.removed != 1 || PieceSeedDecide(1, 5, 0, s.have, s.gen, s.removed, hs) != 1) Fail("the write after an over-removal refusal is not a seed");
        unsigned int g = 0;
        if (PieceWriteGen(1, 9, hs, sent, &g) != 0 || g != 10) Fail("the seed after an over-removal refusal is not above the stored generation");
        PieceRefuseLearn(kPrRefuseOlder, 12, &s);
        if (s.gen != 12 || s.removed != 1) Fail("an older refusal forgot what is known of the stored record");
        PieceRefuseLearn(kPrRefuseNoRecord, 0, &s);
        if (s.have != 0) Fail("a no-record refusal leaves a stored record known");
    }
    /* MED 3: the first look in a world writes nothing when the kept record is the piece's own row */
    if (PieceFirstLookSkip(kPeFirst, 1, 0, 1, 1, 1) != 1) Fail("the first look at a piece equal to its stored record writes");
    if (PieceFirstLookSkip(kPeFirst, 1, 0, 1, 1, 0) != 0 || PieceFirstLookSkip(kPeFirst, 0, 0, 1, 1, 1) != 0 || PieceFirstLookSkip(kPeFirst, 1, 1, 1, 1, 1) != 0
        || PieceFirstLookSkip(kPeFirst, 1, 0, 0, 1, 1) != 0 || PieceFirstLookSkip(kPeFirst, 1, 0, 1, 0, 1) != 0 || PieceFirstLookSkip(kPeMats, 1, 0, 1, 1, 1) != 0)
        Fail("a first look skipped a write that differs, has no live own newest record, or a real change was skipped");
    /* the seed rule: the owner's game has the area loaded, or nobody holds it */
    {
        const std::string key = "12-wall.mod@3,41@1003,-205,100";
        const std::string id = MakePieceRecId(key);
        coopbuild::BuildMsg m, st;
        m.kind = coopbuild::kBuildPlace; m.key = key; m.sid = "12-wall.mod"; m.pos[0] = 1.0f; m.pos[1] = 2.0f; m.pos[2] = 3.0f;
        m.rot[0] = 1.0f; m.rot[1] = 0.0f; m.rot[2] = 0.0f; m.rot[3] = 0.0f; m.needed = 10.0f; m.ownerSlot = coopbuild::kBuildOwnerSender; m.nonce = 7;
        st.kind = coopbuild::kBuildState; st.key = key; st.needed = 10.0f;
        std::vector<char> pb, sb, rec;
        coopbuild::EncodeBuild(&pb, m); coopbuild::EncodeBuild(&sb, st);
        PieceHeader h; h.generation = 1; h.writerSlot = 1; h.ownerSlot = 1; h.key = key; h.placement = 7; h.flags = kPrFlagSeed;
        PieceBody b; b.place.assign(pb.begin(), pb.end()); b.state.assign(sb.begin(), sb.end());
        if (!EncodePieceRecord(&rec, h, b)) { Fail("the seed did not encode"); return; }
        std::vector<int> ownerHere(1, 1), nobody;
        if (PieceJudgeWrite(id, &rec[0], rec.size(), 1, -1, nobody, 0, 0, -1, 0, 0, 0) != kPrAccept) Fail("seed rule: nobody holds the area - the owner's seed was refused");
        if (PieceJudgeWrite(id, &rec[0], rec.size(), 1, 2, nobody, 0, 0, -1, 0, 0, 0) != kPrRefuseSeedNotLoaded) Fail("seed rule: another game holds the area and the owner is away - the seed was taken");
        if (PieceJudgeWrite(id, &rec[0], rec.size(), 1, 2, ownerHere, 0, 0, -1, 0, 0, 0) != kPrAccept) Fail("seed rule: the owner has the area loaded - the seed was refused");
        if (PieceJudgeWrite(id, &rec[0], rec.size(), 1, 1, nobody, 0, 0, -1, 0, 0, 0) != kPrAccept) Fail("seed rule: the owner holds the area - the seed was refused");
        if (PieceSeedPlaceOk(1, 2, 1, 0) != 0 || PieceSeedPlaceOk(1, -1, 1, 0) != 1 || PieceSeedPlaceOk(1, 2, 1, 1) != 1) Fail("PieceSeedPlaceOk");
        int hs = 1; unsigned int sent = 1;
        if (PieceRefuseNext(kPrRefuseSeedNotLoaded, 0, &hs, &sent) != kPwrRetry) Fail("a refused seed is not tried again");
        if (std::string(PieceRefuseWhy(kPrRefuseSeedNotLoaded)) == "refused") Fail("the seed refusal has no words");
    }
    /* LOW 5: tried again on a new area map only when this game reads itself the writer */
    if (PieceRetryDue(1, 1) != 1 || PieceRetryDue(1, 0) != 0 || PieceRetryDue(0, 1) != 0) Fail("PieceRetryDue");
    if (PieceOwnerWriterNow(1, 0, 3) != 1 || PieceOwnerWriterNow(0, 0, -1) != 0 || PieceOwnerWriterNow(0, 1, -1) != 1 || PieceOwnerWriterNow(0, 1, 2) != 0)
        Fail("PieceOwnerWriterNow: loaded here, or a refused seed while nobody holds the area");
    /* MED 4: build stopped - a piece whose progress moved since its last write, asked at the area's unload */
    if (PieceStoppedDue(1, 2.0f, 2.3f) != 1 || PieceStoppedDue(1, 2.0f, 2.0f) != 0 || PieceStoppedDue(0, 0.0f, 5.0f) != 0) Fail("PieceStoppedDue");
    if (PieceWriteDue(2.0f, 2.3f, 10.0f, kPeStopped) != 1) Fail("a stopped build is not a write");
    /* LOW 6: a materials or helper write mid-build leaves the build moving */
    if (PieceEventEndsMoving(kPeMats) != 0 || PieceEventEndsMoving(kPeHelp) != 0 || PieceEventEndsMoving(kPeProgress) != 0 || PieceEventEndsMoving(kPeStarted) != 0)
        Fail("a materials, helper or progress write ended the moving build");
    if (PieceEventEndsMoving(kPeStopped) != 1 || PieceEventEndsMoving(kPeFinished) != 1 || PieceEventEndsMoving(kPePlaced) != 1) Fail("a stop, finish or placement left the build moving");
}
/* ---- per-piece world-server records, fold 2: each answer matched to its write, no write at a save, a holder over a removal mark ---- */
/* T-1045 fold 3 (T1506): one writer decision on both sides; every write gated; a held change sent once on the no -> yes edge */
void t_t1045f3_writer_gate()
{
    using namespace piecerec;
    /* the game's loaded answer is its SENT report, inside the server's 10 s window less the margin */
    if (PieceReportLoaded(-1.0) != 0 || PieceReportLoaded(0.0) != 1 || PieceReportLoaded(8.0) != 1 || PieceReportLoaded(8.5) != 0)
        Fail("PieceReportLoaded: the sent report's age against the world server's window");
    /* the rule answers what the world server's PieceAcceptRule answers, for every sender / holder / owner / loaded combination */
    for (int me = -1; me <= 2; ++me) for (int holder = -1; holder <= 2; ++holder) for (int owner = 0; owner <= 2; ++owner) for (int ld = 0; ld <= 1; ++ld)
    {
        const int upd = PieceAcceptRule(1, 3, owner, ld, 0, 0, 0, 0, 4, owner, ld, me, holder);
        if ((PieceWriterRule(me, owner, 0, ld, holder) != 0) != (upd != kPrRefuseNotWriter)) Fail("PieceWriterRule: an update answered unlike the world server");
        const int sd = PieceAcceptRule(0, 0, -1, 0, 0, 0, 1, 0, 1, owner, ld, me, holder);
        if ((PieceWriterRule(me, owner, 1, ld, holder) != 0) != (sd == kPrAccept)) Fail("PieceWriterRule: a seed answered unlike the world server");
    }
    if (PieceWriterRule(0, 0, 0, 0, 1) != 0) Fail("an own piece's update while the report does not name the area and another game holds it");
    if (PieceWriterRule(0, 0, 0, 1, 1) != 1 || PieceWriterRule(0, 0, 0, 0, 0) != 1) Fail("an own piece: loaded by the report, or held here");
    if (PieceWriterRule(0, 1, 0, 1, 0) != 0 || PieceWriterRule(0, 1, 0, 0, 0) != 1) Fail("another player's piece: only while its owner is away and this game holds the area");
    if (PieceWriterRule(0, 0, 1, 0, -1) != 1 || PieceWriterRule(0, 0, 1, 0, 1) != 0) Fail("an own seed: also while nobody holds the area, never while another holds it unloaded here");
    /* the edge: a change while not the writer is held; ONE send when the answer turns yes; nothing again while it stays yes */
    {
        int said = 0, edge = 0, marked = 1, sends = 0;
        const int ans1[] = { 0, 0, 0, 1, 1, 1 };
        for (int i = 0; i < 6; ++i) { if (marked == 0) continue; const int g = PieceSendGate(ans1[i], &said, &edge); if (g == kPsgSend) { ++sends; marked = 0; } }
        if (sends != 1 || said != 1) Fail("a held change was not sent exactly once when this game became the writer");
        /* refused as not the writer's while this game read itself the writer: no write while the answer stays yes */
        marked = 1; edge = 1; sends = 0;
        const int ans2[] = { 1, 1, 1, 1 };
        for (int i = 0; i < 4; ++i) { if (marked == 0) continue; if (PieceSendGate(ans2[i], &said, &edge) == kPsgSend) { ++sends; marked = 0; } }
        if (sends != 0 || edge != 1) Fail("a refused write was sent again while the writer answer stayed yes");
        const int ans3[] = { 1, 0, 0, 1, 1 };
        for (int i = 0; i < 5; ++i) { if (marked == 0) continue; if (PieceSendGate(ans3[i], &said, &edge) == kPsgSend) { ++sends; marked = 0; } }
        if (sends != 1 || edge != 0) Fail("a refused write was not sent once when the answer turned from no to yes");
        /* a new change ends the wait (PcrMarkW clears edgeWait): one send, still yes */
        marked = 1; edge = 0;
        if (PieceSendGate(1, &said, &edge) != kPsgSend) Fail("a new change while the writer was not written");
    }
    /* a refused write's generation is used again; a later write sent meanwhile, or a refused generation not above the stored one, keeps it */
    {
        unsigned int sg = 7, g = 0;
        PieceRetryGen(1, &sg, 7, 5);
        if (sg != 6 || PieceWriteGen(1, 5, 1, sg, &g) != 0 || g != 7) Fail("PieceRetryGen: the refused generation 7 is not written again");
        sg = 8; PieceRetryGen(1, &sg, 7, 5); if (sg != 8) Fail("PieceRetryGen: a later write's generation was lowered");
        sg = 5; PieceRetryGen(1, &sg, 5, 5); if (sg != 5) Fail("PieceRetryGen: a refused generation at the stored one was lowered");
        sg = 1; PieceRetryGen(1, &sg, 1, 0); if (sg != 0 || PieceWriteGen(0, 0, 1, sg, &g) < 0 || g != 1) Fail("PieceRetryGen: a refused seed is not written at generation 1 again");
    }
}
/* T-1045 fold 4 (review): mirrored inputs; a refusal as the writer recovers on the next real turn; no turn from ageing; in-flight
   answers tracked */
void t_t1045f4_writer_events()
{
    using namespace piecerec;
    /* 4: an update is judged against the stored record's owner */
    if (PieceUpdateOwner(0, 1, 5, 1, 1, 4) != 1) Fail("PieceUpdateOwner: the newest kept copy's owner");
    if (PieceUpdateOwner(0, 1, 3, 1, 1, 4) != 0 || PieceUpdateOwner(0, 0, 0, -1, 0, 0) != 0) Fail("PieceUpdateOwner: this game's own taken write, or nothing known");
    /* 1b: no map = holder not known (no seed on 'nobody'); a frozen restored holder named verbatim refuses another's seed as the server does */
    if (PieceWriterRule(0, 0, 1, 0, kPieceHolderUnknown) != 0 || PieceWriterRule(0, 0, 1, 0, -1) != 1 || PieceWriterRule(0, 0, 1, 0, 1) != 0)
        Fail("a seed: unknown holder, nobody, a (frozen) holder elsewhere");
    if (PieceSeedPlaceOk(0, kPieceHolderUnknown, 0, 0) != 0) Fail("the unknown holder read as nobody");
    /* 1c: refused while this game read itself the writer: answer NO; nothing until a map newer than the refusal; then ONE send */
    {
        int said = 1, edge = 0, marked = 1, sends = 0;
        PieceRefusedAsWriter(&said, &edge);
        if (said != 0 || edge != 1) Fail("PieceRefusedAsWriter: the answer is not NO");
        if (PieceConfirmReady(9, 9, 0, 1, 1, 1) != 0 || PieceConfirmReady(10, 9, 0, 1, 1, 1) != 1) Fail("PieceConfirmReady: only a map after the refusal");
        const int ans[] = { 1, 1, 1 };   /* the newer map says yes, and stays yes */
        for (int i = 0; i < 3; ++i) { if (marked == 0) continue; if (PieceSendGate(ans[i], &said, &edge) == kPsgSend) { ++sends; marked = 0; } }
        if (sends != 1) Fail("a refusal as the writer was not written exactly once at the next real yes");
    }
    /* 2: time passing is no event, and ageing of the report only turns yes -> no */
    if (PieceHeldRecheckDue(7, 7, 3, 3) != 0) Fail("a held piece asked again with no event");
    if (PieceHeldRecheckDue(8, 7, 3, 3) != 1 || PieceHeldRecheckDue(7, 7, 4, 3) != 1) Fail("a held piece not asked on a map / a report naming its area");
    if (PieceHeldRecheckDue(7, 7, 3, 4) != 0) Fail("a report not naming the area counted as its event");
    {
        int said = 0, edge = 0, sends = 0, wasYes = 0, turned = 0;
        for (double age = 0.0; age <= 30.0; age += 0.25)
        {
            const int wr = PieceWriterRule(0, 0, 0, PieceReportLoaded(age), 1);
            if (wr != 0) wasYes = 1; else if (wasYes != 0) turned = 1;
            if (turned != 0 && wr != 0) Fail("ageing turned the answer from no back to yes");
            if (PieceSendGate(wr, &said, &edge) == kPsgSend && turned != 0) ++sends;
        }
        if (turned == 0 || sends != 0) Fail("the report's ageing did not hold, or sent after holding");
    }
    /* 3: an unmarked piece with a write in flight stays watched (its answer is tracked); a done one leaves */
    if (PieceWatchKeep(0, 0, 1) != 1 || PieceWatchKeep(1, 1, 0) != 1 || PieceWatchKeep(1, 0, 0) != 0 || PieceWatchKeep(0, 1, 0) != 0)
        Fail("PieceWatchKeep");
    /* 3: a turn yes -> no -> yes while a write flies, then a refusal: still exactly one write after the refusal */
    {
        int said = 1, edge = 0, sends = 0;   /* the in-flight answer, noted by the drain's watch, reads yes when the refusal lands */
        PieceRefusedAsWriter(&said, &edge);
        if (PieceSendGate(0, &said, &edge) != kPsgHold) Fail("held after the refusal while the map says no");
        if (PieceSendGate(1, &said, &edge) == kPsgSend) ++sends;
        if (sends != 1) Fail("the turn after a refusal was missed");
    }
}
/* T-1045 fold 5 (re-check): after a refusal as the writer, a later map with the SAME inputs writes nothing; changed inputs write once;
   a second refusal on the new inputs holds again; a change to the piece writes once */
void t_t1045f5_refused_inputs()
{
    using namespace piecerec;
    int said = 1, edge = 0, sends = 0, marked = 1, confirm = 0;
    unsigned int atSeq = 0; int atHolder = -1, atBit = -1;
    /* refusal 1, judged on map 5: holder slot 1, the owner's bit 0 */
    PieceRefusedAsWriter(&said, &edge); confirm = 1; atSeq = 5; atHolder = 1; atBit = 0;
    /* map 6, identical inputs: no write */
    if (PieceConfirmReady(6, atSeq, 1, 0, atHolder, atBit) != 0) Fail("an identical later map counted as a change");
    /* map 7: the holder is now this game - one write */
    if (PieceConfirmReady(7, atSeq, 0, 0, atHolder, atBit) != 1) Fail("a changed holder did not count");
    if (PieceSendGate(1, &said, &edge) == kPsgSend) { ++sends; marked = 0; }
    if (sends != 1) Fail("changed inputs did not write exactly once");
    /* refusal 2 on those new inputs (map 7): the next identical map writes nothing */
    marked = 1; PieceRefusedAsWriter(&said, &edge); atSeq = 7; atHolder = 0; atBit = 0;
    if (PieceConfirmReady(8, atSeq, 0, 0, atHolder, atBit) != 0) Fail("an identical map after a second refusal counted as a change");
    if (PieceConfirmReady(9, atSeq, 0, 0, atHolder, atBit) != 0) Fail("a further identical map counted as a change");
    /* the piece changes (PcrMarkW ends the confirm): one write on the normal answer */
    confirm = 0;
    if (confirm == 0 && marked != 0 && PieceSendGate(1, &said, &edge) == kPsgSend) { ++sends; marked = 0; }
    if (sends != 2) Fail("a change to the piece after the second refusal did not write exactly once");
    if (marked != 0 || PieceSendGate(1, &said, &edge) != kPsgSend || edge != 0) Fail("the gate after the change");
}
/* ---- per-piece world-server records, the read side: the apply (src/common/piecerec.h) ---- */
void t_t1045b_piece_apply()
{
    using namespace piecerec;
    /* wait until the area is loaded AND its record list is complete */
    if (PieceApplyChoice(0, 1, 1, 1, 0, 0, 0, 1, 0, 1) != kPaWait) Fail("an area not loaded did not wait");
    if (PieceApplyChoice(1, 0, 1, 1, 0, 0, 0, 1, 0, 1) != kPaWait) Fail("an area whose list is not complete did not wait");
    if (PieceApplyChoice(1, 0, 0, 0, 0, 1, 0, 1, 0, 1) != kPaWait) Fail("no record before the list is complete did not wait");
    /* make / state / reown */
    if (PieceApplyChoice(1, 1, 1, 1, 0, 0, 0, 1, 0, 1) != kPaMake) Fail("a live record of a missing piece was not made");
    if (PieceApplyChoice(1, 1, 1, 1, 0, -1, 0, 1, 0, 1) != kPaMake) Fail("a live record of a piece not known here did not go to the make road");
    if (PieceApplyChoice(1, 1, 1, 1, 0, 1, 0, 1, 0, 1) != kPaState) Fail("a standing piece (own or another's) with a differing record did not take its state");
    if (PieceApplyChoice(1, 1, 1, 1, 0, 1, 0, 1, 1, 1) != kPaNone) Fail("a standing piece already showing the record's state was sent to a road");
    if (PieceApplyChoice(1, 1, 1, 1, 0, 1, 0, 0, 0, 1) != kPaReown) Fail("a standing piece of the wrong owner was not re-owned");
    /* removal marks: only over the very placement, only where something stands or waits */
    if (PieceApplyChoice(1, 1, 1, 1, 1, 1, 0, 1, 0, 1) != kPaRemove) Fail("a removal mark of the standing placement did not remove it");
    if (PieceApplyChoice(1, 1, 1, 1, 1, 1, 0, 1, 0, 0) != kPaNone) Fail("a removal mark took down a piece rebuilt since (another placement)");
    if (PieceApplyChoice(1, 1, 1, 1, 1, -1, 0, 1, 0, 1) != kPaRemove) Fail("a removal mark over a piece not known here did not go to the remove road");
    if (PieceApplyChoice(1, 1, 1, 1, 1, 0, 1, 1, 0, 1) != kPaRemove) Fail("a removal mark did not drop a placement waiting at the key");
    if (PieceApplyChoice(1, 1, 1, 1, 1, 0, 0, 1, 0, 1) != kPaNone) Fail("a removal mark with nothing at the key went to a road");
    if (PieceApplyChoice(1, 1, 1, 0, 0, 1, 0, 1, 0, 1) != kPaUnreadable) Fail("an unreadable record was acted on");
    /* no record never means removal, whatever stands */
    for (int present = -1; present <= 1; ++present)
        for (int pending = 0; pending <= 1; ++pending)
            if (PieceApplyChoice(1, 1, 0, 0, 0, present, pending, 0, 0, 1) != kPaNone || PieceApplyChoice(1, 1, 0, 1, 1, present, pending, 1, 0, 1) != kPaNone)
                Fail("a piece with no record was not left as loaded");
    /* the same state: flags, progress within a thousandth, the same materials */
    {
        const float m1[2] = { 3.0f, 1.0f }, m2[2] = { 3.0f, 2.0f };
        if (PieceStateSame(5.0f, 10.0f, 0, 0, 0, 2, m1, 5.0f, 10.0f, 0, 0, 0, 2, m1) != 1) Fail("an equal state read as different");
        if (PieceStateSame(5.0f, 10.0f, 0, 0, 0, 2, m1, 5.5f, 10.0f, 0, 0, 0, 2, m1) != 0) Fail("a progress difference read as the same");
        if (PieceStateSame(5.0f, 10.0f, 0, 0, 0, 2, m1, 5.0f, 10.0f, 0, 0, 0, 2, m2) != 0) Fail("a materials difference read as the same");
        if (PieceStateSame(10.0f, 10.0f, 1, 1, 0, 2, m1, 10.0f, 10.0f, 1, 0, 0, 2, m1) != 0) Fail("a destroyed difference read as the same");
        if (PieceStateSame(5.0f, 10.0f, 0, 0, 0, 2, m1, 5.0f, 10.0f, 0, 0, 0, -1, 0) != 0) Fail("unread materials read as the same");
    }
    /* the first look in this world yields to a holder's record and to a removal mark of its placement - never a seed over them */
    if (PieceFirstLookYield(kPeFirst, 1, 0, 0, 1) != 1) Fail("the first look overwrote a holder's record with the save");
    if (PieceFirstLookYield(kPeFirst, 1, 1, 0, 1) != 0) Fail("the first look over this game's own record yielded");
    if (PieceFirstLookYield(kPeFirst, 1, 1, 1, 1) != 1) Fail("the first look seeded over a removal mark of its own placement");
    if (PieceFirstLookYield(kPeFirst, 1, 1, 1, 0) != 0) Fail("a rebuilt piece's first look did not seed over the old removal mark");
    if (PieceFirstLookYield(kPeMats, 1, 0, 0, 1) != 0 || PieceFirstLookYield(kPeFirst, 0, 0, 0, 1) != 0) Fail("the yield caught a real change or a piece with no record");
    /* the state goes onto every standing or made piece, a finished one included; progress only onto an unfinished one */
    if (PieceApplyStateGoes(1, 0) != 1 || PieceApplyStateGoes(0, 1) != 1 || PieceApplyStateGoes(0, 0) != 0) Fail("the state rule");
    if (PieceApplyProgressGoes(0, 0) != 1) Fail("an unfinished piece did not take its record's progress");
    if (PieceApplyProgressGoes(1, 0) != 0 || PieceApplyProgressGoes(0, 1) != 0 || PieceApplyProgressGoes(1, 1) != 0) Fail("a finished piece took progress");
    if (std::strcmp(PieceApplyName(kPaMake), "made") != 0 || std::strcmp(PieceApplyName(kPaState), "state") != 0 || std::strcmp(PieceApplyName(kPaRemove), "removed") != 0
        || std::strcmp(PieceApplyName(kPaReown), "reowned") != 0 || std::strcmp(PieceApplyName(kPaNone), "none") != 0) Fail("the apply words in the log lines");
    /* pp.build at a world load: a lone game lays at once; online the row waits for the list, then its record decides */
    if (PiecePpRowChoice(1, 1, 0, 1, 1) != kPpLay) Fail("a lone game's row was not laid at once");
    if (PiecePpRowChoice(0, 0, 0, 0, 0) != kPpLay) Fail("a row with no record id was held");
    if (PiecePpRowChoice(0, 1, 0, 0, 0) != kPpHold) Fail("an online row was laid before its area's list is complete");
    if (PiecePpRowChoice(0, 1, 1, 0, 0) != kPpLay) Fail("a row with no record was not laid (and seeded)");
    if (PiecePpRowChoice(0, 1, 1, 1, 0) != kPpFromRecord) Fail("a row with a live record did not give way to the record");
    if (PiecePpRowChoice(0, 1, 1, 1, 1) != kPpDrop) Fail("a row under a removal mark was not dropped");
    /* the protocol: placement, state and removal travel only in the records from 173 */
    if (kGameProtocolRecordFeed != 173u || hovrec::kGameProtocol < kGameProtocolRecordFeed) Fail("the game-to-game protocol is not the record feed's");
}
/* ---- T-1047: a power piece's live values in its record (format 2; src/common/piecerec.h) ---- */
void t_t1047_piece_live()
{
    using namespace piecerec;
    if (kPrFormat != 1u || kPrFormatLive != 2u || kPieceLiveBatteryBands != 20 || kPeLive != kPeMax) Fail("t1047: the format numbers, the band count or the LIVE event");
    if (kGameProtocolLiveRecord != 174u || hovrec::kGameProtocol < kGameProtocolLiveRecord) Fail("t1047: the game-to-game protocol is not the LIVE record's");
    if (std::string(PieceEventName(kPeLive)) != "live" || PieceEventEndsMoving(kPeLive) != 0 || PieceWriteDue(1.0f, 1.0f, 10.0f, kPeLive) != 1) Fail("t1047: the LIVE event");

    /* format 1 still reads, its LIVE values not known */
    const std::string key = "12-wall.mod@3,41@1003,-205,100";
    const std::vector<char> r1 = T1045Rec(key, 4, 1, 1, 0, 77, 5.0f);
    if (r1.empty()) { Fail("t1047: no format-1 record made"); return; }
    PieceHeader h;
    PieceBody b;
    if (DecodePieceRecord(&r1[0], r1.size(), &h, &b) != kPrOk || h.format != kPrFormat || b.live.have != 0) Fail("t1047: a format-1 record does not read with its LIVE values unknown");

    /* format 2: the LIVE part round-trips */
    b.live.have = 1; b.live.on = 1; b.live.gets = 0; b.live.battery = 37.5f; b.live.n = 2; b.live.stock[0] = 0.25f; b.live.stock[1] = 0.0f;
    std::vector<char> r2;
    if (!EncodePieceRecord(&r2, h, b)) { Fail("t1047: a format-2 record was not written"); return; }
    PieceHeader h2;
    PieceBody b2;
    if (DecodePieceRecord(&r2[0], r2.size(), &h2, &b2) != kPrOk) { Fail("t1047: a format-2 record does not read"); return; }
    if (h2.format != kPrFormatLive || b2.live.have != 1 || b2.live.on != 1 || b2.live.gets != 0 || b2.live.battery != 37.5f || b2.live.n != 2
        || b2.live.stock[0] != 0.25f || b2.live.stock[1] != 0.0f || b2.place != b.place || b2.state != b.state) Fail("t1047: the LIVE part did not round-trip");
    if (r2.size() != r1.size() + 4u + 12u + 8u) Fail("t1047: the LIVE part is not u32 length + flags, battery, count and two stocks");
    {   /* the world server takes a format-2 write whole (its judge reads the record); a game keeps it with its LIVE part */
        std::vector<int> loaded;
        loaded.push_back(1);
        PieceHeader jh;
        (void)loaded;
        /* the owner's seed of a piece with no record, carrying a LIVE part: taken whole */
        const std::vector<char> s1 = T1045Rec(key, 4, 1, 1, kPrFlagSeed, 77, 5.0f);
        PieceHeader sh;
        PieceBody sb;
        std::vector<char> s2;
        if (s1.empty() || DecodePieceRecord(&s1[0], s1.size(), &sh, &sb) != kPrOk) Fail("t1047: no format-1 seed made");
        else
        {
            sb.live = b.live;
            if (!EncodePieceRecord(&s2, sh, sb) || s2.size() < 8u || s2[4] != (char)2) Fail("t1047: a format-2 seed was not written");
            else
            {
                std::vector<int> nobody;
                if (PieceJudgeWrite(MakePieceRecId(key), &s2[0], s2.size(), 1, -1, nobody, 0, 0, -1, 0, 0, &jh) != kPrAccept || jh.format != kPrFormatLive
                    || jh.generation != 4) Fail("t1047: the world server does not take the owner's format-2 seed");
            }
        }
        PieceHeader ih;
        PieceBody ib;
        int dw = 0;
        if (PieceIntakeJudge(MakePieceRecId(key), &r2[0], r2.size(), 0, 0u, &ih, &ib, &dw) != kPiKeep || ib.live.have != 1 || ib.live.battery != 37.5f)
            Fail("t1047: a game does not keep a format-2 record with its LIVE part");
        PieceHeader hd;
        if (DecodePieceHeader(&r2[0], r2.size(), &hd) != kPrOk || hd.format != kPrFormatLive) Fail("t1047: the world server's header read refuses format 2");
    }
    {   /* malformed LIVE parts are refused whole, at the encoder and at the decoder */
        std::vector<char> x;
        PieceBody bb = b; bb.live.on = 2;
        if (EncodePieceRecord(&x, h, bb)) Fail("t1047: a switch that is not 0 / 1 was written");
        bb = b; bb.live.battery = -1.0f;
        if (EncodePieceRecord(&x, h, bb)) Fail("t1047: a negative battery was written");
        bb = b; bb.live.n = 17;
        if (EncodePieceRecord(&x, h, bb)) Fail("t1047: 17 stocks were written");
        const size_t lf = r2.size() - 20u;   /* the LIVE part's flags */
        std::vector<char> t(r2); t[lf] = (char)9;   /* bit 3: unknown (bit 2 is the bar's, T-1047 step 2b) */
        if (DecodePieceRecord(&t[0], t.size(), 0, 0) != kPrBadBody) Fail("t1047: an unknown LIVE flag bit was not refused");
        t = r2; t[lf + 8] = (char)3;
        if (DecodePieceRecord(&t[0], t.size(), 0, 0) != kPrBadBody) Fail("t1047: a stock count that is not the part's length was not refused");
        t = r2; t[lf + 4] = (char)0; t[lf + 5] = (char)0; t[lf + 6] = (char)0xC0; t[lf + 7] = (char)0x7F;
        if (DecodePieceRecord(&t[0], t.size(), 0, 0) != kPrBadBody) Fail("t1047: a battery that is not a number was not refused");
        t = r1; t[4] = (char)2;
        if (DecodePieceRecord(&t[0], t.size(), 0, 0) != kPrBadBody) Fail("t1047: format 2 with no LIVE part was not refused");
        t = r2; t[4] = (char)1;
        if (DecodePieceRecord(&t[0], t.size(), 0, 0) != kPrBadBody) Fail("t1047: format 1 with a LIVE part was not refused");
        const std::vector<char> rm = T1045Rec(key, 5, 1, 1, kPrFlagRemoved, 77, 0.0f);
        if (rm.empty()) Fail("t1047: no removal mark made");
        else { t = rm; t[4] = (char)2; if (DecodePieceRecord(&t[0], t.size(), 0, 0) == kPrOk) Fail("t1047: a format-2 removal mark was read"); }
        PieceBody rb; rb.reason = coopbuild::kBuildReasonDismantled; rb.live = b.live;
        PieceHeader rh = h; rh.flags = kPrFlagRemoved;
        if (!EncodePieceRecord(&x, rh, rb) || x[4] != (char)1) Fail("t1047: a removal mark is not written as format 1");
    }
    {   /* the same test: a LIVE-only change is a difference */
        PieceLive a = b.live, c = b.live, none;
        if (PieceLiveSame(a, c) != 1 || PieceLiveEqual(a, c) != 1) Fail("t1047: equal LIVE values read as different");
        c.on = 0;
        if (PieceLiveSame(a, c) != 0) Fail("t1047: a switch change read as the same");
        c = a; c.battery += 0.01f;
        if (PieceLiveSame(a, c) != 0 || PieceLiveEqual(a, c) != 0) Fail("t1047: a battery change read as the same");
        c = a; c.stock[0] = 0.2f;
        if (PieceLiveSame(a, c) != 0) Fail("t1047: a stock change read as the same");
        if (PieceLiveSame(a, none) != 0 || PieceLiveEqual(a, none) != 0 || PieceLiveEqual(none, none) != 1) Fail("t1047: not known read as the same as known");
        if (PieceLiveApplyChoice(0, 1, 1, 0, 0, 0, kPaNone) != kPlaNow) Fail("t1047: a LIVE-only difference does not apply while the build state is the same");
        if (PieceLiveApplyChoice(0, 1, 1, 0, 0, 0, kPaState) != kPlaNow) Fail("t1047: LIVE does not apply beside a state");
        if (PieceLiveApplyChoice(0, 1, 0, 0, 0, 0, kPaNone) != kPlaNone) Fail("t1047: format 1 (LIVE not known) applied");
        if (PieceLiveApplyChoice(0, 1, 1, 0, 1, 0, kPaNone) != kPlaNone) Fail("t1047: this game's own write applied while the area stayed loaded");
        if (PieceLiveApplyChoice(0, 1, 1, 0, 1, 1, kPaNone) != kPlaNow) Fail("t1047: this game's own record not applied at the first apply since the world loaded");
        if (PieceLiveApplyChoice(0, 1, 1, 0, 0, 0, kPaMake) != kPlaOwe) Fail("t1047: a make does not owe the LIVE values");
        if (PieceLiveApplyChoice(0, 1, 1, 0, 0, 0, kPaRemove) != kPlaNone || PieceLiveApplyChoice(1, 1, 1, 0, 0, 0, kPaNone) != kPlaNone
            || PieceLiveApplyChoice(0, 0, 1, 0, 0, 0, kPaNone) != kPlaNone || PieceLiveApplyChoice(0, 1, 1, 1, 0, 0, kPaNone) != kPlaNone)
            Fail("t1047: a removal, an unreadable body or a seen generation applied LIVE values");
    }
    {   /* the band rule */
        if (PieceLiveBand(0.0f, 100.0f) != 0 || PieceLiveBand(100.0f, 100.0f) != 21 || PieceLiveBand(50.0f, 100.0f) != 11 || PieceLiveBand(4.9f, 100.0f) != 1
            || PieceLiveBand(5.0f, 100.0f) != 2 || PieceLiveBand(0.5f, 0.0f) != 11) Fail("t1047: the bands (empty 0, twentieths 1..20, full 21; no full = 1)");
        PieceLive s, n;
        s.have = 1; s.on = 1; s.battery = 50.0f; s.n = 1; s.stock[0] = 0.93f;
        float peak[kPrLiveMaxStocks];
        for (unsigned int i = 0; i < kPrLiveMaxStocks; ++i) peak[i] = 0.0f;
        peak[0] = 1.0f;
        n = s; n.battery = 52.0f;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 0) Fail("t1047: a battery step inside a band was due");
        n = s; n.battery = 55.5f;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 0) Fail("t1047: a battery an edge hover past its band was due");
        n = s; n.battery = 48.0f;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 0) Fail("t1047: a battery just below its band's lower edge was due");
        n = s; n.battery = 58.0f;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: a battery half a band past its band was not due");
        n = s; n.battery = 47.0f;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: a battery half a band below its band was not due");
        {   /* at full under a flickering load: nothing until it is really down; at empty the same upward */
            PieceLive fu = s; fu.battery = 100.0f;
            n = fu; n.battery = 99.5f;
            if (PieceLiveDue(fu, peak, 100.0f, -1, n, -1) != 0) Fail("t1047: a full battery's flicker was due");
            n = fu; n.battery = 93.0f;
            if (PieceLiveDue(fu, peak, 100.0f, -1, n, -1) != 0) Fail("t1047: a full battery's first fall out of full cost a write of its own");
            n = fu; n.battery = 92.0f;
            if (PieceLiveDue(fu, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: a full battery a band and a half down was not due");
            PieceLive em = s; em.battery = 0.0f;
            n = em; n.battery = 2.0f;
            if (PieceLiveDue(em, peak, 100.0f, -1, n, -1) != 0) Fail("t1047: an empty battery's flicker was due");
            n = em; n.battery = 3.0f;
            if (PieceLiveDue(em, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: a battery half a band out of empty was not due");
            if (PieceLiveBandLeft(55.0f, 54.9f, 100.0f) != 0 || PieceLiveBandLeft(54.9f, 55.0f, 100.0f) != 0 || PieceLiveBandLeft(54.9f, 57.5f, 100.0f) != 1)
                Fail("t1047: PieceLiveBandLeft at a band edge");
        }
        n = s; n.battery = 100.0f;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: a full battery was not due");
        PieceLive e = s; e.battery = 2.0f; n = e; n.battery = 0.0f;
        if (PieceLiveDue(e, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: an empty battery was not due");
        n = s; n.stock[0] = 0.91f;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 0) Fail("t1047: a stock step inside a band was due");
        n = s; n.stock[0] = 0.89f;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 0) Fail("t1047: a stock an edge hover below its band was due");
        n = s; n.stock[0] = 0.87f;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: a stock half a band below its band was not due");
        {   /* a refill written at its peak: its first fall out of full costs no write of its own */
            PieceLive rf = s; rf.stock[0] = 1.0f;
            n = rf; n.stock[0] = 0.96f;
            if (PieceLiveDue(rf, peak, 100.0f, -1, n, -1) != 0) Fail("t1047: a refilled stock's first fall out of full was due");
            n = rf; n.stock[0] = 0.92f;
            if (PieceLiveDue(rf, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: a refilled stock a band and a half down was not due");
        }
        n = s; n.stock[0] = 0.95f;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: a stock that rose was not due");
        PieceLive f = s; f.stock[0] = 0.02f; n = f; n.stock[0] = 0.0f;
        if (PieceLiveDue(f, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: a stock that reached 0 was not due");
        n = s; n.n = 2;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: a change in the number of stocks was not due");
        n = s; n.on = 0;
        if (PieceLiveDue(s, peak, 100.0f, -1, n, -1) != 1) Fail("t1047: a switch change was not due");
        n = s;
        if (PieceLiveDue(s, peak, 100.0f, 0, n, 1) != 1 || PieceLiveDue(s, peak, 100.0f, -1, n, 1) != 0) Fail("t1047: the grid's on-battery flip (or a flag not read)");
        if (PieceLiveDue(PieceLive(), peak, 100.0f, -1, n, -1) != 1 || PieceLiveDue(s, peak, 100.0f, -1, PieceLive(), -1) != 0) Fail("t1047: nothing written / nothing known");
        float pk[kPrLiveMaxStocks];
        for (unsigned int i = 0; i < kPrLiveMaxStocks; ++i) pk[i] = 0.0f;
        PieceLivePeakNote(pk, PieceLive(), s);
        if (pk[0] != 0.93f) Fail("t1047: a first write does not set the stock's peak");
        PieceLive lo = s; lo.stock[0] = 0.5f;
        PieceLivePeakNote(pk, s, lo);
        if (pk[0] != 0.93f) Fail("t1047: a stock that fell moved its peak");
        PieceLive hi = s; hi.stock[0] = 1.0f;
        PieceLivePeakNote(pk, lo, hi);
        if (pk[0] != 1.0f) Fail("t1047: a stock that rose did not set its peak");
    }
    {   /* the writer side: who notes, the area's apply first, then one mark per change */
        if (PieceLiveNoteChoice(0, 1, 1, 1, 0, 1, 1, 1, 0) != kPlnNone) Fail("t1047: a game that does not run the engine steps noted the values");
        if (PieceLiveNoteChoice(1, 0, 1, 1, 1, 1, 1, 1, 0) != kPlnForget) Fail("t1047: values noted before the area's records were applied");
        if (PieceLiveNoteChoice(1, 1, 1, 0, 1, 0, 1, 1, 0) != kPlnBook) Fail("t1047: a kept record with the same values was not booked as the baseline");
        if (PieceLiveNoteChoice(1, 1, 1, 0, 1, 1, 1, 1, 0) != kPlnMark) Fail("t1047: a kept record that differs was not written over");
        if (PieceLiveNoteChoice(1, 1, 1, 0, 0, 1, 1, 1, 0) != kPlnMark) Fail("t1047: no kept LIVE values: the first write was not marked");
        if (PieceLiveNoteChoice(1, 1, 0, 0, 0, 1, 1, 1, 0) != kPlnNone) Fail("t1047: marked before the own piece's first build look");
        if (PieceLiveNoteChoice(1, 1, 1, 1, 0, 1, 0, 1, 0) != kPlnNone) Fail("t1047: no change since the last write was marked");
        if (PieceLiveNoteChoice(1, 1, 1, 1, 0, 1, 1, 0, 0) != kPlnNone) Fail("t1047: a change already marked was marked again");
        if (PieceLiveNoteChoice(1, 1, 1, 1, 0, 1, 1, 1, 1) != kPlnNone) Fail("t1047: a piece already marked was marked again");
        if (PieceLiveNoteChoice(1, 1, 1, 1, 0, 1, 1, 1, 0) != kPlnMark) Fail("t1047: a change was not marked");
    }
}
/* ---- T-1047 step 2b: a production machine's bar and work state on its piece record (LIVE flag bit 2; src/common/piecerec.h) ---- */
/* the most writes in one cycle of a sawtooth bar - 0.01 .. 0.99, then an item made and back at 0 - at `bands`, over three cycles */
int T1047cSawWrites(int bands)
{
    using namespace piecerec;
    PieceLive sent, now;
    sent.have = 1; sent.hasBar = 1; sent.bar = 0.0f; sent.state = 2;
    now = sent;
    float peak[kPrLiveMaxStocks];
    for (unsigned int i = 0; i < kPrLiveMaxStocks; ++i) peak[i] = 0.0f;
    int most = 0;
    for (int cyc = 0; cyc < 3; ++cyc)
    {
        int w = 0;
        for (int i = 1; i <= 100; ++i)
        {
            now.bar = (i < 100) ? (float)i / 100.0f : 0.0f;
            if (PieceLiveDue(sent, peak, 0.0f, -1, now, -1, bands) != 0) { sent = now; ++w; }
        }
        if (w > most) most = w;
    }
    return most;
}
/* T-1047 step 2b review: a sawtooth bar that reaches full 1.0 before its item is made, at `bands`, over three cycles - the most writes in
   one cycle; *fullOk 1 = every cycle wrote the full bar */
int T1047cSawFull(int bands, int* fullOk)
{
    using namespace piecerec;
    PieceLive sent, now;
    sent.have = 1; sent.hasBar = 1; sent.bar = 0.0f; sent.state = 2;
    now = sent;
    float peak[kPrLiveMaxStocks];
    for (unsigned int i = 0; i < kPrLiveMaxStocks; ++i) peak[i] = 0.0f;
    int most = 0;
    *fullOk = 1;
    for (int cyc = 0; cyc < 3; ++cyc)
    {
        int w = 0;
        for (int i = 1; i <= 101; ++i)
        {
            now.bar = (i <= 100) ? (float)i / 100.0f : 0.0f;
            if (PieceLiveDue(sent, peak, 0.0f, -1, now, -1, bands) != 0) { sent = now; ++w; }
            if (i == 100 && sent.bar != 1.0f) *fullOk = 0;
        }
        if (w > most) most = w;
    }
    return most;
}
/* T-1047 step 2b review: a work state flapping 2 / 1 at each look every stepMs for ms, then held at 1 for holdMs, the bar unchanged, each
   look with the hold clock (PieceLiveStateHeld) - the writes made */
int T1047cFlapWrites(unsigned int stepMs, unsigned int ms, unsigned int holdMs)
{
    using namespace piecerec;
    PieceLive sent, now;
    sent.have = 1; sent.hasBar = 1; sent.bar = 0.5f; sent.state = 2;
    now = sent;
    float peak[kPrLiveMaxStocks];
    for (unsigned int i = 0; i < kPrLiveMaxStocks; ++i) peak[i] = 0.0f;
    int have = 0, seen = 0, w = 0;
    unsigned int at = 0u, t = 0u;
    for (; t <= ms; t += stepMs)
    {
        now.state = ((t / stepMs) % 2u == 0u) ? 2 : 1;
        const unsigned int held = PieceLiveStateHeld(&have, &seen, &at, now.state, t);
        if (PieceLiveDue(sent, peak, 0.0f, -1, now, -1, kPieceLiveBarBandsWorked, held) != 0) { sent = now; ++w; }
    }
    const unsigned int end = t + holdMs;
    for (; t <= end; t += stepMs)
    {
        now.state = 1;
        const unsigned int held = PieceLiveStateHeld(&have, &seen, &at, now.state, t);
        if (PieceLiveDue(sent, peak, 0.0f, -1, now, -1, kPieceLiveBarBandsWorked, held) != 0) { sent = now; ++w; }
    }
    return w;
}
void t_t1047b_live_misses()
{
    using namespace piecerec;
    /* a hit's state is sent again after a link loss only while a write carrying it is unanswered */
    {
        std::vector<PieceFly> f; PieceSrvView v;
        PieceFlyAdd(&f, 1, 0, 100, 1);
        if (PieceFlyHit(f) != 1) Fail("t1047b: an unanswered hit's write is not in flight");
        PieceFlyAdd(&f, 2, 0, 101);
        if (PieceFlyHit(f) != 1) Fail("t1047b: a LIVE-only write after an unanswered hit hid the hit");
        if (PieceFlyTaken(&f, 1, 100, &v) != 1 || PieceFlyHit(f) != 0) Fail("t1047b: a taken hit's write still sends its state again");
        /* the owner repairs; this game writes LIVE only and the link drops before the answer: the old hit's state does not go */
        PieceFlyAdd(&f, 3, 0, 102);
        if (PieceFlyHit(f) != 0) Fail("t1047b: a LIVE-only write after a taken hit carries the old hit");
        PieceFlyLinkLost(&f, &v);
        if (PieceFlyHit(f) != 0) Fail("t1047b: a hit in flight after a link loss");
        PieceFlyAdd(&f, 4, 0, 103, 1); PieceFlyAdd(&f, 5, 0, 104);
        if (PieceFlyRefused(&f, 4) != 1 || PieceFlyHit(f) != 0) Fail("t1047b: a refused hit's write still sends its state again");
        /* the oldest write dropped at the cap keeps its hit in flight */
        f.clear();
        PieceFlyAdd(&f, 10, 0, 200, 1);
        for (unsigned int g = 11; g < 11u + (unsigned int)kPieceFlyCap; ++g) PieceFlyAdd(&f, g, 0, 200 + g);
        if (f.size() != kPieceFlyCap || f[0].gen != 11 || PieceFlyHit(f) != 1) Fail("t1047b: a hit was lost with the oldest write at the cap");
        /* the dropped write's own late answer lets its carried hit go: a repair and a link loss then never send the old hit again */
        if (PieceFlyTaken(&f, 1, 200, &v) != 0 || PieceFlyHit(f) != 0 || f.size() != kPieceFlyCap) Fail("t1047b: a taken answer for a write dropped at the cap left its hit in flight");
        f.clear();
        PieceFlyAdd(&f, 10, 0, 200, 1);
        for (unsigned int g = 11; g < 11u + (unsigned int)kPieceFlyCap; ++g) PieceFlyAdd(&f, g, 0, 200 + g);
        if (PieceFlyRefused(&f, 9) != 0 || PieceFlyHit(f) != 1) Fail("t1047b: an answer older than the dropped hit's write let its hit go");
        if (PieceFlyRefused(&f, 10) != 0 || PieceFlyHit(f) != 0) Fail("t1047b: a refusal of the write dropped at the cap left its hit in flight");
        /* two drops: the hit rides on to the next oldest, naming the newest dropped write */
        f.clear();
        PieceFlyAdd(&f, 10, 0, 200, 1);
        for (unsigned int g = 11; g < 12u + (unsigned int)kPieceFlyCap; ++g) PieceFlyAdd(&f, g, 0, 200 + g);
        if (f.size() != kPieceFlyCap || f[0].gen != 12 || f[0].carry != 1 || f[0].carryGen != 11 || PieceFlyHit(f) != 1) Fail("t1047b: a hit carried twice was lost");
        if (PieceFlyTaken(&f, 1, 200, &v) != 0 || PieceFlyHit(f) != 1) Fail("t1047b: an answer for an older dropped write let a newer one's hit go");
        if (PieceFlyTaken(&f, 1, 211, &v) != 0 || PieceFlyHit(f) != 0) Fail("t1047b: the newest dropped write's answer left its carried hit");
        /* a write answered in the list still takes a carried hit with it */
        f.clear();
        PieceFlyAdd(&f, 10, 0, 200, 1);
        for (unsigned int g = 11; g < 11u + (unsigned int)kPieceFlyCap; ++g) PieceFlyAdd(&f, g, 0, 200 + g);
        if (PieceFlyTaken(&f, 1, 211, &v) != 1 || PieceFlyHit(f) != 0) Fail("t1047b: the carrying write taken left its hit");
    }
    /* another player's piece: a write's STATE comes from the newest write sent while a hit's write is unanswered, else the kept record */
    {
        if (PieceHolderStateFromFly(1, 0, 5, 4) != 1) Fail("t1047b: a LIVE-only write after an unanswered hit took the kept state");
        if (PieceHolderStateFromFly(0, 0, 5, 4) != 0) Fail("t1047b: no hit in flight took the state from the in-flight write");
        if (PieceHolderStateFromFly(1, 1, 5, 4) != 0) Fail("t1047b: a removal mark in flight gave the state");
        if (PieceHolderStateFromFly(1, 0, 4, 4) != 0 || PieceHolderStateFromFly(1, 0, 3, 4) != 0) Fail("t1047b: an in-flight write not above the kept record gave the state");
        std::vector<PieceFly> f; PieceSrvView v;
        PieceFlyAdd(&f, 5, 0, 300, 1); PieceFlyAdd(&f, 6, 0, 301);
        if (PieceHolderStateFromFly(PieceFlyHit(f), f.back().removed, f.back().gen, 4) != 1) Fail("t1047b: a second LIVE-only write lost the hit's state");
        if (PieceFlyTaken(&f, 1, 301, &v) != 1 || PieceHolderStateFromFly(PieceFlyHit(f), 0, 7, 6) != 0) Fail("t1047b: the state came from flight after both were taken");
    }
    /* an apply dropped at a full queue, or given up, never counts as landed (the switch is not taken from the record) */
    if (PieceLiveLandedRule(1, 0, 0, 0) != 1) Fail("t1047b: a landed apply");
    if (PieceLiveLandedRule(1, 0, 1, 0) != 0) Fail("t1047b: an apply dropped at a full queue counts as landed");
    if (PieceLiveLandedRule(1, 1, 0, 0) != 0 || PieceLiveLandedRule(0, 0, 0, 0) != 0) Fail("t1047b: a waiting apply or an area not applied counts as landed");
    if (PieceLiveLandedRule(1, 0, 0, 1) != 0) Fail("t1047b: a given-up apply counts as landed");
    if (PieceLiveStateDropRule(0) != 1 || PieceLiveStateDropRule(1) != 0) Fail("t1047b: the POWER_STATE drop rule");
    if (PieceLiveStateDropRule(PieceLiveLandedRule(1, 0, 1, 0)) != 1) Fail("t1047b: a dropped apply takes POWER_STATE");
    if (PieceLiveStateDropRule(PieceLiveLandedRule(1, 0, 0, 1)) != 1) Fail("t1047b: a given-up apply takes POWER_STATE (the writer keeps the record road)");
    /* a given-up apply never blocks this game's notes; a waiting or dropped one does */
    if (PieceLivePendingRule(0, 0, 1) != 0) Fail("t1047b: a given-up apply blocks the notes");
    if (PieceLivePendingRule(1, 0, 0) != 1 || PieceLivePendingRule(0, 1, 0) != 1 || PieceLivePendingRule(0, 0, 0) != 0) Fail("t1047b: the notes' pending rule");
    if (PieceLiveNoteChoice(1, PieceLivePendingRule(0, 0, 1) == 0 ? 1 : 0, 1, 1, 0, 1, 1, 1, 0) != kPlnMark) Fail("t1047b: a given-up piece's change was not marked");
    /* a new record tries a given-up apply again; the next run of faults gives it up again at the cap */
    {
        int failed = 1, n = 0;
        if (PieceLiveNewRecord(&failed) != 1 || failed != 0) Fail("t1047b: a new record did not try a given-up apply again");
        if (PieceLiveNewRecord(&failed) != 0 || failed != 0) Fail("t1047b: a new record over a live apply counted as a retry");
        for (int k = 1; k < kPieceLiveFaultCap; ++k) if (PieceLiveFaultStep(&n, -1, kPieceLiveFaultCap) != 0) Fail("t1047b: a retried apply given up before the cap");
        if (PieceLiveFaultStep(&n, -1, kPieceLiveFaultCap) != 1) Fail("t1047b: a retried apply not given up at the cap");
        if (PieceLiveNewRecord(0) != 0) Fail("t1047b: no flag");
    }
    /* the fault cap: kPieceLiveFaultCap faults in a row give the apply up; no piece here does not end the run, a landed apply does */
    {
        if (kPieceLiveFaultCap != 5) Fail("t1047b: the fault cap");
        int n = 0;
        for (int k = 1; k < kPieceLiveFaultCap; ++k) if (PieceLiveFaultStep(&n, -1, kPieceLiveFaultCap) != 0) Fail("t1047b: given up before the cap");
        if (PieceLiveFaultStep(&n, 0, kPieceLiveFaultCap) != 0 || n != kPieceLiveFaultCap - 1) Fail("t1047b: no piece here changed the run");
        if (PieceLiveFaultStep(&n, -1, kPieceLiveFaultCap) != 1) Fail("t1047b: not given up at the cap");
        n = 3;
        if (PieceLiveFaultStep(&n, 1, kPieceLiveFaultCap) != 0 || n != 0) Fail("t1047b: a written apply did not end the run");
        n = 3;
        if (PieceLiveFaultStep(&n, 2, kPieceLiveFaultCap) != 0 || n != 0) Fail("t1047b: a nothing-to-write apply did not end the run");
        if (PieceLiveFaultStep(0, -1, kPieceLiveFaultCap) != 0) Fail("t1047b: no run");
    }
}
/* ---- the apply's inputs and the own-piece state rule (src/common/piecerec.h) ---- */
void t_t1045b2_piece_rules()
{
    using namespace piecerec;
    /* present: an own piece by its row; another player's by its lookup; otherwise not known */
    if (PieceApplyPresent(1, 1, 0, 0, 0, 0) != 1 || PieceApplyPresent(1, 0, 0, 1, 1, 0) != 0) Fail("an own piece's presence is not its own row");
    if (PieceApplyPresent(0, 0, 1, 0, 1, 0) != 1 || PieceApplyPresent(0, 0, 1, 0, 0, 1) != 0 || PieceApplyPresent(0, 0, 1, 0, 0, 0) != -1) Fail("a copy's lookup");
    if (PieceApplyPresent(0, 0, 0, 1, 0, 1) != 0 || PieceApplyPresent(0, 0, 0, 1, 1, 0) != 1) Fail("a removal mark's key lookup with no copy row");
    if (PieceApplyPresent(0, 0, 0, 0, 1, 0) != -1) Fail("a live record with no copy row is not left to the make road's search");
    /* placement: an own piece only when both numbers are known and equal; another player's unless both known and different */
    if (PieceApplySamePlacement(1, 7u, 7u) != 1 || PieceApplySamePlacement(1, 7u, 8u) != 0) Fail("own placement equal / different");
    if (PieceApplySamePlacement(1, 0u, 7u) != 0 || PieceApplySamePlacement(1, 7u, 0u) != 0 || PieceApplySamePlacement(1, 0u, 0u) != 0) Fail("an own piece was taken down with a placement number unknown");
    if (PieceApplySamePlacement(0, 0u, 7u) != 1 || PieceApplySamePlacement(0, 7u, 8u) != 0) Fail("another player's placement rule");
    if (PieceApplyChoice(1, 1, 1, 1, 1, 1, 0, 1, 0, PieceApplySamePlacement(1, 0u, 7u)) != kPaNone) Fail("a placement-0 removal mark took an own piece down");
    /* a held pp.build row: an unreadable record lays it; a removal mark of another or an unknown placement lays it */
    if (PiecePpRowFinal(kPpFromRecord, 0, 1) != kPpLay || PiecePpRowFinal(kPpFromRecord, 1, 1) != kPpFromRecord) Fail("a held row under an unreadable record");
    if (PiecePpRowFinal(kPpDrop, 1, 0) != kPpLay || PiecePpRowFinal(kPpDrop, 1, 1) != kPpDrop || PiecePpRowFinal(kPpLay, 1, 0) != kPpLay) Fail("a held row under a removal mark");
    /* ahead: more progress, or finished while the piece is not; behind and equal are not */
    if (PieceRecordAhead(6.0f, 0, 5.0f, 0, 10.0f) != 1) Fail("more progress is not ahead");
    if (PieceRecordAhead(4.0f, 0, 5.0f, 0, 10.0f) != 0 || PieceRecordAhead(5.0f, 0, 5.0f, 0, 10.0f) != 0) Fail("a record behind or equal read as ahead");
    if (PieceRecordAhead(10.0f, 1, 9.0f, 0, 10.0f) != 1) Fail("finished while the piece is not is not ahead");
    if (PieceRecordAhead(10.0f, 1, 10.0f, 1, 10.0f) != 0 || PieceRecordAhead(9.0f, 0, 10.0f, 1, 10.0f) != 0) Fail("a finished piece read as behind its record");
    /* the own-piece state rule: a holder's record whole; this game's own record ahead at the first apply since load, materials kept */
    if (PieceOwnStateRule(0, 1, 0, 0) != kPoWhole || PieceOwnStateRule(0, 0, 0, 0) != kPoWhole || PieceOwnStateRule(0, 1, 1, 1) != kPoNone) Fail("a holder's record");
    if (PieceOwnStateRule(1, 1, 1, 0) != kPoAheadKeepMats) Fail("this game's own record ahead at the first apply since load was not laid");
    if (PieceOwnStateRule(1, 0, 1, 0) != kPoNone) Fail("after a reconnect the own record won over the live piece");
    if (PieceOwnStateRule(1, 1, 0, 0) != kPoNone || PieceOwnStateRule(1, 1, 0, 1) != kPoNone) Fail("an own record behind or equal was laid");
    if (PieceApplyChoice(1, 1, 1, 1, 0, 1, 0, 1, PieceOwnStateRule(1, 1, PieceRecordAhead(10.0f, 1, 4.0f, 0, 10.0f), 0) == kPoNone ? 1 : 0, 1) != kPaState)
        Fail("an own unfinished piece behind its own finished record did not take the state");
    if (PieceApplyChoice(1, 1, 1, 1, 0, 1, 0, 1, PieceOwnStateRule(1, 1, PieceRecordAhead(4.0f, 0, 10.0f, 1, 10.0f), 0) == kPoNone ? 1 : 0, 1) != kPaNone)
        Fail("an own finished piece took an unfinished record");
    /* the first look yields to an own record ahead too; not to one behind */
    if (PieceFirstLookYieldAhead(kPeFirst, 1, 1, 0, 1, 1) != 1 || PieceFirstLookYieldAhead(kPeFirst, 1, 1, 0, 1, 0) != 0) Fail("the first look and an own record ahead / behind");
    if (PieceFirstLookYieldAhead(kPeFirst, 1, 0, 0, 1, 0) != 1 || PieceFirstLookYieldAhead(kPeMats, 1, 1, 0, 1, 1) != 0) Fail("the first look's other yields");
}
/* ---- the seen generation and the waiting placement (src/common/piecerec.h) ---- */
void t_t1045b3_piece_seen()
{
    using namespace piecerec;
    /* applied gen 5: a record of gen 5 is none, gen 6 applies */
    unsigned int seen = PieceSeenGenNote(0u, 5u);
    if (PieceSeenGenSkip(0, 1, seen, 5u) != 1) Fail("a record at the applied generation was applied again");
    if (PieceSeenGenSkip(0, 1, seen, 6u) != 0) Fail("a newer record was not applied");
    /* this game's own write at gen 7, then a record of gen 6: none */
    seen = PieceSeenGenNote(seen, 7u);
    if (seen != 7u || PieceSeenGenSkip(0, 1, seen, 6u) != 1) Fail("a record below this game's own write was applied");
    if (PieceSeenGenNote(seen, 3u) != 7u) Fail("the seen generation went down");
    /* the first apply after a world load takes every record; nothing seen takes every record */
    if (PieceSeenGenSkip(1, 1, 7u, 6u) != 0 || PieceSeenGenSkip(0, 0, 0u, 1u) != 0) Fail("the first apply after load or an unseen piece skipped");
    /* a placement (or re-make) already waiting: no second PLACE */
    if (PieceApplyChoice(1, 1, 1, 1, 0, 0, 1, 1, 0, 1) != kPaNone || PieceApplyChoice(1, 1, 1, 1, 0, -1, 1, 1, 0, 1) != kPaNone) Fail("a second placement was chosen");
    if (PieceApplyChoice(1, 1, 1, 1, 0, 0, 0, 1, 0, 1) != kPaMake) Fail("a missing piece with nothing waiting was not made");
}
/* ---- the seen generation while the area stays loaded (src/common/piecerec.h) ---- */
void t_t1045b4_piece_seen_area()
{
    using namespace piecerec;
    const int state = PieceApplyChoice(1, 1, 1, 1, 0, 1, 0, 1, 0, 1), make = PieceApplyChoice(1, 1, 1, 1, 0, 0, 0, 1, 0, 1);
    const int remove = PieceApplyChoice(1, 1, 1, 1, 1, 1, 0, 1, 0, 1);
    /* seen gen 5, the area unloaded and loaded again (its seen generations forgotten): record gen 5 applies */
    if (PieceSeenGenChoice(state, PieceSeenGenSkip(0, 0, 0u, 5u)) != kPaState || PieceSeenGenChoice(make, PieceSeenGenSkip(0, 0, 0u, 5u)) != kPaMake)
        Fail("a record after the area reloaded was skipped");
    /* seen gen 5, the area stayed loaded through a link loss: record gen 5 for a present piece is none */
    if (PieceSeenGenChoice(state, PieceSeenGenSkip(0, 1, 5u, 5u)) != kPaNone) Fail("a reconnect's old record undid the live piece");
    /* seen gen 5, the piece missing: record gen 5 makes it; a removal mark over a present piece removes it */
    if (PieceSeenGenChoice(make, PieceSeenGenSkip(0, 1, 5u, 5u)) != kPaMake) Fail("a missing piece was not made under a seen generation");
    if (PieceSeenGenChoice(remove, PieceSeenGenSkip(0, 1, 5u, 5u)) != kPaRemove) Fail("a removal mark was skipped under a seen generation");
    if (PieceSeenGenChoice(state, PieceSeenGenSkip(0, 1, 5u, 6u)) != kPaState) Fail("a newer record's state was skipped");
}
/* ---- the seen generation per area load, and a waiting placement (src/common/piecerec.h) ---- */
void t_t1045b5_piece_seen_load()
{
    using namespace piecerec;
    const int state = PieceApplyChoice(1, 1, 1, 1, 0, 1, 0, 1, 0, 1);
    /* seen gen 5 at load 1; the area reloaded (load 2) with the link down; record 5 for a present piece: applied */
    if (PieceSeenGenChoice(state, PieceSeenGenSkip(0, PieceSeenGenValid(1, 1u, 2u), 5u, 5u)) != kPaState) Fail("a reload during an outage kept its old seen generation");
    /* the same with no reload: none */
    if (PieceSeenGenChoice(state, PieceSeenGenSkip(0, PieceSeenGenValid(1, 1u, 1u), 5u, 5u)) != kPaNone) Fail("a seen generation of the same load did not hold");
    if (PieceSeenGenValid(0, 1u, 1u) != 0) Fail("nothing seen read as seen");
    /* a placement waits: no second PLACE; the record is noted as seen only when the waiting entry carries its state */
    if (PieceApplyChoice(1, 1, 1, 1, 0, 0, 1, 1, 0, 1) != kPaNone) Fail("a second placement was chosen");
    if (PieceWaitNoteSeen(1) != 1 || PieceWaitNoteSeen(0) != 0) Fail("a waiting placement's newer record was noted seen without its state");
}
/* ---- run T1520: own records and own pieces made from them (src/common/piecerec.h) ---- */
void t_t1045b6_piece_own_make()
{
    using namespace piecerec;
    const std::string key = "12-wall.mod@3,41@1003,-205,100";
    const std::string id = MakePieceRecId(key);
    /* an accepted own write is kept as this game's own record; a refused one is not */
    {
        std::vector<PieceFly> f; PieceSrvView v;
        PieceFlyAdd(&f, 4, 0, 500);
        if (PieceFlyMatch(f, 1, 500) != 0 || PieceFlyMatch(f, 0, 0) != 0) Fail("the accepted write was not matched");
        const std::vector<char> rec = T1045Rec(key, 4, 1, 1, kPrFlagSeed, 77, 2.5f);
        PieceHeader h; PieceBody b; int why = 0;
        if (PieceIntakeJudge(id, &rec[0], rec.size(), 0, 0u, &h, &b, &why) != kPiKeep || h.generation != 4u) Fail("the accepted own write was not kept as a record");
        PieceFlyRefused(&f, 4);
        if (PieceFlyMatch(f, 1, 500) != -1) Fail("a refused write was matched as accepted");
        (void)v;
    }
    /* an own piece missing at an area reload (its row outlived the unload) with a kept own record: made again */
    if (PieceApplyPresentLive(1, 1, 0, 0, 0, 1) != 0) Fail("a reloaded area without the own piece read it as standing");
    if (PieceApplyChoice(1, 1, 1, 1, 0, PieceApplyPresentLive(1, 1, 0, 0, 0, 1), 0, 1, 0, 1) != kPaMake) Fail("the missing own piece was not made from its record");
    if (PieceApplyPresentLive(1, 1, 0, 0, 1, 0) != 1 || PieceApplyPresentLive(1, 0, 0, 0, 0, 0) != 0 || PieceApplyPresentLive(1, 1, 0, 0, 0, 0) != -1) Fail("the own lookup");
    /* a wall made from its record takes the record's state on the STATE road (progress too); a restore keeps its own write, walls none */
    if (PieceMadeStateRoad(1, 1, 1) != kMsStateRoad || PieceMadeStateRoad(1, 1, 0) != kMsStateRoad) Fail("a piece made from its record skipped the STATE road");
    if (PieceMadeStateRoad(0, 1, 0) != kMsInline || PieceMadeStateRoad(0, 1, 1) != kMsNone || PieceMadeStateRoad(1, 0, 1) != kMsNone) Fail("the restore's own write");
    /* decision 1067 at a world load with the save's row: the record's progress, the save's materials; a reload in the same session: the record's */
    /* (args: first after a world load, own rows read, the row at hand, the record ahead) */
    if (PieceOwnMakeMats(1, 1, 1, 1) != kPmRecordSaveMats) Fail("the row's materials were not kept at a world load");
    if (PieceOwnMakeMats(1, 1, 1, 0) != kPmSave) Fail("a record behind the row won");
    if (PieceOwnMakeMats(0, 1, 1, 1) != kPmRecord || PieceOwnMakeMats(0, 0, 0, 1) != kPmRecord) Fail("a same-session reload did not take the record's materials");
    /* rows read, no row (placed and gone within the write gap): the record's materials */
    if (PieceOwnMakeMats(1, 1, 0, 1) != kPmRecord || PieceOwnMakeMats(1, 1, 0, 0) != kPmRecord) Fail("no row: the record's materials were not taken");
    /* rows not read (own records off or unreadable): the items came from the game save - no materials */
    if (PieceOwnMakeMats(1, 0, 0, 1) != kPmRecordNoMats || PieceOwnMakeMats(1, 0, 1, 1) != kPmRecordNoMats) Fail("rows not read: materials were laid");
    /* the held row is the piece's only by key and placement, both known */
    if (PieceHeldRowMatches(key, key, 77u, 77u) != 1) Fail("the piece's own held row was not matched");
    if (PieceHeldRowMatches(key, key, 78u, 77u) != 0 || PieceHeldRowMatches("x", key, 77u, 77u) != 0) Fail("another placement's row was matched");
    /* an older row with no placement number is still the piece's row: its materials, not the record's */
    if (PieceHeldRowMatches(key, key, 0u, 77u) != 1 || PieceHeldRowMatches(key, key, 0u, 0u) != 1) Fail("a row with an unknown placement was taken as no row");
    if (PieceOwnMakeMats(1, 1, PieceHeldRowMatches(key, key, 0u, 77u), 1) != kPmRecordSaveMats) Fail("an unknown-placement row did not give its materials");
    /* asks again come from events: rows not handed - the restore point; an unfinished search - the area's walk */
    if (PieceLookWhy(0, -1) != kPlRows || PieceLookWhy(-1, 1) != kPlSearch || PieceLookWhy(0, 1) != kPlNone) Fail("the reason a make waits");
    if (PieceLookDue(kPlRows, kPlEvEdge) != 1 || PieceLookDue(kPlSearch, kPlEvWalk) != 1) Fail("an event did not ask its waiting make");
    if (PieceLookDue(kPlRows, kPlEvWalk) != 0 || PieceLookDue(kPlSearch, kPlEvEdge) != 0 || PieceLookDue(kPlNone, kPlEvWalk) != 0) Fail("an event asked a make of another reason");
    /* an unfinished search asks for one more walk, at most kPieceLookRewalkMax times; rows not handed never ask for a walk */
    if (PieceLookRewalk(kPlSearch, 0) != 1 || PieceLookRewalk(kPlSearch, kPieceLookRewalkMax - 1) != 1) Fail("an unfinished search did not ask for a walk");
    if (PieceLookRewalk(kPlSearch, kPieceLookRewalkMax) != 0 || PieceLookRewalk(kPlRows, 0) != 0) Fail("the re-walk bound");
    /* an unfinished live search, or own rows not handed yet: never a make - asked again */
    if (PieceApplyLookUnknown(1, kPaMake, -1, 1) != 1 || PieceApplyLookUnknown(1, kPaMake, 0, -1) != 1) Fail("a make on an unknown look went ahead");
    if (PieceApplyLookUnknown(1, kPaMake, 0, 1) != 0 || PieceApplyLookUnknown(1, kPaRemove, -1, 1) != 0 || PieceApplyLookUnknown(1, kPaState, -1, -1) != 0) Fail("the unknown look held a known one");
    /* another player's piece missing here with no copy row and no removal mark: the real present value (-1, not looked up here) goes to
       the placement road - never held as an unknown look (run T1524) */
    {
        const int pOther = PieceApplyPresentLive(0, 0, 0, 0, 0, 0);
        if (pOther != -1 || PieceApplyPresent(0, 0, 0, 0, 0, 0) != -1) Fail("another player's piece with no copy row did not read -1");
        const int chOther = PieceApplyChoice(1, 1, 1, 1, 0, pOther, 0, 1, 0, 1);
        if (chOther != kPaMake) Fail("another player's missing piece was not made");
        if (PieceApplyLookUnknown(0, chOther, pOther, 1) != 0) Fail("another player's missing piece was held as an unknown look (T1524)");
        const int pCopy = PieceApplyPresentLive(0, 0, 1, 0, 0, 0);   /* a copy row, an unfinished search: the re-make road as before */
        if (pCopy != -1 || PieceApplyLookUnknown(0, PieceApplyChoice(1, 1, 1, 1, 0, pCopy, 0, 1, 0, 1), pCopy, 1) != 0) Fail("a copy's re-make was held");
    }
    /* an own record's state landed or dropped: the piece is marked for its look; another game's copy is not */
    if (PieceStateDoneNote(1, 1) != 1 || PieceStateDoneNote(0, 1) != 0) Fail("a landed own record's state did not mark the piece");
    /* a dropped own state marks no look: the piece's old state is never written over a newer record */
    if (PieceStateDoneNote(1, 0) != 2 || PieceStateDoneNote(0, 0) != 0) Fail("a dropped own state marked a look");
    /* rows not read: the record's progress with no materials */
    {
        coopbuild::BuildMsg st;
        st.kind = coopbuild::kBuildState; st.progress = 3.0f; st.needed = 10.0f; st.complete = 0; st.nMats = 1; st.mats[0] = 1.0f;
        PieceOwnMakeNoMats(&st);
        if (st.mats[0] != 0.0f || st.nMats != 1 || st.progress != 3.0f || st.complete != 0) Fail("no-save-row make: materials not 0, or progress not the record's");
    }
    /* the baseline after a failed lay is the piece's: no yield once the area is applied and nothing is queued */
    if (PieceFirstLookYieldPending(1, 1, 0) != 0) Fail("the first look kept yielding after the apply was done (the record's state booked as the baseline)");
    if (PieceFirstLookYieldPending(1, 1, 1) != 1 || PieceFirstLookYieldPending(1, 0, 0) != 1 || PieceFirstLookYieldPending(0, 0, 1) != 0) Fail("the yield while the apply is to come");
}
void t_t1045f2_piece_answers()
{
    using namespace piecerec;
    /* D1: removal g2 sent, the link drops before its answer; the next link's feed gives the kept copy 'g2 removal'; the rebuild's
       seed g3 is taken - the answer is the seed's, and the next removal and rebuild cost no refused update */
    {
        std::vector<PieceFly> f; PieceSrvView v;
        PieceFlyAdd(&f, 1, 0, 100);
        if (PieceFlyTaken(&f, 1, 100, &v) != 1 || v.gen != 1 || !f.empty()) Fail("a taken seed was not matched");
        PieceFlyAdd(&f, 2, 1, 105);
        PieceFlyLinkLost(&f, &v);
        if (!f.empty() || v.have != 0) Fail("a link loss kept an unanswered write or the old server view");
        if (PieceSeedDecide(1, 2, 1, v.have, v.gen, v.removed, 1) != 1) Fail("the rebuild over the kept removal mark is not a seed");
        PieceFlyAdd(&f, 3, 0, 110);
        if (PieceFlyTaken(&f, 1, 110, &v) != 1 || v.gen != 3 || v.removed != 0 || !f.empty()) Fail("the seed's answer was not matched to the seed");
        PieceFlyAdd(&f, 4, 1, 120);
        if (PieceFlyTaken(&f, 1, 120, &v) != 1 || v.gen != 4 || v.removed != 1) Fail("the removal's answer was not matched to the removal");
        if (PieceSeedDecide(1, 2, 1, v.have, v.gen, v.removed, 1) != 1) Fail("the rebuild after a taken removal is an update (refused over the removal mark)");
    }
    /* a repeat the world server answers with nothing leaves no write behind */
    {
        std::vector<PieceFly> f; PieceSrvView v;
        PieceFlyAdd(&f, 5, 0, 200);
        PieceFlyAdd(&f, 6, 1, 201);
        if (PieceFlyTaken(&f, 1, 201, &v) != 1 || v.gen != 6 || v.removed != 1 || !f.empty()) Fail("an answer was matched to a repeat answered with nothing");
        if (PieceFlyTaken(&f, 1, 200, &v) != 0 || v.gen != 6 || v.removed != 1) Fail("an answer with no write left changed the server view");
    }
    /* two writes in one second are answered in order; an answer with no stamp is the oldest write's */
    {
        std::vector<PieceFly> f; PieceSrvView v;
        PieceFlyAdd(&f, 7, 0, 300); PieceFlyAdd(&f, 8, 1, 300);
        if (PieceFlyTaken(&f, 1, 300, &v) != 1 || v.gen != 7 || f.size() != 1) Fail("the first of two writes in a second was not answered first");
        if (PieceFlyTaken(&f, 1, 300, &v) != 1 || v.gen != 8 || v.removed != 1 || !f.empty()) Fail("the second of two writes in a second");
        PieceFlyAdd(&f, 9, 0, 400); PieceFlyAdd(&f, 10, 0, 401);
        if (PieceFlyTaken(&f, 0, 0, &v) != 1 || v.gen != 9 || f.size() != 1) Fail("an answer with no stamp is not the oldest write's");
    }
    /* a refusal names its write's generation: it and the older writes are answered */
    {
        std::vector<PieceFly> f;
        PieceFlyAdd(&f, 11, 0, 500); PieceFlyAdd(&f, 12, 0, 501); PieceFlyAdd(&f, 13, 0, 502);
        if (PieceFlyRefused(&f, 12) != 1 || f.size() != 1 || f[0].gen != 13) Fail("a refusal did not drop its write and the older ones");
        if (PieceFlyRefused(&f, 99) != 0 || f.size() != 1) Fail("a refusal of no write here dropped a write");
    }
    /* at most kPieceFlyCap unanswered writes per piece: the oldest go */
    {
        std::vector<PieceFly> f;
        for (unsigned int g = 1; g <= 20; ++g) PieceFlyAdd(&f, g, 0, 600 + g);
        if (f.size() != kPieceFlyCap || f[0].gen != 5 || f[f.size() - 1].gen != 20) Fail("the unanswered-write cap");
    }
    /* a write lost with the link: an unanswered write marks the piece once at the link change; answered writes mark nothing */
    {
        std::vector<PieceFly> f; PieceSrvView v;
        PieceFlyAdd(&f, 21, 0, 700);
        if (PieceFlyLinkLost(&f, &v) != 1) Fail("a write lost with the link is not written again");
        if (PieceFlyLinkLost(&f, &v) != 0) Fail("a write lost with the link is marked again at the next link change");
        PieceFlyAdd(&f, 22, 0, 710); PieceFlyTaken(&f, 1, 710, &v);
        if (PieceFlyLinkLost(&f, &v) != 0) Fail("an answered write is written again at a link change");
        if (PieceWriteDue(1.0f, 1.0f, 10.0f, kPeAgain) != 1 || PieceEventEndsMoving(kPeAgain) != 0) Fail("the write sent again");
    }
    /* D2: a game save is not a stop - the build goes on, so nothing is written then, and the next step under a tenth writes nothing */
    if (PieceStopMoment(kPsmSave) != 0) Fail("a game save writes 'build stopped' while the build goes on");
    if (PieceStopMoment(kPsmUnload) != 1) Fail("the area's unload does not write 'build stopped'");
    if (PieceWriteDue(2.0f, 2.3f, 10.0f, kPeProgress) != 0) Fail("progress under a tenth after a save is a write");
    /* D3: a holder writes only over a live kept record that no known removal mark is at or above */
    {
        PieceSrvView s;
        PieceRefuseLearn(kPrRefuseOverRemoval, 7, &s);
        if (PieceHolderBaseOk(1, 6, 0, s.have, s.gen, s.removed) != 0) Fail("a holder writes again over a removal mark a refusal taught");
        if (PieceHolderBaseOk(1, 7, 0, 1, 7, 1) != 0) Fail("a holder writes over a removal mark at the kept generation");
        if (PieceHolderBaseOk(1, 6, 0, 1, 5, 1) != 1) Fail("an older removal mark stopped a holder's write over a newer live record");
        if (PieceHolderBaseOk(1, 6, 0, 0, 0, 0) != 1 || PieceHolderBaseOk(1, 6, 0, 1, 8, 0) != 1) Fail("a holder's write over a live kept record was stopped");
        if (PieceHolderBaseOk(0, 0, 0, 0, 0, 0) != 0 || PieceHolderBaseOk(1, 6, 1, 0, 0, 0) != 0) Fail("a holder writes with no live kept record");
    }
}


/* ---- T-889 step 2 (owner decision 862; 896 (b)): the hand-over record pp.handover (src/common/handrec.h) ---- */
hovrec::HandLine T889Line(int role, int state, int kind)
{
    hovrec::HandLine l;
    l.role = role; l.state = state; l.kind = kind;
    l.askerSlot = 2; l.holderSlot = 1; l.otherSlot = (role == hovrec::kRoleSrc) ? 2 : 1;
    l.reqId = 4000000001u; l.atUnix = 1760000000LL;
    l.where = (kind == hovrec::kKindGround) ? "g:G|ground%key|7" : (kind == hovrec::kKindBox ? "b:box key|1" : hovrec::CharWhere(123456, 1));
    l.section = (kind == hovrec::kKindGround) ? "10.500,-2.250,300.000" : "main";
    l.x = -3; l.y = 12;
    l.squadKey = (role == hovrec::kRoleDst) ? "squad.abc|def" : "";
    l.seqE = (role == hovrec::kRoleDst) ? 18446744073709551615ULL : 0ULL;
    l.frozen = hovrec::kVNone;
    l.item = std::string("\x27\x00\x00\x00\x01\xff|%\n", 9);
    if (kind == hovrec::kKindBox) { l.bagHas = 1; l.bagSection = "back,pack|%x"; l.bagX = -1; l.bagY = 7; }   /* T-889 fold 1: commas and escapes */
    if (role == hovrec::kRoleDst && kind != hovrec::kKindBox) { l.plHas = 1; l.plPre = 2; l.plGot = 5; }   /* T-889 fold 3: a placement written down */
    return l;
}
bool T889Same(const hovrec::HandLine& a, const hovrec::HandLine& b)
{
    return a.role == b.role && a.state == b.state && a.kind == b.kind && a.askerSlot == b.askerSlot && a.holderSlot == b.holderSlot
        && a.otherSlot == b.otherSlot && a.reqId == b.reqId && a.atUnix == b.atUnix && a.where == b.where && a.section == b.section
        && a.x == b.x && a.y == b.y && a.squadKey == b.squadKey && a.seqE == b.seqE && a.frozen == b.frozen && a.item == b.item
        && a.bagHas == b.bagHas && a.bagSection == b.bagSection && a.bagX == b.bagX && a.bagY == b.bagY
        && a.plHas == b.plHas && a.plPre == b.plPre && a.plGot == b.plGot;
}
/* a line with a hand-made payload (the six tab fields of a good line around it) */
std::string T889Raw(const std::string& payload) { return "q1\t17\tp2\t7\t100\t" + hovrec::HrToHex(payload); }
const char* const kT889GoodPayload = "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|-|-";

void t_t889_handrec_round_trip()
{
    const int roles[2] = { hovrec::kRoleSrc, hovrec::kRoleDst };
    for (int r = 0; r < 2; ++r)
        for (int k = 0; k < 4; ++k)
        {
            const int st = (roles[r] == hovrec::kRoleSrc) ? hovrec::kStHeld : hovrec::kStIn;
            hovrec::HandLine l = T889Line(roles[r], st, k);
            for (int fz = hovrec::kVNone; fz <= hovrec::kVUnknown; ++fz)
            {
                l.frozen = fz;
                std::string s;
                if (hovrec::HandLineFormat(l, &s) == 0) { Fail("t889: a good line would not format"); continue; }
                if (s.empty() || s[s.size() - 1] != '\n') Fail("t889: a formatted line ends with its newline");
                hovrec::HandLine p;
                if (hovrec::HandLineParse(s, &p) == 0) { Fail("t889: a formatted line would not parse"); continue; }
                if (!T889Same(l, p)) Fail("t889: the round trip changed a field");
            }
        }
    {   /* every settled state of a holder line round-trips */
        const int sts[3] = { hovrec::kStHeld, hovrec::kStClosed, hovrec::kStReturned };
        for (int i = 0; i < 3; ++i)
        {
            hovrec::HandLine l = T889Line(hovrec::kRoleSrc, sts[i], hovrec::kKindGive), p;
            std::string s;
            if (hovrec::HandLineFormat(l, &s) == 0 || hovrec::HandLineParse(s, &p) == 0 || !T889Same(l, p)) Fail("t889: a holder state round trip");
        }
    }
    /* the hand-made good payload parses (the mutations below each break one field of it) */
    {
        hovrec::HandLine p;
        if (hovrec::HandLineParse(T889Raw(kT889GoodPayload), &p) == 0) Fail("t889: the hand-made good line parses");
        else if (p.where != "c:S=5;T=1" || p.item != "\x0a\x0b" || p.otherSlot != 2 || p.reqId != 7u || p.plHas != 0) Fail("t889: the hand-made good line's fields");
    }
    /* the format refuses */
    {
        std::string s;
        hovrec::HandLine l = T889Line(hovrec::kRoleSrc, hovrec::kStHeld, hovrec::kKindTake);
        hovrec::HandLine b = l; b.askerSlot = -1; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889: format took asker -1");
        b = l; b.holderSlot = -1; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889: format took holder -1");
        b = l; b.otherSlot = -1; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889: format took other -1");
        b = l; b.state = hovrec::kStIn; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889: format took a holder line in state in");
        b = l; b.role = hovrec::kRoleDst; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889: format took a receiver line in state held");
        b = l; b.kind = 4; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889: format took kind 4");
        b = l; b.frozen = 3; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889: format took frozen 3");
        b = l; b.where = "x:1"; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889: format took where x:");
        b = l; b.where = "u:"; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889: format took an empty where");
        b = l; b.atUnix = -1; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889: format took a negative time");
        b = l; b.bagHas = 1; b.bagSection = ""; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889: format took a pack path with no section");
        b = l; b.where = "u:123456"; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889 fold 3: format took a uid where");
        b = l; b.where = "c:S=5"; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889 fold 3: format took a character id with no type");
        b = l; b.plHas = 1; b.plPre = 0; b.plGot = 1; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889 fold 3: format took a placement on a holder line");
        hovrec::HandLine d = T889Line(hovrec::kRoleDst, hovrec::kStIn, hovrec::kKindTake);
        b = d; b.plGot = b.plPre; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889 fold 3: format took a placement that added nothing");
        b = d; b.plPre = -1; if (hovrec::HandLineFormat(b, &s) != 0) Fail("t889 fold 3: format took a negative size before");
        b = d; b.plHas = 0; if (hovrec::HandLineFormat(b, &s) == 0) Fail("t889 fold 3: a receiver line with its placement not written down formats");
    }
    /* the parse refuses every malformed field */
    {
        std::string good;
        hovrec::HandLine l = T889Line(hovrec::kRoleDst, hovrec::kStIn, hovrec::kKindTake), p;
        hovrec::HandLineFormat(l, &good);
        good.erase(good.size() - 1);
        const std::string hx = good.substr(good.rfind('\t') + 1);
        const char* const outer[] = {
            "q2\t17\tp1\t4000000001\t1760000000\t", "q1\t16\tp1\t4000000001\t1760000000\t", "q1\t017\tp1\t4000000001\t1760000000\t",
            "q1\t17\tx1\t4000000001\t1760000000\t", "q1\t17\tp\t4000000001\t1760000000\t", "q1\t17\tp01\t4000000001\t1760000000\t",
            "q1\t17\tp-1\t4000000001\t1760000000\t", "q1\t17\tp1\t4294967296\t1760000000\t", "q1\t17\tp1\t\t1760000000\t",
            "q1\t17\tp1\t4000000001\t-5\t", "q1\t17\tp1\t4000000001\t17x\t", "q1\t17\tp1\t4000000001\t1760000000\textra\t",
            "q1\t17\tp1\t4000000001\t" };
        for (size_t i = 0; i < sizeof(outer) / sizeof(outer[0]); ++i)
            if (hovrec::HandLineParse(std::string(outer[i]) + hx, &p) != 0) Fail(std::string("t889: parse took a bad outer field #") + char('a' + (int)i));
        if (hovrec::HandLineParse("q1\t17\tp1\t4000000001\t1760000000\t" + hx.substr(1), &p) != 0) Fail("t889: parse took odd payload hex");
        if (hovrec::HandLineParse("q1\t17\tp1\t4000000001\t1760000000\t" + hx.substr(0, hx.size() - 2) + "zz", &p) != 0) Fail("t889: parse took a bad hex digit");
        if (hovrec::HandLineParse("q1\t17\tp1\t4000000001\t1760000000\t", &p) != 0) Fail("t889: parse took an empty payload");
        const char* const inner[] = {
            "x1|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|-|-",             /* tag: the earlier shape's x1 (T-889 fold 3) */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|-",               /* 15 fields (the fold 2 shape) */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|-|-|-",           /* 17 fields */
            "x2|srcx|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|-|-",            /* role */
            "x2|src|in|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|-|-",               /* a holder line in state in */
            "x2|dst|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|-|-",             /* a receiver line held */
            "x2|src|gone|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|-|-",             /* state */
            "x2|src|held|-2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|-|-",            /* asker */
            "x2|src|held|2|2147483648|take|c:S=5;T=1|main|0|0||0|-|0a0b|-|-",    /* holder over 2^31-1 */
            "x2|src|held|2|1|drop|c:S=5;T=1|main|0|0||0|-|0a0b|-|-",             /* kind */
            "x2|src|held|2|1|take|z:5|main|0|0||0|-|0a0b|-|-",                   /* where */
            "x2|src|held|2|1|take|c:|main|0|0||0|-|0a0b|-|-",                    /* where empty */
            "x2|src|held|2|1|take|u:5|main|0|0||0|-|0a0b|-|-",                   /* T-889 fold 3: a uid where (minted fresh at every load) */
            "x2|src|held|2|1|take|c:S=5|main|0|0||0|-|0a0b|-|-",                 /* a character id with no type */
            "x2|src|held|2|1|take|c:S=05;T=1|main|0|0||0|-|0a0b|-|-",            /* a serial with a leading zero */
            "x2|src|held|2|1|take|c:S=5;T=1x|main|0|0||0|-|0a0b|-|-",            /* a type that is not a number */
            "x2|src|held|2|1|take|c:S=5;T=1%7|main|0|0||0|-|0a0b|-|-",           /* a cut escape */
            "x2|src|held|2|1|take|c:S=5;T=1%zz|main|0|0||0|-|0a0b|-|-",          /* a bad escape */
            "x2|src|held|2|1|take|c:S=5;T=1|ma\x01in|0|0||0|-|0a0b|-|-",         /* a raw control character */
            "x2|src|held|2|1|take|c:S=5;T=1|main|01|0||0|-|0a0b|-|-",            /* x leading zero */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|-0||0|-|0a0b|-|-",            /* y -0 */
            "x2|src|held|2|1|take|c:S=5;T=1|main|abc|0||0|-|0a0b|-|-",           /* x */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|2147483648||0|-|0a0b|-|-",    /* y out of range */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||x|-|0a0b|-|-",             /* seqE */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||18446744073709551616|-|0a0b|-|-",   /* seqE over 2^64-1 */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|3|0a0b|-|-",             /* frozen */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0||0a0b|-|-",              /* frozen empty */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0|-|-",              /* item odd hex */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0g|-|-",               /* item bad hex */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|1,2|-",                  /* T-889 fold 1: a pack path with no section */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|1,2,|-",                 /* an empty pack section */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|x,2,main|-",             /* pack x */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|01,2,main|-",            /* pack x leading zero */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|1,2,ma%zz|-",            /* a bad escape in the pack section */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b||-",                     /* an empty pack field */
            "x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|-|0,1",                  /* T-889 fold 3: a placement on a holder line */
            "x2|dst|in|2|1|take|c:S=5;T=1|main|0|0||1|-|0a0b|-|2,2",                    /* a placement that added nothing */
            "x2|dst|in|2|1|take|c:S=5;T=1|main|0|0||1|-|0a0b|-|-1,2",                   /* a size before below 0 */
            "x2|dst|in|2|1|take|c:S=5;T=1|main|0|0||1|-|0a0b|-|a,2",                    /* a size that is not a number */
            "x2|dst|in|2|1|take|c:S=5;T=1|main|0|0||1|-|0a0b|-|3",                      /* no comma */
            "x2|dst|in|2|1|take|c:S=5;T=1|main|0|0||1|-|0a0b|-|01,3",                   /* a leading zero */
            "x2|dst|in|2|1|take|c:S=5;T=1|main|0|0||1|-|0a0b|-|" };                     /* an empty placed field */
        for (size_t i = 0; i < sizeof(inner) / sizeof(inner[0]); ++i)
            if (hovrec::HandLineParse(T889Raw(inner[i]), &p) != 0) Fail(std::string("t889: parse took a bad payload field #") + char('a' + (int)i));
        /* the ledger's and the queue's lines are not hand-over lines */
        if (hovrec::HandLineParse("q1\t16\tp1\t5\t100\t6831", &p) != 0 || hovrec::HandLineParse("q1\t0\tbox\t5\t100\t", &p) != 0) Fail("t889: parse took another family");
        /* a CR LF ending is stripped */
        if (hovrec::HandLineParse(good + "\r\n", &p) == 0 || !T889Same(l, p)) Fail("t889: a CRLF line parses as the same line");
        /* T-889 fold 1: a pack path round-trips, its section's commas and escapes included */
        if (hovrec::HandLineParse(T889Raw("x2|src|held|2|1|take|c:S=5;T=1|main|0|0||0|-|0a0b|-3,4,a,b%7Cc|-"), &p) == 0 || p.bagHas != 1 || p.bagX != -3
            || p.bagY != 4 || p.bagSection != "a,b|c") Fail("t889: a hand-made pack path parses");
        /* T-889 fold 3: a hand-made placement, a line naming no character and a negative serial parse */
        if (hovrec::HandLineParse(T889Raw("x2|dst|in|2|1|take|c:S=-7;T=1|main|4|1||1|-|0a0b|-|0,3"), &p) == 0 || p.plHas != 1 || p.plPre != 0
            || p.plGot != 3 || p.where != "c:S=-7;T=1" || p.x != 4 || p.y != 1) Fail("t889 fold 3: a hand-made placement parses");
        if (hovrec::HandLineParse(T889Raw("x2|dst|in|2|1|give|c:?|main|0|0||1|-|0a0b|-|-"), &p) == 0 || p.where != "c:?" || p.plHas != 0)
            Fail("t889 fold 3: a line naming no character (c:?) parses");
    }
}

void t_t889_holds_after_load()
{
    struct Row { unsigned long long applied, mark, e; int want; };
    const Row rows[] = {
        { 0, 0, 0, 0 },     /* no E known */
        { 9, 9, 0, 0 },
        { 5, 0, 5, 1 },     /* the loaded record is the one at E */
        { 6, 0, 5, 1 },
        { 4, 0, 5, 0 },     /* the loaded record predates E */
        { 0, 5, 5, 1 },     /* the save was asked for at E (after the placement, E reserved there) */
        { 0, 4, 5, 0 },     /* E-1: a save asked for BEFORE the placement - not placed */
        { 4, 4, 5, 0 },
        { 0, 18446744073709551615ULL, 18446744073709551615ULL, 1 } };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (hovrec::HoldsAfterLoad(rows[i].applied, rows[i].mark, rows[i].e) != rows[i].want) Fail(std::string("t889: HoldsAfterLoad row ") + char('a' + (int)i));
}

void t_t889_verdict()
{
    using namespace hovrec;
    struct Row { int haveIn, frozen, live, holds, squad, markRule, writer, readable, want; };
    const Row rows[] = {
        { 1, kVNone, 1, 0, 1, 1, 1, 1, kVHolds },     /* a live placement */
        { 1, kVNone, 0, 1, 1, 1, 1, 1, kVHolds },     /* after a load, the record holds it */
        { 1, kVNone, 0, 0, 1, 0, 1, 1, kVUnknown },   /* T-889 fold 1 (review 2): after a load with NO mark (quicksave-time / all rule) the record
                                                         and the mark both below E say nothing - the save itself may hold it (was kVNot: a double) */
        { 1, kVNone, 0, 1, 0, 1, 1, 1, kVUnknown },   /* no squad record named (a box): unknown */
        { 1, kVNot, 1, 1, 1, 1, 1, 1, kVNot },        /* frozen wins over everything */
        { 1, kVHolds, 0, 0, 1, 1, 1, 1, kVHolds },
        { 1, kVUnknown, 0, 1, 1, 1, 1, 1, kVHolds },  /* a frozen 2 is not a frozen answer */
        { 0, kVNone, 0, 0, 1, 1, 1, 1, kVNot },       /* no in line, writer on, file read: not placed */
        { 0, kVNone, 0, 0, 1, 1, 0, 1, kVUnknown },   /* writer off */
        { 0, kVNone, 0, 0, 1, 1, 1, 0, kVUnknown },   /* the file could not be read */
        { 1, kVNone, 1, 1, 1, 1, 1, 0, kVUnknown },
        { 1, kVNone, 0, 0, 1, 1, 1, 1, kVNot },       /* under the MARK rule, record and mark below E: the save predates the placement - not placed */
        { 1, kVNone, 0, 1, 1, 0, 1, 1, kVHolds },     /* no mark, but the applied record is at E: holds */
        { 0, kVNone, 0, 0, 1, 0, 1, 1, kVNot } };     /* no in line at all: not placed, whatever the rule */
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (Verdict(rows[i].haveIn, rows[i].frozen, rows[i].live, rows[i].holds, rows[i].squad, rows[i].markRule, rows[i].writer, rows[i].readable) != rows[i].want)
            Fail(std::string("t889: Verdict row ") + char('a' + (int)i));
    /* review 7: an unknown with an `in` line is settled by looking; review 6: a game with no lines answers from memory */
    if (ResolveUnknownLive(kVUnknown, 1, 1) != kVHolds || ResolveUnknownLive(kVUnknown, 1, 0) != kVNot || ResolveUnknownLive(kVUnknown, 1, -1) != kVUnknown)
        Fail("t889: an unknown in line is settled by the live look (present 1, loaded and absent 0, not loaded 2)");
    if (ResolveUnknownLive(kVUnknown, 0, 1) != kVUnknown || ResolveUnknownLive(kVNot, 1, 1) != kVNot || ResolveUnknownLive(kVHolds, 1, 0) != kVHolds)
        Fail("t889: the live look changes only an unknown answer about an in line");
    if (VerdictNoLines(1) != kVHolds || VerdictNoLines(0) != kVUnknown) Fail("t889: a game with no lines answers holds only for its own live placement");
}

void t_t889_freeze()
{
    using namespace hovrec;
    /* the first after-load look freezes a 0 / 1; a live answer, a 2, an already frozen line, or no line does not */
    if (VerdictFreezes(1, kVNone, 0, kVNot) != 1 || VerdictFreezes(1, kVNone, 0, kVHolds) != 1) Fail("t889: an after-load 0 / 1 freezes");
    if (VerdictFreezes(1, kVNone, 1, kVHolds) != 0) Fail("t889: a live answer does not freeze");
    if (VerdictFreezes(1, kVNone, 0, kVUnknown) != 0) Fail("t889: a 2 does not freeze");
    if (VerdictFreezes(1, kVNot, 0, kVNot) != 0 || VerdictFreezes(0, kVNone, 0, kVNot) != 0) Fail("t889: frozen or absent does not freeze again");
    /* a later seq does not flip it: frozen 0, and the record now reads as holding */
    const int first = Verdict(1, kVNone, 0, HoldsAfterLoad(4, 4, 5), 1, 1, 1, 1);
    if (first != kVNot) Fail("t889: the first look says not placed");
    HandLine l = T889Line(kRoleDst, kStIn, kKindTake);
    l.seqE = 5;
    std::vector<std::string> f;
    if (VerdictFreezes(1, l.frozen, 0, first) != 0) l.frozen = first;
    if (HandUpsert(&f, l, kHandCap) != 1) Fail("t889: the frozen line is written");
    HandLine back;
    if (HandFind(f, l.askerSlot, l.reqId, l.holderSlot, kRoleDst, &back) != 0) Fail("t889: the frozen line is found");
    if (Verdict(1, back.frozen, 0, HoldsAfterLoad(9, 9, back.seqE), 1, 1, 1, 1) != kVNot) Fail("t889: a later seq flipped a frozen verdict");
    /* and the other way */
    l.frozen = kVHolds;
    if (Verdict(1, l.frozen, 0, HoldsAfterLoad(0, 0, l.seqE), 1, 1, 1, 1) != kVHolds) Fail("t889: a frozen holds stays holds");
}

void t_t889_writer_hold()
{
    using namespace hovrec;
    if (CharUnpublished(0, 0, 0) != 0) Fail("t889: nothing unpublished writes");
    if (CharUnpublished(1, 0, 0) != 1 || CharUnpublished(0, 1, 0) != 1 || CharUnpublished(0, 0, 1) != 1 || CharUnpublished(1, 1, 1) != 1)
        Fail("t889: a ring move out of the character, an undrained pack move or a split slot holds the write");
    /* CHANGE B (896 b): only a GIVE row (not escrow) whose giver is away releases the syncing */
    if (GiveRowHoldsSync(1, 0, 0) != 0) Fail("t889: a give row with its giver away releases");
    if (GiveRowHoldsSync(1, 0, 1) != 1) Fail("t889: a give row with its giver here holds");
    if (GiveRowHoldsSync(0, 0, 0) != 1 || GiveRowHoldsSync(0, 1, 0) != 1) Fail("t889: a take row holds, giver or not");
    if (GiveRowHoldsSync(1, 1, 0) != 1) Fail("t889: an escrow row holds");
}

void t_t889_actions()
{
    using namespace hovrec;
    if (HolderAction(kStHeld, kVHolds, 1, kHsLacks, -1) != kActClose || HolderAction(kStHeld, kVHolds, 0, kHsLacks, -1) != kActClose) Fail("t889: holds closes");
    if (HolderAction(kStHeld, kVNot, 1, kHsHas, 1) != kActRestoreLive) Fail("t889: not placed with the object kept restores it live");
    if (HolderAction(kStHeld, kVNot, 0, kHsLacks, -1) != kActRebuild) Fail("t889: not placed with no object, the holder's state lacking it, rebuilds");
    for (int hs = kHsLacks; hs <= kHsUnknown; ++hs) for (int lv = -1; lv <= 1; ++lv)
        if (HolderAction(kStHeld, kVUnknown, 1, hs, lv) != kActKeep || HolderAction(kStHeld, kVUnknown, 0, hs, lv) != kActKeep) Fail("t889: unknown keeps the line");
    if (HolderAction(kStClosed, kVNot, 0, kHsLacks, 0) != kActKeep || HolderAction(kStReturned, kVHolds, 0, kHsHas, 1) != kActKeep) Fail("t889: a settled line takes no action");
    /* T-889 fold 1 (review 3): after a reload with no live object the holder's own state decides */
    if (HolderAction(kStHeld, kVNot, 0, kHsHas, -1) != kActCloseNoRebuild) Fail("t889: not placed, our reloaded state has it: closed, NOT rebuilt (a double before)");
    if (HolderAction(kStHeld, kVNot, 0, kHsUnknown, 1) != kActCloseNoRebuild) Fail("t889: not placed, state unknown, the item found here: no rebuild");
    if (HolderAction(kStHeld, kVNot, 0, kHsUnknown, 0) != kActRebuild) Fail("t889: not placed, state unknown, not found here: rebuilt");
    if (HolderAction(kStHeld, kVNot, 0, kHsUnknown, -1) != kActKeep) Fail("t889: not placed, state unknown, its place not loaded: wait");
    if (HolderAction(kStHeld, kVHolds, 0, kHsHas, 1) != kActRemoveThenClose) Fail("t889: holds, our reloaded state has it: our copy removed, then closed");
    if (HolderAction(kStHeld, kVHolds, 0, kHsHas, -1) != kActKeep) Fail("t889: holds, our state has it but not loaded: wait to remove it");
    if (HolderAction(kStHeld, kVHolds, 0, kHsHas, 0) != kActClose) Fail("t889: holds, our state has it but it is not there any more: closed");
    if (HolderAction(kStHeld, kVHolds, 0, kHsUnknown, 1) != kActRemoveThenClose || HolderAction(kStHeld, kVHolds, 0, kHsUnknown, 0) != kActClose
        || HolderAction(kStHeld, kVHolds, 0, kHsUnknown, -1) != kActKeep) Fail("t889: holds, our state unknown: the copy at its square removed / closed / wait");
    /* the receiver on CLOSED */
    if (ReceiverOnClosed(0, 2, 1, kKindTake) != kRcNone) Fail("t889: no in line, nothing");
    if (ReceiverOnClosed(1, 1, 1, kKindTake) != kRcDrop) Fail("t889: CLOSED(closed) drops the line, the copy stays");
    if (ReceiverOnClosed(1, 2, 1, kKindTake) != kRcRemoveCopy || ReceiverOnClosed(1, 2, 1, kKindBox) != kRcRemoveCopy
        || ReceiverOnClosed(1, 2, 1, kKindGround) != kRcRemoveCopy) Fail("t889: CLOSED(returned) removes a provisional copy");
    if (ReceiverOnClosed(1, 2, 0, kKindTake) != kRcDrop) Fail("t889: CLOSED(returned) with no copy here drops the line");
    if (ReceiverOnClosed(1, 2, 1, kKindGive) != kRcDrop) Fail("t889: a give's builder never removes on CLOSED (the REVOKE does)");
    /* T-889 fold 1: CLOSED 3 (the holder keeps no lines - review 6) drops the line; CLOSED 4 (unknown - review 10) keeps it and the copy */
    if (ReceiverOnClosed(1, kClosedNoLines, 1, kKindTake) != kRcDrop || ReceiverOnClosed(1, kClosedNoLines, 0, kKindBox) != kRcDrop) Fail("t889: CLOSED 3 drops the line");
    if (ReceiverOnClosed(1, kClosedUnknown, 1, kKindTake) != kRcKeep || ReceiverOnClosed(1, kClosedUnknown, 0, kKindBox) != kRcKeep) Fail("t889: CLOSED 4 keeps the line");
    if (ReceiverOnClosed(1, kClosedUnknown, 1, kKindGive) != kRcDrop || ReceiverOnClosed(1, kClosedUnknown, 0, kKindGive) != kRcDrop) Fail("t1181: CLOSED 4 ends a give's line, the item stays (owner 1365)");
    if (ReceiverOnClosed(0, kClosedUnknown, 1, kKindTake) != kRcNone) Fail("t889: no line, nothing");
    /* the holder on a receiver's QUERY: HolderAnswer(haveLine, state, memValue, rowLive, writerOn) */
    if (HolderAnswer(1, kStHeld, 0, 0, 1) != 0 || HolderAnswer(1, kStHeld, 1, 1, 1) != 0) Fail("t889: a held line answers nothing");
    if (HolderAnswer(1, kStClosed, 0, 0, 1) != kClosedPlaced) Fail("t889: closed -> CLOSED 1");
    if (HolderAnswer(1, kStReturned, 0, 0, 1) != kClosedReturned) Fail("t889: returned -> CLOSED 2");
    if (HolderAnswer(0, kStHeld, 0, 0, 1) != kClosedUnknown) Fail("t889: review 10 - a missing line is NOT answered closed (it was CLOSED 1: a kept provisional copy)");
    if (HolderAnswer(0, kStHeld, kClosedReturned, 0, 1) != kClosedReturned || HolderAnswer(0, kStHeld, kClosedPlaced, 0, 0) != kClosedPlaced)
        Fail("t889: a missing line settled in this process answers from memory");
    if (HolderAnswer(0, kStHeld, 0, 1, 1) != 0 || HolderAnswer(0, kStHeld, 0, 1, 0) != 0) Fail("t889: a live row answers nothing (its live answer comes)");
    if (HolderAnswer(0, kStHeld, 0, 0, 0) != kClosedNoLines) Fail("t889: review 6 - a holder with its writer off says it keeps no lines");
    /* the re-ask clock */
    if (QueryDue(1000u, 0, 0u, 1000u + kQueryFirstMs - 1u) != 0 || QueryDue(1000u, 0, 0u, 1000u + kQueryFirstMs) != 1) Fail("t889: the first ask waits kQueryFirstMs");
    if (QueryDue(0u, 1, 50000u, 50000u + kQueryEveryMs - 1u) != 0 || QueryDue(0u, 1, 50000u, 50000u + kQueryEveryMs) != 1) Fail("t889: re-asked every 60 s");
    if (QueryDue(0xFFFFFF00u, 1, 0xFFFFFF00u, 0xFFFFFF00u + kQueryEveryMs) != 1) Fail("t889: the clock wraps");
}

void t_t889_file_cap()
{
    using namespace hovrec;
    std::vector<std::string> f;
    f.push_back("a line of a later format");   /* kept as it is, never dropped */
    for (unsigned int i = 0; i < kHandCap - 1; ++i)
    {
        HandLine l = T889Line(kRoleSrc, kStHeld, kKindTake);
        l.reqId = i + 1;
        l.otherSlot = 2 + (int)(i % 4);   /* T-1181: four other players, none past kHandPerOther (64, 64, 64, 63) */
        if (HandUpsert(&f, l, kHandCap) != 1) { Fail("t889: an open line under the cap is written"); return; }
    }
    if (f.size() != kHandCap) Fail("t889: the file holds the cap");
    HandLine extra = T889Line(kRoleDst, kStIn, kKindTake);
    extra.reqId = 999999u;
    const std::vector<std::string> before = f;
    if (HandUpsert(&f, extra, kHandCap) != -1) Fail("t889: an open line that does not fit is refused (lineFull)");
    if (f != before) Fail("t889: a refused line leaves the file as it was");
    /* replacing a line in place always fits */
    HandLine r = T889Line(kRoleSrc, kStClosed, kKindTake);
    r.reqId = 1;
    if (HandUpsert(&f, r, kHandCap) != 1 || f.size() != kHandCap) Fail("t889: a line replaced in place");
    HandLine r2 = T889Line(kRoleSrc, kStReturned, kKindTake);
    r2.reqId = 2;
    HandUpsert(&f, r2, kHandCap);
    /* now two settled lines: the next open line drops the OLDEST settled one only */
    if (HandUpsert(&f, extra, kHandCap) != 1) Fail("t889: a settled line makes room");
    if (f.size() != kHandCap) Fail("t889: still at the cap");
    if (HandFind(f, r.askerSlot, 1u, r.holderSlot, kRoleSrc, 0) >= 0) Fail("t889: the oldest settled line went first");
    if (HandFind(f, r2.askerSlot, 2u, r2.holderSlot, kRoleSrc, 0) < 0) Fail("t889: only one line was dropped");
    if (f[0] != "a line of a later format") Fail("t889: an unknown line is kept");
    if (HandFind(f, extra.askerSlot, extra.reqId, extra.holderSlot, kRoleDst, 0) < 0) Fail("t889: the new line is in");
    /* the role is part of the key: the receiver's line and the holder's line of one transfer are two lines */
    if (HandFind(f, extra.askerSlot, extra.reqId, extra.holderSlot, kRoleSrc, 0) >= 0) Fail("t889: role is part of the key");
    if (HandRemove(&f, extra.askerSlot, extra.reqId, extra.holderSlot, kRoleDst) != 1 || HandRemove(&f, extra.askerSlot, extra.reqId, extra.holderSlot, kRoleDst) != 0)
        Fail("t889: HandRemove removes once");
    if (HandMaxSeq(f) != 0ULL) Fail("t889: holder lines carry no seq");
    {   /* T-889 fold 1 (review 10): a RETURNED line is never dropped by the cap (kept until its CLOSED 2 is acknowledged) */
        std::vector<std::string> g;
        for (unsigned int i = 0; i < kHandCap; ++i)
        {
            HandLine rl = T889Line(kRoleSrc, kStReturned, kKindTake);
            rl.reqId = 10u + i;
            if (HandUpsert(&g, rl, kHandCap) != 1) { Fail("t889: a returned line under the cap is written"); break; }
        }
        HandLine more = T889Line(kRoleSrc, kStHeld, kKindTake);
        more.reqId = 5u;
        if (HandUpsert(&g, more, kHandCap) != -1 || g.size() != kHandCap) Fail("t889: returned lines are not dropped to make room");
        if (CapDrops(kStClosed) != 1 || CapDrops(kStReturned) != 0 || CapDrops(kStHeld) != 0 || CapDrops(kStIn) != 0) Fail("t889: only closed lines drop");
    }
    HandLine d = T889Line(kRoleDst, kStIn, kKindTake);
    d.reqId = 77; d.seqE = 4242;
    HandUpsert(&f, d, kHandCap);
    if (HandMaxSeq(f) != 4242ULL) Fail("t889: HandMaxSeq");
    std::string text;
    for (size_t i = 0; i < f.size(); ++i) text += f[i] + "\r\n";
    std::vector<std::string> back;
    int bad = 0;
    HandSplit(text, &back, &bad);
    if (back != f || bad != 1) Fail("t889: HandSplit keeps every line and counts the unknown one");
    {   /* T-1181: AT MOST kHandPerOther OPEN LINES PER OTHER PLAYER - one player's never-ending lines cannot fill the file for all */
        if (kHandPerOther != 64 || kHandCap != 256) Fail("T-1181: 64 open lines per other player, 256 in the file");
        std::vector<std::string> g;
        for (unsigned int i = 0; i < kHandPerOther; ++i)
        {
            HandLine l = T889Line(kRoleSrc, kStHeld, kKindTake);
            l.otherSlot = 7; l.reqId = 100u + i;
            if (HandUpsert(&g, l, kHandCap) != 1) { Fail("T-1181: an open line under the per-player limit is written"); return; }
        }
        if (HandOpenWith(g, 7, -1) != kHandPerOther || HandOpenWith(g, 8, -1) != 0 || HandOpenWith(g, 7, 0) != kHandPerOther - 1)
            Fail("T-1181: HandOpenWith counts one player's open lines");
        const std::vector<std::string> before7 = g;
        HandLine one = T889Line(kRoleSrc, kStHeld, kKindGive);
        one.otherSlot = 7; one.reqId = 500u;
        if (HandUpsert(&g, one, kHandCap) != -2) Fail("T-1181: a new open line with a player at 64 is refused (peerFull)");
        HandLine in7 = T889Line(kRoleDst, kStIn, kKindGive);
        in7.otherSlot = 7; in7.reqId = 501u;
        if (HandUpsert(&g, in7, kHandCap) != -2) Fail("T-1181: a receiver's in line counts as open too");
        if (g != before7) Fail("T-1181: a refused line leaves the file as it was");
        HandLine other = T889Line(kRoleSrc, kStHeld, kKindGive);
        other.otherSlot = 8; other.reqId = 502u;
        if (HandUpsert(&g, other, kHandCap) != 1) Fail("T-1181: a hand-over with another player still goes");
        HandLine again = T889Line(kRoleSrc, kStHeld, kKindTake);
        again.otherSlot = 7; again.reqId = 100u; again.x = 9;
        if (HandUpsert(&g, again, kHandCap) != 1) Fail("T-1181: an open line already on file is always rewritten");
        HandLine settled = T889Line(kRoleSrc, kStClosed, kKindGive);
        settled.otherSlot = 7; settled.reqId = 600u;
        if (HandUpsert(&g, settled, kHandCap) != 1) Fail("T-1181: a settled line is written with a player at 64");
        HandLine end = T889Line(kRoleSrc, kStReturned, kKindTake);
        end.otherSlot = 7; end.reqId = 101u;
        if (HandUpsert(&g, end, kHandCap) != 1) Fail("T-1181: an open line settles in place");
        if (HandUpsert(&g, one, kHandCap) != 1) Fail("T-1181: one open line ended - the next new one fits");
        if (HandUpsert(&g, in7, kHandCap) != -2) Fail("T-1181: back at 64 - refused again");
        {   /* T-1181 review: a give's receiver line (HovOwnerIn) takes no per-player limit - its item is built already */
            std::vector<std::string> g2 = g;
            if (HandUpsert(&g2, in7, kHandCap, kHandPerOtherOff) != 1 || HandOpenWith(g2, 7, -1) != kHandPerOther + 1)
                Fail("T-1181 review: a give's receiver line is written with that player at 64 open lines");
            std::vector<std::string> two;
            HandLine o1 = T889Line(kRoleSrc, kStHeld, kKindTake), o2 = T889Line(kRoleSrc, kStHeld, kKindTake);
            o1.otherSlot = 8; o1.reqId = 900u; o2.otherSlot = 9; o2.reqId = 901u;
            HandUpsert(&two, o1, 2); HandUpsert(&two, o2, 2);
            if (HandUpsert(&two, in7, 2, kHandPerOtherOff) != -1 || two.size() != 2) Fail("T-1181 review: the file cap still holds for a give's receiver line");
        }
        for (unsigned int i = 0; i < 70; ++i)
        {   /* returned lines are settled: they never count toward the per-player limit (the file cap keeps them) */
            HandLine rl = T889Line(kRoleSrc, kStReturned, kKindTake);
            rl.otherSlot = 20; rl.reqId = 700u + i;
            if (HandUpsert(&g, rl, kHandCap) != 1) { Fail("T-1181: settled lines are not limited per player"); break; }
        }
        HandLine o20 = T889Line(kRoleSrc, kStHeld, kKindTake);
        o20.otherSlot = 20; o20.reqId = 800u;
        if (HandUpsert(&g, o20, kHandCap) != 1) Fail("T-1181: settled lines do not use up a player's open lines");
        /* the overall cap stays, and settled lines are still the first pushed out */
        std::vector<std::string> h;
        HandLine c0 = T889Line(kRoleSrc, kStClosed, kKindTake);
        c0.otherSlot = 9; c0.reqId = 1u;
        HandUpsert(&h, c0, kHandCap);
        for (unsigned int i = 0; i < kHandCap - 1; ++i)
        {
            HandLine l = T889Line(kRoleSrc, kStHeld, kKindTake);
            l.otherSlot = 10 + (int)(i % 4); l.reqId = 10u + i;   /* 64, 64, 64, 63 */
            if (HandUpsert(&h, l, kHandCap) != 1) { Fail("T-1181: the file fills to the cap over four players"); return; }
        }
        HandLine n13 = T889Line(kRoleSrc, kStHeld, kKindTake);
        n13.otherSlot = 13; n13.reqId = 5000u;
        if (HandUpsert(&h, n13, kHandCap) != 1 || h.size() != kHandCap) Fail("T-1181: a full file - the settled line makes room");
        if (HandFind(h, c0.askerSlot, 1u, c0.holderSlot, kRoleSrc, 0) >= 0) Fail("T-1181: the settled line was pushed out first");
        HandLine m13 = T889Line(kRoleSrc, kStHeld, kKindTake);
        m13.otherSlot = 13; m13.reqId = 5001u;
        if (HandUpsert(&h, m13, kHandCap) != -2) Fail("T-1181: that player is at 64 now - peerFull");
        HandLine n14 = T889Line(kRoleSrc, kStHeld, kKindTake);
        n14.otherSlot = 14; n14.reqId = 5002u;
        if (HandUpsert(&h, n14, kHandCap) != -1 || h.size() != kHandCap) Fail("T-1181: the 256 cap stays (lineFull)");
    }
}

/* T-1181: lines this build cannot read move to the side file pp.handover.unread - kept there (written once), never deleted, and no
   longer counted toward the caps (the pure part: what moves and the side file's text; HovLoad / HovMoveUnread do the file work) */
void t_t1181_unread_side()
{
    using namespace hovrec;
    if (HandSideFile() == HandFile() || HandSideFile().compare(0, HandFile().size(), HandFile()) != 0) Fail("T-1181: the side file sits beside pp.handover");
    HandLine a = T889Line(kRoleSrc, kStHeld, kKindTake);
    a.reqId = 11u;
    HandLine b = T889Line(kRoleSrc, kStHeld, kKindGive);   /* open: the cap cannot drop it either */
    b.reqId = 12u;
    std::string sa, sb;
    HandLineFormat(a, &sa); HandLineFormat(b, &sb);
    sa.erase(sa.size() - 1); sb.erase(sb.size() - 1);
    const std::string junk1 = "a line of a later format", junk2 = "q1\t17\tp2\t5\t0\tzz";
    std::vector<std::string> f;
    f.push_back(junk1); f.push_back(sa); f.push_back(junk2); f.push_back(sb);
    /* while they stay, they hold places the cap never frees */
    HandLine c = T889Line(kRoleSrc, kStHeld, kKindTake);
    c.reqId = 13u;
    std::vector<std::string> g = f;
    if (HandUpsert(&g, c, 4) != -1) Fail("T-1181: unreadable lines fill the cap while they stay");
    std::vector<std::string> unread;
    if (HandTakeUnread(&f, &unread) != 2) Fail("T-1181: two unreadable lines move");
    if (f.size() != 2 || f[0] != sa || f[1] != sb) Fail("T-1181: the readable lines stay, in order");
    if (unread.size() != 2 || unread[0] != junk1 || unread[1] != junk2) Fail("T-1181: the unreadable lines, in order");
    if (HandUpsert(&f, c, 3) != 1 || f.size() != 3) Fail("T-1181: moved out, they no longer count toward the cap");
    std::vector<std::string> none;
    if (HandTakeUnread(&f, &none) != 0 || !none.empty() || f.size() != 3) Fail("T-1181: nothing readable moves");
    int added = -1;
    const std::string side = HandSideText("", unread, &added);
    if (side != junk1 + "\n" + junk2 + "\n" || added != 2) Fail("T-1181: a new side file holds the moved lines");
    if (HandSideText(side, unread, &added) != side || added != 0) Fail("T-1181: a line moved again is written once");
    if (HandSideText("an older one", unread, &added) != "an older one\n" + junk1 + "\n" + junk2 + "\n" || added != 2)
        Fail("T-1181: the side file keeps what it held (nothing is taken out)");
    std::vector<std::string> more;
    more.push_back(junk2); more.push_back("q1\t99");
    if (HandSideText(side + "\r\n", more, &added) != side + "\r\n" + "q1\t99\n" || added != 1) Fail("T-1181: only lines it does not hold are appended");
    {   /* T-1181 review: two identical unreadable lines moved together are both kept; moved again, each is matched once */
        std::vector<std::string> twin;
        twin.push_back(junk1); twin.push_back(junk1);
        const std::string s2 = HandSideText("", twin, &added);
        if (s2 != junk1 + "\n" + junk1 + "\n" || added != 2) Fail("T-1181 review: two identical lines moved in one pass are both written");
        if (HandSideText(s2, twin, &added) != s2 || added != 0) Fail("T-1181 review: both moved again - written once each");
        if (HandSideText(junk1 + "\n", twin, &added) != junk1 + "\n" + junk1 + "\n" || added != 1) Fail("T-1181 review: one already held - only the other is added");
    }
}

/* T-1181 handtest lever (TEST-ONLY): its lines name a holder slot no game holds, stay open and meet the same caps; its unreadable line
   is one the parser refuses and the move takes; clear-test takes out only the lever's own lines; count's tally */
void t_t1181_handtest_lever()
{
    using namespace hovrec;
    if (kHandTestSlot <= 1023) Fail("T-1181 handtest: the lever's holder slot is above every slot a game can hold (slotwire.h kSlotMax 1023)");
    const HandLine d = HandTestLine(kRoleDst, 3, 1u, 1760000000LL), s = HandTestLine(kRoleSrc, 3, 2u, 1760000000LL);
    std::string sd, ss;
    HandLine pd, ps;
    if (HandLineFormat(d, &sd) == 0 || HandLineFormat(s, &ss) == 0 || HandLineParse(sd, &pd) == 0 || HandLineParse(ss, &ps) == 0)
    { Fail("T-1181 handtest: the lever's lines format and read back"); return; }
    if (pd.state != kStIn || ps.state != kStHeld || IsOpen(pd.state) == 0 || IsOpen(ps.state) == 0 || pd.otherSlot != 3 || pd.reqId != 1u)
        Fail("T-1181 handtest: dst = an open in line, src = an open held line, naming that player");
    if (HandIsTest(pd) == 0 || HandIsTest(ps) == 0 || HandIsTest(T889Line(kRoleSrc, kStHeld, kKindGive)) != 0) Fail("T-1181 handtest: only the lever's lines are its own");
    if (!pd.item.empty() || pd.seqE != 0ULL || !pd.squadKey.empty()) Fail("T-1181 handtest: no item bytes, no E, no squad record");
    /* the caps apply as to a real line */
    std::vector<std::string> g;
    HandLine real = T889Line(kRoleSrc, kStHeld, kKindTake);
    real.otherSlot = 3;
    HandUpsert(&g, real, kHandCap);
    for (unsigned int i = 1; i < kHandPerOther; ++i)
        if (HandUpsert(&g, HandTestLine(kRoleDst, 3, i, 0), kHandCap) != 1) { Fail("T-1181 handtest: fill writes under the limit"); return; }
    if (HandUpsert(&g, HandTestLine(kRoleDst, 3, kHandPerOther, 0), kHandCap) != -2) Fail("T-1181 handtest: the 65th open line with that player is refused (peerFull)");
    if (HandUpsert(&g, HandTestLine(kRoleDst, 4, kHandPerOther, 0), kHandCap) != 1) Fail("T-1181 handtest: another player's line still goes");
    /* the unreadable line */
    const std::string u = HandTestUnreadLine(70u, 1760000000LL);
    HandLine bad;
    if (HandLineParse(u, &bad) != 0) Fail("T-1181 handtest: the lever's unreadable line is refused by the parser");
    if (HandTestUnreadNo(u) != 70u || HandTestUnreadNo(u + "\r") != 70u) Fail("T-1181 handtest: its number reads back");
    if (HandTestUnreadNo("q1\t17\tp2\t5\t0\tzz") != 0 || HandTestUnreadNo(sd) != 0 || HandTestUnreadNo(HandTestUnreadLine(0u, 0)) != 0)
        Fail("T-1181 handtest: no other line is taken for one of its unreadable lines");
    g.push_back(u);
    const std::string junk = "a line of a later format";
    g.push_back(junk);
    if (HandTestNextId(g) != 71u || HandTestNextId(std::vector<std::string>()) != 1u) Fail("T-1181 handtest: the next id is one past the lever's largest");
    std::vector<std::string> moved, h = g;
    if (HandTakeUnread(&h, &moved) != 2 || moved[0] != u) Fail("T-1181 handtest: the load's move takes its unreadable line");
    if (HandTallyText(g) != "lines=67 src.held=1 src.closed=0 src.returned=0 dst.in=64 unreadable=2 lever=64 open=p3:64,p4:1")
        Fail("T-1181 handtest: count's tally - " + HandTallyText(g));
    if (HandTallyText(std::vector<std::string>()) != "lines=0 src.held=0 src.closed=0 src.returned=0 dst.in=0 unreadable=0 lever=0 open=none")
        Fail("T-1181 handtest: an empty file's tally");
    /* clear-test */
    if (HandTestStrip(&g) != 65 || g.size() != 2 || HandFind(g, real.askerSlot, real.reqId, real.holderSlot, kRoleSrc, 0) != 0 || g[1] != junk)
        Fail("T-1181 handtest: clear-test takes out only the lever's lines");
    if (HandTestStrip(&g) != 0 || g.size() != 2) Fail("T-1181 handtest: nothing more to take out");
}

/* ---- T-889 fold 1 (review 2026-10-17) ---- */
void t_t889f1_holder_state()
{
    using namespace hovrec;
    struct Row { int live; unsigned long long eh; int squad; unsigned long long applied; int markRule; unsigned long long mark; int want; };
    const Row rows[] = {
        { 1, 5, 1, 0, 1, 0, kHsLacks },     /* no reload since the lift: the live state lacks it */
        { 0, 0, 1, 0, 1, 0, kHsUnknown },   /* no E_h */
        { 0, 5, 0, 0, 1, 0, kHsUnknown },   /* no squad record (a box / the ground) */
        { 0, 5, 1, 5, 1, 0, kHsLacks },     /* a record at E_h was loaded: written after the lift */
        { 0, 5, 1, 9, 0, 0, kHsLacks },
        { 0, 5, 1, 0, 1, 5, kHsLacks },     /* mark rule, the save asked for after the lift */
        { 0, 5, 1, 0, 1, 4, kHsHas },       /* mark rule, the save predates the lift (the double this review found) */
        { 0, 5, 1, 4, 1, 3, kHsHas },       /* mark rule, a record before the lift loaded */
        { 0, 5, 1, 4, 0, 0, kHsHas },       /* another rule: an applied record is newer than the save, and it predates the lift */
        { 0, 5, 1, 0, 0, 0, kHsUnknown } }; /* another rule, no record: the save decides - unknown */
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (HolderStateAfterLoad(rows[i].live, rows[i].eh, rows[i].squad, rows[i].applied, rows[i].markRule, rows[i].mark) != rows[i].want)
            Fail(std::string("t889 fold 1: HolderStateAfterLoad row ") + char('a' + (int)i));
}
void t_t889f1_settle_retry()
{
    using namespace hovrec;
    /* review 1: a settle whose rewrite failed is tried again, at most kSettleTries times */
    if (SettleRetryDue(0) != 1 || SettleRetryDue(kSettleTries - 1) != 1 || SettleRetryDue(kSettleTries) != 0) Fail("t889 fold 1: the settle retry is bounded");
    int tries = 0;
    while (SettleRetryDue(tries) != 0 && tries < 1000) ++tries;
    if (tries != kSettleTries) Fail("t889 fold 1: the retry stops at kSettleTries");
}
void t_t889f1_parks()
{
    using namespace hovrec;
    /* review 4: only a plain give with a line parks at its lapse */
    if (GiveLapseParks(1, 0, 0) != 1) Fail("t889 fold 1: a plain give with a line parks (no refund at the lapse)");
    if (GiveLapseParks(0, 0, 0) != 0 || GiveLapseParks(1, 2, 0) != 0 || GiveLapseParks(1, 0, 1) != 0) Fail("t889 fold 1: no line / a priced give / a consume keeps the lapse road");
    /* review 5: an escrowed take row with a line whose asker is away parks and holds no syncing */
    if (ParkTakeRow(0, 1, 1, 0) != 1) Fail("t889 fold 1: an escrowed lined take row with its asker away parks");
    if (ParkTakeRow(0, 1, 1, 1) != 0 || ParkTakeRow(0, 0, 1, 0) != 0 || ParkTakeRow(0, 1, 0, 0) != 0 || ParkTakeRow(1, 1, 1, 0) != 0)
        Fail("t889 fold 1: asker here / not escrow / no line / a give row does not park");
    if (OwnerRowHoldsSync(0, 1, 0, 1) != 0) Fail("t889 fold 1: such a row no longer holds syncing");
    if (OwnerRowHoldsSync(0, 1, 1, 1) != 1 || OwnerRowHoldsSync(0, 1, 0, 0) != 1 || OwnerRowHoldsSync(0, 0, 0, 1) != 1) Fail("t889 fold 1: every other take row holds");
    if (OwnerRowHoldsSync(1, 0, 0, 0) != 0 || OwnerRowHoldsSync(1, 0, 1, 0) != 1 || OwnerRowHoldsSync(1, 1, 0, 0) != 1) Fail("t889 fold 1: the give rule (896 b) is unchanged");
}

/* T-889 fold 2 (re-check 2026-10-17): N1 the exact match at the line's own square, N2 the removal wait, (a) the ground holder,
   N3 the gone-for-good rebuild route, N5 a verdict always consumes a parked row */
void t_t889f2_match()
{
    using namespace hovrec;
    /* N1: sid, haveQty, wantQty, quality read, have q, want q, level read, have level, want level, allowLarger */
    if (LineItemMatch(1, 3, 3, 1, 0.5f, 0.5f, 1, 2, 2, 0) != 1) Fail("t889 fold 2: the very item matches");
    if (LineItemMatch(0, 3, 3, 1, 0.5f, 0.5f, 1, 2, 2, 0) != 0) Fail("t889 fold 2: another record does not match");
    if (LineItemMatch(1, 3, 3, 1, 0.6f, 0.5f, 1, 2, 2, 0) != 0) Fail("t889 fold 2: the same type at another quality is NOT the item");
    if (LineItemMatch(1, 3, 3, 1, 0.5004f, 0.5f, 1, 2, 2, 0) != 1) Fail("t889 fold 2: a quality within a thousandth is the same");
    if (LineItemMatch(1, 4, 3, 1, 0.5f, 0.5f, 1, 2, 2, 0) != 0) Fail("t889 fold 2: the same type in a larger stack is NOT the item (a look)");
    if (LineItemMatch(1, 2, 3, 1, 0.5f, 0.5f, 1, 2, 2, 0) != 0) Fail("t889 fold 2: a smaller stack is not the item");
    if (LineItemMatch(1, 4, 3, 1, 0.5f, 0.5f, 1, 2, 2, 1) != 1) Fail("t889 fold 2: a removal search takes a larger stack of the very item");
    if (LineItemMatch(1, 2, 3, 1, 0.5f, 0.5f, 1, 2, 2, 1) != 0) Fail("t889 fold 2: ... never a smaller one");
    if (LineItemMatch(1, 4, 3, 1, 0.9f, 0.5f, 1, 2, 2, 1) != 0) Fail("t889 fold 2: ... nor a larger stack at another quality");
    if (LineItemMatch(1, 3, 3, 1, 0.5f, 0.5f, 1, 1, 2, 0) != 0) Fail("t889 fold 2: another level is not the item");
    if (LineItemMatch(1, 3, 3, 0, 0.5f, 0.5f, 1, 2, 2, 0) != 0 || LineItemMatch(1, 3, 3, 1, 0.5f, 0.5f, 0, 2, 2, 0) != 0)
        Fail("t889 fold 2: an unreadable quality or level never matches");
    if (LineItemMatch(1, 0, 0, 1, 0.5f, 0.5f, 1, 2, 2, 0) != 0) Fail("t889 fold 2: a line with no quantity matches nothing");
    /* the look at the line's own square: loaded, read, something there, it matches */
    if (SquareLook(0, 1, 1, 1) != -1 || SquareLook(1, 0, 1, 1) != -1) Fail("t889 fold 2: not loaded / unreadable square is unknown");
    if (SquareLook(1, 1, 1, 1) != 1) Fail("t889 fold 2: the item at its square is there");
    if (SquareLook(1, 1, 1, 0) != 0) Fail("t889 fold 2: another item of the same type at the square is NOT there");
    if (SquareLook(1, 1, 0, 0) != 0) Fail("t889 fold 2: an empty square (the item on another square) is NOT there");
    /* and the receiver's unknown answer it settles: 1 holds, 0 not placed, -1 still unknown */
    if (ResolveUnknownLive(kVUnknown, 1, SquareLook(1, 1, 1, 0)) != kVNot || ResolveUnknownLive(kVUnknown, 1, SquareLook(0, 1, 0, 0)) != kVUnknown
        || ResolveUnknownLive(kVUnknown, 1, SquareLook(1, 1, 1, 1)) != kVHolds)
        Fail("t889 fold 2: the receiver's unknown answer follows the square look");
}
void t_t889f2_remove_wait()
{
    using namespace hovrec;
    /* N2: removal answers 1 removed, 0 really gone, -1 not ours to write now, 2 did not take */
    if (HolderRemoveOutcome(1) != kRmClose || HolderRemoveOutcome(0) != kRmClose) Fail("t889 fold 2: a removal that ran, or a copy really gone, closes");
    if (HolderRemoveOutcome(-1) != kRmWait || HolderRemoveOutcome(2) != kRmWait) Fail("t889 fold 2: a copy not removable now keeps the line open");
    /* wait, then close: the first verdict finds the character run elsewhere, the next one removes */
    const int tries[] = { -1, 2, 1 };
    int closedAt = -1;
    for (int i = 0; i < 3 && closedAt < 0; ++i) if (HolderRemoveOutcome(tries[i]) == kRmClose) closedAt = i;
    if (closedAt != 2) Fail("t889 fold 2: the line waits through -1 and 2 and closes on the removal");
}
void t_t889f2_ground_holder()
{
    using namespace hovrec;
    /* (a): a ground holder has no squad record - its state is unknown after a reload, lacks while the line is live */
    if (HolderStateAfterLoad(0, 7, 0, 0, 1, 0) != kHsUnknown || HolderStateAfterLoad(1, 7, 0, 0, 1, 0) != kHsLacks)
        Fail("t889 fold 2: the ground holder's own state");
    struct Row { int verdict; int live; int here; int wantLook; int want; };
    const Row rows[] = {
        { kVHolds,   0,  1, 1, kActRemoveThenClose },   /* a reload of a save from before the pickup: it lies here again - removed */
        { kVHolds,   0,  0, 1, kActClose },             /* not on the ground here: close */
        { kVHolds,   0, -1, 1, kActKeep },              /* this game does not hold that ground now: wait */
        { kVHolds,   1, -1, 0, kActClose },             /* no reload since the pickup: nothing to look at */
        { kVNot,     0,  1, 1, kActCloseNoRebuild },    /* it lies here: close, no rebuild */
        { kVNot,     0,  0, 1, kActRebuild },
        { kVNot,     0, -1, 1, kActKeep },
        { kVNot,     1, -1, 0, kActRebuild },
        { kVUnknown, 0,  1, 0, kActKeep } };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
    {
        const int hs = HolderStateAfterLoad(rows[i].live, 7, 0, 0, 1, 0);
        const int look = HolderNeedsLook(rows[i].verdict, hs);
        if (look != rows[i].wantLook) Fail(std::string("t889 fold 2: ground look row ") + char('a' + (int)i));
        const int act = HolderAction(kStHeld, rows[i].verdict, 0, hs, look != 0 ? rows[i].here : -1);
        if (act != rows[i].want) Fail(std::string("t889 fold 2: ground action row ") + char('a' + (int)i));
    }
    /* a character holder still looks only when its state is not known to lack the item */
    if (HolderNeedsLook(kVHolds, kHsHas) != 1 || HolderNeedsLook(kVHolds, kHsLacks) != 0 || HolderNeedsLook(kVNot, kHsHas) != 0
        || HolderNeedsLook(kVNot, kHsLacks) != 0 || HolderNeedsLook(kVUnknown, kHsUnknown) != 0)
        Fail("t889 fold 2: when the holder looks");
}
void t_t889f2_char_route()
{
    using namespace hovrec;
    /* N3: spawned, dead, mine, faction read, our faction, another player's */
    struct Row { int sp, dead, mine, fr, own, other, want; };
    const Row rows[] = {
        { 0, 0, 0, 0, 0, 0, kRebWait },     /* not loaded here: wait */
        { 1, -1, 1, 1, 1, 0, kRebWait },    /* dead unreadable: wait */
        { 1, 1, 1, 1, 1, 0, kRebGround },   /* dead: the ground at its feet */
        { 1, 1, 0, 0, 0, 0, kRebGround },
        { 1, 0, 1, 0, 0, 0, kRebWait },     /* faction unreadable: wait */
        { 1, 0, 0, 1, 0, 1, kRebGround },   /* now another player's */
        { 1, 0, 1, 1, 0, 1, kRebGround },
        { 1, 0, 1, 1, 1, 0, kRebInto },     /* ours, run here: into it */
        { 1, 0, 0, 1, 1, 0, kRebWait },     /* ours, run by another game for now: wait */
        { 1, 0, 1, 1, 0, 0, kRebGround },   /* dismissed: no longer in our faction */
        { 1, 0, 0, 1, 0, 0, kRebGround } };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (CharRebuildRoute(rows[i].sp, rows[i].dead, rows[i].mine, rows[i].fr, rows[i].own, rows[i].other) != rows[i].want)
            Fail(std::string("t889 fold 2: CharRebuildRoute row ") + char('a' + (int)i));
}
void t_t889f2_parked_consumed()
{
    using namespace hovrec;
    /* N5: parked, same load, a free owner row */
    if (ParkedSettleRoad(0, 1, 1) != kPsNone || ParkedSettleRoad(0, 1, 0) != kPsNone) Fail("t889 fold 2: nothing parked, nothing settled");
    if (ParkedSettleRoad(1, 0, 1) != kPsDrop || ParkedSettleRoad(1, 0, 0) != kPsDrop) Fail("t889 fold 2: parked before a load: dropped (its object is gone)");
    if (ParkedSettleRoad(1, 1, 1) != kPsTable) Fail("t889 fold 2: a free row takes the parked row back");
    if (ParkedSettleRoad(1, 1, 0) != kPsSpare) Fail("t889 fold 2: a FULL table still consumes the parked row (settled in place)");
    for (int sl = 0; sl < 2; ++sl)
        for (int fr = 0; fr < 2; ++fr)
            if (ParkedSettleRoad(1, sl, fr) == kPsNone) Fail("t889 fold 2: a parked row is never left behind a verdict");
}

/* T-889 fold 3 (re-check 2 of fold 2, 2026-10-17): F1 a line names its character by the engine's save-stable handle serial and type,
   F2 the receiver's placed step and its three-way look, F3 the ground holder's removal before GROUND GONE, F4 the search before a ground
   rebuild */
void t_t889f3_char_id()
{
    using namespace hovrec;
    if (CharWhere(123, 1) != "c:S=123;T=1" || CharWhere(-7, 1) != "c:S=-7;T=1") Fail("t889 fold 3: a character id is written S=<serial>;T=<type>");
    int s = 0, ty = 0;
    if (CharIdParse(CharWhere(2147483647, 1), &s, &ty) == 0 || s != 2147483647 || ty != 1) Fail("t889 fold 3: a character id reads back (largest)");
    if (CharIdParse(CharWhere(-2147483647 - 1, 1), &s, &ty) == 0 || s != -2147483647 - 1) Fail("t889 fold 3: a character id reads back (smallest)");
    if (CharIdParse(CharWhere(0, 1), &s, &ty) == 0 || s != 0) Fail("t889 fold 3: serial 0 reads back");
    if (CharIdParse(CharWhereUnknown(), &s, &ty) != 0) Fail("t889 fold 3: c:? names no character");
    if (WhereOk(CharWhereUnknown()) != 1 || WhereOk(CharWhere(5, 1)) != 1 || WhereOk("b:k") != 1 || WhereOk("g:k") != 1)
        Fail("t889 fold 3: a character id, c:?, a box and the ground are places");
    const char* const badWhere[] = { "u:123456", "u:5", "c:", "c:S=5", "c:S=5;T=", "c:S=;T=1", "c:S=05;T=1", "c:S=5;T=1x", "c:S=-0;T=1",
                                     "c:S=5;X=1", "c:s=5;T=1", "c:??", "c:S=5;T=1;", "c:S=2147483648;T=1" };
    for (size_t i = 0; i < sizeof(badWhere) / sizeof(badWhere[0]); ++i)
        if (WhereOk(badWhere[i]) != 0) Fail(std::string("t889 fold 3: a bad place was taken: ") + badWhere[i]);
    /* every kind of line round-trips with a character id, and with c:? */
    {
        HandLine l = T889Line(kRoleSrc, kStHeld, kKindTake), p;
        std::string line;
        l.where = CharWhereUnknown();
        if (HandLineFormat(l, &line) == 0 || HandLineParse(line, &p) == 0 || !T889Same(l, p)) Fail("t889 fold 3: a c:? line round-trips");
        l.where = CharWhere(-2147483647 - 1, 1);
        if (HandLineFormat(l, &line) == 0 || HandLineParse(line, &p) == 0 || !T889Same(l, p)) Fail("t889 fold 3: a negative serial round-trips");
    }
    /* AN EARLIER LINE FAILS CLOSED: a fold 2 line (x1, u:<uid>) is not read - kept on file as it is, counted unreadable, never dropped by
       the cap (so it is held, never settled) */
    {
        const std::string old = T889Raw("x1|src|held|2|1|take|u:5|main|0|0||0|-|0a0b|-");
        HandLine p;
        if (HandLineParse(old, &p) != 0) Fail("t889 fold 3: a fold 2 line (x1, u:<uid>) is not read");
        std::vector<std::string> all;
        int bad = 0;
        HandSplit(old + "\r\n", &all, &bad);
        if (bad != 1 || all.size() != 1 || all[0] != old) Fail("t889 fold 3: an earlier line is kept as it is and counted unreadable");
        int dropped = 0;
        if (HandFit(&all, 0, &dropped) != 0 || all.size() != 1 || dropped != 0) Fail("t889 fold 3: the cap never drops an earlier line");
        if (HandFind(all, 2, 7, 1, kRoleSrc, 0) != -1) Fail("t889 fold 3: an earlier line is never found (never acted on)");
    }
    /* A CUT LINE FAILS CLOSED: every proper prefix of a good line is refused */
    {
        std::string good;
        HandLineFormat(T889Line(kRoleDst, kStIn, kKindTake), &good);
        good.erase(good.size() - 1);
        HandLine p;
        for (size_t cut = 0; cut < good.size(); ++cut)
            if (HandLineParse(good.substr(0, cut), &p) != 0) { Fail("t889 fold 3: a cut line was read"); break; }
        if (HandLineParse(good, &p) == 0) Fail("t889 fold 3: the whole line is read");
    }
    /* the write road when the id cannot be read: nothing moved yet - refused; already moved - written naming no character (held) */
    if (CharIdWriteRoad(1, 1) != kCidWrite || CharIdWriteRoad(1, 0) != kCidWrite) Fail("t889 fold 3: a readable id is written");
    if (CharIdWriteRoad(0, 1) != kCidRefuse) Fail("t889 fold 3: an unreadable id refuses a hand-over that has not moved yet");
    if (CharIdWriteRoad(0, 0) != kCidUnknown) Fail("t889 fold 3: an unreadable id after the move writes c:? (held)");
    /* the line's character is the one loaded character answering its id */
    if (CharMatchUse(1) != 1 || CharMatchUse(0) != 0 || CharMatchUse(2) != 0 || CharMatchUse(3) != 0)
        Fail("t889 fold 3: one match is the character; none or several are not loaded");
}
void t_t889f3_placed()
{
    using namespace hovrec;
    /* F2: loaded, square read, placement written down, item there, kind read, same kind, size there, pre, got -> the answer it gives */
    struct Row { int ld, rd, pk, th, kr, sk, have, pre, got, want; };
    const Row rows[] = {
        { 1, 1, 1, 1, 1, 1,  5, 0, 5, kVHolds },     /* the stack the placement made (an empty square before) */
        { 1, 1, 1, 1, 1, 1,  8, 5, 8, kVHolds },     /* merged onto a stack of 5: the 8 it made */
        { 1, 1, 1, 1, 1, 1,  5, 5, 8, kVNot },       /* the square holds exactly the stack it held before */
        { 1, 1, 1, 0, 0, 0, -1, 0, 5, kVNot },       /* empty and readable */
        { 1, 1, 1, 0, 0, 0, -1, 5, 8, kVNot },       /* nothing of it there, readable */
        { 1, 1, 1, 1, 1, 0,  5, 0, 5, kVNot },       /* another item (another record, quality or level) */
        { 1, 1, 1, 1, 1, 1,  6, 5, 8, kVUnknown },   /* this item in a stack of another size */
        { 1, 1, 1, 1, 1, 1,  3, 0, 5, kVUnknown },
        { 1, 1, 1, 1, 0, 0, -1, 0, 5, kVUnknown },   /* its kind unreadable */
        { 1, 0, 1, 1, 1, 1,  5, 0, 5, kVUnknown },   /* the square unreadable */
        { 0, 1, 1, 1, 1, 1,  5, 0, 5, kVUnknown },   /* not loaded here */
        { 1, 1, 0, 1, 1, 1,  5, 0, 5, kVUnknown },   /* a crash before the placed rewrite: the stack is there - still unknown */
        { 1, 1, 0, 0, 0, 0, -1, 0, 0, kVUnknown },   /* a crash before the placed rewrite, the square empty: unknown, NEVER not placed */
        { 1, 1, 0, 1, 1, 0,  5, 0, 0, kVUnknown } }; /* ... another item on it: unknown */
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
    {
        const Row& r = rows[i];
        const int look = PlacedLook(r.ld, r.rd, r.pk, r.th, r.kr, r.sk, r.have, r.pre, r.got);
        if (ResolveUnknownLive(kVUnknown, 1, look) != r.want) Fail(std::string("t889 fold 3: PlacedLook row ") + char('a' + (int)i));
    }
    /* where the add ended: the section's stacks of the item before and after */
    std::vector<PlStack> b, a;
    PlStack s1 = { 100ULL, 0, 0, 5 }, s2 = { 200ULL, 3, 1, 2 }, nw = { 300ULL, 4, 4, 3 };
    b.push_back(s1); b.push_back(s2);
    int x = -1, y = -1, pre = -1, got = -1;
    a = b; a.push_back(nw);
    if (PlacedWhere(b, a, 3, &x, &y, &pre, &got) != 1 || x != 4 || y != 4 || pre != 0 || got != 3) Fail("t889 fold 3: a new stack on a free square");
    a = b; a[0].qty = 8;
    if (PlacedWhere(b, a, 3, &x, &y, &pre, &got) != 1 || x != 0 || y != 0 || pre != 5 || got != 8) Fail("t889 fold 3: merged onto a stack (5 -> 8)");
    a = b;
    if (PlacedWhere(b, a, 3, &x, &y, &pre, &got) != 0) Fail("t889 fold 3: nothing changed is not told");
    a = b; a[0].qty = 8; a.push_back(nw);
    if (PlacedWhere(b, a, 3, &x, &y, &pre, &got) != 0) Fail("t889 fold 3: two changes are not told");
    a.clear(); a.push_back(s1); a.push_back(nw);
    if (PlacedWhere(b, a, 3, &x, &y, &pre, &got) != 0) Fail("t889 fold 3: a stack gone is not told");
    a = b; a[0].x = 1; a.push_back(nw);
    if (PlacedWhere(b, a, 3, &x, &y, &pre, &got) != 0) Fail("t889 fold 3: a stack moved is not told");
    a = b; a.push_back(nw); a[2].qty = 2;
    if (PlacedWhere(b, a, 3, &x, &y, &pre, &got) != 0) Fail("t889 fold 3: a new stack of another size is not told");
    a = b; a[0].qty = 7;
    if (PlacedWhere(b, a, 3, &x, &y, &pre, &got) != 0) Fail("t889 fold 3: a stack grown by another amount is not told");
    a = b; a.push_back(nw);
    if (PlacedWhere(b, a, 0, &x, &y, &pre, &got) != 0) Fail("t889 fold 3: a placement of nothing is not told");
    {
        std::vector<PlStack> e;
        a.clear(); a.push_back(nw);
        if (PlacedWhere(e, a, 3, &x, &y, &pre, &got) != 1 || pre != 0 || got != 3) Fail("t889 fold 3: the first stack of it in the section");
    }
    /* the end it tells answers the look: the stack it made holds, the stack before (or an empty square) is not placed */
    a = b; a[0].qty = 8;
    PlacedWhere(b, a, 3, &x, &y, &pre, &got);
    if (PlacedLook(1, 1, 1, 1, 1, 1, 8, pre, got) != 1 || PlacedLook(1, 1, 1, 1, 1, 1, 5, pre, got) != 0 || PlacedLook(1, 1, 1, 1, 1, 1, 6, pre, got) != -1)
        Fail("t889 fold 3: the merge's end answers the look");
    /* only a receiver line carries a placement, and it adds something */
    if (PlacedOk(kRoleDst, 1, 0, 1) != 1 || PlacedOk(kRoleDst, 1, 5, 8) != 1 || PlacedOk(kRoleSrc, 0, 0, 0) != 1 || PlacedOk(kRoleDst, 0, 0, 0) != 1)
        Fail("t889 fold 3: good placements");
    if (PlacedOk(kRoleSrc, 1, 0, 1) != 0 || PlacedOk(kRoleDst, 1, 2, 2) != 0 || PlacedOk(kRoleDst, 1, -1, 2) != 0 || PlacedOk(kRoleDst, 1, 3, 1) != 0)
        Fail("t889 fold 3: bad placements");
}
void t_t889f3_ground_order()
{
    using namespace hovrec;
    /* F3: GrRemoveLocal answers 1 removed, 0 not removed, -1 deferred - GROUND GONE only after 1; anything else keeps the line open */
    if (GroundRemoveSendsGone(1) != 1 || GroundRemoveSendsGone(0) != 0 || GroundRemoveSendsGone(-1) != 0) Fail("t889 fold 3: GROUND GONE only after a removal that took");
    if (GroundRemoveLook(1) != 1 || GroundRemoveLook(0) != 2 || GroundRemoveLook(-1) != 2) Fail("t889 fold 3: the removal's look");
    if (HolderRemoveOutcome(GroundRemoveLook(1)) != kRmClose || HolderRemoveOutcome(GroundRemoveLook(0)) != kRmWait
        || HolderRemoveOutcome(GroundRemoveLook(-1)) != kRmWait) Fail("t889 fold 3: a removal that did not take keeps the line open");
    /* deferred, then failed, then removed: GONE once - at the removal - and the line closes then */
    const int rs[] = { -1, 0, 1 };
    int sent = 0, closedAt = -1;
    for (int i = 0; i < 3; ++i)
    {
        sent += GroundRemoveSendsGone(rs[i]);
        if (closedAt < 0 && HolderRemoveOutcome(GroundRemoveLook(rs[i])) == kRmClose) closedAt = i;
        if (closedAt < 0 && sent != 0) Fail("t889 fold 3: GONE sent before the line could close");
    }
    if (sent != 1 || closedAt != 2) Fail("t889 fold 3: one GONE, at the removal that took");
}
void t_t889f3_search_before_rebuild()
{
    using namespace hovrec;
    /* F4: the exact whole-inventory search before a ground rebuild */
    if (GroundRebuildCheck(1, 1) != kGrbHolds) Fail("t889 fold 3: found on the character - no ground rebuild");
    if (GroundRebuildCheck(1, 0) != kGrbRebuild) Fail("t889 fold 3: not on it - the ground rebuild");
    if (GroundRebuildCheck(0, 0) != kGrbWait || GroundRebuildCheck(0, 1) != kGrbWait) Fail("t889 fold 3: an unreadable inventory waits");
    /* every route that ends on the ground is searched first: dead, dismissed / captured (neither ours nor another player's), another player's */
    struct Row { int sp, dead, mine, fr, own, other; };
    const Row ground[] = { { 1, 1, 1, 1, 1, 0 }, { 1, 0, 1, 1, 0, 0 }, { 1, 0, 0, 1, 0, 1 } };
    for (size_t i = 0; i < sizeof(ground) / sizeof(ground[0]); ++i)
    {
        const Row& r = ground[i];
        if (CharRebuildRoute(r.sp, r.dead, r.mine, r.fr, r.own, r.other) != kRebGround) Fail("t889 fold 3: a gone-for-good route is the ground");
        if (GroundRebuildCheck(1, 1) == kGrbRebuild) Fail("t889 fold 3: ... and a copy found there is never rebuilt");
    }
    /* with the item found: verdict 0 closes with no rebuild (returned) - the same settle as the holder's own state having it */
    if (HolderAction(kStHeld, kVNot, 0, kHsHas, -1) != kActCloseNoRebuild) Fail("t889 fold 3: the holder's state has it - no rebuild");
}

/* T-889 fold 4 (re-check 3, 2026-10-17): G1 a late take writes its in line and its placed step through the live take's one road, G2 the
   holder's look before a rebuild, G3 the receiver's look with the resulting stack elsewhere, G4 the line's own square first */
void t_t889f4_late_take_line()
{
    using namespace hovrec;
    /* the one road: the add runs only on a line not refused; a written line always gets its step (placed, or dropped with a failed add) */
    if (TakerMayAdd(0) != 0 || TakerMayAdd(1) != 1 || TakerMayAdd(2) != 1) Fail("t889 fold 4: the add runs only when the in line was not refused");
    if (TakerLineStep(2, 1) != kTlStep || TakerLineStep(2, 0) != kTlStep) Fail("t889 fold 4: a written in line gets its step after the add");
    if (TakerLineStep(1, 1) != kTlMemory || TakerLineStep(1, 0) != kTlNone || TakerLineStep(0, 0) != kTlNone)
        Fail("t889 fold 4: no line kept - a placement is remembered; a refused line places nothing");
    /* a late take, step by step: the in line before the add (its placement not written down - unknown, never "not placed"), the add (a
       merge onto a stack of 5), then the placed step - the line now answers the stack it made */
    HandLine in = T889Line(kRoleDst, kStIn, kKindTake);
    in.plHas = 0; in.plPre = 0; in.plGot = 0;
    std::string s;
    HandLine p;
    if (HandLineFormat(in, &s) == 0 || HandLineParse(s, &p) == 0 || p.plHas != 0) Fail("t889 fold 4: the late take's in line is written before the add");
    if (ResolveUnknownLive(kVUnknown, 1, PlacedLook(1, 1, p.plHas, 0, 0, 0, -1, p.plPre, p.plGot)) != kVUnknown)
        Fail("t889 fold 4: a crash between the late add and its step answers unknown");
    std::vector<PlStack> before, after;
    PlStack s5 = { 10ULL, 2, 3, 5 };
    before.push_back(s5);
    after = before; after[0].qty = 8;
    int x = -1, y = -1, pre = -1, got = -1;
    if (TakerLineStep(2, 1) != kTlStep || PlacedWhere(before, after, 3, &x, &y, &pre, &got) != 1) Fail("t889 fold 4: the late add's end is told");
    p.x = x; p.y = y; p.plHas = 1; p.plPre = pre; p.plGot = got;
    HandLine q;
    if (HandLineFormat(p, &s) == 0 || HandLineParse(s, &q) == 0 || q.plHas != 1 || q.plPre != 5 || q.plGot != 8 || q.x != 2 || q.y != 3)
        Fail("t889 fold 4: the late take's placed step is written");
    if (ResolveUnknownLive(kVUnknown, 1, PlacedLook(1, 1, q.plHas, 1, 1, 1, 8, q.plPre, q.plGot)) != kVHolds)
        Fail("t889 fold 4: after its placed step the late take's line answers holds");
}
void t_t889f4_holder_look()
{
    using namespace hovrec;
    /* path resolved, square look, search read, found elsewhere -> the look; then the holder's action on verdict 0 with its state unknown */
    struct Row { int path, sq, rd, found, look, act; };
    const Row rows[] = {
        { 1,  1, 1, 0,  1, kActCloseNoRebuild },   /* on its square */
        { 1, -1, 1, 1, -1, kActKeep },             /* its square unreadable */
        { 1,  0, 1, 1,  1, kActCloseNoRebuild },   /* not on its square, its exact item elsewhere: holds */
        { 1,  0, 1, 0,  0, kActRebuild },          /* not on its square, nowhere else: rebuilt */
        { 1,  0, 0, 0, -1, kActKeep },             /* the rest unreadable: waits */
        { 0,  0, 1, 1,  1, kActCloseNoRebuild },   /* its pack moved / taken off, the item elsewhere: holds */
        { 0,  0, 1, 0, -1, kActKeep },             /* its pack moved / taken off, nowhere else: UNKNOWN - never rebuilt */
        { 0, -1, 1, 0, -1, kActKeep },
        { 0,  1, 1, 0, -1, kActKeep },             /* a square answer with no path is not taken */
        { 0,  0, 0, 1, -1, kActKeep } };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
    {
        const Row& r = rows[i];
        const int look = HolderNotPlacedLook(r.path, r.sq, r.rd, r.found);
        if (look != r.look) Fail(std::string("t889 fold 4: HolderNotPlacedLook row ") + char('a' + (int)i));
        if (HolderNeedsLook(kVNot, kHsUnknown) != 1 || HolderAction(kStHeld, kVNot, 0, kHsUnknown, look) != r.act)
            Fail(std::string("t889 fold 4: the holder's action on verdict 0, row ") + char('a' + (int)i));
    }
    /* an unresolvable path is never "not there" whatever the search says */
    for (int sq = -1; sq <= 1; ++sq)
        for (int rd2 = 0; rd2 <= 1; ++rd2)
            for (int f = 0; f <= 1; ++f)
                if (HolderNotPlacedLook(0, sq, rd2, f) == 0) Fail("t889 fold 4: an unresolvable pack path answered not there");
}
void t_t889f4_receiver_wide()
{
    using namespace hovrec;
    /* square look, the before-stack at the square, search read, a stack of size got elsewhere, any stack of the item -> the verdict */
    struct Row { int sq, preAt, rd, gotEl, kind, want; };
    const Row rows[] = {
        { 1, 0, 1, 0, 1, kVHolds },     /* the square shows the stack it made */
        { -1, 0, 1, 0, 0, kVUnknown },  /* the square unknown stays unknown */
        { 0, 0, 1, 1, 1, kVUnknown },   /* moved: the resulting stack elsewhere - unknown, NEVER not placed */
        { 0, 1, 1, 1, 1, kVUnknown },   /* the before-stack at the square and a resulting stack elsewhere */
        { 0, 0, 1, 0, 1, kVUnknown },   /* the square empty, this item elsewhere at another size (moved and merged) - unknown */
        { 0, 0, 0, 0, 0, kVUnknown },   /* the rest unreadable */
        { 0, 1, 0, 0, 1, kVUnknown },
        { 0, 0, 1, 0, 0, kVNot },       /* readable and empty of this item: not placed */
        { 0, 1, 1, 0, 1, kVNot } };     /* the exact before-stack at the square and nothing else matching: not placed */
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
    {
        const Row& r = rows[i];
        if (ResolveUnknownLive(kVUnknown, 1, PlacedLookWide(r.sq, r.preAt, r.rd, r.gotEl, r.kind)) != r.want)
            Fail(std::string("t889 fold 4: PlacedLookWide row ") + char('a' + (int)i));
    }
    /* the case: a box receiver's placed 5 on an empty square, moved by its player to another square, then a reload - the line's square
       is empty (PlacedLook 0) and the stack of 5 is elsewhere: unknown (2), so the holder keeps the line held and never rebuilds */
    const int sq = PlacedLook(1, 1, 1, 0, 0, 0, -1, 0, 5);
    const int v = ResolveUnknownLive(kVUnknown, 1, PlacedLookWide(sq, 0, 1, 1, 1));
    if (sq != 0 || v != kVUnknown || HolderAction(kStHeld, v, 0, kHsUnknown, 0) != kActKeep) Fail("t889 fold 4: a moved placement keeps the line held");
}
void t_t889f4_own_square_first()
{
    using namespace hovrec;
    if (GroundSearchHit(1, 0) != kGsSquare || GroundSearchHit(1, 1) != kGsSquare) Fail("t889 fold 4: the line's own square answers first");
    if (GroundSearchHit(0, 1) != kGsElsewhere) Fail("t889 fold 4: a match only elsewhere is told apart (twinElsewhere)");
    if (GroundSearchHit(0, 0) != kGsNone) Fail("t889 fold 4: no match");
    /* found anywhere still holds (the trade-off), not found rebuilds, unreadable waits */
    if (GroundRebuildCheck(1, GroundSearchHit(1, 0) != kGsNone) != kGrbHolds || GroundRebuildCheck(1, GroundSearchHit(0, 1) != kGsNone) != kGrbHolds)
        Fail("t889 fold 4: a match on the square or elsewhere holds");
    if (GroundRebuildCheck(1, GroundSearchHit(0, 0) != kGsNone) != kGrbRebuild) Fail("t889 fold 4: no match rebuilds on the ground");
    if (GroundRebuildCheck(0, 0) != kGrbWait) Fail("t889 fold 4: unreadable waits");
}

/* T-889 fold 5 (2026-10-17): CLOSED 1 is acknowledged only once the item is in a committed record of the receiving game */
void t_t889f5_durable()
{
    using namespace hovrec;
    const unsigned long long E = 40ULL;
    /* the squad's last commit before / at / after the placement's E */
    if (InRecordDurable(1, 39ULL, E, 0, 0ULL, 0ULL) != kDurNo) Fail("t889 fold 5: a commit before E is not durable");
    if (InRecordDurable(1, 40ULL, E, 0, 0ULL, 0ULL) != kDurCommit) Fail("t889 fold 5: a commit at E is durable");
    if (InRecordDurable(1, 41ULL, E, 0, 0ULL, 0ULL) != kDurCommit) Fail("t889 fold 5: a commit after E is durable");
    if (InRecordDurable(0, 0ULL, E, 0, 0ULL, 0ULL) != kDurNo) Fail("t889 fold 5: nothing committed is not durable");
    if (InRecordDurable(1, 99ULL, 0ULL, 0, 0ULL, 0ULL) != kDurNo) Fail("t889 fold 5: no E known is never durable");
    /* a same-bytes skip after E counts once the record it equalled has committed */
    if (InRecordDurable(1, 30ULL, E, 1, 40ULL, 30ULL) != kDurSameBytes) Fail("t889 fold 5: a skip at E against the committed record counts");
    if (InRecordDurable(1, 30ULL, E, 1, 45ULL, 30ULL) != kDurSameBytes) Fail("t889 fold 5: a skip after E against the committed record counts");
    if (InRecordDurable(1, 30ULL, E, 1, 39ULL, 30ULL) != kDurNo) Fail("t889 fold 5: a skip before E does not count");
    if (InRecordDurable(1, 30ULL, E, 1, 45ULL, 35ULL) != kDurNo) Fail("t889 fold 5: a skip against a queued record not yet committed waits");
    if (InRecordDurable(1, 35ULL, E, 1, 45ULL, 35ULL) != kDurSameBytes) Fail("t889 fold 5: ... and counts once that record commits");
    if (InRecordDurable(0, 0ULL, E, 1, 45ULL, 35ULL) != kDurNo) Fail("t889 fold 5: a skip with nothing committed waits");
}
void t_t889f5_closed_road()
{
    using namespace hovrec;
    const unsigned long long E = 40ULL;
    /* live: wait until durable, then drop (and ACK); no squad record (a box) or no E: dropped at once, as before */
    if (ClosedPlacedRoad(1, 1, E, InRecordDurable(1, 39ULL, E, 0, 0ULL, 0ULL), kVNone) != kCpWait) Fail("t889 fold 5: CLOSED 1 before the commit waits");
    if (ClosedPlacedRoad(1, 1, E, InRecordDurable(1, 40ULL, E, 0, 0ULL, 0ULL), kVNone) != kCpDrop) Fail("t889 fold 5: CLOSED 1 at the commit drops");
    if (ClosedPlacedRoad(1, 1, E, InRecordDurable(1, 50ULL, E, 0, 0ULL, 0ULL), kVNone) != kCpDrop) Fail("t889 fold 5: CLOSED 1 after the commit drops");
    if (ClosedPlacedRoad(1, 1, E, InRecordDurable(1, 30ULL, E, 1, 41ULL, 30ULL), kVNone) != kCpDrop) Fail("t889 fold 5: a same-bytes skip drops");
    if (ClosedPlacedRoad(1, 0, E, kDurNo, kVNone) != kCpDropNoRecord) Fail("t889 fold 5: no squad record - dropped at once");
    if (ClosedPlacedRoad(1, 1, 0ULL, kDurNo, kVNone) != kCpDropNoRecord) Fail("t889 fold 5: no E - dropped at once");
    /* after a load a commit proves nothing: the receiver's own after-load verdict decides */
    if (ClosedPlacedRoad(0, 1, E, kDurCommit, kVNot) != kCpKeep) Fail("t889 fold 5: after a load, a later commit never drops a line not held");
    if (ClosedPlacedRoad(0, 1, E, kDurNo, kVUnknown) != kCpKeep) Fail("t889 fold 5: after a load, unknown keeps the line");
    if (ClosedPlacedRoad(0, 1, E, kDurNo, kVHolds) != kCpDrop) Fail("t889 fold 5: after a load, held drops");
    /* the crash window: placed, CLOSED 1 before the commit (wait), crash, reload with the record never landed - its load applied a record
       below E under the mark rule (the save before the take): verdict 0, the line kept with its bytes */
    if (ClosedPlacedRoad(0, 1, E, kDurNo, Verdict(1, kVNone, 0, HoldsAfterLoad(30ULL, 20ULL, E), 1, 1, 1, 1)) != kCpKeep)
        Fail("t889 fold 5: a crash before the record landed keeps the line after the reload");
    if (ClosedPlacedRoad(0, 1, E, kDurNo, Verdict(1, kVNone, 0, HoldsAfterLoad(41ULL, 20ULL, E), 1, 1, 1, 1)) != kCpDrop)
        Fail("t889 fold 5: a record that landed before the crash drops the line after the reload");
}
/* T-889 fold 5b (2026-10-17): the taker's CLOSED 1 after a load - held drops and ACKs; not held and definitely lacking is rebuilt, then
   ACKed once its record commits; unknown waits */
void t_t889f5b_taker_closed()
{
    using namespace hovrec;
    /* the look: the square, then the exact search */
    if (TakerLackLook(0, 1, 0) != kTlkLacks) Fail("t889 fold 5b: a readable square and search finding nothing lacks");
    if (TakerLackLook(1, 1, 0) != kTlkFound) Fail("t889 fold 5b: the square showing the placement is found");
    if (TakerLackLook(0, 1, 1) != kTlkFound) Fail("t889 fold 5b: the exact item elsewhere is found");
    if (TakerLackLook(-1, 1, 0) != kTlkUnknown) Fail("t889 fold 5b: a square not loaded / unknown is unknown");
    if (TakerLackLook(0, 0, 0) != kTlkUnknown) Fail("t889 fold 5b: an unreadable search is unknown");
    /* the decision table */
    if (TakerClosedAfterLoad(kVHolds, kTlkUnknown) != kTcDrop) Fail("t889 fold 5b: held drops and ACKs");
    if (TakerClosedAfterLoad(kVNot, kTlkLacks) != kTcRebuild) Fail("t889 fold 5b: not held and lacking is rebuilt");
    if (TakerClosedAfterLoad(kVNot, kTlkFound) != kTcWait) Fail("t889 fold 5b: not held by record but found here waits");
    if (TakerClosedAfterLoad(kVNot, kTlkUnknown) != kTcWait) Fail("t889 fold 5b: not held but not lookable waits");
    if (TakerClosedAfterLoad(kVUnknown, kTlkLacks) != kTcWait) Fail("t889 fold 5b: unknown waits (owner 897 a)");
    if (TakerClosedAfterLoad(kVUnknown, kTlkFound) != kTcWait) Fail("t889 fold 5b: unknown waits, found or not");
    /* after the rebuild */
    if (TakerRebuildNext(1, 0, 1) != kTnLive) Fail("t889 fold 5b: rebuilt and rewritten goes live");
    if (TakerRebuildNext(1, 0, 0) != kTnKeep) Fail("t889 fold 5b: rebuilt but not rewritten is kept, no ACK");
    if (TakerRebuildNext(1, 1, 0) != kTnDropNow) Fail("t889 fold 5b: a ground rebuild drops and ACKs now");
    if (TakerRebuildNext(-1, 0, 0) != kTnKeep) Fail("t889 fold 5b: a rebuild that waits is kept");
    if (TakerRebuildNext(0, 0, 0) != kTnKeep) Fail("t889 fold 5b: a failed rebuild is kept");
    if (TakerRebuildNext(2, 0, 0) != kTnKeep) Fail("t889 fold 5b: found on a character gone for good is kept");
    /* not held -> rebuild then ACK after the commit: the rebuilt line is live with its new E (E2); the fold-5 road waits, then drops */
    const unsigned long long E2 = 70ULL;
    if (ClosedPlacedRoad(1, 1, E2, InRecordDurable(1, 69ULL, E2, 0, 0ULL, 0ULL), kVNone) != kCpWait) Fail("t889 fold 5b: no ACK before the rebuilt record commits");
    if (ClosedPlacedRoad(1, 1, E2, InRecordDurable(1, 70ULL, E2, 0, 0ULL, 0ULL), kVNone) != kCpDrop) Fail("t889 fold 5b: ACK once the rebuilt record commits");
    if (ClosedPlacedRoad(1, 0, E2, kDurNo, kVNone) != kCpDropNoRecord) Fail("t889 fold 5b: a box rebuild drops at once (no squad record of ours)");
    /* a crash before that commit: the reload's record predates E2 under the mark rule - not held again, so rebuilt again (no double) */
    if (Verdict(1, kVNone, 0, HoldsAfterLoad(69ULL, 50ULL, E2), 1, 1, 1, 1) != kVNot) Fail("t889 fold 5b: a crash before the rebuilt record is not held");
    if (Verdict(1, kVNone, 0, HoldsAfterLoad(71ULL, 50ULL, E2), 1, 1, 1, 1) != kVHolds) Fail("t889 fold 5b: the rebuilt record landed - held, no second rebuild");
    if (Verdict(1, kVNone, 0, HoldsAfterLoad(60ULL, 70ULL, E2), 1, 1, 1, 1) != kVHolds) Fail("t889 fold 5b: a save asked for after the rebuild - held");
}

/* T-710: whose characters a save's squad file holds, and the fence for laying this game's own record over it
   (src/common/savesquadforeign.h). One synthetic squad file: a squad record and character records with and without a hand
   and an "owner faction ID". */
void T710PutRecord(coopown::PodBuf* b, int type, int serial, bool hasHand, const char* owner)
{
    using namespace coopown;
    PodPutI32(b, 1); PodPutI32(b, type); PodPutI32(b, 3);
    PodPutStr(b, "n", 1); PodPutStr(b, "12-x.base", 9); PodPutI32(b, 0);
    PodPutI32(b, 0);                                                   /* boolFields */
    PodPutI32(b, 0);                                                   /* floatFields */
    PodPutI32(b, hasHand ? 3 : 1);                                     /* intFields */
    if (hasHand) { Mmo2PutKeyI(b, "handleTYPE", 1); Mmo2PutKeyI(b, "handleS", serial); }
    Mmo2PutKeyI(b, "char count", 1);
    PodPutI32(b, 0);                                                   /* vectorFields */
    PodPutI32(b, 0);                                                   /* rotationFields */
    PodPutI32(b, owner ? 2 : 1);                                       /* stringFields */
    PodPutKeyStr(b, "name", 4, "x", 1);
    if (owner) PodPutKeyStr(b, "owner faction ID", 16, owner, std::strlen(owner));
    PodPutI32(b, 0);                                                   /* fileFields */
    PodPutI32(b, 0);                                                   /* references */
    PodPutI32(b, 0);                                                   /* instances */
}
void t710_scan_owners()
{
    using namespace coopown;
    char buf[2048];
    PodBuf b;
    PodInit(&b, buf, sizeof(buf));
    PodPutGdcHeader(&b, 7, 4);
    T710PutRecord(&b, 0x1E, 0, false, 0);                  /* the squad record: not a character */
    T710PutRecord(&b, 0x24, 11, true, "coop-p2");
    T710PutRecord(&b, 0x24, 12, true, 0);                  /* no owner field */
    T710PutRecord(&b, 0x24, 13, false, "204-gamedata.base");   /* no hand */
    if (PodOverflow(&b)) { Fail("T-710: the synthetic squad file overflowed its buffer"); return; }
    coopforeign::OwnerScan os;
    GdcScanOut gs;
    if (!coopforeign::ScanCharOwners(buf, b.len, "f.platoon", &os) || !GdcScan(buf, b.len, "f.platoon", &gs)) { Fail("T-710: the synthetic squad file does not scan"); return; }
    if (os.chars.size() != 3 || os.owners.size() != 3) Fail("T-710: three character records give three characters and three owners");
    else
    {
        if (os.chars != gs.chars) Fail("T-710: the owner scan names the characters exactly as GdcScan does (the roster's ids)");
        if (os.chars[0] != HandId(11, 1) || os.chars[2].empty() || os.chars[2][0] != '?') Fail("T-710: a hand gives S=..;T=.., a missing hand the '?' form");
        if (os.owners[0] != "coop-p2" || !os.owners[1].empty() || os.owners[2] != "204-gamedata.base") Fail("T-710: each character's owner faction ID is read, '' when absent");
    }
    /* cut short: refused, never a partial answer used */
    if (coopforeign::ScanCharOwners(buf, b.len - 3, "f.platoon", &os)) Fail("T-710: a file cut short does not scan");
    if (coopforeign::ScanCharOwners(0, 0, "f.platoon", &os)) Fail("T-710: no bytes do not scan");
    /* a squad file with no character */
    PodInit(&b, buf, sizeof(buf));
    PodPutGdcHeader(&b, 7, 1);
    T710PutRecord(&b, 0x1E, 0, false, 0);
    if (!coopforeign::ScanCharOwners(buf, b.len, "e.platoon", &os) || !os.chars.empty() || !os.owners.empty()) Fail("T-710: a squad file with no character scans with none");
}
void t710_foreign_verdict()
{
    using namespace coopforeign;
    /* one owner: another slot's stand-in, the legacy stand-in, this game's own slot, a world faction, nothing, a look-alike */
    if (OwnerIsOtherPlayer("coop-p2", 1) != 1) Fail("T-710: another slot's stand-in is another player's");
    if (OwnerIsOtherPlayer("coop-peer", 1) != 1) Fail("T-710: protocol 67's coop-peer is another player's");
    if (OwnerIsOtherPlayer("coop-p1", 1) != 0) Fail("T-710: a stand-in id of this game's own slot is not another player's");
    if (OwnerIsOtherPlayer("coop-p1", -1) != 1) Fail("T-710: with this game's slot unknown, any stand-in id is another player's");
    if (OwnerIsOtherPlayer("204-gamedata.base", 1) != 0 || OwnerIsOtherPlayer("", 1) != 0) Fail("T-710: a world or player faction id, or none, is not another player's");
    if (OwnerIsOtherPlayer("coop-p", 1) != 0 || OwnerIsOtherPlayer("coop-p07", 1) != 0 || OwnerIsOtherPlayer("coop-px", 1) != 0) Fail("T-710: only an exact coop-p<n> counts");
    /* the file: every branch */
    std::vector<std::string> o;
    int f = -1;
    if (ForeignVerdict(o, 1, &f) != kVerdictNoChars || f != 0) Fail("T-710: no character is kVerdictNoChars, never another player's");
    o.push_back("204-gamedata.base");
    if (ForeignVerdict(o, 1, &f) != kVerdictNone || f != 0) Fail("T-710: no stand-in character is kVerdictNone");
    o.push_back("coop-p2");
    if (ForeignVerdict(o, 1, &f) != kVerdictSome || f != 1) Fail("T-710: one of two is kVerdictSome - the file is not another player's");
    std::vector<std::string> a;
    a.push_back("coop-p2"); a.push_back("coop-peer");
    if (ForeignVerdict(a, 1, &f) != kVerdictAll || f != 2) Fail("T-710: every character a stand-in's is kVerdictAll");
    a.push_back("");
    if (ForeignVerdict(a, 1, 0) != kVerdictSome) Fail("T-710: a character with no owner field is never counted as another player's (positive test)");
    /* qualified: this file's own VerdictName(int) / KindName(int) (payload and clock tests, same anonymous namespace) hide
       coopforeign's from a block-scope using-directive */
    if (std::string(coopforeign::VerdictName(kVerdictAll)) != "another player's" || std::string(coopforeign::VerdictName(kVerdictSome)) != "partly another player's"
        || std::string(coopforeign::VerdictName(kVerdictNone)) != "no other player's" || std::string(coopforeign::VerdictName(kVerdictNoChars)) != "no character")
        Fail("T-710: the verdict names");
}
void t710_fence()
{
    using namespace coopforeign;
    /* the save: file A (the clash file) holds the other player's two, file B this game's other squad holds X */
    std::vector<std::string> fa, fb, rec;
    fa.push_back("S=1;T=1"); fa.push_back("S=2;T=1");
    fb.push_back("S=9;T=1"); fb.push_back("S=9;T=1");
    std::map<std::string, int> holding;
    AddFileChars(&holding, fa); AddFileChars(&holding, fb);
    if (holding["S=9;T=1"] != 1 || holding["S=1;T=1"] != 1) Fail("T-710: a file counts once per character, twice-listed or not");
    rec.push_back("S=5;T=1");
    if (CharsElsewhere(rec, holding, fa) != 0) Fail("T-710: a record character in no file is elsewhere 0 times");
    std::vector<std::string> recB(1, "S=9;T=1");
    if (CharsElsewhere(recB, holding, fa) != 1) Fail("T-710: a record character in another file is counted");
    std::vector<std::string> recA(1, "S=1;T=1");
    if (CharsElsewhere(recA, holding, fa) != 0) Fail("T-710: a record character only in the file itself is not elsewhere");
    std::map<std::string, int> twice(holding); ++twice["S=1;T=1"];
    if (CharsElsewhere(recA, twice, fa) != 1) Fail("T-710: a record character in the file itself AND another file is elsewhere");
    if (Unhanded("?f#3") != 1 || Unhanded("S=1;T=1") != 0 || Unhanded("") != 0) Fail("T-710: the no-hand form");
    /* FenceDecide: every branch, in order */
    if (FenceDecide(kVerdictSome, rec, 0, 0) != kFenceNotForeign || FenceDecide(kVerdictNone, rec, 0, 0) != kFenceNotForeign
        || FenceDecide(kVerdictNoChars, rec, 0, 0) != kFenceNotForeign) Fail("T-710: a file not wholly another player's keeps the existing rule");
    if (FenceDecide(kVerdictAll, rec, 0, 1) != kFenceUnparsed) Fail("T-710 fold 3: an unparsed squad file of the save may hold anyone - the fence fails");
    std::vector<std::string> none;
    if (FenceDecide(kVerdictAll, none, 0, 0) != kFenceRecordEmpty) Fail("T-710: a record holding nobody is never laid over the file");
    std::vector<std::string> unh(rec); unh.push_back("?pp.squad.x.rec#2");
    if (FenceDecide(kVerdictAll, unh, 0, 0) != kFenceUnhanded) Fail("T-710: a record character with no hand cannot be shown to be in no other file");
    if (FenceDecide(kVerdictAll, rec, 1, 0) != kFenceElsewhere) Fail("T-710: a record character in another squad file keeps the existing rule");
    if (FenceDecide(kVerdictAll, rec, 0, 0) != kFenceApply) Fail("T-710: a file of another player's characters, a record of this game's own people in no other file: applied");
    if (std::string(FenceName(kFenceApply)) != "apply" || std::string(FenceName(kFenceNotForeign)).empty() || std::string(FenceName(kFenceRecordEmpty)).empty()
        || std::string(FenceName(kFenceUnhanded)).empty() || std::string(FenceName(kFenceElsewhere)).empty() || std::string(FenceName(kFenceUnparsed)).empty()
        || std::string(FenceName(99)) != "?")
        Fail("T-710: the fence names");
    /* the whole decision as the prepare makes it: the clash file, this game's own record */
    std::vector<std::string> owners; owners.push_back("coop-p2"); owners.push_back("coop-p2");
    const int v = ForeignVerdict(owners, 1, 0);
    if (FenceDecide(v, rec, CharsElsewhere(rec, holding, fa), 0) != kFenceApply) Fail("T-710: the reported clash (two of the host's characters in the joiner's file) lets the joiner's record in");
}
/* T-710 review fold 1: a fenced squad (its save version is the record's own people, its file another player's) that the roster check
   refuses loads that file - the roster counts the file's characters for it, never the record's people. */
void t710_fence_roster()
{
    using namespace coopown;
    std::vector<std::string> fixed(1, "S=7;T=1");
    /* X fenced: record {p}, file {q} (another player's). Z: save {z}, record {z, p} (p joined Z after X's record). X is refused first
       (p twice); counting p for X again would refuse Z too and leave p in no squad. */
    std::vector<RosterSquad> r(2);
    r[0].key = "X"; r[0].apply = 1; r[0].fenced = 1; r[0].saveChars.push_back("S=1;T=1"); r[0].recChars.push_back("S=1;T=1"); r[0].fileChars.push_back("S=9;T=1");
    r[1].key = "Z"; r[1].apply = 1; r[1].saveChars.push_back("S=2;T=1"); r[1].recChars.push_back("S=2;T=1"); r[1].recChars.push_back("S=1;T=1");
    RosterCounts rc;
    const int refused = RosterResolve(&r, fixed, &rc);
    if (refused != 1 || r[0].apply != 0 || r[0].duplicate != 1 || r[1].apply != 1 || rc.duplicate != 1 || rc.conflict != 0)
        Fail("T-710 fold 1: a refused fenced squad counts its file's characters, not the record's - the other squad keeps the person");
    /* the same squads unfenced (X's save version really holds p): both refused, as before */
    std::vector<RosterSquad> u(r);
    u[0].apply = 1; u[0].duplicate = 0; u[0].fenced = 0; u[1].apply = 1; u[1].duplicate = 0; u[1].conflict = 0;
    if (RosterResolve(&u, fixed, 0) != 2 || u[0].apply != 0 || u[1].apply != 0) Fail("T-710 fold 1: an unfenced squad's save version still counts when refused");
    /* a fenced squad alone, its record taken: applied on its own people; the file's characters are not looked for */
    std::vector<RosterSquad> a(1, r[0]);
    a[0].apply = 1; a[0].duplicate = 0;
    if (RosterResolve(&a, fixed, 0) != 0 || a[0].apply != 1) Fail("T-710 fold 1: a fenced squad whose people are each once is applied");
    /* a fenced squad whose record holds a person of another squad file is refused as a duplicate */
    std::vector<RosterSquad> d(1, r[0]);
    d[0].apply = 1; d[0].duplicate = 0; d[0].recChars.push_back("S=7;T=1"); d[0].saveChars = d[0].recChars;
    if (RosterResolve(&d, fixed, 0) != 1 || d[0].apply != 0 || d[0].duplicate != 1) Fail("T-710 fold 1: a fenced record holding a person of another file is refused");
}
void t710_squad_kind()
{
    using namespace coopforeign;
    if (SquadKind(0, 1, 1, 1) != kKindUnread) Fail("T-710: an unreadable faction is unread, whatever else");
    if (SquadKind(1, 1, 1, 1) != kKindOwn) Fail("T-710: this game's player faction is own");
    if (SquadKind(1, 0, 1, 1) != kKindPeer) Fail("T-710: a stand-in in the table is peer");
    if (SquadKind(1, 0, 0, 1) != kKindStandIn) Fail("T-710: a stand-in by record only is stand-in");
    if (SquadKind(1, 0, 0, 0) != kKindWorld) Fail("T-710: any other faction is world");
    if (KindIsOtherPlayer(kKindPeer) != 1 || KindIsOtherPlayer(kKindStandIn) != 1 || KindIsOtherPlayer(kKindOwn) != 0
        || KindIsOtherPlayer(kKindWorld) != 0 || KindIsOtherPlayer(kKindUnread) != 0)
        Fail("T-710: only a stand-in's squad (peer or by record) is another player's - it never takes this game's record, and an unread one is not refused");
    if (std::string(coopforeign::KindName(kKindOwn)) != "own" || std::string(coopforeign::KindName(kKindPeer)) != "peer" || std::string(coopforeign::KindName(kKindStandIn)) != "stand-in"
        || std::string(coopforeign::KindName(kKindWorld)) != "world" || std::string(coopforeign::KindName(kKindUnread)) != "unread")
        Fail("T-710: the kind names");
}

/* ---- T-609 part 2 (H1): a zone record is stamped max(now, last + 1), so two saves in one second both pass the world server's
   not-newer rule (store_main.cpp OnRecordFrom: a stamp <= the held one is refused), and an older stamp is still refused ---- */
void t_t609_zone_stamp()
{
    using namespace zonerec;
    int b = -1;
    const long long s1 = ZoneStampNext(1000, 0, 0, &b);
    if (s1 != 1000 || b != 0) Fail("T-609 H1: a first save was not stamped now");
    const long long s2 = ZoneStampNext(1000, s1, s1, &b);
    if (s2 != 1001 || b != 1) Fail("T-609 H1: a second save in the same second was not stamped past the first");
    const long long s3 = ZoneStampNext(1000, s2, s2, &b);
    if (s3 != 1002 || b != 1) Fail("T-609 H1: a third save in the same second was not stamped past the second");
    if (ZoneStampNext(1005, s3, s3, &b) != 1005 || b != 0) Fail("T-609 H1: a save in a later second was not stamped now");
    if (ZoneStampNext(1000, 0, 1003, &b) != 1004 || b != 1) Fail("T-609 H1: a held record stamped later (another game's) was not passed");
    if (ZoneStampNext(1000, 1002, 0, &b) != 1003) Fail("T-609 H1: the stamp sent last (no record held yet) was not passed");
    /* the world server's rule, as OnRecordFrom applies it: taken only when newer than the held stamp */
    long long held = 0; int taken = 0;
    const long long sent[2] = { s1, s2 };
    for (int i = 0; i < 2; ++i) if (sent[i] > held) { held = sent[i]; ++taken; }
    if (taken != 2) Fail("T-609 H1: two saves in one second were not both taken");
    if (999 > held || s1 > held) Fail("T-609 H1: an older stamp would have been taken");
    /* the old stamp (time(0) twice) is what lost the second save */
    held = 0; taken = 0;
    for (int i = 0; i < 2; ++i) if (1000 > held) { held = 1000; ++taken; }
    if (taken != 1) Fail("T-609 H1: the model of the old defect is wrong");
}
/* ---- T-609 part 2 (W): the world server takes a zone record only from its area's holder ---- */
void t_t609_zone_holder()
{
    using namespace zonerec;
    int sx = -1, sy = -1;
    if (ZoneRecIdParse("zone.12.34", &sx, &sy) != 1 || sx != 12 || sy != 34) Fail("T-609 W: zone.12.34 did not parse");
    if (ZoneRecIdParse("ground.12.34", &sx, &sy) != 0 || ZoneRecIdParse("zone.x.y", &sx, &sy) != 0 || ZoneRecIdParse("zone.", 0, 0) != 0)
        Fail("T-609 W: a non-zone id parsed as a zone");
    if (ZoneJudgeWrite("alice", "alice") != kZrAccept || ZoneJudgeTakes(kZrAccept) != 1) Fail("T-609 W: the holder's save was not taken");
    if (ZoneJudgeWrite("alice", "bob") != kZrRefuseNotHolder || ZoneJudgeTakes(kZrRefuseNotHolder) != 0) Fail("T-609 W: a non-holder's save was taken");
    if (ZoneJudgeWrite("", "bob") != kZrAcceptNoHolder || ZoneJudgeTakes(kZrAcceptNoHolder) != 1) Fail("T-609 W: an area nobody holds refused a save");
    if (ZoneJudgeWrite("alice", "") != kZrAcceptSenderUnknown || ZoneJudgeTakes(kZrAcceptSenderUnknown) != 1) Fail("T-609 W: a save whose sender left was refused");
    /* the zone changing hands, alice -> bob: alice's save before the hand-over is taken, after it refused; bob's the other way round */
    const char* holders[2] = { "alice", "bob" };
    if (ZoneJudgeTakes(ZoneJudgeWrite(holders[0], "alice")) != 1 || ZoneJudgeTakes(ZoneJudgeWrite(holders[0], "bob")) != 0) Fail("T-609 W: before the hand-over");
    if (ZoneJudgeTakes(ZoneJudgeWrite(holders[1], "alice")) != 0 || ZoneJudgeTakes(ZoneJudgeWrite(holders[1], "bob")) != 1) Fail("T-609 W: after the hand-over");
    if (std::string(ZoneJudgeWhy(kZrRefuseNotHolder)).find("does not hold") == std::string::npos) Fail("T-609 W: the refusal's reason text");
}
/* ---- T-609 part 2 (H4): a building the mod dismantled leaves the memoised list at once and stays out of fresh walks for
   kGoneKeepDrains drains ---- */
void t_t609_walk_drop()
{
    using namespace walkdrop;
    char obj[80];
    void* list[4] = { &obj[0], &obj[1], &obj[2], &obj[1] };
    int n = WalkListDrop(list, 4, &obj[1]);
    if (n != 2 || list[0] != &obj[0] || list[1] != &obj[2]) Fail("T-609 H4: the dropped building (twice listed) did not leave the list in order");
    if (WalkListDrop(list, n, &obj[1]) != 2) Fail("T-609 H4: dropping an absent building changed the list");
    GoneRow rows[kGoneCap]; int gn = 0, gnext = 0;
    GoneAdd(rows, kGoneCap, &gn, &gnext, &obj[1], 10);
    GoneAdd(rows, kGoneCap, &gn, &gnext, &obj[1], 10);
    if (gn != 1) Fail("T-609 H4: one building was remembered twice");
    for (long long d = 10; d <= 13; ++d)
    {
        void* fresh[3] = { &obj[0], &obj[1], &obj[2] };
        const int k = WalkListDropRecent(fresh, 3, rows, gn, d, kGoneKeepDrains);
        const int want = (d - 10 < kGoneKeepDrains) ? 2 : 3;
        if (k != want) Fail("T-609 H4: a fresh walk kept the dismantled building in the wrong drains");
        if (k == 2 && (fresh[0] != &obj[0] || fresh[1] != &obj[2])) Fail("T-609 H4: a fresh walk lost a live building");
    }
    gn = 0; gnext = 0;
    for (int i = 0; i < kGoneCap + 1; ++i) GoneAdd(rows, kGoneCap, &gn, &gnext, &obj[i % 80], 20);
    if (gn != kGoneCap || GoneRecent(rows, gn, &obj[kGoneCap], 20, kGoneKeepDrains) != 1 || GoneRecent(rows, gn, &obj[0], 20, kGoneKeepDrains) != 0)
        Fail("T-609 H4: a full ring did not replace its oldest row");
    GoneAdd(rows, kGoneCap, &gn, &gnext, 0, 20);
    if (gn != kGoneCap) Fail("T-609 H4: a null building was remembered");
}
/* ---- T-609 fold 1 (F1): an area row whose player slots.txt does not know (restored, owner slot -1) holds nothing - the zone
   record is judged as "nobody holds it", as GROUND and BOX are, and taken ---- */
void t_t609_area_holder_rule()
{
    using namespace zonerec;
    if (AreaRowHeld("alice", 3) != 1) Fail("T-609 F1: a named row with a slot does not hold its area");
    if (AreaRowHeld("alice", -1) != 0) Fail("T-609 F1: a row whose player has no slot holds its area");
    if (AreaRowHeld("", 3) != 0 || AreaRowHeld("", -1) != 0) Fail("T-609 F1: a row naming nobody holds its area");
    if (!AreaRowHolderId("alice", -1).empty()) Fail("T-609 F1: the unknown-slot row named a holder id");
    if (AreaRowHolderId("alice", 0) != "alice") Fail("T-609 F1: slot 0 is a real slot");
    const int unknownSlot = ZoneJudgeWrite(AreaRowHolderId("alice", -1), "bob");
    if (unknownSlot != kZrAcceptNoHolder || ZoneJudgeTakes(unknownSlot) != 1) Fail("T-609 F1: a save in an area whose holder is not in slots.txt was refused");
    if (ZoneJudgeTakes(ZoneJudgeWrite(AreaRowHolderId("alice", 2), "bob")) != 0) Fail("T-609 F1: a known holder no longer refuses another game");
}
/* ---- T-609 fold 1 (F2): the game takes the area holder's zone copy even when it is not newer, over another writer's record;
   every other not-newer copy is still refused ---- */
void t_t609_zone_recv_holder()
{
    using namespace zonerec;
    const int me = 1, holder = 2, other = 3;
    if (ZoneRecvJudge(0, 0, -1, 1000, other, holder, me) != kZvTakeNewer) Fail("T-609 F2: a first copy was not taken");
    if (ZoneRecvJudge(1, 1000, me, 1001, other, holder, me) != kZvTakeNewer) Fail("T-609 F2: a newer copy was not taken");
    /* the case: this game's own refused save (stamp 1002) against the new holder's copy stamped 1001 / 1002 */
    if (ZoneRecvJudge(1, 1002, me, 1001, holder, holder, me) != kZvTakeHolder || ZoneRecvTakes(kZvTakeHolder) != 1)
        Fail("T-609 F2: the holder's older copy over this game's refused save was not taken");
    if (ZoneRecvJudge(1, 1002, me, 1002, holder, holder, me) != kZvTakeHolder) Fail("T-609 F2: the holder's same-second copy was not taken");
    /* refused: a non-holder's not-newer copy */
    if (ZoneRecvJudge(1, 1002, me, 1001, other, holder, me) != kZvRefuseNotNewer || ZoneRecvTakes(kZvRefuseNotNewer) != 0)
        Fail("T-609 F2: a non-holder's not-newer copy was taken");
    /* refused: the holder's own older copy over its own newer one (the plain stamp order of one writer) */
    if (ZoneRecvJudge(1, 1002, holder, 1001, holder, holder, me) != kZvRefuseNotNewer) Fail("T-609 F2: the holder's older copy replaced its newer one");
    /* refused: this game's own slot, even as holder (a stale push of its own record) */
    if (ZoneRecvJudge(1, 1002, other, 1001, me, me, me) != kZvRefuseNotNewer) Fail("T-609 F2: this game's own older copy was taken");
    /* refused: holder unknown, nobody or frozen (-1 / -2); owner field unknown (-1) */
    if (ZoneRecvJudge(1, 1002, me, 1001, holder, -1, me) != kZvRefuseNotNewer || ZoneRecvJudge(1, 1002, me, 1001, holder, -2, me) != kZvRefuseNotNewer)
        Fail("T-609 F2: a copy was taken with no holder known");
    if (ZoneRecvJudge(1, 1002, me, 1001, -1, -1, me) != kZvRefuseNotNewer) Fail("T-609 F2: an unknown-owner copy was taken");
    /* this game's slot unknown (-1): the holder's copy still wins over this game's record (owner -1) */
    if (ZoneRecvJudge(1, 1002, -1, 1001, holder, holder, -1) != kZvTakeHolder) Fail("T-609 F2: the holder's copy lost while this game's slot was unknown");
}
/* ---- T-609 fold 1 (F3): a zone save held behind another write is judged on the sender id kept with it, not skipped because the
   sender's connection is gone by the time it is decided ---- */
void t_t609_held_sender()
{
    using namespace zonerec;
    /* bob's save held; bob left before it was decided (id now empty); alice holds the area: refused */
    if (ZoneJudgeTakes(ZoneJudgeWrite("alice", ZoneSenderId("bob", ""))) != 0) Fail("T-609 F3: a held non-holder's save was taken after it left");
    /* alice's own held save after she left: taken (the holder's last save is not lost) */
    if (ZoneJudgeWrite("alice", ZoneSenderId("alice", "")) != kZrAccept) Fail("T-609 F3: a held holder's save was not taken after she left");
    /* the id as it arrived wins over the connection's id now (the slot reused by another game) */
    if (ZoneJudgeWrite("alice", ZoneSenderId("bob", "alice")) != kZrRefuseNotHolder) Fail("T-609 F3: a reused connection's id judged a held save");
    /* a message that came with no id falls back to the live connection, then to unknown */
    if (ZoneSenderId("", "carol") != "carol") Fail("T-609 F3: no fallback to the live id");
    if (ZoneJudgeWrite("alice", ZoneSenderId("", "")) != kZrAcceptSenderUnknown) Fail("T-609 F3: a save with no id at all was not the unknown case");
}
/* ---- T-609 (owner decision 911 a): a kept-in-step copy is adopted only when every condition holds; each failing condition is
   named, in order ---- */
void t_t609_zone_kept_adopt()
{
    using namespace zonerec;
    const int me = 1, other = 0; const long gen = 3;
    if (ZoneKeptAdoptJudge(1, gen, gen, me, me, other, 140.0, 150.0) != kZaAdopt) Fail("T-609 A: a kept-in-step copy was not adopted");
    if (ZoneKeptAdoptJudge(1, gen, gen, me, me, other, 150.0, 150.0) != kZaAdopt) Fail("T-609 A: a bit that appeared at the limit was refused");
    if (ZoneKeptAdoptJudge(0, gen, gen, me, me, other, 140.0, 150.0) != kZaNoMark) Fail("T-609 A: a copy with no mark was adopted");
    if (ZoneKeptAdoptJudge(1, gen, gen + 1, me, me, other, 140.0, 150.0) != kZaLinkDropped) Fail("T-609 A: a copy from before a link drop was adopted");
    if (zonerec::ZoneProvisionalJudge(0, 3, 3, 5, 5) != zonerec::kZpMadeGood) Fail("T-823 fold 2: a provisional read with no record, the same link and no teardown was not made good");
    if (zonerec::ZoneProvisionalJudge(1, 3, 3, 5, 5) != zonerec::kZpRecordHeld) Fail("T-823 fold 2: a provisional read was made good while a record of the area is held");
    if (zonerec::ZoneProvisionalJudge(0, 3, 4, 5, 5) != zonerec::kZpLinkChanged || zonerec::ZoneProvisionalJudge(0, 0, 0, 5, 5) != zonerec::kZpLinkChanged
        || zonerec::ZoneProvisionalJudge(0, -1, -1, 5, 5) != zonerec::kZpLinkChanged)
        Fail("T-823 fold 2: a provisional read was made good across a link change (or with no link)");
    if (zonerec::ZoneProvisionalJudge(0, 3, 3, 5, 6) != zonerec::kZpTornDown) Fail("T-823 fold 2: a provisional read was made good across a world teardown");
    if (zonerec::ZoneProvisionalJudge(1, 3, 4, 5, 6) != zonerec::kZpRecordHeld) Fail("T-823 fold 2: the record check is not the first");
    if (zonerec::ZoneProvisionalBlocks(0, 1, 0) != 1 || zonerec::ZoneProvisionalBlocks(0, 0, 0) != 0 || zonerec::ZoneProvisionalBlocks(1, 1, 0) != 0
        || zonerec::ZoneProvisionalBlocks(1, 1, 1) != 1 || zonerec::ZoneProvisionalBlocks(1, 0, 0) != 0)
        Fail("T-823 fold 3: an area-file read is blocked by any record held, a load from a record only by a newer one");
    if (ZoneKeptAdoptJudge(1, gen, gen, other, me, other, 140.0, 150.0) != kZaNotHolder) Fail("T-609 A: a copy was adopted while another game holds the area");
    if (ZoneKeptAdoptJudge(1, gen, gen, -1, me, other, 140.0, 150.0) != kZaNotHolder || ZoneKeptAdoptJudge(1, gen, gen, -2, me, other, 140.0, 150.0) != kZaNotHolder)
        Fail("T-609 A: a copy was adopted with the holder unknown or frozen");
    if (ZoneKeptAdoptJudge(1, gen, gen, -1, -1, other, 140.0, 150.0) != kZaNotHolder) Fail("T-609 A: a copy was adopted with this game's slot unknown");
    if (ZoneKeptAdoptJudge(1, gen, gen, me, me, me, 140.0, 150.0) != kZaOwnRecord) Fail("T-609 A: a copy was adopted over this game's own record");
    if (ZoneKeptAdoptJudge(1, gen, gen, me, me, -1, 140.0, 150.0) != kZaOwnRecord) Fail("T-609 A: a copy was adopted over a record of unknown writer");
    if (ZoneKeptAdoptJudge(1, gen, gen, me, me, other, 0.0, 150.0) != kZaNotLoadedSince) Fail("T-609 A: a copy was adopted with the area not loaded on the map");
    if (ZoneKeptAdoptJudge(1, gen, gen, me, me, other, 151.0, 150.0) != kZaNotLoadedSince) Fail("T-609 A: a copy was adopted whose map bit appeared after the limit");
    /* the first failing condition is the one named */
    if (ZoneKeptAdoptJudge(0, gen, gen + 1, other, me, me, 0.0, 150.0) != kZaNoMark) Fail("T-609 A: the order of the conditions changed");
    if (ZoneKeptAdoptJudge(1, gen, gen, other, me, me, 0.0, 150.0) != kZaNotHolder) Fail("T-609 A: the holder test came after a later one");
    /* the from-record case (review fold): loaded from slot 0's record at gen 3, slot 0 wrote newer records while it held the
       area (the first landed at 150), then this game took it over. Same five conditions. */
    if (ZoneKeptAdoptJudge(1, gen, gen, me, me, other, 120.0, 150.0) != kZaAdopt) Fail("T-609 A: a kept-in-step copy loaded from a record was not adopted");
    if (ZoneKeptAdoptJudge(1, gen, gen, me, me, other, 120.0, 0.0) != kZaNotLoadedSince) Fail("T-609 A: a copy from a record was adopted with no other slot's record landed since the load");
    if (ZoneKeptAdoptJudge(1, gen, gen, me, me, other, 160.0, 150.0) != kZaNotLoadedSince) Fail("T-609 A: a copy from a record reloaded after the newer record landed was adopted");
    if (ZoneKeptAdoptJudge(1, gen, gen, me, me, me, 120.0, 150.0) != kZaOwnRecord) Fail("T-609 A: a copy from a record was adopted over this game's own newer record");
    if (ZoneKeptAdoptJudge(1, gen, gen + 1, me, me, other, 120.0, 150.0) != kZaLinkDropped) Fail("T-609 A: a copy from a record was adopted after a link drop");
    if (ZoneKeptAdoptJudge(1, gen, gen, other, me, other, 120.0, 150.0) != kZaNotHolder) Fail("T-609 A: a copy from a record was adopted while the other game still holds the area");
    if (ZoneKeptAdoptJudge(0, -1, gen, me, me, other, 120.0, 150.0) != kZaNoMark) Fail("T-609 A: a copy from a record with no mark was adopted");
    for (int w = kZaAdopt; w <= kZaNotLoadedSince; ++w) if (std::string(ZoneKeptAdoptWhy(w)) == "unknown") Fail("T-609 A: a reason has no name");
}
/* ---- T-609 fold 1 (F4): a dismantle that faulted, or did not take the building down, leaves it in the walk list ---- */
void t_t609_gone_guard()
{
    using namespace walkdrop;
    if (GoneAfterDismantle(1, 1, 1) != 1) Fail("T-609 F4: a dismantled building was kept");
    if (GoneAfterDismantle(0, 1, 1) != 1 || GoneAfterDismantle(0, 0, 0) != 1) Fail("T-609 F4: a faulted dismantle kept the building (it may be freed)");
    if (GoneAfterDismantle(1, 0, 1) != 1 || GoneAfterDismantle(1, 0, 0) != 1) Fail("T-609 F4: an unreadable state kept the building (it may be freed)");
    if (GoneAfterDismantle(1, 1, 0) != 0) Fail("T-609 F4: an unfinished dismantle (REMOVE retries it) dropped the building");
}
/* T-923: the decision - no town, outside, or a player faction's town allows; a held town refuses at once when the piece creates a
   player town, else only when the spot is inside it at the scale 1.0; an unread "creates player town" is unknown only when it decides */
void t_t923_build_near_town()
{
    using namespace buildnear;
    if (ScaleForKind(4) != kScaleNarrow || ScaleForKind(9) != kScaleNarrow) Fail("T-923: kinds 4 / 9 are not asked at the scale 1.0");
    if (ScaleForKind(0) != kScaleWide || ScaleForKind(8) != kScaleWide || ScaleForKind(3) != kScaleWide) Fail("T-923: another kind is not asked at the scale 2.5");
    if (InsideFirst(0, 1999.0f) != 1) Fail("T-923: a centre nearer than 2000 is not inside");
    if (InsideFirst(0, 2000.0f) != 0 || InsideFirst(0, 5000.0f) != 0) Fail("T-923: a centre 2000 or more away, outside the area, is inside");
    if (InsideFirst(1, 9000.0f) != 1) Fail("T-923: a spot in the area is not inside");
    int c, a1;
    for (c = -1; c <= 1; ++c) for (a1 = 0; a1 <= 1; ++a1)
    {
        if (Refused(0, 1, 0, c, a1) != 0) Fail("T-923: no town refuses");
        if (Refused(1, 0, 0, c, a1) != 0) Fail("T-923: a spot outside the town refuses");
        if (Refused(1, 1, 1, c, a1) != 0) Fail("T-923: a player faction's town refuses (the one exemption)");
    }
    if (Refused(1, 1, 0, 1, 0) != 1 || Refused(1, 1, 0, 1, 1) != 1) Fail("T-923: a piece that creates a player town, inside, is allowed");
    if (Refused(1, 1, 0, 0, 1) != 1) Fail("T-923: an ordinary piece inside at the scale 1.0 is allowed");
    if (Refused(1, 1, 0, 0, 0) != 0) Fail("T-923: an ordinary piece outside the scale 1.0 is refused");
    if (Refused(1, 1, 0, -1, 1) != 1) Fail("T-923: an unread piece inside at the scale 1.0 is not refused");
    if (Refused(1, 1, 0, -1, 0) != -1) Fail("T-923: an unread piece outside the scale 1.0 is not unknown");
}
/* T-923 (decision 967): the confirm's town pick takes a teammate's town only when one was found for a current teammate's slot and no
   own town was, or the teammate's is strictly nearer; an own town nearer, equal, or whose distance did not read is kept */
void t_t923_town_pick_takes_mate()
{
    using namespace buildnear;
    /* inside a teammate's outpost (T-940: its fixed zone) */
    if (TownPickTakesMate(1, 2, kZoneInside, 0, 1) != 1) Fail("T-923: inside a teammate's outpost and in no own town - not taken");
    /* inside an own town: never given away */
    if (TownPickTakesMate(1, 2, kZoneInside, 1, 1) != 0) Fail("T-923: inside an own town too - the teammate's taken");
    /* elsewhere: the engine's choice (T-940: no ring - decision 973's ring replaced by the fixed zone) */
    if (TownPickTakesMate(1, 2, kZoneOut, 0, 1) != 0) Fail("T-923: outside the teammate's zone - taken");
    /* unread answers are never a yes */
    if (TownPickTakesMate(1, 2, kZoneInside, -1, 1) != 0 || TownPickTakesMate(1, 2, -1, 0, 1) != 0) Fail("T-923: an unread inside test - taken");
    if (TownPickTakesMate(0, 2, kZoneInside, 0, 1) != 0 || TownPickTakesMate(1, -1, kZoneInside, 0, 1) != 0) Fail("T-923: no current teammate's town, yet taken");
    if (InsideFirst(0, 1999.0f) != 1 || InsideFirst(0, 2000.0f) != 0 || ScaleForKind(4) != 1.0f || ScaleForKind(1) != 2.5f) Fail("T-923: the near-town rule's ring");
    if (kTownKeepRange != 5000.0f) Fail("T-923: the confirm's keep range is not 5000");
    /* the caller's keep test (0x4D642F): within 5000 kept, past it or no town not, an unread distance unknown */
    if (TownKept(1, 5000.0f) != 1 || TownKept(1, 5000.5f) != 0 || TownKept(0, 10.0f) != 0 || TownKept(1, -1.0f) != -1) Fail("T-923: TownKept");
    /* a teammate's town past the caller's 5000, or whose distance did not read: never the answer (the caller would drop it) */
    if (TownPickWhy(1, 2, kZoneInside, 0, 0) != kPickMatePast || TownPickWhy(1, 2, kZoneInside, 0, -1) != kPickMatePast || TownPickTakesMate(1, 2, kZoneInside, 0, 0) != 0)
        Fail("T-923: a teammate's town past 5000 or unread - taken");
    if (TownPickWhy(0, 2, kZoneInside, 0, 1) != kPickNoMate || TownPickWhy(1, 2, kZoneInside, 1, 1) != kPickOwnInside || TownPickWhy(1, 2, kZoneOut, 0, 1) != kPickOut)
        Fail("T-923: TownPickWhy's other reasons");
}
/* T-923 review MED 2 (protocol 167): a handed outdoor PLACE carries the outpost's centre; with and without the nonce, round trip exact,
   a bad tag / an unowned or furniture hand-over / a cut tail refused, and a PLACE without it reads townHand 0 */
void t_t923_place_town_tail()
{
    using namespace coopbuild;
    BuildMsg m;
    m.kind = kBuildPlace; m.key = "k1"; m.sid = "s1"; m.pos[0] = 10.0f; m.pos[2] = 7000.0f; m.ownerSlot = 3;
    m.townHand = 1; m.townPos[0] = 1234.5f; m.townPos[1] = -98.25f;
    for (int withNonce = 0; withNonce < 2; ++withNonce)
    {
        m.nonce = withNonce != 0 ? 0xA1B2C3D4u : 0u;
        std::vector<char> b;
        BuildMsg o;
        if (!EncodeBuild(&b, m) || DecodeBuild(&b[0], b.size(), &o) != kBuildDecodeOk) { Fail("T-923: a PLACE with a town tail did not round-trip"); continue; }
        if (o.townHand != 1 || o.townPos[0] != 1234.5f || o.townPos[1] != -98.25f || o.nonce != m.nonce || o.ownerSlot != 3) Fail("T-923: the town tail / nonce changed on the wire");
        std::vector<char> x = b; x[x.size() - kBuildTownTail] = (char)2;
        if (DecodeBuild(&x[0], x.size(), &o) == kBuildDecodeOk) Fail("T-923: a town tail with a bad tag accepted");
        x = b; x.pop_back();
        if (DecodeBuild(&x[0], x.size(), &o) == kBuildDecodeOk) Fail("T-923: a cut town tail accepted");
    }
    BuildMsg u = m; u.ownerSlot = kBuildOwnerSender;
    std::vector<char> b;
    if (EncodeBuild(&b, u)) Fail("T-923: a town hand-over with no owner encoded");
    u = m; u.hostKey = "h"; u.hostForm = 1;
    if (EncodeBuild(&b, u)) Fail("T-923: a furniture piece with a town hand-over encoded");
    u = m; u.townHand = 0;
    BuildMsg o;
    if (!EncodeBuild(&b, u) || DecodeBuild(&b[0], b.size(), &o) != kBuildDecodeOk || o.townHand != 0) Fail("T-923: a PLACE without the town tail did not read townHand 0");
}
/* T-906 models, in the engine's layout (x64): a VS2010 std::map node and a list of 0xA0 bytes */
static void PkNode(char* n, char* l, char* p, char* r, float key, unsigned long long val, int nil)
{
    std::memset(n, 0, 0x30);
    *(char**)(n + buildpick::kTreeLeft) = l; *(char**)(n + buildpick::kTreeParent) = p; *(char**)(n + buildpick::kTreeRight) = r;
    *(float*)(n + buildpick::kTreeKey) = key; *(unsigned long long*)(n + buildpick::kTreeValue) = val; n[buildpick::kTreeIsNil] = (char)nil;
}
static int g_pkPastEnd = 0;   /* the last PkWalk's pastEnd */
static unsigned long long PkWalk(const char* list, float draw) { return (unsigned long long)(size_t)buildpick::WalkMap(list, draw, &g_pkPastEnd); }
/* T-906: the walk takes the lowest choice whose running total is above the draw (a draw at or past the end takes the largest); the
   draw from the hash stays inside [0, total), is the same for the same key parts and keeps the weights' shares */
void t_t906_pick_walk_and_draw()
{
    using namespace buildpick;
    if (sizeof(void*) != 8) return;   /* the model is the x64 layout */
    /* three choices, weights 1 / 2 / 3: running totals 1, 3, 6; the root is the middle one */
    static char head[0x30], a[0x30], b[0x30], c[0x30], list[0xA0];
    PkNode(head, a, b, c, 0.0f, 0xDEADull, 1);
    PkNode(b, a, head, c, 3.0f, 102, 0);
    PkNode(a, head, b, head, 1.0f, 101, 0);
    PkNode(c, head, b, head, 6.0f, 103, 0);
    std::memset(list, 0, sizeof list);
    *(char**)(list + kMapHead) = head; *(unsigned long long*)(list + kMapSize) = 3; *(float*)(list + kListTotal) = 6.0f;
    if (PkWalk(list, 0.0f) != 101 || PkWalk(list, 0.99f) != 101) Fail("T-906: a draw below the first running total is not the first choice");
    if (PkWalk(list, 1.0f) != 102 || PkWalk(list, 2.99f) != 102) Fail("T-906: a draw in [1, 3) is not the second choice");
    if (PkWalk(list, 3.0f) != 103 || PkWalk(list, 5.99f) != 103) Fail("T-906: a draw in [3, 6) is not the third choice");
    if (PkWalk(list, 5.99f) != 103 || g_pkPastEnd != 0) Fail("T-906 F4: a draw below the last key was counted past the end");
    if (PkWalk(list, 6.0f) != 103 || g_pkPastEnd != 1 || PkWalk(list, 100.0f) != 103 || g_pkPastEnd != 1) Fail("T-906: a draw at or past the end did not take the largest choice and say pastEnd (the head's empty slot was read)");
    /* F4: the keys in order, for the pastEnd line */
    float keys[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    if (MapKeys(list, keys, 8) != 3 || keys[0] != 1.0f || keys[1] != 3.0f || keys[2] != 6.0f) Fail("T-906 F4: MapKeys did not list 1, 3, 6 in order");
    if (MapKeys(list, keys, 2) != 2 || keys[0] != 1.0f || keys[1] != 3.0f) Fail("T-906 F4: MapKeys past its cap");
    /* the draw */
    if (DrawFromHash(0u, 6.0f) != 0.0f) Fail("T-906: hash 0 is not draw 0");
    if (!(DrawFromHash(0xFFFFFFFFu, 6.0f) < 6.0f) || !(DrawFromHash(0xFFFFFFFFu, 1e-30f) < 1e-30f)) Fail("T-906: the top hash drew the total or past it");
    if (RoundPos(0.49f) != 0 || RoundPos(0.5f) != 1 || RoundPos(-0.5f) != -1 || RoundPos(-47333.9f) != -47334 || RoundPos(724.1f) != 724) Fail("T-906: RoundPos");
    const int p0[3] = { -47334, 724, -55623 }, p1[3] = { -47333, 724, -55623 };
    const unsigned k = WorldKey("w-0123456789abcdef");
    if (k == 0 || WorldKey("") == 0) Fail("T-906: a world key is 0 (0 means 'no key')");
    if (k != WorldKey("w-0123456789abcdef")) Fail("T-906: the same world id gave two keys");
    const unsigned h0 = PickHash(k, "42309-rebirth.mod", "13-gamedata.base", p0, 0, 0);
    if (h0 != PickHash(k, "42309-rebirth.mod", "13-gamedata.base", p0, 0, 0)) Fail("T-906: the same key parts gave two hashes");
    int diff = 0;
    diff += h0 != PickHash(WorldKey("w-other"), "42309-rebirth.mod", "13-gamedata.base", p0, 0, 0);
    diff += h0 != PickHash(k, "45556-rebirth.mod", "13-gamedata.base", p0, 0, 0);
    diff += h0 != PickHash(k, "42309-rebirth.mod", "14-gamedata.base", p0, 0, 0);
    diff += h0 != PickHash(k, "42309-rebirth.mod", "13-gamedata.base", p1, 0, 0);
    diff += h0 != PickHash(k, "42309-rebirth.mod", "13-gamedata.base", p0, 1, 0);
    if (diff != 5) Fail("T-906: a changed key part (world, record, faction, spot, list) left the hash unchanged");
    /* F3: furniture - the host's world position joins the key; two identical houses (same local spot) draw apart, one house is stable */
    const int hostA[3] = { -47100, 730, -55500 }, hostB[3] = { -46800, 730, -55500 };
    const unsigned ha = PickHash(k, "42309-rebirth.mod", "13-gamedata.base", p0, 0, hostA);
    if (ha != PickHash(k, "42309-rebirth.mod", "13-gamedata.base", p0, 0, hostA)) Fail("T-906 F3: the same host gave two hashes");
    if (ha == PickHash(k, "42309-rebirth.mod", "13-gamedata.base", p0, 0, hostB)) Fail("T-906 F3: two hosts at different spots gave one hash");
    if (ha == h0) Fail("T-906 F3: a host left the hash of the piece without one");
    int hostSame = 0;
    for (int i = 0; i < 64; ++i)
    {
        const int hi[3] = { -47100 + i * 300, 730, -55500 };
        hostSame += PickHash(k, "42309-rebirth.mod", "13-gamedata.base", p0, 0, hi) == ha ? 1 : 0;
    }
    if (hostSame != 1) Fail("T-906 F3: identical houses at 64 spots did not all key apart");
    /* the shares: 6000 spots along a street, each choice near its weight's share (1/6, 2/6, 3/6) */
    int got[3] = { 0, 0, 0 };
    for (int i = 0; i < 6000; ++i)
    {
        const int p[3] = { -47334 + (i % 100) * 7, 724, -55623 + (i / 100) * 11 };
        const unsigned long long r = PkWalk(list, DrawFromHash(PickHash(k, "42309-rebirth.mod", "13-gamedata.base", p, 0, 0), 6.0f));
        if (r >= 101 && r <= 103) ++got[r - 101]; else { Fail("T-906: a seeded walk left the map"); break; }
    }
    if (got[0] < 850 || got[0] > 1150 || got[1] < 1800 || got[1] > 2200 || got[2] < 2750 || got[2] > 3250) Fail("T-906: the seeded draw does not keep the weights' shares");
}
/* T-906: the list is found as the engine finds it - the first list whose set holds the record, the bucket walk stopping at a node of
   another bucket, an empty set skipped - and only a list of two or more choices is drawn; the rest is the engine's own call */
void t_t906_pick_find_list_and_pass()
{
    using namespace buildpick;
    if (sizeof(void*) != 8) return;
    /* records whose buckets (8 buckets) are chosen: r1 / r2 in bucket B, rx in another, r6 in B again */
    unsigned long long rec[64]; int nb = 0;
    unsigned long long r1 = 0, r2 = 0, r6 = 0, rx = 0, r3 = 0;
    for (unsigned long long v = 0x10000; v < 0x10000 + 8 * 4096 && nb < 64; v += 8) rec[nb++] = v;
    const unsigned long long B = EngineHash(rec[0]) & 7ull;
    r1 = rec[0];
    for (int i = 1; i < nb; ++i)
    {
        const unsigned long long bi = EngineHash(rec[i]) & 7ull;
        if (bi == B) { if (r2 == 0) r2 = rec[i]; else if (r6 == 0) r6 = rec[i]; else if (r3 == 0) r3 = rec[i]; }
        else if (rx == 0) rx = rec[i];
    }
    if (r2 == 0 || r6 == 0 || r3 == 0 || rx == 0) { Fail("T-906: the model found no records for its buckets"); return; }
    /* nodes {next, hash, key}: list 0's chain r1 -> rx -> r6 (r6 is past a node of another bucket: never found there); list 1 holds r1, r2;
       list 2 holds r3 but its set's size is 0 (skipped) */
    static unsigned long long n0[3], n1[3], n2[3], m0[3], m1[3], e0[3];
    static unsigned long long link0, link1, link2;
    static unsigned long long buckets0[8], buckets1[8], buckets2[8];
    n0[0] = (unsigned long long)(size_t)n1; n0[1] = EngineHash(r1); n0[2] = r1;
    n1[0] = (unsigned long long)(size_t)n2; n1[1] = EngineHash(rx); n1[2] = rx;
    n2[0] = 0;                                 n2[1] = EngineHash(r6); n2[2] = r6;
    m0[0] = (unsigned long long)(size_t)m1; m0[1] = EngineHash(r2); m0[2] = r2;
    m1[0] = 0;                                 m1[1] = EngineHash(r1); m1[2] = r1;
    e0[0] = 0; e0[1] = EngineHash(r3); e0[2] = r3;
    link0 = (unsigned long long)(size_t)n0; link1 = (unsigned long long)(size_t)m0; link2 = (unsigned long long)(size_t)e0;
    std::memset(buckets0, 0, sizeof buckets0); std::memset(buckets1, 0, sizeof buckets1); std::memset(buckets2, 0, sizeof buckets2);
    buckets0[B] = (unsigned long long)(size_t)&link0; buckets1[B] = (unsigned long long)(size_t)&link1; buckets2[B] = (unsigned long long)(size_t)&link2;
    static char lists[3 * 0xA0], fac[0x300];
    std::memset(lists, 0, sizeof lists); std::memset(fac, 0, sizeof fac);
    unsigned long long* bk[3] = { buckets0, buckets1, buckets2 };
    const unsigned long long sizes[3] = { 3, 2, 0 };
    for (int i = 0; i < 3; ++i)
    {
        char* l = lists + i * kListStride;
        *(unsigned long long*)(l + kSetBuckets) = 8; *(unsigned long long*)(l + kSetSize) = sizes[i]; *(unsigned long long**)(l + kSetBucketArr) = bk[i];
    }
    *(unsigned*)(fac + kFacListCount) = 3; *(char**)(fac + kFacListFirst) = lists;
    const char* l = 0;
    if (FindList(fac, r1, &l) != 0 || l != lists) Fail("T-906: a record in lists 0 and 1 did not take list 0 (the first wins)");
    if (FindList(fac, r2, &l) != 1 || l != lists + kListStride) Fail("T-906: a record only in list 1 was not found there");
    if (FindList(fac, r6, &l) != -1 || l != 0) Fail("T-906: a node past another bucket's node was taken (the engine stops there)");
    if (FindList(fac, r3, &l) != -1) Fail("T-906: a set of size 0 was searched (the engine skips it)");
    if (FindList(fac, rx + 0x100000, &l) != -1) Fail("T-906: a record in no list was found");
    *(unsigned*)(fac + kFacListCount) = 0;
    if (FindList(fac, r1, &l) != -1) Fail("T-906: a faction with no lists found one");
    /* only two or more choices are drawn; the rest is the engine's own call */
    if (Decide(0, 5, 6.0f) != kPassNoList) Fail("T-906: no list is not a pass-through");
    if (Decide(1, 0, 0.0f) != kPassEmpty) Fail("T-906: an empty list is not a pass-through (the engine makes nothing)");
    if (Decide(1, 1, 2.0f) != kPassOne) Fail("T-906: one choice is not a pass-through");
    volatile float zero = 0.0f;   /* a NaN made at run time, not folded by the compiler */
    if (Decide(1, 2, 0.0f) != kPassBadTotal || Decide(1, 2, -1.0f) != kPassBadTotal || Decide(1, 2, zero / zero) != kPassBadTotal) Fail("T-906: a total of 0, below 0 or not a number was drawn");
    if (Decide(1, 2, 3.0f) != kSeeded || Decide(1, 7, 0.5f) != kSeeded) Fail("T-906: two or more choices with a total are not drawn");
}
/* The top list is ordered by bytes (ties: more messages, then the lower type), never lists a type with no message and keeps only
   ten; totals are every type's bytes; each kind of link has its own table; a LIVE envelope's inner type is read at the right byte
   each way; the line has the agreed shape; Take empties the window. */
void t_t985_traffic_tally()
{
    using namespace nettally;
    static Table drop;
    for (int l = 0; l < kLinks; ++l) Take(l, &drop);   /* start from empty tables */
    static Dir d;
    std::memset(&d, 0, sizeof(d));
    for (int t = 1; t <= 12; ++t) Add(&d, (unsigned int)t, (size_t)(t * 100));   /* types 1-12: 100-1200 bytes, one message each */
    Add(&d, 200, 1200);   /* ties type 12 on bytes and count: the lower type (12) goes first */
    Add(&d, 7, 0);        /* type 7: a second message, no bytes - still placed by its 700 bytes */
    int ty[kTop];
    const int n = Top(d, ty, kTop);
    const int want[kTop] = { 12, 200, 11, 10, 9, 8, 7, 6, 5, 4 };
    bool same = (n == kTop);
    for (int i = 0; same && i < kTop; ++i) same = (ty[i] == want[i]);
    if (!same) Fail("the top ten is not 12,200,11,10,9,8,7,6,5,4 (n=" + I(n) + ", first=" + I(n > 0 ? ty[0] : -1) + ")");
    if (Bytes(d) != 9000) Fail("totals are not every type's bytes (9000): " + I((int)Bytes(d)));
    if (DirText(d).find("12:1/1200,200:1/1200,11:1/1100,") != 0) Fail("the token does not start 12:1/1200,200:1/1200,11:1/1100,: " + DirText(d));
    static Dir e;
    std::memset(&e, 0, sizeof(e));
    Add(&e, 30, 5000); Add(&e, 31, 2500); Add(&e, 31, 2500);
    if (DirText(e) != "31:2/5000,30:1/5000") Fail("equal bytes: more messages first: " + DirText(e));
    std::memset(&e, 0, sizeof(e));
    if (DirText(e) != "-" || Top(e, ty, kTop) != 0) Fail("an empty table lists a type");
    CountOut(kLinkSession, 59, 100);
    CountOut(kLinkWorld, 59, 40);
    CountIn(kLinkWorld, 53, 30);
    const char down[12] = { 1, 0, 12, 0, 4, 0, 0, 0, 9, 9, 9, 9 };   /* origin slot 1, inner type 12 at byte 2 */
    const char up[16] = { 1, 14, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 9, 9, 9, 9 };   /* route 1, inner type 14 at byte 1 */
    CountLiveIn(kLinkWorld, true, down, sizeof(down));
    CountLiveIn(kLinkBridge, false, up, sizeof(up));
    CountLiveIn(kLinkWorld, true, down, 7);   /* shorter than the down header: not counted */
    CountLiveIn(kLinkBridge, false, up, 11);  /* shorter than the up header: not counted */
    static Table w;
    Take(kLinkSession, &w);
    if (ReportLine(kLinkSession, 60, w) != "[NET] TRAFFIC REPORT link=session secs=60 out[type:count/bytes]=59:1/100 in[type:count/bytes]=- liveIn[inner:count/bytes]=- totals[outB,inB]=100,0")
        Fail("session line: " + ReportLine(kLinkSession, 60, w));
    Take(kLinkWorld, &w);
    if (ReportLine(kLinkWorld, 61, w) != "[NET] TRAFFIC REPORT link=world secs=61 out[type:count/bytes]=59:1/40 in[type:count/bytes]=53:1/30 liveIn[inner:count/bytes]=12:1/17 totals[outB,inB]=40,30")
        Fail("world line: " + ReportLine(kLinkWorld, 61, w));
    Take(kLinkBridge, &w);
    if (ReportLine(kLinkBridge, 59, w) != "[NET] TRAFFIC REPORT link=bridge secs=59 out[type:count/bytes]=- in[type:count/bytes]=- liveIn[inner:count/bytes]=14:1/21 totals[outB,inB]=0,0")
        Fail("bridge line: " + ReportLine(kLinkBridge, 59, w));
    for (int l = 0; l < kLinks; ++l) { Take(l, &w); if (!Empty(w)) Fail("a window is not empty after Take: link " + I(l)); }
}
/* T-999: the keep question for a bound squad - each verdict, and which ones answer 'keep' */
void t_t999_keep_decide()
{
    using namespace sqbirth;
    if (KeepDecide(0, kBirthKeep, 0, 1, 1, 1, 1) != kKeepDeadRelease || KeepAnswers(kKeepDeadRelease) != 0) Fail("T-999: a squad with nobody living was kept");
    if (KeepDecide(-2, kBirthKeep, 0, 1, 1, 1, 1) != kKeepDeadRelease) Fail("T-999: a squad with no members (emptied by hiring or merging) was kept");
    if (KeepDecide(3, kBirthKeep, 0, 0, 0, 0, 1) != kKeepBorn || KeepAnswers(kKeepBorn) != 1) Fail("T-999: a squad born kept was not kept by its birth flag");
    if (KeepDecide(3, kBirthKeep, 1, 0, 0, 0, 1) != kKeepEngine) Fail("T-999 fold 2b: a kept byte read off a roaming-listed squad with no home was kept by the mod");
    if (KeepDecide(3, kBirthKeep, 1, 0, 0, 0, 0) != kKeepUnknown) Fail("T-999 fold 2b: a kept byte read off a squad whose template is not a roaming one was not kept");
    if (KeepDecide(3, kBirthKeep, 1, 0, 1, 1, -1) != kKeepUnknown) Fail("T-999 fold 2b: a kept byte read off a squad whose template could not be read was not kept");
    if (KeepDecide(3, kBirthDiscard, 0, 0, 0, 0, 0) != kKeepEngine || KeepDecide(3, kBirthDiscard, 0, 0, 0, 0, -1) != kKeepEngine) Fail("T-999 fold 2b: the template test changed a squad born not kept");
    if (KeepDecide(3, kBirthKeep, 1, 0, 1, 0, 1) != kKeepPlayerHeld || KeepDecide(3, kBirthKeep, 1, 0, 0, 1, 1) != kKeepOtherHolder) Fail("T-999: a kept byte read off a protected squad was not kept");
    if (KeepDecide(3, kBirthKeep, 1, 1, 0, 0, 1) != kKeepHome || KeepDecide(3, kBirthDiscard, 1, 1, 0, 0, 1) != kKeepHome || KeepDecide(3, kBirthDiscard, 0, 1, 0, 0, 1) != kKeepHome || KeepAnswers(kKeepHome) != 1) Fail("T-999: a squad with a home building (a town's resident, its byte cleared or not) was not kept");
    if (KeepDecide(3, kBirthUnknown, 0, 0, 0, 0, 1) != kKeepUnknown || KeepAnswers(kKeepUnknown) != 1) Fail("T-999: a squad with no birth flag was not kept");
    if (KeepDecide(3, kBirthDiscard, 0, 0, 1, 1, 1) != kKeepPlayerHeld || KeepAnswers(kKeepPlayerHeld) != 1) Fail("T-999: a squad holding a player's character was not kept");
    if (KeepDecide(3, kBirthDiscard, 0, 0, 0, 1, 1) != kKeepOtherHolder || KeepAnswers(kKeepOtherHolder) != 1) Fail("T-999: a squad in an area another game holds was not kept");
    if (KeepDecide(3, kBirthDiscard, 0, 0, 0, 0, 1) != kKeepEngine || KeepAnswers(kKeepEngine) != 0) Fail("T-999: a squad born not kept with nothing to protect was kept");
    if (KeepDecide(3, kBirthDiscard, 1, 0, 0, 0, 1) != kKeepEngine) Fail("T-999: a loaded squad whose kept byte says not kept was kept");
    if (MadeKeptByte(kBirthDiscard) != 0 || MadeKeptByte(kBirthKeep) != 1 || MadeKeptByte(kBirthUnknown) != 1) Fail("T-999: a made squad's kept byte does not follow its birth flag");
    if (DestroyDelete(0, 0, 1, 3, 0, 0) != kDelNone) Fail("T-999: an unbound squad's destroy sent a delete");
    if (DestroyDelete(1, 0, 1, 3, 0, 0) != kDelDiscard) Fail("T-999: the discard of a squad born not kept was not a discard delete");
    if (DestroyDelete(1, 1, 1, 3, 0, 0) != kDelPlain || DestroyDelete(1, 0, 0, 3, 0, 0) != kDelPlain) Fail("T-999: a wipe-out or another destroy was a discard delete");
    if (DestroyDelete(1, 0, 1, 0, 0, 0) != kDelPlain || DestroyDelete(1, 0, 1, -2, 0, 0) != kDelPlain) Fail("T-999: a squad with no living members at its destroy (dead, or emptied) sent a discard delete");
    if (DestroyDelete(1, 0, 1, 3, 0, 1) != kDelNone) Fail("T-999: the discard of one of a same-id pair deleted the record the other names");
}
/* T-999 fold 1: the birth flag a keep question uses and the one a game publishes; the game's own index reader's field 16 */
void t_t999_fold1()
{
    using namespace sqbirth;
    int fl = 9;
    if (BirthChoose(kBirthDiscard, kBirthKeep, kBirthKeep, &fl) != kBirthDiscard || fl != 0) Fail("T-999 fold: a flag noted at creation did not come first");
    if (BirthChoose(kBirthUnknown, kBirthKeep, kBirthDiscard, &fl) != kBirthKeep || fl != 0) Fail("T-999 fold: the bound record's known flag did not come before the squad's own byte");
    if (BirthChoose(kBirthUnknown, kBirthUnknown, kBirthKeep, &fl) != kBirthKeep || fl != 1) Fail("T-999 fold: a flag read off the squad was not marked as read");
    if (BirthChoose(kBirthUnknown, kBirthUnknown, kBirthUnknown, &fl) != kBirthUnknown || fl != 0) Fail("T-999 fold: no flag at all was not unknown");
    if (BirthToPublish(kBirthKeep, 1) != kBirthUnknown || BirthToPublish(kBirthDiscard, 1) != kBirthUnknown) Fail("T-999 fold: a flag read off the squad was published as definite");
    if (BirthToPublish(kBirthKeep, 0) != kBirthKeep || BirthToPublish(kBirthDiscard, 0) != kBirthDiscard || BirthToPublish(kBirthUnknown, 0) != kBirthUnknown) Fail("T-999 fold: a creation flag was not published as noted");
    coopstore::MetaLine m; m.version = 7; m.writtenAt = 5; m.owner = 1; m.x = 1.0f; m.y = 2.0f; m.z = 3.0f; m.squadSid = "sq"; m.factionName = "fac"; m.worldId = "Fac_1"; m.town = "town"; m.len = 0; m.seq = 9;
    for (int bth = -1; bth <= 1; ++bth)
        for (int hm = 0; hm < 2; ++hm)
        {
            m.birth = bth; m.home = hm != 0 ? std::string("hk") : std::string();
            std::string line;
            if (!coopstore::MetaEncodeV7(m, &line)) { Fail("T-999 fold: a line was not written"); continue; }
            std::vector<std::string> f; size_t at = 0;   /* store.cpp ParseMetaLine's own split */
            while (at <= line.size()) { size_t t = line.find('\t', at); if (t == std::string::npos) { f.push_back(line.substr(at)); break; } f.push_back(line.substr(at, t - at)); at = t + 1; }
            if (BirthOfLineFields(f) != bth) Fail("T-999 fold: the game's own index reader does not read field 16 back");
        }
    std::vector<std::string> g(16, std::string("0")); g[0] = "v7"; g[15] = "d\r\n";
    if (BirthOfLineFields(g) != kBirthDiscard) Fail("T-999 fold: field 16 with a line end was not read");
    g[0] = "v6";
    if (BirthOfLineFields(g) != kBirthUnknown) Fail("T-999 fold: a line below v7 read a birth flag");
    g.resize(15); g[0] = "v7";
    if (BirthOfLineFields(g) != kBirthUnknown) Fail("T-999 fold: a line without field 16 read a birth flag");
}
/* T-999 fold 2: a squad whose members cannot be read is kept, and its destroy while discarding is a discard delete; the world server's
   held discard deletes */
void t_t999_fold2()
{
    using namespace sqbirth;
    if (KeepDecide(-1, kBirthDiscard, 0, 0, 0, 0, 1) != kKeepNotRead || KeepDecide(-1, kBirthKeep, 1, 0, 0, 0, 1) != kKeepNotRead || KeepAnswers(kKeepNotRead) != 1) Fail("T-999 fold 2: a squad whose members could not be read was not kept");
    if (KeepDecide(-2, kBirthDiscard, 0, 0, 0, 0, 1) != kKeepDeadRelease) Fail("T-999 fold 2: a squad read with no awake members did not take the dead road");
    if (LivingUnloadRoad(-1, 0) != 1 || LivingUnloadRoad(2, 0) != 1 || LivingUnloadRoad(0, 0) != 0 || LivingUnloadRoad(-2, 0) != 0) Fail("T-999 fold 2: the unload road");
    if (DestroyDelete(1, 0, 1, -1, 0, 0) != kDelDiscard) Fail("T-999 fold 2: a discarding squad whose members could not be read at its destroy did not send the discard delete");
    if (DestroyDelete(1, 0, 1, -1, 0, 1) != kDelNone || DestroyDelete(1, 0, 0, -1, 0, 0) != kDelPlain || DestroyDelete(1, 1, 1, -1, 0, 0) != kDelPlain) Fail("T-999 fold 2: a destroy not read that is shared, not discarding, or a wipe-out");
    if (PendingDiscardStep(1, 10, 10, "B", "A", 0, 0) != kPdKeep) Fail("T-999 fold 2: a held discard delete was applied while another game held the area");
    if (PendingDiscardStep(1, 10, 10, "", "A", 1, 0) != kPdKeep || PendingDiscardStep(1, 10, 10, "A", "A", 1, 0) != kPdKeep) Fail("T-999 fold 2: a held discard delete was applied while another game had the area loaded");
    if (PendingDiscardStep(1, 10, 10, "", "A", 0, 0) != kPdApply || PendingDiscardStep(1, 9, 10, "A", "A", 0, 0) != kPdApply) Fail("T-999 fold 2: a held discard delete with no other holder or loaded game was not applied");
    if (PendingDiscardStep(1, 11, 10, "", "A", 0, 0) != kPdDrop) Fail("T-999 fold 2: a held discard delete was not dropped when its record was written again with a newer stamp");
    if (PendingDiscardStep(0, 0, 10, "", "A", 0, 0) != kPdDrop) Fail("T-999 fold 2: a held discard delete for a record gone another way was not dropped");
    if (PendingDiscardStep(1, 9, 10, "A", "A", 0, PendingDiscardSenderReturned(1, 1)) != kPdDrop) Fail("T-999 fold 2c: a held discard delete was applied after its sender left and came back to the area");
    if (PendingDiscardStep(1, 9, 10, "A", "A", 0, PendingDiscardSenderReturned(0, 1)) != kPdApply) Fail("T-999 fold 2c: a sender still listing the area before any leave was seen dropped its own held delete");
    if (PendingDiscardSenderReturned(1, 0) != 0 || PendingDiscardSenderReturned(0, 0) != 0) Fail("T-999 fold 2c: a sender not in the area counted as back");
    if (PendingDiscardDropOnWrite(10, 11) != 1 || PendingDiscardDropOnWrite(10, 10) != 0 || PendingDiscardDropOnWrite(10, 9) != 0) Fail("T-999 fold 2: a write dropped a held discard delete it is not newer than, or kept one it is");
    if (PendingDiscardAdmit(0, 2, 0) != 1 || PendingDiscardAdmit(2, 2, 0) != 0 || PendingDiscardAdmit(2, 2, 1) != 1) Fail("T-999 fold 2: the held registry's bound");
}
/* T-999 fold 3: a squad the engine's own unload removals emptied keeps its discard mark through the empty keep question, and its
   destroy is the discard delete; one emptied by hiring or merging keeps the plain delete; the death watch's wipe-out wins */
/* T-966: the re-run holds the plan load only when the switch-on would load a plan over a save's pieces; the name is put back
   right after the engine call with no way out between [t966-tests] */
void t_t966_plan_hold()
{
    namespace lh = layouthold;
    if (lh::Decide(0, 1, 0, 3, 0, 5) != lh::kWhyUnread) Fail("an unread interior was decided");
    if (lh::Decide(1, 0, 0, 3, 0, 5) != lh::kWhyNoInterior) Fail("a building with no interior was held");
    if (lh::Decide(1, 1, 1, 3, 0, 5) != lh::kWhyOn) Fail("a switched-on interior was held (its switch-on loads nothing)");
    if (lh::Decide(1, 1, 0, 0, 0, 5) != lh::kWhyNoName) Fail("an empty plan name was held");
    if (lh::Decide(1, 1, 0, 3, 2, 5) != lh::kWhyPlanListed) Fail("a building whose plan list holds pieces was held");
    if (lh::Decide(1, 1, 0, 3, 0, 0) != lh::kWhyNotSaved) Fail("a building never furnished was refused its plan");
    if (lh::Decide(1, 1, 0, lh::kNameCap, 0, 5) != lh::kWhyNameLong) Fail("a name longer than the restore copy was held");
    if (lh::Decide(1, 1, 0, lh::kNameCap - 1, 0, 5) != lh::kWhyHold) Fail("the longest name the copy keeps was not held");
    if (lh::Decide(1, 1, 0, 3, 0, 1) != lh::kWhyHold) Fail("a save-loaded building's plan load was not held");
    int holds = 0;
    for (int w = 0; w < lh::kWhyCount; ++w) { if (lh::Holds(w)) ++holds; if (std::string(lh::WhyName(w)) == "?") Fail("a reason has no word"); }
    if (holds != 1) Fail("not exactly one reason holds");
    for (int rd = 0; rd < 2; ++rd) for (int hi = 0; hi < 2; ++hi) for (int on = 0; on < 2; ++on)
        for (int nm = 0; nm < 2; ++nm) for (int pl = 0; pl < 2; ++pl) for (int pc = 0; pc < 2; ++pc)
        {
            const bool want = rd != 0 && hi != 0 && on == 0 && nm != 0 && pl == 0 && pc != 0;
            if (lh::Holds(lh::Decide(rd, hi, on, nm != 0 ? 9ULL : 0ULL, pl != 0 ? 4 : 0, pc != 0 ? 7ULL : 0ULL)) != want) Fail("the truth table answered wrong");
        }
    /* source: inside T392RerunBuilding the blank, the one engine call and the restore come in that order with no return between,
       and WorldGenRerunPopulate catches a fault inside the engine call */
    const std::string tg = T313ReadFile("../coop-plugin/towngen.cpp"), wg = T313ReadFile("../coop-plugin/worldgen.cpp");
    if (tg.empty() || wg.empty()) { Fail("towngen.cpp / worldgen.cpp not read"); return; }
    const size_t f = tg.find("static int T392RerunBuilding(");
    const size_t e = (f == std::string::npos) ? std::string::npos : tg.find("\n}", f);
    if (f == std::string::npos || e == std::string::npos) { Fail("T392RerunBuilding not found"); return; }
    const std::string body = tg.substr(f, e - f);
    const size_t b0 = body.find("planHeld = T966SetNamePod(b, &blank);");
    const size_t c0 = body.find("WorldGenRerunPopulate(factory, b);");
    const size_t r0 = body.find("planBack = T966SetNamePod(b, &back);");
    if (b0 == std::string::npos || c0 == std::string::npos || r0 == std::string::npos) { Fail("the blank, the call or the restore is missing"); return; }
    if (!(b0 < c0 && c0 < r0)) Fail("the blank, the engine call and the restore are out of order");
    if (body.find("WorldGenRerunPopulate(", c0 + 1) != std::string::npos) Fail("a second engine call in the re-run");
    const size_t ret = body.find("return", b0);
    if (ret != std::string::npos && ret < r0) Fail("a return between the blank and the restore");
    if (body.find("if (planHeld != 0) { const std::string back(planName, (size_t)planLen);") == std::string::npos) Fail("the restore is not keyed on the blank");
    const size_t w0 = wg.find("int WorldGenRerunPopulate(void* factory, void* building)");
    if (w0 == std::string::npos || wg.find("__except (EXCEPTION_EXECUTE_HANDLER) { TownGenResidentsDone(0); return -1; }", w0) == std::string::npos)
        Fail("WorldGenRerunPopulate no longer catches a fault inside the engine call");
}
void t_t999_fold3()
{
    using namespace sqbirth;
    if (DestroyDelete(1, 0, 1, -2, 1, 0) != kDelDiscard) Fail("T-999 fold 3: a discarding squad emptied by its area unload did not send the discard delete");
    if (DestroyDelete(1, 0, 1, -2, 0, 0) != kDelPlain) Fail("T-999 fold 3: a squad emptied by hiring or merging did not keep the plain delete");
    if (DestroyDelete(1, 1, 1, -2, 1, 0) != kDelPlain || DestroyDelete(1, 1, 1, 3, 1, 0) != kDelPlain) Fail("T-999 fold 3: a wipe-out the death watch saw was not the plain delete");
    if (DestroyDelete(1, 0, 1, -2, 1, 1) != kDelNone) Fail("T-999 fold 3: the unload discard of one of a same-id pair deleted the record the other names");
    if (DestroyDelete(1, 0, 0, -2, 1, 0) != kDelPlain || DestroyDelete(0, 0, 1, -2, 1, 0) != kDelNone) Fail("T-999 fold 3: a squad not discarding, or unbound, took the unload road");
    if (DestroyDelete(1, 0, 1, 0, 1, 0) != kDelPlain) Fail("T-999 fold 3: a squad whose members are all dead took the unload road");
    if (LivingUnloadRoad(-2, 1) != 1 || LivingUnloadRoad(-2, 0) != 0 || LivingUnloadRoad(0, 1) != 0 || LivingUnloadRoad(2, 0) != 1 || LivingUnloadRoad(-1, 0) != 1) Fail("T-999 fold 3: the unload road");
    if (DiscardMarkErased(0, 0) != 1 || DiscardMarkErased(0, 1) != 1) Fail("T-999 fold 3: a wiped-out squad kept its discard mark");
    if (DiscardMarkErased(-2, 0) != 1) Fail("T-999 fold 3: a squad emptied by hiring or merging kept its discard mark");
    if (DiscardMarkErased(-2, 1) != 0) Fail("T-999 fold 3: a squad emptied by its area unload lost its discard mark at the empty keep question");
}
/* T-1080: a squad the orphan purge emptied keeps its record at its destroy; a wipe-out is deleted; the mark is used once */
void t_t1080_purge_emptied_destroy()
{
    using namespace sqbirth;
    if (DestroyDeleteMod(1, 1, 0, 0, -1, 0, 0) != kDelModKept) Fail("T-1080: a squad the purge emptied, not discarding, sent a delete at its destroy");
    if (DestroyDeleteMod(1, 1, 0, 1, -2, 0, 0) != kDelModKept || DestroyDeleteMod(1, 1, 0, 1, 0, 0, 0) != kDelModKept) Fail("T-1080: a squad the purge emptied, discarding, off the unload road, sent a delete at its destroy");
    if (DestroyDeleteMod(1, 1, 0, 1, -1, 0, 1) != kDelNone) Fail("T-1080: a purge-emptied squad of a same-id pair took another road than the same-id keep");
    if (DestroyDeleteMod(0, 1, 1, 0, -1, 0, 0) != kDelPlain || DestroyDeleteMod(0, 1, 1, 1, 0, 0, 0) != kDelPlain) Fail("T-1080: a wipe-out the death watch saw was not deleted");
    if (DestroyDeleteMod(1, 1, 1, 0, -1, 0, 0) != kDelPlain || DestroyDeleteMod(1, 1, 1, 1, -2, 1, 0) != kDelPlain) Fail("T-1080: a wipe-out the death watch saw was kept because the purge also marked it");
    if (DestroyDeleteMod(1, 0, 0, 0, -1, 0, 0) != kDelNone || DestroyDeleteMod(1, 0, 1, 1, -2, 1, 0) != kDelNone) Fail("T-1080: an unbound squad's destroy sent a delete");
    {   /* with no purge mark the destroy is DestroyDelete's, every input */
        int same = 1;
        for (int b = 0; b <= 1; ++b) for (int d = 0; d <= 1; ++d) for (int g = 0; g <= 1; ++g) for (int l = -2; l <= 3; ++l) for (int u = 0; u <= 1; ++u) for (int s = 0; s <= 1; ++s)
            if (DestroyDeleteMod(0, b, d, g, l, u, s) != DestroyDelete(b, d, g, l, u, s)) same = 0;
        if (same == 0) Fail("T-1080: a destroy with no purge mark took another road than DestroyDelete");
    }
    {   /* the mark: set when the purge reads the copy empty, taken once by the destroy */
        std::set<void*> marks; int a = 0, b = 0; void* pa = &a; void* pb = &b;
        if (coopsquad::NpcPurgeForgetStamp(1, 1, 0) == 1) marks.insert(pa);
        if (coopsquad::NpcPurgeForgetStamp(1, 1, 2) == 1) marks.insert(pb);   /* the purge took people out, others are left: not marked */
        if (ModRemovedTake(&marks, pb) != 0) Fail("T-1080: a squad the purge did not empty was marked");
        const int first = ModRemovedTake(&marks, pa);
        if (first != 1 || DestroyDeleteMod(first, 1, 0, 0, -1, 0, 0) != kDelModKept) Fail("T-1080: the purge-emptied mark was not used at the destroy");
        const int second = ModRemovedTake(&marks, pa);
        if (second != 0 || !marks.empty() || DestroyDeleteMod(second, 1, 0, 0, -1, 0, 0) != kDelPlain) Fail("T-1080: the purge-emptied mark outlived its destroy (a later squad at the address would keep its record)");
        if (ModRemovedTake<void*>(0, pa) != 0) Fail("T-1080: no mark set answered marked");
    }
}
/* T-1080 fold 1: the mark counts the living (bodies do not count, a failed read marks); the discard delete keeps its own rule */
void t_t1080_fold1()
{
    using namespace sqbirth;
    if (ModRemovedMark(1, 1, 0) != 1) Fail("T-1080 fold 1: the purge left only a body and the squad was not marked");
    if (ModRemovedMark(1, 1, -2) != 1) Fail("T-1080 fold 1: the purge left no member and the squad was not marked");
    if (ModRemovedMark(1, 1, -1) != 1) Fail("T-1080 fold 1: the members could not be read and the squad was not marked (a failed read would delete a world record)");
    if (ModRemovedMark(1, 1, 1) != 0 || ModRemovedMark(1, 1, 3) != 0) Fail("T-1080 fold 1: a squad with someone living was marked");
    if (ModRemovedMark(1, 0, 0) != 0 || ModRemovedMark(1, 0, -1) != 0) Fail("T-1080 fold 1: a squad the purge never touched was marked (a real wipe-out would keep its record)");
    if (ModRemovedMark(0, 1, 0) != 0) Fail("T-1080 fold 1: not a world NPC squad, marked");
    {   /* the body case end to end: one guard dead, the living purged - marked, then its destroy keeps the record */
        std::set<void*> marks; int a = 0; void* pa = &a;
        if (ModRemovedMark(1, 1, 0) == 1) marks.insert(pa);
        if (DestroyDeleteMod(ModRemovedTake(&marks, pa), 1, 0, 0, -1, 0, 0) != kDelModKept) Fail("T-1080 fold 1: a squad left holding a body sent the plain delete");
    }
    if (DestroyDeleteMod(1, 1, 0, 1, -2, 1, 0) != kDelDiscard || DestroyDeleteMod(1, 1, 0, 1, 3, 0, 0) != kDelDiscard || DestroyDeleteMod(1, 1, 0, 1, -1, 0, 0) != kDelDiscard)
        Fail("T-1080 fold 1: the purge mark overrode the discard delete of a squad born not kept");
    if (DestroyDeleteMod(1, 1, 0, 1, -2, 1, 1) != kDelNone) Fail("T-1080 fold 1: the purge mark overrode the same-id keep");
    {   /* the mark only ever turns the plain delete into a keep */
        int ok = 1;
        for (int b = 0; b <= 1; ++b) for (int d = 0; d <= 1; ++d) for (int g = 0; g <= 1; ++g) for (int l = -2; l <= 3; ++l) for (int u = 0; u <= 1; ++u) for (int s = 0; s <= 1; ++s)
        {
            const int was = DestroyDelete(b, d, g, l, u, s), now = DestroyDeleteMod(1, b, d, g, l, u, s);
            if (now != (was == kDelPlain && d == 0 ? (int)kDelModKept : was)) ok = 0;
        }
        if (ok == 0) Fail("T-1080 fold 1: the purge mark changed a destroy other than a plain delete with no wipe-out");
    }
}
/* T-1080 fold 3: a squad carrying the mod-removal mark keeps its record; an unmarked emptied squad - its runner ending it - still
   deletes; the wiring */
size_t T1080BodyEnd(const std::string& src, size_t at) { return at == std::string::npos ? std::string::npos : src.find("\n}", at); }
/* the definition of `head` (an occurrence followed by '{' before any ';' - a declaration is skipped); npos = none */
size_t T1080DefAt(const std::string& src, const char* head)
{
    for (size_t s = src.find(head); s != std::string::npos; s = src.find(head, s + 1))
        if (src.find('{', s) < src.find(';', s)) return s;
    return std::string::npos;
}
int T1080BodyHas(const std::string& src, const char* head, const char* needle)
{
    const size_t s = T1080DefAt(src, head), e = T1080BodyEnd(src, s);
    return (s != std::string::npos && e != std::string::npos && src.find(needle, s) < e) ? 1 : 0;
}
void t_t1080_fold3()
{
    using namespace sqbirth;
    /* the runner ending its squad: hired away, merged, the last member gone - no mark, the plain delete as before */
    if (DestroyDeleteMod(0, 1, 0, 0, -1, 0, 0) != kDelPlain || DestroyDeleteMod(0, 1, 0, 0, -2, 0, 0) != kDelPlain) Fail("T-1080 fold 3: an unmarked emptied squad was not deleted");
    if (DestroyDeleteMod(0, 1, 0, 1, -2, 0, 0) != kDelPlain) Fail("T-1080 fold 3: an unmarked squad emptied by hiring or merging while discarding was not deleted");
    if (DestroyDeleteMod(0, 1, 1, 0, -1, 0, 0) != kDelPlain) Fail("T-1080 fold 3: a wipe-out was not deleted");
    /* the same squad emptied by this game's own removal (the last living person removed): marked, kept */
    {
        std::set<void*> marks; int a = 0, b = 0; void* pa = &a; void* pb = &b;
        if (ModRemovedMark(1, 1, 0) == 1) marks.insert(pa);
        if (ModRemovedMark(1, 1, 2) == 1) marks.insert(pb);   /* the removal left two living: not marked */
        if (DestroyDeleteMod(ModRemovedTake(&marks, pa), 1, 0, 0, -1, 0, 0) != kDelModKept) Fail("T-1080 fold 3: a squad this game's own removal emptied was deleted");
        if (DestroyDeleteMod(ModRemovedTake(&marks, pb), 1, 0, 0, -2, 0, 0) != kDelPlain) Fail("T-1080 fold 3: a squad the removal left living ones in, later emptied by its runner, was kept");
        if (DestroyDeleteMod(ModRemovedTake(&marks, pa), 1, 0, 0, -1, 0, 0) != kDelPlain) Fail("T-1080 fold 3: the mark outlived its destroy");
    }
    if (DestroyDeleteMod(1, 1, 1, 0, -1, 0, 0) != kDelPlain) Fail("T-1080 fold 3: the mark kept a wipe-out the death watch marked");
    if (DestroyDeleteMod(1, 1, 0, 1, 3, 0, 0) != kDelDiscard || DestroyDeleteMod(1, 1, 0, 1, -2, 1, 1) != kDelNone || DestroyDeleteMod(1, 0, 0, 0, -1, 0, 0) != kDelNone)
        Fail("T-1080 fold 3: the mark changed the discard delete, the same-id keep or an unbound destroy");
    /* the wiring */
    const std::string st = T313ReadFile("../coop-plugin/store.cpp");
    const std::string wg = T313ReadFile("../coop-plugin/worldgen.cpp");
    const std::string ws = T313ReadFile("../coop-plugin/worldstate.cpp");
    if (st.empty() || wg.empty() || ws.empty()) { Fail("T-1080 fold 3: could not read the sources"); return; }
    if (T1080BodyHas(st, "void detour_destroyPlatoon(void* faction, void* platoon)", "sqbirth::DestroyDeleteMod(modRemoved, 1,") == 0) Fail("T-1080 fold 3: the engine-destroy road does not ask the mark");
    if (T1080BodyHas(st, "void DrainGoneEntry(", "sqbirth::DestroyDeleteMod(offMod,") == 0) Fail("T-1080 fold 3: the drained destroy does not ask the mark");
    if (T1080BodyHas(st, "void StoreOrphanEmptiedSquad(", "ModRemovedMarkNow(platoon, 1, LivingMembersPod(platoon))") == 0) Fail("T-1080 fold 3: the purge does not mark");
    {
        const size_t s = T1080DefAt(wg, "bool DestroyLocalObject(void* obj)"), e = T1080BodyEnd(wg, s);
        const size_t n = s == std::string::npos ? std::string::npos : wg.find("ModRemovalNote(obj);", s), w = s == std::string::npos ? std::string::npos : wg.find("w->destroy(", s);
        if (e == std::string::npos || n > e || w > e || n > w) Fail("T-1080 fold 3: DestroyLocalObject does not note the removal before the destroy");
    }
    if (T1080BodyHas(ws, "int WsDupDestroyPod(::Character* c)", "ModRemovalNote(") != 0) Fail("T-1080 fold 3: the duplicate-named removal (this game's own extra body, in an area it runs) marks its squad");
}
/* T-1080 fold 4: a squad this game made from a record with no living member of its own here keeps its record at an empty destroy; a
   squad whose own living members here all died deletes; the wiring */
void t_t1080_fold4()
{
    using namespace sqbirth;
    if (ModMarkClearedBy(1) != 1 || ModMarkClearedBy(3) != 1) Fail("T-1080 fold 4: a living member seen here did not clear the mark");
    if (ModMarkClearedBy(0) != 0 || ModMarkClearedBy(-2) != 0 || ModMarkClearedBy(-1) != 0) Fail("T-1080 fold 4: bodies only, no members or an unread squad cleared the mark");
    {   /* made from its record, woke with nobody, the engine destroys it empty: kept */
        std::set<void*> marks; int a = 0; void* pa = &a;
        marks.insert(pa);
        if (DestroyDeleteMod(ModRemovedTake(&marks, pa), 1, 0, 0, -2, 0, 0) != kDelModKept) Fail("T-1080 fold 4: a squad made from its record with nobody of its own here was deleted at its empty destroy");
    }
    {   /* made from its record, its people stood here (the watch saw them living), then all died */
        std::set<void*> marks; int b = 0; void* pb = &b;
        marks.insert(pb);
        if (ModMarkClearedBy(2) != 0) marks.erase(pb);
        if (DestroyDeleteMod(ModRemovedTake(&marks, pb), 1, 1, 0, 0, 0, 0) != kDelPlain) Fail("T-1080 fold 4: a squad whose living members here died (the watch's wipe-out) was kept");
        if (DestroyDeleteMod(ModRemovedTake(&marks, pb), 1, 0, 0, 0, 0, 0) != kDelPlain) Fail("T-1080 fold 4: a squad whose living members here died before the watch marked it was kept");
    }
    if (DestroyDeleteMod(1, 1, 1, 0, 0, 0, 0) != kDelPlain) Fail("T-1080 fold 4: a marked squad's wipe-out was kept");
    const std::string st = T313ReadFile("../coop-plugin/store.cpp");
    if (st.empty()) { Fail("T-1080 fold 4: could not read store.cpp"); return; }
    if (T1080BodyHas(st, "static int CreateUnknownSquad(const Record& r, const char* road)", "ModMadeNote(p, kModMadeCreated)") == 0) Fail("T-1080 fold 4: a squad made from its record is not marked");
    if (T1080BodyHas(st, "void detour_activate(void* platoon)", "ModMadeNote(platoon, kModMadeLoaded)") == 0) Fail("T-1080 fold 4: the wake's reload does not mark");
    if (T1080BodyHas(st, "unsigned char detour_loadFromDisk(", "ModMadeNote(applyPlatoon, kModMadeLoaded)") == 0) Fail("T-1080 fold 4: the engine's read of the record at a wake does not mark");
    if (T1080BodyHas(st, "void DeathWatchTick()", "sqbirth::ModMarkClearedBy(living)") == 0) Fail("T-1080 fold 4: the death watch does not clear the mark on a living member");
    if (T1080BodyHas(st, "void detour_deactivate(void* platoon, void* container)", "g_modRemoved.erase(") != 0 || T1080BodyHas(st, "int HbPublishOne(void* platoon, DWORD now, bool wasDirty)", "g_modRemoved.erase(") != 0)
        Fail("T-1080 fold 4: a record write clears the mark");
}
/* T-1080 fold 5: placed from its record, woke with nobody, kept -> not placed again in this world; a squad only the purge emptied is
   placed again when its area is this game's to run; the wiring */
void t_t1080_fold5()
{
    using namespace sqbirth;
    {   /* the made squad: CreateUnknownSquad marks it in both sets, it wakes with nobody (-2), the engine discards it */
        std::set<void*> removed, made; int a = 0; void* pa = &a;
        removed.insert(pa); made.insert(pa);
        const int mod = ModRemovedTake(&removed, pa), mk = ModRemovedTake(&made, pa);
        const int del = DestroyDeleteMod(mod, 1, 0, 0, -2, 0, 0);
        if (del != kDelModKept) Fail("T-1080 fold 5: the squad made from its record that woke with nobody was not kept");
        const int failed = MadeEmptyPlaceFailed(del, mk);
        if (failed != 1) Fail("T-1080 fold 5: a kept made-empty squad was not entered as failed for this world");
        const int place = townpending::Placeable(0, 0, -1, 1, failed);
        if (place != townpending::kPlaceNoGame || townpending::PlaceNow(place, 0, 0, 1) != 0) Fail("T-1080 fold 5: a made-empty squad is placed again from the same record");
        if (townpending::NoPlacer(place) != 1) Fail("T-1080 fold 5: a made-empty squad's note still holds its town back");
    }
    {   /* the purge-emptied squad: removal mark only - placed again from its record when its area is this game's */
        const int del = DestroyDeleteMod(1, 1, 0, 0, -2, 0, 0);
        const int failed = MadeEmptyPlaceFailed(del, 0);
        if (del != kDelModKept || failed != 0) Fail("T-1080 fold 5: a purge-emptied squad was entered as failed");
        const int place = townpending::Placeable(0, 0, -1, 1, failed);
        if (place != townpending::kPlaceByThisGame || townpending::PlaceNow(place, 0, 0, 1) != 1) Fail("T-1080 fold 5: a purge-emptied squad is not placed from its record when its area is this game's");
    }
    if (MadeEmptyPlaceFailed(kDelPlain, 1) != 0 || MadeEmptyPlaceFailed(kDelDiscard, 1) != 0 || MadeEmptyPlaceFailed(kDelNone, 1) != 0)
        Fail("T-1080 fold 5: a destroy that did not keep the record entered the squad as failed");
    const std::string st = T313ReadFile("../coop-plugin/store.cpp");
    if (st.empty()) { Fail("T-1080 fold 5: could not read store.cpp"); return; }
    if (T1080BodyHas(st, "void detour_destroyPlatoon(void* faction, void* platoon)", "sqbirth::MadeEmptyPlaceFailed(del, modMade)") == 0
        || T1080BodyHas(st, "void DrainGoneEntry(", "sqbirth::MadeEmptyPlaceFailed(offDel, offMade)") == 0)
        Fail("T-1080 fold 5: a destroy does not enter a made-empty squad as failed");
    if (T1080BodyHas(st, "void MadeEmptyNotPlacedAgain(const std::string& worldId)", "g_notePlaceFailed.insert(worldId)") == 0) Fail("T-1080 fold 5: the made-empty entry is not the failed-creation registry");
    if (T1080BodyHas(st, "void PlaceUnplacedNotes()", "NotePlaceable(") == 0 || T1080BodyHas(st, "int NotePlaceable(", "g_notePlaceFailed.count(worldId)") == 0)
        Fail("T-1080 fold 5: the placement no longer asks the failed-creation registry");
}
/* T-1084 step 1: a sleeping copy is built from a record only by the off-screen authority, a game whose configured role is
   single-player, or the game that holds the squad's area; the kept-records sweep and the travel-in road; the wiring */
void t_t1084_copy_gate()
{
    using namespace coopsquad;
    if (CopyBuildGate(0, 0, 0, 1) != kCopyNotOther) Fail("T-1084: a game that is not the authority built a copy in an area another game or nobody holds");
    if (CopyBuildGate(0, 0, -2, -2) != kCopyNotOther) Fail("T-1084: a copy was built in a frozen area (its holder not in the world)");
    if (CopyBuildGate(0, 0, -1, -1) != kCopyNotNoMap) Fail("T-1084: a copy was built with no fresh area map");
    if (CopyBuildGate(1, 0, 0, 0) != kCopyBuild) Fail("T-1084: the off-screen authority did not build its copy for travel where no game holds the area");
    /* T-1084 step 3: the authority's arm - only where no game holds the area (the T1571 loop) */
    if (CopyBuildGate(1, 0, 0, 1) != kCopyNotOther || CopyBuildGate(1, 0, 0, -2) != kCopyNotOther) Fail("T-1084 step 3: the authority built a copy in an area another game holds, or a frozen one");
    if (CopyBuildGate(1, 0, -1, -1) != kCopyNotNoMap) Fail("T-1084 step 3: the authority built a copy with no fresh area map");
    if (CopyBuildGate(1, 0, 1, 0) != kCopyBuild || CopyBuildGate(1, 1, 0, 1) != kCopyBuild) Fail("T-1084 step 3: the authority that holds the area, or a single-player game, did not build");
    if (CopyBuildGate(0, 1, 0, 1) != kCopyBuild) Fail("T-1084: a game configured single-player did not build its copy");
    if (CopyBuildGate(0, 0, 1, 0) != kCopyBuild) Fail("T-1084: the game that holds the area did not take today's road");
    /* the sweep's pick, branch by branch: usable, hasCopy, failedBefore, gate, loadedHere, newlyHeldHere, onlyNewlyHeld */
    if (SweepPick(1, 0, 0, CopyBuildGate(1, 0, 0, 0), 0, 0, 0) != kSwBuild) Fail("T-1084 sweep: the new authority did not build a kept record");
    if (SweepPick(1, 0, 0, CopyBuildGate(0, 0, 1, 0), 0, 0, 0) != kSwBuild) Fail("T-1084 sweep: a kept record in an area this game holds was not built");
    if (SweepPick(1, 0, 0, CopyBuildGate(0, 0, 0, 1), 0, 0, 0) != kSwNoCopy) Fail("T-1084 sweep: a kept record in an area this game does not hold was built");
    if (SweepPick(1, 0, 0, CopyBuildGate(0, 0, -1, -1), 0, 0, 0) != kSwNoCopy) Fail("T-1084 sweep: a kept record was built with no fresh map");
    if (SweepPick(1, 0, 0, CopyBuildGate(0, 0, -2, -2), 0, 0, 0) != kSwNoCopy) Fail("T-1084 sweep: a kept record was built in a frozen area");
    if (SweepPick(0, 0, 0, kCopyBuild, 0, 1, 1) != kSwGone) Fail("T-1084 sweep: a gone / deleted / kept-gated record was built");
    if (SweepPick(1, 1, 0, kCopyBuild, 0, 1, 1) != kSwHasCopy) Fail("T-1084 sweep: a record with a copy here was built again");
    if (SweepPick(1, 0, 1, kCopyBuild, 0, 1, 1) != kSwFailedBefore) Fail("T-1084 sweep: a record whose build failed in this world was tried again");
    if (SweepPick(1, 0, 0, CopyBuildGate(1, 0, 0, 0), 1, 0, 0) != kSwDeferLoaded) Fail("T-1084 sweep: a record in an area loaded here was built beside the holder's group");
    /* the newly-held trigger's selection */
    if (SweepPick(1, 0, 0, CopyBuildGate(0, 0, 1, 0), 0, 1, 1) != kSwBuild) Fail("T-1084 sweep: a record inside a newly held area was not built");
    if (SweepPick(1, 0, 0, CopyBuildGate(0, 0, 1, 0), 1, 1, 1) != kSwBuild) Fail("T-1084 sweep: a record inside a newly held area loaded here was deferred (this game is the holder)");
    if (SweepPick(1, 0, 0, CopyBuildGate(0, 0, 1, 0), 0, 0, 1) != kSwOutside) Fail("T-1084 sweep: a record outside the newly held areas was taken by that trigger");
    if (SweepPick(1, 0, 0, CopyBuildGate(0, 0, 0, 1), 0, 1, 1) != kSwNoCopy) Fail("T-1084 sweep: a newly held area that is no longer held was built in");
    if (NewlyHeld(0, 1) != 1 || NewlyHeld(1, 1) != 0 || NewlyHeld(0, 0) != 0 || NewlyHeld(1, 0) != 0 || NewlyHeld(0, -2) != 0) Fail("T-1084: the newly-held edge");
    /* travel in: usable, hasCopy, failedBefore, mineHeld */
    if (TravelInBuild(1, 0, 0, 1) != 1) Fail("T-1084 travel-in: a squad moved into an area this game holds was not built");
    if (TravelInBuild(1, 0, 0, 0) != 0 || TravelInBuild(1, 0, 0, -1) != 0 || TravelInBuild(1, 0, 0, -2) != 0) Fail("T-1084 travel-in: built in an area this game does not hold");
    if (TravelInBuild(0, 0, 0, 1) != 0 || TravelInBuild(1, 1, 0, 1) != 0 || TravelInBuild(1, 0, 1, 1) != 0) Fail("T-1084 travel-in: built from a gone record, beside a copy, or after a failed build");
    /* fold 2: the queue's area filter */
    if (SweepQueueTakes(0, 0, 0) != 1 || SweepQueueTakes(0, 1, 1) != 1) Fail("T-1084 sweep queue: a record the trigger names was not queued");
    if (SweepQueueTakes(0, 1, 0) != 0) Fail("T-1084 sweep queue: a record outside the newly held areas was queued by that trigger");
    if (SweepQueueTakes(1, 0, 0) != 0 || SweepQueueTakes(1, 1, 1) != 0) Fail("T-1084 sweep queue: a zone record was queued");
    /* the per-entry why and area */
    if (SweepInNewlyHeld(kSweepWhyNewlyHeld, 77, 77) != 1 || SweepInNewlyHeld(kSweepWhyNewlyHeld | kSweepWhyAuthority, 77, 77) != 1) Fail("T-1084 sweep: a record still in the area it was queued for was not taken as newly held");
    if (SweepInNewlyHeld(kSweepWhyNewlyHeld, 77, 78) != 0) Fail("T-1084 sweep: a record that left the area it was queued for was taken as newly held");
    if (SweepInNewlyHeld(kSweepWhyAuthority, 77, 77) != 0 || SweepInNewlyHeld(kSweepWhyNewlyHeld, -1, -1) != 0) Fail("T-1084 sweep: an entry not queued as newly held was taken as newly held");
    if (SweepOnlyNewlyHeld(kSweepWhyNewlyHeld) != 1 || SweepOnlyNewlyHeld(kSweepWhyNewlyHeld | kSweepWhyMapFresh) != 0 || SweepOnlyNewlyHeld(kSweepWhyAuthority) != 0) Fail("T-1084 sweep: the only-newly-held reason");
    /* the change detection, area by area */
    {
        int n = 9;
        if (SweepAreaStep(0, 1, &n) != 1 || n != 1) Fail("T-1084 area step: a newly held area was not seen");
        n = 9; if (SweepAreaStep(1, 1, &n) != 1 || n != 0) Fail("T-1084 area step: an area held before was seen as newly held");
        n = 9; if (SweepAreaStep(1, -1, &n) != 1 || n != 0) Fail("T-1084 area step: no fresh map changed the stored answer");
        n = 9; if (SweepAreaStep(0, -1, &n) != 0 || n != 0) Fail("T-1084 area step: no fresh map made an area held");
        n = 9; if (SweepAreaStep(1, 0, &n) != 0 || n != 0) Fail("T-1084 area step: an area given up was kept as held");
        n = 9; if (SweepAreaStep(1, -2, &n) != 0 || n != 0) Fail("T-1084 area step: a frozen area was kept as held");
        n = 9; if (SweepAreaStep(SweepAreaStep(1, -1, &n), 1, &n) != 1 || n != 0) Fail("T-1084 area step: a map gap then the same holder was seen as newly held");
    }
    /* the map sequence */
    if (SweepMapApplied(5u, 5u) != 0 || SweepMapApplied(5u, 6u) != 1 || SweepMapApplied(0xFFFFFFFFu, 0u) != 1) Fail("T-1084 sweep: an applied area map");
    if (SweepMapFreshFires(1, 1) != 1 || SweepMapFreshFires(1, 0) != 0 || SweepMapFreshFires(0, 1) != 0) Fail("T-1084 sweep: trigger (b)");
    /* trigger (a): the turn to authority, on known answers only */
    {
        int last = 0;
        if (SweepAuthorityTurned(0, 0, &last) != 0 || last != 0) Fail("T-1084 authority: an unknown answer fired or was stored");
        if (SweepAuthorityTurned(1, 1, &last) != 1 || last != 1) Fail("T-1084 authority: the turn to authority did not fire");
        if (SweepAuthorityTurned(0, 0, &last) != 0 || last != 1) Fail("T-1084 authority: a link drop changed the last known answer");
        if (SweepAuthorityTurned(1, 1, &last) != 0) Fail("T-1084 authority: a relink fired as a new authority");
        if (SweepAuthorityTurned(1, 0, &last) != 0 || last != 0) Fail("T-1084 authority: losing the authority fired or was not stored");
        if (SweepAuthorityTurned(1, 1, &last) != 1) Fail("T-1084 authority: a later turn to authority did not fire");
    }
    /* an old world's leftover roaming squad */
    if (SweepRetireRoad(1, 1) != kSwRoadRetire) Fail("T-1084 sweep: a leftover in an area this game holds was not retired");
    if (SweepRetireRoad(0, 1) != kSwRoadLeftoverSkip) Fail("T-1084 sweep: a leftover in an area this game does not hold was retired or built");
    if (SweepRetireRoad(1, 0) != kSwRoadBuild || SweepRetireRoad(0, 0) != kSwRoadBuild) Fail("T-1084 sweep: a squad that is no leftover was not built");
    const std::string st = T313ReadFile("../coop-plugin/store.cpp");
    if (st.empty()) { Fail("T-1084: could not read store.cpp"); return; }
    if (T1080BodyHas(st, "int CopyGateNow(float x, float z)", "OffscreenAuthority()") == 0 || T1080BodyHas(st, "int CopyGateNow(float x, float z)", "MineHeldTS(SectorOf(x, z))") == 0
        || T1080BodyHas(st, "int CopyGateNow(float x, float z)", "RoleIsSingle()") == 0)
        Fail("T-1084: the gate does not read the exact states (the named authority, the configured role, the area map's holder)");
    {
        const size_t s = T1080DefAt(st, "void ApplyRemoteRecord("), e = T1080BodyEnd(st, s);
        const size_t g = s == std::string::npos ? std::string::npos : st.find("CopyGateCount(CopyGateRecord(worldId, r), kCopyRoadRecv)", s);
        const size_t c = s == std::string::npos ? std::string::npos : st.find("const int recvMade = CreateUnknownSquad(r, \"RECV\");", s);
        if (e == std::string::npos || g > e || c > e || g > c) Fail("T-1084: a record arriving for a squad unknown here is not gated before its copy is built");
    }
    if (T1080BodyHas(st, "int StoreRecreateSleepingFromRecord(const std::string& worldId, int runnerRestart)", "kCopyRoadRunnerRestart : kCopyRoadPeerDrop") == 0) Fail("T-1084: the re-create roads are not gated and counted apart");
    if (T1080BodyHas(st, "void PlaceUnplacedNotes()", "kCopyRoadPlace") == 0 || T1080BodyHas(st, "void PlaceUnplacedNotes()", "if (CopyGateRecord(due[i], r) != coopsquad::kCopyBuild) continue;") == 0)
        Fail("T-1084: the town-note placing is not gated at its due list and its creation");
    if (T1080BodyHas(st, "void ApplyRemoteRecord(", "coopsquad::TravelInBuild(") == 0 || T1080BodyHas(st, "void ApplyRemoteRecord(", "RetireOldRoaming(tr)") == 0) Fail("T-1084: the travel-in road does not ask its rule and the retire rule");
    if (T1080BodyHas(st, "void KeptRecordSweepTick()", "coopsquad::SweepPick(") == 0 || T1080BodyHas(st, "void KeptRecordSweepTick()", "coopsquad::SweepAreaStep(") == 0
        || T1080BodyHas(st, "void KeptRecordSweepTick()", "coopsquad::SweepAuthorityTurned(") == 0 || T1080BodyHas(st, "void KeptRecordSweepTick()", "coopsquad::SweepRetireRoad(held, OldRoamingLeftover(r))") == 0
        || T1080BodyHas(st, "void KeptRecordSweepTick()", "!g_noteRaiseDone") == 0 || T1080BodyHas(st, "void KeptRecordSweepTick()", "!g_sweepFirst.empty() ? g_sweepFirst : g_sweepPending") == 0
        || T1080BodyHas(st, "void KeptSweepQueue(int why, const std::set<int>* sectors)", "coopsquad::SweepQueueTakes(") == 0
        || T1080BodyHas(st, "void KeptRecordSweepTick()", "CreateUnknownSquad(r, \"SWEEP\")") == 0)
        Fail("T-1084: the kept-records sweep does not ask its rules on its three events");
    if (T1080BodyHas(st, "int CopyGateCount(int gate, int road)", "g_sweepNoMapPending = true") == 0) Fail("T-1084: a no-map refusal does not arm the sweep");
    /* T-1085 (owner 1131 a): a squad the engine does not keep - its copy is built only where its area is loaded here */
    {
        const int kNo = sqbirth::MadeKeptByte(sqbirth::kBirthDiscard), kKept = sqbirth::MadeKeptByte(sqbirth::kBirthKeep), kUnk = sqbirth::MadeKeptByte(sqbirth::kBirthUnknown);
        if (kNo != 0 || kKept != 1 || kUnk != 1) Fail("T-1085: the kept answer the gate reads is not the copy's kept byte");
        const int gates[6] = { CopyBuildGate(1, 0, 0, 0), CopyBuildGate(0, 0, 1, 0), CopyBuildGate(0, 1, 0, 1), CopyBuildGate(0, 0, 0, 1), CopyBuildGate(0, 0, -1, -1), CopyBuildGate(1, 0, 0, 1) };
        for (int i = 0; i < 6; ++i)
            for (int loaded = 0; loaded <= 1; ++loaded)
                if (CopyKeptGate(gates[i], kKept, loaded) != gates[i] || CopyKeptGate(gates[i], kUnk, loaded) != gates[i])
                    Fail("T-1085: a kept squad (or one whose record carries no flag) is not gated as before");
        if (CopyKeptGate(CopyBuildGate(1, 0, 0, 0), kNo, 0) != kCopyNotKeptUnloaded) Fail("T-1085: the authority built a not-kept squad's copy in an area not loaded here");
        if (CopyKeptGate(CopyBuildGate(1, 0, 0, 0), kNo, 1) != kCopyBuild) Fail("T-1085: the authority did not build a not-kept squad's copy in an area loaded here");
        if (CopyKeptGate(CopyBuildGate(0, 0, 1, 0), kNo, 0) != kCopyNotKeptUnloaded) Fail("T-1085: the holder built a not-kept squad's copy in an area not loaded here");
        if (CopyKeptGate(CopyBuildGate(0, 0, 1, 0), kNo, 1) != kCopyBuild) Fail("T-1085: the holder did not build a not-kept squad's copy in an area loaded here");
        if (CopyKeptGate(CopyBuildGate(0, 1, 0, 1), kNo, 0) != kCopyNotKeptUnloaded || CopyKeptGate(CopyBuildGate(0, 1, 0, 1), kNo, 1) != kCopyBuild) Fail("T-1085: a single-player game's not-kept copy is not gated on its loaded area");
        if (CopyKeptGate(CopyBuildGate(0, 0, -1, -1), kNo, 0) != kCopyNotNoMap || CopyKeptGate(CopyBuildGate(0, 0, -1, -1), kNo, 1) != kCopyNotNoMap
            || CopyKeptGate(CopyBuildGate(1, 0, -1, -1), kNo, 1) != kCopyNotNoMap) Fail("T-1085: the no-map answer was not kept for a not-kept squad");
        if (CopyKeptGate(CopyBuildGate(0, 0, 0, 1), kNo, 1) != kCopyNotOther || CopyKeptGate(CopyBuildGate(1, 0, 0, 1), kNo, 1) != kCopyNotOther) Fail("T-1085: a not-kept squad's copy was built in a loaded area another game holds");
        if (CopyKeptGate(kCopyBuild, kNo, 0) != kCopyNotKeptUnloaded || CopyKeptGate(kCopyBuild, kNo, 1) != kCopyBuild) Fail("T-1085: the travel-in road's rule");
        /* the engine's loaded byte could not be read (-1): not loaded - refused and waits; a kept squad is unchanged */
        if (CopyKeptGate(kCopyBuild, kNo, -1) != kCopyNotKeptUnloaded || CopyKeptGate(CopyBuildGate(1, 0, 0, 0), kNo, -1) != kCopyNotKeptUnloaded
            || CopyKeptGate(kCopyBuild, kKept, -1) != kCopyBuild) Fail("T-1085: an unreadable loaded byte was taken as loaded, or changed a kept squad");
        /* the area-load re-ask: queued with its area, built even though the area is loaded here */
        if (SweepInNewlyHeld(kSweepWhyLoaded, 77, 77) != 1 || SweepInNewlyHeld(kSweepWhyLoaded | kSweepWhyAuthority, 77, 77) != 1 || SweepInNewlyHeld(kSweepWhyLoaded, 77, 78) != 0)
            Fail("T-1085 sweep: a record queued at its area's load is not judged by its area");
        if (SweepOnlyNewlyHeld(kSweepWhyLoaded) != 1 || SweepOnlyNewlyHeld(kSweepWhyLoaded | kSweepWhyNewlyHeld) != 1 || SweepOnlyNewlyHeld(kSweepWhyLoaded | kSweepWhyMapFresh) != 0 || SweepOnlyNewlyHeld(0) != 0)
            Fail("T-1085 sweep: the only-area-bound reason");
        if (SweepPick(1, 0, 0, CopyKeptGate(CopyBuildGate(1, 0, 0, 0), kNo, 1), 1, SweepInNewlyHeld(kSweepWhyLoaded, 77, 77), SweepOnlyNewlyHeld(kSweepWhyLoaded)) != kSwBuild)
            Fail("T-1085 sweep: a not-kept record whose area loaded here was not built");
        if (SweepPick(1, 0, 0, CopyKeptGate(CopyBuildGate(1, 0, 0, 0), kNo, 0), 0, 0, 0) != kSwNoCopy) Fail("T-1085 sweep: a not-kept record was built in an area not loaded here");
        if (SweepPick(1, 0, 0, CopyKeptGate(CopyBuildGate(1, 0, 0, 0), kKept, 0), 0, 0, 0) != kSwBuild) Fail("T-1085 sweep: a kept record is no longer built by the authority");
        /* the wiring: the same kept-byte call as the creation, the loaded grid, every road */
        if (T1080BodyHas(st, "static int CreateUnknownSquad(const Record& r, const char* road)", "SetPersistentPod(p, sqbirth::MadeKeptByte(r.birth) != 0)") == 0
            || T1080BodyHas(st, "static int CopyGateKept(int gate, const std::string& id, const Record& r)", "sqbirth::MadeKeptByte(r.birth)") == 0
            || T1080BodyHas(st, "static int CopyGateKept(int gate, const std::string& id, const Record& r)", "ZoneLoadedSectorTri(s.x, s.y)") == 0)
            Fail("T-1085: the gate does not read the kept answer the creation writes, or the loaded area");
        /* the loaded area is the engine's own byte (ZoneLoadedSectorTri), never the mod's once-a-second list, at the gate and the wait */
        if (T1080BodyHas(st, "static int CopyGateKept(int gate, const std::string& id, const Record& r)", "SectorLoadedHereTS") != 0
            || T1080BodyHas(st, "void KeptRecordSweepTick()", "const int zoneLoaded = ZoneLoadedSectorTri(s.x, s.y);") == 0
            || T1080BodyHas(st, "void KeptRecordSweepTick()", "SectorLoadedHereTS") != 0)
            Fail("T-1085: the not-kept gate or its wait reads the mod's loaded list, not the engine's loaded byte");
        if (T1080BodyHas(st, "void KeptRecordSweepTick()", "gate = CopyGateRecord(id, rt->second);") == 0 || T1080BodyHas(st, "void KeptRecordSweepTick()", "e.why |= kSweepLoaded;") == 0
            || T1080BodyHas(st, "void KeptRecordSweepTick()", "g_notKeptWait") == 0)
            Fail("T-1085: the sweep does not gate not-kept records or ask them again when their area loads");
        /* a waiting record whose squad has become kept leaves the wait unqueued (kSweepLoaded would skip the kept squads' loaded-area deferral) */
        {
            const std::string drop = "if (sqbirth::MadeKeptByte(rt->second.birth) != 0) { ++g_notKeptNowKept; g_notKeptWait.erase(it++); continue; }";
            const size_t b = T1080DefAt(st, "void KeptRecordSweepTick()");
            const size_t d = b == std::string::npos ? std::string::npos : st.find(drop, b);
            const size_t q = b == std::string::npos ? std::string::npos : st.find("e.why |= kSweepLoaded;", b);
            if (T1080BodyHas(st, "void KeptRecordSweepTick()", drop.c_str()) == 0 || d == std::string::npos || q == std::string::npos || d > q)
                Fail("T-1085: the wait's scan does not drop a now-kept record before queuing it at its area's load");
        }
        if (T1080BodyHas(st, "void PlaceUnplacedNotes()", "const int gate = CopyGateRecord(it->first, *rp);") == 0
            || T1080BodyHas(st, "int StoreRecreateSleepingFromRecord(const std::string& worldId, int runnerRestart)", "CopyGateRecord(worldId, it->second)") == 0
            || T1080BodyHas(st, "void ApplyRemoteRecord(", "CopyGateKept(coopsquad::kCopyBuild, worldId, tr)") == 0)
            Fail("T-1085: a road that builds a copy from its record does not ask the not-kept rule");
        /* a squad made from its record is marked read when it is made: its first wake builds the record's people, not the template's */
        if (T1080BodyHas(st, "static int CreateUnknownSquad(const Record& r, const char* road)", "MarkReadPod(cq.unloaded) == 1") == 0)
            Fail("T-1085: a squad made from its record is not marked read when it is made");
    }
}
/* T-999: the world server's judgement of a discard delete; the retire rule */
void t_t999_server_and_retire()
{
    using namespace sqbirth;
    if (ServerDiscardJudge(0, "B", "A", 2) != kSrvApply) Fail("T-999: a plain delete was judged by the area's holder");
    if (ServerDiscardJudge(1, "B", "A", 0) != kSrvRefuseOtherHolds) Fail("T-999: a discard delete was taken while another game held the area");
    if (ServerDiscardJudge(1, "A", "A", 0) != kSrvApply || ServerDiscardJudge(1, "", "A", 0) != kSrvApply) Fail("T-999: a discard delete from the holder, or with no holder, was refused");
    if (ServerDiscardJudge(1, "A", "A", 1) != kSrvRefuseOtherLoaded || ServerDiscardJudge(1, "", "A", 2) != kSrvRefuseOtherLoaded) Fail("T-999: a discard delete was taken while another game had the area loaded");
    if (RetireJudge(kBirthUnknown, 1, 0, 1, 0) != 1) Fail("T-999: an old roaming record with nothing to keep it was not retired");
    if (RetireJudge(kBirthDiscard, 1, 0, 1, 0) != 0 || RetireJudge(kBirthKeep, 1, 0, 1, 0) != 0) Fail("T-999: a record with a birth flag was retired");
    if (RetireJudge(kBirthUnknown, 0, 0, 1, 0) != 0) Fail("T-999: a record with a home key was retired");
    if (RetireJudge(kBirthUnknown, 1, 1, 1, 0) != 0) Fail("T-999: a player's record was retired");
    if (RetireJudge(kBirthUnknown, 1, 0, 0, 0) != 0 || RetireJudge(kBirthUnknown, 1, 0, -1, 0) != 0) Fail("T-999: a resident or unread template was retired");
    if (RetireJudge(kBirthUnknown, 1, 0, 1, 1) != 0 || RetireJudge(kBirthUnknown, 1, 0, 1, -1) != 0) Fail("T-999: a record naming a player, or unread, was retired");
    const char b[] = "xxstand-p3yy";
    if (BytesHold(b, sizeof(b) - 1, "stand-p") != 1 || BytesHold(b, sizeof(b) - 1, "stand-q") != 0 || BytesHold(b, sizeof(b) - 1, "") != 0 || BytesHold(b, 3, "stand-p") != 0) Fail("T-999: the byte search");
}
/* T-999: the flag on the wire (RECORD flags, CONTEXT word, RECORD_GONE's discard mark) and on the index line */
void t_t999_birth_wire()
{
    using namespace sqbirth;
    for (int bth = -1; bth <= 1; ++bth)
    {
        if (BirthOfFlags(FlagsOfBirth(bth)) != bth || BirthOfFlags(FlagsOfBirth(bth) | 1u) != bth) Fail("T-999: the RECORD flags byte does not round-trip the birth flag");
        if (BirthOfWire(WireOfBirth(bth)) != bth || BirthOfField(BirthField(bth)) != bth) Fail("T-999: CONTEXT's word or the line's field does not round-trip the birth flag");
    }
    if (FlagsMerge(FlagsOfBirth(kBirthDiscard), 1u) != (1u | FlagsOfBirth(kBirthDiscard))) Fail("T-999: a write that does not know the birth flag lost the one held");
    if (FlagsMerge(FlagsOfBirth(kBirthDiscard) | 1u, FlagsOfBirth(kBirthKeep)) != FlagsOfBirth(kBirthKeep)) Fail("T-999: a write that knows the birth flag did not replace it, or kept a bit it did not send");
    if (BirthMerge(kBirthKeep, kBirthUnknown) != kBirthKeep || BirthMerge(kBirthKeep, kBirthDiscard) != kBirthDiscard) Fail("T-999: the record's birth merge");
    std::vector<char> g; townrefill::AppendGoneDiscard(&g);
    std::string town = "x";
    if (g.size() != 4 || townrefill::GoneIsDiscard(&g[0], g.size(), 0) != 1 || townrefill::ReadGoneTown(&g[0], g.size(), 0, &town) != 0 || !town.empty()) Fail("T-999: a discard gone is not read as a discard naming no town");
    std::vector<char> z(4, 0);
    if (townrefill::GoneIsDiscard(&z[0], z.size(), 0) != 0 || townrefill::GoneIsDiscard(&g[0], g.size(), 4) != 0) Fail("T-999: a gone without the mark read as a discard");
    coopstore::MetaLine m; m.version = 7; m.writtenAt = 5; m.owner = 1; m.x = 1.0f; m.y = 2.0f; m.z = 3.0f; m.squadSid = "sq"; m.factionName = "fac"; m.worldId = "Fac_1"; m.town = "town"; m.len = 0; m.seq = 9;
    std::string line; coopstore::MetaLine b;
    if (!coopstore::MetaEncodeV7(m, &line) || !coopstore::MetaParse(line, &b) || b.birth != -1 || !b.home.empty()) Fail("T-999: a line without the birth field does not read as unknown");
    if (line.find("\t\n") != std::string::npos) Fail("T-999: a line without the birth field gained an empty field");
    m.birth = 0; b = coopstore::MetaLine();
    if (!coopstore::MetaEncodeV7(m, &line) || !coopstore::MetaParse(line, &b) || b.birth != 0 || !b.home.empty()) Fail("T-999: a homeless line with birth 'not kept' does not round-trip");
    m.birth = 1; m.home = "hk"; b = coopstore::MetaLine();
    if (!coopstore::MetaEncodeV7(m, &line) || !coopstore::MetaParse(line, &b) || b.birth != 1 || b.home != "hk") Fail("T-999: a line with a home and birth 'kept' does not round-trip");
    b = coopstore::MetaLine();
    if (!coopstore::MetaParse("v7\t1\t0\t0\t0\t0\tsid\tfac\tFac_1\t0\ttown\t0\t00000000\t3\thome\n", &b) || b.birth != -1 || b.home != "home") Fail("T-999: an older v7 line with a home does not read as unknown birth");
    b = coopstore::MetaLine();
    if (!coopstore::MetaParse("v7\t1\t0\t0\t0\t0\tsid\tfac\tFac_1\t0\ttown\t0\t00000000\t3\t\tq\n", &b) || b.birth != -1) Fail("T-999: an unknown birth text did not read as unknown");
}
/* A shop record: its id from the building's key (no height, the name hashed, prefix shop.), its bytes whole or refused, the world
   server's rule (the area's holder only, named as the writer, a rising generation, the same bytes a repeat) and keeping by generation. */
void t_t772_shop_record()
{
    using namespace shoprec;
    const std::string key = "Hub bar@3,4@1234,-567,89";
    const std::string id = MakeShopRecId(key);
    if (id.empty() || id.compare(0, 5, "shop.") != 0) Fail("t772: no shop id from a position key");
    if (id != MakeShopRecId("Hub bar@3,4@1234,-567,95")) Fail("t772: the height changed the shop id");
    if (id.substr(5) != coopbrec::MakeBoxRecId(key).substr(4)) Fail("t772: the shop id is not the box id rule with the shop prefix");
    int sx = -1, sy = -1;
    if (ShopRecIdParse(id, &sx, &sy) != 1 || sx != 3 || sy != 4) Fail("t772: the shop id does not parse to its area square");
    if (!IsShopRecId(id) || !IsShopRecId("SHOP.x") || IsShopRecId("box.3.4.#00000000.1.2") || coopbrec::IsBoxRecId(id)) Fail("t772: the id tests mix shop and box ids");
    if (!MakeShopRecId("not a key").empty()) Fail("t772: a text that is not a position key made a shop id");
    ShopRec r;
    r.generation = 7; r.writerSlot = 2; r.key = key; r.deadline = 62.75; r.refills = 3; r.repairMark = kSrNoRepair;
    ShopSquad a; a.name = "barman"; a.money = 5000; a.moneyChanges = 4; a.refillsApplied = 3;
    ShopSquad b; b.name = "bar guards"; b.money = 0; b.moneyChanges = 1; b.refillsApplied = 2;
    r.squads.push_back(a); r.squads.push_back(b);
    std::vector<char> bytes;
    if (!EncodeShopRec(&bytes, r)) Fail("t772: a valid shop record did not encode");
    ShopRec back;
    if (DecodeShopRec(&bytes[0], bytes.size(), &back) != kSrOk || back.generation != 7 || back.writerSlot != 2 || back.key != key
        || back.deadline != 62.75 || back.refills != 3 || back.repairMark != kSrNoRepair || back.squads.size() != 2
        || back.squads[0].name != "barman" || back.squads[0].money != 5000 || back.squads[0].moneyChanges != 4 || back.squads[1].refillsApplied != 2)
        Fail("t772: the shop record did not round-trip");
    std::vector<char> cut(bytes.begin(), bytes.end() - 1);
    if (DecodeShopRec(&cut[0], cut.size(), 0) != kSrShort) Fail("t772: a record cut short was not refused");
    std::vector<char> extra = bytes; extra.push_back(0);
    if (DecodeShopRec(&extra[0], extra.size(), 0) != kSrTrailing) Fail("t772: bytes after the last squad were not refused");
    std::vector<char> mark = bytes; mark[0] = 'X';
    if (DecodeShopRec(&mark[0], mark.size(), 0) != kSrBadMarker) Fail("t772: a wrong marker was not refused");
    ShopRec dup = r; dup.squads[1].name = "barman";
    if (EncodeShopRec(&bytes, dup) || !bytes.empty()) Fail("t772: a record naming one squad twice encoded");
    ShopRec neg = r; neg.squads[0].money = -1;
    if (EncodeShopRec(&bytes, neg)) Fail("t772: negative money encoded");
    ShopRec odd = r; odd.deadline = 0.0; odd.deadline = odd.deadline / odd.deadline;
    if (EncodeShopRec(&bytes, odd)) Fail("t772: a deadline that is not a number encoded");
    if (!EncodeShopRec(&bytes, r)) Fail("t772: the record did not encode again");
    const char* p = &bytes[0]; const size_t n = bytes.size();
    if (ShopJudgeWrite(id, p, n, 2, 2, 0, 0, 0, 0) != coopbrec::kBrAccept) Fail("t772: the holder's first record was not taken");
    if (ShopJudgeWrite(id, p, n, 1, 2, 0, 0, 0, 0) != coopbrec::kBrRefuseWriter) Fail("t772: a record naming another writer was taken");
    if (ShopJudgeWrite(id, p, n, 2, 1, 0, 0, 0, 0) != coopbrec::kBrRefuseNotWriter) Fail("t772: a write from a game that is not the holder was taken");
    if (ShopJudgeWrite(id, p, n, 2, -1, 0, 0, 0, 0) != coopbrec::kBrRefuseNotWriter) Fail("t772: a write with nobody holding the area was taken");
    if (ShopJudgeWrite(id, p, n, 2, 2, 1, 6, 0, 0) != coopbrec::kBrAccept) Fail("t772: a newer generation from the holder was refused");
    if (ShopJudgeWrite(id, p, n, 2, 2, 1, 8, 0, 0) != coopbrec::kBrRefuseOlder) Fail("t772: an older generation was taken");
    if (ShopJudgeWrite(id, p, n, 2, 2, 1, 7, 1, 0) != coopbrec::kBrRepeat) Fail("t772: the same bytes at the stored generation were not a repeat");
    if (ShopJudgeWrite(id, p, n, 2, 2, 1, 7, 0, 0) != coopbrec::kBrRefuseSameGen) Fail("t772: other bytes at the stored generation were taken");
    if (ShopJudgeWrite(MakeShopRecId("Other@3,4@1,1,1"), p, n, 2, 2, 0, 0, 0, 0) != coopbrec::kBrRefuseWrongBox) Fail("t772: a record under another shop's id was taken");
    if (ShopJudgeWrite("shop.x", p, n, 2, 2, 0, 0, 0, 0) != coopbrec::kBrRefuseBadId) Fail("t772: a bad id was taken");
    if (ShopIntakeKeep(0, 0, 1) != 1 || ShopIntakeKeep(1, 5, 6) != 1 || ShopIntakeKeep(1, 5, 5) != 0 || ShopIntakeKeep(1, 5, 4) != 0) Fail("t772: keeping by generation");
    if (ShopNextGen(1, 5, 0) != 6 || ShopNextGen(1, 5, 9) != 10 || ShopNextGen(0, 0, 0) != 1) Fail("t772: the next write's generation");
}
/* The restock gate: only the holder by the current map; not before the record is known; a timed or holder-started refill only when
   the building is due or the squad is behind; a forced refill only for a squad the record does not carry; held while any player is
   inside; the refused restock leaves the record's time (or a short step) on the engine. */
void t_t772_shop_gate()
{
    using namespace shoprec;
    if (ShopGateDecide(kRoadTimed, 0, 1, 1, 1, 1, 0) != kGateRefuseNotHolder) Fail("t772: a game that is not the holder refilled");
    if (ShopGateDecide(kRoadForced, 0, 1, 0, 0, 1, 0) != kGateRefuseNotHolder) Fail("t772: a forced refill ran on a game that is not the holder");
    if (ShopGateDecide(kRoadLever, 0, 1, 1, 1, 1, 0) != kGateRefuseNotHolder) Fail("t772: the lever ran on a game that is not the holder");
    if (ShopGateDecide(kRoadTimed, 1, 0, 0, 0, 1, 0) != kGateRefuseNotKnown) Fail("t772: the holder refilled before the record was known");
    if (ShopGateDecide(kRoadForced, 1, 0, 0, 0, 1, 0) != kGateRefuseNotKnown) Fail("t772: a forced refill ran before the record was known");
    if (ShopGateDecide(kRoadForced, 1, 1, 1, 1, 1, 0) != kGateRefuseHasRecord) Fail("t772: a forced refill ran over a record that carries the squad");
    if (ShopGateDecide(kRoadForced, 1, 1, 1, 0, 1, 0) != kGateRun) Fail("t772: a forced refill for a squad the record does not carry was refused");
    if (ShopGateDecide(kRoadForced, 1, 1, 0, 0, 1, 1) != kGateRun) Fail("t772: a forced refill with no record was refused");
    if (ShopGateDecide(kRoadTimed, 1, 1, 1, 1, 0, 0) != kGateRefuseNotDue) Fail("t772: a timed refill ran before the shop's time");
    if (ShopGateDecide(kRoadHolderStarted, 1, 1, 1, 1, 0, 0) != kGateRefuseNotDue) Fail("t772: a holder-started refill ran before the shop's time");
    if (ShopGateDecide(kRoadTimed, 1, 1, 1, 1, 1, 0) != kGateRun) Fail("t772: a due refill on the holder did not run");
    if (ShopGateDecide(kRoadTimed, 1, 1, 0, 0, 1, 0) != kGateRun) Fail("t772: the engine's own time with no record did not run");
    if (ShopGateDecide(kRoadTimed, 1, 1, 1, 1, 1, 1) != kGateHeldPlayerInside) Fail("t772: a due refill ran while a player was inside");
    if (ShopGateDecide(kRoadHolderStarted, 1, 1, 1, 1, 1, 1) != kGateHeldPlayerInside) Fail("t772: a holder-started refill ran while a player was inside");
    if (ShopGateDecide(kRoadRepair, 1, 1, 1, 1, 1, 0) != kGateRun) Fail("t772: the repair's refill did not run when due");
    if (ShopGateDecide(kRoadLever, 1, 0, 1, 1, 0, 1) != kGateRun) Fail("t772: the lever did not run on the holder");
    ShopRec r; r.deadline = 100.0; r.refills = 4;
    ShopSquad a; a.name = "a"; a.refillsApplied = 4; r.squads.push_back(a);
    ShopSquad b; b.name = "b"; b.refillsApplied = 3; r.squads.push_back(b);
    if (ShopDue(r, 0, 99.0) != 0) Fail("t772: a squad that answered the building's last refill was due before the time");
    if (ShopDue(r, 0, 100.0) != 1) Fail("t772: the building's time passing did not make it due");
    if (ShopDue(r, 1, 50.0) != 1) Fail("t772: a squad behind the building's refill was not due");
    if (ShopDue(r, -1, 50.0) != 1) Fail("t772: a squad the record does not carry was not due");
    if (ShopEngineTimer(1, 100.0, 50.0) != 100.0) Fail("t772: a refused restock did not leave the record's time on the engine");
    if (ShopEngineTimer(1, 40.0, 50.0) != 50.0 + kSrRetryHours || ShopEngineTimer(0, 0.0, 50.0) != 50.0 + kSrRetryHours) Fail("t772: a past or missing time did not leave a short step");
}
/* The owed forced fill: a forced call refused before this game holds the area or knows the record is parked; a timed call refused
   is not; a forced fill that can never run is unparked; a run unparks only when the engine moved its deadline. */
void t_t1098_shop_park()
{
    using namespace shoprec;
    if (ShopParkDecide(kGateRefuseNotHolder, kRoadForced, 0, 0) != kParkPark) Fail("t1098: a forced fill refused before this game held the area was not parked");
    if (ShopParkDecide(kGateRefuseNotKnown, kRoadForced, 0, 0) != kParkPark) Fail("t1098: a forced fill refused before the record was known was not parked");
    if (ShopParkDecide(kGateRefuseNotHolder, kRoadTimed, 0, 0) != kParkLeave) Fail("t1098: a timed call refused on a game that is not the holder changed the park");
    if (ShopParkDecide(kGateRefuseNotHolder, kRoadHolderStarted, 0, 0) != kParkLeave || ShopParkDecide(kGateRefuseNotHolder, kRoadLever, 0, 0) != kParkLeave) Fail("t1098: a call that was not forced was parked");
    if (ShopParkDecide(kGateRefuseHasRecord, kRoadForced, 0, 0) != kParkUnpark) Fail("t1098: a forced fill of a squad the record carries stayed parked");
    if (ShopParkDecide(kGateRefuseFull, kRoadForced, 0, 0) != kParkUnpark || ShopParkDecide(kGateRefuseFull, kRoadTimed, 0, 0) != kParkUnpark) Fail("t1098: a squad the full record has no room for stayed parked");
    if (ShopParkDecide(kGateRefuseNotDue, kRoadTimed, 0, 0) != kParkLeave || ShopParkDecide(kGateHeldPlayerInside, kRoadTimed, 0, 0) != kParkLeave) Fail("t1098: a not-due or held call changed the park");
    if (ShopParkDecide(kGateRun, kRoadForced, 1, 1) != kParkUnpark || ShopParkDecide(kGateRun, kRoadTimed, 1, 1) != kParkUnpark) Fail("t1098: a run that moved the deadline stayed parked");
    if (ShopParkDecide(kGateRun, kRoadForced, 1, 0) != kParkLeave) Fail("t1098: a forced run whose engine refilled nothing was unparked");
}
/* After a refill on the holder: written only when the engine moved the squad's deadline; the building refills once and its time
   becomes the engine's; a second squad answering the same refill only records it; money with a change count, applied by the runner
   only when the count moved; the repair makes the shop due once per refill. */
void t_t772_shop_after_run()
{
    using namespace shoprec;
    ShopRec r; r.key = "Hub bar@3,4@1234,-567,89"; r.writerSlot = 1;
    if (ShopAfterRun(&r, 0, "a", 0, 124.0, 100.0, 1, 900) != 0 || r.generation != 0) Fail("t772: a restock that refilled nothing was written");
    if (ShopAfterRun(&r, 0, "a", 1, 124.0, 100.0, 1, 900) != 1) Fail("t772: the first refill was not written");
    if (r.refills != 1 || r.deadline != 124.0 || r.generation != 1 || r.squads.size() != 1 || r.squads[0].money != 900
        || r.squads[0].moneyChanges != 1 || r.squads[0].refillsApplied != 1) Fail("t772: the first refill's record");
    if (ShopAfterRun(&r, 1, "b", 1, 125.0, 101.0, 1, 300) != 1 || r.refills != 1 || r.deadline != 124.0 || r.squads.size() != 2
        || r.squads[1].refillsApplied != 1 || r.generation != 2) Fail("t772: a second squad of the building moved the building's refill");
    if (ShopDue(r, 1, 101.0) != 0 || ShopDue(r, 0, 101.0) != 0) Fail("t772: a squad was due again after the building's refill");
    if (ShopAfterRun(&r, 1, "a", 1, 150.0, 126.0, 1, 1000) != 1 || r.refills != 2 || r.deadline != 150.0 || r.squads[0].moneyChanges != 2)
        Fail("t772: the building's next refill");
    if (ShopDue(r, 1, 126.5) != 1) Fail("t772: the other squad was not due after the building refilled");
    if (ShopAfterRun(&r, 1, "a", 1, 151.0, 127.0, 0, 0) != 1 || r.squads[0].money != 1000 || r.squads[0].moneyChanges != 2)
        Fail("t772: an unread money changed the record's money");
    if (ShopMoneyApplyDue(0, 0, 5) != 0) Fail("t772: the first look took the record's money");
    if (ShopMoneyApplyDue(1, 5, 5) != 0) Fail("t772: a resend of the same change count changed the money again");
    if (ShopMoneyApplyDue(1, 4, 5) != 1) Fail("t772: a new money change was not taken");
    const unsigned int g = r.generation;
    if (ShopMoneySet(&r, "a", 42) != 1 || r.squads[0].money != 42 || r.squads[0].moneyChanges != 3 || r.generation != g + 1) Fail("t772: the money set");
    if (ShopMoneySet(&r, "zz", 42) != 0 || ShopMoneySet(&r, "a", -5) != 0) Fail("t772: a money set on no squad or below 0 changed the record");
    if (ShopRepairAllowed(r) != 1) Fail("t772: a shop never repaired could not be repaired");
    if (ShopRepairApply(&r, 130.0) != 1 || r.deadline != 130.0 || r.repairMark != r.refills) Fail("t772: the repair did not make the shop due now");
    if (ShopRepairApply(&r, 131.0) != 0) Fail("t772: a second repair before any refill was asked");
    if (ShopAfterRun(&r, 1, "a", 1, 155.0, 131.0, 1, 0) != 1 || ShopRepairApply(&r, 132.0) != 0) Fail("t772: a repair right after the repair's own refill was asked (a loop)");
    if (ShopAfterRun(&r, 1, "a", 1, 180.0, 156.0, 1, 0) != 1 || ShopRepairApply(&r, 157.0) != 1) Fail("t772: a repair after a later refill was refused");
}
/* Trades through the shop record: a trade request is applied to the money once despite a resend (also from a new holder); two
   players' requests are both kept; a game that is not the holder shows the record's money plus its own trades in flight the record
   does not carry yet; a keeper the record does not carry keeps today's roads; the holder's pot becomes the record's money. */
void t_t772_shop_money()
{
    using namespace shoprec;
    ShopSquad s; s.name = "barman"; s.money = 1000;
    if (ShopReqSeen(s, 5, 77, 1) != 0) Fail("t772m: a first request was taken as applied");
    if (ShopReqNote(&s, 5, 77, 1) != 1 || ShopReqSeen(s, 5, 77, 1) != 1) Fail("t772m: an applied request was not remembered");
    if (ShopReqNote(&s, 5, 77, 1) != 0) Fail("t772m: a resent request changed the record");
    if (ShopReqSeen(s, 5, 77, 2) != 0) Fail("t772m: the asker's next request was taken as applied");
    if (ShopReqNote(&s, 5, 77, 2) != 1 || ShopReqSeen(s, 5, 77, 1) != 1 || ShopReqSeen(s, 5, 77, 2) != 1) Fail("t772m: an older request of the same epoch was not covered");
    if (ShopReqSeen(s, 5, 78, 1) != 0) Fail("t772m: a request of the asker's new process (epoch) was taken as applied");
    if (ShopReqNote(&s, 9, 300, 4) != 1 || ShopReqSeen(s, 9, 300, 4) != 1 || ShopReqSeen(s, 5, 77, 2) != 1 || s.reqs.size() != 2)
        Fail("t772m: two players' requests were not both kept");
    if (ShopReqSeen(s, 9, 77, 2) != 0) Fail("t772m: one asker's request was taken as another's");
    if (ShopReqNote(&s, 5, 78, 1) != 1 || ShopReqSeen(s, 5, 77, 2) != 0 || ShopReqSeen(s, 5, 78, 1) != 1 || s.reqs.size() != 2)
        Fail("t772m: an asker's new epoch did not replace its old one");
    ShopSquad full;
    for (unsigned int a = 0; a < kSrMaxReqs + 3; ++a) ShopReqNote(&full, a, 1, 1);
    if (full.reqs.size() != kSrMaxReqs || ShopReqSeen(full, kSrMaxReqs + 2, 1, 1) != 1 || ShopReqSeen(full, 0, 1, 1) != 0) Fail("t772m: a full asker list did not drop its oldest");
    /* a resend after a holder change: the new holder reads the record the old holder wrote */
    ShopRec r; r.key = "Hub bar@3,4@1234,-567,89"; r.writerSlot = 1; r.generation = 4; r.deadline = 30.0; r.refills = 1;
    ShopSquad b; b.name = "barman"; b.money = 1000; b.moneyChanges = 2; b.refillsApplied = 1;
    ShopReqNote(&b, 5, 77, 3);
    r.squads.push_back(b);
    std::vector<char> bytes;
    if (!EncodeShopRec(&bytes, r)) Fail("t772m: a record with an asker list did not encode");
    ShopRec back;
    if (DecodeShopRec(&bytes[0], bytes.size(), &back) != kSrOk || back.squads[0].reqs.size() != 1 || back.squads[0].reqs[0].asker != 5
        || back.squads[0].reqs[0].epoch != 77 || back.squads[0].reqs[0].done.size() != 1 || back.squads[0].reqs[0].done[0].id != 3)
        Fail("t772m: the asker list did not round-trip");
    if (ShopReqSeen(back.squads[0], 5, 77, 3) != 1) Fail("t772m: a resend to the new holder would be applied again");
    ShopRec twice = r; ShopReq dup; dup.asker = 5; dup.epoch = 1; twice.squads[0].reqs.push_back(dup);
    if (EncodeShopRec(&bytes, twice)) Fail("t772m: a record naming one asker twice encoded");
    /* the holder's capture: a pot that differs from the record is a new change; the same pot is no change */
    const unsigned int g = r.generation;
    if (ShopMoneyFromPot(&r, "barman", 1000) != 0 || r.generation != g) Fail("t772m: an unchanged pot was written");
    if (ShopMoneyFromPot(&r, "barman", 1250) != 1 || r.squads[0].money != 1250 || r.squads[0].moneyChanges != 3 || r.generation != g + 1)
        Fail("t772m: a trade on the holder's pot did not become a change");
    if (ShopMoneyFromPot(&r, "nobody", 5) != 0 || ShopMoneyFromPot(&r, "barman", -1) != 0) Fail("t772m: a pot of no squad, or below 0, was written");
    /* the runner's pot: the record plus its own requests in flight the record does not carry */
    if (ShopReqCovered(r.squads[0], 77, 3) != 1 || ShopReqCovered(r.squads[0], 77, 4) != 0 || ShopReqCovered(r.squads[0], 99, 1) != 0)
        Fail("t772m: which of this game's requests the record carries");
    if (ShopRunnerPot(1250, 0) != 1250 || ShopRunnerPot(1250, 300) != 1550 || ShopRunnerPot(1250, -400) != 850) Fail("t772m: the runner's pot with requests in flight");
    if (ShopRunnerPot(100, -400) != 0 || ShopRunnerPot(kSrMoneyMax, 5) != kSrMoneyMax) Fail("t772m: the runner's pot left its range");
    /* the roads */
    if (ShopMoneyRoute(0, 1) != kMoneyToday || ShopMoneyRoute(0, 0) != kMoneyToday) Fail("t772m: a keeper without a record left today's roads");
    if (ShopMoneyRoute(1, 1) != kMoneyHolderCapture || ShopMoneyRoute(1, 0) != kMoneyFromRecord) Fail("t772m: a recorded keeper's road");
    /* a refused write: the working copy goes back to the world server's stored record (the last one received, or this game's own
       accepted write of that generation); a record at the working copy's generation from another writer is taken */
    {
        ShopRec srv; srv.key = r.key; srv.writerSlot = 1; srv.generation = 9; srv.deadline = 30.0; srv.refills = 1;
        ShopSquad sq; sq.name = "barman"; sq.money = 700; sq.moneyChanges = 5;
        ShopReqNote(&sq, 5, 77, 11, 1, 300);
        srv.squads.push_back(sq);
        std::vector<ShopRec> own;
        ShopRec base;
        if (ShopRefusalBase(1, srv, own, 9, 9, &base) != 1 || base.writerSlot != 1 || base.squads[0].money != 700 || ShopReqSeen(base.squads[0], 5, 77, 11) != 1)
            Fail("t772h: a refused write did not go back to the server's last record (its money and request marks)");
        if (ShopRefusalBase(0, srv, own, 9, 9, &base) != 0) Fail("t772h: a refusal with no server record and no own write found a base");
        ShopRec acc = srv; acc.writerSlot = 2; acc.generation = 10; acc.squads[0].money = 900;
        own.push_back(acc);
        if (ShopRefusalBase(1, srv, own, 10, 11, &base) != 1 || base.generation != 10 || base.squads[0].money != 900)
            Fail("t772h: this game's own stored write was not the base after a later write's refusal");
        if (ShopRefusalBase(1, srv, own, 10, 10, &base) != 0) Fail("t772h: the refused write itself was taken as the stored one");
        if (ShopRefusalBase(1, srv, own, 12, 13, &base) != 0) Fail("t772h: an unknown stored generation found a base");
        if (ShopRefusalBase(1, srv, own, 0, 1, &base) != 0) Fail("t772h: a refusal with nothing stored found a base");
        if (ShopArrivalTakes(1, 9, 2, 0, 0, 9, 1) != 1) Fail("t772h: another writer's record at the working copy's generation was not taken");
        if (ShopArrivalTakes(1, 9, 2, 0, 0, 9, 2) != 0) Fail("t772h: this game's own generation replaced the working copy");
        if (ShopArrivalTakes(1, 10, 2, 0, 0, 9, 1) != 0) Fail("t772h: an older record replaced a newer working copy");
        if (ShopArrivalTakes(1, 10, 2, 1, 9, 9, 1) != 1) Fail("t772h: a record at the stored generation after a refusal was not taken");
        if (ShopArrivalTakes(1, 10, 2, 1, 9, 8, 1) != 0) Fail("t772h: a record below the stored generation ended the refused state");
        if (ShopArrivalTakes(0, 0, 0, 0, 0, 1, 1) != 1 || ShopArrivalTakes(1, 4, 1, 0, 0, 5, 1) != 1) Fail("t772h: a first or a newer record was not taken");
    }
    /* a resend is answered from the entry (n, move); only that exact id counts as applied; a cancel marks it once */
    {
        ShopSquad q; q.name = "barman";
        ShopReqNote(&q, 5, 77, 20, 2, 300);
        ShopReqNote(&q, 5, 77, 22, 1, -120);
        const ShopReqDone* d = ShopReqFind(q, 5, 77, 20);
        if (d == 0 || d->n != 2 || d->move != 300 || d->revoked != 0) Fail("t772g: a resend did not find its applied entry");
        if (ShopReqSeen(q, 5, 77, 21) != 0 || ShopReqSeen(q, 5, 77, 19) != 0) Fail("t772g: a lower id than the last applied was taken as applied");
        if (ShopReqSeen(q, 5, 78, 20) != 0 || ShopReqSeen(q, 6, 77, 20) != 0) Fail("t772g: another epoch's or asker's request was taken as applied");
        if (ShopReqCovered(q, 77, 21) != 0 || ShopReqCovered(q, 77, 22) != 1 || ShopReqCovered(q, 77, 20) != 1) Fail("t772g: an in-flight request covered by id range, not exactly");
        int mv = 0;
        if (ShopReqRevoke(&q, 5, 77, 20, &mv) != 1 || mv != 300 || ShopReqFind(q, 5, 77, 20) == 0 || ShopReqFind(q, 5, 77, 20)->revoked != 1)
            Fail("t772g: a cancel was not marked with its money");
        if (ShopReqRevoke(&q, 5, 77, 20, &mv) != 0 || mv != 0) Fail("t772g: a cancel was taken twice");
        if (ShopReqNote(&q, 5, 77, 20, 2, 300) != 0) Fail("t772g: a cancelled request was noted as applied again");
        ShopRec rr = r; rr.squads[0] = q;
        std::vector<char> bb;
        ShopRec back2;
        if (!EncodeShopRec(&bb, rr) || DecodeShopRec(&bb[0], bb.size(), &back2) != kSrOk) Fail("t772g: a record with applied entries did not round-trip");
        else
        {
            const ShopReqDone* e = ShopReqFind(back2.squads[0], 5, 77, 22);
            const ShopReqDone* gq = ShopReqFind(back2.squads[0], 5, 77, 20);
            if (e == 0 || e->move != -120 || e->n != 1 || gq == 0 || gq->revoked != 1) Fail("t772g: an applied entry did not round-trip");
        }
        for (unsigned int i = 0; i < kSrReqKeep + 2; ++i) ShopReqNote(&q, 5, 77, 100 + i, 1, 1);
        if (q.reqs[0].done.size() != kSrReqKeep || ShopReqSeen(q, 5, 77, 100) != 0 || ShopReqSeen(q, 5, 77, 100 + kSrReqKeep + 1) != 1)
            Fail("t772g: a full per-asker list did not keep its newest entries");
        ShopRec big; big.key = r.key; big.writerSlot = 1; big.generation = 1;
        for (unsigned int i = 0; i < kSrMaxSquads; ++i)
        {
            ShopSquad x; x.name = std::string(kSrNameMax, (char)('a' + (int)i));
            for (unsigned int a = 0; a < kSrMaxReqs; ++a) for (unsigned int j = 0; j < kSrReqKeep; ++j) ShopReqNote(&x, a, 1, j + 1, 1, -1);
            big.squads.push_back(x);
        }
        if (!EncodeShopRec(&bb, big)) Fail("t772g: a full record (every squad, asker and entry) did not fit the size cap");
    }
    /* who may move a recorded keeper's money: only the exact holder with a usable copy; the plain item road never (shop-tail only) */
    if (ShopTradeRole(0, 0, 0) != 0 || ShopTradeRole(0, 1, 1) != 0) Fail("t772x: a keeper no record carries left today's roads");
    if (ShopTradeRole(1, 1, 0) != 1) Fail("t772x: the exact holder with a usable copy could not record a trade");
    if (ShopTradeRole(1, 0, 0) != 2) Fail("t772x: a game that is not the exact holder applied a recorded keeper's trade");
    if (ShopTradeRole(1, 1, 1) != 2) Fail("t772x: a refused copy applied a recorded keeper's trade");
    /* a record with no room for another squad refuses that squad's refill (no refill every pass with nothing written) */
    {
        ShopRec fr; fr.key = r.key; fr.writerSlot = 1; fr.deadline = 50.0; fr.refills = 1;
        for (unsigned int i = 0; i < kSrMaxSquads; ++i) { ShopSquad x; x.name = std::string(1, (char)('a' + (int)i)); x.refillsApplied = 1; fr.squads.push_back(x); }
        if (ShopRecFull(fr, -1) != 1 || ShopRecFull(fr, 0) != 0) Fail("t772l: a full record");
        if (ShopGateDecide(kRoadTimed, 1, 1, 1, 0, 1, 0, 1) != kGateRefuseFull || ShopGateDecide(kRoadForced, 1, 1, 1, 0, 1, 0, 1) != kGateRefuseFull
            || ShopGateDecide(kRoadHolderStarted, 1, 1, 1, 0, 1, 0, 1) != kGateRefuseFull) Fail("t772l: a 17th squad's refill ran");
        if (ShopGateDecide(kRoadLever, 1, 1, 1, 0, 1, 0, 1) != kGateRun) Fail("t772l: the lever did not run on a full record");
        const unsigned int g0 = fr.generation;
        if (ShopAfterRun(&fr, 1, "zz", 1, 80.0, 60.0, 1, 5) != 0 || fr.generation != g0 || fr.squads.size() != kSrMaxSquads) Fail("t772l: a 17th squad was written");
    }
    /* the pot a game that is not the holder shows: every own in-flight trade that moved that pot here, whoever runs the keeper */
    {
        ShopSquad q; q.name = "barman"; q.money = 1000;
        ShopReqNote(&q, 3, 77, 5, 1, 200);
        int potA = 0, potB = 0;
        long long sum = 0;
        sum += ShopInFlightAdd(q, 77, 5, &potA, &potA, 200);    /* in the record already */
        sum += ShopInFlightAdd(q, 77, 6, &potA, &potA, 150);    /* a runner's move */
        sum += ShopInFlightAdd(q, 77, 7, &potA, &potA, -40);    /* a copy's move (another game runs the keeper) */
        sum += ShopInFlightAdd(q, 77, 8, &potB, &potA, 999);    /* another pot */
        sum += ShopInFlightAdd(q, 77, 9, 0, &potA, 999);        /* no pot read */
        if (sum != 110 || ShopRunnerPot(q.money, sum) != 1110) Fail("t772p: the in-flight sum on the pot");
    }
    /* the holder's pot by change: the record's money plus its own trades since the pot last matched it */
    if (ShopPotByChange(1100, 1050, 1050) != 1100 || ShopPotByChange(1150, 1060, 1050) != 1160 || ShopPotByChange(10, 0, 50) != 0
        || ShopPotByChange(kSrMoneyMax, 10, 0) != kSrMoneyMax) Fail("t772d: the holder's pot by change");
    /* REBASE at a holder change: A (old holder) wrote gen 5 = 1000 + 100; B (new holder, built on gen 4 = 1000) applied +50 and a
       request, and wrote gen 5 - refused. Whichever arrives first, B ends with 1150 and both requests, never 1050 or 1100. */
    {
        std::vector<std::string> held(1, "barman");
        ShopRec p4; p4.generation = 4; p4.writerSlot = 1; { ShopSquad s; s.name = "barman"; s.money = 1000; p4.squads.push_back(s); }
        ShopRec a5 = p4; a5.generation = 5; a5.squads[0].money = 1100; a5.squads[0].moneyChanges = 1; ShopReqNote(&a5.squads[0], 7, 1, 2, 1, 100);
        ShopRec b5 = p4; b5.generation = 5; b5.writerSlot = 2; b5.squads[0].money = 1050; b5.squads[0].moneyChanges = 1; ShopReqNote(&b5.squads[0], 3, 9, 4, 1, 50);
        /* refusal first (B's copy stale), then A's 5 arrives: the parent is the server's last record before it (gen 4) */
        std::vector<ShopRec> own;   /* B's refused write left its own list */
        const ShopRec* par = ShopRebaseParent(1, p4, own, 4);
        ShopRec out;
        if (par == 0 || par->generation != 4) Fail("t772d: the parent of an arrival after a refusal");
        else if (ShopRebase(a5, b5, *par, held, &out) != 2 || out.squads[0].money != 1150 || out.squads[0].moneyChanges != 2
                 || ShopReqFind(out.squads[0], 7, 1, 2) == 0 || ShopReqFind(out.squads[0], 3, 9, 4) == 0)
            Fail("t772d: A's arrival after B's refusal lost a change or a request");
        /* A's 5 arrives first (same generation, another writer) while B's 5 is still in B's own list: B's 5 is not under A's 5,
           so the parent is still gen 4 - the same rebase; B writes it as gen 6 */
        std::vector<ShopRec> ownFirst(1, b5);
        par = ShopRebaseParent(1, p4, ownFirst, 4);
        if (par == 0 || par->generation != 4) Fail("t772d: the parent of an arrival at the generation of this game's own write");
        ShopRec b6 = out; b6.generation = 6; b6.writerSlot = 2;
        own.push_back(b6);
        /* then the refusal of B's 5 names A's 5 as stored - the record B's copy is already built on: it stands (no double) */
        if (ShopRefusalSuperseded(1, 5, 1, 5, 1) != 1) Fail("t772d: a refusal after its stored record's arrival made the copy fall back");
        if (ShopRefusalSuperseded(1, 4, 1, 5, 1) != 0 || ShopRefusalSuperseded(1, 5, 2, 5, 1) != 0 || ShopRefusalSuperseded(0, 5, 1, 5, 1) != 0)
            Fail("t772d: a refusal was taken as superseded with the copy built on another record");
        par = ShopRebaseParent(1, a5, own, 5);
        if (par == 0 || par->generation != 5 || par->writerSlot != 1) Fail("t772d: this game's write after its base was taken as under the base");
        /* an ordinary hand-over: B's gen 5 was stored, A built gen 6 on it - nothing of B's goes on again */
        std::vector<ShopRec> ownB(1, b5);
        ShopRec a6 = b5; a6.generation = 6; a6.writerSlot = 1; a6.squads[0].money = 1150;
        par = ShopRebaseParent(1, p4, ownB, 5);
        if (par == 0 || par->generation != 5 || ShopRebase(a6, b5, *par, held, &out) != 0 || out.squads[0].money != 1150)
            Fail("t772d: an arrival built on this game's stored write took its changes again");
        /* a squad whose pot this game does not hold: the arrival is the truth */
        if (ShopRebase(a5, b5, p4, std::vector<std::string>(), &out) != 0 || out.squads[0].money != 1100 || ShopReqFind(out.squads[0], 3, 9, 4) != 0)
            Fail("t772d: a squad this game does not hold was rebased");
        /* a cancel this game marked since the parent goes on as a mark */
        ShopRec pc = a5; ShopRec wc = a5; int mv = 0;
        ShopReqRevoke(&wc.squads[0], 7, 1, 2, &mv);
        wc.squads[0].money -= 100;
        ShopRec ac = a5; ac.generation = 6;
        if (ShopRebase(ac, wc, pc, held, &out) != 2 || out.squads[0].money != 1000 || ShopReqFind(out.squads[0], 7, 1, 2) == 0
            || ShopReqFind(out.squads[0], 7, 1, 2)->revoked != 1) Fail("t772d: a cancel's mark and undo did not go on top");
        if (ShopRebaseParent(0, p4, std::vector<ShopRec>(), 9) != 0) Fail("t772d: a parent was found with none known");
    }
    /* the asker's floor: a full list drops its lowest id; an id at or below the floor that is not kept is refused, never applied */
    {
        ShopSquad f; f.name = "barman";
        for (unsigned int i = 0; i < kSrReqKeep; ++i) ShopReqNote(&f, 5, 1, 17 - i, 1, 10);   /* ids 17 down to 10 */
        if (ShopReqBelowFloor(f, 5, 1, 9) != 1 || ShopReqBelowFloor(f, 5, 1, 10) != 0) Fail("t772e: the floor of a full list");
        if (ShopReqNote(&f, 5, 1, 18, 1, 10) != 1 || ShopReqSeen(f, 5, 1, 10) != 0 || ShopReqSeen(f, 5, 1, 11) != 1 || ShopReqSeen(f, 5, 1, 18) != 1)
            Fail("t772e: a full list did not drop its lowest id");
        if (ShopReqBelowFloor(f, 5, 1, 10) != 1 || ShopReqBelowFloor(f, 5, 1, 11) != 0 || ShopReqBelowFloor(f, 5, 1, 19) != 0)
            Fail("t772e: a dropped id was not below the floor");
        if (ShopReqBelowFloor(f, 5, 2, 3) != 0 || ShopReqBelowFloor(f, 6, 1, 3) != 0) Fail("t772e: another epoch or asker had a floor");
        if (ShopReqNote(&f, 5, 1, 4, 1, 10) != 0 || ShopReqSeen(f, 5, 1, 4) != 0) Fail("t772e: an id below the floor was kept");
        ShopSquad g; g.name = "barman"; ShopReqNote(&g, 5, 1, 3, 1, 10);
        if (ShopReqBelowFloor(g, 5, 1, 1) != 0) Fail("t772e: a list that is not full had a floor");
        /* the rebase does not bring back an id the arrival's list dropped */
        ShopRec on; on.generation = 3; on.squads.push_back(f);
        ShopRec par; par.generation = 1; { ShopSquad s; s.name = "barman"; par.squads.push_back(s); }
        ShopRec wk = par; ShopReqNote(&wk.squads[0], 5, 1, 9, 1, 10);
        ShopRec out;
        if (ShopRebase(on, wk, par, std::vector<std::string>(1, "barman"), &out) != 0 || ShopReqSeen(out.squads[0], 5, 1, 9) != 0)
            Fail("t772e: the rebase brought back an id below the floor");
    }
    /* a restock whose shop does not read: only a read leader with no home takes the old road */
    if (ShopUnreadVerdict(1, 0) != 0 || ShopUnreadVerdict(0, 0) != 2 || ShopUnreadVerdict(0, 1) != 2 || ShopUnreadVerdict(1, 1) != 2)
        Fail("t772u: the unread-shop verdict");
    /* an asker's cancel: marked only by the exact holder with a usable copy; elsewhere counted, never undone through the record */
    if (ShopRevokeRoad(0, 1, 0) != 0 || ShopRevokeRoad(1, 1, 0) != 1 || ShopRevokeRoad(1, 0, 0) != 2 || ShopRevokeRoad(1, 1, 1) != 2)
        Fail("t772c: the cancel road");
}
/* T-772c: a home shop trade's cancel at the holder of the shop's area. The holder's step per case (not the holder before anything;
   no record yet is no answer), the asker's ending per answer and which answer it takes, when it sends (a refusal held 10 s), the
   settle-only road (this game holds the area: decided here; no holder: kept pending) and verdict (never a fresh apply), the cancelled
   marker (it blocks a later fresh apply, marking twice changes nothing), the rebase (a cancelled entry's money out of the pot; a marker
   never cancels an applied entry), the holder's steps on a fake record (undo once on a repeat and across a link loss, an echo after a
   lost answer, a marker meeting a stored applied entry, a cancel naming another shop), the request flagged settle-only and the tail
   kinds 2 / 3 on the wire (kind 4 and an answer out of range refused), and the write an echo answers. */
void t_t772c_holder_cancel()
{
    using namespace shoprec;
    using namespace coopshop;
    int ans = -1;
    /* the holder's step: npcBuy, caravan, stockOk, exact, carried, known, stale, entry, marker, stored (0 / 1 owed / 2 undone), appliedHere, descOk */
    if (CancelHolderDecide(1, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 1, &ans) != kChAnswerNow || ans != kCaRefused) Fail("t772c: an NPC purchase's cancel was not refused");
    if (CancelHolderDecide(0, 1, 1, 1, 1, 1, 0, 1, 0, 0, 1, 1, &ans) != kChAnswerNow || ans != kCaRefused) Fail("t772c: a caravan's cancel by kind 2 was not refused");
    if (CancelHolderDecide(0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, &ans) != kChAnswerNow || ans != kCaNotHolder) Fail("t772c: a shop that does not resolve or is held elsewhere was not not-holder");
    if (CancelHolderDecide(0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, &ans) != kChAnswerNow || ans != kCaNotHolder) Fail("t772c: a game the area map does not name answered other than not-holder");
    if (CancelHolderDecide(0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, &ans) != kChAnswerNow || ans != kCaNotHolder) Fail("t772c: no record and not the exact holder was not not-holder");
    if (CancelHolderDecide(0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, &ans) != kChNoAnswer) Fail("t772c: the exact holder with the record not arrived answered (it waits for the repeat)");
    if (CancelHolderDecide(0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, &ans) != kChAnswerNow || ans != kCaUnknown) Fail("t772c: no record will come and no applied row was not unknown");
    if (CancelHolderDecide(0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, &ans) != kChUndoLocal) Fail("t772c: no record and an applied row here was not undone from the row");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, &ans) != kChNoAnswer) Fail("t772c: a refused copy answered");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsBelowFloor, 0, 0, 0, 1, &ans) != kChAnswerNow || ans != kCaUnknown) Fail("t772c: below the floor was not unknown");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsRevoked, 1, 2, 0, 1, &ans) != kChAnswerNow || ans != kCaNotApplied) Fail("t772c: a stored marker was not not-applied");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsRevoked, 0, 2, 0, 1, &ans) != kChAnswerNow || ans != kCaUndone) Fail("t772c: a stored undone entry was not undone");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsRevoked, 0, 1, 0, 1, &ans) != kChWaitStored || ans != kCaUndone) Fail("t772c: an undone mark in this game's copy only was answered");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsRevoked, 1, 0, 0, 1, &ans) != kChWaitStored || ans != kCaNotApplied) Fail("t772c: a marker in this game's copy only was answered before it was stored");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsApplied, 0, 0, 0, 1, &ans) != kChMarkOwed) Fail("t772c: an applied entry was not marked owed first");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsOwed, 0, 0, 0, 1, &ans) != kChWaitStored) Fail("t772c: an owed mark in this game's copy only was undone");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsOwed, 0, 1, 0, 1, &ans) != kChWaitStored || ans != kCaUndone) Fail("t772c: the cancel step did not wait for the owed sweep");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsClaimed, 0, 1, 0, 1, &ans) != kChWaitStored || ans != kCaUndone) Fail("t772c: a claimed undo was not waited for");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsApplied, 0, 0, 0, 0, &ans) != kChAnswerNow || ans != kCaRefused) Fail("t772c: a cancel naming another item than the entry undid it");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsOwed, 0, 1, 0, 0, &ans) != kChAnswerNow || ans != kCaRefused) Fail("t772c: an owed undo ran for a cancel naming another item");
    if (CancelHolderDecide(0, 0, 1, 1, 1, 1, 0, kRsNone, 0, 0, 0, 1, &ans) != kChMarker) Fail("t772c: no entry did not get a marker");
    /* the asker's ending per answer, and which answer it takes: only from the game the cancel was last sent to */
    if (CancelAskerOnAnswer(kCaUndone) != kCeEnd || CancelAskerOnAnswer(kCaNotApplied) != kCeEnd || CancelAskerOnAnswer(kCaRefused) != kCeEnd
        || CancelAskerOnAnswer(kCaUnknown) != kCeEnd) Fail("t772c: an ending answer kept the cancel");
    if (CancelAskerOnAnswer(kCaNotHolder) != kCeAwaitHolderChange || CancelAskerOnAnswer(kCaWriteRefused) != kCeAwaitHolderChange)
        Fail("t772c: not-holder / write-refused ended the cancel");
    if (CancelAnswerTake(2, 2, kCaUndone) != kTakeEnded || CancelAnswerTake(2, 2, kCaNotHolder) != kTakeKept || CancelAnswerTake(2, 2, kCaWriteRefused) != kTakeKept)
        Fail("t772c: an answer from the game it was sent to was not taken");
    if (CancelAnswerTake(3, 2, kCaUndone) != kTakeIgnored || CancelAnswerTake(-1, 2, kCaUndone) != kTakeIgnored || CancelAnswerTake(3, -1, kCaNotApplied) != kTakeIgnored)
        Fail("t772c: an answer from a game the cancel was not last sent to was taken");
    /* when the asker sends: holder, mySlot, refusedBy, refusedAgeMs */
    if (CancelSendDue(-1, 0, -1, 0) != kCsWait || CancelSendDue(-2, 0, -1, 0) != kCsWait) Fail("t772c: a cancel went with no holder known (or a frozen area)");
    if (CancelSendDue(2, 0, -1, 0) != kCsSend) Fail("t772c: an unanswered cancel was not sent to the holder (again each second)");
    if (CancelSendDue(2, 0, 2, 0) != kCsWait || CancelSendDue(2, 0, 2, kRefusedHoldMs - 1) != kCsWait) Fail("t772c: a not-holder answer was sent again to the same game at once");
    if (CancelSendDue(2, 0, 2, kRefusedHoldMs) != kCsSend) Fail("t772c: a refusal never expired (a holder briefly not exact leaves the cancel stuck)");
    if (CancelSendDue(3, 0, 2, 0) != kCsSend) Fail("t772c: a not-holder answer was not sent to the next holder");
    if (CancelSendDue(0, 0, 2, 0) != kCsLocal || CancelSendDue(0, 0, -1, 0) != kCsLocal) Fail("t772c: this game holding the area did not answer its own cancel");
    if (CancelSendDue(0, -1, -1, 0) != kCsSend) Fail("t772c: a game with no slot took a holder slot for its own");
    /* the settle-only road: homeStock, holder, mySlot, refusedBy (the game that could not answer), refusedAgeMs */
    if (SettleOnlyDue(1, 2, 0, 1, 0) != kLrSettleOnly) Fail("t772c: a lapse with another holder was not asked settle-only");
    if (SettleOnlyDue(0, 2, 0, 1, 0) != kLrLapse) Fail("t772c: a caravan was asked settle-only");
    if (SettleOnlyDue(1, 0, 0, 1, 0) != kLrLocal) Fail("t772c: this game holding the area did not decide it from its own record");
    if (SettleOnlyDue(1, -1, 0, 1, 0) != kLrWait || SettleOnlyDue(1, -2, 0, 1, 0) != kLrWait) Fail("t772c: no holder known (or the area frozen after its holder left) settled it as a refusal");
    if (SettleOnlyDue(1, 1, 0, 1, 0) != kLrWait || SettleOnlyDue(1, 1, 0, 1, kRefusedHoldMs) != kLrSettleOnly) Fail("t772c: the game that could not answer was asked again at once, or never");
    /* the settle-only verdict: keeperFound, exact, carried, known, stale, entry, stored */
    if (SettleOnlyDecide(1, 1, 1, 1, 0, kRsApplied, 0) != kSoFromRecord || SettleOnlyDecide(1, 1, 1, 1, 0, kRsNone, 0) != kSoMarker) Fail("t772c: the settle-only verdict");
    if (SettleOnlyDecide(1, 1, 1, 1, 0, kRsRevoked, 1) != kSoRefuse || SettleOnlyDecide(1, 1, 1, 1, 0, kRsBelowFloor, 0) != kSoRefuse || SettleOnlyDecide(1, 1, 0, 1, 0, kRsNone, 0) != kSoRefuse)
        Fail("t772c: a settle-only ask with a stored cancel, below the floor, or no record that will come was not refused for good");
    if (SettleOnlyDecide(1, 1, 1, 1, 0, kRsRevoked, 0) != kSoWaitStored) Fail("t772c: a settle-only refusal went before its marker was stored");
    if (SettleOnlyDecide(0, 1, 1, 1, 0, kRsApplied, 0) != kSoNotHolder || SettleOnlyDecide(1, 0, 1, 1, 0, kRsApplied, 0) != kSoNotHolder
        || SettleOnlyDecide(1, 1, 0, 0, 0, kRsNone, 0) != kSoNotHolder || SettleOnlyDecide(1, 1, 1, 1, 1, kRsApplied, 0) != kSoNotHolder)
        Fail("t772c: a settle-only ask not answerable now was refused for good");
    for (int bits = 0; bits < 64; ++bits)
        for (int e = 0; e <= kRsBelowFloor; ++e)
        {
            const int v = SettleOnlyDecide(bits & 1, (bits >> 1) & 1, (bits >> 2) & 1, (bits >> 3) & 1, (bits >> 4) & 1, e, (bits >> 5) & 1);
            if (v < kSoFromRecord || v > kSoWaitStored) Fail("t772c: a settle-only verdict outside its five answers");
            if (v == kSoFromRecord && e != kRsApplied) Fail("t772c: a settle-only ask was answered ok with no applied entry");
        }
    /* the marker: an entry of no units and no money, cancelled - a resend finds it seen and cancelled, never applies it fresh */
    ShopSquad s; s.name = "barman";
    int mk = 0, mv = 0;
    if (ShopReqMarker(&s, 5, 9, 3) != 1) Fail("t772c: no marker was written");
    const ShopReqDone* d = ShopReqFind(s, 5, 9, 3);
    if (d == 0 || d->revoked != 1 || d->n != 0 || d->move != 0) Fail("t772c: the marker is not a cancelled entry of nothing");
    if (ShopReqState(s, 5, 9, 3, &mk) != kRsRevoked || mk != 1) Fail("t772c: the marker does not read as a marker");
    if (ShopReqSeen(s, 5, 9, 3) != 1) Fail("t772c: a later resend of a marked request would be applied fresh");
    if (ShopReqMarker(&s, 5, 9, 3) != 0 || ShopReqRevoke(&s, 5, 9, 3, &mv) != 0) Fail("t772c: a marked request was marked again");
    /* an applied entry: marked once - the undo hangs on 'marked now' */
    ShopReqNote(&s, 5, 9, 4, 2, 120);
    if (ShopReqState(s, 5, 9, 4, &mk) != kRsApplied || ShopReqMarker(&s, 5, 9, 4) != 0) Fail("t772c: an applied entry took a marker");
    if (ShopReqRevoke(&s, 5, 9, 4, &mv) != 1 || mv != 120) Fail("t772c: an applied entry was not marked with its money");
    if (ShopReqRevoke(&s, 5, 9, 4, &mv) != 0 || mv != 0) Fail("t772c: a second mark would undo the trade again");
    if (ShopReqState(s, 5, 9, 4, &mk) != kRsRevoked || mk != 0) Fail("t772c: an applied cancelled entry reads as a marker");
    ShopSquad f; f.name = "barman";
    for (unsigned int i = 0; i < kSrReqKeep; ++i) ShopReqNote(&f, 5, 1, 17 - i, 1, 10);
    if (ShopReqState(f, 5, 1, 9, &mk) != kRsBelowFloor || ShopReqMarker(&f, 5, 1, 9) != 0) Fail("t772c: below the floor took a marker");
    ShopRec rr; rr.squads.push_back(s);
    if (ShopRecMarked(rr, "barman", 5, 9, 4) != 1 || ShopRecMarked(rr, "barman", 5, 9, 99) != 0 || ShopRecMarked(rr, "other", 5, 9, 4) != 0)
        Fail("t772c: a stored record's mark was misread");
    /* the rebase: a cancelled entry holds no money, whichever game marked it */
    {
        const std::vector<std::string> held(1, "barman");
        ShopRec par; par.generation = 4; { ShopSquad q; q.name = "barman"; q.money = 1000; par.squads.push_back(q); }
        ShopRec out;
        ShopRec wk = par; ShopReqNote(&wk.squads[0], 7, 1, 2, 1, 100); wk.squads[0].money = 1100;
        ShopRec on = par; on.generation = 5; ShopReqMarker(&on.squads[0], 7, 1, 2);
        ShopRebase(on, wk, par, held, &out);
        if (out.squads[0].money != 1100 || ShopRecMarked(out, "barman", 7, 1, 2) != 0 || ShopReqState(out.squads[0], 7, 1, 2, &mk) != kRsApplied)
            Fail("t772c: another game's marker cancelled this game's applied trade (the trade happened: it stands)");
        ShopRec on2 = par; on2.generation = 5; ShopReqNote(&on2.squads[0], 7, 1, 3, 1, 100); on2.squads[0].money = 1100;
        ShopRec wk2 = par; ShopReqMarker(&wk2.squads[0], 7, 1, 3);
        ShopRebase(on2, wk2, par, held, &out);
        if (out.squads[0].money != 1100 || ShopRecMarked(out, "barman", 7, 1, 3) != 0 || ShopReqState(out.squads[0], 7, 1, 3, &mk) != kRsApplied)
            Fail("t772c: this game's marker cancelled another holder's applied trade (the trade happened: it stands)");
        ShopRec p3 = par; ShopReqNote(&p3.squads[0], 7, 1, 4, 1, 100); p3.squads[0].money = 1100;
        ShopRec w3 = p3; ShopReqRevoke(&w3.squads[0], 7, 1, 4, &mv); w3.squads[0].money = 1000;
        ShopRec o3 = w3; o3.generation = 6;
        ShopRebase(o3, w3, p3, held, &out);
        if (out.squads[0].money != 1000) Fail("t772c: an undo both games made was taken twice");
        ShopRec o4 = p3; o4.generation = 6;
        ShopRebase(o4, w3, p3, held, &out);
        if (out.squads[0].money != 1000 || ShopRecMarked(out, "barman", 7, 1, 4) != 1) Fail("t772c: this game's own undo did not go on top once");
    }
    /* THE OWED UNDO ON A FAKE RECORD: asker 5, epoch 9, request 4 applied by a holder (2 units, 120 money), stored. Holder A (slot 1)
       marks it owed; the owed sweep's decision (ShopOwedDue) claims it, applies it once under A's own stored claim, marks it undone. */
    {
        ShopRec srv; srv.generation = 7; srv.key = "Hub bar@3,4@1234,-567,89"; srv.repairMark = kSrNoRepair;
        { ShopSquad q; q.name = "barman"; q.money = 1120; ShopReqNote(&q, 5, 9, 4, 2, 120); srv.squads.push_back(q); }
        const std::vector<std::string> heldA(1, "barman");
        std::vector<ShopReqKey> ranA;
        ShopRec a = srv;
        ShopCancelOut o = ShopCancelStep(&a, 1, 1, srv, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, ShopKeyHas(ranA, 5, 9, 4), 1, 2);
        if (o.step != kChMarkOwed || o.mark != 1 || o.undo != 0 || o.n != 2 || o.move != 120) Fail("t772c: a cancel did not mark the applied entry owed first");
        o = ShopCancelStep(&a, 1, 1, srv, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, ShopKeyHas(ranA, 5, 9, 4), 1, 2);
        if (o.step != kChWaitStored || o.undo != 0 || o.mark != 0) Fail("t772c: a repeat before the owed mark was stored undid or marked again");
        ShopRec back = srv;   /* the owed write refused: the copy is the stored record again - marked owed again, nothing undone */
        o = ShopCancelStep(&back, 1, 1, srv, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, 0, 1, 2);
        if (o.step != kChMarkOwed || o.undo != 0) Fail("t772c: a refused owed write had undone something");
        ShopRec s1 = a; s1.generation = 8;   /* the owed mark stored: the cancel step waits, the sweep claims */
        o = ShopCancelStep(&a, 1, 1, s1, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, 0, 1, 2);
        if (o.step != kChWaitStored || o.undo != 0) Fail("t772c: the cancel step applied an owed undo itself");
        if (ShopOwedDue((int)kRevOwed, 0, (int)kRevOwed, 0, 1, 1, 1, 0) != kOwClaim) Fail("t772c: the exact holder did not claim a stored owed undo");
        if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevOwed, 0, 1, 1, 1, 0) != kOwWait) Fail("t772c: a claim in flight was claimed again");
        /* A HAND-OVER: A (slot 1) and B (slot 2) both read the owed mark and claim; the world server stores A's - B's rebases under it */
        ShopRec aC = s1; ShopRec bC = s1;
        if (ShopReqClaim(&aC.squads[0], 5, 9, 4, 1) != 1 || ShopReqClaim(&bC.squads[0], 5, 9, 4, 2) != 1 || ShopReqClaim(&aC.squads[0], 5, 9, 4, 1) != 0) Fail("t772c: a claim was not written once");
        ShopRec stA = aC; stA.generation = 9;
        ShopRec bOut;
        ShopRebase(stA, bC, s1, heldA, &bOut);
        const ShopReqDone* bd = ShopReqFind(bOut.squads[0], 5, 9, 4);
        if (bd == 0 || bd->revoked != kRevClaimed || bd->claimer != 1) Fail("t772c: a second claim overwrote the stored claim in the rebase");
        int applies = 0;
        for (int slot = 1; slot <= 2; ++slot) if (ShopOwedDue((int)kRevClaimed, (unsigned int)slot, (int)kRevClaimed, 1, slot, 1, 1, 0) == kOwApply) ++applies;
        if (applies != 1) Fail("t772c: an owed undo was applied by other than exactly one game (the one whose claim is stored)");
        ShopRec bU = bC; int mv = 0; ShopReqRevoke(&bU.squads[0], 5, 9, 4, &mv);   /* B's undone mark over A's stored claim never goes on */
        ShopRebase(stA, bU, s1, heldA, &bOut);
        bd = ShopReqFind(bOut.squads[0], 5, 9, 4);
        if (bd == 0 || bd->revoked != kRevClaimed || bd->claimer != 1) Fail("t772c: an undone mark went over another game's stored claim");
        /* A applies (its undo memory), marks undone; the undone write lost: A marks undone again, never applies again */
        ShopKeyAdd(&ranA, 5, 9, 4, 256);
        if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, ShopKeyHas(ranA, 5, 9, 4)) != kOwMarkUndone) Fail("t772c: a lost undone write made the claimer apply again");
        if (ShopOwedDue((int)kRevUndone, 1, (int)kRevClaimed, 1, 1, 1, 1, 1) != kOwWait) Fail("t772c: an undone mark in flight was written again at once");
        /* A quits and rejoins (a new world: its undo memory is empty) with the claim still stored: the shop's counters' records decide -
           one that lists the undo: marked undone only; records without it: applied (t772c_goods_undo, the full case) */
        std::vector<ShopReqKey> fresh;
        if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, ShopKeyHas(fresh, 5, 9, 4), kUgIn) != kOwMarkUndone) Fail("t772c: a claimer whose counters hold the undo applied it again");
        if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, ShopKeyHas(fresh, 5, 9, 4), kUgOut) != kOwApply) Fail("t772c: a claimer whose counters lack the undo did not apply it");
        if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, ShopKeyHas(fresh, 5, 9, 4), kUgWait) != kOwWait) Fail("t772c: a claimer applied with its counters not decided");
        if (ShopOwedDue((int)kRevOwed, 0, (int)kRevOwed, 0, 1, 0, 1, 0) != kOwWait || ShopOwedDue((int)kRevOwed, 0, (int)kRevOwed, 0, 1, 1, 0, 0) != kOwWait
            || ShopOwedDue((int)kRevOwed, 0, (int)kRevOwed, 0, -1, 1, 1, 0) != kOwWait || ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 0, 1, 0) != kOwWait)
            Fail("t772c: an owed undo was claimed or applied off the exact holder, with no slot or with nothing to undo from");
        if (ShopOwedDue((int)kRevApplied, 0, (int)kRevApplied, 0, 1, 1, 1, 0) != kOwNone || ShopOwedDue((int)kRevUndone, 1, (int)kRevUndone, 1, 1, 1, 1, 1) != kOwNone || ShopOwedDue(-1, 0, -1, 0, 1, 1, 1, 0) != kOwNone)
            Fail("t772c: the sweep acted on an entry with nothing owed");
        /* this game's note that its change to a counter carries an undo: noted at the filing, ended when the counter is set from a stored
           record or the world goes, kept while the counter's area is away */
        if (ShopUndoNoteStands(0, kUnFiled) != 1 || ShopUndoNoteStands(1, kUnSet) != 0 || ShopUndoNoteStands(1, kUnWorldGone) != 0
            || ShopUndoNoteStands(1, kUnAreaLeft) != 1 || ShopUndoNoteStands(0, kUnAreaLeft) != 0)
            Fail("t1073a: the counter's undo note does not live from its filing until the counter is set from a stored record");
        for (int oldRule = 0; oldRule < 2; ++oldRule)
        {   /* T-1074 THE GOODS KEPT WHILE AWAY: A applies an undo (the goods filed in the counter, noted), the counter's area leaves A before
               any write of it is stored, and comes back with the stored record the counter was last set from (not laid in again: the
               counter keeps the goods). The note stands, so the change is written again with the undo's key and stored: goods once, the
               refund once. oldRule: the note dropped at the area's leave - the goods read as lost and the undo was applied again: twice */
            int goods = 0, coins = 0, note = 0, stored = 0, partial = 0;
            goods += 1; note = ShopUndoNoteStands(note, kUnFiled);
            note = (oldRule != 0) ? 0 : ShopUndoNoteStands(note, kUnAreaLeft);
            std::vector<ShopUndoTouch> t(1);
            t[0].stored = stored; t[0].inFlight = note;
            int d = ShopUndoDurable(t, 0, &partial);
            if (d == kUgWait) stored = 1;   /* written again from the note: the write carries the key and is stored */
            else if (d == kUgOut)
            {   /* judged lost: the counter's stored record decides */
                std::vector<ShopUndoPiece> p(1);
                p[0].localHas = note; p[0].haveRecord = 1; p[0].recordHas = stored; p[0].applyDone = 1; p[0].ownSeed = 0; p[0].never = 0;
                if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, 0, ShopUndoGoods(p)) == kOwApply) { goods += 1; stored = 1; }
            }
            t[0].stored = stored; t[0].inFlight = 0;
            d = ShopUndoDurable(t, 0, &partial);
            if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, 0, d) == kOwMarkUndone) coins += 1;
            if (oldRule == 0 && (goods != 1 || coins != 1))
                Fail("t1073a: T-1074 an undo whose counter left with its area ends with " + I(goods) + " goods and " + I(coins) + " refunds (want 1 and 1)");
            if (oldRule != 0 && goods != 2) Fail("t1073a: the T-1074 control (the note dropped at the area's leave) does not show the goods twice");
        }
        for (int oldRule = 0; oldRule < 2; ++oldRule)
        {   /* T-1073 THE GOODS WIPED BY A NEWER RECORD: A applies an undo (goods filed, noted), its write not stored; the area goes to B, B
               writes the counter from the record before A's change (no goods, no key); A gets the area back and that newer record is laid
               into the counter - the goods are gone. The note ends at that set, so the counters' records decide: they lack the undo, it is
               applied again once - goods once, the refund once. oldRule: the note kept past the set - the change was written again
               without the goods but with the key, read as stored, and the money moved with no goods back */
            int goods = 0, coins = 0, note = 0, stored = 0, partial = 0;
            goods += 1; note = ShopUndoNoteStands(note, kUnFiled);
            note = ShopUndoNoteStands(note, kUnAreaLeft);
            goods = 0;   /* B's newer record laid in */
            if (oldRule == 0) note = ShopUndoNoteStands(note, kUnSet);
            std::vector<ShopUndoTouch> t(1);
            t[0].stored = stored; t[0].inFlight = note;
            int d = ShopUndoDurable(t, 0, &partial);
            if (d == kUgWait) stored = 1;   /* written again from the note: the counter as it is now, with the key */
            else if (d == kUgOut)
            {
                std::vector<ShopUndoPiece> p(1);
                p[0].localHas = note; p[0].haveRecord = 1; p[0].recordHas = stored; p[0].applyDone = 1; p[0].ownSeed = 0; p[0].never = 0;
                if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, 0, ShopUndoGoods(p)) == kOwApply) { goods += 1; stored = 1; }
            }
            t[0].stored = stored; t[0].inFlight = 0;
            d = ShopUndoDurable(t, 0, &partial);
            if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, 0, d) == kOwMarkUndone) coins += 1;
            if (oldRule == 0 && (goods != 1 || coins != 1))
                Fail("t1073a: T-1073 an undo whose goods a newer record wiped ends with " + I(goods) + " goods and " + I(coins) + " refunds (want 1 and 1)");
            if (oldRule != 0 && (goods != 0 || coins != 1)) Fail("t1073a: the T-1073 control (the note kept past the set) does not show the refund with no goods");
        }
        {   /* the wiring: the note ends at every set of a counter from a stored record, and no longer at its area's leave */
            const std::string itx = T313ReadFile("../coop-plugin/items.cpp");
            size_t ns = 0, at = 0;
            while ((at = itx.find("w.haveMine = 1; w.mineGen = kh.generation; ShopUndoNoteOn(id, shoprec::kUnSet);", at)) != std::string::npos) { ++ns; ++at; }
            if (ns != 4 || itx.find("g_boxUndoLocal.erase(it->first);   /* the counter went with its area */") != std::string::npos
                || itx.find("ShopUndoNoteOn(it->first, shoprec::kUnAreaLeft);") == std::string::npos)
                Fail("t1073a: the counter's undo note is not ended at every set from a stored record, or still ends at its area's leave");
        }
        /* the undone mark stored: the cancel is answered undone */
        ShopRec aU = stA; ShopReqRevoke(&aU.squads[0], 5, 9, 4, &mv);
        ShopRec s3 = aU; s3.generation = 10;
        o = ShopCancelStep(&aU, 1, 1, s3, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, 1, 1, 2);
        if (o.step != kChAnswerNow || o.answer != kCaUndone || o.undo != 0) Fail("t772c: a stored undone mark was not answered undone");
        o = ShopCancelStep(&aU, 1, 1, stA, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, 1, 1, 2);
        if (o.step != kChWaitStored) Fail("t772c: an undone mark in this game's copy only was answered");
        /* the claimed entry on the wire with its claimer; the owed count */
        std::vector<char> cb; ShopRec cback;
        if (!EncodeShopRec(&cb, stA) || DecodeShopRec(&cb[0], cb.size(), &cback) != kSrOk) Fail("t772c: a record with a claimed entry did not round-trip");
        const ShopReqDone* cd = ShopReqFind(cback.squads[0], 5, 9, 4);
        if (cd == 0 || cd->revoked != kRevClaimed || cd->claimer != 1) Fail("t772c: a claimed entry lost its claimer on the wire");
        if (ShopRecOwedCount(stA) != 1 || ShopRecOwedCount(s3) != 0 || ShopRecOwedCount(srv) != 0) Fail("t772c: the owed count is wrong");
        {   /* a record with no owed or claimed entry is byte for byte the released form, written out here field by field */
            static const unsigned char kRel[] = {
                0x53, 0x48, 0x50, 0x31,  2, 0, 0, 0,  10, 0, 0, 0,  0, 0, 0, 0,                  /* 'SHP1', format 2, generation 10, writer 0 */
                24, 0, 0, 0,  'H', 'u', 'b', ' ', 'b', 'a', 'r', '@', '3', ',', '4', '@', '1', '2', '3', '4', ',', '-', '5', '6', '7', ',', '8', '9',
                0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0,  0xFF, 0xFF, 0xFF, 0xFF,                    /* deadline 0.0, refills 0, no repair mark */
                1, 0, 0, 0,  6, 0, 0, 0,  'b', 'a', 'r', 'm', 'a', 'n',  0x60, 0x04, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,   /* barman, 1120, 0, 0 */
                1, 0, 0, 0,  5, 0, 0, 0,  9, 0, 0, 0,  1, 0, 0, 0,                              /* asker 5, epoch 9, one entry */
                4, 0, 0, 0,  2, 0, 0, 0,  120, 0, 0, 0,  1, 0, 0, 0 };                          /* id 4, 2 units, 120, undone */
            std::vector<char> rb; ShopRec rback;
            int same = (EncodeShopRec(&rb, s3) && rb.size() == sizeof kRel) ? 1 : 0;
            for (size_t i = 0; same != 0 && i < rb.size(); ++i) if ((unsigned char)rb[i] != kRel[i]) same = 0;
            if (same == 0 || DecodeShopRec(&rb[0], rb.size(), &rback) != kSrOk || rback.generation != 10 || ShopRecRank(rback, "barman", 5, 9, 4) != 3)
                Fail("t772c: a record with no owed entry is not byte for byte the released form");
        }
        /* the cancel names another shop, or fewer units than the entry moved: refused, nothing marked; the applied row here vouches for it */
        ShopRec w2 = srv;
        o = ShopCancelStep(&w2, 1, 1, srv, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, 0, 0, 2);
        if (o.step != kChAnswerNow || o.answer != kCaRefused || o.mark != 0 || o.undo != 0) Fail("t772c: a cancel naming another shop was marked");
        o = ShopCancelStep(&w2, 1, 1, srv, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1);
        if (o.step != kChAnswerNow || o.answer != kCaRefused) Fail("t772c: a cancel naming fewer units than the entry was marked");
        o = ShopCancelStep(&w2, 1, 1, srv, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0);
        if (o.step != kChMarkOwed || o.mark != 1) Fail("t772c: the applied row kept here did not vouch for its cancel");
        /* no copy of the record: not arrived (no answer), none will come (unknown), the applied row here (undone once, a repeat answered) */
        ShopRec none;
        o = ShopCancelStep(&none, 0, 0, none, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 0, 0, 0, 1, 2);
        if (o.step != kChNoAnswer) Fail("t772c: the exact holder answered with its record not arrived");
        o = ShopCancelStep(&none, 0, 0, none, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, 0, 1, 2);
        if (o.step != kChAnswerNow || o.answer != kCaUnknown) Fail("t772c: no record that will come was not unknown");
        o = ShopCancelStep(&none, 0, 0, none, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 0, 1, 0, 1, 2);
        if (o.step != kChUndoLocal || o.undo != 1 || o.answer != kCaUndone) Fail("t772c: the applied row here was not undone with no record");
        o = ShopCancelStep(&none, 0, 0, none, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 0, 1, 1, 1, 2);
        if (o.step != kChAnswerNow || o.undo != 0 || o.answer != kCaUndone) Fail("t772c: a repeat of an undo from the row undid it again");
        /* not the exact holder: not-holder before anything, the copy untouched */
        ShopRec w3 = srv;
        o = ShopCancelStep(&w3, 1, 1, srv, "barman", 5, 9, 4, 0, 0, 1, 0, 0, 1, 0, 0, 1, 2);
        if (o.step != kChAnswerNow || o.answer != kCaNotHolder || o.mark != 0 || ShopRecMarked(w3, "barman", 5, 9, 4) != 0) Fail("t772c: a game not the exact holder marked the record");
        /* the settle-only step: no entry - a marker, a repeat waits, refused once stored; an applied entry - ok from the record; an owed one refused once stored */
        ShopRec w4 = srv;
        ShopSettleOut so = ShopSettleOnlyStep(&w4, 1, 1, srv, "barman", 5, 9, 6, 1, 1, 0, 1);
        if (so.v != kSoMarker || so.mark != 1 || ShopRecMarked(w4, "barman", 5, 9, 6) != 1) Fail("t772c: a settle-only ask with no entry wrote no marker");
        so = ShopSettleOnlyStep(&w4, 1, 1, srv, "barman", 5, 9, 6, 1, 1, 0, 1);
        if (so.v != kSoWaitStored || so.mark != 0) Fail("t772c: a settle-only refusal went before its marker was stored");
        so = ShopSettleOnlyStep(&w4, 1, 1, w4, "barman", 5, 9, 6, 1, 1, 0, 1);
        if (so.v != kSoRefuse) Fail("t772c: a stored settle-only marker was not refused for good");
        so = ShopSettleOnlyStep(&w4, 1, 1, srv, "barman", 5, 9, 4, 1, 1, 0, 1);
        if (so.v != kSoFromRecord || so.n != 2 || so.move != 120) Fail("t772c: a settle-only ask of an applied entry was not answered from it");
        ShopRec w5 = srv;
        so = ShopSettleOnlyStep(&w5, 1, 1, s1, "barman", 5, 9, 4, 1, 1, 0, 1);
        if (so.v != kSoRefuse) Fail("t772c: a settle-only ask of an entry the stored record has owed was answered applied");
        /* A MARKER AND AN APPLIED ENTRY MEETING, either way round: the trade happened - the applied entry stands, its money in the pot */
        const std::vector<std::string> held(1, "barman");
        ShopRec par; par.generation = 4; { ShopSquad q; q.name = "barman"; q.money = 1000; par.squads.push_back(q); }
        ShopRec onto = par; onto.generation = 5; ShopReqNote(&onto.squads[0], 5, 9, 7, 1, 100); onto.squads[0].money = 1100;
        ShopRec mine = par; ShopReqMarker(&mine.squads[0], 5, 9, 7);
        ShopRec out;
        ShopRebase(onto, mine, par, held, &out);
        int mk = 0;
        if (out.squads[0].money != 1100 || ShopReqState(out.squads[0], 5, 9, 7, &mk) != kRsApplied || ShopRecMarked(out, "barman", 5, 9, 7) != 0)
            Fail("t772c: a marker cancelled an applied entry another holder stored");
        ShopRec o5 = out;
        so = ShopSettleOnlyStep(&o5, 1, 1, onto, "barman", 5, 9, 7, 1, 1, 0, 1);
        if (so.v != kSoFromRecord || so.n != 1 || so.move != 100) Fail("t772c: a settle-only ask after its marker was dropped was not answered from the applied entry");
        o = ShopCancelStep(&o5, 1, 1, onto, "barman", 5, 9, 7, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1);
        if (o.step != kChMarkOwed || o.mark != 1) Fail("t772c: a cancel after its marker was dropped did not mark the applied entry owed");
        /* the owed mark rebased: it goes on top of onto's applied entry with no money taken out (the undo has not run) */
        ShopRec owedOut;
        ShopRebase(onto, o5, out, held, &owedOut);
        if (ShopRecRank(owedOut, "barman", 5, 9, 7) != 1 || owedOut.squads[0].money != 1100) Fail("t772c: an owed mark rebased took the money out before its undo");
        /* THE ASKER: not-holder from slot 2, then the map names slot 3 - sent there; slot 2's late ending is ignored, slot 3's ends it */
        int sentSlot = 2, refusedBy = -1;
        if (CancelAnswerTake(sentSlot, 2, kCaNotHolder) != kTakeKept) Fail("t772c: the asker did not keep a not-holder cancel");
        refusedBy = sentSlot;
        if (CancelSendDue(2, 0, refusedBy, 0) != kCsWait) Fail("t772c: the asker asked the not-holder again at once");
        if (CancelSendDue(3, 0, refusedBy, 0) != kCsSend) Fail("t772c: the asker did not ask the new holder");
        sentSlot = 3;
        if (CancelAnswerTake(sentSlot, 2, kCaUndone) != kTakeIgnored) Fail("t772c: the old holder's ending was taken after the cancel went to the new holder");
        if (CancelAnswerTake(sentSlot, 3, kCaNotApplied) != kTakeEnded) Fail("t772c: the new holder's ending did not end the cancel");
        /* A WAITING TRADE AROUND A SAVE AND AT TEARDOWN: used, soWait, dir (0 buy), held, haveEscrow */
        if (PendSaveHold(1, 1, 0, 1, 0) != kPsPurse || PendSaveHold(1, 1, 1, 0, 1) != kPsEscrow) Fail("t772c: a waiting trade's price or item was not put back for the save");
        if (PendSaveHold(1, 0, 0, 1, 0) != kPsNone || PendSaveHold(0, 1, 0, 1, 0) != kPsNone || PendSaveHold(1, 1, 0, 0, 0) != kPsNone || PendSaveHold(1, 1, 1, 0, 0) != kPsNone)
            Fail("t772c: a save put back what a trade does not hold, or a trade not waiting");
        /* A SAVE THAT DID NOT FINISH, through the save hold's own decision (PendSaveAct): a hold still put back is taken out first -
           by the tick, and by the next save before it puts back - never put back twice */
        {
            int saveHeld = kPsNone;
            if (PendSaveAct(0, saveHeld, 1, 1, 0, 1, 0) != kPaPurse) Fail("t772c: a waiting buy's reservation was not put back for the save");
            saveHeld = kPsPurse;   /* put back; the save throws - no after-save step */
            if (PendSaveAct(1, saveHeld, 1, 1, 0, 1, 0) != kPaTakeOut) Fail("t772c: the tick did not take out a hold left by a save that did not finish");
            if (PendSaveAct(0, saveHeld, 1, 1, 0, 1, 0) != kPaTakeOut) Fail("t772c: the next save put a hold back over one still put back (twice)");
            if (PendSaveAct(0, kPsEscrow, 0, 0, 1, 0, 1) != kPaTakeOut) Fail("t772c: the hold of a row no longer used was not taken out");
            if (PendSaveAct(0, kPsNone, 1, 1, 1, 0, 1) != kPaEscrow || PendSaveAct(1, kPsNone, 1, 1, 0, 1, 0) != kPaNone || PendSaveAct(0, kPsNone, 1, 0, 0, 1, 0) != kPaNone)
                Fail("t772c: the save hold put back at the wrong time or for a trade not waiting");
            int held = 1, drop = 0;
            if (PendSaveTakeEnd(kPsPurse, 0, &held, &drop) != 1 || held != 0 || drop != 0 || (Settle(0, 0, held) & kActRefund) != 0 || (Settle(0, 0, 1) & kActRefund) == 0)
                Fail("t772c: a buy's failed take-out did not end the trade with no second refund");
            int sheld = 0;
            if (PendSaveTakeEnd(kPsEscrow, 0, &sheld, &drop) != 1 || drop != 1) Fail("t772c: a sale's failed take-out kept an escrow inside an inventory");
            if (PendSaveTakeEnd(kPsPurse, 1, &held, &drop) != 0 || PendSaveTakeEnd(kPsNone, 0, &held, &drop) != 0) Fail("t772c: a take-out with nothing to end ended a trade");
        }
        /* AN OWED ENTRY CARRIES WHAT ITS UNDO NEEDS: on the wire, through the rebase, and taken by the step from the stored record */
        {
            ShopUndoDesc u; u.has = 1; u.keeperUid = 77; u.dir = 1; u.base = "sword"; u.company = "co"; u.material = "steel"; u.color = "red";
            u.quality = 0.5f; u.charges = 2.0f; u.functionKind = 3; u.level = 4; u.unique = 1;
            ShopRec wr; wr.generation = 3; wr.writerSlot = 1; wr.key = "Hub bar@3,4@1234,-567,89"; wr.repairMark = kSrNoRepair;
            { ShopSquad q; q.name = "barman"; q.money = 500; ShopReqNote(&q, 5, 9, 11, 1, -40); ShopReqNote(&q, 5, 9, 12, 1, 60); wr.squads.push_back(q); }
            if (ShopReqOwe(&wr.squads[0], 5, 9, 11, &u) != 1 || ShopReqOwe(&wr.squads[0], 5, 9, 11, &u) != 0) Fail("t772c: an owed mark was not written once");
            std::vector<char> b;
            ShopRec back;
            if (!EncodeShopRec(&b, wr) || DecodeShopRec(&b[0], b.size(), &back) != kSrOk) Fail("t772c: a record with an owed entry did not round-trip");
            const ShopReqDone* od = ShopReqFind(back.squads[0], 5, 9, 11);
            if (od == 0 || od->revoked != kRevOwed || od->undo.has != 1 || od->undo.keeperUid != 77 || od->undo.dir != 1 || od->undo.base != "sword"
                || od->undo.color != "red" || od->undo.quality != 0.5f || od->undo.charges != 2.0f || od->undo.level != 4 || od->undo.unique != 1)
                Fail("t772c: an owed entry lost what its undo needs on the wire");
            const ShopReqDone* ad = ShopReqFind(back.squads[0], 5, 9, 12);
            if (ad == 0 || ad->revoked != kRevApplied || ad->undo.has != 0) Fail("t772c: an applied entry gained an undo description on the wire");
            std::vector<char> cut = b; cut.resize(cut.size() - 1);
            if (DecodeShopRec(&cut[0], cut.size(), &back) == kSrOk) Fail("t772c: a cut owed entry decoded");
            /* the rebase carries the owed mark with its description onto another holder's record */
            const std::vector<std::string> held2(1, "barman");
            ShopRec par2 = wr; ShopReqDone* pd2 = ShopReqFindMut(&par2.squads[0], 5, 9, 11); pd2->revoked = kRevApplied; pd2->undo = ShopUndoDesc();
            ShopRec onto2 = par2; onto2.generation = 4;
            ShopRec out2;
            ShopRebase(onto2, wr, par2, held2, &out2);
            const ShopReqDone* rd2 = ShopReqFind(out2.squads[0], 5, 9, 11);
            if (rd2 == 0 || rd2->revoked != kRevOwed || rd2->undo.base != "sword" || rd2->undo.keeperUid != 77) Fail("t772c: the rebase dropped an owed entry's description");
            /* the next holder's copy behind the stored owed mark takes the description with the state */
            ShopRec behind = par2;
            ShopCancelOut oc = ShopCancelStep(&behind, 1, 1, wr, "barman", 5, 9, 11, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1);
            const ShopReqDone* bd = ShopReqFind(behind.squads[0], 5, 9, 11);
            if (oc.step != kChWaitStored || bd == 0 || bd->revoked != kRevOwed || bd->undo.keeperUid != 77) Fail("t772c: a copy took the stored owed state without its description");
        }
    }
    /* the request flagged settle-only: 'SHS1', no price legs, decoded flagged; an unflagged one is not */
    {
        coopshop::ShopReq q; q.has = 1; q.keeperUid = 9; q.homeKey = "Hub bar@3,4@1,2,3"; q.sid = "sid"; q.n = 2; q.unitPrice = 50; q.entry = 0; q.epoch = 77;
        q.stock = kStockHome; q.cult = 1.0f; q.local = 1.0f; q.settleOnly = 1;
        std::vector<char> b;
        if (!EncodeReq(&b, q) || !TagAt(&b[0], b.size(), 0, kReqTagSettle) || ReqTagAt(&b[0], b.size(), 0) == 0) Fail("t772c: a settle-only request did not encode as 'SHS1'");
        coopshop::ShopReq g; size_t at = 0;
        if (!DecodeReq(&b[0], b.size(), &at, &g) || at != b.size() || g.settleOnly != 1 || g.n != 2 || g.epoch != 77 || g.homeKey != q.homeKey || g.cult != 0.0f)
            Fail("t772c: a settle-only request did not round-trip");
        q.settleOnly = 0; b.clear(); at = 0;
        if (!EncodeReq(&b, q) || !TagAt(&b[0], b.size(), 0, kReqTagV4) || !DecodeReq(&b[0], b.size(), &at, &g) || g.settleOnly != 0) Fail("t772c: an ordinary request read as settle-only");
    }
    /* the tail kinds 2 and 3; kind 4 and an answer out of range refused */
    {
        ShopCancel c; c.stock = kStockHome; c.homeKey = "Hub bar@3,4@1,2,3"; c.keeperUid = 9; c.dir = 1; c.n = 3; c.price = 150;
        c.desc.baseSid = "sid"; c.desc.companySid = "co"; c.desc.quality = 0.5f; c.desc.charges = 2.0f; c.desc.functionKind = 4; c.desc.level = 2; c.desc.unique = 1;
        std::vector<char> w(4, (char)0);
        if (!EncodeCancelTail(&w, 77, c)) Fail("t772c: a cancel did not encode");
        ShopTail t; size_t at = 4;
        if (!TagAt(&w[0], w.size(), 4, kTailTag) || !DecodeTail(&w[0], w.size(), &at, &t) || at != w.size() || t.kind != kTailCancel || t.epoch != 77
            || t.cancel.homeKey != c.homeKey || t.cancel.n != 3 || t.cancel.price != 150 || t.cancel.dir != 1 || t.cancel.keeperUid != 9 || SameDesc(t.cancel.desc, c.desc) != 1)
            Fail("t772c: the cancel did not round-trip");
        at = 4; if (DecodeTail(&w[0], w.size() - 1, &at, &t)) Fail("t772c: a cut cancel decoded");
        ShopCancel bad = c; bad.homeKey = ""; std::vector<char> wb;
        if (EncodeCancelTail(&wb, 77, bad) || !wb.empty() || EncodeCancelTail(&wb, 0, c)) Fail("t772c: a home cancel with no key, or epoch 0, encoded");
        std::vector<char> a3;
        if (!EncodeCancelAnswerTail(&a3, 77, kCaWriteRefused)) Fail("t772c: an answer did not encode");
        ShopTail t3; at = 0;
        if (!DecodeTail(&a3[0], a3.size(), &at, &t3) || at != a3.size() || t3.kind != kTailCancelAnswer || t3.answer != kCaWriteRefused || t3.epoch != 77)
            Fail("t772c: the answer did not round-trip");
        std::vector<char> a4 = a3; a4[a4.size() - 1] = (char)(kCaMax + 1); at = 0;
        if (DecodeTail(&a4[0], a4.size(), &at, &t3) || EncodeCancelAnswerTail(&a4, 77, kCaMax + 1)) Fail("t772c: an answer out of range went through");
        std::vector<char> k4; EncodeTail(&k4, 77, 4); k4.push_back((char)0); at = 0;
        if (DecodeTail(&k4[0], k4.size(), &at, &t3)) Fail("t772c: a shop tail of kind 4 decoded");
    }
    /* the write an echo answers: the oldest write in flight sent with its stamp */
    {
        std::vector<ShopFlight> fl(3);
        fl[0].gen = 5; fl[0].stamp = 100; fl[1].gen = 6; fl[1].stamp = 100; fl[2].gen = 7; fl[2].stamp = 101;
        if (ShopFlightFor(fl, 100) != 0 || ShopFlightFor(fl, 101) != 2 || ShopFlightFor(fl, 99) != -1) Fail("t772c: an echo answered the wrong write");
    }
}
/* AN OWED UNDO'S GOODS LIVE IN BOX RECORDS: a shop counter's record lists the undos its contents hold, so a claimer that loads an older
   save (its counters' stock comes from those records) never applies an undo twice; the undone mark carries the undo's money in the same
   record; a claim whose claimer has gone is re-claimed and applied only into goods that lack it; the rebase holds the owed cap. */
void t_t772c_goods_undo()
{
    using namespace shoprec;
    using namespace coopbrec;
    const std::string ck = "12-counter.mod@3,4@1003,-205,100";
    std::vector<cooppar::ParityItem> items;
    items.push_back(T823BoxItem("katana", 0, 0, 1));
    /* THE UNDO LIST: a counter's record with none is byte for byte the form without it; with one, that form plus the flag and the list */
    BoxHeader h; h.flags = 0; h.generation = 5; h.writerSlot = 1; h.kind = kBrKindShop; h.ownerSlot = -1; h.key = ck;
    std::vector<char> plain, listed;
    if (!EncodeBoxRecord(&plain, h, items)) { Fail("t772c goods: a counter's record was not encoded"); return; }
    BoxHeader hl = h;
    BoxUndoKey k1; k1.asker = 5; k1.epoch = 9; k1.id = 4;
    hl.undos.push_back(k1);
    if (!EncodeBoxRecord(&listed, hl, items)) { Fail("t772c goods: a counter's record with an undo list was not encoded"); return; }
    const size_t fo = (size_t)kBrFixedBytes + ck.size();   /* the flags field */
    {
        std::vector<char> want = plain;
        const unsigned int f = kBrFlagUndos; std::memcpy(&want[fo], &f, 4);
        BrPutU32(&want, 1u); BrPutU32(&want, 5u); BrPutU32(&want, 9u); BrPutU32(&want, 4u);
        if (listed != want) Fail("t772c goods: a record with an undo list is not the form without it plus its flag and the list");
    }
    BoxHeader back; std::vector<cooppar::ParityItem> bi;
    if (DecodeBoxRecord(&plain[0], plain.size(), &back, &bi) != kBrOk || !back.undos.empty() || back.flags != 0 || bi.size() != 1)
        Fail("t772c goods: a record with no undo list did not read as before");
    if (DecodeBoxRecord(&listed[0], listed.size(), &back, &bi) != kBrOk || back.undos.size() != 1 || BoxUndoHas(back.undos, 5, 9, 4) != 1
        || (back.flags & kBrFlagUndos) == 0 || bi.size() != 1)
        Fail("t772c goods: an undo list did not read back");
    {   /* refused: cut short, a byte after the list, empty, a key twice, over the cap, on a record that is not a shop's */
        for (size_t n = plain.size(); n < listed.size(); ++n) if (DecodeBoxRecord(&listed[0], n, 0, 0) == kBrOk) { Fail("t772c goods: a cut undo list was read"); break; }
        std::vector<char> t = listed; t.push_back(0);
        if (DecodeBoxRecord(&t[0], t.size(), 0, 0) != kBrTrailing) Fail("t772c goods: a byte after the undo list was read");
        const unsigned int f = kBrFlagUndos;
        std::vector<char> e = plain; std::memcpy(&e[fo], &f, 4); BrPutU32(&e, 0u);
        if (DecodeBoxRecord(&e[0], e.size(), 0, 0) != kBrBadUndos) Fail("t772c goods: an empty undo list was read");
        std::vector<char> tw = plain; std::memcpy(&tw[fo], &f, 4); BrPutU32(&tw, 2u);
        for (int r = 0; r < 2; ++r) { BrPutU32(&tw, 5u); BrPutU32(&tw, 9u); BrPutU32(&tw, 4u); }
        if (DecodeBoxRecord(&tw[0], tw.size(), 0, 0) != kBrBadUndos) Fail("t772c goods: an undo list naming a key twice was read");
        std::vector<char> ov = plain; std::memcpy(&ov[fo], &f, 4); BrPutU32(&ov, kBrUndoMax + 1u);
        for (unsigned int r = 0; r <= kBrUndoMax; ++r) { BrPutU32(&ov, 5u); BrPutU32(&ov, 9u); BrPutU32(&ov, r); }
        if (DecodeBoxRecord(&ov[0], ov.size(), 0, 0) != kBrBadUndos) Fail("t772c goods: an undo list over its cap was read");
        std::vector<char> bx = listed; bx[16] = (char)kBrKindBox;
        if (DecodeBoxRecord(&bx[0], bx.size(), 0, 0) != kBrBadHeader) Fail("t772c goods: an undo list on a storage box's record was read");
        std::vector<char> out;
        BoxHeader d2 = hl; d2.undos.push_back(k1);
        if (EncodeBoxRecord(&out, d2, items)) Fail("t772c goods: an undo list naming a key twice was written");
        BoxHeader d3 = h;
        for (unsigned int r = 0; r <= kBrUndoMax; ++r) { BoxUndoKey x; x.asker = 5; x.epoch = 9; x.id = r; d3.undos.push_back(x); }
        if (EncodeBoxRecord(&out, d3, items)) Fail("t772c goods: an undo list over its cap was written");
        BoxHeader d4 = hl; d4.kind = kBrKindBox;
        if (EncodeBoxRecord(&out, d4, items)) Fail("t772c goods: a storage box's record carried an undo list");
        BoxHeader d5 = h; d5.flags = kBrFlagUndos;   /* the flag follows the list: none, no flag */
        if (!EncodeBoxRecord(&out, d5, items) || out != plain) Fail("t772c goods: a flag with no list was written");
        const std::vector<int> loaded(1, 1);
        if (BoxJudgeWrite(MakeBoxRecId(ck), &listed[0], listed.size(), 1, 1, loaded, 1, 4u, -1, kBrKindShop, 0, 0) != kBrAccept)
            Fail("t772c goods: the world server refused a counter's record with an undo list");
    }
    {   /* THE CARRY: the kept record's keys, then this game's own, less the ones the shop record has done; past the cap the oldest go */
        std::vector<BoxUndoKey> kept, local, done, out;
        unsigned int dropped = 9;
        BoxUndoKey a; a.asker = 1; a.epoch = 1; a.id = 1;
        BoxUndoKey b = a; b.id = 2;
        BoxUndoKey c = a; c.id = 3;
        kept.push_back(a); kept.push_back(b); local.push_back(b); local.push_back(c); done.push_back(a);
        BoxUndoCarry(kept, local, done, &out, &dropped);
        if (out.size() != 2 || out[0].id != 2 || out[1].id != 3 || dropped != 0) Fail("t772c goods: the undo list carried is not the kept and own keys less the done ones");
        kept.clear(); done.clear(); local.clear();
        for (unsigned int r = 0; r < kBrUndoMax + 3u; ++r) { BoxUndoKey x = a; x.id = 100u + r; local.push_back(x); }
        BoxUndoCarry(kept, local, done, &out, &dropped);
        if (out.size() != kBrUndoMax || dropped != 3u || out[0].id != 103u) Fail("t772c goods: an undo list past its cap did not drop the oldest");
    }
    /* A CLAIMER THAT LOADS AN OLDER SAVE: A (slot 1) claimed the owed undo of trade {5, 9, 4}, applied it into counter 1 and wrote counter
       1's record with the key (`listed`); its undone write was lost; it quit and loaded an older save - its undo memory is empty and its
       counters' stock comes from their records. The record holds the undo: A writes undone only, it never applies it again. */
    {
        std::vector<ShopUndoPiece> p(2);
        for (size_t i = 0; i < p.size(); ++i) { p[i].localHas = 0; p[i].haveRecord = 1; p[i].recordHas = 0; p[i].applyDone = 1; p[i].ownSeed = 0; }
        BoxHeader rd;
        if (DecodeBoxRecord(&listed[0], listed.size(), &rd, 0) != kBrOk) Fail("t772c goods: counter 1's record did not read");
        p[0].recordHas = BoxUndoHas(rd.undos, 5, 9, 4);
        std::vector<ShopReqKey> fresh;   /* the older save's world: no undo ran in it */
        int g = ShopUndoGoods(p);
        if (g != kUgIn || ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, ShopKeyHas(fresh, 5, 9, 4), g) != kOwMarkUndone)
            Fail("t772c goods: a claimer that loaded an older save applied an undo its counter's record holds a second time");
        p[0].recordHas = 0;   /* the box write was lost too: the goods lack it - applied once */
        g = ShopUndoGoods(p);
        if (g != kUgOut || ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, 0, g) != kOwApply) Fail("t772c goods: goods without the undo did not take it");
        p[1].applyDone = 0;   /* a counter's record not put in yet at this load */
        g = ShopUndoGoods(p);
        if (g != kUgWait || ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, 0, g) != kOwWait) Fail("t772c goods: an undo was applied with a counter's record not put in");
        p[1].applyDone = 1; p[1].haveRecord = 0;
        if (ShopUndoGoods(p) != kUgWait) Fail("t772c goods: an undo was applied into a counter with no record");
        p[1].haveRecord = 1; p[1].ownSeed = 1;
        if (ShopUndoGoods(p) != kUgWait) Fail("t772c goods: an undo was decided on this game's own unsettled first write");
        p[1].ownSeed = 0; p[1].localHas = 1;   /* this game's own change, not stored yet */
        if (ShopUndoGoods(p) != kUgWait) Fail("t772c goods: this game's own unstored change on a counter was decided");
        const std::vector<ShopUndoPiece> none;
        if (ShopUndoGoods(none) != kUgNever) Fail("t772c goods: a shop with no counter did not end the cancel");
    }
    const std::vector<std::string> held(1, "barman");
    ShopRec claimed; claimed.generation = 9; claimed.key = "Hub bar@3,4@1234,-567,89"; claimed.repairMark = kSrNoRepair;
    { ShopSquad q; q.name = "barman"; q.money = 1120; ShopReqNote(&q, 5, 9, 4, 2, 120); claimed.squads.push_back(q); }
    ShopReqOwe(&claimed.squads[0], 5, 9, 4);
    ShopReqClaim(&claimed.squads[0], 5, 9, 4, 1);
    /* THE UNDONE MARK CARRIES THE UNDO'S MONEY in the same record: a lost write lost both, the mark written again takes it once; a
       rebase onto a record already marked undone takes it once */
    {
        ShopRec mk = claimed; int mv = 0;
        if (ShopReqUndo(&mk.squads[0], 5, 9, 4, &mv) != 1 || mv != 120 || mk.squads[0].money != 1000 || ShopRecRank(mk, "barman", 5, 9, 4) != 3)
            Fail("t772c goods: the undone mark did not take the undo's money out of the record with it");
        if (ShopReqUndo(&mk.squads[0], 5, 9, 4, &mv) != 0 || mk.squads[0].money != 1000) Fail("t772c goods: a second undone mark took the money again");
        ShopRec again = claimed;   /* the write lost: the stored record still has the claim and the money */
        if (ShopReqUndo(&again.squads[0], 5, 9, 4, &mv) != 1 || again.squads[0].money != 1000) Fail("t772c goods: the mark written again did not take the money once");
        ShopRec onto = mk; onto.generation = 10;
        ShopRec out;
        ShopRebase(onto, mk, claimed, held, &out);
        if (out.squads[0].money != 1000 || ShopRecRank(out, "barman", 5, 9, 4) != 3) Fail("t772c goods: a rebased undone mark took the undo's money twice");
        ShopRec onto2 = claimed; onto2.generation = 10; onto2.squads[0].money = 1150;   /* another trade moved the money meanwhile */
        ShopRebase(onto2, mk, claimed, held, &out);
        if (out.squads[0].money != 1030 || ShopRecRank(out, "barman", 5, 9, 4) != 3) Fail("t772c goods: a rebased undone mark did not take its money once over another trade");
    }
    /* A CLAIM WHOSE CLAIMER HAS GONE: A (slot 1) claimed and left before its undone mark; B (slot 2) is the exact holder. B waits while it
       has watched A's claim less than kReclaimMs, then re-claims; once its claim is stored B applies only into goods that lack the undo;
       A, back as the holder, waits on B's claim. */
    {
        ShopClaimWatch cw;
        if (ShopClaimGoneMs(&cw, 1, 1, 1000u) != 0 || ShopClaimGoneMs(&cw, 1, 1, 1000u + kReclaimMs - 1u) != kReclaimMs - 1u) Fail("t772c goods: the claim watch did not count");
        if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 2, 1, 1, 0, kUgOut, kReclaimMs - 1u) != kOwWait)
            Fail("t772c goods: a claim was re-claimed before its claimer had been away kReclaimMs");
        const unsigned int gone = ShopClaimGoneMs(&cw, 1, 1, 1000u + kReclaimMs);
        if (gone != kReclaimMs || ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 2, 1, 1, 0, kUgOut, gone) != kOwReclaim)
            Fail("t772c goods: a claim whose claimer had gone was not re-claimed (it would wait for ever and hold one of the owed places)");
        if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 2, 0, 1, 0, kUgOut, gone) != kOwWait) Fail("t772c goods: a claim was re-claimed off the exact holder");
        if (ShopOwedDue((int)kRevClaimed, 2, (int)kRevClaimed, 1, 2, 1, 1, 0, kUgOut, gone) != kOwWait) Fail("t772c goods: a re-claim in flight was written again");
        if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 2, 1, 0, 0, kUgOut, gone) != kOwWait) Fail("t772c goods: a claim was re-claimed with nothing to undo from");
        if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 2, 1, 0, 0, kUgIn, gone) != kOwReclaim) Fail("t772c goods: a claim whose goods hold the undo was not re-claimed");
        if (ShopClaimGoneMs(&cw, 0, 1, 1000u + kReclaimMs + 5u) != 0 || ShopClaimGoneMs(&cw, 1, 1, 1000u + kReclaimMs + 6u) != 0
            || ShopClaimGoneMs(&cw, 1, 1, 1000u + kReclaimMs + 16u) != 10u)
            Fail("t772c goods: the claim watch did not start again after this game stopped being the exact holder");
        if (ShopClaimGoneMs(&cw, 1, 3, 1000u + 3u * kReclaimMs) != 0) Fail("t772c goods: the claim watch carried over to another claimer");
        ShopRec b = claimed;
        if (ShopReqReclaim(&b.squads[0], 5, 9, 4, 3, 2) != 0 || ShopReqReclaim(&b.squads[0], 5, 9, 4, 1, 1) != 0) Fail("t772c goods: a re-claim not over the named claimer's claim was written");
        const ShopReqDone* bd = ShopReqFind(b.squads[0], 5, 9, 4);
        if (ShopReqReclaim(&b.squads[0], 5, 9, 4, 1, 2) != 1 || bd == 0 || bd->claimer != 2 || bd->revoked != kRevClaimed) Fail("t772c goods: the re-claim was not written");
        ShopRec ow = claimed; ShopReqFindMut(&ow.squads[0], 5, 9, 4)->revoked = kRevOwed;
        if (ShopReqReclaim(&ow.squads[0], 5, 9, 4, 1, 2) != 0) Fail("t772c goods: an owed entry was re-claimed (it is claimed)");
        if (ShopOwedDue((int)kRevClaimed, 2, (int)kRevClaimed, 2, 2, 1, 1, 0, kUgIn) != kOwMarkUndone) Fail("t772c goods: the re-claimer applied an undo the old claimer's counter record holds");
        if (ShopOwedDue((int)kRevClaimed, 2, (int)kRevClaimed, 2, 2, 1, 1, 0, kUgOut) != kOwApply) Fail("t772c goods: the re-claimer did not apply an undo the goods lack");
        if (ShopOwedDue((int)kRevClaimed, 2, (int)kRevClaimed, 2, 2, 1, 1, 0, kUgWait) != kOwWait) Fail("t772c goods: the re-claimer applied with the goods not decided");
        if (ShopOwedDue((int)kRevClaimed, 2, (int)kRevClaimed, 2, 1, 1, 1, 1, kUgIn, 0) != kOwWait) Fail("t772c goods: the returning old claimer acted on another game's claim");
        /* B's re-claim refused (A's undone mark landed first): A's mark and money stand */
        ShopRec aDone = claimed; int mv = 0; ShopReqUndo(&aDone.squads[0], 5, 9, 4, &mv); aDone.generation = 10;
        ShopRec out;
        ShopRebase(aDone, b, claimed, held, &out);
        if (ShopRecRank(out, "barman", 5, 9, 4) != 3 || out.squads[0].money != 1000) Fail("t772c goods: a refused re-claim went over the stored undone mark");
        /* A's undone mark refused (B's re-claim stored first): B's claim stands, and the money A's mark took goes back with it */
        ShopRec bStored = b; bStored.generation = 10;
        ShopRebase(bStored, aDone, claimed, held, &out);
        const ShopReqDone* od = ShopReqFind(out.squads[0], 5, 9, 4);
        if (od == 0 || od->revoked != kRevClaimed || od->claimer != 2) Fail("t772c goods: the old claimer's undone mark went over the re-claim");
        if (out.squads[0].money != 1120) Fail("t772c goods: the old claimer's undone mark left its money behind without the mark (the re-claimer would take it again)");
    }
    /* THE REBASE HOLDS THE OWED CAP: this game's owed entries past kSrOwedCap stay applied (their cancels ask again), so a merged record
       never carries more undo descriptions than one record holds */
    {
        ShopRec par; par.generation = 4; par.key = "Hub bar@3,4@1234,-567,89"; par.repairMark = kSrNoRepair;
        for (unsigned int s = 0; s < 6; ++s)
        {
            ShopSquad q; q.name = (s == 0) ? std::string("barman") : "keeper" + std::string(1, (char)('a' + s)); q.money = 1000;
            for (unsigned int a = 0; a < kSrMaxReqs; ++a) for (unsigned int x = 0; x < kSrReqKeep; ++x) ShopReqNote(&q, 10u + a, 1u, 1u + x, 1, 0);
            par.squads.push_back(q);
        }
        ShopRec onto = par; onto.generation = 5;
        ShopRec work = par;
        ShopUndoDesc u; u.has = 1; u.base = "katana"; u.keeperUid = 7;
        unsigned int nOnto = 0, nWork = 0;
        for (size_t s = 0; s < par.squads.size(); ++s)
            for (unsigned int a = 0; a < kSrMaxReqs; ++a)
                for (unsigned int x = 0; x < kSrReqKeep; ++x)
                {
                    if (s < 3 && nOnto < 30u && x < 2u) { ShopReqOwe(&onto.squads[s], 10u + a, 1u, 1u + x, &u); ++nOnto; }
                    else if (s >= 3 && nWork < 10u && x < 2u) { ShopReqOwe(&work.squads[s], 10u + a, 1u, 1u + x, &u); ++nWork; }
                }
        if (ShopRecOwedCount(onto) != 30u || ShopRecOwedCount(work) != 10u) Fail("t772c goods: the cap case was not built");
        std::vector<std::string> all;
        for (size_t s = 0; s < par.squads.size(); ++s) all.push_back(par.squads[s].name);
        ShopRec out; int capHeld = 0;
        ShopRebase(onto, work, par, all, &out, &capHeld);
        std::vector<char> ob;
        if (ShopRecOwedCount(out) != kSrOwedCap || capHeld != 8) Fail("t772c goods: the rebase carried owed entries past the cap");
        if (!EncodeShopRec(&ob, out)) Fail("t772c goods: the rebased record at the owed cap does not encode");
    }
}
/* AN APPLIED OWED UNDO IS WRITTEN UNDONE (WITH ITS MONEY) ONLY ONCE THE GOODS ARE DURABLY UNDONE: every counter it changed has its
   record stored with the undo's key; a change lost before that leaves the claim open and the undo goes again, once. A counter that can
   never hold the undo in a record ends the cancel as refused - the trade stands, the asker is answered refused, the owed place is free. */
void t_t772c_durable_undo()
{
    using namespace shoprec;
    using namespace coopshop;
    ShopRec claimed; claimed.generation = 9; claimed.key = "Hub bar@3,4@1234,-567,89"; claimed.repairMark = kSrNoRepair;
    { ShopSquad q; q.name = "barman"; q.money = 1120; ShopReqNote(&q, 5, 9, 4, 2, 120); claimed.squads.push_back(q); }
    ShopReqOwe(&claimed.squads[0], 5, 9, 4);
    ShopReqClaim(&claimed.squads[0], 5, 9, 4, 1);
    /* A (slot 1) applied the undo into one counter; that counter's write carrying the key is not stored yet: no undone mark, no money */
    {
        std::vector<ShopUndoTouch> t(1); t[0].stored = 0; t[0].inFlight = 1;
        int partial = 9;
        int g = ShopUndoDurable(t, 0, &partial);
        if (g != kUgWait || partial != 0 || ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, 0, g) != kOwWait)
            Fail("t772c durable: the undone mark (and its money) went out while the counter's write carrying the undo was not stored");
        /* the counter's write is lost (its area left, the game quit, the holder moved): nothing stored, nothing in flight. The claim is
           still open with the money in the record; the counters' stored records lack the key - the undo goes again, once */
        t[0].inFlight = 0;
        if (ShopUndoDurable(t, 0, &partial) != kUgOut) Fail("t772c durable: a counter change lost before its record stored it was not taken as lost");
        if (ShopRecRank(claimed, "barman", 5, 9, 4) != 2 || claimed.squads[0].money != 1120) Fail("t772c durable: the open claim is not the stored record's");
        std::vector<ShopUndoPiece> p(1); p[0].haveRecord = 1; p[0].applyDone = 1;
        const int gr = ShopUndoGoods(p);
        if (gr != kUgOut || ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, 0, gr) != kOwApply)
            Fail("t772c durable: the claimer did not apply again an undo whose counter change was lost");
        if (ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 2, 1, 1, 0, gr, kReclaimMs) != kOwReclaim
            || ShopOwedDue((int)kRevClaimed, 2, (int)kRevClaimed, 2, 2, 1, 1, 0, gr) != kOwApply)
            Fail("t772c durable: a re-claimer did not apply an undo whose first apply was lost");
        /* B's counter write is stored (its echo): now the undone mark and the money, once */
        t[0].stored = 1;
        g = ShopUndoDurable(t, 0, &partial);
        if (g != kUgIn || ShopOwedDue((int)kRevClaimed, 2, (int)kRevClaimed, 2, 2, 1, 1, 0, g) != kOwMarkUndone)
            Fail("t772c durable: a stored counter write did not let the undone mark go");
        ShopRec b = claimed; int mv = 0;
        if (ShopReqReclaim(&b.squads[0], 5, 9, 4, 1, 2) != 1 || ShopReqUndo(&b.squads[0], 5, 9, 4, &mv) != 1 || b.squads[0].money != 1000)
            Fail("t772c durable: the money did not go once with the undone mark");
        if (ShopUndoDurable(t, 1, &partial) != kUgWait) Fail("t772c durable: the undone mark went with the undo's units still kept, not filed");
        const std::vector<ShopUndoTouch> none;
        if (ShopUndoDurable(none, 0, &partial) != kUgIn) Fail("t772c durable: an undo that changed no counter waited");
        std::vector<ShopUndoTouch> two(2); two[0].stored = 1; two[0].inFlight = 0; two[1].stored = 0; two[1].inFlight = 0;
        if (ShopUndoDurable(two, 0, &partial) != kUgIn || partial != 1) Fail("t772c durable: a part stored and a part lost was applied again over the stored part");
        two[1].inFlight = 1;
        if (ShopUndoDurable(two, 0, &partial) != kUgWait || partial != 0) Fail("t772c durable: the mark went with a counter's write still in flight");
        std::vector<ShopUndoPiece> q(1); q[0].haveRecord = 1; q[0].applyDone = 1; q[0].localHas = 1;
        if (ShopUndoGoods(q) != kUgWait) Fail("t772c durable: this game's own unstored change was read as held");
    }
    /* EVERY COUNTER STATE ENDS: a record that failed to go in is not decided; a counter that can never hold the undo ends the cancel */
    {
        std::vector<ShopUndoPiece> p(2);
        for (size_t i = 0; i < p.size(); ++i) { p[i].haveRecord = 1; p[i].applyDone = 1; }
        p[1].applyDone = 0;   /* its record was refused at this load: the older save's stock stands, without the undo */
        if (ShopUndoGoods(p) != kUgWait) Fail("t772c durable: a counter whose record failed to go in was decided");
        p[1].never = 1;
        int g = ShopUndoGoods(p);
        if (g != kUgNever || ShopOwedDue((int)kRevClaimed, 1, (int)kRevClaimed, 1, 1, 1, 1, 0, g) != kOwRefuse)
            Fail("t772c durable: a counter that can never hold the undo did not end the cancel (it would hold an owed place for ever)");
        p[0].recordHas = 1;
        if (ShopUndoGoods(p) != kUgIn) Fail("t772c durable: a stored record holding the undo did not decide it");
        const std::vector<ShopUndoPiece> none;
        if (ShopUndoGoods(none) != kUgNever) Fail("t772c durable: a shop with no counter did not end the cancel");
        ShopRec r = claimed;
        const unsigned int before = ShopRecOwedCount(r);
        if (ShopReqRefuse(&r.squads[0], 5, 9, 4) != 1 || ShopRecOwedCount(r) != before - 1u || r.squads[0].money != 1120 || ShopRecMarked(r, "barman", 5, 9, 4) != 0)
            Fail("t772c durable: a refused cancel did not free its owed place, or moved money, or reads as cancelled");
        int mk = 0;
        if (ShopReqRefuse(&r.squads[0], 5, 9, 4) != 0 || ShopReqOwe(&r.squads[0], 5, 9, 4) != 0 || ShopReqState(r.squads[0], 5, 9, 4, &mk) != kRsCancelRefused)
            Fail("t772c durable: a refused cancel was marked again");
        if (ShopOwedDue((int)kRevRefused, 1, (int)kRevClaimed, 1, 1, 1, 1, 0, kUgNever) != kOwWait) Fail("t772c durable: a refusal in flight was written again");
        std::vector<char> bytes; ShopRec back;
        if (!EncodeShopRec(&bytes, r) || DecodeShopRec(&bytes[0], bytes.size(), &back) != kSrOk || ShopReqFind(back.squads[0], 5, 9, 4) == 0
            || ShopReqFind(back.squads[0], 5, 9, 4)->revoked != kRevRefused)
            Fail("t772c durable: a refused cancel did not round-trip");
        ShopRec st = r; st.generation = 10;
        ShopRec w1 = r;
        ShopCancelOut o = ShopCancelStep(&w1, 1, 1, st, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, 0, 1, 2);
        if (o.step != kChAnswerNow || o.answer != kCaRefused || o.undo != 0 || o.mark != 0 || CancelAnswerTake(3, 3, o.answer) != kTakeEnded)
            Fail("t772c durable: the asker of a refused cancel was not answered refused (ending it)");
        ShopRec w2 = r;
        o = ShopCancelStep(&w2, 1, 1, claimed, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, 0, 1, 2);
        if (o.step != kChWaitStored || o.answer != kCaRefused) Fail("t772c durable: a refusal in this game's copy only was answered");
        ShopRec w3 = claimed;
        o = ShopCancelStep(&w3, 1, 1, st, "barman", 5, 9, 4, 0, 0, 1, 1, 0, 1, 0, 0, 1, 2);
        if (o.step != kChAnswerNow || o.answer != kCaRefused) Fail("t772c durable: a stored refusal was not taken into the copy");
        ShopRec w4 = r;
        ShopSettleOut so = ShopSettleOnlyStep(&w4, 1, 1, st, "barman", 5, 9, 4, 1, 1, 0, 1);
        if (so.v != kSoFromRecord || so.n != 2 || so.move != 120) Fail("t772c durable: a settle-only ask of a refused cancel was not answered from the standing trade");
    }
}
/* A forced restock is refused only when the counters hold the world server's stock applied at this load and the call is neither the
   mod's own test command nor a wandering trader's; the timed restock always runs. One counter holds that stock only with a shop
   record that has items and was applied at this load. */
void t_t1018_setup_restock()
{
    using namespace shopsetup;
    if (SetupRestockDecide(0, 0, 0, 1) != kSetupRestockRun) Fail("T-1018: the timed restock (force 0) did not run over stored stock");
    if (SetupRestockDecide(0, 0, 0, 0) != kSetupRestockRun) Fail("T-1018: the timed restock (force 0) did not run");
    if (SetupRestockDecide(1, 0, 0, 1) != kSetupRestockRefuse) Fail("T-1018: a forced restock over stored stock was not refused");
    if (SetupRestockDecide(2, 0, 0, 1) != kSetupRestockRefuse) Fail("T-1018: a non-zero force other than 1 did not count as forced");
    if (SetupRestockDecide(1, 0, 0, 0) != kSetupRestockRun) Fail("T-1018: a forced restock with no stored stock (no records, empty, unread) did not run");
    if (SetupRestockDecide(1, 1, 0, 1) != kSetupRestockRun) Fail("T-1018: the mod's own test command's restock was refused");
    if (SetupRestockDecide(1, 0, 1, 1) != kSetupRestockRun) Fail("T-1018: a wandering trader's forced restock was refused");
    if (SetupPieceHeld(1, 1, 3, coopbrec::kBasDone) != 1) Fail("T-1018: an applied shop record with items did not hold");
    if (SetupPieceHeld(1, 1, 3, coopbrec::kBasNotWriter) != 1) Fail("T-1018: a shop record another game writes did not hold");
    if (SetupPieceHeld(1, 1, 3, coopbrec::kBasRefused) != 1) Fail("T-1018: a refused shop record (this game's box stands) did not hold");
    if (SetupPieceHeld(1, 1, 3, coopbrec::kBasNone) != 0) Fail("T-1018: a shop record not decided at this load held");
    if (SetupPieceHeld(1, 1, 3, coopbrec::kBasWait) != 0) Fail("T-1018: a shop record waiting at this load held");
    if (SetupPieceHeld(1, 1, 3, coopbrec::kBasNoBox) != 0) Fail("T-1018: a shop record whose box is not here held");
    if (SetupPieceHeld(1, 1, 3, 5) != 0 || SetupPieceHeld(1, 1, 3, -1) != 0) Fail("T-1018: an unknown apply state held");
    if (SetupPieceHeld(0, 1, 3, coopbrec::kBasDone) != 0) Fail("T-1018: a counter with no record held");
    if (SetupPieceHeld(1, 0, 3, coopbrec::kBasDone) != 0) Fail("T-1018: a plain box record held");
    if (SetupPieceHeld(1, 1, 0, coopbrec::kBasDone) != 0) Fail("T-1018: an empty shop record held");
    if (SetupPieceHeld(1, 1, -1, coopbrec::kBasDone) != 0) Fail("T-1018: a negative item count held");
    if (RestockDeadlineAsEngine(38.5, 24.0f, 24000.0f) != 62.5) Fail("T-1018: the deadline is not now + the squad's refresh time");
    if (RestockDeadlineAsEngine(38.5, 12.0f, 24000.0f) != 50.5) Fail("T-1018: a squad's own 12 is not used");
    if (RestockDeadlineAsEngine(38.5, 0.0f, 24000.0f) != 24038.5) Fail("T-1018: a missing (0) field does not take the default");
    if (RestockDeadlineAsEngine(38.5, -3.0f, 24000.0f) != 24038.5) Fail("T-1018: a negative field does not take the default");
}
/* A keeper whose money is not known: the engine's restock is asked when the counters do not hold the stored stock; when they do, the money from
   the squad's record if it carries it, else left not known - never a restock. */
void t_t1018_keeper_unknown_money()
{
    using namespace shopsetup;
    if (KeeperUnknownMoneyRoad(0, 0) != kKeeperMoneyRestock) Fail("T-1018: no stored stock, no record money: not the restock");
    if (KeeperUnknownMoneyRoad(0, 1) != kKeeperMoneyRestock) Fail("T-1018: no stored stock, record money: not the restock");
    if (KeeperUnknownMoneyRoad(1, 1) != kKeeperMoneyFromRecord) Fail("T-1018: stored stock, record money: not taken from the record");
    if (KeeperUnknownMoneyRoad(1, 0) != kKeeperMoneyLeftUnknown) Fail("T-1018: stored stock, no record money: not left unknown");
}
/* T-986: a copy in a native combat window past the far snap's distance from its owner closes the window; at or inside it, with no
   window, an unread distance or no bound, it does not */
void t_t986_window_drift_bound()
{
    using namespace winbound;
    if (DriftClosesWindow(true, 250.0f, 250.0f)) Fail("T-986: a copy exactly at the bound closed its window");
    if (!DriftClosesWindow(true, 250.5f, 250.0f) || !DriftClosesWindow(true, 6700.0f, 250.0f)) Fail("T-986: a copy past the bound kept its window (T1425: 5,000-6,700)");
    if (DriftClosesWindow(true, 0.0f, 250.0f) || DriftClosesWindow(true, 249.9f, 250.0f)) Fail("T-986: a copy inside the bound closed its window");
    if (DriftClosesWindow(false, 6700.0f, 250.0f)) Fail("T-986: a copy with no window was closed");
    volatile float zero = 0.0f;   /* a NaN and an infinity made at run time, not folded by the compiler */
    if (DriftClosesWindow(true, zero / zero, 250.0f) || DriftClosesWindow(true, 1.0f / zero, 250.0f)) Fail("T-986: an unread distance closed a window");
    if (DriftClosesWindow(true, 6700.0f, 0.0f) || DriftClosesWindow(true, 6700.0f, -1.0f)) Fail("T-986: a bound of 0 or below closed a window");
}
/* T-986 fold 1: only the owner's OFF edge leaves combat mode and ends the action; a drift close keeps both; the reasons */
void t_t986f1_close_kind()
{
    using namespace winbound;
    if (!CloseLeavesCombat(kCloseOwnerOff) || !CloseEndsAction(kCloseOwnerOff)) Fail("T-986 fold 1: the owner's OFF edge kept combat mode or the action");
    if (CloseLeavesCombat(kCloseDrift)) Fail("T-986 fold 1: a drift close left combat mode while the owner fights");
    if (CloseEndsAction(kCloseDrift)) Fail("T-986 fold 1: a drift close ended the action (the F408 race)");
    if (CloseLeavesCombat(7) || CloseEndsAction(7)) Fail("T-986 fold 1: an unknown kind undid something");
    if (std::string(CloseReason(kCloseDrift)) != "drift" || std::string(CloseReason(kCloseOwnerOff)) != "authority_off") Fail("T-986 fold 1: the close reasons");
}
/* T-937: a kept outpost's town - its pieces' own links first (the majority, a tie to the row's handle, then the nearer), then the
   town held, then a town within the radius; found only when none holds and the links were all read; which pieces may be relinked;
   which duplicate is kept and when it may be removed */
void t_t937_kept_town_choice()
{
    townkeep::TownRow row;
    row.owner = 0; row.pos[0] = 1000.0f; row.pos[2] = 2000.0f;
    const int rh[5] = { 13, 271, 5, 7, 9 };
    for (int j = 0; j < 5; ++j) row.hand[j] = rh[j];
    std::vector<townkeep::TownCand> c(3);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 5; ++j) c[(size_t)i].hand[j] = 100 + i * 10 + j;
    for (int j = 0; j < 5; ++j) c[2].hand[j] = rh[j];   /* c2 carries the row's handle */
    c[0].pos[0] = 1030.0f; c[0].pos[2] = 2000.0f;   /* 30 off the kept spot */
    c[1].pos[0] = 1400.0f; c[1].pos[2] = 2000.0f;   /* 400 off */
    c[2].pos[0] = 3000.0f; c[2].pos[2] = 2000.0f;   /* 2000 off */
    int pick = -1;
    c[1].links = 5; c[2].links = 2; c[0].cached = 1;
    if (townkeep::KeptTownChoose(c, row, 0.0f, 0, 1, &pick) != townkeep::kChoseLinks || pick != 1) Fail("T-937: the town most pieces link to is not chosen over the town held and a nearer one");
    c[2].links = 5;
    if (townkeep::KeptTownChoose(c, row, 0.0f, 0, 1, &pick) != townkeep::kChoseLinks || pick != 2) Fail("T-937: a links tie does not go to the town with the row's handle");
    for (int j = 0; j < 5; ++j) c[2].hand[j] = 900 + j;
    c[0].links = 5;
    if (townkeep::KeptTownChoose(c, row, 0.0f, 0, 1, &pick) != townkeep::kChoseLinks || pick != 0) Fail("T-937: a links tie with no row handle does not go to the nearer town");
    c[0].links = 0; c[1].links = 0; c[2].links = 1;
    if (townkeep::KeptTownChoose(c, row, 0.0f, 1, 1, &pick) != townkeep::kChoseLinks || pick != 2) Fail("T-937: a town holding a piece is not chosen when other links did not read");
    c[2].links = 0;
    if (townkeep::KeptTownChoose(c, row, 0.0f, 0, 1, &pick) != townkeep::kChoseCache || pick != 0) Fail("T-937: the town held is not taken when no piece links anywhere");
    c[0].cached = 0;
    if (townkeep::KeptTownChoose(c, row, 0.0f, 0, 1, &pick) != townkeep::kChoseRadius || pick != 0) Fail("T-937: a town 30 units off is not taken");
    c[0].pos[0] = 6000.0f;
    if (townkeep::KeptTownChoose(c, row, 0.0f, 0, 1, &pick) != townkeep::kChoseFound || pick != -1) Fail("T-937: no town within the radius does not found");
    c[1].radius = 450.0f;
    if (townkeep::KeptTownChoose(c, row, 0.0f, 0, 1, &pick) != townkeep::kChoseRadius || pick != 1) Fail("T-937: a town within its own radius is not taken");
    c[1].radius = 0.0f;
    if (townkeep::KeptTownChoose(c, row, 420.0f, 0, 1, &pick) != townkeep::kChoseRadius || pick != 1) Fail("T-937: a town within the pieces' cover radius is not taken");
    if (townkeep::KeptTownChoose(c, row, 380.0f, 0, 1, &pick) != townkeep::kChoseFound) Fail("T-937: a town past the cover radius is taken");
    c[1].radius = 450.0f; c[2].radius = 5000.0f;
    if (townkeep::KeptTownChoose(c, row, 0.0f, 0, 1, &pick) != townkeep::kChoseRadius || pick != 1) Fail("T-937: of two towns within their radius the nearer is not taken");
    c[1].radius = 0.0f; c[2].radius = 0.0f;
    if (townkeep::KeptTownChoose(c, row, 0.0f, 1, 1, &pick) != townkeep::kChoseRefused || pick != -1) Fail("T-937: a town is founded while the links did not all read");
    if (townkeep::KeptTownChoose(c, row, 0.0f, 0, 0, &pick) != townkeep::kChoseNone) Fail("T-937: founding not possible is not none");
    if (townkeep::ChooseRadius(c[1], 0.0f) != townkeep::kTownSameRadius) Fail("T-937: the radius floor is not kTownSameRadius");
    std::vector<townkeep::HandCount> hc;
    townkeep::HandCountAdd(&hc, c[1].hand); townkeep::HandCountAdd(&hc, c[1].hand); townkeep::HandCountAdd(&hc, c[0].hand);
    if (hc.size() != 2 || townkeep::HandCountOf(hc, c[1].hand) != 2 || townkeep::HandCountOf(hc, c[0].hand) != 1 || townkeep::HandCountOf(hc, rh) != 0) Fail("T-937: the links count per town is wrong");
    if (townkeep::RelinkVerdict(1, townkeep::kLinkOwnLive, 1, 1, 0) != townkeep::kRelinkAlready) Fail("T-937: a piece already in the chosen town is relinked");
    if (townkeep::RelinkVerdict(0, townkeep::kLinkNone, 1, 1, 0) != townkeep::kRelinkYes) Fail("T-937: a piece with a dead link is not relinked");
    if (townkeep::RelinkVerdict(0, townkeep::kLinkOwnLive, 1, 1, 0) != townkeep::kRelinkSkipLive || townkeep::RelinkVerdict(0, townkeep::kLinkOwnLive, 1, 1, 1) != townkeep::kRelinkSkipLive)
        Fail("T-937: a piece in a live own town is relinked into another town");
    if (townkeep::RelinkVerdict(0, townkeep::kLinkOtherLive, 1, 1, 0) != townkeep::kRelinkSkipLive) Fail("T-937: a piece in a live world town (a bought house) is relinked");
    if (townkeep::RelinkVerdict(0, townkeep::kLinkOtherLive, 1, 1, 1) != townkeep::kRelinkYes) Fail("T-937: a piece just made for the row is not relinked out of the engine's world town");
    if (townkeep::RelinkVerdict(0, townkeep::kLinkNone, 0, 1, 0) != townkeep::kRelinkSkipNotOwn || townkeep::RelinkVerdict(0, townkeep::kLinkNone, 1, 0, 1) != townkeep::kRelinkSkipNotOwn)
        Fail("T-937: a piece not the owner's, or keyed on no row, is relinked");
    if (townkeep::RelinkVerdict(0, -1, 1, 1, 1) != townkeep::kRelinkUnread) Fail("T-937: a piece whose link did not read is relinked");
    for (int j = 0; j < 5; ++j) c[2].hand[j] = rh[j];
    c[0].links = 1; c[1].links = 4; c[2].links = 4;
    if (townkeep::LinksMajority(c, row) != 2 || townkeep::DuplicateCount(c, 2) != 2) Fail("T-937: the duplicate kept is not the majority with the row's handle");
    c[0].links = 0; c[2].links = 0;
    if (townkeep::DuplicateCount(c, townkeep::LinksMajority(c, row)) != 0) Fail("T-937: one town holding the row is seen as duplicated");
    if (townkeep::DuplicateRemoveStep(0, 0, 0, 0, 1, 1, 1) != townkeep::kRemoveYes) Fail("T-937: an empty duplicate is not removed");
    if (townkeep::DuplicateRemoveStep(0, 0, 0, 1, 1, 1, 1) != townkeep::kRemoveChosen) Fail("T-937: a chosen town is removed");
    if (townkeep::DuplicateRemoveStep(1, 0, 0, 0, 1, 1, 1) != townkeep::kRemoveUnread) Fail("T-937: a duplicate is removed with the pieces not all read");
    if (townkeep::DuplicateRemoveStep(0, 0, 0, 0, 0, 1, 1) != townkeep::kRemoveAreaOut) Fail("T-937: a duplicate is removed with an area not loaded");
    if (townkeep::DuplicateRemoveStep(0, 3, 0, 0, 1, 1, 1) != townkeep::kRemoveLinked) Fail("T-937: a duplicate a piece names is removed");
    if (townkeep::DuplicateRemoveStep(0, 0, 1, 0, 1, 1, 1) != townkeep::kRemoveStore) Fail("T-937: a duplicate whose store holds pieces is removed");
    if (townkeep::DuplicateRemoveStep(0, 0, 0, 0, 1, 1, 0) != townkeep::kRemoveNoCall) Fail("T-937: a duplicate is removed with no TownDestroy");
    /* the majority by all of the owner's loaded pieces: the town holding the base's other buildings wins over one holding more keyed pieces */
    std::vector<townkeep::TownCand> mj(2);
    for (int j = 0; j < 5; ++j) { mj[0].hand[j] = 300 + j; mj[1].hand[j] = 400 + j; }
    mj[0].pos[0] = 1500.0f; mj[0].pos[2] = 2000.0f; mj[1].pos[0] = 1010.0f; mj[1].pos[2] = 2000.0f;
    mj[0].links = 1; mj[0].owned = 40; mj[1].links = 6; mj[1].owned = 6;
    if (townkeep::KeptTownChoose(mj, row, 0.0f, 0, 1, &pick) != townkeep::kChoseLinks || pick != 0) Fail("T-937: the town most of the owner's pieces link to does not win over one holding more keyed pieces");
    if (townkeep::LinksMajority(mj, row) != 0 || townkeep::DuplicateCount(mj, 0) != 1) Fail("T-937: the repair does not keep the town most of the owner's pieces link to");
    mj[1].owned = 40;
    if (townkeep::LinksMajority(mj, row) != 1) Fail("T-937: an owned-pieces tie does not go to the town with more keyed pieces");
    mj[1].links = 0; mj[1].owned = 400;
    if (townkeep::LinksMajority(mj, row) != 0) Fail("T-937: a town no keyed piece of the row links to wins the majority");
    /* an empty town list read whole founds (fold 5: the keep-reset case - no own town yet, the row's pieces loaded); read in part, refused */
    const std::vector<townkeep::TownCand> noTowns;
    if (townkeep::KeptTownChoose(noTowns, row, 0.0f, 0, 1, &pick) != townkeep::kChoseFound || pick != -1) Fail("T-937: an empty town list read whole does not found (the keep-reset case)");
    if (townkeep::KeptTownChoose(noTowns, row, 0.0f, 1, 1, &pick) != townkeep::kChoseRefused || pick != -1) Fail("T-937: an empty town list read in part founds");
    if (townkeep::KeptTownChoose(noTowns, row, 0.0f, 0, 0, &pick) != townkeep::kChoseNone || pick != -1) Fail("T-937: an empty town list read whole with founding not possible is not none");
    if (townkeep::LinksMajority(noTowns, row) != -1 || townkeep::DuplicateCount(noTowns, -1) != 0) Fail("T-937: an empty town list has a majority or a duplicate");
    /* a row with no handle: a links tie to the nearer; equal distances to the earlier in the list */
    townkeep::TownRow nr;
    nr.owner = 0; nr.pos[0] = 1000.0f; nr.pos[2] = 2000.0f;
    std::vector<townkeep::TownCand> eq(2);
    for (int j = 0; j < 5; ++j) { eq[0].hand[j] = 500 + j; eq[1].hand[j] = 600 + j; }
    eq[0].pos[0] = 1100.0f; eq[0].pos[2] = 2000.0f; eq[1].pos[0] = 950.0f; eq[1].pos[2] = 2000.0f;
    eq[0].links = 2; eq[1].links = 2;
    if (townkeep::LinksMajority(eq, nr) != 1) Fail("T-937: with no row handle a links tie does not go to the nearer town");
    eq[1].pos[0] = 900.0f;
    if (townkeep::LinksMajority(eq, nr) != 0) Fail("T-937: a links tie at equal distances does not go to the earlier town");
    eq[0].links = 0; eq[1].links = 0; eq[0].pos[0] = 1030.0f; eq[1].pos[0] = 970.0f;
    if (townkeep::KeptTownChoose(eq, nr, 0.0f, 0, 1, &pick) != townkeep::kChoseRadius || pick != 0) Fail("T-937: two towns at equal distances within the radius do not go to the earlier");
    /* a town radius that did not read (negative, NaN): unsure, never founded */
    std::vector<townkeep::TownCand> rr(2);
    for (int j = 0; j < 5; ++j) { rr[0].hand[j] = 700 + j; rr[1].hand[j] = 800 + j; }
    rr[0].pos[0] = 5000.0f; rr[0].pos[2] = 2000.0f; rr[1].pos[0] = 7000.0f; rr[1].pos[2] = 2000.0f;
    rr[0].radius = -1.0f;
    if (townkeep::KeptTownChoose(rr, row, 0.0f, 0, 1, &pick) != townkeep::kChoseRefused || pick != -1) Fail("T-937: a town is founded while a town's radius read -1");
    volatile float zz = 0.0f;
    rr[0].radius = zz / zz;
    if (townkeep::KeptTownChoose(rr, row, 0.0f, 0, 1, &pick) != townkeep::kChoseRefused) Fail("T-937: a town is founded while a town's radius read NaN");
    if (townkeep::ChooseRadius(rr[0], 0.0f) != townkeep::kTownSameRadius) Fail("T-937: a NaN radius is not the radius floor");
    rr[1].pos[0] = 1030.0f;
    if (townkeep::KeptTownChoose(rr, row, 0.0f, 0, 1, &pick) != townkeep::kChoseRadius || pick != 1) Fail("T-937: a town within the radius is not taken beside a town whose radius did not read");
    rr[0].radius = 450.0f; rr[1].pos[0] = 7000.0f;
    if (townkeep::KeptTownChoose(rr, row, 0.0f, 0, 1, &pick) != townkeep::kChoseFound) Fail("T-937: towns whose radius read and stand far off do not found");
    /* a refusal while a town is held: the town held is taken */
    rr[1].cached = 1;
    if (townkeep::KeptTownChoose(rr, row, 0.0f, 1, 1, &pick) != townkeep::kChoseCache || pick != 1) Fail("T-937: the town held is not taken while the links did not all read");
    /* the areas around a duplicate */
    if (townkeep::DupAroundHalf(-1.0f, 100.0f) >= 0.0f || townkeep::DupAroundHalf(zz / zz, 0.0f) >= 0.0f) Fail("T-937: a duplicate's radius that did not read gives a square");
    if (townkeep::DupAroundHalf(100.0f, 300.0f) != 550.0f || townkeep::DupAroundHalf(600.0f, 300.0f) != 900.0f || townkeep::DupAroundHalf(0.0f, 0.0f) != 250.0f)
        Fail("T-937: the square around a duplicate is the wrong size");
    if (townkeep::DuplicateRemoveStep(0, 0, 0, 0, 1, 0, 1) != townkeep::kRemoveAroundOut) Fail("T-937: a duplicate is removed with an area around it not loaded");
    if (townkeep::DuplicateRemoveStep(0, 0, 0, 0, 1, -1, 1) != townkeep::kRemoveNoRadius) Fail("T-937: a duplicate is removed with its radius not read");
    if (townkeep::DuplicateRemoveStep(0, 0, 0, 0, 0, 0, 1) != townkeep::kRemoveAreaOut) Fail("T-937: the row's areas are not looked at before the areas around the duplicate");
    /* the main base near an outpost: only the owner's pieces within the outpost's reach count, so a big town with one keyed link does not win */
    std::vector<townkeep::OwnPiece> op;
    townkeep::OwnPiece q;
    q.pos[1] = 0.0f; q.pos[2] = 2000.0f;
    for (int i = 0; i < 20; ++i) { for (int j = 0; j < 5; ++j) q.hand[j] = 910 + j; q.pos[0] = 1000.0f + (float)i * 5.0f; op.push_back(q); }   /* the outpost's town: within 100 */
    for (int i = 0; i < 150; ++i) { for (int j = 0; j < 5; ++j) q.hand[j] = 920 + j; q.pos[0] = 1600.0f + (float)i; op.push_back(q); }   /* the main base: 600 and more off */
    for (int j = 0; j < 5; ++j) q.hand[j] = 920 + j;
    q.pos[0] = 1300.0f; op.push_back(q);   /* the outpost's keyed piece in the main base: 300 off */
    std::vector<townkeep::HandCount> inReach;
    townkeep::OwnedInReach(op, row, 300.0f, &inReach);   /* reach 300 + kTownCoverMargin */
    std::vector<townkeep::TownCand> om(2);
    for (int j = 0; j < 5; ++j) { om[0].hand[j] = 910 + j; om[1].hand[j] = 920 + j; }
    om[0].pos[0] = 1050.0f; om[0].pos[2] = 2000.0f; om[1].pos[0] = 1700.0f; om[1].pos[2] = 2000.0f;
    om[0].links = 3; om[1].links = 1;
    om[0].owned = townkeep::HandCountOf(inReach, om[0].hand); om[1].owned = townkeep::HandCountOf(inReach, om[1].hand);
    if (om[0].owned != 20 || om[1].owned != 1) Fail("T-937: the owner's pieces past the outpost's reach are counted");
    if (townkeep::KeptTownChoose(om, row, 0.0f, 0, 1, &pick) != townkeep::kChoseLinks || pick != 0 || townkeep::LinksMajority(om, row) != 0)
        Fail("T-937: a big town nearby with one keyed link wins over the outpost's own town");
    townkeep::OwnedInReach(op, row, zz / zz, &inReach);
    if (!inReach.empty()) Fail("T-937: pieces are counted with a reach that is not a number");
    /* a town another kept row holds: taken by this row's links (two rows may share one town), by its radius step only when no open
       town stands within its radius, never its duplicate; with its radius not read it holds the founding */
    std::vector<townkeep::TownCand> sh(2);
    for (int j = 0; j < 5; ++j) { sh[0].hand[j] = 930 + j; sh[1].hand[j] = 940 + j; }
    sh[0].pos[0] = 1020.0f; sh[0].pos[2] = 2000.0f; sh[1].pos[0] = 1300.0f; sh[1].pos[2] = 2000.0f;
    sh[0].otherRow = 1; sh[0].links = 5; sh[0].owned = 50; sh[1].links = 1; sh[1].owned = 2;
    if (townkeep::KeptTownChoose(sh, row, 0.0f, 0, 1, &pick) != townkeep::kChoseLinks || pick != 0 || townkeep::LinksMajority(sh, row) != 0)
        Fail("T-937: a town another kept row holds is not this row's while most of its pieces link to it");
    sh[1].owned = 60;
    if (townkeep::LinksMajority(sh, row) != 1 || townkeep::DuplicateCount(sh, 1) != 0 || townkeep::IsDuplicate(sh, 0, 1) != 0)
        Fail("T-937: another kept row's town is counted as this row's duplicate");
    sh[1].links = 0; sh[1].owned = 2;
    if (townkeep::KeptTownChoose(sh, row, 0.0f, 0, 1, &pick) != townkeep::kChoseLinks || pick != 0) Fail("T-937: this row's pieces in another row's town do not take it");
    sh[0].links = 0;
    if (townkeep::KeptTownChoose(sh, row, 0.0f, 0, 1, &pick) != townkeep::kChoseRadius || pick != 0) Fail("T-937: another row's town 20 units off is not shared by the radius (with no open town within it)");
    sh[0].radius = -1.0f;
    if (townkeep::KeptTownChoose(sh, row, 0.0f, 0, 1, &pick) != townkeep::kChoseRefused) Fail("T-937: another row's town is shared while its own radius did not read (an unread town may be this row's own)");
    sh[0].radius = 0.0f;
    if (townkeep::KeptTownChoose(sh, row, 0.0f, 1, 1, &pick) != townkeep::kChoseRefused) Fail("T-937: another row's town is shared while the town list is unsure");
    sh[0].pos[0] = 1000.0f + 150.0f; sh[1].pos[0] = 5000.0f;   /* no open town within the reach: only the held one 150 off */
    if (townkeep::KeptTownChoose(sh, row, 400.0f, 0, 1, &pick) == townkeep::kChoseRadius && pick == 0) Fail("T-937: another row's town is shared through the pieces' reach, past its own radius");
    sh[1].pos[0] = 1300.0f;
    sh[0].radius = -1.0f; sh[0].pos[0] = 5000.0f;
    if (townkeep::KeptTownChoose(sh, row, 0.0f, 0, 1, &pick) != townkeep::kChoseRefused) Fail("T-937: a town is founded while another row's town's radius did not read");
    sh[0].radius = 0.0f;
    if (townkeep::KeptTownChoose(sh, row, 0.0f, 0, 1, &pick) != townkeep::kChoseFound) Fail("T-937: another row's town far off holds the founding");
    sh[0].pos[0] = 1020.0f; sh[1].pos[0] = 1040.0f;
    if (townkeep::KeptTownChoose(sh, row, 0.0f, 0, 1, &pick) != townkeep::kChoseRadius || pick != 1) Fail("T-937: an open town within the radius is not taken beside a nearer town another row holds");
    sh[1].pos[0] = 1300.0f; sh[0].cached = 1;
    if (townkeep::KeptTownChoose(sh, row, 0.0f, 0, 1, &pick) != townkeep::kChoseCache || pick != 0) Fail("T-937: the town held for this row (and another) is not taken");
    sh[0].links = 5;
    if (townkeep::KeptTownChoose(sh, row, 0.0f, 0, 1, &pick) != townkeep::kChoseLinks || pick != 0) Fail("T-937: a town held for this row (and another) is not this row's");
    /* two kept rows whose keyed pieces both link to town T: both take T by their links, whichever holds it first */
    std::vector<townkeep::TownCand> two(1);
    for (int j = 0; j < 5; ++j) two[0].hand[j] = 950 + j;
    two[0].pos[0] = 1010.0f; two[0].pos[2] = 2000.0f; two[0].links = 3; two[0].owned = 30;
    if (townkeep::KeptTownChoose(two, row, 0.0f, 0, 1, &pick) != townkeep::kChoseLinks || pick != 0) Fail("T-937: the first row whose pieces link to a town does not take it");
    two[0].otherRow = 1; two[0].links = 2;   /* the second row: the first holds T */
    if (townkeep::KeptTownChoose(two, row, 0.0f, 0, 1, &pick) != townkeep::kChoseLinks || pick != 0 || townkeep::DuplicateCount(two, 0) != 0)
        Fail("T-937: the second row whose pieces link to a town another row holds does not take it");
    /* the outpost (row) beside the main base (br): the outpost takes its own town, the base row the base's town, in either call order */
    townkeep::TownRow br;
    br.owner = 0; br.pos[0] = 1700.0f; br.pos[2] = 2000.0f;
    std::vector<townkeep::HandCount> baseReach;
    townkeep::OwnedInReach(op, br, 100.0f, &baseReach);   /* reach 100 + kTownCoverMargin */
    std::vector<townkeep::TownCand> bm(om);   /* bm[0] the outpost's town, bm[1] the base's */
    bm[0].links = 0; bm[1].links = 10;
    bm[0].owned = townkeep::HandCountOf(baseReach, bm[0].hand); bm[1].owned = townkeep::HandCountOf(baseReach, bm[1].hand);
    if (bm[0].owned != 0 || bm[1].owned != 150) Fail("T-937: the base row's reach counts the wrong pieces");
    for (int order = 0; order < 2; ++order)
    {
        std::vector<townkeep::TownCand> oc(om), bc(bm);
        if (order == 0) bc[0].otherRow = 1; else oc[1].otherRow = 1;   /* the row chosen first holds its town when the other chooses */
        int po = -1, pb = -1;
        const int ho = townkeep::KeptTownChoose(oc, row, 0.0f, 0, 1, &po), hb = townkeep::KeptTownChoose(bc, br, 0.0f, 0, 1, &pb);
        if (ho != townkeep::kChoseLinks || po != 0 || hb != townkeep::kChoseLinks || pb != 1) Fail("T-937: the outpost and the main base do not each take their own town in either call order");
    }
    /* the walk again before founding, and a waiting duplicate each look */
    if (townkeep::ChooseAgain(townkeep::kChoseFound, 5, 5) != 1 || townkeep::ChooseAgain(townkeep::kChoseFound, 5, 6) != 0 || townkeep::ChooseAgain(townkeep::kChoseRadius, 5, 5) != 0)
        Fail("T-937: a found choice from an older walk is not walked again (or a fresh one is)");
    if (townkeep::RewalkUnsure(1, 5, 6, 1) != 0 || townkeep::RewalkUnsure(0, 5, 6, 1) != 1 || townkeep::RewalkUnsure(1, 5, 5, 1) != 1 || townkeep::RewalkUnsure(1, 5, 6, 0) != 1)
        Fail("T-937: a walk again that did not run whole is not unsure");
    if (townkeep::PendingStep(0, 0, 1) != townkeep::kPendGone || townkeep::PendingStep(1, 1, 1) != townkeep::kPendDead || townkeep::PendingStep(1, 0, 0) != townkeep::kPendGone || townkeep::PendingStep(1, 0, 1) != townkeep::kPendTry)
        Fail("T-937: a waiting duplicate's step is wrong");
}
/* T-985 C1: the kept mirror folder's name - stable for one player in one world, apart for another player or world */
void t_t985c1_mirror_name()
{
    if (mirrorkeep::Fnv64("") != 14695981039346656037ULL || mirrorkeep::Fnv64("a") != 12638187200555641996ULL) Fail("T-985 C1: FNV-1a 64 known vectors");
    const std::string a = mirrorkeep::IdentityHash("world-1", "player-1");
    if (a.size() != 16 || a.find_first_not_of("0123456789abcdef") != std::string::npos) Fail("T-985 C1: the identity hash is not 16 hex digits: " + a);
    if (a != mirrorkeep::IdentityHash("world-1", "player-1")) Fail("T-985 C1: the same player in the same world got another name after a restart");
    if (a == mirrorkeep::IdentityHash("world-1", "player-2")) Fail("T-985 C1: two players of one world share a mirror folder");
    if (a == mirrorkeep::IdentityHash("world-2", "player-1")) Fail("T-985 C1: a reset world (new id) reuses the old folder");
    if (mirrorkeep::StableDirName(a) != "mirror-k" + a) Fail("T-985 C1: the stable folder name");
    if (mirrorkeep::StableDirName(a).compare(0, 7, "mirror-") != 0) Fail("T-985 C1: the stable name lost the mirror- prefix the world server's leftover rule reads");
    if (mirrorkeep::ProcessDirName(8940) != "mirror-8940") Fail("T-985 C1: the per-process fallback is not the old name");
    if (mirrorkeep::NameDecide(1, 1) != mirrorkeep::kNameStable) Fail("T-985 C1: both ids known gave no stable name");
    if (mirrorkeep::NameDecide(0, 1) != mirrorkeep::kNameNoWorldId || mirrorkeep::NameDecide(0, 0) != mirrorkeep::kNameNoWorldId) Fail("T-985 C1: no world id gave a stable name");
    if (mirrorkeep::NameDecide(1, 0) != mirrorkeep::kNameNoPlayer) Fail("T-985 C1: no player id gave a stable name");
}
static mirrorkeep::KeptRow T985c1Row(const std::string& key, long long stamp)
{
    mirrorkeep::KeptRow r;
    r.key = key; r.squadSid = "sid 1"; r.factionName = "Holy Nation"; r.town = ""; r.home = "home-7";
    r.x = -12.375f; r.y = 0.0f; r.z = 1.0e-38f; r.writtenAt = stamp; r.posAt = stamp + 5; r.owner = 4294967295u; r.seq = 18446744073709551615ULL; r.birth = -1;
    r.size = 123456789ULL; r.mtime = 133000000000000000ULL; r.crc = 3421780262u;
    return r;
}
static bool T985c1Same(const mirrorkeep::KeptRow& a, const mirrorkeep::KeptRow& b)
{
    return a.key == b.key && a.squadSid == b.squadSid && a.factionName == b.factionName && a.town == b.town && a.home == b.home
        && mirrorkeep::FloatBits(a.x) == mirrorkeep::FloatBits(b.x) && mirrorkeep::FloatBits(a.y) == mirrorkeep::FloatBits(b.y) && mirrorkeep::FloatBits(a.z) == mirrorkeep::FloatBits(b.z)
        && a.writtenAt == b.writtenAt && a.posAt == b.posAt && a.owner == b.owner && a.seq == b.seq && a.birth == b.birth
        && a.size == b.size && a.mtime == b.mtime && a.crc == b.crc;
}
/* T-985 C1: the kept table round trip, and the rows it will not write */
void t_t985c1_table_round_trip()
{
    std::vector<mirrorkeep::KeptRow> in;
    in.push_back(T985c1Row("zone.20.32", 1000));
    in.push_back(T985c1Row("Nameless_0", 7));
    mirrorkeep::KeptRow tab = T985c1Row("bad", 9); tab.squadSid = "a\tb"; in.push_back(tab);   /* a tab in a text field: not written */
    mirrorkeep::KeptRow nostamp = T985c1Row("pos-only", 0); in.push_back(nostamp);              /* no file stamp: not written */
    std::string text; long long refused = 0;
    if (!mirrorkeep::TableFormat("wid-1", "00112233445566ff", in, &text, &refused) || refused != 2) { Fail("T-985 C1: the table did not format, or refused other than 2 rows"); return; }
    std::vector<mirrorkeep::KeptRow> out; long long bad = 0;
    if (mirrorkeep::TableParse(text, "wid-1", "00112233445566ff", &out, &bad) != mirrorkeep::kTableOk || bad != 0 || out.size() != 2) { Fail("T-985 C1: the table did not read back whole: " + text); return; }
    if (!T985c1Same(out[0], in[0]) || !T985c1Same(out[1], in[1])) Fail("T-985 C1: a row read back differs from the row written");
    std::vector<mirrorkeep::KeptRow> none;
    if (!mirrorkeep::TableFormat("wid-1", "00112233445566ff", none, &text, 0) || mirrorkeep::TableParse(text, "wid-1", "00112233445566ff", &out, &bad) != mirrorkeep::kTableOk || !out.empty())
        Fail("T-985 C1: an empty table did not read back empty");
    if (mirrorkeep::TableFormat("", "00112233445566ff", in, &text, 0) || mirrorkeep::TableFormat("wid-1", "", in, &text, 0)) Fail("T-985 C1: a table with no world id or identity was formatted");
}
/* T-985 C1: a table of another world or another player, a torn or a damaged one, is refused whole */
void t_t985c1_table_refused()
{
    std::vector<mirrorkeep::KeptRow> in; in.push_back(T985c1Row("zone.1.2", 50)); in.push_back(T985c1Row("Group_9", 60));
    std::string text;
    if (!mirrorkeep::TableFormat("wid-1", "aaaaaaaaaaaaaaaa", in, &text, 0)) { Fail("T-985 C1: the table did not format"); return; }
    std::vector<mirrorkeep::KeptRow> out; long long bad = 0;
    if (mirrorkeep::TableParse(text, "wid-2", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableOtherWorld || !out.empty()) Fail("T-985 C1: another world's table was used");
    if (mirrorkeep::TableParse(text, "", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableOtherWorld) Fail("T-985 C1: a table was used with no world id");
    if (mirrorkeep::TableParse(text, "wid-1", "bbbbbbbbbbbbbbbb", &out, &bad) != mirrorkeep::kTableOtherPlayer || !out.empty()) Fail("T-985 C1: another player's table was used");
    if (mirrorkeep::TableParse("", "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableBadHeader) Fail("T-985 C1: an empty file read as a table");
    if (mirrorkeep::TableParse("hello\nend\t0\t0\n", "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableBadHeader) Fail("T-985 C1: a stranger's file read as a table");
    /* torn: the end line cut, or gone */
    if (mirrorkeep::TableParse(text.substr(0, text.size() - 1), "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableTorn || !out.empty()) Fail("T-985 C1: a table cut short was used");
    const std::string::size_type endAt = text.rfind("\nend\t");
    if (mirrorkeep::TableParse(text.substr(0, endAt + 1), "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableTorn) Fail("T-985 C1: a table with no end line was used");
    /* torn: a row dropped from the middle (the count and the checksum no longer agree) */
    const std::string::size_type r2 = text.find("\nr\tGroup_9");
    if (r2 == std::string::npos || mirrorkeep::TableParse(text.substr(0, r2 + 1) + text.substr(endAt + 1), "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableTorn)
        Fail("T-985 C1: a table missing a row was used");
    /* damaged: one stamp raised (the claim a stale table must never make) */
    std::string raised = text; const std::string::size_type st = raised.find("\t50\t");
    if (st == std::string::npos) { Fail("T-985 C1: the test row's stamp is not where the test expects"); return; }
    raised.replace(st, 4, "\t59\t");
    if (mirrorkeep::TableParse(raised, "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableTorn) Fail("T-985 C1: a table with a changed stamp was used");
    /* anything after the end line */
    if (mirrorkeep::TableParse(text + "r\tx\n", "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableTorn) Fail("T-985 C1: a table with a line after its end was used");
    /* an intact table holding a row this build cannot read: that row alone is skipped and counted */
    std::string body = std::string("swkept\t1\twid-1\taaaaaaaaaaaaaaaa\n") + "r\tunreadable\n";
    body += "end\t1\t" + mirrorkeep::Hex16(mirrorkeep::Fnv64(body)) + "\n";
    bad = 0;
    if (mirrorkeep::TableParse(body, "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableOk || bad != 1 || !out.empty()) Fail("T-985 C1: an unreadable row in an intact table");
    if (mirrorkeep::WhyOfTable(mirrorkeep::kTableOk) != mirrorkeep::kWhyKept || mirrorkeep::WhyOfTable(mirrorkeep::kTableOtherWorld) != mirrorkeep::kWhyOtherWorld
        || mirrorkeep::WhyOfTable(mirrorkeep::kTableOtherPlayer) != mirrorkeep::kWhyOtherPlayer || mirrorkeep::WhyOfTable(mirrorkeep::kTableTorn) != mirrorkeep::kWhyTorn
        || mirrorkeep::WhyOfTable(mirrorkeep::kTableBadHeader) != mirrorkeep::kWhyBadHeader) Fail("T-985 C1: a table verdict reported under the wrong reason");
}
/* T-985 C1: a row is taken only when its file is there with the size, time and content it recorded; which rows are written */
void t_t985c1_row_check()
{
    const mirrorkeep::KeptRow r = T985c1Row("zone.3.4", 100);
    if (mirrorkeep::RowCheck(r, 1, r.size, r.mtime) != mirrorkeep::kRowOk) Fail("T-985 C1: a matching file was refused");
    if (mirrorkeep::RowCheck(r, 0, r.size, r.mtime) != mirrorkeep::kRowNoFile) Fail("T-985 C1: a row with no file was taken");
    if (mirrorkeep::RowCheck(r, 1, r.size + 1, r.mtime) != mirrorkeep::kRowChanged) Fail("T-985 C1: a file of another size was taken");
    if (mirrorkeep::RowCheck(r, 1, r.size, r.mtime + 1) != mirrorkeep::kRowChanged) Fail("T-985 C1: a file rewritten after the table was taken");
    if (mirrorkeep::RowTakes(0, 0, 5) != 1 || mirrorkeep::RowTakes(1, 4, 5) != 1) Fail("T-985 C1: a newer kept row was not taken");
    if (mirrorkeep::RowTakes(1, 5, 5) != 0 || mirrorkeep::RowTakes(1, 6, 5) != 0) Fail("T-985 C1: a kept row replaced an equal or newer one");
    if (mirrorkeep::RowListed(0, 10, 1, 0) != 1) Fail("T-985 C1: a live mirror record was not written");
    if (mirrorkeep::RowListed(1, 10, 1, 0) != 0 || mirrorkeep::RowListed(0, 0, 1, 0) != 0 || mirrorkeep::RowListed(0, 10, 0, 0) != 0 || mirrorkeep::RowListed(0, 10, 1, 1) != 0)
        Fail("T-985 C1: a gone, position-only, world-server-folder or deleted record was written");
    mirrorkeep::KeptRow p;
    if (mirrorkeep::RowParse("r\tk\t0\t0\t0\t00000000\t00000000\t00000000\t0\t-1\t0\t0\t00000000\t\t\t\t", &p)) Fail("T-985 C1: a row with no file stamp was read");
    if (mirrorkeep::RowParse("r\tk\t5\t0\t4294967296\t00000000\t00000000\t00000000\t0\t-1\t0\t0\t00000000\t\t\t\t", &p)) Fail("T-985 C1: an owner past 32 bits was read");
    if (!mirrorkeep::RowParse("r\tk\t5\t0\t0\t00000000\t00000000\t00000000\t0\t-1\t0\t0\t00000000\t\t\t\t", &p) || p.key != "k" || p.writtenAt != 5) Fail("T-985 C1: a minimal row was not read");
}
/* T-985 C1 fold 1 (review LOW 9, LOW 8): the load order - a deleted or area-off row is skipped before its file is looked at, a row whose
   file is gone or changed is refused before the stamp test, a row not newer is skipped, and only a row taken notes its town and home */
void t_t985c1f1_load_order()
{
    if (mirrorkeep::RowLoad(1, 0, mirrorkeep::kRowOk, 0, 0, 5) != mirrorkeep::kLoadSkipped) Fail("T-985 C1 fold 1: a row deleted here was not skipped");
    if (mirrorkeep::RowLoad(0, 1, mirrorkeep::kRowNoFile, 0, 0, 5) != mirrorkeep::kLoadSkipped) Fail("T-985 C1 fold 1: an area row with area records off was judged by its file");
    if (mirrorkeep::RowLoad(0, 0, mirrorkeep::kRowNoFile, 0, 0, 5) != mirrorkeep::kLoadRejected || mirrorkeep::RowLoad(0, 0, mirrorkeep::kRowChanged, 1, 9, 5) != mirrorkeep::kLoadRejected)
        Fail("T-985 C1 fold 1: a row whose file is gone or changed was not refused first");
    if (mirrorkeep::RowLoad(0, 0, mirrorkeep::kRowOk, 1, 5, 5) != mirrorkeep::kLoadNotNewer || mirrorkeep::RowLoad(0, 0, mirrorkeep::kRowOk, 1, 6, 5) != mirrorkeep::kLoadNotNewer)
        Fail("T-985 C1 fold 1: a row not newer than the table's own was taken");
    if (mirrorkeep::RowLoad(0, 0, mirrorkeep::kRowOk, 0, 0, 5) != mirrorkeep::kLoadTaken || mirrorkeep::RowLoad(0, 0, mirrorkeep::kRowOk, 1, 4, 5) != mirrorkeep::kLoadTaken)
        Fail("T-985 C1 fold 1: a good newer row was not taken");
    if (mirrorkeep::RowLoadNotes(mirrorkeep::kLoadNotNewer) != 0 || mirrorkeep::RowLoadNotes(mirrorkeep::kLoadSkipped) != 0 || mirrorkeep::RowLoadNotes(mirrorkeep::kLoadRejected) != 0)
        Fail("T-985 C1 fold 1: a row not taken noted its town and home (review LOW 8)");
    if (mirrorkeep::RowLoadNotes(mirrorkeep::kLoadTaken) != 1) Fail("T-985 C1 fold 1: a row taken made no town or home note");
}
/* T-985 C1 fold 1 (review LOW 9, MED 5): a row's size and time vouch for its own file only, and no content is read */
void t_t985c1f1_stamp_pairing()
{
    std::vector<mirrorkeep::KeptRow> in;
    mirrorkeep::KeptRow a = T985c1Row("Group_1", 10); a.size = 100; a.mtime = 1000; a.crc = 0; in.push_back(a);
    mirrorkeep::KeptRow b = T985c1Row("Group_2", 20); b.size = 200; b.mtime = 2000; b.crc = 0; in.push_back(b);
    std::string text; std::vector<mirrorkeep::KeptRow> out; long long bad = 0;
    if (!mirrorkeep::TableFormat("wid-1", "aaaaaaaaaaaaaaaa", in, &text, 0) || mirrorkeep::TableParse(text, "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableOk || out.size() != 2)
    { Fail("T-985 C1 fold 1: the pairing table did not read back"); return; }
    const mirrorkeep::KeptRow& ra = out[0].key == "Group_1" ? out[0] : out[1];
    const mirrorkeep::KeptRow& rb = out[0].key == "Group_1" ? out[1] : out[0];
    if (ra.key != "Group_1" || rb.key != "Group_2" || ra.size != 100 || ra.mtime != 1000 || rb.size != 200 || rb.mtime != 2000) { Fail("T-985 C1 fold 1: a row's file stamps came back on another row"); return; }
    if (mirrorkeep::RowCheck(ra, 1, 100, 1000) != mirrorkeep::kRowOk || mirrorkeep::RowCheck(rb, 1, 200, 2000) != mirrorkeep::kRowOk) Fail("T-985 C1 fold 1: a row was refused against its own file");
    if (mirrorkeep::RowCheck(ra, 1, 200, 2000) != mirrorkeep::kRowChanged || mirrorkeep::RowCheck(rb, 1, 100, 1000) != mirrorkeep::kRowChanged) Fail("T-985 C1 fold 1: a row was taken against another record's file");
    if (mirrorkeep::RowCheck(ra, 1, 100, 2000) != mirrorkeep::kRowChanged || mirrorkeep::RowCheck(ra, 1, 200, 1000) != mirrorkeep::kRowChanged) Fail("T-985 C1 fold 1: half a stamp pair was enough");
    mirrorkeep::KeptRow c1 = a; c1.crc = 3421780262u;   /* a C1 table's real CRC: read, never checked */
    if (mirrorkeep::RowCheck(c1, 1, 100, 1000) != mirrorkeep::kRowOk) Fail("T-985 C1 fold 1: the row's CRC field was checked (the file would have to be read)");
}
/* T-985 C1 fold 1 (review HIGH 1, LOW 9): the unnamed-key drop at the first whole snapshot's end, and the hold-back before it */
void t_t985c1f1_unnamed_drop()
{
    if (mirrorkeep::UnnamedDecide(1, 0, 1, 50, 50) != mirrorkeep::kUnnamedDrop) Fail("T-985 C1 fold 1: a kept key the whole snapshot did not name was kept");
    if (mirrorkeep::UnnamedDecide(1, 1, 1, 50, 50) != mirrorkeep::kUnnamedKeep) Fail("T-985 C1 fold 1: a kept key the snapshot named was dropped");
    if (mirrorkeep::UnnamedDecide(0, 0, 1, 50, 50) != mirrorkeep::kUnnamedKeep) Fail("T-985 C1 fold 1: a snapshot that is not whole dropped a key");
    if (mirrorkeep::UnnamedDecide(1, 0, 0, 0, 50) != mirrorkeep::kUnnamedKeep) Fail("T-985 C1 fold 1: a key already gone here was dropped again");
    if (mirrorkeep::UnnamedDecide(1, 0, 1, 51, 50) != mirrorkeep::kUnnamedChangedHere) Fail("T-985 C1 fold 1: a key written here since the load was dropped");
    if (mirrorkeep::KeptHeldBack(1, 0) != 1) Fail("T-985 C1 fold 1: a kept key not yet named was not held back");
    if (mirrorkeep::KeptHeldBack(1, 1) != 0 || mirrorkeep::KeptHeldBack(0, 0) != 0) Fail("T-985 C1 fold 1: a named key, or one after the snapshot, was held back");
}
/* T-985 C1 fold 1 (review MED 4, LOW 9): two keys whose mirror files are one name to Windows are both left out */
void t_t985c1f1_name_clash()
{
    std::vector<std::string> names;
    names.push_back("C:\\w\\mirror-k1\\Group_1.platoon");
    names.push_back("C:\\w\\mirror-k1\\group_1.platoon");   /* case only */
    names.push_back("C:\\w\\mirror-k1\\Other_2.platoon");
    names.push_back("C:\\w\\mirror-k1\\a_b.platoon");       /* 'a b' and 'a_b' both name a_b */
    names.push_back("C:\\w\\mirror-k1\\a_b.platoon");
    std::vector<int> clash;
    if (mirrorkeep::NameClashMarks(names, &clash) != 4 || clash.size() != 5) { Fail("T-985 C1 fold 1: the clash count"); return; }
    if (clash[0] != 1 || clash[1] != 1 || clash[2] != 0 || clash[3] != 1 || clash[4] != 1) Fail("T-985 C1 fold 1: the wrong rows were left out for a shared file name");
    std::vector<std::string> none;
    if (mirrorkeep::NameClashMarks(none, &clash) != 0 || !clash.empty()) Fail("T-985 C1 fold 1: an empty list clashed");
    if (mirrorkeep::LowerAscii("AbC_~E4") != "abc_~e4") Fail("T-985 C1 fold 1: the lower-casing");
}
/* T-985 C1 fold 1 (review LOW 7): a world reset moves a game's kept mirror-k<16 hex> folder too */
void t_t985c1f1_reset_kept_mirror()
{
    if (worldreset::ResetClassify("mirror-k0123456789abcdef", 1) != worldreset::kResetMove || worldreset::ResetClassify("MIRROR-K0123456789ABCDEF", 1) != worldreset::kResetMove)
        Fail("T-985 C1 fold 1: a kept mirror folder is not moved by a reset");
    if (worldreset::ResetClassify("mirror-k0123", 1) != worldreset::kResetUnknown || worldreset::ResetClassify("mirror-k0123456789abcdeg", 1) != worldreset::kResetUnknown
        || worldreset::ResetClassify("mirror-k0123456789abcdef0", 1) != worldreset::kResetUnknown || worldreset::ResetClassify("mirror-k", 1) != worldreset::kResetUnknown)
        Fail("T-985 C1 fold 1: a folder that only looks like a kept mirror was moved");
    if (worldreset::ResetClassify("mirror-k0123456789abcdef", 0) == worldreset::kResetMove) Fail("T-985 C1 fold 1: a FILE of a kept mirror's name was moved as a folder");
    if (worldreset::ResetClassify("mirror-4242", 1) != worldreset::kResetMove) Fail("T-985 C1 fold 1: the per-process mirror folder is no longer moved");
    if (!worldreset::ResetIsMirror("mirror-k0123456789abcdef") || !worldreset::ResetIsMirror("Mirror-12") || worldreset::ResetIsMirror("mirror-kxyz")) Fail("T-985 C1 fold 1: a mirror folder name is misread");
}
/* T-985 C1 fold 1 (finding 3): a world server behind the table (restored from a backup) refuses it whole; a C1 table (no seq) is
   accepted; the seq rides in the header and is part of the checksum */
void t_t985c1f1_server_behind()
{
    if (mirrorkeep::ServerBehind(100, 1, 99) != 1) Fail("T-985 C1 fold 1: a world server behind the kept table was not refused");
    if (mirrorkeep::ServerBehind(100, 1, 100) != 0 || mirrorkeep::ServerBehind(100, 1, 250) != 0) Fail("T-985 C1 fold 1: a world server level with or past the table refused it");
    if (mirrorkeep::ServerBehind(0, 1, 5) != 0) Fail("T-985 C1 fold 1: a C1 table (no stored seq) was refused");
    if (mirrorkeep::ServerBehind(100, 0, 0) != 0) Fail("T-985 C1 fold 1: an unknown seqHigh refused the table");
    if (mirrorkeep::BehindDrops(1, 50, 50) != 1) Fail("T-985 C1 fold 1: a key still the table's copy was kept from a refused table");
    if (mirrorkeep::BehindDrops(1, 60, 50) != 0 || mirrorkeep::BehindDrops(0, 0, 50) != 0) Fail("T-985 C1 fold 1: a re-fetched or gone key was dropped");
    std::vector<mirrorkeep::KeptRow> in; in.push_back(T985c1Row("Group_1", 10));
    std::string text; std::vector<mirrorkeep::KeptRow> out; long long bad = 0; unsigned long long seq = 7;
    if (!mirrorkeep::TableFormatSeq("wid-1", "aaaaaaaaaaaaaaaa", 4242ULL, in, &text, 0)
        || mirrorkeep::TableParseSeq(text, "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad, &seq) != mirrorkeep::kTableOk || seq != 4242ULL || out.size() != 1)
        Fail("T-985 C1 fold 1: the stored seq did not read back");
    if (mirrorkeep::TableParse(text, "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad) != mirrorkeep::kTableOk) Fail("T-985 C1 fold 1: a table with a seq was refused by the plain reader");
    std::string raised = text; const std::string::size_type at = raised.find("\t4242\n");
    if (at == std::string::npos) { Fail("T-985 C1 fold 1: the seq is not the header's fifth field"); return; }
    raised.replace(at, 6, "\t9999\n");
    if (mirrorkeep::TableParseSeq(raised, "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad, &seq) != mirrorkeep::kTableTorn) Fail("T-985 C1 fold 1: a changed seq passed the checksum");
    if (!mirrorkeep::TableFormat("wid-1", "aaaaaaaaaaaaaaaa", in, &text, 0) || text.substr(0, text.find('\n')) != "swkept\t1\twid-1\taaaaaaaaaaaaaaaa")
        Fail("T-985 C1 fold 1: the C1 header changed");
    seq = 7;
    if (mirrorkeep::TableParseSeq(text, "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad, &seq) != mirrorkeep::kTableOk || seq != 0ULL) Fail("T-985 C1 fold 1: a C1 table was not read with no seq");
    std::string body = "swkept\t1\twid-1\taaaaaaaaaaaaaaaa\tx\n"; body += "end\t0\t" + mirrorkeep::Hex16(mirrorkeep::Fnv64(body)) + "\n";
    std::string zero = "swkept\t1\twid-1\taaaaaaaaaaaaaaaa\t0\n"; zero += "end\t0\t" + mirrorkeep::Hex16(mirrorkeep::Fnv64(zero)) + "\n";
    if (mirrorkeep::TableParseSeq(body, "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad, &seq) != mirrorkeep::kTableBadHeader
        || mirrorkeep::TableParseSeq(zero, "wid-1", "aaaaaaaaaaaaaaaa", &out, &bad, &seq) != mirrorkeep::kTableBadHeader)
        Fail("T-985 C1 fold 1: a header with an unreadable or zero seq was read");
    if (std::string(mirrorkeep::WhyName(mirrorkeep::kWhyServerBehind)).compare(0, 12, "serverBehind") != 0) Fail("T-985 C1 fold 1: the serverBehind reason");
}
/* T-985 C1: the unnamed-key drop runs only for a snapshot seen whole - since T-985 C2b fold 6 the LAST PAGE_END's complete, nothing else */
void t_t985c1f2_snapshot_whole()
{
    if (mirrorkeep::SnapshotWholeForDrop(1, 1) != 1) Fail("T-985 C2b fold 6: a LAST page that said complete was not taken as whole");
    if (mirrorkeep::SnapshotWholeForDrop(1, 0) != 0) Fail("T-985 C2b fold 6: a LAST page that said NOT complete was taken as whole");
    if (mirrorkeep::SnapshotWholeForDrop(0, 1) != 0 || mirrorkeep::SnapshotWholeForDrop(0, 0) != 0) Fail("T-985 C2b fold 6: a page that is not the last was taken as a whole snapshot");
    if (mirrorkeep::UnnamedDecide(0, 0, 1, 50, 50) != mirrorkeep::kUnnamedKeep) Fail("T-985 C1: a snapshot not seen whole dropped an unnamed key");
}
/* T-985 C2b fold 6: the world server judges the snapshot whole (coopfeed::FeedSnapshotComplete) and says so on the LAST PAGE_END; the
   game's only test is that flag (mirrorkeep::SnapshotWholeForDrop). The world server's paging is run here on a map, three records a
   page, through the wire, as store_main.cpp OnRecordFeed runs it: FeedPageWalk, FeedGoneTake, the refused sends, the cut. */
struct T985f6Send
{
    const std::set<std::string>* refuse; unsigned int failed;
    T985f6Send() : refuse(0), failed(0) {}
    size_t operator()(const std::string& k, const int&) { if (refuse != 0 && refuse->count(k) != 0) ++failed; return 1; }
};
struct T985f6Feed
{
    std::map<std::string, int> live; std::set<std::string> beginKeys, refuse; std::string cursor;
    long long begin, sent, gone, lastSent, sendFailed; unsigned int lastFlag, complete; bool hasList, done;
    T985f6Feed() : begin(0), sent(0), gone(0), lastSent(0), sendFailed(0), lastFlag(0), complete(0), hasList(false), done(false) {}
    void Fill(const char* keys) { live.clear(); for (const char* k = keys; *k != 0; ++k) live[std::string(1, *k)] = 1; }
    void Begin()   /* the first-page ASK: the BEGIN's key list is taken and every total starts over (world server and game) */
    {
        beginKeys.clear();
        for (std::map<std::string, int>::const_iterator k = live.begin(); k != live.end(); ++k) beginKeys.insert(k->first);
        hasList = true; begin = (long long)live.size(); sent = 0; gone = 0; lastSent = 0; sendFailed = 0; lastFlag = 0; complete = 0; cursor.clear(); done = false;
    }
    bool Page()   /* one ASK's answer after cursor, read back from its PAGE_END as the game reads it */
    {
        T985f6Send s; s.refuse = &refuse; std::string lk; unsigned int n = 0;
        bool last = coopfeed::FeedPageWalk(live, cursor, (size_t)1 << 30, 3, s, &lk, &n);
        unsigned int g = hasList ? coopfeed::FeedGoneTake(&beginKeys, live, cursor, lk, last) : 0u;
        sendFailed += (long long)s.failed;
        const bool cut = lk.size() > coopfeed::kFeedKeyMax;
        unsigned int c = coopfeed::FeedSnapshotComplete(last, hasList, beginKeys.size(), cut, sendFailed);
        std::vector<char> pe; coopfeed::FeedMsg m;
        if (!coopfeed::FeedEncodePageEnd(&pe, lk, n, last, 1ULL, g, c))
        {   /* the world server's answer to a key over the wire's bound: an empty LAST page, never complete */
            pe.clear(); g = 0; c = 0; last = true;
            if (!coopfeed::FeedEncodePageEnd(&pe, std::string(), 0, true, 1ULL)) return false;
        }
        if (!coopfeed::FeedDecode(&pe[0], pe.size(), &m) || m.gone != g || m.complete != c || (m.last != 0) != last) return false;
        sent += (long long)m.sent; gone += (long long)m.gone; lastSent = (long long)m.sent; lastFlag = m.last; complete = m.complete; cursor = m.key; done = m.last != 0;
        return true;
    }
    bool Run() { for (int k = 0; k < 100 && !done; ++k) if (!Page()) return false; return done; }
    int Whole() const { return mirrorkeep::SnapshotWholeForDrop(lastFlag, complete); }
    bool CountSaysWhole() const { return sent + gone >= begin && !(lastSent == 0 && sent > 0); }   /* fold 5's count rule, kept here only to show it fooled */
};
void t_t985c2bf6_feed_complete()
{
    {   /* the rule's arms */
        if (coopfeed::FeedSnapshotComplete(true, true, 0, false, 0) != 1u) Fail("fold 6: a whole snapshot was not complete");
        if (coopfeed::FeedSnapshotComplete(false, true, 0, false, 0) != 0u) Fail("fold 6: a page that is not the last said complete");
        if (coopfeed::FeedSnapshotComplete(true, false, 0, false, 0) != 0u) Fail("fold 6: a feed with no BEGIN key list said complete");
        if (coopfeed::FeedSnapshotComplete(true, true, 1, false, 0) != 0u) Fail("fold 6: a BEGIN key no page judged was left and the snapshot said complete");
        if (coopfeed::FeedSnapshotComplete(true, true, 0, true, 0) != 0u) Fail("fold 6: a cut page said complete");
        if (coopfeed::FeedSnapshotComplete(true, true, 0, false, 1) != 0u) Fail("fold 6: a refused record send said complete");
    }
    {   /* all paged: seven records in three pages; an empty world */
        T985f6Feed f; f.Fill("abcdefg"); f.Begin();
        if (!f.Run() || f.sent != 7 || f.gone != 0 || f.Whole() != 1) Fail("fold 6: a snapshot with every record paged was not whole");
        T985f6Feed e; e.Begin();
        if (!e.Run() || e.sent != 0 || e.Whole() != 1) Fail("fold 6: an empty world's snapshot was not whole");
    }
    {   /* one deleted mid-feed, before its page reached it: whole (the delete is reported as gone) */
        T985f6Feed f; f.Fill("abcdefg"); f.Begin();
        if (!f.Page()) Fail("fold 6: a page did not go through the wire");
        f.live.erase("e");
        if (!f.Run() || f.sent != 6 || f.gone != 1 || f.Whole() != 1) Fail("fold 6: a snapshot with a record deleted mid-feed was not whole");
    }
    {   /* a delete of a key already paged: whole, nothing reported gone */
        T985f6Feed f; f.Fill("abcdefg"); f.Begin();
        if (!f.Page()) Fail("fold 6: a page did not go through the wire");
        f.live.erase("b");
        if (!f.Run() || f.sent != 7 || f.gone != 0 || f.Whole() != 1) Fail("fold 6: a delete of a key already paged changed the judgement");
    }
    {   /* fold 5's LOW 1: a create after the BEGIN plus one refused send - any count is fooled, the world server's flag is not */
        T985f6Feed f; f.Fill("abcdefg"); f.Begin();
        if (!f.Page()) Fail("fold 6: a page did not go through the wire");
        f.live["dd"] = 1; f.refuse.insert("f");
        if (!f.Run() || f.sent != 8 || f.gone != 0 || !f.CountSaysWhole() || f.sendFailed != 1 || f.Whole() != 0) Fail("fold 6: a refused record send hidden by a create was taken as whole");
    }
    {   /* a refused send alone */
        T985f6Feed f; f.Fill("abc"); f.refuse.insert("a"); f.Begin();
        if (!f.Run() || f.Whole() != 0) Fail("fold 6: a snapshot with a refused record send was taken as whole");
    }
    {   /* a cut page: a key over the wire's bound ends the feed with an empty LAST page; the keys after it are never judged */
        T985f6Feed f; f.Fill("abde"); f.live["c" + std::string(coopfeed::kFeedKeyMax, 'z')] = 1; f.Begin();
        if (!f.Run() || f.lastFlag != 1u || f.lastSent != 0 || f.Whole() != 0 || f.beginKeys.size() != 2u) Fail("fold 6: a cut page mid-feed was taken as whole");
        T985f6Feed t; t.Fill("ab"); t.live[std::string(coopfeed::kFeedKeyMax + 1, 'z')] = 1; t.Begin();
        if (!t.Run() || !t.beginKeys.empty() || t.Whole() != 0) Fail("fold 6: a cut LAST page was taken as whole although every BEGIN key was judged");
    }
    {   /* fold 5's LOW 2: a create then delete after a page's last key - an empty LAST page with nothing gone is whole */
        T985f6Feed f; f.Fill("abcdef"); f.Begin();
        if (!f.Page()) Fail("fold 6: a page did not go through the wire");
        f.live["g"] = 1;
        if (!f.Page() || f.done) Fail("fold 6: the page before the delete ended the feed");
        f.live.erase("g");
        if (!f.Run() || f.sent != 6 || f.gone != 0 || f.lastSent != 0 || f.CountSaysWhole() || f.Whole() != 1) Fail("fold 6: an empty LAST page after a create then delete was not whole");
    }
    {   /* a re-asked range counts nothing twice and the snapshot stays whole; a skipped range leaves BEGIN keys and is not */
        T985f6Feed f; f.Fill("abcdefg"); f.Begin();
        if (!f.Page() || !f.Page()) Fail("fold 6: a page did not go through the wire");
        f.cursor = "c";
        if (!f.Run() || f.sent != 10 || f.gone != 0 || f.Whole() != 1) Fail("fold 6: a re-asked range changed the judgement");
        T985f6Feed s; s.Fill("abcdefg"); s.Begin();
        if (!s.Page()) Fail("fold 6: a page did not go through the wire");
        s.cursor = "e";
        if (!s.Run() || s.beginKeys.size() != 2u || s.Whole() != 0) Fail("fold 6: a snapshot whose ASKs skipped BEGIN keys was taken as whole");
    }
    {   /* an ASK with no BEGIN key list behind it (the list freed at OFF, the last page or a disconnect) is never complete */
        T985f6Feed f; f.Fill("abc");
        if (!f.Run() || f.Whole() != 0) Fail("fold 6: a feed with no BEGIN key list was taken as whole");
    }
    {   /* T1533's two-page shape: a record of the second page's range deleted between the BEGIN and that page */
        T985f6Feed f; f.Fill("abcde"); f.Begin();
        if (!f.Page()) Fail("fold 6: a page did not go through the wire");
        f.live.erase("e");
        if (!f.Run() || f.begin != 5 || f.sent != 4 || f.gone != 1 || f.Whole() != 1) Fail("fold 6: T1533's two-page snapshot with a record deleted mid-feed was not whole");
    }
    {   /* a record deleted then created again before its page: paged, whole, nothing gone */
        T985f6Feed f; f.Fill("abcdefg"); f.Begin();
        if (!f.Page()) Fail("fold 6: a page did not go through the wire");
        f.live.erase("e"); f.live["e"] = 1;
        if (!f.Run() || f.sent != 7 || f.gone != 0 || f.Whole() != 1) Fail("fold 6: a record deleted and created again before its page changed the judgement");
    }
    {   /* FeedGoneTake: a range judged once counts nothing again; a range that runs backwards is not judged */
        std::set<std::string> bk; bk.insert("a"); bk.insert("b"); bk.insert("c");
        std::map<std::string, int> now; now["a"] = 1; now["c"] = 1;
        if (coopfeed::FeedGoneTake(&bk, now, std::string(), "c", false) != 1u || !bk.empty() || coopfeed::FeedGoneTake(&bk, now, std::string(), "c", false) != 0u)
            Fail("fold 5: a range was counted twice");
        bk.insert("x");
        if (coopfeed::FeedGoneTake(&bk, now, "z", "c", false) != 0u || bk.size() != 1u) Fail("fold 5: a page range that runs backwards was judged");
    }
    {   /* the wire: PAGE_END {.., u32 gone, u8 last, u8 complete, u64 seqHigh}; complete only on the LAST page; the fold 5 form no longer decodes */
        std::vector<char> b; coopfeed::FeedMsg m;
        if (!coopfeed::FeedEncodePageEnd(&b, "k", 5, true, 9ULL, 3, 1) || !coopfeed::FeedDecode(&b[0], b.size(), &m) || m.sent != 5u || m.gone != 3u || m.last != 1u || m.complete != 1u || m.seqHigh != 9ULL)
            Fail("fold 6: a PAGE_END with complete did not round-trip");
        std::vector<char> nl;
        if (!coopfeed::FeedEncodePageEnd(&nl, "k", 5, false, 9ULL, 0, 1) || !coopfeed::FeedDecode(&nl[0], nl.size(), &m) || m.complete != 0u || m.last != 0u)
            Fail("fold 6: a page that is not the last carried complete");
        std::vector<char> bad(b); bad[bad.size() - 10] = 0;
        if (coopfeed::FeedDecode(&bad[0], bad.size(), &m)) Fail("fold 6: complete on a page that is not the last decoded");
        std::vector<char> two(b); two[two.size() - 9] = 2;
        if (coopfeed::FeedDecode(&two[0], two.size(), &m)) Fail("fold 6: a complete byte over 1 decoded");
        std::vector<char> old(b); old.erase(old.begin() + (old.size() - 9));
        if (coopfeed::FeedDecode(&old[0], old.size(), &m)) Fail("fold 6: a PAGE_END without complete (the fold 5 form) decoded");
    }
    {   /* the sources: the game judges by the flag alone and starts its counts over at the first ask; the world server judges, counts
           refused sends, and keeps the BEGIN key list per game only while that snapshot runs */
        const std::string game = T313ReadFile("../coop-plugin/store.cpp"), server = T313ReadFile("../coop-store/store_main.cpp");
        if (game.empty() || server.empty()) { Fail("fold 6: could not read the plugin's store.cpp or the world server's store_main.cpp"); return; }
        if (game.find("const int whole = mirrorkeep::SnapshotWholeForDrop(m.last, m.complete);") == std::string::npos
            || game.find("MirrorKeepSnapshotEnd(whole, g_indexExpected, g_feedSnapshotSent, g_feedSnapshotGone);") == std::string::npos
            || game.find("MirrorKeepStaleHoldEnd(whole);") == std::string::npos || game.find("PreTownWaitEnd(whole);") == std::string::npos
            || game.find("SnapshotWholeForDrop(g_indexExpected") != std::string::npos || game.find("SnapshotWholeByGoneOnly") != std::string::npos
            || game.find("SnapshotWholeForDrop(beginTotal") != std::string::npos)
            Fail("fold 6: a snapshot END is judged by something other than the LAST PAGE_END's complete");
        if (game.find("++g_feedAsksFirst; g_snapshotApplied = false; g_feedSnapshotSent = 0; g_feedSnapshotGone = 0;") == std::string::npos)
            Fail("fold 6: the game does not start its snapshot counts over at the first ask");
        if (server.find("complete = coopfeed::FeedSnapshotComplete(last, bkc != g_feedBeginKeys.end()") == std::string::npos
            || server.find("coopfeed::FeedEncodePageEnd(&pe, lastKey, sent, last, seqHigh, gone, complete)") == std::string::npos
            || server.find("gone = coopfeed::FeedGoneTake(&bk->second, g_records, m.key, lastKey, last);") == std::string::npos
            || server.find("if (!taken) ++failed;") == std::string::npos || server.find("if (!aoiSentOk) ++failed;") == std::string::npos
            || server.find("tot.sendFailed += (long long)snd.failed;") == std::string::npos)
            Fail("fold 6: the world server does not judge the snapshot (complete, refused sends) into its LAST PAGE_END");
        if (server.find("std::set<std::string>& bk = g_feedBeginKeys[from]; bk.clear();") == std::string::npos)
            Fail("fold 6: the world server does not clear the BEGIN key list on a new start");
        if (server.find("g_feedSnap.erase(from); g_feedBeginKeys.erase(from);   /* its world is going away") == std::string::npos)
            Fail("fold 6: the world server does not free the BEGIN key list on stop-feed (OFF)");
        if (server.find("g_feedSnap.erase(from); g_feedBeginKeys.erase(from);   /* the snapshot is out") == std::string::npos)
            Fail("fold 6: the world server does not free the BEGIN key list at the last page");
        if (server.find("g_feedSnap.erase(p); g_feedBeginKeys.erase(p);   /* and the stamps it said it holds") == std::string::npos)
            Fail("fold 6: the world server does not free the BEGIN key list with the connection");
        if (game.find("PreTownWaitRefusedSay(\"the wait has ended by its time bound\");") == std::string::npos) Fail("fold 5: the town wait's time-bound end does not name the refused towns");
    }
}
/* T-985 C1: the kept table's stored seq is judged at the first WELCOME, with or without its world numbers */
void t_t985c1f2_seq_judge()
{
    if (mirrorkeep::SeqJudge(0ULL, 0, 0ULL) != mirrorkeep::kSeqKeep || mirrorkeep::SeqJudge(0ULL, 1, 5ULL) != mirrorkeep::kSeqKeep) Fail("T-985 C1: a table with no stored seq was refused");
    if (mirrorkeep::SeqJudge(10ULL, 1, 9ULL) != mirrorkeep::kSeqRefuseBehind) Fail("T-985 C1: a world server behind the table was not refused");
    if (mirrorkeep::SeqJudge(10ULL, 1, 10ULL) != mirrorkeep::kSeqKeep || mirrorkeep::SeqJudge(10ULL, 1, 11ULL) != mirrorkeep::kSeqKeep) Fail("T-985 C1: a world server level with or ahead of the table was refused");
    if (mirrorkeep::SeqJudge(10ULL, 0, 0ULL) != mirrorkeep::kSeqRefuseUnjudged) Fail("T-985 C1: a table with a stored seq was used with no world numbers to judge it");
    if (std::string(mirrorkeep::WhyName(mirrorkeep::kWhyUnjudged)).compare(0, 9, "unjudged:") != 0) Fail("T-985 C1: the unjudged reason");
}
/* T-985 C1: MirrorKeepWrite's rows keep their own file's stamp through the name-clash filter */
void t_t985c1f2_list_unclashed()
{
    std::vector<mirrorkeep::KeptRow> rows; std::vector<std::string> names;
    mirrorkeep::KeptRow a = T985c1Row("Group_1", 10); mirrorkeep::RowStamp(&a, 100ULL, 1000ULL); rows.push_back(a); names.push_back("C:\\w\\mirror-k1\\Group_1.platoon");
    mirrorkeep::KeptRow b = T985c1Row("group_1", 20); mirrorkeep::RowStamp(&b, 200ULL, 2000ULL); rows.push_back(b); names.push_back("C:\\w\\mirror-k1\\group_1.platoon");
    mirrorkeep::KeptRow c = T985c1Row("Other_2", 30); mirrorkeep::RowStamp(&c, 300ULL, 3000ULL); rows.push_back(c); names.push_back("C:\\w\\mirror-k1\\Other_2.platoon");
    mirrorkeep::KeptRow d = T985c1Row("Third_3", 40); mirrorkeep::RowStamp(&d, 400ULL, 4000ULL); rows.push_back(d); names.push_back("C:\\w\\mirror-k1\\Third_3.platoon");
    if (a.size != 100ULL || a.mtime != 1000ULL || a.crc != 0u) Fail("T-985 C1: RowStamp did not set its own file's size and time (and crc 0)");
    std::vector<mirrorkeep::KeptRow> listed; std::vector<size_t> clashAt;
    if (mirrorkeep::ListUnclashed(rows, names, &listed, &clashAt) != 2 || clashAt.size() != 2 || clashAt[0] != 0u || clashAt[1] != 1u)
    { Fail("T-985 C1: the two clashing rows were not the ones left out"); return; }
    if (listed.size() != 2 || listed[0].key != "Other_2" || listed[0].size != 300ULL || listed[0].mtime != 3000ULL
        || listed[1].key != "Third_3" || listed[1].size != 400ULL || listed[1].mtime != 4000ULL)
        Fail("T-985 C1: a listed row lost its own file's stamp or its order");
    names.pop_back();
    if (mirrorkeep::ListUnclashed(rows, names, &listed, &clashAt) != -1 || !listed.empty()) Fail("T-985 C1: rows and file names out of step were listed");
}
/* T-985 C1: a kept row for a key LoadIndex already gave a record is held beside it, and swapped in only when the key is named and newer */
void t_t985c1f2_kept_beside()
{
    if (mirrorkeep::KeptPlace(mirrorkeep::kLoadTaken, 1, 0) != mirrorkeep::kKeptBeside) Fail("T-985 C1: a kept row replaced the index-load record of its key");
    if (mirrorkeep::KeptPlace(mirrorkeep::kLoadTaken, 0, 0) != mirrorkeep::kKeptReplace) Fail("T-985 C1: a kept row with no index-load record was not taken");
    if (mirrorkeep::KeptPlace(mirrorkeep::kLoadTaken, 1, 1) != mirrorkeep::kKeptReplace) Fail("T-985 C1: an area row was held beside (it shares the index-load record's mirror file)");
    if (mirrorkeep::KeptPlace(mirrorkeep::kLoadNotNewer, 1, 0) != mirrorkeep::kKeptNotTaken || mirrorkeep::KeptPlace(mirrorkeep::kLoadSkipped, 0, 0) != mirrorkeep::kKeptNotTaken
        || mirrorkeep::KeptPlace(mirrorkeep::kLoadRejected, 1, 0) != mirrorkeep::kKeptNotTaken)
        Fail("T-985 C1: a row not taken was placed");
    if (mirrorkeep::BesideSwapIn(1, 10, 20) != 1) Fail("T-985 C1: a named kept row newer than the record here was not swapped in");
    if (mirrorkeep::BesideSwapIn(1, 20, 20) != 0 || mirrorkeep::BesideSwapIn(1, 30, 20) != 0) Fail("T-985 C1: a kept row replaced a record as new or newer");
    if (mirrorkeep::BesideSwapIn(0, 0, 20) != 0) Fail("T-985 C1: a kept row came back for a key gone here");
}
/* T-985 C1: a refused table keeps the keys the current snapshot already named */
void t_t985c1f3_refuse_named()
{
    if (mirrorkeep::RefuseDrops(1, 50, 50, 0) != 1) Fail("T-985 C1: an unnamed key still the table's copy was kept from a refused table");
    if (mirrorkeep::RefuseDrops(1, 50, 50, 1) != 0) Fail("T-985 C1: a key the snapshot named was dropped by the refusal");
    if (mirrorkeep::RefuseDrops(1, 60, 50, 0) != 0 || mirrorkeep::RefuseDrops(0, 0, 50, 0) != 0) Fail("T-985 C1: a re-fetched or gone key was dropped by the refusal");
}
/* T-985: the area file's text with a correct end line, for the refusal cases */
std::string T985AreaText(const std::string& above, unsigned long long rows)
{
    return above + "end\t" + mirrorkeep::U64Text(rows) + "\t" + mirrorkeep::Hex16(mirrorkeep::Fnv64(above)) + "\n";
}
/* T-985: the area file - player areas in order (followed first), each area once, loaded areas not already listed; written and read back */
void t_t985_area_file_round_trip()
{
    const int ps[] = { 5, 5, 6, 5, 5, 5, 70, 1, -1, 3 };   /* a repeat, one off the grid, one unknown */
    const int ld[] = { 6, 5, 7, 7, 7, 7, 8, 8, 5, 5 };     /* two player areas again, a repeat */
    areafile::Content c;
    areafile::Build(ps, 5, ld, 5, &c);
    if (c.player.size() != 4 || c.player[0] != 5 || c.player[1] != 5 || c.player[2] != 6 || c.player[3] != 5) Fail("T-985: the player areas lost their order or a repeat stayed");
    if (c.loaded.size() != 4 || c.loaded[0] != 7 || c.loaded[1] != 7 || c.loaded[2] != 8 || c.loaded[3] != 8) Fail("T-985: a loaded area that is a player area (or a repeat) was listed again");
    std::string text;
    if (!areafile::Format("world-1", "00112233aabbccdd", 1234567ULL, c, &text)) { Fail("T-985: the area file was not formatted"); return; }
    areafile::Content back; unsigned long long stamp = 0;
    if (areafile::Parse(text, "world-1", "00112233aabbccdd", &back, &stamp) != areafile::kOk || stamp != 1234567ULL || areafile::SameContent(back, c) == 0)
        Fail("T-985: the area file did not read back as written");
    areafile::Content none; areafile::Build(0, 0, 0, 0, &none);
    if (!areafile::Format("world-1", "h", 1ULL, none, &text) || areafile::Parse(text, "world-1", "h", &back, 0) != areafile::kOk || !back.player.empty() || !back.loaded.empty())
        Fail("T-985: an empty area file did not read back empty");
    if (areafile::Format("", "h", 1ULL, c, &text) || areafile::Format("w\t1", "h", 1ULL, c, &text) || areafile::Format("w", "", 1ULL, c, &text))
        Fail("T-985: an area file was formatted with no (or a bad) world id or identity");
    const int follow[] = { 9, 9, 1, 1, 9, 9 };
    const int loadFollow[] = { 1, 1, 9, 9, 2, 2 };
    areafile::Build(follow, 3, loadFollow, 3, &c);
    if (c.player.size() != 4 || c.player[0] != 9 || c.player[1] != 9 || c.loaded.size() != 2 || c.loaded[0] != 2) Fail("T-985: the followed character's area is not first, or was listed twice");
    if (areafile::Rows(c, 0) != "p\t9\t9\np\t1\t1\nl\t2\t2\n") Fail("T-985: the area file's rows are not p before l in list order");
}
/* T-985: an area file of another world or player, torn, damaged or with a bad row is refused whole */
void t_t985_area_file_refused()
{
    const int ps[] = { 3, 4, 10, 11 };
    const int ld[] = { 3, 5 };
    areafile::Content c; areafile::Build(ps, 2, ld, 1, &c);
    std::string good;
    if (!areafile::Format("world-1", "hash-1", 77ULL, c, &good)) { Fail("T-985: the area file was not formatted"); return; }
    areafile::Content back;
    if (areafile::Parse(good, "world-2", "hash-1", &back, 0) != areafile::kOtherWorld || !back.player.empty()) Fail("T-985: another world's area file was not refused");
    if (areafile::Parse(good, "world-1", "hash-2", &back, 0) != areafile::kOtherPlayer || !back.player.empty()) Fail("T-985: another player's area file was not refused");
    if (areafile::Parse("", "world-1", "hash-1", &back, 0) != areafile::kBadHeader || areafile::Parse("swkept\t1\tworld-1\thash-1\t77\nend\t0\tx\n", "world-1", "hash-1", &back, 0) != areafile::kBadHeader)
        Fail("T-985: an empty file or a record table read as an area file");
    for (size_t cut = good.find('\n') + 1; cut < good.size(); cut += 3)
        if (areafile::Parse(good.substr(0, cut), "world-1", "hash-1", &back, 0) == areafile::kOk || !back.player.empty()) { Fail("T-985: a truncated area file was taken"); break; }
    std::string bad = good;
    const size_t row = bad.find("p\t10\t11");
    if (row == std::string::npos) { Fail("T-985: the area file has no row for 10,11"); return; }
    bad[row + 2] = '2';   /* p 20 11: the checksum no longer matches */
    if (areafile::Parse(bad, "world-1", "hash-1", &back, 0) != areafile::kTorn || !back.player.empty()) Fail("T-985: an area file with a wrong checksum was taken");
    const std::string head = "swareas\t1\tworld-1\thash-1\t77\n";
    if (areafile::Parse(T985AreaText(head + "p\t1\t1\np\t1\t1\n", 2), "world-1", "hash-1", &back, 0) != areafile::kTorn) Fail("T-985: an area file naming an area twice was taken");
    if (areafile::Parse(T985AreaText(head + "p\t1\t1\nl\t1\t1\n", 2), "world-1", "hash-1", &back, 0) != areafile::kTorn) Fail("T-985: an area file naming an area as player and loaded was taken");
    if (areafile::Parse(T985AreaText(head + "p\t64\t1\n", 1), "world-1", "hash-1", &back, 0) != areafile::kTorn) Fail("T-985: an area file with an area off the grid was taken");
    if (areafile::Parse(T985AreaText(head + "q\t1\t1\n", 1), "world-1", "hash-1", &back, 0) != areafile::kTorn) Fail("T-985: an area file with an unknown row was taken");
    if (areafile::Parse(T985AreaText(head + "p\t1\t1\n", 2), "world-1", "hash-1", &back, 0) != areafile::kTorn) Fail("T-985: an area file whose row count is wrong was taken");
    if (areafile::Parse(T985AreaText(head + "p\t1\t1\n", 1), "world-1", "hash-1", &back, 0) != areafile::kOk || back.player.size() != 2) Fail("T-985: a well-made hand-written area file was refused");
}
/* T-985: the measure - live player areas missing from the file, beyond its ring 2, and file areas in neither live list */
void t_t985_area_file_missing()
{
    std::vector<int> file, live;
    file.push_back(10); file.push_back(10); file.push_back(20); file.push_back(20);
    live.push_back(10); live.push_back(10); live.push_back(12); live.push_back(11); live.push_back(30); live.push_back(30);
    if (areafile::CountNotIn(live, file) != 2) Fail("T-985: live player areas missing from the file were miscounted");
    if (areafile::CountBeyondRing(live, file, areafile::kRing) != 1) Fail("T-985: an area two squares from a file area is not inside its ring 2 (or one far away is)");
    if (areafile::CountNotIn(file, live) != 1) Fail("T-985: file areas not in the live lists were miscounted");
    std::vector<int> empty;
    if (areafile::CountNotIn(live, empty) != 3 || areafile::CountBeyondRing(live, empty, areafile::kRing) != 3 || areafile::RingDistance(1, 1, empty) != -1)
        Fail("T-985: an empty file did not count every live area missing");
    if (areafile::RingDistance(13, 10, file) != 3 || areafile::InRing(12, 12, file, 2) != 1 || areafile::InRing(13, 13, file, 2) != 0) Fail("T-985: the ring distance is not Chebyshev");
    areafile::Content a, b;
    const int p1[] = { 1, 1, 2, 2 };
    const int p2[] = { 2, 2, 1, 1 };
    areafile::Build(p1, 2, 0, 0, &a); areafile::Build(p2, 2, 0, 0, &b);
    if (areafile::SameContent(a, b) != 0) Fail("T-985: a new followed character (the order changed) did not count as a change");
    areafile::Build(p1, 2, 0, 0, &b);
    if (areafile::SameContent(a, b) == 0 || areafile::Rows(a, 0) != areafile::Rows(b, 0)) Fail("T-985: the same lists counted as a change");
}
/* T-1175: the area file with and without the last save's list - version 2 only with one, version 1 byte for byte as before */
void t_t1175_area_file_save_list()
{
    const int ps[] = { 44, 11 };
    const int ld[] = { 44, 12, 20, 32 };
    const int sps[] = { 20, 32, 44, 11 };   /* the save's player areas: one is also a live area */
    const int sld[] = { 21, 32 };
    areafile::Content live, save, back, backSave;
    areafile::Build(ps, 1, ld, 2, &live); areafile::Build(sps, 2, sld, 1, &save);
    std::string v1, v1old, v2;
    unsigned long long stamp = 0; unsigned int ver = 9u;
    if (!areafile::FormatWithSave("world-1", "hash-1", 55ULL, live, 0, &v1) || !areafile::Format("world-1", "hash-1", 55ULL, live, &v1old) || v1 != v1old)
        { Fail("T-1175: an area file with no save list is not written as before"); return; }
    areafile::Content none;
    if (!areafile::FormatWithSave("world-1", "hash-1", 55ULL, live, &none, &v2) || v2 != v1) Fail("T-1175: an empty save list changed the file");
    if (v1.compare(0, 10, "swareas\t1\t") != 0) Fail("T-1175: a file with no save list is not version 1");
    if (areafile::ParseWithSave(v1, "world-1", "hash-1", &back, &backSave, &stamp, &ver) != areafile::kOk || ver != 1u || areafile::HasAreas(backSave) != 0
        || areafile::SameContent(back, live) == 0 || stamp != 55ULL)
        Fail("T-1175: a version 1 file did not read as its live lists and no save list");
    if (!areafile::FormatWithSave("world-1", "hash-1", 66ULL, live, &save, &v2)) { Fail("T-1175: the area file with a save list was not formatted"); return; }
    if (v2.compare(0, 10, "swareas\t2\t") != 0 || v2.find("sp\t20\t32\nsp\t44\t11\nsl\t21\t32\nend\t6\t") == std::string::npos)
        Fail("T-1175: the save list's rows are not sp then sl after the live rows, counted in the end line");
    if (areafile::ParseWithSave(v2, "world-1", "hash-1", &back, &backSave, &stamp, &ver) != areafile::kOk || ver != 2u || stamp != 66ULL
        || areafile::SameContent(back, live) == 0 || areafile::SameContent(backSave, save) == 0)
        Fail("T-1175: the area file with a save list did not read back as written");
    if (areafile::Parse(v2, "world-1", "hash-1", &back, 0) != areafile::kOk || areafile::SameContent(back, live) == 0) Fail("T-1175: the live lists of a version 2 file did not read alone");
    /* an old file, written by hand as the earlier build wrote it */
    const std::string head1 = "swareas\t1\tworld-1\thash-1\t77\n", head2 = "swareas\t2\tworld-1\thash-1\t77\n";
    if (areafile::ParseWithSave(T985AreaText(head1 + "p\t3\t3\nl\t4\t4\n", 2), "world-1", "hash-1", &back, &backSave, 0, &ver) != areafile::kOk || ver != 1u
        || back.player.size() != 2 || back.loaded.size() != 2 || !backSave.player.empty() || !backSave.loaded.empty())
        Fail("T-1175: an old area file did not read as no save list");
    if (areafile::ParseWithSave(T985AreaText(head1 + "p\t3\t3\nsp\t4\t4\n", 2), "world-1", "hash-1", &back, &backSave, 0, 0) != areafile::kTorn)
        Fail("T-1175: a version 1 file with a save row was taken");
    if (areafile::ParseWithSave(T985AreaText(head2 + "p\t3\t3\n", 1), "world-1", "hash-1", &back, &backSave, 0, 0) != areafile::kTorn)
        Fail("T-1175: a version 2 file with no save list was taken");
    if (areafile::ParseWithSave(T985AreaText(head2 + "p\t3\t3\nsp\t3\t3\n", 2), "world-1", "hash-1", &back, &backSave, 0, 0) != areafile::kOk || backSave.player.size() != 2)
        Fail("T-1175: a save area that is also a live area was refused");
    if (areafile::ParseWithSave(T985AreaText(head2 + "p\t3\t3\nsp\t5\t5\nsl\t5\t5\n", 3), "world-1", "hash-1", &back, &backSave, 0, 0) != areafile::kTorn
        || !back.player.empty() || !backSave.player.empty())
        Fail("T-1175: a save list naming an area twice was taken (or a refusal left lists behind)");
    if (areafile::ParseWithSave(T985AreaText("swareas\t3\tworld-1\thash-1\t77\np\t3\t3\n", 1), "world-1", "hash-1", &back, &backSave, 0, 0) != areafile::kBadHeader)
        Fail("T-1175: an area file of an unknown version was taken");
    std::string bad = v2;
    const size_t row = bad.find("sp\t20\t32");
    if (row == std::string::npos) { Fail("T-1175: no save row for 20,32"); return; }
    bad[row + 3] = '3';   /* sp 30 32: the checksum no longer matches */
    if (areafile::ParseWithSave(bad, "world-1", "hash-1", &back, &backSave, 0, 0) != areafile::kTorn) Fail("T-1175: a damaged save row was taken");
}
/* T-1175: the load's area list is the live lists and the last save's list - a record within ring 2 of the save's area is fetched,
   one near neither is not */
void t_t1175_area_list_union()
{
    const int ps[] = { 44, 11 };
    const int ld[] = { 45, 11 };
    const int sps[] = { 20, 32, 44, 11 };
    const int sld[] = { 45, 11, 19, 32 };
    areafile::Content live, save, empty;
    areafile::Build(ps, 1, ld, 1, &live); areafile::Build(sps, 2, sld, 2, &save);
    std::vector<int> xy; long long fromLive = -1, fromSave = -1;
    areafile::UnionAreas(live, save, &xy, &fromLive, &fromSave);
    if (fromLive != 2 || fromSave != 2 || xy.size() != 8) { Fail("T-1175: the union did not name each area once, by the list it came from"); return; }
    if (xy[0] != 44 || xy[1] != 11 || xy[2] != 20 || xy[3] != 32 || xy[4] != 45 || xy[5] != 11 || xy[6] != 19 || xy[7] != 32)
        Fail("T-1175: the union's player areas do not come first (live, then the save's)");
    if (cooppre::PreAoiClass(22, 34, xy, cooppre::kPreRing) != cooppre::kPreAoiIn) Fail("T-1175: a record within ring 2 of the save's area is not fetched");
    if (cooppre::PreAoiClass(43, 13, xy, cooppre::kPreRing) != cooppre::kPreAoiIn) Fail("T-1175: a record within ring 2 of the live area is not fetched");
    if (cooppre::PreAoiClass(30, 20, xy, cooppre::kPreRing) != cooppre::kPreAoiOut) Fail("T-1175: a record near neither list was fetched");
    areafile::UnionAreas(live, empty, &xy, &fromLive, &fromSave);
    if (fromLive != 2 || fromSave != 0 || cooppre::PreAoiClass(22, 34, xy, cooppre::kPreRing) != cooppre::kPreAoiOut)
        Fail("T-1175: with no save list the area list is not the live lists alone");
    areafile::UnionAreas(empty, save, &xy, &fromLive, &fromSave);
    if (fromLive != 0 || fromSave != 4) Fail("T-1175: a save list alone did not make the area list");
}
/* T-1175: the save list is taken only at this world's own finished save with lists noted, kept through later live writes, and
   replaced at the next such save */
void t_t1175_save_list_kept()
{
    const int a[] = { 20, 32 };
    const int b[] = { 44, 11 };
    const int c[] = { 50, 50 };
    areafile::Content held, save, back, backSave;
    areafile::Build(a, 1, 0, 0, &held);
    if (areafile::TakeSaveList(1, 1, held, &save) != 1 || areafile::SameContent(save, held) == 0) Fail("T-1175: an own save with lists noted did not take them");
    areafile::Build(b, 1, 0, 0, &held);   /* the player walks on: later live writes */
    std::string text;
    if (!areafile::FormatWithSave("w", "h", 2ULL, held, &save, &text) || areafile::ParseWithSave(text, "w", "h", &back, &backSave, 0, 0) != areafile::kOk
        || backSave.player.size() != 2 || backSave.player[0] != 20 || back.player[0] != 44)
        Fail("T-1175: a later live write did not keep the last save's list");
    if (areafile::TakeSaveList(0, 1, held, &save) != 0 || save.player[0] != 20) Fail("T-1175: another folder's save replaced the save list");
    if (areafile::TakeSaveList(1, 0, held, &save) != -1 || save.player[0] != 20) Fail("T-1175: a save with no lists noted replaced the save list");
    if (areafile::TakeSaveList(1, 1, held, &save) != 1 || save.player[0] != 44) Fail("T-1175: the next own save did not replace the save list");
    areafile::Build(c, 1, 0, 0, &held);
    if (!areafile::FormatWithSave("w", "h", 3ULL, held, &save, &text) || areafile::ParseWithSave(text, "w", "h", &back, &backSave, 0, 0) != areafile::kOk
        || backSave.player.size() != 2 || backSave.player[0] != 44 || back.player[0] != 50)
        Fail("T-1175: the replaced save list was not written with the next live write");
}

/* T-1175: a load of the profile save that the records store laid nothing over starts the characters where the save put them, so
   its first player-area list is the save's list; a load the store laid squads over, an unarmed load or one before the file was read
   leaves the save list as it was */
void t_t1175_load_start_list()
{
    const int a[] = { 20, 32 };
    const int b[] = { 44, 11 };
    const int c[] = { 50, 50 };
    areafile::Content save, first, empty;
    areafile::Build(a, 1, 0, 0, &save);   /* the file's save list: an older save */
    areafile::Build(b, 1, c, 1, &first);  /* the load's first list: where the quit save put them */
    if (areafile::TakeLoadStartList(0, 0, 1, first, &save) != 0 || save.player[0] != 20) Fail("T-1175: an unarmed load took its first list");
    if (areafile::TakeLoadStartList(1, 0, 1, empty, &save) != -3 || save.player[0] != 20) Fail("T-1175: an empty list replaced the save list");
    if (areafile::TakeLoadStartList(1, 1, 1, first, &save) != -2 || save.player[0] != 20) Fail("T-1175: a load the records store moved squads in took its first list");
    if (areafile::TakeLoadStartList(1, 0, 0, first, &save) != -1 || save.player[0] != 20) Fail("T-1175: a first list before the file was read replaced the save list");
    if (areafile::TakeLoadStartList(1, 1, 0, first, &save) != -2 || save.player[0] != 20) Fail("T-1175: a moved squad was not told before readiness");
    if (areafile::TakeLoadStartList(1, 0, 1, first, &save) != 1 || areafile::SameContent(save, first) == 0) Fail("T-1175: the first list of an own load the store left alone was not taken");
    std::string text;
    areafile::Content held, back, backSave;
    areafile::Build(c, 1, 0, 0, &held);   /* the player walks on */
    if (!areafile::FormatWithSave("w", "h", 4ULL, held, &save, &text) || areafile::ParseWithSave(text, "w", "h", &back, &backSave, 0, 0) != areafile::kOk
        || backSave.player.size() != 2 || backSave.player[0] != 44 || back.player[0] != 50)
        Fail("T-1175: the load-start save list was not written with the next live write");
}


/* ---- T-922: src/common/templatekey.h + spawnage.h - a SPAWN carries the template's stringID AND its shown name ---- */
void t_t922_template_sid_sender()
{
    if (templatekey::SidToSend("1214-gamedata.base") != "1214-gamedata.base") Fail("a usable stringID is sent");
    if (!templatekey::SidToSend("").empty()) Fail("an entry with no stringID sends none");
    if (!templatekey::SidToSend(std::string(96, 'x')).empty()) Fail("an id longer than kMaxSid is not sent");
    if (templatekey::SidToSend(std::string(95, 'x')) != std::string(95, 'x')) Fail("an id of exactly kMaxSid bytes is sent");
    if (templatekey::kMaxSid != 95u || coopspawn::kSpawnMaxSid != templatekey::kMaxSid) Fail("the SPAWN's bound is templatekey's 95");
    if (!templatekey::SidToSend("12-gamedata\tbase").empty() || !templatekey::SidToSend("@x").empty()) Fail("a control byte or a leading '@': none sent");
    if (templatekey::SidToSend("7-Mod \xC3\xA9t\xC3\xA9.mod") != "7-Mod \xC3\xA9t\xC3\xA9.mod") Fail("a mod file name with spaces or non-ASCII bytes is still an id");
    if (templatekey::SentByName("1214-gamedata.base") || !templatekey::SentByName("") || !templatekey::SentByName(std::string(96, 'x'))) Fail("by name alone only without a usable id");
}
void t_t922_template_lookup_order()
{
    const int kChar = 3, kAnimal = 41, kOther = 9;   /* any three distinct type numbers */
    if (!templatekey::TypeAccepted(kChar, kChar, kAnimal) || !templatekey::TypeAccepted(kAnimal, kChar, kAnimal)) Fail("a CHARACTER or a RECORD_ANIMAL entry is taken");
    if (templatekey::TypeAccepted(kOther, kChar, kAnimal)) Fail("an entry of another kind found by id is not a template");
    if (!templatekey::TryAsId("1214-gamedata.base") || templatekey::TryAsId("") || templatekey::TryAsId(std::string(96, 'x'))) Fail("only a usable stringID is tried");
    templatekey::Lookup l;
    /* id hit: the name lookup does not run */
    l.tried = true; l.idFound = true; l.idTypeOk = true; l.idType = kChar;
    if (templatekey::NameLookupNeeded(l) || templatekey::Verdict(l) != templatekey::kTkIdHit) Fail("an accepted entry found by id: an id hit, no name lookup");
    if (templatekey::TriedWords("1214-gamedata.base", "Mercenary Guild", l).find("not needed") == std::string::npos) Fail("an id hit says the name was not needed");
    /* id miss (the receiver lacks the sender's id), name hit */
    l = templatekey::Lookup(); l.tried = true; l.nameTried = true; l.nameFound = true;
    if (!templatekey::NameLookupNeeded(l) || templatekey::Verdict(l) != templatekey::kTkNameFallback) Fail("no entry has the id: the shown name decides");
    /* both miss */
    l = templatekey::Lookup(); l.tried = true; l.nameTried = true;
    if (templatekey::Verdict(l) != templatekey::kTkRefused) Fail("neither the id nor the name: refused");
    const std::string w = templatekey::TriedWords("1214-gamedata.base", "Guilde des Mercenaires", l);
    if (w.find("id '1214-gamedata.base': no entry") == std::string::npos || w.find("shown name 'Guilde des Mercenaires': no CHARACTER") == std::string::npos)
        Fail("the refusal line names the id, then the name, and each result: " + w);
    /* an entry of another kind by id: the name decides */
    l = templatekey::Lookup(); l.tried = true; l.idFound = true; l.idTypeOk = false; l.idType = kOther; l.nameTried = true; l.nameFound = true;
    if (templatekey::Verdict(l) != templatekey::kTkNameFallback) Fail("another kind by id, the name found: the name decides");
    l.nameFound = false;
    if (templatekey::Verdict(l) != templatekey::kTkRefused) Fail("another kind by id and no name: refused");
    if (templatekey::TriedWords("5-x.mod", "N", l).find("type 9,") == std::string::npos) Fail("the refusal line gives the other kind's type");
    /* no id sent (an older entry or an unreadable one): the name alone */
    l = templatekey::Lookup(); l.nameTried = true; l.nameFound = true;
    if (templatekey::Verdict(l) != templatekey::kTkNameFallback) Fail("no id sent: the shown name decides");
    if (templatekey::TriedWords("", "Goat", l).find("none sent") == std::string::npos) Fail("the line says no id was sent");
    l = templatekey::Lookup();
    if (templatekey::Verdict(l) != templatekey::kTkRefused) Fail("nothing sent: refused");
    if (templatekey::Num(0) != "0" || templatekey::Num(41) != "41" || templatekey::Num(-1) != "-1") Fail("the type number words");
}
void t_t926f2_camera_road_words()
{
    using namespace swtab;
    if (!CameraRoadOffered(0) || CameraRoadOffered(1) || CameraRoadOffered(3)) Fail("the camera road: offered only with no living character");
    if (BackLabel("Kael") != "BRING KAEL BACK") Fail("the title");
    if (ButtonCaption(1, 1000) != "BRING BACK  c.1,000") Fail("the one button, the cost as today");
    if (CameraRoadLine("Kael") != "None of your characters is alive. Kael comes back where your camera is looking.") Fail("the caption line");
    if (RefusalWords(kRefNoSpot, "Kael", "", 0, 0) != "There is no safe spot here. Move the camera and try again.") Fail("no safe spot: the approved words");
    if (RefusalWords(kRefAtLimit, "Kael", "", 0, 0) != "You already have as many characters as you can lead.") Fail("the camera road's other refusal");
    if (std::string(RefusalTag(kRefNoSpot)) != "noSpot" || std::string(RefusalTag(kRefAtLimit)) != "atLimit"
        || std::string(RefusalTag(kRefCodes)) != "?") Fail("the tags");
    for (int k = 0; k < kRefCodes; ++k)
    {
        const std::string w = RefusalWords(k, "Kael", "", 1000, 0);
        if (w.find("town") != std::string::npos || w.find("Town") != std::string::npos) Fail("no refusal names a town");
        if (std::string(RefusalTag(k)) == "enemyUnread") Fail("the camera road has no enemy refusal");
    }
    const std::string head = "Kael returns with all limbs and no equipment. Their gear stays with their body where they fell.";
    if (BoxTextCamera("Kael", 1000, swfee::kGrowthSteady, 1, 0) != "Bring Kael back where your camera is looking for c.1,000? " + head
        + " The price is c.1,000 x 1 because 1 character you brought back is alive. After this one, the next will cost c.2,000.") Fail("the box, where the camera is looking");
    if (BoxText("Ruka", "Beep", 0, swfee::kGrowthSteep, 3, 0) != "Bring Ruka back beside Beep? Ruka returns with all limbs and no equipment. Their gear stays with their body where they fell.")
        Fail("the squadmate box is unchanged");
}
void t_t926f2_spot_order()
{
    using namespace swtab;
    const std::vector<SpotXZ> c = SpotCandidates(1000.0f, 2000.0f);
    if ((int)c.size() != kSpotCandidates || kSpotCandidates != 1 + kSpotRingCount * kSpotBearings) Fail("the candidates: the point and every ring");
    if (!(std::fabs(c[0].x - 1000.0f) < 0.01f && std::fabs(c[0].z - 2000.0f) < 0.01f)) Fail("the camera's point first");
    if (!(std::fabs(c[1].x - 1100.0f) < 0.01f && std::fabs(c[1].z - 2000.0f) < 0.01f)) Fail("then 10 m out, bearing 0");
    for (int i = 1; i < (int)c.size(); ++i)
    {
        const float d = std::sqrt((c[i].x - 1000.0f) * (c[i].x - 1000.0f) + (c[i].z - 2000.0f) * (c[i].z - 2000.0f));
        if (std::fabs(d - SpotRing(SpotRingOf(i) - 1)) > 0.1f) Fail("each candidate on its ring");
        if (i > 1 && SpotRingOf(i) < SpotRingOf(i - 1)) Fail("the nearest ring first");
    }
    if (SpotRing(0) != 100.0f || SpotRing(1) != 200.0f || SpotRing(2) != 300.0f || SpotRing(kSpotRingCount) != 0.0f) Fail("rings at 10, 20, 30 m");
    if (SpotRingOf(0) != 0 || SpotRingOf(1) != 1 || SpotRingOf(kSpotBearings) != 1 || SpotRingOf(kSpotBearings + 1) != 2
        || SpotRingOf(kSpotCandidates - 1) != kSpotRingCount) Fail("the ring of a candidate");
    if (SpotBearingOf(0) != 0 || SpotBearingOf(1) != 0 || SpotBearingOf(kSpotBearings) != kSpotBearings - 1) Fail("the bearing of a candidate");
    std::vector<SpotTry> tr((size_t)kSpotCandidates);
    for (size_t i = 0; i < tr.size(); ++i) { tr[i].loaded = 1; tr[i].ground = 1; tr[i].clear = 1; }
    int why = -1;
    if (FirstSafeSpot(tr, 1, &why) != 0 || why != kSpotOk) Fail("the camera's point when it passes");
    tr[0].clear = 0;
    if (FirstSafeSpot(tr, 1, &why) != 1 || why != kSpotOk) Fail("the first ring spot when the point is by a building");
    tr[1].clear = 0; tr[2].ground = 0;
    if (FirstSafeSpot(tr, 1, &why) != 3) Fail("the first that passes wins (no enemy test)");
}
void t_t926f2_spot_refusals()
{
    using namespace swtab;
    int why = -1;
    std::vector<SpotTry> tr((size_t)kSpotCandidates);
    for (size_t i = 1; i < tr.size(); ++i) { tr[i].loaded = 1; tr[i].ground = 1; tr[i].clear = 1; }
    if (FirstSafeSpot(tr, 1, &why) != -1 || why != kSpotNotLoaded) Fail("the camera's own area not loaded: refused, however clear the rings");
    tr[0].loaded = 1;
    for (size_t i = 0; i < tr.size(); ++i) tr[i].ground = 0;
    if (FirstSafeSpot(tr, 1, &why) != -1 || why != kSpotNoGround) Fail("no ground anywhere");
    for (size_t i = 0; i < tr.size(); ++i) { tr[i].ground = 1; tr[i].clear = 0; }
    if (FirstSafeSpot(tr, 1, &why) != -1 || why != kSpotByBuilding) Fail("every spot by a building");
    for (size_t i = 0; i < tr.size(); ++i) tr[i].clear = 1;
    if (FirstSafeSpot(tr, 0, &why) != -1 || why != kSpotListUnread) Fail("a building list cut short calls no spot clear");
    for (size_t i = 0; i < 5; ++i) tr[i].clear = 0;
    tr[3].loaded = 0; tr[3].clear = 1;
    if (FirstSafeSpot(tr, 1, &why) != 5 || why != kSpotOk) Fail("an unloaded ring spot is never taken");
    if (FirstSafeSpot(std::vector<SpotTry>(), 1, &why) != -1 || why != kSpotNone) Fail("no candidates");
    tr[0].loaded = 0;
    if (FirstSafeSpot(tr, 0, &why, 0) != -1 || why != kSpotListUnread) Fail("the engine's area list unread: a list unread, not an area not loaded");
    if (FirstSafeSpot(tr, 0, &why, 1) != -1 || why != kSpotNotLoaded) Fail("the area list read and the camera's area not loaded");
    if (std::string(SpotTag(kSpotNoCamera)) != "noCamera" || std::string(SpotTag(kSpotCodes)) != "?") Fail("the spot tags");
    for (int k = 0; k < kSpotCodes; ++k) if (std::string(SpotTag(k)) == "enemyNear") Fail("an enemy nearby is no spot refusal");
    std::vector<SpotXZ> b; b.push_back(SpotXZ(1100.0f, 2150.0f));
    if (SpotClear(SpotXZ(1100.0f, 2000.0f), b, kSpotClearUnits) != 0 || SpotClear(SpotXZ(1100.0f, 1700.0f), b, kSpotClearUnits) != 1) Fail("by a building / clear of it");
}
void t_t926f2_view_and_limit()
{
    using namespace swtab;
    ViewIn in; in.on = 1; in.rows = 2; in.selected = 1; in.mates = 0; in.price = 1000; in.money = 5000; in.moneyRead = 1;
    View v = ViewFor(in);
    if (v.caption != kCapRefusal || v.refusal != kRefNoMate || v.btnEnabled || !v.dropShown) Fail("the squadmate road with nobody: today's refusal, greyed");
    in.cameraRoad = 1; v = ViewFor(in);
    if (v.caption != kCapCamera || !v.btnEnabled || !v.pickShown || v.dropShown) Fail("the camera road: the line, the title and one button, no drop-down");
    in.money = 10; v = ViewFor(in);
    if (v.refusal != kRefShort || v.btnEnabled) Fail("the camera road, short money: greyed");
    in.money = 5000; in.note = kRefNoSpot; v = ViewFor(in);
    if (v.caption != kCapRefusal || v.refusal != kRefNoSpot || !v.btnEnabled) Fail("no safe spot shows over the line; the button stays lit to try again");
    in.note = kRefNone; in.busy = 1; v = ViewFor(in);
    if (v.caption != kCapBusy || v.btnEnabled) Fail("while it runs");
    in.busy = 0; in.atLimit = 1; v = ViewFor(in);
    if (v.caption != kCapRefusal || v.refusal != kRefAtLimit || v.btnEnabled) Fail("at the limit on the camera road: the words, greyed");
    in.boxUp = 1; v = ViewFor(in);
    if (v.refusal == kRefAtLimit || !v.btnEnabled) Fail("the box open: BRING BACK's road refuses at the limit instead");
    if (AtCharacterLimit(1, 256, 256) != 1 || AtCharacterLimit(1, 256, 300) != 1 || AtCharacterLimit(1, 256, 255) != 0) Fail("at / over / below the limit");
    if (AtCharacterLimit(0, 256, 999) != -1 || AtCharacterLimit(1, 0, 5) != -1 || AtCharacterLimit(1, -3, 5) != -1) Fail("an unread limit is not judged");
    {
        int o = 0;
        if (LimitForRefusal(1, 256, 0, &o) != 1 || o != 256) Fail("no test cap: the game's limit");
        if (LimitForRefusal(1, 256, 3, &o) != 1 || o != 3 || AtCharacterLimit(1, o, 3) != 1 || AtCharacterLimit(1, o, 2) != 0) Fail("a test cap lowers the limit");
        if (LimitForRefusal(1, 256, 300, &o) != 1 || o != 256) Fail("a test cap never raises the limit");
        if (LimitForRefusal(0, 0, 2, &o) != 1 || o != 2 || LimitForRefusal(1, -3, 2, &o) != 1 || o != 2) Fail("a test cap judges when the game's limit is unread");
        if (LimitForRefusal(0, 0, 0, &o) != 0 || AtCharacterLimit(0, o, 9) != -1) Fail("no cap, unread: not judged");
    }
    if (DropDownSwitched(0, 1) != 1 || DropDownSwitched(1, 0) != 1 || DropDownSwitched(1, 1) != 0 || DropDownSwitched(0, 0) != 0) Fail("the drop-down switches only when the road changes");
    if (PressPick(1, -1) != 0 || PressPick(0, 5) != 5 || PressPick(0, -1) != -1) Fail("the press: nothing to pick on the camera road");
    if (ConfirmRoad(0, 7, 1) != kConfirmNone || ConfirmRoad(9, 0, 0) != kConfirmNone || ConfirmRoad(9, 0, 1) != kConfirmCamera || ConfirmRoad(9, 7, 0) != kConfirmBeside)
        Fail("the confirm's road");
    ViewIn s; s.on = 1; s.rows = 1; s.selected = 1; s.mates = 2; s.price = 0;
    v = ViewFor(s);
    if (v.caption != kCapNone || !v.btnEnabled || !v.dropShown) Fail("a living squadmate: the tab as before");
}
void t_t922_spawn_sid_roundtrip()
{
    std::vector<char> b;
    coopspawn::EncodeSpawn(&b, 77u, 1.0f, 2.0f, 3.0f, "Mercenary Guild", "Hivers", false, 0.0f);
    coopspawn::AppendSpawnStats(&b, 0);
    coopspawn::AppendSpawnFlags(&b, 0);
    const size_t noTail = b.size();
    coopspawn::AppendSpawnTemplateSid(&b, "1214-gamedata.base");
    coopspawn::SpawnMsg m;
    if (coopspawn::DecodeSpawn(&b[0], b.size(), &m) != coopspawn::kSpawnDecodeOk) { Fail("a SPAWN with both texts did not decode"); return; }
    if (m.templateName != "Mercenary Guild" || m.templateSid != "1214-gamedata.base" || m.templateSidAbsent || m.templateSidBad || m.flagsAbsent)
        Fail("both texts round-trip: the shown name in its field, the stringID in the tail");
    coopspawn::SpawnMsg o;
    if (coopspawn::DecodeSpawn(&b[0], noTail, &o) != coopspawn::kSpawnDecodeOk || !o.templateSidAbsent || !o.templateSid.empty() || o.templateName != "Mercenary Guild")
        Fail("a payload ending at the flags byte: no id, the name alone");
    coopspawn::SpawnMsg c;
    if (coopspawn::DecodeSpawn(&b[0], b.size() - 3, &c) != coopspawn::kSpawnDecodeOk || !c.templateSidBad || !c.templateSid.empty())
        Fail("a cut stringID is not used and the SPAWN still decodes");
    std::vector<char> l(b.begin(), b.begin() + noTail);
    coopspawn::AppendSpawnTemplateSid(&l, std::string(96, 'x'));
    if (l.size() != noTail + 4) Fail("an over-long id is written as none");
    coopspawn::SpawnMsg e;
    if (coopspawn::DecodeSpawn(&l[0], l.size(), &e) != coopspawn::kSpawnDecodeOk || !e.templateSid.empty() || e.templateSidAbsent || e.templateSidBad)
        Fail("a length-0 id reads as none sent");
    std::vector<char> h(b.begin(), b.begin() + noTail);
    coopspawn::SpawnPutU32(&h, 0xFFFFFFFFu); h.push_back('x');
    coopspawn::SpawnMsg hm;
    if (coopspawn::DecodeSpawn(&h[0], h.size(), &hm) != coopspawn::kSpawnDecodeOk || !hm.templateSidBad || !hm.templateSid.empty())
        Fail("a hostile id length is refused, the SPAWN kept");
}


/* T-924 fault 2: townmove::Decide for every branch - the 1-unit and 25-unit edges, a town that is not the mod's own copy never moved,
   the guard, engine writes blocked, an unreadable gap; the load sweep's radius test; the constants */
void t_t924f_town_move_decide()
{
    /* every townmove name qualified: this file's anonymous namespace has its own KindName (C2668) */
    const int sk = townmove::kStepKeep, st = townmove::kStepTake;
    if (townmove::kStepKeep != baseshow::kRowKeep || townmove::kStepTake != baseshow::kRowTake) Fail("T-924 f: the steps differ from baseshow's");
    if (townmove::kMinGap != 1.0f || townmove::kMaxGap != 25.0f || townmove::kNoGround != -99.0f || townmove::kLineCap != 40) Fail("T-924 f: a threshold changed");
    if (townmove::Decide(sk, 1, 0.0f, 0.0f, 0, 0) != townmove::kSame || townmove::Decide(st, 1, 0.5f, -0.5f, 0, 0) != townmove::kSame) Fail("T-924 f: a copy at the sent centre was not the same");
    if (townmove::Decide(sk, 1, 1.0f, 0.0f, 0, 0) != townmove::kSame || townmove::Decide(sk, 1, 0.0f, -1.0f, 0, 0) != townmove::kSame) Fail("T-924 f: a gap of exactly 1 unit moved the copy");
    if (townmove::Decide(sk, 1, 1.01f, 0.0f, 0, 0) != townmove::kMove || townmove::Decide(st, 1, 0.0f, -1.01f, 0, 0) != townmove::kMove) Fail("T-924 f: a gap just over 1 unit did not move the copy");
    if (townmove::Decide(sk, 1, 1.5f, -8.9f, 0, 0) != townmove::kMove) Fail("T-924 f: the report's ~9-unit drift did not move the copy");
    if (townmove::Decide(sk, 1, 25.0f, 0.0f, 0, 0) != townmove::kMove || townmove::Decide(st, 1, -15.0f, 20.0f, 0, 0) != townmove::kMove) Fail("T-924 f: a gap of exactly 25 units did not move the copy");
    if (townmove::Decide(sk, 1, 25.01f, 0.0f, 0, 0) != townmove::kRefound || townmove::Decide(sk, 1, 0.0f, -300.0f, 0, 0) != townmove::kRefound) Fail("T-924 f: a gap over 25 units was not re-founded (fold 1)");
    if (townmove::Decide(sk, 0, 9.0f, 0.0f, 0, 0) != townmove::kNotOurs || townmove::Decide(st, 0, 9.0f, 0.0f, 0, 0) != townmove::kNotOurs) Fail("T-924 f: a kept outpost's town (not the mod's own) was not left alone");
    if (townmove::Decide(st, 0, 0.0f, 0.0f, 1, 1) != townmove::kNotOurs || townmove::Decide(sk, 0, 100.0f, 0.0f, 0, 0) != townmove::kNotOurs) Fail("T-924 f: a town not the mod's own was judged");
    if (townmove::Decide(0, 1, 9.0f, 0.0f, 0, 0) != townmove::kNotOurs || townmove::Decide(3, 1, 9.0f, 0.0f, 0, 0) != townmove::kNotOurs) Fail("T-924 f: a row with no matched copy (none / founded) was judged");
    if (townmove::Decide(sk, 1, 9.0f, 0.0f, 1, 0) != townmove::kGuarded || townmove::Decide(st, 1, 0.0f, 25.0f, 1, 0) != townmove::kGuarded) Fail("T-924 f: a target inside another town's radius was not guarded");
    if (townmove::Decide(sk, 1, 0.5f, 0.0f, 1, 0) != townmove::kSame || townmove::Decide(sk, 1, 30.0f, 0.0f, 1, 0) != townmove::kGuarded) Fail("T-924 f: the guard changed a same answer, or did not hold a new copy");
    if (townmove::Decide(sk, 1, 9.0f, 0.0f, 0, 1) != townmove::kBlocked || townmove::Decide(sk, 1, 9.0f, 0.0f, 1, 1) != townmove::kBlocked) Fail("T-924 f: blocked engine writes did not hold the move");
    if (townmove::Decide(sk, 1, 0.0f, 0.0f, 0, 1) != townmove::kSame || townmove::Decide(sk, 1, 40.0f, 0.0f, 0, 1) != townmove::kBlocked) Fail("T-924 f: blocked changed a same answer, or did not hold a new copy");
    volatile float zero = 0.0f;   /* a NaN and an infinity made at run time */
    if (townmove::Decide(sk, 1, zero / zero, 0.0f, 0, 0) != townmove::kTooFar || townmove::Decide(sk, 1, 0.0f, 1.0f / zero, 0, 0) != townmove::kTooFar) Fail("T-924 f: an unreadable gap was moved to");
    if (townmove::InsideRadius(0.0f, 0.0f, 10.0f, 0.0f, 10.5f) != 1 || townmove::InsideRadius(0.0f, 0.0f, 3.0f, 4.0f, 6.0f) != 1) Fail("T-924 f: a target inside a radius was not inside");
    if (townmove::InsideRadius(0.0f, 0.0f, 10.0f, 0.0f, 10.0f) != 0 || townmove::InsideRadius(0.0f, 0.0f, 0.0f, 50.0f, 10.0f) != 0) Fail("T-924 f: a target on or past a radius was inside (the sweep's test is strict)");
    if (townmove::InsideRadius(0.0f, 0.0f, 500.0f, 0.0f, zero / zero) != 1) Fail("T-924 f: an unreadable radius did not guard");
    for (int k = 0; k < townmove::kKindN; ++k)
    {
        if (std::string(townmove::KindName(k)) == "?") Fail("T-924 f: a kind has no name");
        for (int j = k + 1; j < townmove::kKindN; ++j) if (std::string(townmove::KindName(k)) == townmove::KindName(j)) Fail("T-924 f: two kinds share a name");
    }
    if (std::string(townmove::KindName(townmove::kKindN)) != "?") Fail("T-924 f: an out-of-range kind has a name");
    if (townmove::GapXZ(3.0f, -4.0f) != 5.0f) Fail("T-924 f: the gap");
}
/* T-924 FOLD 1 (review D1 / D3 / D4): a gap over 25 units of the mod's own copy is re-founded (retired, a new copy founded at the
   sent centre) - never for a town not the mod's own, held by the guard and by blocked writes, never for a gap that is not a number;
   the off-map test (no area added); the height written on no ground */
void t_t924f_fold1_refound_offmap_height()
{
    const int sk = townmove::kStepKeep, st = townmove::kStepTake;
    volatile float zero = 0.0f;
    const float qnan = zero / zero, pinf = 1.0f / zero;
    /* refound: T1453's 58 units, a taken town at 50, just over the edge */
    if (townmove::Decide(sk, 1, 0.0f, -58.0f, 0, 0) != townmove::kRefound) Fail("T-924 fold 1: T1453's 58-unit gap of the mod's own copy was not re-founded");
    if (townmove::Decide(st, 1, 30.0f, 40.0f, 0, 0) != townmove::kRefound) Fail("T-924 fold 1: a taken copy 50 units out was not re-founded");
    if (townmove::Decide(sk, 1, 25.01f, 0.0f, 0, 0) != townmove::kRefound || townmove::Decide(sk, 1, 25.0f, 0.0f, 0, 0) != townmove::kMove) Fail("T-924 fold 1: the 25-unit edge between a move and a new copy");
    /* not ours: left, whatever the gap */
    if (townmove::Decide(sk, 0, 58.0f, 0.0f, 0, 0) != townmove::kNotOurs || townmove::Decide(st, 0, 0.0f, 58.0f, 0, 0) != townmove::kNotOurs) Fail("T-924 fold 1: a town not the mod's own was re-founded");
    if (townmove::Decide(3, 1, 58.0f, 0.0f, 0, 0) != townmove::kNotOurs) Fail("T-924 fold 1: a row founded this apply was re-founded");
    /* the guard and blocked writes hold a new copy as they hold a move */
    if (townmove::Decide(sk, 1, 58.0f, 0.0f, 1, 0) != townmove::kGuarded) Fail("T-924 fold 1: a new copy inside another town's radius was not guarded");
    if (townmove::Decide(sk, 1, 58.0f, 0.0f, 0, 1) != townmove::kBlocked || townmove::Decide(sk, 1, 58.0f, 0.0f, 1, 1) != townmove::kBlocked) Fail("T-924 fold 1: blocked writes did not hold a new copy");
    /* a gap that is not a number: never written, whatever the guard or blocked say */
    if (townmove::Decide(sk, 1, qnan, 0.0f, 0, 0) != townmove::kTooFar || townmove::Decide(sk, 1, 0.0f, pinf, 0, 0) != townmove::kTooFar) Fail("T-924 fold 1: a NaN / infinite gap was re-founded");
    if (townmove::Decide(sk, 1, 1.0e20f, 0.0f, 0, 0) != townmove::kTooFar) Fail("T-924 fold 1: a gap whose square is past a float was re-founded");
    if (townmove::Decide(sk, 1, qnan, 0.0f, 1, 1) != townmove::kTooFar) Fail("T-924 fold 1: the guard or blocked changed a not-a-number answer");
    if (std::string(townmove::KindName(townmove::kRefound)) != "refound") Fail("T-924 fold 1: the refound kind's name");
    /* review D3: off the map */
    if (townmove::kMapEdge != poskey::kSectorOrigin) Fail("T-924 fold 1: the map edge differs from poskey's sector origin");
    if (townmove::OffMap(0.0f, 0.0f) != 0 || townmove::OffMap(147455.9f, -147456.0f) != 0 || townmove::OffMap(-147456.0f, 147455.9f) != 0) Fail("T-924 fold 1: a centre on the map was off it");
    if (townmove::OffMap(147456.0f, 0.0f) != 1 || townmove::OffMap(0.0f, 147456.0f) != 1) Fail("T-924 fold 1: the far edge (cell 64) was on the map");
    if (townmove::OffMap(-147456.1f, 0.0f) != 1 || townmove::OffMap(0.0f, -200000.0f) != 1) Fail("T-924 fold 1: a centre past the near edge was on the map");
    if (townmove::OffMap(qnan, 0.0f) != 1 || townmove::OffMap(0.0f, pinf) != 1) Fail("T-924 fold 1: a centre that is not a number was on the map");
    /* review D4: the height */
    if (townmove::HeightOf(12.5f, 3.0f, 7.0f) != 12.5f) Fail("T-924 fold 1: the ground height was not written");
    if (townmove::HeightOf(townmove::kNoGround, 3.0f, 7.0f) != 3.0f) Fail("T-924 fold 1: on no ground the sent height was not written");
    if (townmove::HeightOf(townmove::kNoGround, qnan, 7.0f) != 7.0f || townmove::HeightOf(townmove::kNoGround, pinf, 7.0f) != 7.0f
        || townmove::HeightOf(townmove::kNoGround, -pinf, 7.0f) != 7.0f) Fail("T-924 fold 1: on no ground a sent height that is not a number was written");
    if (townmove::Finite(1.0f) != 1 || townmove::Finite(-3.0e38f) != 1 || townmove::Finite(qnan) != 0 || townmove::Finite(pinf) != 0 || townmove::Finite(-pinf) != 0) Fail("T-924 fold 1: Finite");
}
void t_t924f_fold4_pending_link()
{
    volatile float zero = 0.0f;
    const float qnan = zero / zero;
    /* the link pass while a re-found waits */
    /* the old copy at (0, 0), the owner's sent centre at (400, 0) */
    if (townmove::PendingLinkSkip(0, 0, 1, 390.0f, 0.0f, 0.0f, 0.0f, 400.0f, 0.0f) != 0) Fail("T-924 fold 4: a base not pending had a link left out");
    if (townmove::PendingLinkSkip(1, 1, 1, 390.0f, 0.0f, 0.0f, 0.0f, 400.0f, 0.0f) != 0) Fail("T-924 fold 4: a piece already naming the old copy was left out");
    if (townmove::PendingLinkSkip(1, 0, 1, 60.0f, -40.0f, 0.0f, 0.0f, 400.0f, 0.0f) != 0 || townmove::PendingLinkSkip(1, 0, 1, 199.0f, 0.0f, 0.0f, 0.0f, 400.0f, 0.0f) != 0)
        Fail("T-924 fold 4: a piece near the old copy (past kMaxGap) was left out");
    if (townmove::PendingLinkSkip(1, 0, 1, 390.0f, 10.0f, 0.0f, 0.0f, 400.0f, 0.0f) != 1 || townmove::PendingLinkSkip(1, 0, 1, 201.0f, 0.0f, 0.0f, 0.0f, 400.0f, 0.0f) != 1)
        Fail("T-924 fold 4: a piece near the new site was linked into the old copy");
    if (townmove::PendingLinkSkip(1, 0, 1, 200.0f, 0.0f, 0.0f, 0.0f, 400.0f, 0.0f) != 0 || townmove::PendingLinkSkip(1, 0, 1, 200.0f, 50.0f, 0.0f, 0.0f, 400.0f, 0.0f) != 0)
        Fail("T-924 fold 4: an equidistant piece was left out");
    if (townmove::PendingLinkSkip(1, 0, 0, 390.0f, 0.0f, 0.0f, 0.0f, 400.0f, 0.0f) != 0 || townmove::PendingLinkSkip(1, 0, 1, qnan, 0.0f, 0.0f, 0.0f, 400.0f, 0.0f) != 0)
        Fail("T-924 fold 4: an unread spot was left out");
    /* the pending record kept only while the answer is kRefound */
    if (townmove::PendingDrop(townmove::kRefound) != 0) Fail("T-924 fold 4: a refound answer dropped its pending record");
    for (int k = 0; k < townmove::kKindN; ++k)
        if (k != townmove::kRefound && townmove::PendingDrop(k) != 1) Fail("T-924 fold 4: another answer kept a pending record");
}
void t_t924f_fold2_refound_limits()
{
    const int sk = townmove::kStepKeep;
    volatile float zero = 0.0f;
    const float qnan = zero / zero, pinf = 1.0f / zero;
    const int farK = townmove::Decide(sk, 1, 58.0f, 0.0f, 0, 0), nearK = townmove::Decide(sk, 1, 9.0f, 0.0f, 0, 0);
    if (farK != townmove::kRefound || nearK != townmove::kMove) Fail("T-924 fold 2: the Decide legs the refusals start from");
    /* off the map: a refound and a move alike, refused (nothing taken out, nothing written) */
    if (townmove::Refusal(farK, 147456.0f, 0.0f, 0) != townmove::kOffMap || townmove::Refusal(farK, 0.0f, -1.0e7f, 0) != townmove::kOffMap) Fail("T-924 fold 2: a refound to an off-map centre was not refused");
    if (townmove::Refusal(nearK, -147456.1f, 0.0f, 0) != townmove::kOffMap || townmove::Refusal(nearK, 0.0f, qnan, 0) != townmove::kOffMap) Fail("T-924 fold 2: a move to an off-map centre was not refused");
    if (townmove::Refusal(farK, 1000.0f, -2000.0f, 0) != townmove::kRefound || townmove::Refusal(nearK, 147455.9f, -147456.0f, 0) != townmove::kMove) Fail("T-924 fold 2: a centre on the map was refused");
    if (townmove::Refusal(farK, pinf, 0.0f, 1) != townmove::kOffMap) Fail("T-924 fold 2: off the map is not answered before a repeat");
    /* once per world */
    if (townmove::Refusal(farK, 1000.0f, -2000.0f, 1) != townmove::kRepeat) Fail("T-924 fold 2: a second refound of a base this world was not refused");
    if (townmove::Refusal(nearK, 1000.0f, -2000.0f, 1) != townmove::kMove) Fail("T-924 fold 2: a move of a refounded base was refused as a repeat");
    for (int k = 0; k < townmove::kKindN; ++k)
        if (k != townmove::kMove && k != townmove::kRefound && townmove::Refusal(k, 147456.0f, 0.0f, 1) != k) Fail("T-924 fold 2: a kind that writes nothing was changed by the refusals");
    if (std::string(townmove::KindName(townmove::kOffMap)) != "offMap" || std::string(townmove::KindName(townmove::kRepeat)) != "repeat") Fail("T-924 fold 2: the new kinds' names");
    /* HeightOf: a ground answer that is not a number is never written */
    if (townmove::HeightOf(qnan, 3.0f, 7.0f) != 7.0f || townmove::HeightOf(pinf, 3.0f, 7.0f) != 7.0f || townmove::HeightOf(-pinf, qnan, 7.0f) != 7.0f) Fail("T-924 fold 2: a ground answer that is not a number was written");
    if (townmove::HeightOf(-12.0f, 3.0f, 7.0f) != -12.0f || townmove::HeightOf(townmove::kNoGround, 3.0f, 7.0f) != 3.0f) Fail("T-924 fold 2: a ground answer that is a number was not written as before");
}
/* ---- T-752: the announce roads judge a non-player squad whole - its squad spot OR any member's own spot (src/common/liverelay.h) ---- */
void t_t752_squad_held_by_member()
{
    using namespace cooplive;
    int why = -1;
    const int in1[3] = { 0, 1, 0 }, none3[3] = { 0, 0, 0 }, ann3[3] = { 1, 1, 1 }, near1[3] = { 1, 0, 0 };
    if (AnnounceSquadHeld(0, 3, in1, none3, ann3, &why) != 1 || why != kAnnSqWhyMember) Fail("t752: a squad whose spot is out and one member in another game's area was not held");
    if (AnnounceSquadHeld(0, 3, none3, none3, ann3, &why) != 0 || why != kAnnSqWhyNone) Fail("t752: a squad wholly outside every held area was held");
    if (AnnounceSquadHeld(1, 0, 0, 0, 0, &why) != 1 || why != kAnnSqWhySquad) Fail("t752: a squad whose spot is held was not held");
    if (AnnounceSquadHeld(1, 3, none3, none3, none3, &why) != 1 || why != kAnnSqWhySquad) Fail("t752: a held squad spot did not decide first");
    if (AnnounceSquadHeld(0, 3, none3, near1, ann3, &why) != 1 || why != kAnnSqWhyMargin) Fail("t752: an announced member within the margin did not keep its squad");
    if (AnnounceSquadHeld(0, 3, none3, near1, none3, &why) != 0) Fail("t752: a member never announced was kept by the margin");
    if (AnnounceSquadHeld(0, 3, in1, near1, ann3, &why) != 1 || why != kAnnSqWhyMember) Fail("t752: a member inside was taken as kept by the margin");
    {   /* hysteresis by distance: in (announced), just outside (kept), past the margin (withdrawn), back just outside (still out) */
        const int inR[1] = { 1 }, outR[1] = { 0 }, nearR[1] = { 1 };
        int annR[1] = { 0 };
        if (AnnounceSquadHeld(0, 1, inR, outR, annR, &why) != 1) Fail("t752: walk - a member stepping in did not take its squad");
        annR[0] = 1;
        if (AnnounceSquadHeld(0, 1, outR, nearR, annR, &why) != 1 || why != kAnnSqWhyMargin) Fail("t752: walk - a member just outside was withdrawn at once");
        if (AnnounceSquadHeld(0, 1, outR, outR, annR, &why) != 0) Fail("t752: walk - a member past the margin kept its squad");
        annR[0] = 0;
        if (AnnounceSquadHeld(0, 1, outR, nearR, annR, &why) != 0) Fail("t752: walk - a withdrawn member just outside was announced again before stepping in");
    }
    float xs[4], zs[4];
    AnnounceMarginCorners(100.0f, -200.0f, kAnnMemberMarginUnits, xs, zs);
    for (int k = 0; k < 4; ++k)
        if (std::fabs(std::fabs(xs[k] - 100.0f) - kAnnMemberMarginUnits) > 0.01f || std::fabs(std::fabs(zs[k] + 200.0f) - kAnnMemberMarginUnits) > 0.01f) Fail("t752: a margin corner is not at the margin");
    if (!(xs[0] < 100.0f && xs[1] > 100.0f && zs[0] < -200.0f && zs[2] > -200.0f)) Fail("t752: the margin corners do not surround the spot");
    if (!(kAnnMemberMarginUnits > 0.0f && kAnnMemberMarginUnits < 4608.0f)) Fail("t752: the margin is not inside one area's side (the four corners would miss an area)");
    /* one answer per squad per generation: every member gets the first answer */
    AnnSquadMemo memo;
    int h = 0, w = 0;
    const void* sqA = (const void*)0x1000;
    const void* sqB = (const void*)0x2000;
    if (AnnSquadMemoGet(memo, sqA, 5, &h, &w) != 0) Fail("t752: an empty memo answered");
    if (AnnSquadMemoPut(&memo, sqA, 5, 1, kAnnSqWhyMember) != -1) Fail("t752: a first answer reported an earlier one");
    for (int m = 0; m < 6; ++m)
    {
        h = -1; w = -1;
        if (AnnSquadMemoGet(memo, sqA, 5, &h, &w) != 1 || h != 1 || w != kAnnSqWhyMember) Fail("t752: a member of the squad got another answer in the same generation");
    }
    if (AnnSquadMemoGet(memo, sqA, 6, &h, &w) != 0) Fail("t752: an answer was reused in the next generation");
    if (AnnSquadMemoGet(memo, sqB, 5, &h, &w) != 0) Fail("t752: another squad got this squad's answer");
    if (AnnSquadMemoPut(&memo, sqA, 6, 0, kAnnSqWhyNone) != kAnnSqWhyMember) Fail("t752: the next answer did not report the earlier one");
    if (AnnSquadMemoPut(&memo, 0, 6, 1, kAnnSqWhySquad) != -1 || AnnSquadMemoGet(memo, 0, 6, &h, &w) != 0) Fail("t752: a squad with no address was kept");
    AnnSquadMemo big;
    for (size_t i = 1; i <= kAnnSquadMemoMax; ++i) AnnSquadMemoPut(&big, (const void*)(i * 16), 7, 0, kAnnSqWhyNone);
    AnnSquadMemoPut(&big, (const void*)0x99990, 8, 1, kAnnSqWhySquad);
    if (big.size() != 1 || AnnSquadMemoGet(big, (const void*)0x99990, 8, &h, &w) != 1) Fail("t752: a full memo did not drop its older generations");
    if (std::string(AnnounceSquadWhyName(kAnnSqWhyMargin)) != "margin" || std::string(AnnounceSquadWhyName(kAnnSqWhyOwn)) != "own"
        || std::string(AnnounceSquadWhyName(kAnnSqWhyMember)) != "member") Fail("t752: the why names");
}
/* ---- T-752: which members count for a squad's answer, and whether the members are walked (src/common/liverelay.h) ---- */
void t_t752_member_rows()
{
    using namespace cooplive;
    int h = -1, nr = -1, a = -1;
    if (AnnounceMemberRow(1, 1, 1, 0, 1, kAnnMarkAnnounced, &h, &nr, &a) != kAnnMemDead) Fail("t752 rows: a dead member standing in a held area counted");
    if (AnnounceMemberRow(-1, 1, 1, 0, 1, kAnnMarkAnnounced, &h, &nr, &a) != kAnnMemDeadUnread) Fail("t752 rows: a member with an unreadable dead flag counted");
    if (AnnounceMemberRow(0, 0, 1, 0, 1, kAnnMarkAnnounced, &h, &nr, &a) != kAnnMemNoSpot) Fail("t752 rows: a member whose spot was not read counted");
    if (AnnounceMemberRow(0, 1, 1, 0, 0, kAnnMarkNone, &h, &nr, &a) != kAnnMemCounts || h != 1 || a != 0 || nr != 0) Fail("t752 rows: a living member inside a held area (not run here) did not count as held");
    if (AnnounceMemberRow(0, 1, 0, 1, 1, kAnnMarkNoRecord, &h, &nr, &a) != kAnnMemCounts || a != 1 || nr != 1 || h != 0) Fail("t752 rows: an adopted member with no mark yet was not taken as announced");
    if (AnnounceMemberRow(0, 1, 0, 1, 1, kAnnMarkWithheld, &h, &nr, &a) != kAnnMemCounts || a != 0 || nr != 0) Fail("t752 rows: a withheld member was kept by the margin");
    if (AnnounceMemberRow(0, 1, 0, 1, 0, kAnnMarkAnnounced, &h, &nr, &a) != kAnnMemCounts || a != 0 || nr != 0) Fail("t752 rows: a member this game does not run was taken as announced");
    if (AnnounceMemberRow(0, 1, 1, 1, 1, kAnnMarkAnnounced, &h, &nr, &a) != kAnnMemCounts || h != 1 || nr != 0) Fail("t752 rows: a member inside was also marked near");
    if (AnnounceMemberRow(0, 1, 0, 1, 1, kAnnMarkAnnounced, &h, &nr, &a) != kAnnMemCounts || a != 1 || nr != 1 || h != 0) Fail("t752 rows: an announced member near a held area was not kept by the margin");
    if (AnnounceMemberRow(0, 1, 0, 1, 1, kAnnMarkAnnounced, 0, 0, 0) != kAnnMemCounts) Fail("t752 rows: absent outputs refused");
    if (AnnounceSquadWalk(1, 80, 64) != kAnnWalkNone || AnnounceSquadWalk(1, 5, 5) != kAnnWalkNone) Fail("t752 rows: members walked though the squad's spot is held");
    if (AnnounceSquadWalk(0, 5, 5) != kAnnWalkSquad) Fail("t752 rows: a fully read squad was not judged whole");
    if (AnnounceSquadWalk(0, 80, 64) != kAnnWalkSelf) Fail("t752 rows: a squad above the reader's cap was judged whole");
    {   /* a body in the held area and every living member outside: the squad is not held, the living members are withdrawn */
        int mh[3], mn[3], ma[3], n = 0, why = -1;
        if (AnnounceMemberRow(1, 1, 1, 0, 1, kAnnMarkAnnounced, &mh[n], &mn[n], &ma[n]) == kAnnMemCounts) ++n;
        if (AnnounceMemberRow(0, 1, 0, 0, 1, kAnnMarkAnnounced, &mh[n], &mn[n], &ma[n]) == kAnnMemCounts) ++n;
        if (AnnounceMemberRow(0, 1, 0, 0, 1, kAnnMarkAnnounced, &mh[n], &mn[n], &ma[n]) == kAnnMemCounts) ++n;
        if (n != 2 || AnnounceSquadHeld(0, n, mh, mn, ma, &why) != 0) Fail("t752 rows: a dead member's body kept its squad held");
        n = 0;
        if (AnnounceMemberRow(0, 1, 1, 0, 1, kAnnMarkAnnounced, &mh[n], &mn[n], &ma[n]) == kAnnMemCounts) ++n;
        if (n != 1 || AnnounceSquadHeld(0, n, mh, mn, ma, &why) != 1 || why != kAnnSqWhyMember) Fail("t752 rows: a living member inside did not hold its squad");
    }
}
/* T-948 b: sqsplit::SplitDecide and MoveDecide for every branch */
void t_t948b_squad_split_decide()
{
    using sqsplit::SplitDecide; using sqsplit::MoveDecide;
    const int sk = sqsplit::kSplitSkip, ru = sqsplit::kSplitRun;
    if (SplitDecide(7, 7, 0) != sk || SplitDecide(1, 1, 0) != sk) Fail("T-948 b: a squad of copies only was split");
    if (SplitDecide(7, 6, 0) != ru || SplitDecide(2, 1, 0) != ru || SplitDecide(3, 0, 0) != ru) Fail("T-948 b: a squad with an own member was not run");
    if (SplitDecide(7, 6, 1) != ru || SplitDecide(1, 0, 1) != ru) Fail("T-948 b: a squad with an unsure member was skipped");
    if (SplitDecide(0, 0, 0) != ru || SplitDecide(-1, 0, 1) != ru || SplitDecide(-1, 0, 0) != ru) Fail("T-948 b: an empty or unreadable squad was skipped");
    if (MoveDecide(1, 0) != sqsplit::kMoveRefuse) Fail("T-948 b: a copy was moved to a new squad");
    if (MoveDecide(1, 1) != sqsplit::kMovePass || MoveDecide(1, 3) != sqsplit::kMovePass) Fail("T-948 b: the mod's own move of a copy was refused");
    if (MoveDecide(0, 0) != sqsplit::kMoveRun || MoveDecide(0, 2) != sqsplit::kMoveRun) Fail("T-948 b: an own character's move was not run");
    if (MoveDecide(-1, 0) != sqsplit::kMoveRun) Fail("T-948 b: an unsure answer refused the move (it counts as this game's own)");
}

/* T-948 d: a caged uid-less body that duplicates another game's registered copy leaves its cage the engine's own way and is removed;
   a caged body that duplicates no live copy is never let out (orphanpurge.h Decide) */
void t_t948d_caged_duplicate()
{
    using namespace coopor;
    int why = -1;
    Seen s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1;
    if (Decide(s, &why) != kActCageOut) Fail("T-948 d: a caged duplicate of a live copy is let out of its cage first");
    s.cageQueued = 1;
    if (Decide(s, &why) != kActLater || why != kWhyCageWait) Fail("T-948 d: a queued cage-out waits for the safe point");
    s.cageQueued = 0; s.cageTried = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyCageStuck) Fail("T-948 d: still caged after its cage-out ran - left standing, never called twice");
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.cageAddr = 0;
    if (Decide(s, &why) != kActSkip || why != kWhyCageNoAddr) Fail("T-948 d: no setPrisonMode address - never destroyed in its cage");
    s = SeenNone(); s.dupOfLiveCopy = 1; s.cageTried = 1;
    if (Decide(s, &why) != kActDestroy) Fail("T-948 d: a duplicate out of its cage is removed");
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.nearPlayer = 1;
    if (Decide(s, &why) != kActCageOut) Fail("T-948 d: a caged duplicate near a player is let out too");
    /* a caged body that duplicates no live copy: today's skip, whatever its cage marks say */
    s = SeenNone(); s.inSomething = 2;
    if (Decide(s, &why) != kActSkip || why != kWhyCaged) Fail("T-948 d: a caged body with no live copy beside it is never let out");
    s.cageQueued = 1; s.cageTried = 1; s.nearPlayer = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyCaged) Fail("T-948 d: a caged non-duplicate stays skipped");
    /* the player-tie tests, the registration, the stand-in rule, the area and the waits still come first */
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.guarded = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyPlayerTied) Fail("T-948 d: a player-tied caged duplicate is never touched");
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.beingCarried = 1; s.carrierFound = 1; s.carrierIsPlayer = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyCarriedByPlayer) Fail("T-948 d: a caged duplicate carried by a player is never touched");
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.known = 1;
    if (Decide(s, &why) != kActNone) Fail("T-948 d: a registered body is never a duplicate orphan");
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.standIn = 1;
    if (Decide(s, &why) != kActNone) Fail("T-948 d: a stand-in is the stand-in purge's");
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.areaOther = 0;
    if (Decide(s, &why) != kActNone) Fail("T-948 d: outside another game's area nothing is done");
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.areaOther = -1;
    if (Decide(s, &why) != kActNone) Fail("T-948 d: with no area map nothing is done");
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.fightNear = 1;
    if (Decide(s, &why) != kActLater || why != kWhyInCombatWithPlayer) Fail("T-948 d: a caged duplicate in a fight with a player waits");
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.linkedMs = 0;
    if (Decide(s, &why) != kActLater || why != kWhyLinkTooFresh) Fail("T-948 d: a caged duplicate waits for the link clock");
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.notedMs = 0;
    if (Decide(s, &why) != kActLater || why != kWhyNotedTooShort) Fail("T-948 d: a caged duplicate waits for the noted clock");
    if (HoldWhilePending(kActLater, kWhyCageWait) != 1 || HoldWhilePending(kActCageOut, kWhyNone) != 1) Fail("T-948 d fold 1: a body is held from its let-out until it is removed");
    if (HoldWhilePending(kActSkip, kWhyCageStuck) != 0 || HoldWhilePending(kActSkip, kWhyCaged) != 0) Fail("T-948 d fold 1: a body left standing is not held");
    /* fold 1: the stronger caged test, and one copy for one body */
    if (CagedDupMatch(50.0f, 0.0f, 0) != 0) Fail("T-948 d fold 1: a caged body 50 units from a same-kind copy is not its duplicate");
    if (CagedDupMatch(2.5f, 0.0f, 0) != 1 || CagedDupMatch(0.0f, 3.0f, 0) != 1 || CagedDupMatch(3.1f, 0.0f, 0) != 0) Fail("T-948 d fold 1: the 3-unit caged radius");
    if (CagedDupMatch(50.0f, 0.0f, 1) != 1 || CagedDupMatch(101.0f, 0.0f, 1) != 0) Fail("T-948 d fold 1: the same cage within 100 units matches, not beyond");
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = CagedDupMatch(50.0f, 0.0f, 0);
    if (Decide(s, &why) != kActSkip || why != kWhyCaged) Fail("T-948 d fold 1: a caged body 50 units from a same-kind copy is skipped as caged");
    {
        const float dx[1] = { 1.0f }, dz[1] = { 0.0f }; const int match[1] = { 1 }; int taken[1] = { 0 };
        const int first = PickPairedCopy(dx, dz, match, taken, 1);
        if (first == 0) taken[0] = 1;
        const int second = PickPairedCopy(dx, dz, match, taken, 1);
        Seen a = SeenNone(); a.inSomething = 2; a.dupOfLiveCopy = first >= 0 ? 1 : 0;
        Seen b = SeenNone(); b.inSomething = 2; b.dupOfLiveCopy = second >= 0 ? 1 : 0;
        int out = 0;
        if (Decide(a, &why) == kActCageOut) ++out;
        if (Decide(b, &why) == kActCageOut) ++out;
        if (first != 0 || second != -1 || out != 1) Fail("T-948 d fold 1: two caged bodies and one copy - only one is removed");
    }
    /* fold 3: two bodies in the copy's own cage on its spot are both its duplicates; one of them in another cage is not */
    {
        const float dx[1] = { 1.0f }, dz[1] = { 0.0f };
        int m1[1], t1[1], m2[1], t2[1];
        m1[0] = CagedDupMatch(1.0f, 0.0f, 1); t1[0] = CopyTakenFor(0, SameCageTight(1.0f, 0.0f, 1));
        const int p1 = PickPairedCopy(dx, dz, m1, t1, 1);   /* the first body pairs with the copy */
        m2[0] = CagedDupMatch(1.0f, 0.0f, 1); t2[0] = CopyTakenFor(p1 == 0 ? 1 : 0, SameCageTight(1.0f, 0.0f, 1));
        const int p2same = PickPairedCopy(dx, dz, m2, t2, 1);
        m2[0] = CagedDupMatch(1.0f, 0.0f, 0); t2[0] = CopyTakenFor(p1 == 0 ? 1 : 0, SameCageTight(1.0f, 0.0f, 0));
        const int p2other = PickPairedCopy(dx, dz, m2, t2, 1);
        Seen a = SeenNone(); a.inSomething = 2; a.dupOfLiveCopy = p1 >= 0 ? 1 : 0;
        Seen b = SeenNone(); b.inSomething = 2; b.dupOfLiveCopy = p2same >= 0 ? 1 : 0;
        Seen o = SeenNone(); o.inSomething = 2; o.dupOfLiveCopy = p2other >= 0 ? 1 : 0;
        int same = 0, other = 0;
        if (Decide(a, &why) == kActCageOut) { ++same; ++other; }
        if (Decide(b, &why) == kActCageOut) ++same;
        if (Decide(o, &why) == kActCageOut) ++other;
        if (p1 != 0 || p2same != 0 || same != 2) Fail("T-948 d fold 3: two bodies in the copy's cage on its spot - both removed");
        if (p2other != -1 || other != 1) Fail("T-948 d fold 3: two bodies on the copy's spot, one in another cage - only one removed");
        if (SameCageTight(3.1f, 0.0f, 1) != 0 || SameCageTight(2.0f, 0.0f, 0) != 0 || CopyTakenFor(1, 0) != 1 || CopyTakenFor(0, 0) != 0)
            Fail("T-948 d fold 3: the same-cage exception is only the copy's own cage within 3 units");
    }
    /* fold 4: the same-cage exception only where a second same-kind prisoner can only be a split */
    if (SameCageExtraAllowed(1, 1) != 1 || SameCageExtraAllowed(1, 2) != 1) Fail("T-948 d fold 4: a one-place cage - the exception holds");
    if (SameCageExtraAllowed(2, 2) != 0) Fail("T-948 d fold 4: two places holding two people - no exception");
    if (SameCageExtraAllowed(2, 3) != 1) Fail("T-948 d fold 4: two places holding three people - the exception holds");
    if (SameCageExtraAllowed(-1, 1) != 0 || SameCageExtraAllowed(2, -1) != 0 || SameCageExtraAllowed(-1, -1) != 0) Fail("T-948 d fold 4: unread - no exception");
    {
        const float dx[1] = { 1.0f }, dz[1] = { 0.0f }; const int m[1] = { 1 };
        int t[1];
        t[0] = CopyTakenFor(1, (SameCageTight(1.0f, 0.0f, 1) != 0 && SameCageExtraAllowed(2, 2) != 0) ? 1 : 0);
        if (PickPairedCopy(dx, dz, m, t, 1) != -1) Fail("T-948 d fold 4: a second body in a two-place cage holding two is not the paired copy's duplicate");
        t[0] = CopyTakenFor(1, (SameCageTight(1.0f, 0.0f, 1) != 0 && SameCageExtraAllowed(1, 2) != 0) ? 1 : 0);
        if (PickPairedCopy(dx, dz, m, t, 1) != 0) Fail("T-948 d fold 4: a second body in a one-place cage is the copy's duplicate");
    }
    /* fold 2: a body the decision refuses does not hold the copy; only a queued let-out records it; a removal keeps it */
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.guarded = 1;
    { const int a = Decide(s, &why); if (PairRecordOnAct(a) != 0 || PairReleaseOnAct(a) != 1) Fail("T-948 d fold 2: a player-tied body does not hold the copy"); }
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.cageAddr = 0;
    { const int a = Decide(s, &why); if (PairRecordOnAct(a) != 0 || PairReleaseOnAct(a) != 1) Fail("T-948 d fold 2: a body with no let-out address does not hold the copy"); }
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.cageTried = 1;
    { const int a = Decide(s, &why); if (PairRecordOnAct(a) != 0 || PairReleaseOnAct(a) != 1) Fail("T-948 d fold 2: a body still caged after its let-out releases the copy"); }
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.known = 1;
    { const int a = Decide(s, &why); if (PairRecordOnAct(a) != 0 || PairReleaseOnAct(a) != 1) Fail("T-948 d fold 2: a body no longer an orphan releases the copy"); }
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1; s.fightNear = 1;
    { const int a = Decide(s, &why); if (PairRecordOnAct(a) != 0 || PairReleaseOnAct(a) != 0) Fail("T-948 d fold 2: a waiting body neither takes nor gives up the copy"); }
    s = SeenNone(); s.inSomething = 2; s.dupOfLiveCopy = 1;
    { const int a = Decide(s, &why); if (PairRecordOnAct(a) != 1 || PairReleaseOnAct(a) != 0) Fail("T-948 d fold 2: a queued let-out records the copy"); }
    s = SeenNone(); s.dupOfLiveCopy = 1; s.cageTried = 1;
    { const int a = Decide(s, &why); if (a != kActDestroy || PairReleaseOnAct(a) != 0) Fail("T-948 d fold 2: a removal keeps the copy paired"); }
    {
        const float dx[2] = { 5.0f, 1.0f }, dz[2] = { 0.0f, 0.0f }; const int match[2] = { 1, 1 }; int taken[2] = { 0, 0 };
        if (PickPairedCopy(dx, dz, match, taken, 2) != 1) Fail("T-948 d fold 1: the nearest copy is paired");
        taken[1] = 1; if (PickPairedCopy(dx, dz, match, taken, 2) != 0) Fail("T-948 d fold 1: the next unpaired copy is taken");
        const int none[2] = { 0, 1 }; if (PickPairedCopy(dx, dz, none, taken, 2) != -1) Fail("T-948 d fold 1: no unpaired matching copy - none");
    }
    if (std::string(WhyToken(kWhyCageNoAddr)) != "cageNoAddr" || std::string(WhyToken(kWhyCageWait)) != "cageWait"
        || std::string(WhyToken(kWhyCageStuck)) != "cageStuck" || kWhyCount != 17) Fail("T-948 d: report tokens");
}

/* T-948 d: the squad a save split off another game's people is removed only when every input says so (squadorigin.h SaveSplitDrop) */
void t_t948d_save_split_squad()
{
    using namespace sqorigin;
    SaveSplitSeen s = SaveSplitSeenDroppable();
    if (SaveSplitDrop(s) != kSplitDrop) Fail("T-948 d: the save-split squad (my block, decided as the holder's, empty, unbound, unrecorded) is removed");
    s = SaveSplitSeenDroppable(); s.readable = 0;      if (SaveSplitDrop(s) != kSplitUnread) Fail("T-948 d squad: unread kept");
    s = SaveSplitSeenDroppable(); s.worldFaction = 0;  if (SaveSplitDrop(s) != kSplitNotWorld) Fail("T-948 d squad: a player's / peer's / stand-in's squad kept");
    s = SaveSplitSeenDroppable(); s.context = 1;       if (SaveSplitDrop(s) != kSplitContext) Fail("T-948 d squad: a context squad kept");
    s = SaveSplitSeenDroppable(); s.bound = 1;         if (SaveSplitDrop(s) != kSplitBound) Fail("T-948 d squad: a bound squad kept");
    s = SaveSplitSeenDroppable(); s.recorded = 1;      if (SaveSplitDrop(s) != kSplitRecorded) Fail("T-948 d squad: a recorded squad kept");
    s = SaveSplitSeenDroppable(); s.handedOver = 1;    if (SaveSplitDrop(s) != kSplitHandedOver) Fail("T-948 d squad: a handed-over squad kept");
    s = SaveSplitSeenDroppable(); s.queued = 1;        if (SaveSplitDrop(s) != kSplitQueued) Fail("T-948 d squad: one already on the removal road is left to it");
    s = SaveSplitSeenDroppable(); s.uidMembers = 1; s.members = 1; if (SaveSplitDrop(s) != kSplitUidMember) Fail("T-948 d squad: a member with a uid keeps it");
    s = SaveSplitSeenDroppable(); s.uidMembers = -1;   if (SaveSplitDrop(s) != kSplitUidMember) Fail("T-948 d squad: an unread uid count keeps it");
    s = SaveSplitSeenDroppable(); s.members = 1;       if (SaveSplitDrop(s) != kSplitNotEmpty) Fail("T-948 d squad: a member still in it keeps it");
    s = SaveSplitSeenDroppable(); s.saveMade = 0;      if (SaveSplitDrop(s) != kSplitNotSave) Fail("T-948 d squad: a squad made since the load kept");
    s = SaveSplitSeenDroppable(); s.blockOwner = 0;    if (SaveSplitDrop(s) != kSplitNotMyBlock) Fail("T-948 d squad: another block's squad kept");
    s = SaveSplitSeenDroppable(); s.mySlot = -1;       if (SaveSplitDrop(s) != kSplitNotMyBlock) Fail("T-948 d squad: no slot of my own - kept");
    {
        const int whys[5] = { kByFloor, kByHandover, kByMembers, kByRecord, kFallback };
        for (int i = 0; i < 5; ++i)
        {
            s = SaveSplitSeenDroppable(); s.originWhy = whys[i];
            if (SaveSplitDrop(s) != kSplitNotByHolder) Fail("T-948 d squad: an origin not from the area holder keeps it");
        }
    }
    s = SaveSplitSeenDroppable(); s.originSlot = 1;    if (SaveSplitDrop(s) != kSplitOriginMine) Fail("T-948 d squad: decided as mine - kept");
    s = SaveSplitSeenDroppable(); s.originSlot = -1;   if (SaveSplitDrop(s) != kSplitOriginMine) Fail("T-948 d squad: no origin slot - kept");
    s = SaveSplitSeenDroppable(); s.heldByOther = -1;  if (SaveSplitDrop(s) != kSplitNoMap) Fail("T-948 d squad: no fresh area map - kept");
    s = SaveSplitSeenDroppable(); s.heldByOther = 0;   if (SaveSplitDrop(s) != kSplitNotOtherHeld) Fail("T-948 d squad: an area no other game holds - kept");
    s = SaveSplitSeenDroppable(); s.heldByOther = -2;  if (SaveSplitDrop(s) != kSplitNotOtherHeld) Fail("T-948 d squad: a frozen area - kept");
    s = SaveSplitSeenDroppable(); s.holderInWorld = 0; if (SaveSplitDrop(s) != kSplitHolderAway) Fail("T-948 d squad: a holder not in the world - kept");
    s = SaveSplitSeenDroppable(); s.holderSlot = -1;   if (SaveSplitDrop(s) != kSplitHolderAway) Fail("T-948 d squad: no holder - kept");
    s = SaveSplitSeenDroppable(); s.holderSlot = 1;    if (SaveSplitDrop(s) != kSplitHolderAway) Fail("T-948 d squad: this game holds the area - kept");
    s = SaveSplitSeenDroppable(); s.holderSlot = 2;    if (SaveSplitDrop(s) != kSplitHolderAway) Fail("T-948 d squad: the holder is not the origin's slot - kept");
    /* every seat decides alike: the same squad on a game in slot 0 with the holder in slot 1, and in slot 5 with the holder in slot 2 */
    s = SaveSplitSeenDroppable(); s.mySlot = 0; s.blockOwner = 0; s.originSlot = 1; s.holderSlot = 1;
    if (SaveSplitDrop(s) != kSplitDrop) Fail("T-948 d squad: the rule on seat 0 differs");
    s = SaveSplitSeenDroppable(); s.mySlot = 5; s.blockOwner = 5; s.originSlot = 2; s.holderSlot = 2;
    if (SaveSplitDrop(s) != kSplitDrop) Fail("T-948 d squad: the rule on seat 5 differs");
    /* fold 2: a sleeping squad goes only when its own sleep record reads 0 people - at the decision and at the removal tick */
    s = SaveSplitSeenDroppable(); s.asleep = 1; s.sleepCount = 1;
    if (SaveSplitDrop(s) != kSplitSleepMembers) Fail("T-948 d fold 2: a sleeping squad whose record counts 1 person is kept");
    s.sleepCount = -1; if (SaveSplitDrop(s) != kSplitSleepUnread) Fail("T-948 d fold 2: a sleeping squad whose count is unreadable is kept");
    s.sleepCount = 0; if (SaveSplitDrop(s) != kSplitDrop) Fail("T-948 d fold 2: a sleeping squad whose record counts 0 is removed");
    s = SaveSplitSeenDroppable(); s.asleep = 0; s.sleepCount = -1; if (SaveSplitDrop(s) != kSplitDrop) Fail("T-948 d fold 2: an awake squad is not judged by a sleep record");
    if (SaveSplitTickKeep(0, 0, 1) != 1 || SaveSplitTickKeep(0, 0, -1) != 1 || SaveSplitTickKeep(0, 0, 0) != 0) Fail("T-948 d fold 2: the tick keeps a sleeping squad unless its record reads 0");
    if (SaveSplitTickKeep(1, 1, 0) != 1 || SaveSplitTickKeep(1, 0, 0) != 0 || SaveSplitTickKeep(1, 0, 5) != 0) Fail("T-948 d fold 2: the tick keeps an awake squad only while someone is in it");
    for (int w = 1; w < kSplitWhyCount; ++w) if (std::string(SplitWhyToken(w)) == "?") Fail("T-948 d squad: a reason has no token");
    if (std::string(SplitWhyToken(kSplitDrop)) != "?" || std::string(SplitWhyToken(kSplitWhyCount)) != "?") Fail("T-948 d squad: 0 / count are not reasons");
}

/* ---- T-1047 step 5: the outpost link (townkeep.h outpost id, baseshow.h every game a copy / HideStep, piecerec.h LIVE bit 5) ---- */
townkeep::TownRow T1047fRow(int owner, float x, float z, unsigned id, const char* key)
{
    townkeep::TownRow t;
    t.owner = owner; t.pos[0] = x; t.pos[1] = 0.0f; t.pos[2] = z; t.name = "Base"; t.id = id;
    if (key != 0) t.keys.push_back(key);
    return t;
}
/* the world server mints the id: a said row within 50 u of a row on file keeps its id, one sharing a key keeps that row's, one matching
   neither takes a carried (reset) outpost's id by the same rule, else a new one past the highest on file */
void t_t1047f_outpost_id_mint()
{
    using namespace townkeep;
    if (NextOutpostId(std::vector<TownRow>(), std::vector<TownRow>()) != 1u) Fail("t1047f: the first outpost id is not 1");
    std::vector<TownRow> all;
    all.push_back(T1047fRow(1, 100.0f, 100.0f, 7u, "12-a.mod@3,41@1000,1000,0"));
    all.push_back(T1047fRow(1, 5000.0f, 5000.0f, 9u, "12-b.mod@3,41@50000,50000,0"));
    all.push_back(T1047fRow(2, 100.0f, 100.0f, 3u, 0));
    std::vector<TownRow> carried;
    carried.push_back(T1047fRow(1, 30000.0f, 30000.0f, 42u, 0));
    unsigned next = NextOutpostId(all, carried);
    if (next != 43u) Fail("t1047f: the next id is not one past the highest on file (both files): " + I((int)next));
    std::vector<TownRow> said;
    said.push_back(T1047fRow(-1, 120.0f, 110.0f, 0u, 0));                                  /* centre within 50 u of id 7 */
    said.push_back(T1047fRow(-1, 9000.0f, 9000.0f, 0u, "12-b.mod@3,41@50000,50000,0"));    /* drifted, shares id 9's key */
    said.push_back(T1047fRow(-1, 30010.0f, 30000.0f, 0u, 0));                              /* a reset carried it: id 42 */
    said.push_back(T1047fRow(-1, 20000.0f, 20000.0f, 0u, 0));                              /* new */
    for (size_t i = 0; i < said.size(); ++i) said[i].owner = -1;
    ReplaceSlotTowns(&all, 1, said, 1, &next, &carried);
    unsigned got[4] = { 0, 0, 0, 0 };
    int other = 0;
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].owner != 1) { if (all[i].id == 3u) ++other; continue; }
        for (size_t j = 0; j < said.size(); ++j) if (all[i].pos[0] == said[j].pos[0] && all[i].pos[2] == said[j].pos[2]) got[j] = all[i].id;
    }
    if (got[0] != 7u) Fail("t1047f: a said outpost at a row's centre did not keep its id: " + I((int)got[0]));
    if (got[1] != 9u) Fail("t1047f: a said outpost sharing a key did not keep its row's id: " + I((int)got[1]));
    if (got[2] != 42u) Fail("t1047f: a said outpost a reset carried did not take the carried id: " + I((int)got[2]));
    if (got[3] != 43u || next != 44u) Fail("t1047f: a new outpost did not take a new id: " + I((int)got[3]));
    if (other != 1) Fail("t1047f: another slot's row lost its id");
    /* the same SAY again: every id kept, nothing minted */
    std::vector<TownRow> again(all);
    ReplaceSlotTowns(&again, 1, said, 1, &next, &carried);
    if (next != 44u || EncodeTownsFile(kPlayerTownsHeader, again, true) != EncodeTownsFile(kPlayerTownsHeader, all, true)) Fail("t1047f: the same SAY again minted or moved an id");
    /* a carried id a row of the slot already holds is never taken twice */
    std::vector<TownRow> two;
    two.push_back(T1047fRow(1, 30000.0f, 30000.0f, 42u, 0));
    std::vector<TownRow> s2;
    s2.push_back(T1047fRow(-1, 30000.0f, 30000.0f, 0u, 0));
    s2.push_back(T1047fRow(-1, 30020.0f, 30000.0f, 0u, 0));
    unsigned n2 = 50;
    ReplaceSlotTowns(&two, 1, s2, 1, &n2, &carried);
    if (two.size() != 2 || two[0].id != 42u || two[1].id != 50u) Fail("t1047f: a carried id was given to a second outpost");
}
/* the files carry the id (format 4); a format-3 file reads with no ids, which are then given (GiveMissingIds) - a duplicate too */
void t_t1047f_towns_file_ids()
{
    using namespace townkeep;
    std::vector<TownRow> rows;
    rows.push_back(T1047fRow(1, 100.0f, 100.0f, 7u, "12-a.mod@3,41@1000,1000,0"));
    rows.push_back(T1047fRow(2, 200.0f, 200.0f, 12u, 0));
    const std::string text = EncodeTownsFile(kPlayerTownsHeader, rows, true);
    std::vector<TownRow> back;
    std::string why;
    if (!DecodeTownsFile(text, kPlayerTownsHeader, true, &back, &why) || back.size() != 2 || back[0].id != 7u || back[1].id != 12u) Fail("t1047f: the outpost id does not survive the file: " + why);
    if (text.find("player-towns\t5\n") != 0) Fail("t1047f: the file is not format 5 (T-940: with the anchor)");
    /* format 3: the same lines without the id field */
    std::string old = "player-towns\t3\n";
    size_t at = text.find('\n') + 1;
    while (at < text.size())
    {
        size_t e = text.find('\n', at);
        std::string line = text.substr(at, e - at);
        if (line.size() > 2 && line[0] == 'T') for (int k = 0; k < 3; ++k) line = line.substr(0, line.rfind('\t'));   /* the id and (T-940) the anchor x, z */
        old += line + "\n";
        at = e + 1;
    }
    if (!DecodeTownsFile(old, kPlayerTownsHeader, true, &back, &why) || back.size() != 2 || back[0].id != 0u || back[1].id != 0u || back[0].keys.size() != 1)
        Fail("t1047f: a format-3 file does not read with no ids: " + why);
    unsigned next = NextOutpostId(back, std::vector<TownRow>());
    std::set<unsigned> taken;
    if (next != 1u || GiveMissingIds(&back, &next, &taken) != 2 || back[0].id != 1u || back[1].id != 2u || next != 3u) Fail("t1047f: an older file's rows are not given ids");
    back[1].id = 1u;   /* a hand-edited duplicate */
    taken.clear(); next = 3u;
    if (GiveMissingIds(&back, &next, &taken) != 1 || back[0].id != 1u || back[1].id != 3u) Fail("t1047f: a duplicate id is kept on two outposts");
    std::string neg = text;
    const size_t t1 = neg.find("\t7\t");
    if (t1 == std::string::npos) { Fail("t1047f: the id field is not before the anchor on the line"); return; }
    neg.replace(t1, 3, "\t-7\t");
    if (DecodeTownsFile(neg, kPlayerTownsHeader, true, &back, &why)) Fail("t1047f: a negative outpost id is read");
}
/* the writer's value, the owner's mark, the apply step, and the drain's order (the link before the LIVE power) */
void t_t1047f_link_apply_order()
{
    using namespace piecerec;
    if (PieceTownWriteId(1, 5u, 3u) != 5u || PieceTownWriteId(1, 0u, 3u) != 3u || PieceTownWriteId(0, 5u, 3u) != 3u || PieceTownWriteId(0, 5u, 0u) != 0u)
        Fail("t1047f: the link a write carries is not the owner's engine link, else the stored one");
    if (PieceTownMarkDue(1, 1, 5u, 3u, 0u) != 1 || PieceTownMarkDue(1, 1, 5u, 5u, 0u) != 0 || PieceTownMarkDue(1, 1, 5u, 3u, 5u) != 0
        || PieceTownMarkDue(0, 1, 5u, 3u, 0u) != 0 || PieceTownMarkDue(1, 0, 5u, 3u, 0u) != 0 || PieceTownMarkDue(1, 1, 0u, 3u, 0u) != 0)
        Fail("t1047f: the owner's link write is marked when it should not be, or not when it should");
    if (PieceTownApplyStep(0, 0, 0u, 1, 1, 0) != kTlUnknown) Fail("t1047f: a record with no link relinks");
    if (PieceTownApplyStep(0, 1, 4u, 1, 1, 0) != kTlOwn) Fail("t1047f: a record's link is applied over the owner's own engine link");
    if (PieceTownApplyStep(0, 0, 4u, 0, 1, 0) != kTlInside) Fail("t1047f: a piece inside a building is relinked");
    if (PieceTownApplyStep(0, 0, 4u, 1, 0, 0) != kTlNoTown) Fail("t1047f: a link with no town here is not counted as such");
    if (PieceTownApplyStep(0, 0, 4u, 1, 1, 1) != kTlSame || PieceTownApplyStep(0, 0, 4u, 1, 1, 0) != kTlRelink) Fail("t1047f: the relink step is wrong");
    if (PieceTownApplyStep(1, 0, 4u, 1, 1, 0) != kTlUnknown) Fail("t1047f: a removal mark relinks");
    const std::string s = T313ReadFile("../coop-plugin/build.cpp");
    if (s.empty()) { Fail("t1047f: build.cpp not read"); return; }
    const size_t link = s.find("TlApplyKey(ready[i]);"), live = s.find("const int res = PwLiveApply(ready[i], lv);");
    if (link == std::string::npos || live == std::string::npos || link > live) Fail("t1047f: the drain does not apply the record's link before its LIVE power values");
    if (s.find("if (w.removed == 0) PcrTownFill(id, w, slot, &b.live);") == std::string::npos) Fail("t1047f: a record write does not carry the link");
    if (s.find("if (TlRecordLinkOf(r->first) != 0) { ++g_tlShowLinkYield; continue; }") == std::string::npos) Fail("t1047f: the key-row link fights the record's link");
}
/* every game gets every player's outposts (own, teammates', others'), with id, kind and handle words; another player's copy is hidden
   until discovered (the engine's own mark); a discovered id is kept on the copy list */
void t_t1047f_hidden_rule()
{
    using namespace baseshow;
    if (HideStep(kShowOther, 1, 1, 0) != kHideHide) Fail("t1047f: another player's new copy is not hidden");
    if (HideStep(kShowOther, 1, 0, 0) != kHideNone) Fail("t1047f: a hidden copy is hidden twice");
    if (HideStep(kShowOther, 1, 0, 1) != kHideShow || HideStep(kShowOther, 1, 1, 1) != kHideNone) Fail("t1047f: a copy discovered earlier is hidden again");
    if (HideStep(kShowOther, 0, 1, 0) != kHideNoted || HideStep(kShowOther, 0, 0, 0) != kHideNone || HideStep(kShowOther, 0, -1, 0) != kHideNone) Fail("t1047f: the discovery is not noted by the engine's mark");
    if (HideStep(kShowMate, 1, 1, 0) != kHideNone || HideStep(kShowMate, 0, 0, 0) != kHideShow) Fail("t1047f: a teammate's copy is not on the map at once");
    if (HideStep(kShowOwn, 1, 1, 0) != kHideNone) Fail("t1047f: the player's own outpost is touched");
    std::vector<townkeep::TownRow> said;
    said.push_back(T1047fRow(3, 1.0f, 1.0f, 30u, 0));
    said.push_back(T1047fRow(0, 2.0f, 2.0f, 10u, 0));
    said.push_back(T1047fRow(1, 3.0f, 3.0f, 11u, 0));
    said[1].hand[0] = 7; said[1].hand[4] = 99;
    std::set<int> mates; mates.insert(1);
    std::map<int, std::string> names;
    int cut = 0;
    const std::vector<ShowRow> rows = RowsForAll(said, 0, mates, names, &cut);
    if (rows.size() != 3 || rows[0].kind != kShowOwn || rows[0].id != 10u || rows[0].hand[4] != 99 || rows[1].kind != kShowMate || rows[1].id != 11u
        || rows[2].kind != kShowOther || rows[2].id != 30u || cut != 0)
        Fail("t1047f: SHOW does not carry every player's outposts, own first, others last, with id and handle words");
    if (RowsFor(said, 0, mates, names, &cut).size() != 1) Fail("t1047f: the teammates' view changed");
    std::vector<char> b;
    unsigned flags = 9;
    std::vector<ShowRow> back;
    if (!Encode(&b, 0, rows) || Decode(&b[0], b.size(), &flags, &back) != 1 || back.size() != 3 || back[2].kind != kShowOther || back[2].id != 30u || back[0].hand[4] != 99 || back[0].hand[0] != 7)
        Fail("t1047f: the SHOW row's id, kind and handle words do not round trip");
    if (Decode(&b[0], b.size() - 20, &flags, &back) != 0) Fail("t1047f: a SHOW of the older row shape reads");
    std::vector<ShowRow> badKind(rows);
    badKind[0].kind = 3;
    if (Encode(&b, 0, badKind)) Fail("t1047f: an unknown row kind is sent");
    const std::vector<townkeep::TownRow> kr = KeyRowsFor(said, rows);
    for (size_t i = 0; i < kr.size(); ++i) if (kr[i].owner == 0) Fail("t1047f: a key row is sent for the viewer's own outpost");
    if (kr.size() != 2) Fail("t1047f: the copies' key rows are not sent");
    /* pass 0: the id pairs a moved copy with its row */
    std::vector<Shown> shown(1);
    shown[0].owner = 3; shown[0].id = 30u; shown[0].pos[0] = 90000.0f; shown[0].pos[2] = 90000.0f;
    std::vector<ShowRow> one(1, rows[2]);
    std::vector<int> rowTo; std::vector<size_t> gone;
    MatchIdent(shown, one, std::vector<townkeep::TownRow>(), &rowTo, &gone);
    if (rowTo.size() != 1 || rowTo[0] != 0 || !gone.empty()) Fail("t1047f: a copy is not matched to its row by the outpost id");
    /* fold 1: the discovered ids are kept per save (t1047f1_discovery_per_save), not on the copy list: format 1 as before */
    std::vector<CopyRow> cb; std::string why;
    if (EncodeCopyList(std::vector<CopyRow>()) != "shown-town-copies\t1\n" || DecodeCopyList("shown-town-copies\t1\n", &cb, &why) != 0) Fail("t1047f: the copy list is not format 1");
}
/* ---- T-1047 step 5 fold 1 ---- */
/* fix 2: the world server mints ids 1..3, the owner's game then says only two outposts (the top id's outpost removed), the file is
   written and read back as at a restart: the next new outpost takes 4, never 3 again (without the N line the next id would be one past
   the highest on file: 3) */
void t_t1047f1_id_never_reused()
{
    using namespace townkeep;
    std::vector<TownRow> all;
    std::vector<TownRow> said;
    said.push_back(T1047fRow(-1, 100.0f, 100.0f, 0u, 0));
    said.push_back(T1047fRow(-1, 9000.0f, 9000.0f, 0u, 0));
    said.push_back(T1047fRow(-1, 30000.0f, 30000.0f, 0u, 0));
    unsigned next = NextIdAtLoad(0u, all, std::vector<TownRow>());
    ReplaceSlotTowns(&all, 1, said, 1, &next, 0);
    if (all.size() != 3 || next != 4u) { Fail("t1047f1: three new outposts did not take ids 1..3"); return; }
    said.pop_back();   /* the outpost with id 3 removed */
    ReplaceSlotTowns(&all, 1, said, 1, &next, 0);
    unsigned top = 0;
    for (size_t i = 0; i < all.size(); ++i) if (all[i].id > top) top = all[i].id;
    if (all.size() != 2 || top != 2u) { Fail("t1047f1: the removed outpost's row stayed"); return; }
    const std::string file = EncodeTownsFile(kPlayerTownsHeader, all, true, next);
    std::vector<TownRow> back;
    std::string why;
    unsigned fileNext = 0;
    if (!DecodeTownsFile(file, kPlayerTownsHeader, true, &back, &why, &fileNext) || back.size() != 2 || fileNext != 4u) { Fail("t1047f1: the next id does not survive the file: " + why); return; }
    unsigned restart = NextIdAtLoad(fileNext, back, std::vector<TownRow>());
    if (restart != 4u) Fail("t1047f1: after a restart the next id is " + I((int)restart) + ", not 4 - a removed id would be minted again");
    std::vector<TownRow> s2(said);
    s2.push_back(T1047fRow(-1, 60000.0f, 60000.0f, 0u, 0));
    ReplaceSlotTowns(&back, 1, s2, 1, &restart, 0);
    int got4 = 0, got3 = 0;
    for (size_t i = 0; i < back.size(); ++i) { if (back[i].id == 4u) ++got4; if (back[i].id == 3u) ++got3; }
    if (got4 != 1 || got3 != 0) Fail("t1047f1: the new outpost after the restart did not take 4");
    /* a file with no N line (written before) reads, its next id one past the highest; a bad or second N line is refused */
    if (!DecodeTownsFile(EncodeTownsFile(kPlayerTownsHeader, all, true), kPlayerTownsHeader, true, &back, &why, &fileNext) || fileNext != 0u
        || NextIdAtLoad(fileNext, back, std::vector<TownRow>()) != 3u)
        Fail("t1047f1: a file with no next-id line does not read");
    if (DecodeTownsFile("player-towns\t4\nN\t0\n", kPlayerTownsHeader, true, &back, &why, &fileNext)) Fail("t1047f1: a next id of 0 reads");
    if (DecodeTownsFile("player-towns\t4\nN\t5\nN\t6\n", kPlayerTownsHeader, true, &back, &why, &fileNext)) Fail("t1047f1: two next-id lines read");
    if (DecodeTownsFile("player-towns\t3\nN\t5\n", kPlayerTownsHeader, true, &back, &why, &fileNext)) Fail("t1047f1: a format-3 file with a next-id line reads");
    if (NextIdAtLoad(2u, all, std::vector<TownRow>()) != 3u) Fail("t1047f1: a next id below the ids on file is taken");
}
/* the outposts one game holds, for the link tests: its own (slot 1, id 10), a teammate's copy (slot 2, id 20), another player's copy
   (slot 3, id 30) */
std::vector<baseshow::IdTown> T1047f1Towns()
{
    std::vector<baseshow::IdTown> t(3);
    t[0].id = 10u; t[0].owner = 1; t[0].team = 1; t[0].hand[0] = 4; t[0].hand[4] = 101;
    t[1].id = 20u; t[1].owner = 2; t[1].team = 1; t[1].hand[0] = 4; t[1].hand[4] = 202;
    t[2].id = 30u; t[2].owner = 3; t[2].team = 0; t[2].hand[0] = 4; t[2].hand[4] = 303;
    return t;
}
/* fix 2: a record of slot 1's piece names outpost 30 (slot 3's, not a teammate): no game links it there; its own, or its teammate's
   (T-923: a piece placed in a teammate's outpost), is linked */
void t_t1047f1_link_owner_checked()
{
    using namespace baseshow;
    const std::vector<IdTown> t = T1047f1Towns();
    std::vector<int> same(3, 0);
    same[1] = 1;   /* slot 2 and slot 1 are one team */
    if (TownOfIdFor(t, 30u, 1, same) != kIdTownOwnerBad) Fail("t1047f1: a link into another player's outpost is not refused");
    if (TownOfIdFor(t, 10u, 1, same) != 0) Fail("t1047f1: a link into the owner's own outpost is refused");
    if (TownOfIdFor(t, 20u, 1, same) != 1) Fail("t1047f1: a link into a teammate's outpost is refused");
    if (TownOfIdFor(t, 30u, 3, same) != 2) Fail("t1047f1: slot 3's own link is refused");
    if (TownOfIdFor(t, 99u, 1, same) != kIdTownNone || TownOfIdFor(t, 0u, 1, same) != kIdTownNone) Fail("t1047f1: an unknown id names a town");
    std::vector<IdTown> g(t);
    g[0].going = 1;
    if (TownOfIdFor(g, 10u, 1, same) != kIdTownNone) Fail("t1047f1: a going town is linked");
    std::vector<int> none(3, 0);
    if (TownOfIdFor(t, 20u, 1, none) != kIdTownOwnerBad) Fail("t1047f1: an ex-teammate's outpost is still linked");
    if (LinkOwnerOk(-1, 1, 1) != 0 || LinkOwnerOk(2, -1, 1) != 0) Fail("t1047f1: an unknown owner passes");
}
/* fix 1: the owner's game reads its own piece's engine link - a non-teammate's copy is never written into the record (the stored
   link is kept), and such a link is seen to move; its own and a teammate's outpost are written */
void t_t1047f1_own_piece_team_only()
{
    using namespace baseshow;
    const std::vector<IdTown> t = T1047f1Towns();
    const int own[5] = { 4, 0, 0, 0, 101 }, mate[5] = { 4, 0, 0, 0, 202 }, other[5] = { 4, 0, 0, 0, 303 }, none[5] = { 4, 0, 0, 0, 404 };
    const unsigned kept = 10u;
    if (piecerec::PieceTownWriteId(1, IdOfHand(t, other, 1), kept) != kept) Fail("t1047f1: an own piece's write names a non-teammate's outpost");
    if (piecerec::PieceTownMarkDue(1, 1, IdOfHand(t, other, 1), kept, 0u) != 0) Fail("t1047f1: a write is marked for a non-teammate's outpost");
    if (piecerec::PieceTownWriteId(1, IdOfHand(t, mate, 1), kept) != 20u || piecerec::PieceTownWriteId(1, IdOfHand(t, own, 1), 20u) != 10u)
        Fail("t1047f1: an own piece's write does not name its own team's outpost");
    if (IdOfHand(t, other, 0) != 30u) Fail("t1047f1: another player's copy has no id for a copy's own link");
    if (HandIsOtherCopy(t, other) != 1 || HandIsOtherCopy(t, mate) != 0 || HandIsOtherCopy(t, own) != 0 || HandIsOtherCopy(t, none) != 0)
        Fail("t1047f1: a non-teammate's copy is not told from the own team's towns");
    std::vector<IdTown> ex(t);
    ex[1].team = 0;   /* the teammate left the team */
    if (IdOfHand(ex, mate, 1) != 0 || HandIsOtherCopy(ex, mate) != 1) Fail("t1047f1: an ex-teammate's copy is still taken for an own piece");
    /* the placement's pick and the redirect: source checks (engine code, no offline road) - the redirect asks the team table */
    const std::string s = T313ReadFile("../coop-plugin/build.cpp");
    if (s.empty()) { Fail("t1047f1: build.cpp not read"); return; }
    const size_t ms = s.rfind("int BdTownMateSlot(void* tf, const int* hand, int* src)");   /* its definition (a declaration comes first) */
    const size_t mm = s.find("if (PolicyTeammateSlotIsMate(so) == 0) return -1;", ms);
    if (ms == std::string::npos || mm == std::string::npos || mm - ms > 300) Fail("t1047f1: the placement redirect takes a shown town without asking the team table");
    if (s.find("engineId = TlTeamIdOfTownWords(bh);") == std::string::npos) Fail("t1047f1: an own piece's write takes any town's id");
    if (s.find("if (TlOwnOutOfOther(k.h.key, b, k, towns) != 0) ++writes;") == std::string::npos) Fail("t1047f1: the look does not move an own piece out of a non-teammate's copy");
}
/* fix 3 / 6: a hidden copy discovered by the engine's rule inside the 5 s look's window is noted when it is taken out before a save,
   written beside the save, read at its load, and the copy founded again then is not hidden again; a teammate's outpost counts as seen,
   so after the team ends it stays shown; an older save's file is not taken */
void t_t1047f1_discovery_per_save()
{
    using namespace baseshow;
    Shown s;
    s.owner = 3; s.id = 30u; s.kind = kShowOther; s.hidden = 1;
    std::set<unsigned> disc;
    if (NoteBeforeTakeOut(s, 0, &disc) != 0 || !disc.empty()) Fail("t1047f1: a copy not discovered is noted");
    if (NoteBeforeTakeOut(s, -1, &disc) != 0) Fail("t1047f1: an unread mark is noted");
    if (NoteBeforeTakeOut(s, 1, &disc) != 1 || disc.count(30u) != 1) Fail("t1047f1: a discovery is not noted at the take-out");
    if (NoteBeforeTakeOut(s, 1, &disc) != 0) Fail("t1047f1: a discovery is noted twice");
    Shown m;
    m.owner = 2; m.id = 20u; m.kind = kShowMate;
    if (NoteBeforeTakeOut(m, -1, &disc) != 1 || disc.count(20u) != 1) Fail("t1047f1: a teammate's outpost is not noted as seen");
    Shown own;
    own.owner = 1; own.id = 10u; own.kind = kShowOwn;
    if (NoteBeforeTakeOut(own, 1, &disc) != 0) Fail("t1047f1: the player's own outpost is noted");
    /* the save's file, and its load */
    const std::string file = EncodeDiscovered(disc);
    std::set<unsigned> loaded;
    if (!DecodeDiscovered(file, &loaded) || loaded != disc) { Fail("t1047f1: the discovered outposts do not survive the file beside the save"); return; }
    /* the copy founded again after the load: not hidden (the founding marks it discovered: 1) */
    if (HideStep(kShowOther, 1, 1, loaded.count(30u) != 0 ? 1 : 0) == kHideHide) Fail("t1047f1: a copy discovered before the save is hidden again after it");
    if (HideStep(kShowOther, 1, 1, 0) != kHideHide) Fail("t1047f1: without the note the copy would not be hidden (the test's premise)");
    /* the ex-teammate (kind now kShowOther): shown, from the per-save set */
    if (HideStep(kShowOther, 1, 1, loaded.count(20u) != 0 ? 1 : 0) == kHideHide || HideStep(kShowOther, 1, 0, loaded.count(20u) != 0 ? 1 : 0) != kHideShow)
        Fail("t1047f1: an ex-teammate's outpost is hidden");
    if (NoteDiscovered(kShowOther, 31u, 1, 0) != 1 || NoteDiscovered(kShowOther, 31u, 1, 1) != 0 || NoteDiscovered(kShowOther, 0u, 1, 0) != 0) Fail("t1047f1: the look's note is wrong");
    std::set<unsigned> bad;
    if (DecodeDiscovered("discovered-outposts\t1\nD\t0\n", &bad) || DecodeDiscovered("discovered-outposts\t2\n", &bad) || DecodeDiscovered("", &bad) || !bad.empty())
        Fail("t1047f1: a bad discovered file reads");
    if (!DecodeDiscovered("discovered-outposts\t1\n", &bad) || !bad.empty()) Fail("t1047f1: an empty discovered file does not read");
    if (DiscoveredFileCurrent(100ULL, 200ULL) != 0 || DiscoveredFileCurrent(0ULL, 200ULL) != 0 || DiscoveredFileCurrent(300ULL, 200ULL) != 1 || DiscoveredFileCurrent(300ULL, 0ULL) != 0)
        Fail("t1047f1: an older save's file is taken, or the save's own refused");
    /* the take-out paths read the mark first (engine code: source checks) */
    const std::string src = T313ReadFile("../coop-plugin/build.cpp");
    const size_t to = src.find("int BdShowTakeOut(baseshow::Shown* s, const char* why)");
    const size_t note = src.find("TlNoteTakeOut(*s, t);", to), destroy = src.find("BdTownDestroyPod(t)", to);
    if (to == std::string::npos || note == std::string::npos || destroy == std::string::npos || note > destroy) Fail("t1047f1: a copy is taken out before its discovered mark is read");
    const size_t sg = src.find("void BdShowSaveGuard()"), sgNote = src.find("TlNoteTakeOut(g_shown[i], nt);", sg), sgOut = src.find("BdShowTakeOut(&g_shown[i]", sg);
    if (sg == std::string::npos || sgNote == std::string::npos || sgOut == std::string::npos || sgNote > sgOut) Fail("t1047f1: the save's take-out does not note the discoveries first");
}
/* fix 4: the relink's power cycle is an event - switched on again: nothing to reset, and a player's own flip noted before the relink
   still asks; not on again: the record's switch written once (or left off with no record values); no time window in the power look */
void t_t1047f1_relink_power_event()
{
    using namespace piecerec;
    if (PieceTownCycleStep(kCycleBack, 1) != kCycleStepNothing || PieceTownCycleStep(kCycleNone, 1) != kCycleStepNothing) Fail("t1047f1: a switch back on is acted on");
    if (PieceTownCycleStep(kCycleFailed, 1) != kCycleStepRecordSwitch) Fail("t1047f1: a failed switch-on does not write the record's switch");
    if (PieceTownCycleStep(kCycleFailed, 0) != kCycleStepLeftOff) Fail("t1047f1: a failed switch-on with no record values is not counted");
    /* the power look after a cycle back on: the switch reads as before - the writer's switch held here (on) against the live switch */
    if (powerwire::PowerAskDue(1, 1, 1) != 0) Fail("t1047f1: a switch back on asks");
    if (powerwire::PowerAskDue(1, 0, 1) != 1) Fail("t1047f1: a player's flip before the relink lost its ask");
    const std::string s = T313ReadFile("../coop-plugin/build.cpp");
    if (s.empty()) { Fail("t1047f1: build.cpp not read"); return; }
    const size_t look = s.find("void PwLook(void* b, PwRow& r)"), lookEnd = s.find("void PwChargeGated(void* b)", look);
    if (look == std::string::npos || lookEnd == std::string::npos) { Fail("t1047f1: PwLook not found"); return; }
    if (s.substr(look, lookEnd - look).find("Tl") != std::string::npos) Fail("t1047f1: the power look still has a relink window");
    if (s.find("int BdBuildingSetTownPod(void* b, void* t, int* cycle = 0)") == std::string::npos) Fail("t1047f1: the relink does not report its power cycle");
}
/* fix 5: a link write refused or lost (no write owed, none sent and unanswered, the stored record still not naming the engine's
   link) is marked again at the next look; while it is owed or in flight it is not marked twice */
void t_t1047f1_link_mark_again()
{
    using namespace piecerec;
    const unsigned engine = 5u, kept = 3u;
    if (PieceTownMarkDue(1, 1, engine, kept, PieceTownMarkStands(engine, 1, 0)) != 0 || PieceTownMarkDue(1, 1, engine, kept, PieceTownMarkStands(engine, 0, 1)) != 0)
        Fail("t1047f1: a link write owed or in flight is marked twice");
    if (PieceTownMarkDue(1, 1, engine, kept, PieceTownMarkStands(engine, 0, 0)) != 1) Fail("t1047f1: a refused or lost link write is never marked again");
    if (PieceTownMarkDue(1, 1, engine, engine, PieceTownMarkStands(engine, 0, 0)) != 0) Fail("t1047f1: a taken link write is marked again");
    const std::string s = T313ReadFile("../coop-plugin/build.cpp");
    if (s.find("markedId = piecerec::PieceTownMarkStands(m->second, w->marked, w->fly.empty() ? 0 : 1);") == std::string::npos) Fail("t1047f1: the owner's mark is not dropped once its write is settled");
    if (s.find("TlForgetWorld();") == std::string::npos) Fail("t1047f1: the link marks outlive the world");
}
/* ---- T-1047 step 5 fold 2 ---- */
/* fix 1: while the team list is not held nothing checks (so nothing moves) an own piece - the wait counted once - and once the list
   arrives the owed check runs once; a placement while it is not held is not moved then, and is checked once it is */
void t_t1047f2_own_waits_for_team()
{
    using namespace baseshow;
    OwnCheck g(16);
    unsigned char applied[16];
    std::memset(applied, 0, sizeof applied);
    applied[3] = 1;
    int moves = 0;
    OwnCheckAreaApplied(&g, 3);   /* the load: area 3's records applied while the list is not held (a reconnect) */
    for (int tick = 0; tick < 5; ++tick)
        if (OwnCheckAreasDue(&g, 0) != 0) { ++moves; OwnCheckAreaDone(&g, 3); }
    if (moves != 0) Fail("t1047f2: an own piece is checked (and can be moved) while the team list is not held");
    if (g.waits != 1) Fail("t1047f2: the wait for the team list is not counted once");
    /* the team list arrives: held, teammate slot 2 */
    std::vector<int> mates(1, 2);
    OwnCheckTeamSeen(&g, 1, OwnCheckTeamSig(1, mates), applied, 16);
    for (int tick = 0; tick < 5; ++tick)
        if (OwnCheckAreasDue(&g, 1) != 0) { ++moves; OwnCheckAreaDone(&g, 3); }
    if (moves != 1) Fail("t1047f2: the owed check does not run exactly once when the team list is held");
    /* the placement during a drop: not checked then, checked once the list is held again */
    OwnCheckTeamSeen(&g, 2, OwnCheckTeamSig(0, std::vector<int>()), applied, 16);   /* the drop clears the list */
    if (OwnCheckAreasDue(&g, 0) != 0) Fail("t1047f2: a check runs during the drop");
    applied[5] = 1;   /* the placement's area: its records applied here (fold 3: the real call passes no area - every applied one owed) */
    if (OwnCheckPlacement(&g, 0, applied, 16) != 0) Fail("t1047f2: a placement is moved while the team list is not held");
    if (g.pieceWaits != 1) Fail("t1047f2: the placement's skip is not counted");
    if (OwnCheckAreasDue(&g, 0) != 0) Fail("t1047f2: the placement's area is checked during the drop");
    OwnCheckTeamSeen(&g, 3, OwnCheckTeamSig(1, mates), applied, 16);   /* the list arrives again, same teammate */
    if (OwnCheckAreasDue(&g, 1) != 1 || g.owed[5] == 0 || g.owed[3] == 0) Fail("t1047f2: the placement's area or the applied areas are not checked once the list is held");
    if (OwnCheckPlacement(&g, 1, applied, 16) != 1) Fail("t1047f2: a placement with the list held is not checked at once");
    /* a reconnect where the list goes stale with no clear (no table change number moved): the placement still owes its check */
    OwnCheck h(16);
    OwnCheckTeamSeen(&h, 7, OwnCheckTeamSig(1, mates), applied, 16);
    for (int c = 0; c < 16; ++c) OwnCheckAreaDone(&h, c);
    unsigned char ap4[16];
    std::memset(ap4, 0, sizeof ap4);
    ap4[4] = 1;   /* fold 3: the placement's area is owed by the applied-area loop, as the real call (area -1) */
    if (OwnCheckPlacement(&h, 0, ap4, 16) != 0 || OwnCheckAreasDue(&h, 1) != 1 || h.owed[4] == 0) Fail("t1047f2: a placement skipped with no team change is never checked");
}
/* fix 2: the own-piece check runs on its three events only - an area's records applied (a load), the team list changed (a teammate
   added or removed), a placement - and never as a sweep: ticks with no event check nothing */
void t_t1047f2_own_check_events_only()
{
    using namespace baseshow;
    OwnCheck g(8);
    unsigned char applied[8];
    std::memset(applied, 0, sizeof applied);
    applied[1] = 1; applied[2] = 1;
    std::vector<int> mates(1, 2);
    OwnCheckTeamSeen(&g, 1, OwnCheckTeamSig(1, mates), applied, 8);   /* the first list */
    for (int c = 0; c < 8; ++c) OwnCheckAreaDone(&g, c);
    long long runs0 = g.runs;
    for (int tick = 0; tick < 1000; ++tick) if (OwnCheckAreasDue(&g, 1) != 0) Fail("t1047f2: the own-piece check runs with no event (a sweep)");
    if (g.runs != runs0) Fail("t1047f2: a run is counted with no event");
    OwnCheckAreaApplied(&g, 1);   /* event 1: the load (area 1's records applied) */
    if (OwnCheckAreasDue(&g, 1) != 1 || g.owed[1] == 0 || g.owed[2] != 0) Fail("t1047f2: an area's apply does not owe exactly its own pieces a check");
    OwnCheckAreaDone(&g, 1);
    if (OwnCheckAreasDue(&g, 1) != 0) Fail("t1047f2: the load's check runs twice");
    if (OwnCheckTeamSeen(&g, 2, OwnCheckTeamSig(1, mates), applied, 8) != 0) Fail("t1047f2: a team list with the same teammates is taken for a change");
    if (OwnCheckTeamSeen(&g, 2, OwnCheckTeamSig(1, std::vector<int>()), applied, 8) != 0) Fail("t1047f2: the same change number is read twice");
    std::vector<int> more(mates);
    more.push_back(4);
    if (OwnCheckTeamSeen(&g, 3, OwnCheckTeamSig(1, more), applied, 8) != 1) Fail("t1047f2: a teammate added is no event");   /* event 2 */
    if (OwnCheckAreasDue(&g, 1) != 1 || g.owed[1] == 0 || g.owed[2] == 0 || g.owed[0] != 0) Fail("t1047f2: a team change does not owe every applied area");
    for (int c = 0; c < 8; ++c) OwnCheckAreaDone(&g, c);
    std::vector<int> fewer(1, 4);
    if (OwnCheckTeamSeen(&g, 4, OwnCheckTeamSig(1, fewer), applied, 8) != 1) Fail("t1047f2: a teammate removed is no event");
    for (int c = 0; c < 8; ++c) OwnCheckAreaDone(&g, c);
    if (OwnCheckPlacement(&g, 1, applied, 8) != 1) Fail("t1047f2: a placement is not checked");   /* event 3 */
    if (OwnCheckAreasDue(&g, 1) != 0) Fail("t1047f2: a placement checked at once owes an area sweep too");
    if (OwnCheckTeamSig(1, more) != OwnCheckTeamSig(1, std::vector<int>(more.rbegin(), more.rend()))) Fail("t1047f2: the teammates' order changes the team's signature");
    OwnCheckAreaApplied(&g, 6);
    OwnCheckForgetWorld(&g);
    if (OwnCheckAreasDue(&g, 1) != 0) Fail("t1047f2: a check owed in a world gone runs in the next");
    /* the engine glue: the 5 s look leaves the own pieces, the check is driven from the events (source checks - engine code) */
    const std::string s = T313ReadFile("../coop-plugin/build.cpp");
    if (s.empty()) { Fail("t1047f2: build.cpp not read"); return; }
    if (s.find("if (k.h.ownerSlot == mySlot || link == 0) continue;") == std::string::npos) Fail("t1047f2: the 5 s look still looks at the own pieces");
    if (s.find("PcaApplyArea(c, me);\r\n        TlOwnAreaApplied(c);") == std::string::npos && s.find("PcaApplyArea(c, me);\n        TlOwnAreaApplied(c);") == std::string::npos)
        Fail("t1047f2: an area's apply does not owe its own pieces a check");
    if (s.find("if (baseshow::OwnCheckPlacement(&g_tlOwn, TeamTableKnown() ? 1 : 0, g_pcaDone, ") == std::string::npos) Fail("t1047f2: the placement's move does not wait for the team list");
}
/* fix 3: a failed link copy still reports its power cycle (a piece left off is written back or counted) and is not tried again -
   no repeated power cycle - until its record or a town changes */
struct T1047f2Ops : baseshow::RelinkOps
{
    int result, cycleOut, copies, afters, lastCycle;
    T1047f2Ops() : result(0), cycleOut(piecerec::kCycleFailed), copies(0), afters(0), lastCycle(-1) {}
    int Copy(int* cycle) { ++copies; *cycle = cycleOut; return result; }
    void CycleAfter(int cycle) { ++afters; lastCycle = cycle; }
};
void t_t1047f2_failed_copy_once()
{
    using namespace baseshow;
    std::map<std::string, LinkFail> fails;
    long long skipped = 0;
    const int from[5] = { 4, 0, 0, 0, 7 }, to[5] = { 4, 0, 0, 0, 8 }, to2[5] = { 4, 0, 0, 0, 9 };
    T1047f2Ops ops;
    if (RelinkPiece(&ops, &fails, 16, "k1", 5u, from, to, &skipped) != kRelinkFailed) Fail("t1047f2: a failed copy is not reported failed");
    if (ops.afters != 1 || ops.lastCycle != piecerec::kCycleFailed) Fail("t1047f2: a failed copy does not reach the power cycle's report");
    if (piecerec::PieceTownCycleStep(ops.lastCycle, 1) != piecerec::kCycleStepRecordSwitch) Fail("t1047f2: the piece left off is not written back");
    for (int look = 0; look < 10; ++look) RelinkPiece(&ops, &fails, 16, "k1", 5u, from, to, &skipped);
    if (ops.copies != 1 || ops.afters != 1 || skipped != 10) Fail("t1047f2: a failing copy is tried (and power cycled) again with nothing changed");
    RelinkPiece(&ops, &fails, 16, "k1", 6u, from, to, &skipped);   /* the record changed */
    if (ops.copies != 2) Fail("t1047f2: a failed copy is not tried again once its record changed");
    RelinkPiece(&ops, &fails, 16, "k1", 6u, from, to2, &skipped);   /* the town it goes to changed */
    if (ops.copies != 3) Fail("t1047f2: a failed copy is not tried again once its town changed");
    RelinkPiece(&ops, &fails, 16, "k1", 6u, to, to2, &skipped);     /* the town it stands in changed */
    if (ops.copies != 4) Fail("t1047f2: a failed copy is not tried again once the town it stands in changed");
    ops.result = 1; ops.cycleOut = piecerec::kCycleBack;
    if (RelinkPiece(&ops, &fails, 16, "k1", 7u, to, to2, &skipped) != kRelinkDone || fails.count("k1") != 0 || ops.afters != 5) Fail("t1047f2: a copy that worked is still remembered failed");
    ops.result = 0;
    RelinkPiece(&ops, &fails, 1, "k2", 1u, from, to, &skipped);
    RelinkPiece(&ops, &fails, 1, "k3", 1u, from, to, &skipped);
    if (fails.size() != 1) Fail("t1047f2: the failed-copy memory passes its cap");
    /* the engine glue: both relinks go through it (source checks - engine code) */
    const std::string s = T313ReadFile("../coop-plugin/build.cpp");
    if (s.empty()) { Fail("t1047f2: build.cpp not read"); return; }
    if (s.find("if (TlRelink(key, b, t, k, bh, words, liveFollows) != baseshow::kRelinkDone) return 0;") == std::string::npos
        || s.find("if (TlRelink(key, b, t, k, bh, words, 0) != baseshow::kRelinkDone) return 0;") == std::string::npos)
        Fail("t1047f2: a relink does not go through the failed-copy rule");
}
/* fix 4: a load whose prepare read its save's discovered set and then failed (no new world) is disarmed - its set never applies to
   a later world; a load that made a world binds the set to it; two waiting saves each get their own take-out's set */
void t_t1047f2_failed_load_disarmed()
{
    using namespace baseshow;
    DiscArm a;
    DiscArmAtPrepare(&a);
    if (DiscArmTake(&a, 5) != 0 || a.armed == 0) Fail("t1047f2: a set is taken while its load call is still running");
    if (DiscArmAtLoadEnd(&a, 5, 5) != 1 || a.armed != 0) Fail("t1047f2: a load that made no world stays armed");
    if (DiscArmTake(&a, 6) != 0) Fail("t1047f2: a failed load's set applies to the next world");
    DiscArmAtPrepare(&a);
    if (DiscArmAtLoadEnd(&a, 5, 6) != 0 || a.gen != 6) Fail("t1047f2: a load that made a world is not bound to it");
    if (DiscArmTake(&a, 6) != 1 || DiscArmTake(&a, 6) != 0) Fail("t1047f2: the world a load made does not take its set once");
    DiscArmAtPrepare(&a);
    DiscArmAtLoadEnd(&a, 6, 7);
    if (DiscArmTake(&a, 8) != 0 || a.armed != 0) Fail("t1047f2: a set bound to one world applies to another");
    /* two waiting saves, both asked before either take-out */
    std::map<long long, std::set<unsigned> > hist;
    std::set<unsigned> s6, s7, got;
    s6.insert(30u);
    s7 = s6; s7.insert(31u);
    DiscGuardNote(&hist, 6, s6, 8);
    DiscGuardNote(&hist, 7, s7, 8);
    if (DiscGuardClaim(&hist, 5, &got) != 1 || got != s6) Fail("t1047f2: the first save does not get its own take-out's set");
    if (DiscGuardClaim(&hist, 5, &got) != 1 || got != s7) Fail("t1047f2: two waiting saves share one set");
    if (DiscGuardClaim(&hist, 5, &got) != 0) Fail("t1047f2: a third save finds a set no take-out made for it");
    for (long long n = 1; n <= 20; ++n) DiscGuardNote(&hist, n, s6, 8);
    if (hist.size() != 8) Fail("t1047f2: the take-out sets pass their cap");
}
/* ---- T-1047 step 5 fold 3 ---- */
/* fix 1: a TOWN EVENT - a town founded, taken up or moved owes the own pieces of its area and the 8 around it a check (the engine's
   own-outpost founding 0x4D6300 hands this player's wilderness buildings to their nearest town once 0x933F40 has founded it); a copy
   kept where it stood is no event; the team-held gate holds as for every event */
void t_t1047f3_town_event()
{
    using namespace baseshow;
    OwnCheck g(kOwnTownGrid * kOwnTownGrid);
    if (OwnCheckTownAt(&g, 10, 20) != 9) Fail("t1047f3: a town event does not owe its area and the 8 around it");
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (g.owed[(size_t)((10 + dx) * kOwnTownGrid + 20 + dy)] == 0) Fail("t1047f3: an area around the town is not owed");
    if (g.owed[(size_t)(12 * kOwnTownGrid + 20)] != 0) Fail("t1047f3: an area two away from the town is owed");
    if (OwnCheckAreasDue(&g, 0) != 0) Fail("t1047f3: a town event is checked while the team list is not held");
    if (OwnCheckAreasDue(&g, 1) != 1) Fail("t1047f3: a town event is not checked once the team list is held");
    OwnCheck b(kOwnTownGrid * kOwnTownGrid);
    if (OwnCheckTownAt(&b, 0, 0) != 4 || OwnCheckTownAt(&b, -1, 3) != 0 || OwnCheckTownAt(&b, 3, kOwnTownGrid) != 0) Fail("t1047f3: the grid's edge is not kept");
    if (CopyTownChanged(kRowKeep, 0) != 0) Fail("t1047f3: a copy kept where it stood owes a check (a check at every apply)");
    if (CopyTownChanged(kRowFound, 0) != 1 || CopyTownChanged(kRowTake, 0) != 1 || CopyTownChanged(kRowKeep, 1) != 1) Fail("t1047f3: a copy founded, taken up or moved is no event");
    /* the engine glue: both roads raise it (source checks - engine code) */
    const std::string s = T313ReadFile("../coop-plugin/build.cpp");
    if (s.empty()) { Fail("t1047f3: build.cpp not read"); return; }
    if (s.find("if (t != 0 && posOk != 0) TlOwnTownAt(p, ") == std::string::npos) Fail("t1047f3: a founding the hook saw is no town event");
    if (s.find("if (baseshow::CopyTownChanged(step, (moveKind == townmove::kMove || moveKind == townmove::kRefound) ? 1 : 0) != 0)") == std::string::npos)
        Fail("t1047f3: a copy town made or moved is no town event");
    if (s.find("if (baseshow::OwnCheckTownAt(&g_tlOwn, sx, sy) == 0) return;") == std::string::npos) Fail("t1047f3: the town event does not owe the areas around it");
}
/* fix 2: the team-held gate of each engine step (move, mark, the record write's link read) and the town-link tick: the own-piece
   check runs at every ready tick, so an event is checked at once - never only at the 5 s look, never while not ready */
void t_t1047f3_gate_and_tick()
{
    using namespace baseshow;
    if (OwnStepDue(0, 1) != 0) Fail("t1047f3: an own piece's step goes on while the team list is not held");
    if (OwnStepDue(1, 1) != 1) Fail("t1047f3: an own piece's step is held back with the team list held");
    if (OwnStepDue(1, 0) != 0 || OwnStepDue(1, -1) != 0 || OwnStepDue(0, 0) != 0) Fail("t1047f3: a piece inside a building (or unread) goes on");
    OwnCheck g(8);
    unsigned char applied[8];
    std::memset(applied, 0, sizeof applied);
    applied[2] = 1;
    std::vector<int> mates(1, 3);
    OwnCheckTeamSeen(&g, 1, OwnCheckTeamSig(1, mates), applied, 8);
    for (int c = 0; c < 8; ++c) OwnCheckAreaDone(&g, c);
    OwnCheckAreaApplied(&g, 2);   /* an event just after a look */
    int ownRuns = 0, looks = 0;
    unsigned since = 16;
    for (int tick = 0; tick < 20; ++tick, since += 16)
    {
        const int plan = TownLinkTickPlan(1, 0, since, 5000u);
        if ((plan & kTickOwnCheck) != 0 && OwnCheckAreasDue(&g, 1) != 0) { ++ownRuns; OwnCheckAreaDone(&g, 2); }
        if ((plan & kTickLook) != 0) ++looks;
    }
    if (ownRuns != 1) Fail("t1047f3: an event's check waits for the 5 s look (or runs twice)");
    if (looks != 0) Fail("t1047f3: the look runs before its 5 s");
    if (TownLinkTickPlan(0, 0, 9999u, 5000u) != 0) Fail("t1047f3: a tick that is not ready (writes blocked, off the main thread, a save) checks or looks");
    if (TownLinkTickPlan(1, -1, 10u, 5000u) != 0 || TownLinkTickPlan(1, -1, 5000u, 5000u) != kTickLook) Fail("t1047f3: with no slot the own check runs, or the look stops");
    if (TownLinkTickPlan(1, 2, 5000u, 5000u) != (kTickOwnCheck | kTickLook)) Fail("t1047f3: a look's tick skips the own check");
    /* the engine glue (source checks - engine code) */
    const std::string s = T313ReadFile("../coop-plugin/build.cpp");
    if (s.empty()) { Fail("t1047f3: build.cpp not read"); return; }
    if (s.find("if (baseshow::OwnStepDue(TlOwnTeamHeld(), BdBuildingTopLevelPod(b)) == 0) return;") == std::string::npos) Fail("t1047f3: the mark does not wait for the team list");
    if (s.find("if (baseshow::OwnStepDue(TlOwnTeamHeld(), BdBuildingTopLevelPod(b)) == 0) return 0;") == std::string::npos) Fail("t1047f3: the move does not wait for the team list");
    if (s.find("baseshow::OwnStepDue(TlOwnTeamHeld(), top) != 0 && BdBuildingTownHandPod(pc, bh) != 0") == std::string::npos) Fail("t1047f3: the record write reads the engine link without the team list");
    const size_t own = s.find("if ((plan & baseshow::kTickOwnCheck) != 0) TlOwnCheckTick(KeptMySlot());");
    const size_t look = s.find("if ((plan & baseshow::kTickLook) == 0) return;");
    if (s.find("const int plan = baseshow::TownLinkTickPlan(ready, KeptMySlot(), ") == std::string::npos || own == std::string::npos || look == std::string::npos || own > look)
        Fail("t1047f3: the town-link tick does not reach the own-piece check before the look's clock");
}
/* fix 2: the load-end wiring (the generation read before the prepare, the end reached with it) and each save's own discovered set */
struct T1047f3Load : baseshow::DiscLoadOps
{
    baseshow::DiscArm* arm; long gen; int makesWorld; std::string order;
    T1047f3Load(baseshow::DiscArm* a, long g, int m) : arm(a), gen(g), makesWorld(m) {}
    long Gen() { order += "G"; return gen; }
    void Prepare() { order += "P"; baseshow::DiscArmAtPrepare(arm); }
    int Load() { order += "L"; if (makesWorld != 0) ++gen; return makesWorld; }
    void Ended(long genAtPrepare) { order += "E"; baseshow::DiscArmAtLoadEnd(arm, genAtPrepare, gen); }
};
void t_t1047f3_load_and_save_wiring()
{
    using namespace baseshow;
    DiscArm a;
    T1047f3Load bad(&a, 5, 0);
    if (DiscLoadRun(&bad) != 0 || bad.order != "GPLE") Fail("t1047f3: the load's generation is not read before its prepare, or its end is not reached");
    if (a.armed != 0 || DiscArmTake(&a, 6) != 0) Fail("t1047f3: a failed load's discovered set stays armed for a later world");
    T1047f3Load good(&a, 6, 1);
    DiscLoadRun(&good);
    if (a.gen != 7 || DiscArmTake(&a, 7) != 1) Fail("t1047f3: a load that made a world does not bind its set to it");
    DiscGuard dg;
    std::set<unsigned> s6, s7, s8;
    s6.insert(30u);
    s7 = s6; s7.insert(31u);
    long long na = -1, nb = -1, nc = -1;
    std::string ta = DiscGuardAsk(dg, s6, &na), tb = DiscGuardAsk(dg, s6, &nb);   /* two saves asked before either take-out */
    DiscGuardTakeOut(&dg, s6, 8);
    DiscGuardTakeOut(&dg, s7, 8);   /* an outpost discovered between the two take-outs */
    if (DiscGuardText(&dg, na, &ta) != 1 || ta != EncodeDiscovered(s6)) Fail("t1047f3: the first save does not get its own take-out's set");
    if (DiscGuardText(&dg, nb, &tb) != 1 || tb != EncodeDiscovered(s7)) Fail("t1047f3: the second save does not get its own take-out's set");
    s8 = s7; s8.insert(32u);
    std::string tc = DiscGuardAsk(dg, s8, &nc);
    if (DiscGuardText(&dg, nc, &tc) != 0 || tc != EncodeDiscovered(s8)) Fail("t1047f3: a save with no take-out since its request loses its request's set");
    /* the engine glue (source checks - engine code) */
    const std::string b = T313ReadFile("../coop-plugin/build.cpp"), st = T313ReadFile("../coop-plugin/store.cpp");
    if (b.empty() || st.empty()) { Fail("t1047f3: build.cpp or store.cpp not read"); return; }
    if (b.find("return baseshow::DiscGuardText(&g_tlGuard, guardN0, text);") == std::string::npos
        || b.find("baseshow::DiscGuardTakeOut(&g_tlGuard, g_tlDiscovered, kTlGuardHistCap);") == std::string::npos
        || b.find("return baseshow::DiscGuardAsk(g_tlGuard, g_tlDiscovered, guardN);") == std::string::npos)
        Fail("t1047f3: the save's discovered set does not go through baseshow::DiscGuard");
    if (st.find("return baseshow::DiscLoadRun(&ops);") == std::string::npos || st.find("void Ended(long genAtPrepare) { coop::BuildDiscoveredLoadEnded(genAtPrepare); coop::ItemsBoxOwedLoadEnded(genAtPrepare); }") == std::string::npos)
        Fail("t1047f3: the load does not end through baseshow::DiscLoadRun");
    if (st.find("coop::BuildDiscoveredGuard(g_discGuardN0[i], &dt);") == std::string::npos) Fail("t1047f3: a landed save does not ask for its own set");
}
/* ---- T-940: every player outpost's fixed zone (townkeep.h THE FIXED ANCHOR, baseshow.h SHOW's anchor, buildnear.h ZoneVerdict) ---- */
/* the anchor is the first said centre, kept through the centre's drift (a SAY at a drifted centre still inherits the row by its key),
   through a carried (reset) outpost's id, and through a file round trip; an older file's row takes its centre at the read */
void t_t940_anchor_fixed()
{
    using namespace townkeep;
    std::vector<TownRow> all;
    std::vector<TownRow> said;
    said.push_back(T1047fRow(-1, 1000.0f, 2000.0f, 0u, "12-a.mod@3,41@1000,2000,0"));
    unsigned next = 1;
    ReplaceSlotTowns(&all, 1, said, 1, &next, 0);
    float ax = 0.0f, az = 0.0f;
    if (all.size() != 1 || all[0].anchored != 1) { Fail("t940: a new outpost row has no anchor"); return; }
    AnchorOf(all[0], &ax, &az);
    if (ax != 1000.0f || az != 2000.0f) Fail("t940: a new outpost's anchor is not its founding spot");
    /* the engine moves the centre toward the buildings: 40 u (a centre match), then 900 u more (only the key matches) */
    said[0].pos[0] = 1040.0f;
    ReplaceSlotTowns(&all, 1, said, 1, &next, 0);
    said[0].pos[0] = 1940.0f; said[0].pos[2] = 2300.0f;
    ReplaceSlotTowns(&all, 1, said, 1, &next, 0);
    AnchorOf(all[0], &ax, &az);
    if (all.size() != 1 || all[0].pos[0] != 1940.0f || ax != 1000.0f || az != 2000.0f || all[0].id != 1u) Fail("t940: the anchor moved with the centre's drift");
    /* a file round trip keeps it */
    const std::string text = EncodeTownsFile(kPlayerTownsHeader, all, true, next);
    std::vector<TownRow> back;
    std::string why;
    if (!DecodeTownsFile(text, kPlayerTownsHeader, true, &back, &why) || back.size() != 1) { Fail("t940: the file does not read back: " + why); return; }
    AnchorOf(back[0], &ax, &az);
    if (back[0].anchored != 1 || ax != 1000.0f || az != 2000.0f || back[0].pos[0] != 1940.0f) Fail("t940: the anchor does not survive the file");
    /* an older file (format 4: no anchor fields) - the centre at the read is the anchor, and it stays from then on */
    std::string old = "player-towns\t4\n";
    size_t at = text.find('\n') + 1;
    while (at < text.size())
    {
        size_t e = text.find('\n', at);
        std::string line = text.substr(at, e - at);
        if (line.size() > 2 && line[0] == 'T') for (int k = 0; k < 2; ++k) line = line.substr(0, line.rfind('\t'));
        old += line + "\n";
        at = e + 1;
    }
    if (!DecodeTownsFile(old, kPlayerTownsHeader, true, &back, &why) || back.size() != 1) { Fail("t940: a format-4 file does not read: " + why); return; }
    AnchorOf(back[0], &ax, &az);
    if (back[0].anchored != 1 || ax != 1940.0f || az != 2300.0f) Fail("t940: a format-4 row's anchor is not its centre at the read");
    said[0].pos[0] = 2500.0f;
    ReplaceSlotTowns(&back, 1, said, 1, &next, 0);
    AnchorOf(back[0], &ax, &az);
    if (ax != 1940.0f || az != 2300.0f) Fail("t940: an older file's anchor moved at the next SAY");
    /* a reset carried the outpost (kept-towns.txt): a new SAY far from it takes its id by key - and its anchor */
    std::vector<TownRow> carried(1, all[0]);
    carried[0].owner = 1;
    std::vector<TownRow> fresh;
    std::vector<TownRow> s2(1, T1047fRow(-1, 9000.0f, 9000.0f, 0u, "12-a.mod@3,41@1000,2000,0"));
    unsigned n2 = 5;
    ReplaceSlotTowns(&fresh, 1, s2, 1, &n2, &carried);
    AnchorOf(fresh[0], &ax, &az);
    if (fresh.size() != 1 || fresh[0].id != 1u || ax != 1000.0f || az != 2000.0f) Fail("t940: a carried outpost's anchor is not kept with its id");
}
/* the zone rule: a stranger's anchor refuses within 7000 u (allowed at 7001); own (or a teammate's) zone always buildable; founding
   refused under 12000 u; teammates never refuse; no anchors = the game's own rule alone (NPC towns untouched) */
void t_t940_zone_verdict()
{
    using namespace buildnear;
    if (kZoneRadius != 5000.0f || kZoneGap != 2000.0f || kZoneRefuse != 7000.0f || kAnchorApart != 12000.0f) Fail("t940: the zone sizes are not 500 m + 200 m, 1.2 km apart");
    ZoneAnchor a[3];
    a[0].owner = 4; a[0].id = 21u; a[0].x = 0.0f; a[0].z = 0.0f; a[0].rel = kRelOther;      /* a stranger's outpost */
    int w = -1;
    float d = -1.0f;
    if (ZoneVerdict(a, 1, 6999.0f, 0.0f, 0, &w, &d) != kZoneRefusedGap || w != 0 || d < 6998.0f || d > 7000.0f) Fail("t940: a spot 6999 u from a stranger's anchor is not refused");
    if (ZoneVerdict(a, 1, 7000.0f, 0.0f, 0, &w, &d) != kZoneRefusedGap) Fail("t940: a spot at 7000 u is not refused");
    if (ZoneVerdict(a, 1, 7001.0f, 0.0f, 0, &w, &d) != kZoneFree || w != -1) Fail("t940: a spot at 7001 u is refused");
    if (ZoneAllowed(kZoneRefusedGap) != 0 || ZoneAllowed(kZoneRefusedFound) != 0 || ZoneAllowed(kZoneOwnOverGap) != 1 || ZoneAllowed(kZoneFree) != 1) Fail("t940: ZoneAllowed");
    /* founding: refused under 12000 u, allowed at 12000; a joining piece at 8000 is allowed */
    if (ZoneVerdict(a, 1, 0.0f, 11999.0f, 1, &w, &d) != kZoneRefusedFound || w != 0) Fail("t940: founding 11999 u from a stranger's anchor is not refused");
    if (ZoneVerdict(a, 1, 0.0f, 12000.0f, 1, &w, &d) != kZoneFree) Fail("t940: founding 12000 u away is refused");
    if (ZoneVerdict(a, 1, 0.0f, 8000.0f, 0, &w, &d) != kZoneFree || ZoneVerdict(a, 1, 0.0f, 8000.0f, -1, &w, &d) != kZoneFree) Fail("t940: a piece that founds nothing (or unknown) is refused past the gap");
    /* an old world: own anchor 9000 u from the stranger's - own zone wins inside it, the stranger's gap outside it */
    a[1].owner = 1; a[1].id = 3u; a[1].x = 9000.0f; a[1].z = 0.0f; a[1].rel = kRelOwn;
    if (ZoneVerdict(a, 2, 5000.0f, 0.0f, 0, &w, &d) != kZoneOwnOverGap || w != 0) Fail("t940: inside own zone within a stranger's gap is not allowed");
    if (ZoneVerdict(a, 2, 9000.0f, 4000.0f, 1, &w, &d) != kZoneOwnInside || w != 1) Fail("t940: inside own zone is not allowed");
    if (ZoneVerdict(a, 2, 3000.0f, 3000.0f, 0, &w, &d) != kZoneRefusedGap) Fail("t940: the stranger's zone outside own zone is not refused");
    if (OwnZoneAt(a, 2, 9000.0f, 4999.0f) != 1 || OwnZoneAt(a, 2, 9000.0f, 5001.0f) != -1 || OwnZoneAt(a, 2, 0.0f, 0.0f) != -1) Fail("t940: OwnZoneAt");
    /* a teammate never refuses, and its zone is buildable even inside a stranger's gap */
    a[2].owner = 2; a[2].id = 8u; a[2].x = 0.0f; a[2].z = -9000.0f; a[2].rel = kRelMate;
    if (ZoneVerdict(a + 2, 1, 0.0f, -9000.0f, 1, &w, &d) != kZoneMateInside || ZoneVerdict(a + 2, 1, 0.0f, -3000.0f, 1, &w, &d) != kZoneFree)
        Fail("t940: a teammate's outpost refuses or its zone is not its zone");
    if (ZoneVerdict(a, 3, 0.0f, -5000.0f, 0, &w, &d) != kZoneOwnOverGap) Fail("t940: a teammate's zone within a stranger's gap is not allowed");
    /* decision 973's rings replaced: inside the teammate's fixed zone only */
    if (MateZoneFixed(5000.0f) != kZoneInside || MateZoneFixed(5001.0f) != kZoneOut || MateZoneFixed(-1.0f) != -1) Fail("t940: the teammate's fixed zone");
    if (TownPickTakesMate(1, 2, MateZoneFixed(4000.0f), 0, 1) != 1 || TownPickTakesMate(1, 2, MateZoneFixed(6000.0f), 0, 1) != 0)
        Fail("t940: a piece inside a teammate's fixed zone does not join it, or one outside it does");
    /* no anchors: the game's own rule alone (an NPC town's refusal unchanged) */
    if (ZoneVerdict(a, 0, 0.0f, 0.0f, 1, &w, &d) != kZoneFree || Refused(1, 1, 0, 1, 1) != 1) Fail("t940: with no outposts the game's own rule is not the answer");
}

/* T-940: the build cursor's zone stub at 0x4DB2F6 - golden bytes typed from a hand listing (not from ZoneStubBuild), site
   0x1404DB2F6, callee 0x1122334455667788:
      0  push rax rcx rdx r8 r9 r10 r11    11  sub rsp, 0x88    18  movdqu [rsp+0x20+16i], xmm i (i 0..5)
     54  mov rcx, rsi    57  mov rax, callee    67  call rax    69  movdqu xmm i, [rsp+0x20+16i]    105  add rsp, 0x88
    112  pop r11 r10 r9 r8 rdx rcx rax    123  cmp dword [rsi+0x38], 0    127  jne +14
    129  jmp [rip] -> 0x1404DB2FC (site + 6)    143  jmp [rip] -> 0x1404DB365 (site + 0x6F, the jne's own target)    157 bytes */
void t_t940_cursor_stub()
{
    using namespace buildnear;
    static const unsigned char gold[157] = {
        0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,
        0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00,
        0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30, 0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40,
        0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50, 0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70,
        0x48, 0x89, 0xF1,
        0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0xFF, 0xD0,
        0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30, 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40,
        0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50, 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70,
        0x48, 0x81, 0xC4, 0x88, 0x00, 0x00, 0x00,
        0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58,
        0x83, 0x7E, 0x38, 0x00,
        0x75, 0x0E,
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0xFC, 0xB2, 0x4D, 0x40, 0x01, 0x00, 0x00, 0x00,
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x65, 0xB3, 0x4D, 0x40, 0x01, 0x00, 0x00, 0x00 };
    unsigned char p[200];
    for (int i = 0; i < 200; ++i) p[i] = 0xCC;
    const int n = ZoneStubBuild(p, 0x1404DB2F6ULL, 0x1122334455667788ULL);
    if (n != 157 || kZoneStubLen != 157) { Fail("t940: the zone stub is not 157 bytes (" + I(n) + ")"); return; }
    for (int i = 0; i < 157; ++i)
        if (p[i] != gold[i]) { Fail("t940: zone stub byte " + I(i) + " is " + I((int)p[i]) + ", the listing says " + I((int)gold[i])); break; }
    if (p[157] != 0xCC) Fail("t940: the zone stub wrote past its length");
    /* the stack at the call, read from the emitted bytes - every push before the `sub rsp, imm32` (50..57, or 41 50..57), the
       imm32, and the call (FF D0) after it: rsp 16-aligned at the site must stay 16-aligned at the call */
    {
        int at = 0, pushed = 0, sub = -1, callAt = -1;
        while (at < n)
        {
            if (p[at] >= 0x50 && p[at] <= 0x57) { pushed += 8; ++at; continue; }
            if (p[at] == 0x41 && at + 1 < n && p[at + 1] >= 0x50 && p[at + 1] <= 0x57) { pushed += 8; at += 2; continue; }
            break;
        }
        if (at + 7 <= n && p[at] == 0x48 && p[at + 1] == 0x81 && p[at + 2] == 0xEC) sub = (int)p[at + 3] | ((int)p[at + 4] << 8) | ((int)p[at + 5] << 16) | ((int)p[at + 6] << 24);
        for (int i = at; i + 1 < n; ++i) if (p[i] == 0xFF && p[i + 1] == 0xD0) { callAt = i; break; }
        if (sub < 0x20 + 6 * 16 || callAt < 0 || (pushed + sub) % 16 != 0) Fail("t940: the emitted stub misaligns the stack at its call (pushed " + I(pushed) + ", sub " + I(sub) + ")");
        /* the add before the pops gives back what the sub took */
        int addOk = 0;
        for (int i = callAt; i + 7 <= n; ++i) if (p[i] == 0x48 && p[i + 1] == 0x81 && p[i + 2] == 0xC4) { addOk = ((int)p[i + 3] | ((int)p[i + 4] << 8)) == sub ? 1 : 0; break; }
        if (addOk != 1) Fail("t940: the emitted stub does not give back its stack");
    }
    /* the site's six bytes, read from the emitter's own output - its re-done compare is the site's first four, and its two exits
       are site + 6 (fall through) and the jne's own target, site + 6 + the jne's rel8 (the site's byte 5) */
    {
        if (kZoneSitePatchLen != 6 || kZoneSiteOrig[4] != 0x75) Fail("t940: the site's fifth byte is not a short jne");
        for (int i = 0; i < 4; ++i) if (p[123 + i] != kZoneSiteOrig[i]) Fail("t940: the stub's re-done compare is not the site's own bytes");
        if (p[127] != 0x75) Fail("t940: the stub's branch is not a jne (the site's)");
        unsigned long long fall = 0, jt = 0;
        for (int i = 7; i >= 0; --i) { fall = (fall << 8) | p[135 + i]; jt = (jt << 8) | p[149 + i]; }
        if (fall != 0x1404DB2F6ULL + 6 || jt != 0x1404DB2F6ULL + 6 + kZoneSiteOrig[5] || kZoneSiteJneTo != 6 + kZoneSiteOrig[5])
            Fail("t940: the stub's exits are not site + 6 and the site's jne target");
    }
    unsigned char pt[6];
    if (ZoneSitePatch(pt, 0x1404DB2F6ULL, 0x1404EB2F6ULL) != 1 || pt[0] != 0xE9 || pt[1] != 0xFB || pt[2] != 0xFF || pt[3] != 0x00 || pt[4] != 0x00 || pt[5] != 0x90)
        Fail("t940: the site patch is not jmp rel32 (stub - site - 5) + nop");
    if (ZoneSitePatch(pt, 0x1404DB2F6ULL, 0x1404DB2F6ULL - 0x100ULL) != 1 || pt[1] != 0xFB || pt[2] != 0xFE || pt[3] != 0xFF || pt[4] != 0xFF) Fail("t940: a stub below the site");
    if (ZoneSitePatch(pt, 0x1404DB2F6ULL, 0x1404DB2F6ULL + 0x90000000ULL) != 0) Fail("t940: a stub out of rel32 reach is patched");
}
/* T-940: whose another player's row is - unknown (no zone, no gap) until the team table is held, so it can never refuse a teammate;
   an unread own-town lookup never founds */
void t_t940f1_team_wait_and_unread()
{
    using namespace buildnear;
    if (ZoneRelOf(1, 0, 0) != kRelOwn || ZoneRelOf(0, 0, 1) != kRelUnknown || ZoneRelOf(0, 0, 0) != kRelUnknown || ZoneRelOf(0, 1, 1) != kRelMate || ZoneRelOf(0, 1, 0) != kRelOther)
        Fail("t940f1: ZoneRelOf");
    ZoneAnchor a[2];
    a[0].owner = 2; a[0].id = 5u; a[0].x = 0.0f; a[0].z = 0.0f; a[0].rel = ZoneRelOf(0, 0, 1);   /* a teammate, table not held */
    int w = -1;
    float d = -1.0f;
    if (ZoneVerdict(a, 1, 1000.0f, 0.0f, 1, &w, &d) != kZoneFree || ZoneVerdict(a, 1, 8000.0f, 0.0f, 1, &w, &d) != kZoneFree)
        Fail("t940f1: a player's row refuses before the team table is held");
    a[0].rel = ZoneRelOf(0, 1, 0);   /* the table held: a stranger refuses */
    if (ZoneVerdict(a, 1, 1000.0f, 0.0f, 0, &w, &d) != kZoneRefusedGap) Fail("t940f1: a stranger's row does not refuse once the table is held");
    /* an unknown row is no zone either: it does not let a stranger's gap through */
    a[0].rel = kRelUnknown; a[1].owner = 4; a[1].id = 6u; a[1].x = 3000.0f; a[1].z = 0.0f; a[1].rel = kRelOther;
    if (ZoneVerdict(a, 2, 0.0f, 0.0f, 0, &w, &d) != kZoneRefusedGap || w != 1) Fail("t940f1: an unknown row grants a zone");
    /* a faulted or missing own-town lookup (ownNear -1) never founds, so never refuses as a founding */
    if (ZoneFounds(1, -1) != -1) Fail("t940f1: an unread own-town lookup founds");
    a[1].x = 9000.0f;
    if (ZoneVerdict(a + 1, 1, 0.0f, 0.0f, ZoneFounds(1, -1), &w, &d) != kZoneFree) Fail("t940f1: an unread own-town lookup refuses as a founding");
}
/* T-940: what the hook does with preview+0x90 for every town kind it can hold, refused and allowed; the founding read */
void t_t940_cursor_act()
{
    using namespace buildnear;
    const int refusing[2] = { kZoneRefusedGap, kZoneRefusedFound };
    for (int k = 0; k < 2; ++k)
    {
        const int v = refusing[k];
        if (ZoneCursorAct(v, kHeldNone, 1) != kActRefuse || ZoneCursorAct(v, kHeldNone, 0) != kActNoCopy) Fail("t940: a refused spot with no town held - not refused with the copy / no-copy not said");
        if (ZoneCursorAct(v, kHeldNpc, 1) != kActLeave || ZoneCursorAct(v, kHeldNpc, 0) != kActLeave) Fail("t940: an NPC town's refusal is touched");
        if (ZoneCursorAct(v, kHeldOtherPlayer, 1) != kActRefuse || ZoneCursorAct(v, kHeldOtherPlayer, 0) != kActLeave) Fail("t940: another player's copy held, refused - wrong act");
        if (ZoneCursorAct(v, kHeldOwnOrMate, 1) != kActRefuse || ZoneCursorAct(v, kHeldOwnOrMate, 0) != kActNoCopy) Fail("t940: own / teammate town held, refused - wrong act");
        if (ZoneCursorAct(v, -1, 1) != kActLeave) Fail("t940: an unread held town is touched");
    }
    const int allowing[4] = { kZoneFree, kZoneOwnInside, kZoneMateInside, kZoneOwnOverGap };
    for (int k = 0; k < 4; ++k)
    {
        const int v = allowing[k];
        if (ZoneCursorAct(v, kHeldOtherPlayer, 0) != kActClear || ZoneCursorAct(v, kHeldOtherPlayer, 1) != kActClear) Fail("t940: another player's copy held where the rule allows - not cleared");
        if (ZoneCursorAct(v, kHeldNpc, 0) != kActLeave || ZoneCursorAct(v, kHeldOwnOrMate, 0) != kActLeave || ZoneCursorAct(v, kHeldNone, 0) != kActLeave || ZoneCursorAct(v, -1, 0) != kActLeave)
            Fail("t940: an NPC, own, teammate or no town touched where the rule allows");
    }
    /* founding: the creates-town record and no own outpost within 5000; unread never founds */
    if (ZoneFounds(1, 0) != 1 || ZoneFounds(1, 1) != 0 || ZoneFounds(0, 0) != 0 || ZoneFounds(-1, 0) != -1 || ZoneFounds(1, -1) != -1) Fail("t940: ZoneFounds");
    /* the founding test end to end: a founding piece 9000 u from a stranger's anchor is refused, a joining one is not */
    ZoneAnchor a[1];
    a[0].owner = 4; a[0].id = 21u; a[0].x = 0.0f; a[0].z = 0.0f; a[0].rel = kRelOther;
    int w = -1;
    float d = -1.0f;
    if (ZoneCursorAct(ZoneVerdict(a, 1, 9000.0f, 0.0f, ZoneFounds(1, 0), &w, &d), kHeldNone, 1) != kActRefuse
        || ZoneCursorAct(ZoneVerdict(a, 1, 9000.0f, 0.0f, ZoneFounds(1, 1), &w, &d), kHeldNone, 1) != kActLeave)
        Fail("t940: founding 9000 u from a stranger's anchor - not refused, or a joining piece refused");
}

/* T-940 (run T1550): an NPC town whose ring the engine would refuse, hidden behind a player's town nearer to the spot, goes into the
   cursor's town slot - behind a teammate's copy the T-923 stub forgot, and behind a stranger's copy the zone rule clears; the zone
   rule's own refusal (the stranger's copy) wins over it; an NPC town already held is never asked about */
void t_t940_npc_behind()
{
    using namespace buildnear;
    /* the look-up's per-town kind: own / teammate's = a candidate meaning no NPC town, NPC = a candidate, a stranger's copy or unread = left out */
    if (NpcBehindKind(kHeldOwnOrMate) != kNbOwn || NpcBehindKind(kHeldNpc) != kNbNpc || NpcBehindKind(kHeldOtherPlayer) != kNbSkip || NpcBehindKind(-1) != kNbSkip)
        Fail("t940: the NPC-town look-up judges a town wrongly (own / teammate's, NPC, stranger's copy, unread)");
    /* a teammate's copy nearest: the stub forgot it, the slot is empty, the zone rule allows; the look-up meets the teammate's copy first
       and answers no NPC town (npcRefuses 0), so the slot is left as the engine set it */
    const int mateAct = ZoneCursorAct(kZoneMateInside, kHeldNone, 0);
    if (mateAct != kActLeave || ZoneNpcAsk(mateAct, kHeldNone, 1) != 1) Fail("t940: behind a teammate's forgotten copy the look-up is not asked");
    if (ZoneCursorSlot(mateAct, kHeldNone, 1, 0) != kSlotLeave) Fail("t940: a teammate's copy nearest - the slot is touched");
    /* a stranger's copy outside its own ring nearest (slot empty, zone rule free), an NPC ring behind it refusing: the NPC town is held */
    if (ZoneCursorSlot(ZoneCursorAct(kZoneFree, kHeldNone, 0), kHeldNone, 1, 1) != kSlotNpc) Fail("t940: a stranger's copy nearest + an NPC ring refusing - the NPC town is not held");
    if (ZoneNpcAsk(ZoneCursorAct(kZoneFree, kHeldNone, 0), kHeldNone, 0) != 0 || ZoneCursorSlot(kActLeave, kHeldNone, 0, 1) != kSlotLeave)
        Fail("t940: with no player's town nearest the engine's own answer (none) is changed");
    /* a stranger's copy the rule clears */
    const int clearAct = ZoneCursorAct(kZoneFree, kHeldOtherPlayer, 0);
    if (clearAct != kActClear || ZoneCursorSlot(clearAct, kHeldOtherPlayer, 0, 1) != kSlotNpc) Fail("t940: a stranger's copy cleared + an NPC ring refusing - the NPC town is not held");
    if (ZoneCursorSlot(clearAct, kHeldOtherPlayer, 0, 0) != kSlotNone) Fail("t940: a stranger's copy cleared, no NPC ring - the slot is not emptied");
    /* the zone rule refuses: the stranger's copy wins, the NPC town is not even asked about */
    const int refuseAct = ZoneCursorAct(kZoneRefusedGap, kHeldNone, 1);
    if (refuseAct != kActRefuse || ZoneNpcAsk(refuseAct, kHeldNone, 1) != 0 || ZoneCursorSlot(refuseAct, kHeldNone, 1, 1) != kSlotCopy)
        Fail("t940: an NPC ring wins over the zone rule's refusal");
    /* refused with no copy to show it: an NPC ring refusing still shows a refusal */
    if (ZoneCursorSlot(ZoneCursorAct(kZoneRefusedFound, kHeldNone, 0), kHeldNone, 0, 1) != kSlotNpc) Fail("t940: no copy, an NPC ring refusing - not held");
    /* an NPC town already held (the engine's own refusal), or an unread slot: never asked, never touched */
    if (ZoneNpcAsk(kActLeave, kHeldNpc, 1) != 0 || ZoneCursorSlot(kActLeave, kHeldNpc, 1, 1) != kSlotLeave || ZoneCursorSlot(kActLeave, -1, 1, 1) != kSlotLeave)
        Fail("t940: an NPC town held or an unread slot is touched");
}

/* The swing credit (swinggate.h): one owner swing at a target lets the copy of another player's character start one swing at
   that target, within kLifeMs of its arrival, at most kCap held; spent only when the engine really starts the swing; own
   characters, NPC copies, unclassified copies and world NPCs always pass. */
/* The [FIGHT] names (fightlog.h): a name prints only when it is 1..48 printable characters with no quote; '-' is no object. */
void t_t1101_fight_name()
{
    namespace fl = fightlog;
    char out[fl::kNameBuf];
    if (!fl::CleanName("HtH_Punch", 9, out) || std::strcmp(out, "HtH_Punch") != 0) Fail("t1101 fight: a plain move name was not copied");
    if (fl::CleanName("Katana high slash", 6, out) != 1 || std::strcmp(out, "Katana") != 0) Fail("t1101 fight: the length was not honoured");
    if (fl::CleanName(0, 5, out) != 0 || std::strcmp(out, "?") != 0) Fail("t1101 fight: an unread name is not '?'");
    if (fl::CleanName("x", 0, out) != 0 || std::strcmp(out, "?") != 0) Fail("t1101 fight: an empty name is not '?'");
    const char bad1[] = { 'a', 0x01, 'b' };
    if (fl::CleanName(bad1, 3, out) != 0 || std::strcmp(out, "?") != 0) Fail("t1101 fight: a control character printed");
    const char bad2[] = { 'a', (char)0xE9, 'b' };
    if (fl::CleanName(bad2, 3, out) != 0 || std::strcmp(out, "?") != 0) Fail("t1101 fight: a non-ASCII byte printed");
    if (fl::CleanName("it's", 4, out) != 0 || std::strcmp(out, "?") != 0) Fail("t1101 fight: a quote printed inside '...'");
    char longName[64];
    std::memset(longName, 'A', sizeof(longName));
    if (fl::CleanName(longName, 48, out) != 1 || std::strlen(out) != 48) Fail("t1101 fight: a 48-character name was refused");
    if (fl::CleanName(longName, 49, out) != 0 || std::strcmp(out, "?") != 0) Fail("t1101 fight: a 49-character name printed");
    fl::NoName(out);
    if (std::strcmp(out, "-") != 0) Fail("t1101 fight: no object is not '-'");
}

/* The [FIGHT] ring (fightlog.h): oldest first, a full ring refuses and counts the loss once, at most kCap events ever, and the
   kCap-th line is the one that carries the cap note. */
void t_t1101_fight_cap()
{
    namespace fl = fightlog;
    static fl::Ring g;
    fl::Reset(&g);
    fl::Event ev;
    std::memset(&ev, 0, sizeof(ev));
    fl::Event got[fl::kRing];
    long long lost = 0;
    for (int i = 0; i < fl::kRing; ++i) { ev.a = (unsigned int)i + 1; if (!fl::Push(&g, ev)) Fail("t1101 fight: a free slot refused"); }
    if (fl::Push(&g, ev) != 0) Fail("t1101 fight: a full ring accepted");
    if (fl::Take(&g, got, 10, &lost) != 10 || got[0].a != 1 || got[9].a != 10 || lost != 1) Fail("t1101 fight: take order or loss count wrong");
    if (fl::Take(&g, got, fl::kRing, &lost) != fl::kRing - 10 || got[0].a != 11 || lost != 0) Fail("t1101 fight: the rest not taken or loss counted twice");
    int accepted = fl::kRing, lines = 0, lastAt = 0;
    for (int i = 0; i < fl::kRing; ++i) { ++lines; if (fl::LineIsLast(&g)) Fail("t1101 fight: an early line carried the cap note"); }
    for (int round = 0; round < 100; ++round)
    {
        for (int i = 0; i < 8; ++i) if (fl::Push(&g, ev)) ++accepted;
        const int n = fl::Take(&g, got, fl::kRing, &lost);
        for (int i = 0; i < n; ++i) { ++lines; if (fl::LineIsLast(&g)) lastAt = lines; }
    }
    if (accepted != fl::kCap || g.accepted != fl::kCap) Fail("t1101 fight: the cap is not kCap events");
    if (fl::Push(&g, ev) != 0) Fail("t1101 fight: an event accepted after the cap");
    if (lastAt != fl::kCap || lines != fl::kCap || g.written != fl::kCap) Fail("t1101 fight: the cap note is not on the kCap-th line");
}

void t_t1101_swing_credit()
{
    namespace sg = swinggate;
    static sg::Store st;
    sg::Reset(&st);
    const unsigned int A = 101, B = 202, X = 7, Y = 8;

    /* who is gated */
    if (sg::RuleFor(A, 0, 1) != sg::kGated) Fail("t1101: another player's character's copy is not gated");
    if (sg::RuleFor(A, 1, 1) != sg::kOwn || sg::RuleFor(A, 1, 0) != sg::kOwn) Fail("t1101: this game's own character is gated");
    if (sg::RuleFor(A, 0, 0) != sg::kNpc) Fail("t1101: an NPC copy is gated");
    if (sg::RuleFor(A, 0, -1) != sg::kUnclassified) Fail("t1101: an unclassified copy is gated");
    if (sg::RuleFor(0, 0, 1) != sg::kNotCopy) Fail("t1101: a world NPC (no uid) is gated");

    /* own, NPC, unclassified and world NPC starts always run the original, with no credit, and are counted */
    if (sg::Before(&st, A, 1, 1, X, 1000) != sg::kRun || sg::Before(&st, A, 0, 0, X, 1000) != sg::kRun
        || sg::Before(&st, A, 0, -1, X, 1000) != sg::kRun || sg::Before(&st, 0, 0, -1, X, 1000) != sg::kRun)
        Fail("t1101: an ungated attack start was not simply run");
    if (st.c.passOwn != 1 || st.c.passNpc != 1 || st.c.passUnclassified != 1 || st.c.passNotCopy != 1 || st.c.declined != 0)
        Fail("t1101: the pass counters do not show one of each");

    /* no owner swing: declined, every frame, as one hold run */
    if (sg::Gate(&st, A, 0, 1, X, 1000, 1) != 0 || sg::Gate(&st, A, 0, 1, X, 1016, 1) != 0) Fail("t1101: a copy swung with no owner swing");
    if (st.c.declined != 2 || st.c.holds != 1) Fail("t1101: two declines in a row are not one hold run");

    /* add then the engine starts: one swing, then declined again (a new hold run) */
    sg::Reset(&st);
    if (sg::Add(&st, A, X, 2000) != 1) Fail("t1101: a credit was not stored");
    if (sg::Gate(&st, A, 0, 1, X, 2010, 1) != 1) Fail("t1101: the owner's swing did not let the copy swing");
    if (sg::Gate(&st, A, 0, 1, X, 2020, 1) != 0) Fail("t1101: one owner swing let the copy swing twice");
    if (st.c.added != 1 || st.c.allowed != 1 || st.c.declined != 1 || st.c.holds != 1) Fail("t1101: add / spend counters wrong");
    sg::Add(&st, A, X, 2100);
    if (sg::Gate(&st, A, 0, 1, X, 2110, 1) != 1 || sg::Gate(&st, A, 0, 1, X, 2120, 1) != 0 || st.c.holds != 2)
        Fail("t1101: a started swing does not end the hold run");

    /* the engine refused (no technique fits the gap): the credit is kept for its next attempt, counted engineNo */
    sg::Reset(&st);
    sg::Add(&st, A, X, 3000);
    if (sg::Before(&st, A, 0, 1, X, 3010) != sg::kRunThenSettle) Fail("t1101: a held credit did not let the engine's attack start run");
    sg::After(&st, A, X, 3010, 0);
    if (st.c.engineNo != 1 || st.c.allowed != 0) Fail("t1101: an engine refusal was not counted engineNo");
    if (sg::Gate(&st, A, 0, 1, X, 3026, 0) != 0 || sg::Gate(&st, A, 0, 1, X, 3042, 1) != 1) Fail("t1101: an engine refusal burnt the credit");
    if (sg::Gate(&st, A, 0, 1, X, 3058, 1) != 0 || st.c.engineNo != 2 || st.c.allowed != 1) Fail("t1101: the kept credit was spent twice");

    /* targets: a credit for X is not spent on an attack at Y nor on an unreadable target; target 0 matches anything */
    sg::Reset(&st);
    sg::Add(&st, A, X, 4000);
    if (sg::Gate(&st, A, 0, 1, Y, 4010, 1) != 0) Fail("t1101: a credit for X was spent on an attack at Y");
    if (sg::Gate(&st, A, 0, 1, 0, 4011, 1) != 0) Fail("t1101: a credit for X was spent on an unreadable target");
    if (sg::Gate(&st, A, 0, 1, X, 4012, 1) != 1) Fail("t1101: the credit for X was not spent on an attack at X");
    sg::Add(&st, A, 0, 4100);
    if (sg::Gate(&st, A, 0, 1, 0, 4110, 1) != 1) Fail("t1101: a target-0 credit was not spent on an unreadable target");
    sg::Add(&st, A, 0, 4200);
    if (sg::Gate(&st, A, 0, 1, Y, 4210, 1) != 1) Fail("t1101: a target-0 credit was not spent on an attack at Y");
    /* a credit for X then one for Y: the newer owner swing supersedes the older (dropped, counted); an attack at X finds none,
       one at Y spends the Y credit */
    const long long sup0 = st.c.supersededDrop;
    sg::Add(&st, A, X, 4300); sg::Add(&st, A, Y, 4301);
    if (st.c.supersededDrop != sup0 + 1 || sg::Gate(&st, A, 0, 1, X, 4310, 1) != 0 || sg::Gate(&st, A, 0, 1, Y, 4311, 1) != 1
        || sg::Gate(&st, A, 0, 1, Y, 4312, 1) != 0)
        Fail("t1101: the newer credit did not supersede the older, or the matching credit was not the one spent");

    /* lapse: a credit older than kLifeMs is gone, counted */
    sg::Reset(&st);
    sg::Add(&st, A, X, 5000);
    if (sg::Gate(&st, A, 0, 1, X, 5000 + sg::kLifeMs - 1, 1) != 1) Fail("t1101: a credit inside its window was refused");
    sg::Add(&st, A, X, 7000);
    if (sg::Gate(&st, A, 0, 1, X, 7000 + sg::kLifeMs, 1) != 0) Fail("t1101: a lapsed credit was spent");
    if (st.c.lapsed != 1) Fail("t1101: the lapsed credit was not counted");

    /* a burst (after a network stall several swings arrive at once): each newer swing supersedes the older credit, so the
       copy holds one (kCap 1), the newest; nothing is capped */
    sg::Reset(&st);
    sg::Add(&st, A, X, 9000); sg::Add(&st, A, X, 9001); sg::Add(&st, A, X, 9002);
    if (sg::kCap != 1 || st.c.supersededDrop != 2 || st.c.capped != 0 || st.c.added != 3) Fail("t1101: a burst of three did not keep only the newest credit");
    if (sg::Gate(&st, A, 0, 1, X, 9010, 1) != 1 || sg::Gate(&st, A, 0, 1, X, 9011, 1) != 0)
        Fail("t1101: a burst of three gave other than one swing");
    /* the kept credit is the NEWEST: at 9001 + kLifeMs the 9002 credit is still live */
    sg::Reset(&st);
    sg::Add(&st, A, X, 9000); sg::Add(&st, A, X, 9001); sg::Add(&st, A, X, 9002);
    if (sg::Gate(&st, A, 0, 1, X, 9001 + sg::kLifeMs, 1) != 1) Fail("t1101: the burst kept an older credit than the newest");

    /* one copy's credit is not another's; an ungated start never spends one */
    sg::Reset(&st);
    sg::Add(&st, A, X, 10000);
    if (sg::Gate(&st, B, 0, 1, X, 10001, 1) != 0) Fail("t1101: copy B swung on copy A's credit");
    if (sg::Gate(&st, A, 0, 1, X, 10002, 1) != 1) Fail("t1101: copy A lost its credit to copy B's attempt");
    sg::Add(&st, A, X, 10100);
    if (sg::Gate(&st, A, 0, 0, X, 10101, 1) != 1 || sg::Gate(&st, A, 0, 1, X, 10102, 1) != 1) Fail("t1101: an ungated start spent a credit");

    /* the millisecond clock wrapping inside a window */
    sg::Reset(&st);
    sg::Add(&st, A, X, 0xFFFFFF00u);
    if (sg::Gate(&st, A, 0, 1, X, 0x00000100u, 1) != 1) Fail("t1101: a credit across the clock wrap was refused");

    /* every slot holding a live credit: a new copy's credit is dropped (noSlot), then a lapsed slot is reused */
    sg::Reset(&st);
    for (int i = 0; i < sg::kSlots; ++i) sg::Add(&st, 1000 + i, X, 20000);
    if (sg::Add(&st, 5000, X, 20001) != 0 || st.c.noSlot != 1) Fail("t1101: a full store took a credit");
    if (sg::Gate(&st, 5000, 0, 1, X, 20002, 1) != 0 || st.c.noSlot != 2) Fail("t1101: a copy with no slot swung");
    if (sg::Add(&st, 5000, X, 20000 + sg::kLifeMs) != 1) Fail("t1101: a lapsed slot was not reused");
    if (sg::Gate(&st, 5000, 0, 1, X, 20000 + sg::kLifeMs + 1, 1) != 1) Fail("t1101: the reused slot's credit was not spent");

    /* hold log lines: at most kLogPerCopy per copy, each run once */
    sg::Reset(&st);
    unsigned int uids[8];
    int runs[8];
    int lines = 0;
    for (int r = 0; r < sg::kLogPerCopy + 2; ++r)
    {
        sg::Gate(&st, A, 0, 1, X, 30000 + r * 10, 1);
        lines += sg::TakeLogs(&st, uids, runs, 8);
        if (sg::TakeLogs(&st, uids, runs, 8) != 0) Fail("t1101: one hold run gave two log lines");
        sg::Add(&st, A, X, 30000 + r * 10 + 1);
        sg::Gate(&st, A, 0, 1, X, 30000 + r * 10 + 2, 1);
    }
    if (lines != sg::kLogPerCopy) Fail("t1101: the hold lines are not capped per copy");
}

/* The swing credit lives until the copy's next started swing (T1613: a copy's swing lasts ~1.6-1.8 s, so a credit arriving
   mid-swing must outlast it), up to the kLifeMs safety limit from arrival, and is dropped at the owner's combat-off; with no
   owner swing (the owner on BLOCK) the copy never starts one. */
void t_t1101_swing_credit_hold()
{
    namespace sg = swinggate;
    static sg::Store st;
    const unsigned int A = 101, B = 202, X = 7;

    /* a credit used 1.8 s after arrival (the copy was still playing its previous swing) is allowed */
    sg::Reset(&st);
    sg::Add(&st, A, X, 1000);
    if (sg::Gate(&st, A, 0, 1, X, 1000 + 1800, 1) != 1) Fail("t1101 hold: a credit used at 1.8 s was refused");
    if (st.c.lapsed != 0 || st.c.allowed != 1) Fail("t1101 hold: the 1.8 s credit was counted lapsed");

    /* the engine refusing for 1.8 s (no technique fits while the previous swing plays) keeps it for the started swing */
    sg::Reset(&st);
    sg::Add(&st, A, X, 1000);
    for (unsigned int t = 1016; t < 2800; t += 16)
        if (sg::Gate(&st, A, 0, 1, X, t, 0) != 0) Fail("t1101 hold: an engine refusal started a swing");
    if (sg::Gate(&st, A, 0, 1, X, 2800, 1) != 1) Fail("t1101 hold: a credit kept through engine refusals was not spent");

    /* a credit used 5.1 s after arrival lapses, counted */
    sg::Reset(&st);
    sg::Add(&st, A, X, 1000);
    if (sg::Gate(&st, A, 0, 1, X, 1000 + 5100, 1) != 0) Fail("t1101 hold: a credit used at 5.1 s was spent");
    if (st.c.lapsed != 1 || st.c.allowed != 0) Fail("t1101 hold: the 5.1 s credit was not counted lapsed");

    /* the owner's combat-off drops every held credit of that copy only, counted; the next start is declined */
    sg::Reset(&st);
    sg::Add(&st, A, X, 1000); sg::Add(&st, A, X, 1001);
    sg::Add(&st, B, X, 1002);
    if (st.c.supersededDrop != 1 || sg::DropAll(&st, A) != 1 || st.c.dropCombatOff != 1) Fail("t1101 hold: combat-off did not drop the one held credit (the second swing superseded the first)");
    if (sg::Gate(&st, A, 0, 1, X, 1100, 1) != 0) Fail("t1101 hold: a copy swung on a credit dropped at combat-off");
    if (sg::Gate(&st, B, 0, 1, X, 1101, 1) != 1) Fail("t1101 hold: another copy's credit was dropped at A's combat-off");
    if (sg::DropAll(&st, A) != 0 || sg::DropAll(&st, 999) != 0 || st.c.dropCombatOff != 1) Fail("t1101 hold: an empty combat-off counted");
    /* a new owner swing after the combat-off is a credit again */
    sg::Add(&st, A, X, 1200);
    if (sg::Gate(&st, A, 0, 1, X, 1210, 1) != 1) Fail("t1101 hold: a swing after combat-off gave no credit");

    /* BLOCK-like: the owner swings nothing for 10 s; the copy's engine asks every frame and never starts */
    sg::Reset(&st);
    for (unsigned int t = 1000; t < 11000; t += 16)
        if (sg::Gate(&st, A, 0, 1, X, t, 1) != 0) Fail("t1101 hold: a copy with no owner swing started one");
    if (st.c.allowed != 0 || st.c.holds != 1) Fail("t1101 hold: the no-swing case is not one hold run with no swing");
}

/* T-1097: this game's awake copy built from the world record, or from its save, yields to the announcing game; a copy made live here
   keeps the id refusal; a handed-over squad holding a person this game runs is kept; the pair guard refuses both of a pair
   (src/common/sameidpair.h) */
void t1097_same_id_copy()
{
    using namespace sameid;
    /* where the copy came from */
    if (CopyOrigin(1, 1, 5u, 100u) != kCopyFromRecord || CopyOrigin(1, 0, 0u, 0u) != kCopyFromRecord) Fail("origin: a copy built from the record is not read as one");
    if (CopyOrigin(0, 1, 5u, 100u) != kCopyFromSave) Fail("origin: a number below the load floor is not the save's");
    if (CopyOrigin(0, 1, 100u, 100u) != kCopyLive || CopyOrigin(0, 1, 65540u, 100u) != kCopyLive) Fail("origin: a number at or above the floor is not made live");
    if (CopyOrigin(0, 0, 5u, 0u) != kCopyLive) Fail("origin: an unknown floor reads as the save's");
    /* a record-built copy yields - decided as this game's or not, by any source */
    if (CopyYield(kCopyFromRecord, 1, 0, 0, 0, 2, 2) != kVerdictYield || CopyYield(kCopyFromRecord, 1, 1, 0, 0, 2, 2) != kVerdictYield
        || CopyYield(kCopyFromRecord, 0, 0, 0, 0, 0, 0) != kVerdictYield) Fail("yield: a record-built copy does not yield");
    /* a save-born awake copy yields */
    if (CopyYield(kCopyFromSave, 1, 0, 0, 0, 0, 0) != kVerdictYield) Fail("yield: a save copy decided as this game's by a source other than its members does not yield");
    if (CopyYield(kCopyFromSave, 0, 0, 0, 0, 3, 3) != kVerdictYield) Fail("yield: a save copy decided as another game's, every person this game's, does not yield");
    /* ...except where the load-age weighing decides, or another game's person is aboard */
    if (CopyYield(kCopyFromSave, 1, 1, 0, 0, 3, 3) != kVerdictKeepLoadAge) Fail("yield: a save copy decided by its own members is put away over the load-age test");
    if (CopyYield(kCopyFromSave, 0, 0, 0, 0, 3, 2) != kVerdictKeepOthersAboard) Fail("yield: a save copy holding another game's person is put away");
    /* a copy made live here keeps the id refusal; decided as another game's, the earlier roads decide */
    if (CopyYield(kCopyLive, 1, 0, 0, 0, 2, 2) != kVerdictKeepLive || CopyYield(kCopyLive, 1, 1, 0, 0, 2, 2) != kVerdictKeepLive) Fail("yield: a copy made live here loses the id refusal");
    if (CopyYield(kCopyLive, 0, 0, 0, 0, 2, 2) != kVerdictToday) Fail("yield: a live copy decided as another game's is judged by this rule");
    /* handed over, a person this game runs (or an unread one) still in it: kept, whatever the origin */
    if (CopyYield(kCopyFromRecord, 0, 0, 0, 1, 2, 1) != kVerdictKeepHandedOverMine || CopyYield(kCopyFromSave, 0, 0, 0, 1, 2, 2) != kVerdictKeepHandedOverMine)
        Fail("yield: a handed-over squad holding a person this game runs is put away");
    /* a player's side; people unread */
    if (CopyYield(kCopyFromRecord, 0, 0, 1, 0, 0, 0) != kVerdictKeepPlayerSide || CopyYield(kCopyFromSave, 0, 0, 1, 0, 0, 0) != kVerdictKeepPlayerSide) Fail("yield: a player-side squad is put away");
    if (CopyYield(kCopyFromRecord, 1, 0, 0, 0, -1, 0) != kVerdictKeepUnread || CopyYield(kCopyFromSave, 0, 0, 0, 0, 2, -1) != kVerdictKeepUnread) Fail("yield: a copy whose people are unread is put away");
    /* the twin step follows the yield and nothing else */
    if (TwinAdoptRefused(kVerdictYield) != 1) Fail("twin: a body in a platoon about to be put away is adopted");
    for (int v = 0; v < kVerdictCount; ++v) { if (v != kVerdictYield && TwinAdoptRefused(v) != 0) Fail("twin: adoption refused on a verdict that keeps the platoon"); if (VerdictWord(v)[0] == '?') Fail("verdict: a word is missing"); }
    /* the pair guard refuses both of a pair */
    if (PairRefuse(1, 0) != 1 || PairRefuse(0, 1) != 1 || PairRefuse(1, 1) != 1 || PairRefuse(0, 0) != 0) Fail("pair: the refusal table");
    {
        int a = 0, b = 0, c = 0; const void* pa = &a; const void* pb = &b; const void* pc = &c; const void* partner = 0;
        PairBook book;
        /* both bound to the id: each refused at once, each naming the other */
        if (book.Judge(pa, "Faction_7", pb, &partner) != 1 || partner != pb) Fail("pair: the first of two bound platoons writes");
        if (book.Judge(pb, "Faction_7", pa, &partner) != 1 || partner != pa) Fail("pair: the second of two bound platoons writes");
        book.Clear();
        /* one bound (pa), one answering by its engine id (pb): pa sees no other binding and writes until pb is judged; from then on both are refused */
        if (book.Judge(pa, "Faction_7", 0, &partner) != 0) Fail("pair: a lone platoon is refused");
        if (book.Judge(pb, "Faction_7", pa, &partner) != 1) Fail("pair: the unbound side writes beside the bound one");
        if (book.Judge(pa, "Faction_7", 0, &partner) != 1 || partner != pb) Fail("pair: the bound side writes once the other side was found");
        if (book.Judge(pc, "Faction_8", 0, &partner) != 0) Fail("pair: a platoon of another id is refused");
        if (book.Pairs() != 1) Fail("pair: the notes do not name one id");
        /* the other side is forgotten (destroyed): the pair is gone, the one left writes again */
        book.Forget(pb);
        if (book.Judge(pa, "Faction_7", 0, &partner) != 0 || book.Pairs() != 0) Fail("pair: the side left alone stays refused after its pair was forgotten");
    }
}

/* T-1097: one side's put-away as spawn.cpp PrepareContextForSpawn takes it - the load-age lift (its member half taken as met, so a lift
   is over-counted, never missed; never for a copy made live, whose number is not below a known floor, so its own members never decide
   it), then the copy rule through its gates and the yield's order. origin: this side's copy; announced: the other side's copy as its
   CONTEXT carried it. 1 = this side's copy is put away. */
static int T1097SidePutsAway(int origin, int announced, int ownBlk, int why, int sweepAll, int playerSide, int hoKeep, int sess, int mine, int live,
                             unsigned int ownAge, unsigned int peerAge, int mySeat, int peerSeat)
{
    const int lift = (ownBlk != 0 && origin != sameid::kCopyLive && sqorigin::OwnIdLiftedByLoadAge(why, sweepAll, ownAge, peerAge, mySeat, peerSeat) == sqorigin::kOwnIdLifted) ? 1 : 0;
    const int ownId = (ownBlk != 0 && lift == 0) ? 1 : 0;
    const int v = sameid::YieldGate(sameid::CopyYield(origin, ownId, why == sqorigin::kByMembers ? 1 : 0, playerSide, hoKeep, sess, mine), live, live,
                                    sameid::YieldOrder(origin, announced, ownAge, peerAge, mySeat, peerSeat));
    return (lift != 0 || v == sameid::kVerdictYield) ? 1 : 0;
}
/* 1 = some state of one side, its copy of this origin, puts it away under these shared inputs (every id answer and source, sweep, side,
   hand-over, people count and gate) */
static int T1097AnyPutsAway(int origin, int announced, unsigned int ownAge, unsigned int peerAge, int mySeat, int peerSeat)
{
    const int whys[3] = { sqorigin::kByFloor, sqorigin::kByMembers, sqorigin::kByRecord };
    const int sm[5][2] = { { 2, 2 }, { 2, 1 }, { 0, 0 }, { -1, 0 }, { 2, -1 } };
    for (int ownBlk = 0; ownBlk < 2; ++ownBlk) for (int w = 0; w < 3; ++w) for (int sweep = 0; sweep < 2; ++sweep)
        for (int ps = 0; ps < 2; ++ps) for (int ho = 0; ho < 2; ++ho) for (int k = 0; k < 5; ++k) for (int live = 0; live < 2; ++live)
            if (T1097SidePutsAway(origin, announced, ownBlk, whys[w], sweep, ps, ho, sm[k][0], sm[k][1], live, ownAge, peerAge, mySeat, peerSeat) != 0) return 1;
    return 0;
}
/* T-1097 fold 1: across every combination, the two games never both put their copy of one squad away */
void t1097f_never_both_put_away()
{
    using namespace sameid;
    /* the case the turn closes: CopyYield alone yields a record-built copy from the older load while the announcer's own save copy,
       decided by its members from the younger load, is lifted there */
    if (CopyYield(kCopyFromRecord, 1, 0, 0, 0, 2, 2) != kVerdictYield || sqorigin::OwnIdLiftedByLoadAge(sqorigin::kByMembers, 1, 30000u, 3600000u, 1, 0) != sqorigin::kOwnIdLifted)
        Fail("t1097f: the mutual case's premise moved");
    if (YieldGate(kVerdictYield, 1, 1, YieldOrder(kCopyFromRecord, kCopyFromSave, 3600000u, 30000u, 0, 1)) != kVerdictKeepTurn) Fail("t1097f: a record-built copy from the older load yields while the announcer's younger copy is lifted");
    const unsigned int ages[6] = { 1000u, 4000u, 6000u, 30000u, 3600000u, sqorigin::kLoadAgeUnknown };
    const unsigned int transit[2] = { 0u, 7000u };
    const int seats[4] = { -1, 0, 1, 2 };
    /* every pair of origins; each side reads the other's origin as carried, or unknown (a sender without the field) */
    for (int oa = 0; oa < 3; ++oa) for (int ob = 0; ob < 3; ++ob) for (int ua = 0; ua < 2; ++ua) for (int ub = 0; ub < 2; ++ub)
    {
        const int bSeenOrigin = ub != 0 ? kCopyUnknown : ob, aSeenOrigin = ua != 0 ? kCopyUnknown : oa;
        for (int a = 0; a < 6; ++a) for (int b = 0; b < 6; ++b) for (int sa = 0; sa < 4; ++sa) for (int sb = 0; sb < 4; ++sb) for (int ta = 0; ta < 2; ++ta) for (int tb = 0; tb < 2; ++tb)
        {
            const unsigned int A = ages[a], B = ages[b];
            /* each side sees the other's age short by its transit (LoadAgeAdvance from the send) */
            const unsigned int bSeenByA = B == sqorigin::kLoadAgeUnknown ? B : (B > transit[tb] ? B - transit[tb] : 0u);
            const unsigned int aSeenByB = A == sqorigin::kLoadAgeUnknown ? A : (A > transit[ta] ? A - transit[ta] : 0u);
            if (T1097AnyPutsAway(oa, bSeenOrigin, A, bSeenByA, seats[sa], seats[sb]) != 0 && T1097AnyPutsAway(ob, aSeenOrigin, B, aSeenByB, seats[sb], seats[sa]) != 0)
            { Fail("t1097f: both games can put their copy of one squad away"); return; }
        }
    }
}
/* T-1097 folds 1 and 3: the bartender case (the host's running copy, the joiner's record-built copy) still yields on the joiner; a late
   announcement does not take a copy down; an unknown order gives no turn */
void t1097f_bartender_and_late()
{
    using namespace sameid;
    const unsigned int host = 3600000u, joiner = 30000u;   /* the host (seat 0) has run for an hour; the joiner (seat 1) loaded 30 s ago */
    for (int ownId = 0; ownId < 2; ++ownId) for (int ann = kCopyUnknown; ann <= kCopyLive; ++ann)
        if (YieldGate(CopyYield(kCopyFromRecord, ownId, 0, 0, 0, 2, 2), 1, 1, YieldOrder(kCopyFromRecord, ann, joiner, host, 1, 0)) != kVerdictYield) Fail("bartender: the joiner's record-built copy does not yield to the host's running copy");
    if (YieldGate(CopyYield(kCopyFromRecord, 0, 0, 0, 0, 2, 2), 1, 1, YieldOrder(kCopyFromRecord, kCopyFromRecord, 4000u, 6000u, 1, 0)) != kVerdictYield) Fail("bartender: within the tie the higher seat does not give way");
    /* the host, on the joiner's announcement of a copy the joiner does not run live (from a record, a save, or unknown), keeps its copy:
       its load is the older one */
    const int notRunning[3] = { kCopyUnknown, kCopyFromRecord, kCopyFromSave };
    for (int i = 0; i < 3; ++i)
        if (T1097SidePutsAway(kCopyLive, notRunning[i], 1, sqorigin::kByFloor, 0, 0, 0, 2, 2, 1, host, joiner, 0, 1) != 0
            || T1097SidePutsAway(kCopyFromRecord, notRunning[i], 0, sqorigin::kByFloor, 0, 0, 0, 2, 2, 1, host, joiner, 0, 1) != 0
            || T1097SidePutsAway(kCopyFromSave, notRunning[i], 1, sqorigin::kByMembers, 1, 0, 0, 2, 2, 1, host, joiner, 0, 1) != 0) Fail("bartender: the host's copy is put away for the joiner's copy that is not running");
    /* late: the announcing game out of the world, or the person's recorded owner another game */
    if (YieldGate(kVerdictYield, 0, 1, 1) != kVerdictKeepLate || YieldGate(kVerdictYield, 0, 0, 1) != kVerdictKeepLate) Fail("late: a yield acted on an announcement whose game left the world");
    if (YieldGate(kVerdictYield, 1, 0, 1) != kVerdictKeepOwnerMoved) Fail("late: a yield acted on an announcement whose person another game now owns");
    if (YieldGate(kVerdictKeepLive, 0, 0, 0) != kVerdictKeepLive || YieldGate(kVerdictToday, 1, 1, 1) != kVerdictToday || YieldGate(kVerdictKeepLoadAge, 1, 1, 0) != kVerdictKeepLoadAge) Fail("gate: a verdict other than yield is changed");
    if (YieldTurn(sqorigin::kLoadAgeUnknown, 1000u, 1, 0) != 0 || YieldTurn(1000u, sqorigin::kLoadAgeUnknown, 1, 0) != 0 || YieldTurn(1000u, 1000u, -1, 0) != 0 || YieldTurn(1000u, 2000u, 1, 1) != 0)
        Fail("turn: an unknown order gives a turn");
    if (YieldOrder(kCopyFromRecord, kCopyFromSave, sqorigin::kLoadAgeUnknown, 1000u, 1, 0) != 0 || YieldOrder(kCopyFromSave, kCopyUnknown, 1000u, 1000u, -1, 0) != 0)
        Fail("turn: an unknown order gives a turn against an announced copy that is not running");
    if (VerdictWord(kVerdictKeepTurn)[0] == '?' || VerdictWord(kVerdictKeepLate)[0] == '?' || VerdictWord(kVerdictKeepOwnerMoved)[0] == '?') Fail("verdict: a new word is missing");
}
static int T1097fStandsNone(const void*, const std::string&) { return 0; }
static int T1097fStandsAll(const void*, const std::string&) { return 1; }
/* T-1097: a noted partner that no longer stands no longer refuses; one forgotten (destroyed / retired, or unbound) neither */
void t1097f_pair_partner_gone()
{
    int a = 0, b = 0; const void* pa = &a; const void* pb = &b; const void* partner = 0;
    sameid::PairBook book;
    if (book.Judge(pb, "Faction_7", pa, &partner, T1097fStandsAll) != 1) Fail("pair gone: the unbound side beside a bound one writes");
    if (book.Judge(pa, "Faction_7", 0, &partner, T1097fStandsAll) != 1 || partner != pb) Fail("pair gone: a standing partner's note does not refuse");
    if (book.Judge(pa, "Faction_7", 0, &partner, T1097fStandsNone) != 0 || book.Pairs() != 0) Fail("pair gone: a partner that is gone still refuses");
    if (book.Judge(pb, "Faction_7", pa, &partner, T1097fStandsAll) != 1) Fail("pair gone: the pair is not noted again");
    book.Forget(pb);
    if (book.Judge(pa, "Faction_7", 0, &partner, T1097fStandsAll) != 0) Fail("pair gone: a forgotten platoon still refuses its partner");
}

/* T-1097: the reported case - the host (seat 0) loaded first, its copy built from the world record as the area's holder; the joiner
   (seat 1) runs the squad and announces its copy as running. The host's copy yields at once; the joiner never yields its running copy. */
void t1097g_reported_case_record_yields_to_running()
{
    using namespace sameid;
    const unsigned int host = 3600000u, joiner = 30000u;
    /* the premise the old order failed on: the load-age order alone names the joiner, not the host */
    if (YieldTurn(host, joiner, 0, 1) != 0) Fail("t1097g: the premise moved - the load-age order names the host's older load");
    const int people[3][2] = { { 4, 4 }, { 4, 0 }, { 2, 2 } };
    for (int ownId = 0; ownId < 2; ++ownId) for (int k = 0; k < 3; ++k)
        if (YieldGate(CopyYield(kCopyFromRecord, ownId, 0, 0, 0, people[k][0], people[k][1]), 1, 1, YieldOrder(kCopyFromRecord, kCopyLive, host, joiner, 0, 1)) != kVerdictYield)
            Fail("t1097g: the host's record-built copy does not yield to the joiner's running copy");
    /* and a save-born copy here yields to a running one the same way, whatever the ages */
    if (YieldGate(CopyYield(kCopyFromSave, 0, 0, 0, 0, 3, 3), 1, 1, YieldOrder(kCopyFromSave, kCopyLive, host, joiner, 0, 1)) != kVerdictYield) Fail("t1097g: a save copy here does not yield to a running one");
    /* the gates still hold: late, or the person's owner moved */
    if (YieldGate(CopyYield(kCopyFromRecord, 0, 0, 0, 0, 4, 4), 0, 1, YieldOrder(kCopyFromRecord, kCopyLive, host, joiner, 0, 1)) != kVerdictKeepLate
        || YieldGate(CopyYield(kCopyFromRecord, 0, 0, 0, 0, 4, 4), 1, 0, YieldOrder(kCopyFromRecord, kCopyLive, host, joiner, 0, 1)) != kVerdictKeepOwnerMoved)
        Fail("t1097g: a running announcement that no longer stands takes the copy down");
    /* the joiner, judging the host's announcement of any origin, never puts its running copy away */
    for (int ann = kCopyUnknown; ann <= kCopyFromSave; ++ann)
        if (T1097AnyPutsAway(kCopyLive, ann, joiner, host, 1, 0) != 0) Fail("t1097g: the joiner's running copy is put away");
}

/* T-1097: two copies of one kind (record and record, save and save, or mixed) - the load-age order: exactly one yields when it names
   one, both kept when it names none; never both put away */
void t1097g_same_kind_one_or_neither()
{
    using namespace sameid;
    const int kinds[2] = { kCopyFromRecord, kCopyFromSave };
    const unsigned int ages[5] = { 1000u, 4000u, 6000u, 3600000u, sqorigin::kLoadAgeUnknown };
    const int seats[3] = { -1, 0, 1 };
    for (int ka = 0; ka < 2; ++ka) for (int kb = 0; kb < 2; ++kb) for (int a = 0; a < 5; ++a) for (int b = 0; b < 5; ++b) for (int sa = 0; sa < 3; ++sa) for (int sb = 0; sb < 3; ++sb)
    {
        const int oa = kinds[ka], ob = kinds[kb];
        const int ya = YieldGate(CopyYield(oa, 0, 0, 0, 0, 2, 2), 1, 1, YieldOrder(oa, ob, ages[a], ages[b], seats[sa], seats[sb])) == kVerdictYield ? 1 : 0;
        const int yb = YieldGate(CopyYield(ob, 0, 0, 0, 0, 2, 2), 1, 1, YieldOrder(ob, oa, ages[b], ages[a], seats[sb], seats[sa])) == kVerdictYield ? 1 : 0;
        if (ya != 0 && yb != 0) { Fail("t1097g: both copies of one kind yield"); return; }
        /* a known order names one: the younger load by more than the tie, or within it the higher seat */
        const unsigned int gap = ages[a] > ages[b] ? ages[a] - ages[b] : ages[b] - ages[a];
        const int known = (ages[a] != sqorigin::kLoadAgeUnknown && ages[b] != sqorigin::kLoadAgeUnknown && seats[sa] >= 0 && seats[sb] >= 0
                           && (gap > sqorigin::kLoadAgeTieMs || seats[sa] != seats[sb])) ? 1 : 0;
        if (known != 0 && ya + yb != 1) { Fail("t1097g: a known load-age order does not name exactly one copy"); return; }
        if (known == 0 && ya + yb != 0) { Fail("t1097g: an unknown order puts a copy away"); return; }
    }
    /* the case named: record and record, the host's load older - the joiner's copy yields, the host's is kept (keepTurn) */
    if (YieldGate(CopyYield(kCopyFromRecord, 0, 0, 0, 0, 2, 2), 1, 1, YieldOrder(kCopyFromRecord, kCopyFromRecord, 3600000u, 30000u, 0, 1)) != kVerdictKeepTurn
        || YieldGate(CopyYield(kCopyFromRecord, 0, 0, 0, 0, 2, 2), 1, 1, YieldOrder(kCopyFromRecord, kCopyFromRecord, 30000u, 3600000u, 1, 0)) != kVerdictYield)
        Fail("t1097g: record against record does not take the load-age order");
}

/* T-1097: a running copy never yields, to any announcement, at any age */
void t1097g_running_never_yields()
{
    using namespace sameid;
    const unsigned int ages[4] = { 1000u, 30000u, 3600000u, sqorigin::kLoadAgeUnknown };
    for (int ann = kCopyUnknown; ann <= kCopyFromSave; ++ann) for (int a = 0; a < 4; ++a) for (int b = 0; b < 4; ++b) for (int s = 0; s < 2; ++s)
    {
        if (YieldOrder(kCopyLive, ann, ages[a], ages[b], s, 1 - s) != 0) { Fail("t1097g: the order names a running copy"); return; }
        if (T1097AnyPutsAway(kCopyLive, ann, ages[a], ages[b], s, 1 - s) != 0) { Fail("t1097g: a running copy is put away"); return; }
    }
}

/* the pair guard's partner, as store.cpp PairPartnerStands reads it: bound (1 this id, 0 not, -1 another) and engine id (1 / 0 / -1 unread) */
static int g_t1097gBound[2] = { 1, 1 }, g_t1097gEngine[2] = { 1, 1 };
static int g_t1097gSlots[2] = { 0, 0 };
static int T1097gStands(const void* p, const std::string&)
{
    const int i = (p == &g_t1097gSlots[0]) ? 0 : 1;
    return sameid::PartnerStands(g_t1097gBound[i], g_t1097gEngine[i]);
}
/* T-1097: the pair guard holds while a partner sleeps; it ends at the partner's forget or unbind. The reported sequence: two copies put
   to sleep 26 ms apart - both writes refused while both exist. */
void t1097g_pair_guard_holds_asleep()
{
    using namespace sameid;
    /* the standing table: asleep or awake is not an input */
    if (PartnerStands(1, -1) != 1 || PartnerStands(1, 0) != 1 || PartnerStands(-1, 1) != 0 || PartnerStands(0, 1) != 1 || PartnerStands(0, 0) != 0 || PartnerStands(0, -1) != 1)
        Fail("t1097g: the partner standing table");
    const void* pa = &g_t1097gSlots[0]; const void* pb = &g_t1097gSlots[1]; const void* partner = 0;
    PairBook book;
    g_t1097gBound[0] = g_t1097gBound[1] = 1; g_t1097gEngine[0] = g_t1097gEngine[1] = 1;
    /* both bound to the id (the record-built copy and the announcement's copy): the first to sleep is refused */
    if (book.Judge(pa, "Ninja_65538", pb, &partner, T1097gStands) != 1 || partner != pb) Fail("t1097g: the first sleep write of a pair is written");
    /* it is asleep now, still bound and not forgotten: the second sleep write is refused too */
    if (book.Judge(pb, "Ninja_65538", pa, &partner, T1097gStands) != 1 || partner != pa) Fail("t1097g: the second sleep write is written beside a sleeping partner");
    if (book.Judge(pb, "Ninja_65538", 0, &partner, T1097gStands) != 1) Fail("t1097g: the note of a sleeping partner refuses nothing");
    /* the sleeping partner is destroyed (its forget): the pair ends, the one left writes */
    book.Forget(pa);
    if (book.Judge(pb, "Ninja_65538", 0, &partner, T1097gStands) != 0 || book.Pairs() != 0) Fail("t1097g: the pair outlives its partner's forget");
    /* an unbound side noted by its engine id counts while it reads as this id; bound to another id it is dropped */
    book.Clear(); g_t1097gBound[1] = 0; g_t1097gEngine[1] = 1;
    if (book.Judge(pb, "Ninja_65538", pa, &partner, T1097gStands) != 1) Fail("t1097g: the unbound side beside a bound one writes");
    if (book.Judge(pa, "Ninja_65538", 0, &partner, T1097gStands) != 1 || partner != pb) Fail("t1097g: an unbound partner by its engine id does not count");
    g_t1097gEngine[1] = -1;
    if (book.Judge(pa, "Ninja_65538", 0, &partner, T1097gStands) != 1) Fail("t1097g: an unread engine id releases the guard");
    g_t1097gBound[1] = -1;
    if (book.Judge(pa, "Ninja_65538", 0, &partner, T1097gStands) != 0) Fail("t1097g: a partner bound to another id still refuses");
    g_t1097gBound[0] = g_t1097gBound[1] = 1; g_t1097gEngine[0] = g_t1097gEngine[1] = 1;
}
void t_townver_revision_rule()
{
    if (cooptv::TvAccept(0, 0, 1) != 1 || cooptv::TvAccept(0, 0, 2) != 0 || cooptv::TvAccept(0, 0, 0) != 0) Fail("with nothing stored only revision 1 is accepted");
    if (cooptv::TvAccept(1, 3, 4) != 1 || cooptv::TvAccept(1, 3, 3) != 0 || cooptv::TvAccept(1, 3, 5) != 0 || cooptv::TvAccept(1, 3, 1) != 0) Fail("only stored + 1 is accepted");
    if (cooptv::TvCacheTake(0, 0, cooptv::kTvStored, 1) != 1 || cooptv::TvCacheTake(1, 5, cooptv::kTvStored, 4) != 0 || cooptv::TvCacheTake(1, 5, cooptv::kTvStored, 5) != 1
        || cooptv::TvCacheTake(1, 5, cooptv::kTvStored, 6) != 1 || cooptv::TvCacheTake(1, 5, cooptv::kTvRefused, 1) != 1) Fail("the game's cache rule is wrong");
    cooptv::TownVer rec; rec.key = "k"; rec.ver = "X"; rec.fac = "F"; rec.rev = 3;
    if (cooptv::TvPublishRev(0, rec, "", "F") != 0 || cooptv::TvPublishRev(0, rec, "X", "F") != 1) Fail("with no record: the town's own record says nothing, a version is revision 1");
    if (cooptv::TvPublishRev(1, rec, "X", "F") != 0 || cooptv::TvPublishRev(1, rec, "Y", "F") != 4 || cooptv::TvPublishRev(1, rec, "X", "G") != 4 || cooptv::TvPublishRev(1, rec, "", "F") != 4)
        Fail("with a record: only a change is published, at stored + 1");
    /* two holders race on one town: both hold rev 1 and write rev 2; the first is stored, the second REFUSED and takes the first's */
    std::map<std::string, cooptv::TownVer> server, gameA, gameB;
    cooptv::TownVer s1 = rec; s1.rev = 1; server["k"] = s1; gameA["k"] = s1; gameB["k"] = s1;
    cooptv::TownVer wa = s1; wa.ver = "A"; wa.rev = cooptv::TvPublishRev(1, gameA["k"], "A", "F"); gameA["k"] = wa;
    cooptv::TownVer wb = s1; wb.ver = "B"; wb.rev = cooptv::TvPublishRev(1, gameB["k"], "B", "F"); gameB["k"] = wb;
    if (wa.rev != 2 || wb.rev != 2) Fail("both racing writes are revision 2");
    if (!cooptv::TvAccept(1, server["k"].rev, wa.rev)) Fail("the first write is refused"); else server["k"] = wa;
    if (cooptv::TvAccept(1, server["k"].rev, wb.rev)) Fail("the second write is accepted - the town would flip-flop");
    else if (cooptv::TvCacheTake(1, gameB["k"].rev, cooptv::kTvRefused, server["k"].rev)) gameB["k"] = server["k"];
    if (cooptv::TvCacheTake(1, gameA["k"].rev, cooptv::kTvStored, server["k"].rev)) gameA["k"] = server["k"];
    if (gameA["k"].ver != "A" || gameB["k"].ver != "A" || server["k"].ver != "A" || server["k"].rev != 2) Fail("the race does not converge on the first write");
    /* a world saved before the record existed: only the holder seeds, only with the whole list here and nothing stored */
    if (cooptv::TvSeedDue(1, 0, 0, 1, 1) != 1) Fail("the holder's save version with no record is not seeded");
    if (cooptv::TvSeedDue(0, 0, 0, 1, 1) || cooptv::TvSeedDue(1, 1, 0, 1, 1) || cooptv::TvSeedDue(1, 0, 1, 1, 1) || cooptv::TvSeedDue(1, 0, 0, 0, 1)
        || cooptv::TvSeedDue(1, 0, 0, 1, 0) || cooptv::TvSeedDue(1, 0, 0, 1, -1)) Fail("a save version is seeded before the list, over a record, after a refusal, with no version or off the holder");
    /* every game on the chooser's version path queues its save's version (the caller passes 1); only the runner publishes it (TvSeedDue above) */
    if (cooptv::TvSeedQueue(1, 0, 1) != 1 || cooptv::TvSeedQueue(0, 0, 1) != 0 || cooptv::TvSeedQueue(1, 1, 1) != 0 || cooptv::TvSeedQueue(1, 0, 0) != 0)
        Fail("a save version is queued for the seed off the version path, over a record or with no version");
    /* this game's own write stays pending until the world server is seen to hold it */
    if (cooptv::TvMineKeep(cooptv::kTvStored, 4, 4) != 0 || cooptv::TvMineKeep(cooptv::kTvStored, 5, 4) != 0 || cooptv::TvMineKeep(cooptv::kTvRefused, 2, 4) != 0
        || cooptv::TvMineKeep(cooptv::kTvStored, 3, 4) != 1) Fail("the pending-write rule is wrong");
    /* a refusal ends the pending write unless it holds exactly the revision below it - then it answers an earlier write */
    if (cooptv::TvMineKeep(cooptv::kTvRefused, 3, 4) != 1) Fail("a late refusal of an earlier write ends the pending one");
    if (cooptv::TvMineKeep(cooptv::kTvRefused, 4, 4) != 0 || cooptv::TvMineKeep(cooptv::kTvRefused, 5, 4) != 0 || cooptv::TvMineKeep(cooptv::kTvRefused, 0, 1) != 0)
        Fail("a refusal of the pending write keeps it pending");
    {   /* rev 2 was refused (another game's rev 2 is stored); this game then writes rev 3, which is accepted - the first refusal arriving late must not end it */
        if (!cooptv::TvAccept(1, 2, 3) || cooptv::TvMineKeep(cooptv::kTvRefused, 2, 3) != 1) Fail("the late refusal of rev 2 ends the pending rev 3");
    }
    {   /* the link drops after rev 2 was handed over: the next list carries rev 1, the cache keeps 2, the write stays pending and goes again */
        cooptv::TownVer s = rec; s.rev = 1; cooptv::TownVer mine = s; mine.ver = "Y"; mine.rev = 2;
        if (cooptv::TvCacheTake(1, mine.rev, cooptv::kTvStored, s.rev) != 0) Fail("the lost write's own cache row is overwritten by the older list");
        if (cooptv::TvMineKeep(cooptv::kTvStored, s.rev, mine.rev) != 1) Fail("a lost write is not kept pending");
        if (!cooptv::TvAccept(1, s.rev, mine.rev)) Fail("the write sent again is refused by the world server");
    }
}
void t_townver_key_and_gates()
{
    if (cooptv::TvKey("1079-gamedata.base", -62461.0f, 16346.8f) != "1079-gamedata.base@-62461,16347") Fail("Squin's key is not the readout's key");
    if (cooptv::TvKey("x", -0.5f, 0.49f) != "x@0,0" || cooptv::TvKey("x", -0.51f, 1.5f) != "x@-1,2") Fail("the key does not round to whole metres the readout's way");
    if (cooptv::TvKey("40631-Newwworld.mod", 100.0f, 200.0f) == cooptv::TvKey("40631-Newwworld.mod", 300.0f, 200.0f)) Fail("two placements of one town record share a key");
    /* TvChooseMode(onMain, hooked, readOk, playerOwned, standIn, inWorld, holder) */
    if (cooptv::TvChooseMode(1, 1, 1, 0, 0, 1, 1) != cooptv::kTvDecide) Fail("the holder does not decide");
    if (cooptv::TvChooseMode(1, 1, 1, 0, 0, 1, 0) != cooptv::kTvRefuse) Fail("a game that does not run the area is not refused a switch");
    if (cooptv::TvChooseMode(1, 1, 1, 0, 1, 1, 1) != cooptv::kTvStandIn) Fail("another player's town is switched");
    if (cooptv::TvChooseMode(0, 1, 1, 0, 0, 1, 1) != cooptv::kTvEngineAlone || cooptv::TvChooseMode(1, 0, 1, 0, 0, 1, 1) != cooptv::kTvEngineAlone
        || cooptv::TvChooseMode(1, 1, 0, 0, 0, 1, 1) != cooptv::kTvEngineAlone || cooptv::TvChooseMode(1, 1, 1, 1, 0, 1, 1) != cooptv::kTvEngineAlone
        || cooptv::TvChooseMode(1, 1, 1, 0, 0, 0, 1) != cooptv::kTvEngineAlone) Fail("off the main thread, unhooked, unreadable, player-owned or in no world is not the engine alone");
    /* TvNoteRebuild(hasNote, noteVer, verNow): rebuilt only when the area file's note names another version than the town has now */
    if (cooptv::TvNoteRebuild(1, "", "D") != 1 || cooptv::TvNoteRebuild(1, "D", "") != 1 || cooptv::TvNoteRebuild(1, "D", "E") != 1) Fail("an area file built with another version is not rebuilt");
    if (cooptv::TvNoteRebuild(1, "D", "D") != 0 || cooptv::TvNoteRebuild(1, "", "") != 0) Fail("an area file built with the town's version is rebuilt (an owner-only difference must not rebuild)");
    if (cooptv::TvNoteRebuild(0, "", "D") != 0 || cooptv::TvNoteRebuild(0, "D", "D") != 0) Fail("an area file with no note for the town is rebuilt (old files, off-thread writes and base files keep their buildings)");
    /* TvAsBuilt: what a town's buildings in the loaded area are built with after the chooser - that area's next note */
    if (cooptv::TvAsBuilt(1, 1, "D", "E") != "E" || cooptv::TvAsBuilt(0, 1, "D", "E") != "D" || cooptv::TvAsBuilt(0, 0, "", "E") != "E") Fail("the as-built version is wrong");
    if (cooptv::TvAreaIndex(20, 32) != 20 * 64 + 32 || cooptv::TvAreaIndex(-1, 0) != -1 || cooptv::TvAreaIndex(0, 64) != -1) Fail("the area index is wrong");
    {   /* THE NOTE BOOK: notes per (area loaded, town) */
        typedef std::vector<std::pair<std::string, std::string> > Rows;
        cooptv::TvNoteBook b; Rows none, out; std::string v; int has = 0;
        const int a2032 = cooptv::TvAreaIndex(20, 32), a2033 = cooptv::TvAreaIndex(20, 33);
        /* T1653's case: town T placed in 20.32 is also checked when 20.33 loads; 20.32's file notes T = D, 20.33's file notes no T */
        Rows d; d.push_back(std::make_pair(std::string("T"), std::string("D")));
        b.Read(a2032, 1, cooptv::kTvNoteFile, d);
        if (!b.Take("T", &has, &v) || !has || v != "D") Fail("the loaded area's note for its own town is not taken");
        if (b.Take("T", &has, &v)) Fail("a note is taken twice in one load");
        b.Built("T", 1, 1, "D", "E");   /* rebuilt to E in 20.32 */
        b.Read(a2033, 1, cooptv::kTvNoteFile, none);
        if (!b.Take("T", &has, &v) || has) Fail("20.33 is given 20.32's note for T (its rebuild would follow another area's file)");
        b.Built("T", 0, has, v, "E");
        b.For(a2032, &out); if (out.size() != 1 || out[0].first != "T" || out[0].second != "E") Fail("20.32's note is overwritten from 20.33");
        b.For(a2033, &out); if (out.size() != 1 || out[0].first != "T" || out[0].second != "E") Fail("20.33's note does not list the town checked from it");
        /* 20.33 never saved: its base files read, then no file of its own - every town checked from it gets "" and is rebuilt once */
        b.Read(a2033, 1, cooptv::kTvNoteBase, none); b.Read(a2033, 0, cooptv::kTvNoteNone, none);
        if (!b.Take("T", &has, &v) || !has || v != "" || cooptv::TvNoteRebuild(has, v, "E") != 1) Fail("a town checked from a base-only area is not rebuilt to its version");
        b.Built("T", 1, has, v, "E");
        b.For(a2033, &out); b.Read(a2033, 1, cooptv::kTvNoteFile, out);   /* the next visit reads what that write noted */
        if (!b.Take("T", &has, &v) || cooptv::TvNoteRebuild(has, v, "E") != 0) Fail("an area rebuilt with the version is rebuilt again");
        /* a save's load pass: noted D, now E, nothing rebuilt -> the write keeps D, the next visit rebuilds */
        b.Read(a2032, 1, cooptv::kTvNoteFile, d);
        b.Take("T", &has, &v); b.Built("T", 0, has, v, "E");
        b.For(a2032, &out); b.Read(a2032, 1, cooptv::kTvNoteFile, out);
        if (!b.Take("T", &has, &v) || cooptv::TvNoteRebuild(has, v, "E") != 1) Fail("a version put on at the load pass is never rebuilt");
        /* the file's own entries ride on for towns not checked this visit */
        Rows two; two.push_back(std::make_pair(std::string("T"), std::string("D"))); two.push_back(std::make_pair(std::string("U"), std::string("F")));
        b.Read(a2032, 1, cooptv::kTvNoteFile, two); b.Take("T", &has, &v); b.Built("T", 1, has, v, "E");
        b.For(a2032, &out); if (out.size() != 2 || out[0].second != "E" || out[1].first != "U" || out[1].second != "F") Fail("the write does not carry the file's other entries");
        /* a read off the main thread: the area is forgotten - nothing taken, nothing written for it */
        b.Forget(a2032);
        if (b.Take("T", &has, &v)) Fail("a note is taken from a forgotten area");
        b.For(a2032, &out); if (!out.empty()) Fail("a forgotten area's write carries a note");
        /* a copy with no note: kept, and noted with the version now */
        b.Read(a2032, 1, cooptv::kTvNoteNone, none);
        if (!b.Take("T", &has, &v) || has || cooptv::TvNoteRebuild(has, v, "E") != 0) Fail("a copy with no note gives a note");
        b.Built("T", 0, has, v, "E"); b.For(a2032, &out); if (out.size() != 1 || out[0].second != "E") Fail("a kept town with no note is not noted with the version now");
    }
    {   /* a world load / teardown begins BEFORE the engine's teardown writes its areas: the book serves those writes, the new world's first read empties it */
        typedef std::vector<std::pair<std::string, std::string> > Rows;
        cooptv::TvNoteBook b; Rows none, out; std::string v; int has = 0;
        const int a = cooptv::TvAreaIndex(20, 32), c = cooptv::TvAreaIndex(5, 5);
        Rows d; d.push_back(std::make_pair(std::string("T"), std::string("D")));
        b.Read(a, 1, cooptv::kTvNoteFile, d); b.Take("T", &has, &v); b.Built("T", 0, has, v, "E");   /* a load pass: still built with D */
        b.WorldEnded();
        b.For(a, &out); if (out.size() != 1 || out[0].first != "T" || out[0].second != "D") Fail("a teardown write loses the area's note (the owed rebuild is lost)");
        if (b.Take("T", &has, &v)) Fail("a note is taken after the world ended, before any read of the new world");
        b.Read(c, 1, cooptv::kTvNoteNone, none);
        b.For(a, &out); if (!out.empty()) Fail("the last world's notes survive the new world's first read");
        if (!b.Take("T", &has, &v) || has) Fail("the new world's first read is not followed");
    }
    {   /* the teardown call returns with no read of the next world: the book is emptied, so the next world's first write carries nothing */
        typedef std::vector<std::pair<std::string, std::string> > Rows;
        cooptv::TvNoteBook b; Rows out; std::string v; int has = 0;
        const int a = cooptv::TvAreaIndex(20, 32);
        Rows d; d.push_back(std::make_pair(std::string("T"), std::string("D")));
        b.Read(a, 1, cooptv::kTvNoteFile, d); b.Take("T", &has, &v); b.Built("T", 0, has, v, "E");
        b.WorldEnded(); b.TeardownDone();
        b.For(a, &out); if (!out.empty()) Fail("a write after the teardown returned carries the last world's note");
        /* a load: the new world's first read comes inside the same call - TeardownDone keeps the new world's notes */
        b.Read(a, 1, cooptv::kTvNoteFile, d); b.WorldEnded();
        Rows e; e.push_back(std::make_pair(std::string("U"), std::string("F")));
        b.Read(a, 1, cooptv::kTvNoteFile, e); b.TeardownDone();
        if (!b.Take("U", &has, &v) || !has || v != "F") Fail("TeardownDone after the new world's first read drops its notes");
    }
    /* the note's fields */
    {
        std::string v = "x";
        if (cooptv::TvNoteVerField("") != "=" || cooptv::TvNoteVerField("55651-rebirth.mod") != "=55651-rebirth.mod") Fail("a version field is not '=' + the version");
        if (!cooptv::TvNoteVerParse("=", &v) || v != "" || !cooptv::TvNoteVerParse(cooptv::TvNoteVerField("12-x.mod"), &v) || v != "12-x.mod") Fail("a version field does not read back");
        if (cooptv::TvNoteVerParse("", &v) || cooptv::TvNoteVerParse("12-x.mod", &v)) Fail("a field with no '=' is read as a version");
        if (cooptv::TvNoteCountParse("0") != 0 || cooptv::TvNoteCountParse("12") != 12 || cooptv::TvNoteCountParse("999") != cooptv::kTvNoteCap) Fail("the town count is not read (capped)");
        if (cooptv::TvNoteCountParse("") != -1 || cooptv::TvNoteCountParse("-1") != -1 || cooptv::TvNoteCountParse("1a") != -1 || cooptv::TvNoteCountParse("1234567") != -1) Fail("a malformed town count is read");
        if (std::string(cooptv::kTvNoteId) != "sw-town-builtwith") Fail("the note's record id changed (old area files would lose their notes)");
    }
    /* an area whose last file read is an engine base file: every town gets a note of "" (its base version) */
    {
        if (cooptv::TvIsBaseAreaPath("./data/newland/leveldata/Newwworld/zone.20.32.zone") != 1 || cooptv::TvIsBaseAreaPath(".\\DATA\\Newland\\LevelData\\rebirth\\zone.20.32.zone") != 1)
            Fail("an engine base area file is not recognised");
        if (cooptv::TvIsBaseAreaPath("C:\\code\\kenshi\\save\\mp - Profile/zone/zone.20.32.zone") != 0 || cooptv::TvIsBaseAreaPath("") != 0) Fail("a save's area file is taken for a base file");
        if (cooptv::TvNoteSource(1, 0) != cooptv::kTvNoteBase || cooptv::TvNoteSource(1, 1) != cooptv::kTvNoteBase || cooptv::TvNoteSource(0, 1) != cooptv::kTvNoteFile
            || cooptv::TvNoteSource(0, 0) != cooptv::kTvNoteNone) Fail("the source of a read's notes is wrong");
        std::string v = "x";
        const int baseNote = cooptv::TvNoteFor(cooptv::kTvNoteBase, 0, "D", &v);
        if (!baseNote || v != "" || cooptv::TvNoteRebuild(baseNote, v, "D") != 1) Fail("base files only, the town's version differs: not rebuilt");
        if (cooptv::TvNoteRebuild(baseNote, v, "") != 0) Fail("base files only, the town at its base version: rebuilt");
        v = "x";
        const int copyNone = cooptv::TvNoteFor(cooptv::kTvNoteNone, 0, "", &v);
        if (copyNone || v != "x" || cooptv::TvNoteRebuild(copyNone, v, "D") != 0) Fail("a save / world-server copy with no note: rebuilt");
        const int copyMissing = cooptv::TvNoteFor(cooptv::kTvNoteFile, 0, "", &v);
        if (copyMissing || cooptv::TvNoteRebuild(copyMissing, v, "D") != 0) Fail("a town missing from a copy's note: rebuilt");
        if (!cooptv::TvNoteFor(cooptv::kTvNoteFile, 1, "D", &v) || v != "D") Fail("a copy's note entry is not given to its town");
    }
}
/* a reference of the model: NPC terms answer `holds`, a standing term answers `st` (the LOCAL player's standing), expected `e` */
cooptv::TvModelRef TownVerNpc(int holds) { cooptv::TvModelRef r; r.npcTerms = 1; r.standingTerms = 0; r.npcAnswer = holds; r.standingAnswer = 1; r.expected = 1; return r; }
cooptv::TvModelRef TownVerStanding(int st, int expected) { cooptv::TvModelRef r; r.npcTerms = 0; r.standingTerms = 1; r.npcAnswer = 1; r.standingAnswer = st; r.expected = expected; return r; }
void t_townver_ignore_standings()
{
    if (cooptv::TvIgnoreAction(1, 0) != cooptv::kTvEngine || cooptv::TvIgnoreAction(0, 0) != cooptv::kTvEngine || cooptv::TvIgnoreAction(0, 1) != cooptv::kTvExpected || cooptv::TvIgnoreAction(2, 1) != cooptv::kTvStripped)
        Fail("the per-world-state 260 action is wrong");
    { cooptv::TvModelRef m; m.npcTerms = 1; m.standingTerms = 1; m.npcAnswer = 1; m.standingAnswer = 0; m.expected = 1;
      if (!cooptv::TvModelRefMet(m, 1) || cooptv::TvModelRefMet(m, 0)) Fail("a mixed world state is decided by its NPC terms alone under 260"); }
    /* FORT SIMION: [0] Fort Yabuta = Yabuta of the Sands ALIVE & NOT player ally of Rebel Farmers; [1] destroyed = Emperor Tengu not ALIVE */
    for (int ally = 0; ally <= 1; ++ally)
    {
        for (int yabuta = 0; yabuta <= 1; ++yabuta) for (int tengu = 0; tengu <= 1; ++tengu)
        {
            std::vector<std::vector<cooptv::TvModelRef> > c(2);
            c[0].push_back(TownVerNpc(yabuta)); c[0].push_back(TownVerStanding(ally, 0));
            c[1].push_back(TownVerNpc(!tengu));
            const int want = yabuta ? 0 : (!tengu ? 1 : -1);
            if (cooptv::TvModelChoose(c, -1, 1) != want) Fail("Fort Simion under 260: yabuta=" + I(yabuta) + " tengu=" + I(tengu) + " ally=" + I(ally) + " chose " + I(cooptv::TvModelChoose(c, -1, 1)) + ", want " + I(want));
            if (ally && yabuta && tengu && cooptv::TvModelChoose(c, -1, 0) != -1) Fail("the engine alone gives an ally of Rebel Farmers the normal Fort Simion (the divergence 260 removes)");
        }
    }
    /* OKRAN'S SHIELD: [0] empire = Valtena fallen (pi) & Tengu ALIVE (w2); [1] destroyed = Valtena fallen (pi) & Tengu dead (w2);
       [2] reavers = Tsugi fallen (pi) & Valamon ALIVE & Valtena fallen (pi) & Tengu dead (w4); [3] shek = Esata ALIVE & player ally of Shek & Valtena fallen (pi) (w3) */
    for (int ally = 0; ally <= 1; ++ally)
    {
        for (int bits = 0; bits < 32; ++bits)
        {
            const int valtenaFallenPi = bits & 1, esata = (bits >> 1) & 1, tengu = (bits >> 2) & 1, tsugiFallenPi = (bits >> 3) & 1, valamon = (bits >> 4) & 1;
            std::vector<std::vector<cooptv::TvModelRef> > c(4);
            c[0].push_back(TownVerNpc(valtenaFallenPi)); c[0].push_back(TownVerNpc(tengu));
            c[1].push_back(TownVerNpc(valtenaFallenPi)); c[1].push_back(TownVerNpc(!tengu));
            c[2].push_back(TownVerNpc(tsugiFallenPi)); c[2].push_back(TownVerNpc(valamon)); c[2].push_back(TownVerNpc(valtenaFallenPi)); c[2].push_back(TownVerNpc(!tengu));
            c[3].push_back(TownVerNpc(esata)); c[3].push_back(TownVerStanding(ally, 1)); c[3].push_back(TownVerNpc(valtenaFallenPi));
            int want = -1;
            if (valtenaFallenPi)
            {
                if (!tengu && tsugiFallenPi && valamon) want = 2;
                else if (esata) want = 3;
                else want = tengu ? 0 : 1;
            }
            if (cooptv::TvModelChoose(c, -1, 1) != want) Fail("Okran's Shield under 260: bits=" + I(bits) + " ally=" + I(ally) + " chose " + I(cooptv::TvModelChoose(c, -1, 1)) + ", want " + I(want));
        }
    }
    /* the chooser's own rules: the current version is skipped, a version with no reference never applies, ties go to the earliest */
    std::vector<std::vector<cooptv::TvModelRef> > c(3);
    c[1].push_back(TownVerNpc(1)); c[2].push_back(TownVerNpc(1));
    if (cooptv::TvModelChoose(c, -1, 1) != 1 || cooptv::TvModelChoose(c, 1, 1) != 2) Fail("the model's tie / current / empty rules are wrong");
}

/* T-1118 (a): the save window around the engine's save calls (src/common/linkkeep.h) - saveLevelData nests inside saveGame,
   one window per outermost call, its kind handed back once; the totals and the log cap. */
void t_t1118_link_keep_window()
{
    linkkeep::Window w;
    if (linkkeep::Exit(&w) != -1 || w.depth != 0) Fail("an exit with no open window closed something (depth " + I(w.depth) + ")");
    if (!linkkeep::Enter(&w)) Fail("the first entry is not the outermost");
    linkkeep::SetKind(&w, linkkeep::kKept);
    if (linkkeep::Enter(&w)) Fail("a nested entry (an area write inside the game save) opened a second window");
    if (w.kind != linkkeep::kKept) Fail("a nested entry changed the window's kind to " + I(w.kind));
    if (linkkeep::Exit(&w) != -1) Fail("the inner exit closed the window (depth now " + I(w.depth) + ")");
    const int k = linkkeep::Exit(&w);
    if (k != linkkeep::kKept) Fail("the outer exit handed back kind " + I(k) + ", want kKept");
    if (w.depth != 0 || w.kind != linkkeep::kOff) Fail("a closed window keeps depth " + I(w.depth) + " kind " + I(w.kind));
    if (linkkeep::Exit(&w) != -1 || w.depth != 0) Fail("an extra exit after the close counted (depth " + I(w.depth) + ")");
    if (!linkkeep::Enter(&w) || w.kind != linkkeep::kOff) Fail("a new window does not start off");
    if (linkkeep::Exit(&w) != linkkeep::kOff) Fail("a window whose kind was never set is not off");
    if (linkkeep::Decide(0, 0) != linkkeep::kOff || linkkeep::Decide(0, 1) != linkkeep::kOff) Fail("a window with no link up is not off");
    if (linkkeep::Decide(1, 0) != linkkeep::kKept) Fail("an up link PumpLink would poll is not kept");
    if (linkkeep::Decide(1, 1) != linkkeep::kHeld) Fail("an up link PumpLink holds is not left held");
    if (linkkeep::Refused(1) != linkkeep::kNoHelper) Fail("a direct link whose helper did not start is not counted noHelper");
    if (linkkeep::Refused(0) != linkkeep::kNoKeeper) Fail("a link with no helper (Steam) is not counted noKeeper");
    linkkeep::Totals t;
    linkkeep::Pass p; p.services = 40; p.sentBytes = 900000; p.ackedPackets = 700; p.lockWaitMaxUs = 120;
    linkkeep::Pass q; q.services = 3; q.sentBytes = 10; q.ackedPackets = 1; q.lockWaitMaxUs = 50;
    if (linkkeep::Add(&t, linkkeep::kOff, 500, p) || t.stalls != 0 || t.maxStallMs != 0) Fail("a window with no link was counted");
    if (linkkeep::Add(&t, 7, 500, p) || t.stalls != 0) Fail("a window of an unknown kind was counted");
    if (!linkkeep::Add(&t, linkkeep::kKept, 290, p) || !linkkeep::Add(&t, linkkeep::kKept, 120, q)) Fail("a kept window was not counted");
    if (!linkkeep::Add(&t, linkkeep::kHeld, 310, p) || !linkkeep::Add(&t, linkkeep::kNoKeeper, 80, p)) Fail("a held or no-keeper window was not counted");
    if (!linkkeep::Add(&t, linkkeep::kNoHelper, 60, p)) Fail("a window whose helper did not start was not counted");
    if (t.stalls != 5 || t.kept != 2 || t.held != 1 || t.noKeeper != 1 || t.noHelper != 1)
        Fail("windows counted stalls=" + I((int)t.stalls) + " kept=" + I((int)t.kept) + " held=" + I((int)t.held) + " noKeeper=" + I((int)t.noKeeper)
             + " noHelper=" + I((int)t.noHelper) + ", want 5 2 1 1 1");
    if (t.services != 43 || t.sentBytes != 900010 || t.ackedPackets != 701)
        Fail("only kept windows add helper numbers: services=" + I((int)t.services) + " sentBytes=" + I((int)t.sentBytes) + " acked=" + I((int)t.ackedPackets) + ", want 43 900010 701");
    if (t.lockWaitMaxUs != 120 || t.maxStallMs != 310)
        Fail("maxima lockWaitMaxUs=" + I((int)t.lockWaitMaxUs) + " maxStallMs=" + I((int)t.maxStallMs) + ", want 120 310 (a held window's stall counts, its pass does not)");
    const int logged[] = { 1, 2, 3, 4, 5, 100, 200 };
    const int quiet[] = { 0, 6, 7, 99, 101, 199 };
    for (int i = 0; i < 7; ++i) if (!linkkeep::LogThis(logged[i])) Fail("window " + I(logged[i]) + " is not logged");
    for (int j = 0; j < 6; ++j) if (linkkeep::LogThis(quiet[j])) Fail("window " + I(quiet[j]) + " is logged");
}

/* T-942: each class from the owner's action table, and the site answers */
void t_t942_thing_classes()
{
    using namespace swaccess;
    if (Classify(true, false, false, false, kPolicyShared) != kOwn) Fail("this game's own faction is own");
    if (Classify(false, false, false, true, kPolicyShared) != kNotPlayer) Fail("a faction no player's (unknown) is not a player's, whatever its standing");
    if (Classify(false, true, true, false, kPolicyLocked) != kTeam || Classify(false, true, true, true, kPolicyOwner) != kTeam) Fail("a teammate whatever the option");
    if (Classify(false, true, false, true, kPolicyShared) != kFriendAllowed) Fail("a friend under shared is allowed");
    if (Classify(false, true, false, true, kPolicyOwner) != kFriendRefused || Classify(false, true, false, true, kPolicyLocked) != kFriendRefused
        || Classify(false, true, false, true, kPolicyUnknown) != kFriendRefused) Fail("a friend under owner / locked / an option not heard yet is refused");
    /* the friend test: the thing owner's own side towards this player, as its players tab sets it (ALLY = 100) or the ally flag */
    if (!TabAlly(1, 100.0f, false) || !TabAlly(1, 99.6f, false) || !TabAlly(1, 0.0f, true)) Fail("the players tab's ally, or the ally flag, is a friend");
    if (TabAlly(1, 60.0f, false) || TabAlly(1, 99.0f, false)) Fail("a standing of 50 or more that the players tab did not set is no friend");
    if (TabAlly(0, 100.0f, true) || TabAlly(-1, 100.0f, false)) Fail("a stance that was not read is no friend (a stranger)");
    if (Classify(false, true, false, false, kPolicyShared) != kStranger) Fail("a player who is neither is a stranger");
    if (!LikeOwn(kOwn) || !LikeOwn(kTeam) || !LikeOwn(kFriendAllowed) || LikeOwn(kFriendRefused) || LikeOwn(kStranger) || LikeOwn(kNotPlayer)) Fail("used as own");
}
/* owner 1188 / 1189 a / 1192 a, class x option, through the sites' own functions: the stealing decision's answer for a move between
   this player's character and the owner's box or character (ClearsStealing(PairClass)), and the window for the owner's character
   awake / down (WindowFor; caller's choice 3 = the engine's own) */
void t_t942_thing_table()
{
    using namespace swaccess;
    const int pols[4] = { kPolicyShared, kPolicyOwner, kPolicyLocked, kPolicyUnknown };
    for (int i = 0; i < 4; ++i)
    {
        const int p = pols[i];
        const bool shared = (p == kPolicyShared);
        const int team = Classify(false, true, true, false, p), fr = Classify(false, true, false, true, p), st = Classify(false, true, false, false, p);
        const int own = Classify(true, false, false, false, p), npc = Classify(false, false, false, true, p);
        if (team != kTeam || fr != (shared ? kFriendAllowed : kFriendRefused) || st != kStranger || own != kOwn || npc != kNotPlayer) Fail("classes, option " + I(p));
        /* no stealing: a teammate always, a friend under shared only; a stranger always stealing; own and NPC things: the engine's */
        if (!ClearsStealing(PairClass(kOwn, team)) || ClearsStealing(PairClass(kOwn, fr)) != shared || ClearsStealing(PairClass(kOwn, st))
            || ClearsStealing(PairClass(kOwn, own)) || ClearsStealing(PairClass(kOwn, npc))) Fail("the stealing answer, option " + I(p));
        /* down or carried: a loot window for every other player (stealing per the line above); never a shop window */
        if (WindowFor(team, true, true, 3) != kTradeNever || WindowFor(fr, true, true, 1) != kTradeNever || WindowFor(st, true, true, 3) != kTradeNever)
            Fail("a down character opens a loot window, option " + I(p));
        /* awake: a teammate / allowed friend as this player's own; a stranger / refused friend no window at all */
        if (WindowFor(team, true, false, 1) != kTradeNever || WindowFor(fr, true, false, 3) != (shared ? kTradeNever : kNoWindow)
            || WindowFor(st, true, false, 3) != kNoWindow) Fail("an awake character, option " + I(p));
        /* a box of another player: never a shop window; own and NPC things keep the caller's choice */
        if (WindowFor(st, false, false, 1) != kTradeNever || WindowFor(own, true, false, 1) != 1 || WindowFor(npc, true, false, 3) != 3)
            Fail("boxes and every other owner, option " + I(p));
    }
}
void t_t942_thing_sites()
{
    using namespace swaccess;
    if (PairClass(kOwn, kTeam) != kTeam || PairClass(kTeam, kOwn) != kTeam) Fail("own character and a teammate's box: the teammate's");
    if (PairClass(kOwn, kStranger) != kStranger || PairClass(kTeam, kFriendRefused) != kFriendRefused) Fail("the side least own decides");
    if (PairClass(kOwn, kNotPlayer) != kNotPlayer || PairClass(kNotPlayer, kTeam) != kNotPlayer) Fail("a side no player's leaves the move to the game");
    if (PairClass(kOwn, kOwn) != kOwn) Fail("own with own");
    if (!ClearsStealing(kTeam) || !ClearsStealing(kFriendAllowed) || ClearsStealing(kFriendRefused) || ClearsStealing(kStranger)
        || ClearsStealing(kOwn) || ClearsStealing(kNotPlayer)) Fail("only a teammate's or an allowed friend's stealing answer is cleared");
    if (WindowFor(kFriendAllowed, true, false, 1) != kTradeNever || WindowFor(kFriendRefused, true, false, 1) != kNoWindow) Fail("an awake friend's character");
    if (WindowFor(kNotPlayer, true, false, 1) != 1 || WindowFor(kOwn, true, true, 3) != 3) Fail("every other owner keeps the caller's choice");
    if (CounterOf(kTeam) != 0 || CounterOf(kFriendAllowed) != 1 || CounterOf(kFriendRefused) != 2 || CounterOf(kStranger) != 3
        || CounterOf(kOwn) != -1 || CounterOf(kNotPlayer) != -1) Fail("the counters");
    int a = 1, b = 2, c = 3;
    const void* facs[4] = { &a, &b, &c, 0 };
    const int who[4] = { kTeam, kFriendRefused, kFriendAllowed, kStranger };
    if (ClassIn(facs, who, 3, &b) != kFriendRefused || ClassIn(facs, who, 3, (const void*)0) != kNotPlayer || ClassIn(facs, who, 2, &c) != kNotPlayer)
        Fail("the table lookup");
    const void* out[4]; int drops = -1;
    const int n = LikeOwnList(facs, who, 4, out, 4, &drops);
    if (n != 2 || out[0] != &a || out[1] != &c || drops != 0) Fail("the stub cells hold teammates and allowed friends only");
    if (LikeOwnList(facs, who, 4, out, 1, &drops) != 1 || drops != 1) Fail("the cells past the cap are counted");
}
/* T-942 / owner 1191: the wording and the 5 s grouping */
void t_t942_item_notice()
{
    using namespace itemnotice;
    std::vector<Group> open;
    if (Add(&open, 2, "Sam", 77, "Hob", kTook, "Crossbow", 1, 1000) != 1 || Add(&open, 2, "Sam", 77, "Hob", kTook, "Bolts", 1, 2000) != 1
        || Add(&open, 2, "Sam", 77, "Hob", kTook, "Bolts", 1, 3000) != 1) Fail("adds");
    Add(&open, 2, "Sam", 77, "Hob", kGave, "Dried Meat", 3, 3500);   /* another direction: its own line */
    Add(&open, 4, "Ann", 77, "Hob", kTook, "Bread", 1, 3600);         /* another player: its own line */
    Add(&open, 2, "Sam", 78, "Kit", kTook, "Bread", 1, 3700);         /* another character: its own line */
    if (open.size() != 4) Fail("one line per player, character and direction: " + I((int)open.size()));
    if (!Due(&open, 5999, false).empty()) Fail("nothing is due before 5 s");
    std::vector<Group> d = Due(&open, 6000, false);
    if (d.size() != 1 || Sentence(d[0]) != "Sam took Crossbow, 2 x Bolts from Hob.") Fail("the take line: " + (d.empty() ? std::string("none") : Sentence(d[0])));
    d = Due(&open, 8500, false);
    if (d.size() != 1 || Sentence(d[0]) != "Sam gave 3 x Dried Meat to Hob.") Fail("the give line");
    Add(&open, 2, "Sam", 77, "Hob", kTook, "Rag", 1, 6100);   /* after the first line's 5 s: a new line */
    d = Due(&open, 0, true);
    if (d.size() != 3 || Sentence(d[2]) != "Sam took Rag from Hob." || !open.empty()) Fail("all due at the end, in order");
    if (Add(&open, 2, "Sam", 77, "Hob", kTook, "", 1, 0) != 0 || Add(&open, 2, "Sam", 0, "Hob", kTook, "Rag", 1, 0) != 0) Fail("no item or no character: no line");
    Group g; g.slot = 1; g.charUid = 5; g.dir = kGave; g.firstMs = 0; g.more = false;
    Item it; it.name = "Rag"; it.units = 1; g.items.push_back(it);
    if (Sentence(g) != "A player gave Rag to your character.") Fail("names not known");
    if (!Wanted(1, 77, 0, true, true, true) || Wanted(1, 0, 0, true, true, true) || Wanted(1, 77, 1, true, true, true) || Wanted(1, 77, 0, false, true, true)
        || Wanted(1, 77, 0, true, false, true) || Wanted(0, 77, 0, true, true, true)) Fail("only a served non-trade move on this player's character by another player");
    if (Wanted(1, 77, 0, true, true, false)) Fail("a taker that is not that player's own character (an NPC its game runs): no line");
}

/* T-943: the site table - 26 sites, each one's compare form (site 24: a call, kCallPlayer) and the bytes it must hold; every compare is `cmp qword [rax+0x250], x`
   with x = 0 or a register (the stub's answer assumes ZF=1 = no PlayerInterface) */
void t_t943_npc_sites()
{
    using namespace swnpc;
    if (kSites != 26 || kSenseSites != 8) Fail("26 sites, the senses first, then the memory, then sites 23 - 26");
    for (int k = 0; k < kSites; ++k)
    {
        const Site& s = SiteAt(k);
        for (int j = 0; j < k; ++j) if (std::string(SiteAt(j).row) == s.row) Fail("row names are unique: " + std::string(s.row));
        const bool sense = k < kSenseSites || k >= 22;   /* sites 23 - 26 (0x8586A8, 0x65DA7D, 0x65DADD, 0x8517F4) were added after the memory's */
        if (std::string(s.row).find(sense ? "NpcSense" : "NpcMem") != 0) Fail("senses first, then memory, then sites 23 - 26: " + I(k));
        unsigned char b[kCheckLen];
        if (s.form == kCallPlayer)
        {   /* a call site: no fixed bytes (its rel32 is the build's own); test al, al after the call */
            if (Expected(k, b) != 0 || TestLen(s.form) != kCallLen || s.next[0] != 0x84 || s.next[1] != 0xC0) Fail("a call site: CallSiteOk checks it, 84 C0 after it: " + I(k));
            continue;
        }
        if (Expected(k, b) != kCheckLen) Fail("10 bytes checked");
        const int L = TestLen(s.form);
        if (std::memcmp(b, TestBytes(s.form), (size_t)L) != 0) Fail("the site starts with its compare: " + I(k));
        /* REX.W (r15: REX.W+R), modrm mod=10 rm=rax, disp32 0x250 */
        if ((b[0] & 0xFB) != 0x48 || (b[2] & 0xC7) != 0x80 || b[3] != 0x50 || b[4] != 0x02 || b[5] != 0 || b[6] != 0) Fail("cmp qword [rax+0x250]: " + I(k));
        if (L == 8 && (b[1] != 0x83 || (b[2] & 0x38) != 0x38 || b[7] != 0)) Fail("the 8-byte form compares with 0: " + I(k));
        if (L == 7 && b[1] != 0x39) Fail("the 7-byte form compares with a register: " + I(k));
        /* after the compare: its jcc, or one instruction that leaves the flags (mov / lea) before it */
        const unsigned char a0 = b[L], a1 = b[L + 1];
        const bool jcc = a0 == 0x74 || a0 == 0x75 || (a0 == 0x0F && (a1 == 0x84 || a1 == 0x85));
        const bool keeps = a0 == 0x48 && (a1 == 0x8B || a1 == 0x8D);
        if (!jcc && !keeps) Fail("a jcc or a flag-keeping instruction after the compare: " + I(k));
    }
    /* the register forms: rsi, rbx, r15 in the modrm reg field */
    if (((TestBytes(kCmpRsi7)[2] >> 3) & 7) != 6 || ((TestBytes(kCmpRbx7)[2] >> 3) & 7) != 3 || ((TestBytes(kCmpR15_7)[2] >> 3) & 7) != 7
        || TestBytes(kCmpR15_7)[0] != 0x4C) Fail("the register forms");
    if (SiteAt(8).form != kCmpRsi7 || SiteAt(10).form != kCmpRbx7 || SiteAt(14).form != kCmpR15_7) Fail("0x670E16 rsi, 0x6738FF rbx, 0x674D62 r15");
    {   /* site 23: 1.0.65 0x8586A8 / 1.0.68 0x859578 - cmp qword [rax+0x250],0 then je +0x1F (over the watch-set re-add) */
        static const unsigned char want23[kCheckLen] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00, 0x74, 0x1F };
        unsigned char b23[kCheckLen];
        Expected(22, b23);
        if (std::string(SiteAt(22).row) != "NpcSensePlayerTest23" || SiteAt(22).form != kCmpZero8 || std::memcmp(b23, want23, (size_t)kCheckLen) != 0)
            Fail("site 23: NpcSensePlayerTest23, 48 83 B8 50 02 00 00 00 74 1F");
    }
    {   /* site 24: 1.0.65 0x65DA7D / 1.0.68 0x65E50D - the call to 'counts as the player'; site 25: 1.0.65 0x65DADD / 1.0.68 0x65E56D -
           cmp qword [rax+0x250],0 then je rel32 (the end point's player test, on the same target) */
        static const unsigned char want25[kCheckLen] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x84 };
        unsigned char b25[kCheckLen];
        Expected(24, b25);
        if (std::string(SiteAt(23).row) != "NpcSensePlayerTest24" || SiteAt(23).form != kCallPlayer) Fail("site 24: NpcSensePlayerTest24, the call form");
        if (std::string(SiteAt(24).row) != "NpcSensePlayerTest25" || SiteAt(24).form != kCmpZero8 || std::memcmp(b25, want25, (size_t)kCheckLen) != 0)
            Fail("site 25: NpcSensePlayerTest25, 48 83 B8 50 02 00 00 00 0F 84");
    }
    {   /* site 26: 1.0.65 0x8517F4 / 1.0.68 0x8526C4 - inside my private building 0x8516F0: cmp qword [rax+0x250],0 (the target's
           faction) then je +0x14 (to the 'not inside' return) */
        static const unsigned char want26[kCheckLen] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00, 0x74, 0x14 };
        unsigned char b26[kCheckLen];
        Expected(25, b26);
        if (std::string(SiteAt(25).row) != "NpcSensePlayerTest26" || SiteAt(25).form != kCmpZero8 || std::memcmp(b26, want26, (size_t)kCheckLen) != 0)
            Fail("site 26: NpcSensePlayerTest26, 48 83 B8 50 02 00 00 00 74 14");
    }
}
/* T-943: the stub each form gets - the compare at 0 and at NOTPEER, the cells, and BACK to the site + the compare's length (the jcc) */
void t_t943_npc_stub()
{
    using namespace coopcrime;
    const int off = 0x400, cells = 16;
    {   /* the 8-byte form is the default sight stub, byte for byte */
        std::vector<unsigned char> a(0x1000, 0xCC), b(0x1000, 0xCC);
        const int la = SightStubBuild(&a[0], 0x1408589B7ULL, cells, off);
        const int lb = SightStubBuild(&b[0], 0x1408589B7ULL, cells, off, swnpc::TestBytes(swnpc::kCmpZero8), swnpc::TestLen(swnpc::kCmpZero8));
        if (la != lb || a != b) Fail("T-943: the 8-byte form is the default stub");
    }
    for (int f = 0; f < swnpc::kForms; ++f)
    {
        const int L = swnpc::TestLen(f);
        const unsigned char* t = swnpc::TestBytes(f);
        const unsigned long long site = 0x140670E16ULL;
        std::vector<unsigned char> p(0x1000, 0xCC);
        const int len = SightStubBuild(&p[0], site, cells, off, t, L);
        const int H = L + 6, N = H + 13 * cells, hit = N + L + 2, back = hit + 12;
        if (len != SightStubLenFor(L, cells) || len != back + 14 || len > off) Fail("T-943: the length, form " + I(f));
        if (std::memcmp(&p[0], t, (size_t)L) != 0 || std::memcmp(&p[N], t, (size_t)L) != 0) Fail("T-943: the compare at 0 and at NOTPEER, form " + I(f));
        int d = 0;
        std::memcpy(&d, &p[L + 2], 4); if (p[L] != 0x0F || p[L + 1] != 0x85 || H + d != back) Fail("T-943: a player faction goes straight back, form " + I(f));
        for (int k = 0; k < cells; ++k)
        {
            const int o = H + 13 * k;
            std::memcpy(&d, &p[o + 3], 4); if (p[o] != 0x48 || p[o + 1] != 0x3B || p[o + 2] != 0x05 || o + 7 + d != off + 8 + 8 * k) { Fail("T-943: cell " + I(k) + " compares rax with its cell, form " + I(f)); break; }
            std::memcpy(&d, &p[o + 9], 4); if (o + 13 + d != hit) { Fail("T-943: cell " + I(k) + " jumps to HIT, form " + I(f)); break; }
        }
        if (p[N + L] != 0xEB || N + L + 2 + (int)p[N + L + 1] != back) Fail("T-943: NOTPEER goes back, form " + I(f));
        std::memcpy(&d, &p[hit + 4], 4); if (p[hit] != 0xF0 || hit + 8 + d != off) Fail("T-943: HIT counts into the site's hits, form " + I(f));
        if (p[hit + 8] != 0x48 || p[hit + 9] != 0x83 || p[hit + 10] != 0xF8 || p[hit + 11] != 0x00) Fail("T-943: HIT answers ZF=0 (cmp rax,0), form " + I(f));
        unsigned long long to = 0;
        std::memcpy(&to, &p[back + 6], 8);
        if (p[back] != 0xFF || p[back + 1] != 0x25 || to != site + (unsigned long long)L) Fail("T-943: BACK returns to the jcc at site + " + I(L) + ", form " + I(f));
        if (p[len] != 0xCC) Fail("T-943: nothing written past the stub, form " + I(f));
    }
}
/* T-943: the call form - the site check (E8 rel32 reaching 'counts as the player' directly or through one E9 jump, then 84 C0) and
   its stub (the engine's call first, then the target's faction against the cells) */
void t_t943_npc_call()
{
    using namespace swnpc;
    /* the real sites: 1.0.65 0x65DA7D -> jump 0x4110A -> 0x855CF0; 1.0.68 0x65E50D -> jump 0x41132 -> 0x856BC0 */
    const unsigned char at65[7] = { 0xE8, 0x88, 0x36, 0x9E, 0xFF, 0x84, 0xC0 }, hop65[5] = { 0xE9, 0xE1, 0x4B, 0x81, 0x00 };
    const unsigned char at68[7] = { 0xE8, 0x20, 0x2C, 0x9E, 0xFF, 0x84, 0xC0 }, hop68[5] = { 0xE9, 0x89, 0x5A, 0x81, 0x00 };
    const unsigned long long s65 = 0x14065DA7DULL, s68 = 0x14065E50DULL;
    if (CallDest(at65, s65) != 0x14004110AULL || CallDest(at68, s68) != 0x140041132ULL) Fail("the call's target decoded (1.0.65 0x4110A, 1.0.68 0x41132)");
    if (!CallSiteOk(at65, s65, hop65, 0x140855CF0ULL) || !CallSiteOk(at68, s68, hop68, 0x140856BC0ULL)) Fail("the real sites reach their build's function through the jump");
    if (CallSiteOk(at65, s65, hop65, 0x140856BC0ULL) || CallSiteOk(at68, s68, hop68, 0x140855CF0ULL) || CallSiteOk(at65, s65, hop65, 0))
        Fail("the other build's function, or no row: refused");
    {   /* a direct call to the function */
        const unsigned long long site = 0x140001000ULL, callee = 0x140855CF0ULL;
        const int rel = (int)(long long)(callee - (site + 5));
        unsigned char at[7] = { 0xE8, 0, 0, 0, 0, 0x84, 0xC0 };
        CallPut32(at + 1, rel);
        const unsigned char nohop[5] = { 0x40, 0x53, 0x48, 0x83, 0xEC };
        if (CallDest(at, site) != callee || !CallSiteOk(at, site, nohop, callee) || !CallSiteOk(at, site, 0, callee)) Fail("a direct call to the function");
        if (CallSiteOk(at, site, nohop, callee + 0x10)) Fail("a direct call to another function: refused");
    }
    {   /* a wrong follow byte, a non-E8 site, a target that is no jump to the function */
        unsigned char at[7]; unsigned char hop[5];
        std::memcpy(at, at65, 7); at[6] = 0xC9; if (CallSiteOk(at, s65, hop65, 0x140855CF0ULL)) Fail("84 C9 after the call: refused");
        std::memcpy(at, at65, 7); at[5] = 0x85; if (CallSiteOk(at, s65, hop65, 0x140855CF0ULL)) Fail("85 C0 after the call: refused");
        std::memcpy(at, at65, 7); at[0] = 0xE9; if (CallSiteOk(at, s65, hop65, 0x140855CF0ULL) || CallDest(at, s65) != 0) Fail("a jump, not a call: refused");
        std::memcpy(at, at65, 7); at[0] = 0x90; if (CallSiteOk(at, s65, hop65, 0x140855CF0ULL)) Fail("not a call: refused");
        std::memcpy(hop, hop65, 5); hop[0] = 0xE8; if (CallSiteOk(at65, s65, hop, 0x140855CF0ULL)) Fail("the target is not a jump: refused");
        std::memcpy(hop, hop65, 5); hop[1] = 0xE2; if (CallSiteOk(at65, s65, hop, 0x140855CF0ULL)) Fail("the jump goes elsewhere: refused");
        if (CallSiteOk(at65, s65, 0, 0x140855CF0ULL)) Fail("the jump unread: refused");
    }
    /* the stub */
    const int off = 0x400, cells = 16;
    const unsigned long long to = 0x14004110AULL;
    std::vector<unsigned char> p(0x1000, 0xCC);
    const int len = CallStubBuild(&p[0], to, cells, off);
    const int N = 45 + 13 * cells, hit = N + 4, done = N + 14;
    if (len != CallStubLenFor(cells) || len != done + 6 || len > off) Fail("the call stub's length");
    static const unsigned char head[5] = { 0x51, 0x48, 0x83, 0xEC, 0x20 };
    if (std::memcmp(&p[0], head, 5) != 0) Fail("push rcx; sub rsp, 0x20 (rsp 16-aligned at its calls)");
    unsigned long long imm = 0;
    std::memcpy(&imm, &p[7], 8);
    if (p[5] != 0x48 || p[6] != 0xB8 || imm != to || p[15] != 0xFF || p[16] != 0xD0) Fail("mov rax, the site's call target; call rax");
    int d = 0;
    std::memcpy(&d, &p[21], 4);
    if (p[17] != 0x84 || p[18] != 0xC0 || p[19] != 0x0F || p[20] != 0x85 || 25 + d != done) Fail("the engine's yes goes straight to DONE");
    static const unsigned char fac[14] = { 0x48, 0x8B, 0x4C, 0x24, 0x20, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x58, 0x48, 0x85, 0xC0 };
    if (std::memcmp(&p[25], fac, 14) != 0) Fail("the target from its saved slot, its faction through vtable +0x58, tested for null");
    std::memcpy(&d, &p[41], 4);
    if (p[39] != 0x0F || p[40] != 0x84 || 45 + d != N) Fail("no faction: NO");
    for (int k = 0; k < cells; ++k)
    {
        const int o = 45 + 13 * k;
        std::memcpy(&d, &p[o + 3], 4); if (p[o] != 0x48 || p[o + 1] != 0x3B || p[o + 2] != 0x05 || o + 7 + d != off + 8 + 8 * k) { Fail("call stub cell " + I(k) + " compares rax with its cell"); break; }
        std::memcpy(&d, &p[o + 9], 4); if (p[o + 7] != 0x0F || p[o + 8] != 0x84 || o + 13 + d != hit) { Fail("call stub cell " + I(k) + " jumps to HIT"); break; }
    }
    if (p[N] != 0x31 || p[N + 1] != 0xC0 || p[N + 2] != 0xEB || N + 4 + (int)p[N + 3] != done) Fail("NO: al = 0, then DONE");
    std::memcpy(&d, &p[hit + 4], 4);
    if (p[hit] != 0xF0 || p[hit + 1] != 0x48 || p[hit + 2] != 0xFF || p[hit + 3] != 0x05 || hit + 8 + d != off) Fail("HIT counts into the site's hits");
    if (p[hit + 8] != 0xB0 || p[hit + 9] != 0x01) Fail("HIT: al = 1");
    static const unsigned char tail[6] = { 0x48, 0x83, 0xC4, 0x20, 0x59, 0xC3 };
    if (std::memcmp(&p[done], tail, 6) != 0) Fail("DONE: add rsp, 0x20; pop rcx; ret");
    if (p[len] != 0xCC) Fail("nothing written past the call stub");
}
/* T-943: the teammate list - teammates only, no nulls or repeats, capped; each faction keeps its cell; the log cap */
void t_t943_npc_list()
{
    using namespace swnpc;
    int a = 1, b = 2, c = 3, d = 4;
    const void* in[6] = { &a, 0, &b, &a, &c, &d };
    const void* out[4]; int drops = -1;
    if (MateList(in, 6, out, 4, &drops) != 4 || out[0] != &a || out[1] != &b || out[2] != &c || out[3] != &d || drops != 0) Fail("nulls and repeats left out");
    if (MateList(in, 6, out, 2, &drops) != 2 || drops != 2) Fail("those past the cap are counted");
    if (MateList(0, 3, out, 4, &drops) != 0 || drops != 0 || MateList(in, 0, out, 4, &drops) != 0) Fail("no team: an empty list");
    /* cells: b leaves, d arrives - a and c keep their cells, d takes b's */
    const void* cur[4] = { 0, 0, 0, 0 };
    const void* next[4]; int cd = 0;
    const void* w1[3] = { &a, &b, &c };
    if (coopcrime::StableCellsPlace(cur, w1, 3, next, 4, &cd) != 3) Fail("three teammates held");
    for (int k = 0; k < 4; ++k) cur[k] = next[k];
    const void* w2[3] = { &a, &c, &d };
    coopcrime::StableCellsPlace(cur, w2, 3, next, 4, &cd);
    if (next[0] != cur[0] || next[2] != cur[2] || next[1] != &d || next[3] != 0) Fail("a teammate keeps its cell; a newcomer takes the freed one");
    coopcrime::StableCellsPlace(next, 0, 0, cur, 4, &cd);
    if (cur[0] != 0 || cur[1] != 0 || cur[2] != 0 || cur[3] != 0) Fail("an empty list empties every cell");
    if (!SayNow(1, 0, 0) || SayNow(5, 5, 1) || !SayNow(6, 5, 2) || SayNow(9, 5, kSayCap)) Fail("a line when the count rose, kSayCap lines at most");
}
/* T-947 (decision 968 a, owner 1437 a): another player's base in the name tag's colour of the standing; a friendly player's outpost
   shown at once and remembered; the [TEAMBASE] naming */
void t_t947_base_colours()
{
    using namespace baseshow;
    /* the class per standing: a teammate team blue whatever the level; else friendly / neutral / enemy; a level not read neutral */
    if (ColourOfStanding(nametag::kFriendly, 0) != kColourFriendly || ColourOfStanding(nametag::kNeutral, 0) != kColourNeutral
        || ColourOfStanding(nametag::kHostile, 0) != kColourEnemy || ColourOfStanding(nametag::kUnknown, 0) != kColourNeutral) Fail("T-947: a level's colour class");
    if (ColourOfStanding(nametag::kHostile, 1) != kColourTeam || ColourOfStanding(nametag::kFriendly, 1) != kColourTeam) Fail("T-947: a teammate is not team blue");
    /* the exact colours: the name tags' (#5a9be6, #5cb473, #f0cd57, #e65139), alpha 1 */
    const float want[4][4] = { { 0x5a / 255.0f, 0x9b / 255.0f, 0xe6 / 255.0f, 1.0f }, { 0x5c / 255.0f, 0xb4 / 255.0f, 0x73 / 255.0f, 1.0f },
                               { 0xf0 / 255.0f, 0xcd / 255.0f, 0x57 / 255.0f, 1.0f }, { 0xe6 / 255.0f, 0x51 / 255.0f, 0x39 / 255.0f, 1.0f } };
    for (int k = 0; k < kColourClasses; ++k)
    {
        float got[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
        ColourRgba(k, got);
        for (int i = 0; i < 4; ++i) if (got[i] != want[k][i]) Fail(std::string("T-947: the rgba of the ") + ColourName(k) + " colour");
    }
    float tb[4]; ColourRgba(kColourTeam, tb);
    const nametag::Rgb team = nametag::TeamColour();
    if (tb[0] != team.r || tb[1] != team.g || tb[2] != team.b) Fail("T-947: team blue is not the name tags' TeamColour");
    if (std::string(ColourName(kColourTeam)) != "team" || std::string(ColourName(kColourFriendly)) != "friendly" || std::string(ColourName(kColourNeutral)) != "neutral"
        || std::string(ColourName(kColourEnemy)) != "enemy" || std::string(ColourName(4)) != "?") Fail("T-947: the colour names");
    /* the pointer: the four side by side, 16 bytes apart */
    const unsigned long long mb = 0x7000ull, sl = 0x9000ull;
    if (ColourOfPointer(mb, mb) != kColourTeam || ColourOfPointer(mb + 16, mb) != kColourFriendly || ColourOfPointer(mb + 48, mb) != kColourEnemy) Fail("T-947: a mod colour pointer is not its class");
    if (ColourOfPointer(mb + 64, mb) != -1 || ColourOfPointer(mb + 8, mb) != -1 || ColourOfPointer(mb - 16, mb) != -1 || ColourOfPointer(mb, 0) != -1) Fail("T-947: a pointer outside the four reads as one");
    /* the [TEAMBASE] naming: the mod's four by name, the engine's five as before, anything else '?' */
    if (std::string(PickColourName(mb, sl, mb)) != "team" || std::string(PickColourName(mb + 16, sl, mb)) != "friendly"
        || std::string(PickColourName(mb + 32, sl, mb)) != "neutral" || std::string(PickColourName(mb + 48, sl, mb)) != "enemy") Fail("T-947: the [TEAMBASE] line does not name the mod's colours");
    if (std::string(PickColourName(sl, sl, mb)) != "ally" || std::string(PickColourName(sl + 48, sl, mb)) != "own") Fail("T-947: the engine's colours lost their names");
    if (std::string(PickColourName(0x8000ull, sl, mb)) != "?" || std::string(PickColourName(mb + 64, sl, mb)) != "?") Fail("T-947: an unknown colour is named");
    /* the hide rule with the standing: a teammate and a FRIENDLY player's outpost shown at once */
    const int fr = nametag::kFriendly, ne = nametag::kNeutral, en = nametag::kHostile;
    if (HideStep(kShowOther, 1, 1, 0, fr) != kHideNone || HideStep(kShowOther, 0, 0, 0, fr) != kHideShow || HideStep(kShowOther, 1, 0, 0, fr) != kHideShow)
        Fail("T-947: a friendly player's outpost is not on the map at once");
    if (HideStep(kShowOther, 1, 1, 0, HideLevel(en, 1)) != kHideNone || HideStep(kShowOther, 0, 0, 0, HideLevel(ne, 1)) != kHideShow) Fail("T-947: a teammate by the table is not on the map at once");
    if (HideLevel(en, 0) != en || HideLevel(ne, 1) != fr) Fail("T-947: HideLevel");
    /* neutral and enemy: hidden until discovered by the engine's rule, as before */
    if (HideStep(kShowOther, 1, 1, 0, ne) != kHideHide || HideStep(kShowOther, 1, 1, 0, en) != kHideHide) Fail("T-947: a neutral / enemy player's new copy is not hidden");
    if (HideStep(kShowOther, 0, 1, 0, ne) != kHideNoted || HideStep(kShowOther, 0, 0, 0, en) != kHideNone) Fail("T-947: a neutral / enemy copy's discovery is not by the engine's mark");
    if (HideStep(kShowOther, 1, 1, 0) != HideStep(kShowOther, 1, 1, 0, ne) || HideStep(kShowOther, 0, 1, 0) != kHideNoted) Fail("T-947: the rule without a level changed");
    if (HideStep(kShowOwn, 1, 1, 0, fr) != kHideNone || NoteDiscovered(kShowOwn, 5u, 1, 0, fr) != 0) Fail("T-947: the player's own outpost is touched");
    /* noted as discovered: a friendly player's at once (even from a town just made: mark not read), a neutral one not */
    if (NoteDiscovered(kShowOther, 5u, -1, 0, fr) != 1 || NoteDiscovered(kShowOther, 5u, -1, 0, ne) != 0 || NoteDiscovered(kShowOther, 0u, -1, 0, fr) != 0
        || NoteDiscovered(kShowOther, 5u, 1, 1, fr) != 0) Fail("T-947: a friendly player's outpost is not noted as discovered");
    /* owner 1437 a: friendly, then fallen to enemy - it stays shown through the remembered set (a look, and a re-found after a save) */
    std::set<unsigned> disc;
    if (NoteDiscovered(kShowOther, 9u, -1, disc.count(9u) != 0 ? 1 : 0, fr) != 0) disc.insert(9u);
    const int rem = disc.count(9u) != 0 ? 1 : 0;
    if (rem != 1) Fail("T-947: the friendly outpost was not remembered");
    if (HideStep(kShowOther, 0, 1, rem, en) != kHideNone || HideStep(kShowOther, 0, 1, rem, ne) != kHideNone) Fail("T-947: a friendly player's outpost is hidden when the standing falls");
    if (HideStep(kShowOther, 1, 1, rem, en) == kHideHide || HideStep(kShowOther, 1, 0, rem, en) != kHideShow) Fail("T-947: a fallen friend's outpost is hidden again at a re-found");
    if (HideStep(kShowOther, 1, 1, 0, en) != kHideHide) Fail("T-947: without the note it would be hidden (the test's premise)");
}
/* T-947: the hook calls the original for anything not a shown copy, from the address table's row (no literal address); the show step
   takes the standing; a standing event re-runs it */
void t_t947_base_colour_sources()
{
    const std::string s = T313ReadFile("../coop-plugin/build.cpp");
    if (s.empty()) { Fail("T-947: build.cpp not read"); return; }
    const size_t d = s.rfind("const float* detour_mapMarkerColour(void* item)");
    const size_t e = d == std::string::npos ? std::string::npos : s.find("\n}", d);
    if (d == std::string::npos || e == std::string::npos) { Fail("T-947: the colour detour not found"); return; }
    const std::string body = s.substr(d, e - d);
    if (body.find("const int idx = (item != 0 && BdOnMain() != 0) ? BdShowIndexOf(item) : -1;") == std::string::npos
        || body.find("if (idx < 0) return orig_mapMarkerColour(item);") == std::string::npos) Fail("T-947: the hook does not call the original for a null item, another item or off the main thread");
    if (body.find("return g_bdMarkRgba[baseshow::ColourOfStanding(level, mate)];") == std::string::npos) Fail("T-947: the hook does not answer the mod's colour of the standing");
    if (body.find("0x") != std::string::npos) Fail("T-947: a literal number in the colour detour");
    if (s.find("coop::AddHook((void*)(rbase + (uintptr_t)kBdMapMarkerColourRva), (void*)&detour_mapMarkerColour, (void**)&orig_mapMarkerColour)") == std::string::npos)
        Fail("T-947: the hook is not installed at the address table's MapMarkerColour row");
    const size_t fill = s.find("baseshow::ColourRgba(k, g_bdMarkRgba[k]);"), hook = s.find("(void*)&detour_mapMarkerColour, (void**)&orig_mapMarkerColour");
    if (fill == std::string::npos || hook == std::string::npos || fill > hook) Fail("T-947: the colours are not filled before the hook is installed");
    if (s.find("const int step = baseshow::HideStep(s->kind, madeNow, disc, remembered, hl);") == std::string::npos
        || s.find("baseshow::NoteDiscovered(s->kind, s->id, madeNow != 0 ? -1 : disc, remembered, hl)") == std::string::npos) Fail("T-947: the show step does not take the standing");
    if (s.find("if ((plan & baseshow::kTickOwnCheck) != 0) TlStandingTick();") == std::string::npos) Fail("T-947: a standing event does not re-run the show step");
    if (s.find("if (g_tlStandSeen != 0 && ep == g_tlStandEpoch && tno == g_tlStandTeamNo && reg == g_tlStandReg) return;") == std::string::npos) Fail("T-947: the standing step is not on the event");
}
/* T-947 fold 1: a friendly reading counts for the show / remember step only once that player's side came from the wire; the event
   steps only the owners whose answer changed; the log's own colour read is not counted */
void t_t947_fold1()
{
    using namespace baseshow;
    const int fr = nametag::kFriendly, ne = nametag::kNeutral, en = nametag::kHostile, un = nametag::kUnknown;
    if (ReceivedLevel(fr, 0) != un || ReceivedLevel(fr, 1) != fr || ReceivedLevel(ne, 0) != ne || ReceivedLevel(en, 0) != en || ReceivedLevel(un, 1) != un)
        Fail("T-947 fold 1: ReceivedLevel");
    /* a save's old friendly standing, before the wire's: the neutral rule - hidden, not remembered */
    const int before = HideLevel(ReceivedLevel(fr, 0), 0);
    if (HideStep(kShowOther, 1, 1, 0, before) != kHideHide || NoteDiscovered(kShowOther, 7u, -1, 0, before) != 0) Fail("T-947 fold 1: a friendly reading before the wire's shows or remembers the base");
    /* the wire's friendly standing: shown at once and remembered */
    const int after = HideLevel(ReceivedLevel(fr, 1), 0);
    if (HideStep(kShowOther, 0, 0, 0, after) != kHideShow || NoteDiscovered(kShowOther, 7u, 0, 0, after) != 1) Fail("T-947 fold 1: the wire's friendly standing does not show the base");
    /* a teammate by the table is not gated */
    if (HideLevel(ReceivedLevel(ne, 0), 1) != fr || HideStep(kShowOther, 1, 1, 0, HideLevel(ReceivedLevel(fr, 0), 1)) != kHideNone) Fail("T-947 fold 1: a teammate waits for the wire");
    /* the event: only owners whose hide level changed (or new) are stepped; *last follows now */
    std::map<int, int> last, now; std::vector<int> due;
    now[1] = fr; now[2] = ne;
    StandingStepOwners(&last, now, &due);
    if (due.size() != 2 || due[0] != 1 || due[1] != 2 || last != now) Fail("T-947 fold 1: new owners are not stepped");
    StandingStepOwners(&last, now, &due);
    if (!due.empty()) Fail("T-947 fold 1: owners with the same answer are stepped again (an NPC fight's epoch move)");
    now[1] = ne;
    StandingStepOwners(&last, now, &due);
    if (due.size() != 1 || due[0] != 1) Fail("T-947 fold 1: only the owner whose answer changed is stepped");
    now[2] = un;   /* a friendly reading that is not the wire's yet reads unknown: a change from neutral */
    StandingStepOwners(&last, now, &due);
    if (due.size() != 1 || due[0] != 2) Fail("T-947 fold 1: an answer moving to unknown is not stepped");
    std::map<int, int> one; one[2] = un;
    StandingStepOwners(&last, one, &due);
    if (!due.empty() || last.count(1) != 0) Fail("T-947 fold 1: an owner with no shown base is kept");
    std::map<int, int> back; back[1] = ne; back[2] = un;
    StandingStepOwners(&last, back, &due);
    if (due.size() != 1 || due[0] != 1) Fail("T-947 fold 1: an owner back again is not stepped");
    /* the engine glue (source checks) */
    const std::string s = T313ReadFile("../coop-plugin/build.cpp");
    const std::string r = T313ReadFile("../coop-plugin/relations.cpp");
    if (s.empty() || r.empty()) { Fail("T-947 fold 1: a source not read"); return; }
    if (s.find("const int hl = baseshow::HideLevel(baseshow::ReceivedLevel(standing, theirsIn), mate);") == std::string::npos
        || s.find("now[g_shown[i].owner] = baseshow::HideLevel(baseshow::ReceivedLevel(lv, theirsIn), mate);") == std::string::npos)
        Fail("T-947 fold 1: the show step or the event does not gate a friendly reading on the wire's");
    if (s.find("baseshow::StandingStepOwners(&g_tlStandHl, now, &due);") == std::string::npos) Fail("T-947 fold 1: the event steps every owner");
    if (s.find("g_bdStanding.clear();") != std::string::npos) Fail("T-947 fold 1: an epoch move drops every owner's standing");
    const size_t on = s.find("g_markColourReading = 1;"), rd = s.find("const int colourRead = BdMarkColourPod("), off = rd == std::string::npos ? std::string::npos : s.find("g_markColourReading = 0;", rd);
    if (on == std::string::npos || rd == std::string::npos || off == std::string::npos || !(on < rd && rd < off)
        || s.find("if (g_markColourReading == 0) ++g_markColoured;") == std::string::npos) Fail("T-947 fold 1: the log's colour read is counted as the map's");
    const size_t mk = r.find("if (ts >= 0) g_theirSideIn.insert(ts);"), ev = r.find("if (w == 1) TagNoteRelation(owner, other);");
    if (mk == std::string::npos || ev == std::string::npos || mk > ev) Fail("T-947 fold 1: the wire's side is not marked before its event");
    if (r.find("void RelationsForgetQueue() { g_theirSideIn.clear();") == std::string::npos) Fail("T-947 fold 1: the mark outlives the world");
}
/* T-947 (run T1925 fix 1): a copy the game discovered without its name (drawn neutral grey, the colour hook never asked) gets its name
   mark from the show step - a teammate's and any other player's, every standing; never one the step hides or puts on the map itself */
void t_t947_t1925_name()
{
    using namespace baseshow;
    const int fr = nametag::kFriendly, ne = nametag::kNeutral, en = nametag::kHostile, un = nametag::kUnknown;
    /* B's Alpha Yard: a teammate's copy the game discovered before the team formed - no hide step, the name mark set */
    if (HideStep(kShowMate, 0, 1, 1, fr) != kHideNone || NameStep(kShowMate, 1, 0, kHideNone) != 1) Fail("T1925 fix 1: a teammate's copy discovered without its name keeps the neutral grey");
    /* B's Charlie Yard: a stranger's copy the engine discovered now - noted, and the name mark set, at every standing */
    const int lv[4] = { fr, ne, en, un };
    for (int i = 0; i < 4; ++i)
    {
        const int st = HideStep(kShowOther, 0, 1, 0, lv[i]);
        if (NameStep(kShowOther, 1, 0, st) != 1) Fail("T1925 fix 1: another player's copy discovered without its name keeps the neutral grey at some standing");
    }
    if (NameStep(kShowOther, 1, 0, kHideNoted) != 1 || NameStep(kShowOther, 1, 0, kHideNone) != 1) Fail("T1925 fix 1: NameStep for another player's discovered copy");
    /* never: a copy the step hides or shows (that write sets both marks), a name already known or not read, a town not discovered or not read, the own outpost */
    if (NameStep(kShowOther, 1, 0, kHideHide) != 0 || NameStep(kShowMate, 1, 0, kHideShow) != 0 || NameStep(kShowOther, 1, 1, kHideNone) != 0
        || NameStep(kShowOther, 1, -1, kHideNone) != 0 || NameStep(kShowOther, 0, 0, kHideNone) != 0 || NameStep(kShowOther, -1, 0, kHideNone) != 0
        || NameStep(kShowOwn, 1, 0, kHideNone) != 0 || NameStep(kShowMate, 0, 0, kHideNone) != 0)
        Fail("T1925 fix 1: the name mark is set where it must not be");
    /* a stranger's new copy the founding marked: hidden, never named */
    if (NameStep(kShowOther, 1, 0, HideStep(kShowOther, 1, 1, 0, ne)) != 0) Fail("T1925 fix 1: a copy being hidden gets its name");
    const std::string b = T313ReadFile("../coop-plugin/build.cpp");
    if (b.empty()) { Fail("T1925 fix 1: build.cpp not read"); return; }
    if (b.find("const int named = disc == 1 ? BdTownNameKnownPod(t) : -1;") == std::string::npos
        || b.find("const int nameUp = baseshow::NameStep(s->kind, disc, named, step);") == std::string::npos
        || b.find("if (BdTownSetNameKnownPod(t) == 0) ++g_tlRefused;") == std::string::npos)
        Fail("T1925 fix 1: the show step does not read and set the name mark");
    if (b.find("__try { ((BdTownSetBoolFn)((*(void***)t)[kVtTownSetNameKnown / 8]))(t, true); return 1; }") == std::string::npos
        || b.find("__try { return ((BdTownGetBoolFn)((*(void***)t)[kVtTownNameKnown / 8]))(t) ? 1 : 0; }") == std::string::npos
        || b.find("const size_t kVtTownNameKnown = 0x340;") == std::string::npos)
        Fail("T1925 fix 1: the name mark is not read / written through the town's own calls (vt+0x340 / vt+0x348), guarded");
    if (b.find("if (g_shown[i].going != 0 || (g_shown[i].kind != baseshow::kShowOther && g_shown[i].kind != baseshow::kShowMate)) continue;") == std::string::npos)
        Fail("T1925 fix 1: the look does not step a teammate's copy (a refused name write is not retried)");
}

/* T-1191: the lclick lever's stand - d units from the building's centre, towards the character, ground plane only */
void t_t1191_beside_spot()
{
    float x = 0, z = 0;
    if (!rcgive::BesideSpot(100.0f, 200.0f, 400.0f, 600.0f, 75.0f, &x, &z) || std::fabs(x - 145.0f) > 0.01f || std::fabs(z - 260.0f) > 0.01f) Fail("75 along the 3-4-5 line");
    if (!rcgive::BesideSpot(100.0f, 200.0f, 110.0f, 200.0f, 75.0f, &x, &z) || std::fabs(x - 175.0f) > 0.01f || std::fabs(z - 200.0f) > 0.01f) Fail("a near character is pushed out to d");
    if (!rcgive::BesideSpot(5.0f, 5.0f, 5.0f, 5.0f, 40.0f, &x, &z) || std::fabs(x - 45.0f) > 0.01f || std::fabs(z - 5.0f) > 0.01f) Fail("on the centre: the +x side");
    if (!rcgive::BesideSpot(-54000.0f, -96000.0f, -54000.0f, -97000.0f, 75.0f, &x, &z) || std::fabs(x + 54000.0f) > 0.01f || std::fabs(z + 96075.0f) > 0.05f) Fail("world-sized coordinates, -z side");
    x = 9.0f; z = 9.0f;
    if (rcgive::BesideSpot(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, &x, &z) || x != 9.0f || z != 9.0f) Fail("d 0: refused, nothing written");
}

/* T-858 (ported from pull request 4, task 5): a paid conversation never takes the money and gives nothing (talkrefund.h, talkwire.h
   END unrun). The refund amount; the book (once, forgotten, other conversations / players untouched, cap); no clock - a booking
   lives until its conversation's END or the link's end; a restarted game's reused numbers never meet an old booking; the END's
   unrun list on the wire, old and new; every end path walked on the pure parts. */
namespace
{
struct T858Same { bool operator()(unsigned int a, unsigned int b) const { return a == b; } };
}
void t_t858_paid_refund()
{
    using namespace cooptalk;
    std::vector<int> ty, va;
    ty.push_back(8); va.push_back(500);   /* TAKE_MONEY 500 (a bribe) */
    /* the amount */
    if (TalkRefundAmount(1, 2000, 1500, ty, va) != 500) Fail("t858: a 500 bribe taken is 500 owed back");
    if (TalkRefundAmount(1, 2000, 1800, ty, va) != 200) Fail("t858: never more than the purse dropped");
    if (TalkRefundAmount(1, 9000, 1000, ty, va) != 500) Fail("t858: never more than the line's TAKE_MONEY");
    if (TalkRefundAmount(0, 2000, 1500, ty, va) != 0) Fail("t858: a refused ACT took nothing (refusal path)");
    if (TalkRefundAmount(1, -1, 1500, ty, va) != 0 || TalkRefundAmount(1, 2000, -1, ty, va) != 0) Fail("t858: an unread purse books nothing");
    if (TalkRefundAmount(1, 2000, 2000, ty, va) != 0 || TalkRefundAmount(1, 2000, 2500, ty, va) != 0) Fail("t858: no drop, nothing owed");
    { std::vector<int> t2, v2; t2.push_back(9); v2.push_back(500);
      if (TalkRefundAmount(1, 2000, 1500, t2, v2) != 0) Fail("t858: a line without TAKE_MONEY books nothing"); }
    { std::vector<int> t2(ty), v2(va); t2.push_back(8); v2.push_back(300);
      if (TalkRefundAmount(1, 2000, 1200, t2, v2) != 800) Fail("t858: two TAKE_MONEY in one line add up"); }
    { std::vector<int> t2(ty), v2(va); t2.push_back(8); v2.push_back(2147483647);
      if (TalkRefundAmount(1, 2147483647, 0, t2, v2) != 2147483647) Fail("t858: a huge take is capped at an int"); }

    /* the book */
    TalkRefundBook b;
    const T858Same same;
    std::vector<unsigned int> unrun;
    std::vector<TalkPaid> due;
    if (b.Note(7, 11, 1, 500, 3, same) != 1 || b.Note(7, 11, 1, 500, 3, same) != 2 || b.rows.size() != 1 || b.replaced != 1)
        Fail("t858: an ACT is booked once (a repeated ACT overwrites its row, never a second one)");
    if (b.Note(7, 11, 2, 0, 3, same) != 0) Fail("t858: nothing taken, nothing booked");
    b.Note(7, 11, 3, 200, 3, same); b.Note(7, 12, 1, 50, 3, same); b.Note(8, 11, 1, 70, 3, same);
    /* timeout / late or lost ACT_RESULT: A's END lists seq 1 (held, unrun); seq 3's NPC part ran (its answer came) */
    unrun.push_back(1);
    b.OnEnd(7, 11, unrun, &due, same);
    if (due.size() != 1 || due[0].seq != 1 || due[0].amount != 500) Fail("t858: the END's unrun seq is refunded, the ran one is not");
    if (due.empty() || due[0].base != 3) Fail("t858: a refund carries the ledger the money was taken from (a world switch drops it, never pays it elsewhere)");
    /* a repeated END, a repeated ACT_RESULT, a reconnect: nothing left for that conversation */
    due.clear(); b.OnEnd(7, 11, unrun, &due, same);
    if (!due.empty()) Fail("t858: a refund is never paid twice (repeated END)");
    /* other conversations and other players untouched */
    if (b.rows.size() != 2) Fail("t858: the other conversation / player's rows stay");
    due.clear(); unrun.clear(); unrun.push_back(1); b.OnEnd(8, 12, unrun, &due, same);
    if (!due.empty() || b.rows.size() != 2) Fail("t858: an END from another player for another's conversation refunds nothing");
    /* normal case: the answer came in time - the END lists nothing - nothing refunded, the rows forgotten */
    due.clear(); unrun.clear(); b.OnEnd(7, 12, unrun, &due, same);
    if (!due.empty() || b.rows.size() != 1) Fail("t858: a normal end refunds nothing and forgets the conversation");
    /* an END listing a seq this game never booked (refused here, or nothing taken) refunds nothing */
    due.clear(); unrun.clear(); unrun.push_back(99); b.OnEnd(8, 11, unrun, &due, same);
    if (!due.empty() || !b.rows.empty()) Fail("t858: an unbooked unrun seq refunds nothing; the conversation is forgotten");

    /* LOW fold 1 - no clock: a long conversation keeps its booking until its own END (the old 10-minute expiry is gone) */
    b.Note(7, 20, 1, 300, 3, same);
    for (unsigned int c = 100; c < 200; ++c) { due.clear(); unrun.clear(); b.OnEnd(7, c, unrun, &due, same); }   /* other talks come and go */
    due.clear(); unrun.clear(); unrun.push_back(1); b.OnEnd(7, 20, unrun, &due, same);
    if (due.size() != 1 || due[0].amount != 300) Fail("t858: a booking lives as long as its conversation, however long");

    /* LOW fold 2 - the NPC's game restarts: the link goes first (ForgetPeer), so its old booking (conv 5, seq 1) never meets the
       restarted game's new conv 5 / seq 1 - neither blocking the new booking nor refunded by the new END */
    b.Note(7, 5, 1, 400, 3, same); b.Note(8, 5, 1, 90, 3, same);
    /* review LOW 1: a restart WITHOUT the link's end (the world server's 10 s PLAYER_GONE hold, cancelled on re-admission): the new
       launch's conv 5 / seq 1 meets the old row - it books OVER it (the newest payment wins) and the new END refunds the new amount */
    { TalkRefundBook s; s.Note(7, 5, 1, 400, 3, same); s.Note(7, 6, 1, 10, 3, same);
      if (s.Note(7, 5, 1, 250, 4, same) != 2 || s.rows.size() != 2 || s.replaced != 1) Fail("t858: a stale row with the same numbers is overwritten, not blocking");
      if (s.rows.back().convId != 5 || s.rows.back().amount != 250 || s.rows.back().base != 4) Fail("t858: the overwrite carries the newest amount and ledger, newest place");
      std::vector<TalkPaid> d2; std::vector<unsigned int> u2; u2.push_back(1);
      s.OnEnd(7, 5, u2, &d2, same);
      if (d2.size() != 1 || d2[0].amount != 250) Fail("t858: the new END refunds the new amount, never the old launch's");
      if (s.Note(8, 6, 1, 33, 3, same) != 1 || s.rows.size() != 2) Fail("t858: another player's same numbers are their own row"); }
    b.ForgetPeer(7, 0, same);
    if (b.rows.size() != 1 || b.rows[0].peer != 8 || b.dropped != 1) Fail("t858: the link's end drops only that player's bookings, counted");
    if (b.Note(7, 5, 1, 250, 3, same) != 1) Fail("t858: after the link's restart the same numbers book anew");
    due.clear(); unrun.clear(); unrun.push_back(1); b.OnEnd(7, 5, unrun, &due, same);
    if (due.size() != 1 || due[0].amount != 250) Fail("t858: the new END refunds the new booking, never the old launch's");
    b.ForgetPeer(0, 1, same);
    if (!b.rows.empty() || b.dropped != 2) Fail("t858: every link gone drops every booking");

    /* bounded */
    for (unsigned int s = 1; s <= TalkRefundBook::kMax + 5; ++s) b.Note(1, 1, s, 1, 0, same);
    if (b.rows.size() != TalkRefundBook::kMax || b.evicted != 5 || b.rows[0].seq != 6) Fail("t858: the book is bounded, the oldest goes");
    { const long long ev = b.evicted;   /* an overwrite at the cap evicts nothing (the row it replaces makes the room) */
      if (b.Note(1, 1, 10, 7, 0, same) != 2 || b.evicted != ev || b.rows.size() != TalkRefundBook::kMax) Fail("t858: an overwrite at the cap evicts nothing"); }

    /* review LOW 2 / 3: the bounded lists drop with a count - never silently */
    { std::vector<int> v; long long lost = 0;
      for (int i = 1; i <= 32; ++i) if (TalkPushCapped(&v, i, 32, &lost) != 0) Fail("t858: under the cap nothing goes");
      if (lost != 0 || v.size() != 32) Fail("t858: 32 notes fit their cap of 32");
      if (TalkPushCapped(&v, 33, 32, &lost) != 1 || lost != 1 || v.size() != 32 || v.front() != 2 || v.back() != 33) Fail("t858: at the cap the oldest note goes, counted");
      std::vector<int> d; long long dl = 0;
      for (size_t i = 0; i < kTalkRefundDueCap + 3; ++i) TalkPushCapped(&d, (int)i, kTalkRefundDueCap, &dl);
      if (d.size() != kTalkRefundDueCap || dl != 3 || d.front() != 3) Fail("t858: waiting refunds are capped at 64, the oldest goes, counted");
      std::vector<int> z; long long zl = 0;
      if (TalkPushCapped(&z, 1, 0, &zl) != 1 || !z.empty() || zl != 1) Fail("t858: a zero cap keeps nothing, counted"); }
    { std::vector<unsigned int> u; long long over = 0;
      for (unsigned int s = 1; s <= kTalkMaxUnrun + 4; ++s) TalkUnrunAdd(&u, s, &over);
      if (u.size() != kTalkMaxUnrun || over != 4 || u.back() != kTalkMaxUnrun) Fail("t858: an END lists 16 unrun seqs; the rest are counted"); }

    /* the wire: END carries the unrun list; an END without it (protocol 193's shape) names none */
    TalkMsg e; e.kind = kTalkEnd; e.convId = 11; e.npcUid = 3; e.targetUid = 4; e.reason = kTalkEndTimeout;
    e.unrun.push_back(1); e.unrun.push_back(0xFFFFFFFFu);
    std::vector<char> w;
    if (!EncodeTalk(&w, e)) Fail("t858: END with unrun encodes");
    TalkMsg d;
    if (w.empty() || DecodeTalk(&w[0], w.size(), &d) != kTalkDecodeOk || d.unrun.size() != 2 || d.unrun[0] != 1 || d.unrun[1] != 0xFFFFFFFFu || d.reason != kTalkEndTimeout)
        Fail("t858: END's unrun round-trips");
    { TalkMsg o(e); o.unrun.clear(); std::vector<char> w2; EncodeTalk(&w2, o);   /* nothing unrun: 20 bytes + T-478's empty line id (2, protocol 199) */
      TalkMsg d2; d2.unrun.push_back(9);
      if (w2.size() != 22 || DecodeTalk(&w2[0], w2.size(), &d2) != kTalkDecodeOk || !d2.unrun.empty()) Fail("t858: an END without the list names no unrun seq"); }
    { TalkMsg o(e); o.unrun.assign(kTalkMaxUnrun, 5); std::vector<char> w2; TalkMsg d2;   /* the 16 cap: exactly 16 go */
      if (!EncodeTalk(&w2, o) || DecodeTalk(&w2[0], w2.size(), &d2) != kTalkDecodeOk || d2.unrun.size() != kTalkMaxUnrun) Fail("t858: 16 unrun seqs round-trip"); }
    { TalkMsg o(e); o.unrun.assign(kTalkMaxUnrun + 1, 5); std::vector<char> w2;
      if (EncodeTalk(&w2, o)) Fail("t858: more than kTalkMaxUnrun seqs are not encoded"); }
    { std::vector<char> w2(w); w2[w2.size() - 9] = (char)(kTalkMaxUnrun + 1);   /* the count byte (2 seqs = 8 bytes after it) */
      TalkMsg d2;
      if (DecodeTalk(&w2[0], w2.size(), &d2) != kTalkDecodeTooMany) Fail("t858: a count over the cap is refused"); }
    { std::vector<char> w2(w); w2.pop_back();
      TalkMsg d2;
      if (DecodeTalk(&w2[0], w2.size(), &d2) == kTalkDecodeOk) Fail("t858: a cut unrun list is refused"); }
}


/* T-858 fold (T1914): every end of a forwarded conversation sends END with the unrun list while the link is up - also when the
   talker's game ended it first (ANSWER CLOSED / BUSY / NO_CHAR / SHOW_FAILED for the first line: END TALKER_CLOSED). Which END each
   answer sends; the talker's game takes TALKER_CLOSED for a window it closed itself as expected, not unknown; T1914's path walked:
   the dropped paid answer, the talker closes, the END lists the line, one refund, a repeated END refunds nothing. */
void t_t858_end_paths()
{
    using namespace cooptalk;
    /* A: which END each ANSWER sends */
    for (unsigned int seq = 1; seq <= 3; ++seq)
    {
        if (TalkEndForAnswer(kTalkAnsReply, seq) != 0) Fail("t858 end: a REPLY goes on - no END");
        if (TalkEndForAnswer(kTalkAnsClosed, seq) != kTalkEndTalkerClosed) Fail("t858 end: the talker closed (T1914) - END TALKER_CLOSED with the list");
        if (TalkEndForAnswer(kTalkAnsBusy, seq) != kTalkEndTalkerClosed) Fail("t858 end: BUSY - END TALKER_CLOSED");
        if (TalkEndForAnswer(kTalkAnsNoChar, seq) != kTalkEndTalkerClosed) Fail("t858 end: NO_CHAR - END TALKER_CLOSED");
    }
    if (TalkEndForAnswer(kTalkAnsShowFailed, 1) != kTalkEndTalkerClosed) Fail("t858 end: SHOW_FAILED for the first line - END TALKER_CLOSED (no window there)");
    if (TalkEndForAnswer(kTalkAnsShowFailed, 2) != kTalkEndEngine || TalkEndForAnswer(kTalkAnsShowFailed, 9) != kTalkEndEngine)
        Fail("t858 end: SHOW_FAILED for a next line stays END ENGINE (P26s4 fold 1 M2: that window must close)");
    for (int r = kTalkAnsClosed; r <= kTalkAnsShowFailed; ++r)
        if (TalkEndForAnswer(r, 1) == 0 || TalkEndForAnswer(r, 2) == 0) Fail("t858 end: no answer that ends this side leaves the END unsent");
    /* B: TALKER_CLOSED with no window here is expected; every other reason with no window stays unknown */
    if (TalkEndExpectsNoWindow(kTalkEndTalkerClosed) != 1) Fail("t858 end: TALKER_CLOSED after this game's own close is expected");
    for (int r = 0; r <= kTalkEndNotStarted; ++r)
        if (TalkEndExpectsNoWindow(r) != 0) Fail("t858 end: the older reasons with no window stay unknown");
    if (std::strcmp(TalkEndName(kTalkEndTalkerClosed), "TALKER_CLOSED") != 0) Fail("t858 end: the reason's log name");
    if (kTalkEndTalkerClosed <= kTalkEndNotStarted || kTalkEndTalkerClosed > 255) Fail("t858 end: a new reason value, one byte on the wire");
    /* the wire: END TALKER_CLOSED carries the list */
    TalkMsg e; e.kind = kTalkEnd; e.convId = 2; e.npcUid = 3; e.targetUid = 1; e.reason = TalkEndForAnswer(kTalkAnsClosed, 2);
    e.unrun.push_back(2);
    std::vector<char> w;
    TalkMsg d;
    if (!EncodeTalk(&w, e) || w.empty() || DecodeTalk(&w[0], w.size(), &d) != kTalkDecodeOk || d.reason != kTalkEndTalkerClosed
        || d.unrun.size() != 1 || d.unrun[0] != 2)
        Fail("t858 end: END TALKER_CLOSED with its unrun seq round-trips");
    /* T1914's path: B paid 200 for conv 2 seq 2, A dropped the ACT_RESULT (the line stays held), B closed; A's END lists seq 2 */
    TalkRefundBook b;
    const T858Same same;
    std::vector<TalkPaid> due;
    b.Note(7, 2, 1, 50, 3, same);    /* seq 1: answered, its NPC part ran - not listed */
    b.Note(7, 2, 2, 200, 3, same);   /* seq 2: the dropped answer */
    b.OnEnd(7, 2, d.unrun, &due, same);
    if (due.size() != 1 || due[0].seq != 2 || due[0].amount != 200) Fail("t858 end: the talker-closed END refunds the dropped paid line once, the ran one not");
    if (!b.rows.empty()) Fail("t858 end: the talker-closed END settles the conversation's bookings");
    due.clear(); b.OnEnd(7, 2, d.unrun, &due, same);
    if (!due.empty()) Fail("t858 end: a repeated END refunds nothing (no double refund)");
    /* a talker-closed END with nothing unrun refunds nothing and still forgets the conversation (no row waits for the cap) */
    b.Note(7, 4, 1, 80, 3, same);
    { std::vector<unsigned int> none; due.clear(); b.OnEnd(7, 4, none, &due, same);
      if (!due.empty() || !b.rows.empty()) Fail("t858 end: everything ran - nothing refunded, the booking forgotten"); }
}


/* T-23 (owner 1460 a): a piece placed in an ABSENT teammate's outpost is finishable while it stays the owner's - the hand-over seed's
   rule (the world server's and the game's), the help's target (the piece's writer), the cap skip on the writer, and the helpers' last
   applied work numbers carried in the record */
void t_t23_absent_owner_rules()
{
    using namespace piecerec;
    const int owner = 0, placer = 1, third = 2;
    /* the hand-over seed: the placer the writer (owner away, placer the holder), nobody holds, or the placer's game has the area loaded */
    if (PieceHandSeedOk(placer, placer, owner, 0, 1) != 1) Fail("t23: the placer holding an absent owner's area may not seed its hand-over");
    if (PieceHandSeedOk(placer, -1, owner, 0, 0) != 1) Fail("t23: a hand-over seed into an area nobody holds was refused");
    if (PieceHandSeedOk(placer, owner, owner, 1, 1) != 1) Fail("t23: the owner present - the placer's hand-over seed (its only road) was refused");
    if (PieceHandSeedOk(placer, third, owner, 0, 0) != 0) Fail("t23: a hand-over seed from a game without the area loaded, another holding it, was taken");
    if (PieceHandSeedOk(placer, placer, placer, 0, 1) != 0 || PieceHandSeedOk(placer, placer, -1, 0, 1) != 0 || PieceHandSeedOk(-1, -1, owner, 0, 1) != 0)
        Fail("t23: a hand-over seed naming its sender, nobody, or from no slot was taken");
    /* the world server's rule over a hand-over seed */
    if (PieceAcceptRule(0, 0, -1, 0, 0, 0, 1, 0, 1, owner, 0, placer, placer, 1, 1) != kPrAccept) Fail("t23: the absent owner's hand-over seed was refused");
    if (PieceAcceptRule(0, 0, -1, 0, 0, 0, 1, 0, 1, owner, 0, placer, third, 1, 0) != kPrRefuseSeedNotLoaded) Fail("t23: a hand-over seed was taken from a game that may not place it");
    if (PieceAcceptRule(0, 0, -1, 0, 0, 0, 1, 0, 1, placer, 1, placer, placer, 1, 1) != kPrRefuseSeedNotOwner) Fail("t23: a hand-over seed naming its sender was taken");
    if (PieceAcceptRule(0, 0, -1, 0, 0, 0, 1, 0, 1, owner, 0, placer, placer, 0, 1) != kPrRefuseSeedNotOwner) Fail("t23: an ordinary seed naming another owner was taken (only a hand-over may)");
    if (PieceAcceptRule(1, 3, owner, 0, 0, 0, 1, 0, 4, owner, 0, placer, placer, 1, 1) != kPrRefuseSeedExists) Fail("t23: a hand-over seed replaced a live record");
    if (PieceAcceptRule(1, 3, owner, 0, 1, 0, 1, 0, 4, owner, 0, placer, placer, 1, 1) != kPrAccept || PieceAcceptRule(1, 3, owner, 0, 1, 0, 1, 0, 3, owner, 0, placer, placer, 1, 1) != kPrRefuseOlder)
        Fail("t23: a hand-over seed over a removal mark is not judged by its generation");
    /* after the seed: the placer writes while the owner is away and it holds the area; the owner's game takes over when it loads the area */
    if (PieceAcceptRule(1, 1, owner, 0, 0, 0, 0, 0, 2, owner, 0, placer, placer) != kPrAccept) Fail("t23: the placer holding the area could not write the absent owner's piece");
    if (PieceAcceptRule(1, 1, owner, 1, 0, 0, 0, 0, 2, owner, 1, placer, placer) != kPrRefuseNotWriter) Fail("t23: the placer wrote the piece while its owner's game has the area loaded");
    if (PieceAcceptRule(1, 2, owner, 1, 0, 0, 0, 0, 3, owner, 1, owner, placer) != kPrAccept) Fail("t23: the returning owner could not write its own piece over the placer's record");
    /* the game's send gate gives the world server's answers */
    if (PieceHandSeedWriterRule(placer, owner, 0, 1, placer) != 1 || PieceHandSeedWriterRule(placer, owner, 0, 0, third) != 0 || PieceHandSeedWriterRule(placer, owner, 1, 1, owner) != 1)
        Fail("t23: the hand-over seed's send gate differs from the world server's rule");
    /* the help's target: the piece's writer */
    if (PieceWriterSlotOf(owner, 1, placer) != owner) Fail("t23: help went past an owner whose game has the area loaded");
    if (PieceWriterSlotOf(owner, 0, placer) != placer || PieceWriterSlotOf(owner, 0, third) != third) Fail("t23: help on an absent owner's piece did not go to the area's holder");
    if (PieceWriterSlotOf(owner, 0, -1) != owner || PieceWriterSlotOf(owner, 0, kPieceHolderUnknown) != owner) Fail("t23: help with no holder known did not wait for the owner");
    /* the writer's state is written at changes a player notices */
    if (PieceWorkWriteDue(0, 0.0f, 0, 0, 0.1f, 0, 0, 10.0f) != 1) Fail("t23: the first work note was not written");
    if (PieceWorkWriteDue(1, 1.0f, 0, 0, 1.5f, 0, 0, 10.0f) != 0) Fail("t23: a step inside a tenth was written");
    if (PieceWorkWriteDue(1, 1.5f, 0, 0, 2.0f, 0, 0, 10.0f) != 1) Fail("t23: progress crossing a tenth was not written");
    if (PieceWorkWriteDue(1, 9.99f, 0, 0, 9.99f, 1, 0, 10.0f) != 1 || PieceWorkWriteDue(1, 5.0f, 0, 0, 5.0f, 0, 1, 10.0f) != 1) Fail("t23: finished / ruined was not written");
    /* the cap: help on a copy is capped; on the piece's writer the step is the piece's own */
    if (coopbuild::BuildHelpCapsCopy(0) != 1 || coopbuild::BuildHelpCapsCopy(1) != 0) Fail("t23: the copy cap is not skipped only on the piece's writer");
    /* the help verdict on a copy this game writes */
    if (coopbuild::BuildHelpRowVerdictW(1, 0, 1, 1, 0, 0, 0) != coopbuild::kBuildHelpRowOk) Fail("t23: help on a copy this game writes was not applied");
    if (coopbuild::BuildHelpRowVerdictW(1, 0, 1, 0, 0, 0, 0) != coopbuild::kBuildHelpRowGone || coopbuild::BuildHelpRowVerdictW(1, 0, 1, 1, 1, 0, 0) != coopbuild::kBuildHelpRowGone)
        Fail("t23: help on a copy this game does not write, or on a removed copy, was applied");
    if (coopbuild::BuildHelpRowVerdictW(1, 1, 0, 0, 0, 0, 0) != coopbuild::kBuildHelpRowOk || coopbuild::BuildHelpRowVerdictW(0, 0, 0, 1, 0, 0, 0) != coopbuild::kBuildHelpRowUnknown)
        Fail("t23: help on an own piece, or with no row, changed");
    /* the helpers' numbers carried in the record: the returning owner continues each helper's sequence */
    {
        std::vector<coopbuild::BuildHelpAck> own, rec;
        coopbuild::BuildHelpAck a; a.slot = (unsigned short)placer; a.seq = 3; own.push_back(a);
        coopbuild::BuildHelpAck r1; r1.slot = (unsigned short)placer; r1.seq = 7; rec.push_back(r1);
        coopbuild::BuildHelpAck r2; r2.slot = (unsigned short)third; r2.seq = 0; r2.live = 4; rec.push_back(r2);
        if (coopbuild::BuildHelpAcksRaise(&own, rec) != 2) Fail("t23: the record's helper numbers did not rise into the owner's rows");
        const coopbuild::BuildHelpAck* p = coopbuild::BuildHelpAckFor(&own, (unsigned short)placer, 0);
        const coopbuild::BuildHelpAck* t = coopbuild::BuildHelpAckFor(&own, (unsigned short)third, 0);
        if (p == 0 || p->seq != 7 || t == 0 || t->seq != 4 || t->live != 0) { Fail("t23: the carried numbers are not the record's"); return; }
        if (coopbuild::BuildHelpSeqVerdict(p->seq, 8) != coopbuild::kBuildHelpApply || coopbuild::BuildHelpSeqVerdict(p->seq, 7) != coopbuild::kBuildHelpDup)
            Fail("t23: the helper's next work after the carry-over is held or applied twice");
        std::vector<coopbuild::BuildHelpAck> low;
        coopbuild::BuildHelpAck l; l.slot = (unsigned short)placer; l.seq = 5; low.push_back(l);
        if (coopbuild::BuildHelpAcksRaise(&own, low) != 0 || coopbuild::BuildHelpAckFor(&own, (unsigned short)placer, 0)->seq != 7) Fail("t23: a lower carried number lowered a helper's row");
    }
    /* a hand-over seed's header: only with the seed flag, never naming its writer the owner; a whole record through the world server's judge */
    {
        const std::string key = "12-wall.mod@3,41@1003,-205,100";
        const std::string id = MakePieceRecId(key);
        PieceHeader h; h.generation = 1; h.writerSlot = placer; h.ownerSlot = owner; h.key = key; h.placement = 7; h.flags = kPrFlagSeed | kPrFlagHanded;
        if (!PieceHeaderValid(h)) Fail("t23: a hand-over seed's header is not valid");
        PieceHeader bad = h; bad.flags = kPrFlagHanded;
        PieceHeader bad2 = h; bad2.ownerSlot = placer;
        PieceHeader bad3 = h; bad3.flags = kPrFlagHanded | kPrFlagRemoved;
        if (PieceHeaderValid(bad) || PieceHeaderValid(bad2) || PieceHeaderValid(bad3)) Fail("t23: a hand-over flag without a seed, naming its writer, or on a removal mark is valid");
        coopbuild::BuildMsg m, st;
        m.kind = coopbuild::kBuildPlace; m.key = key; m.sid = "12-wall.mod"; m.pos[0] = 1.0f; m.pos[1] = 2.0f; m.pos[2] = 3.0f;
        m.rot[0] = 1.0f; m.rot[1] = 0.0f; m.rot[2] = 0.0f; m.rot[3] = 0.0f; m.needed = 10.0f; m.ownerSlot = coopbuild::kBuildOwnerSender; m.nonce = 7;
        st.kind = coopbuild::kBuildState; st.key = key; st.needed = 10.0f;
        std::vector<char> pb, sb, rec;
        coopbuild::EncodeBuild(&pb, m); coopbuild::EncodeBuild(&sb, st);
        PieceBody b; b.place.assign(pb.begin(), pb.end()); b.state.assign(sb.begin(), sb.end());
        if (!EncodePieceRecord(&rec, h, b)) { Fail("t23: the hand-over seed did not encode"); return; }
        std::vector<int> placerHere(1, placer), ownerAndPlacer, nobody;
        ownerAndPlacer.push_back(owner); ownerAndPlacer.push_back(placer);
        if (PieceJudgeWrite(id, &rec[0], rec.size(), placer, placer, placerHere, 0, 0, -1, 0, 0, 0) != kPrAccept) Fail("t23: the world server refused the hand-over seed of the absent owner's area's holder");
        if (PieceJudgeWrite(id, &rec[0], rec.size(), placer, owner, ownerAndPlacer, 0, 0, -1, 0, 0, 0) != kPrAccept) Fail("t23: the world server refused the hand-over seed with the owner present");
        if (PieceJudgeWrite(id, &rec[0], rec.size(), placer, third, nobody, 0, 0, -1, 0, 0, 0) != kPrRefuseSeedNotLoaded) Fail("t23: the world server took a hand-over seed from a game without the area");
        if (PieceJudgeWrite(id, &rec[0], rec.size(), owner, placer, placerHere, 0, 0, -1, 0, 0, 0) != kPrRefuseWriter) Fail("t23: a hand-over seed sent by another game than its writer was taken");
        PieceHeader dh;
        if (DecodePieceHeader(&rec[0], rec.size(), &dh) != kPrOk || (dh.flags & kPrFlagHanded) == 0 || dh.ownerSlot != owner) Fail("t23: the hand-over seed does not read back");
    }
}

/* T-23: the sources, '\r' taken out first (a CRLF checkout reads the same) */
static std::string T23NoCr(const std::string& s)
{
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) if (s[i] != '\r') o += s[i];
    return o;
}
void t_t23_wiring()
{
    const std::string bc = T23NoCr(T313ReadFile("../coop-plugin/build.cpp"));
    const std::string se = T23NoCr(T313ReadFile("../coop-plugin/net/session.cpp"));
    const std::string sm = T23NoCr(T313ReadFile("../coop-store/store_main.cpp"));
    const std::string hr = T23NoCr(T313ReadFile("../common/handrec.h"));
    const std::string pr = T23NoCr(T313ReadFile("../common/piecerec.h"));
    if (bc.empty() || se.empty() || sm.empty() || hr.empty() || pr.empty()) { Fail("t23 wiring: a source file did not read"); return; }
    /* one road: the hand-over message road is gone */
    const char* const gone[] = { "g_owed", "BdOwed", "kBuildHandAck", "BdSendAck", "g_bdAckSlot", "build_handoffs_owed_" };
    for (size_t i = 0; i < sizeof(gone) / sizeof(gone[0]); ++i)
        if (bc.find(gone[i]) != std::string::npos) Fail(std::string("t23 wiring: the hand-over message road is still in build.cpp: ") + gone[i]);
    const char* const need[] = {
        "h.flags = piecerec::kPrFlagSeed | piecerec::kPrFlagHanded;",
        "return piecerec::PieceHandSeedWriterRule(me, howner, hBit, meLoaded, holder);",
        "const int ownPiece = (w.removed != 0 || handSeed != 0 ||",
        "const int toSlot = BdHelpWriterSlot(key, h.ownerSlot);",
        "if (!net::SendBuildToSlot(m, toSlot)) break;",
        "BdHelpWriterRetireAsk(it->first); continue; }",
        "const int wHere = (cand != 0 && coopbuild::BuildHelpCapsCopy(BdCopyWriterAt(b)) == 0) ? 1 : 0;",
        "if (cand != 0 && wHere == 0)",
        "else if (wHere != 0) BdWriterStepAfter(b);",
        "const int rv = coopbuild::BuildHelpRowVerdictW(",
        "if (cw != 0) { BdHelpAcksTakeRecord(m.key, r); ++g_helpOnCopyWriter; }",
        "if (r.via == 3 && r.own == 0) { BdWriterNoteRow(key, r, 0, 1); return; }",
        "if (own != 0 && writerMine == 0 && removed == 0 && body != 0) PcaHelpAcksTake(key, k.h.placement, st);",
        "if (w.hAcksHave != 0) st.helpAcks = w.hAcks;",
        "const bool sent = true;   /* every placement travels in its piece record" };
    for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); ++i)
        if (bc.find(need[i]) == std::string::npos) Fail(std::string("t23 wiring: build.cpp lacks ") + need[i]);
    if (pr.find("(h.flags & kPrFlagHanded) != 0 ? 1 : 0, coopbrec::BrSlotIn(loadedSlots, senderSlot));") == std::string::npos)
        Fail("t23 wiring: the world server's judge does not pass the hand-over flag and the sender's loaded bit");
    if (se.find("const unsigned int kProtocolVersion = 213;") == std::string::npos || hr.find("const unsigned int kGameProtocol = 213;") == std::string::npos)
        Fail("t23 wiring: the game-to-game protocol is not 213");
    if (sm.find("const unsigned int kProtocol = 145;") == std::string::npos || piecerec::kProtocol != 145u) Fail("t23 wiring: the world-server protocol is not 145");
}
/* T-23 review fold: a refused hand-over seed is a seed again; the seed carries the owner's outpost the piece got; the writer's owed
   work is flushed at its area's unload; the writer's work moves the help baseline (wiring) */
void t_t23_review_fold()
{
    using namespace piecerec;
    {   /* fold 1: refused as not loaded with nothing stored - nothing of this game's is stored: the next write is a seed */
        int hs = 1; unsigned int sent = 5u;
        PieceSrvView v;
        PieceRefuseLearn(kPrRefuseSeedNotLoaded, 0u, &v);
        if (PieceRefuseNext(kPrRefuseSeedNotLoaded, 0u, &hs, &sent) != kPwrRetry) Fail("t23 fold 1: a refused seed is not tried again");
        PieceRetryGen(hs, &sent, 5u, 0u);
        if (hs != 0 || sent != 0u || v.have != 0) Fail("t23 fold 1: a refused seed with nothing stored still counts as sent");
        if (PieceSeedDecide(0, 0u, 0, v.have, v.gen, v.removed, hs) != 1) Fail("t23 fold 1: the retry of a refused seed is not a seed (the hand-over row would read as another player's piece)");
    }
    {   /* fold 1: refused as not loaded over a removal mark - a seed again, above the mark */
        int hs = 1; unsigned int sent = 8u;
        PieceSrvView v;
        PieceRefuseLearn(kPrRefuseSeedNotLoaded, 7u, &v);
        if (PieceRefuseNext(kPrRefuseSeedNotLoaded, 7u, &hs, &sent) != kPwrRetry) Fail("t23 fold 1: a refused seed over a mark is not tried again");
        PieceRetryGen(hs, &sent, 8u, 7u);
        if (v.have != 1 || v.gen != 7u || v.removed != 1) Fail("t23 fold 1: the refusal over a removal mark is not learned as a mark");
        if (PieceSeedDecide(0, 0u, 0, v.have, v.gen, v.removed, hs) != 1) Fail("t23 fold 1: the retry over a removal mark is not a seed");
        unsigned int g = 0u;
        if (hs != 1 || sent != 7u || PieceWriteGen(0, 0u, hs, sent, &g) < 0 || g <= 7u) Fail("t23 fold 1: the retry over a removal mark does not go above it");
    }
    {   /* the other refusals keep their roads */
        int hs = 1; unsigned int sent = 4u;
        if (PieceRefuseNext(kPrRefuseNotWriter, 3u, &hs, &sent) != kPwrRetry || hs != 1 || sent != 4u) Fail("t23 fold 1: a not-the-writer refusal changed");
    }
    {   /* fold 2: the hand-over seed's link - the town the piece got, only when it is the named owner's outpost */
        std::vector<baseshow::IdTown> t(3);
        t[0].id = 11u; t[0].owner = 0; t[0].hand[0] = 5; t[0].team = 1;
        t[1].id = 22u; t[1].owner = 2; t[1].hand[0] = 6; t[1].team = 0;
        t[2].id = 33u; t[2].owner = 0; t[2].hand[0] = 7; t[2].going = 1;
        const int h0[5] = { 5, 0, 0, 0, 0 }, h1[5] = { 6, 0, 0, 0, 0 }, h2[5] = { 7, 0, 0, 0, 0 }, none[5] = { 0, 0, 0, 0, 0 }, other[5] = { 9, 0, 0, 0, 0 };
        if (baseshow::IdOfHandOwnedBy(t, h0, 0) != 11u) Fail("t23 fold 2: the owner's outpost the piece was placed in is not its seed's link");
        if (baseshow::IdOfHandOwnedBy(t, h1, 0) != 0u) Fail("t23 fold 2: another player's outpost became the owner's piece's link");
        if (baseshow::IdOfHandOwnedBy(t, h0, 1) != 0u) Fail("t23 fold 2: an outpost of someone other than the named owner became the link");
        if (baseshow::IdOfHandOwnedBy(t, h2, 0) != 0u) Fail("t23 fold 2: a town going away became the link");
        if (baseshow::IdOfHandOwnedBy(t, none, 0) != 0u || baseshow::IdOfHandOwnedBy(t, other, 0) != 0u || baseshow::IdOfHandOwnedBy(t, h0, -1) != 0u)
            Fail("t23 fold 2: an unknown town or owner gave a link");
        if (PieceTownWriteId(1, 11u, 0u) != 11u) Fail("t23 fold 2: the hand-over seed does not carry its outpost");
    }
    /* fold 4: the writer's owed work at its area's unload */
    if (PieceWorkFlushDue(kPsmUnload, 1, 1, 0) != 1) Fail("t23 fold 4: a writer's owed work is not written as its area unloads");
    if (PieceWorkFlushDue(kPsmUnload, 0, 1, 0) != 0) Fail("t23 fold 4: nothing owed was written again");
    if (PieceWorkFlushDue(kPsmUnload, 1, 0, 0) != 0) Fail("t23 fold 4: an own piece took the copy writer's flush (its own BUILD STOPPED covers it)");
    if (PieceWorkFlushDue(kPsmUnload, 1, 1, 1) != 0) Fail("t23 fold 4: a removed copy's work was flushed");
    if (PieceWorkFlushDue(kPsmUnload + 1, 1, 1, 0) != 0) Fail("t23 fold 4: a save (no stop) flushed the work");
    if (PieceWorkFlushNoPiece(1, 0) != 1 || PieceWorkFlushNoPiece(1, 1) != 0 || PieceWorkFlushNoPiece(0, 0) != 0) Fail("t23 fold 4: PieceWorkFlushNoPiece");
    if (PieceWorkWriteDue(1, 0.31f, 0, 0, 0.35f, 0, 0, 1.0f) != 0) Fail("t23 fold 4: a step inside a tenth is written at once (the flush is its road)");
    /* re-check fold: the owed work ends when this game stops being the piece's writer (the owner's game back while the area stays loaded) */
    if (PieceWorkOweKeep(1, 1) != 1) Fail("t23 re-check: the writer's owed work was dropped while this game is still the writer");
    if (PieceWorkOweKeep(1, 0) != 0) Fail("t23 re-check: owed work kept after this game stopped being the writer (flushed later over the owner's record)");
    if (PieceWorkOweKeep(0, 1) != 0) Fail("t23 re-check: nothing owed became owed");
    /* the wiring */
    const std::string bc = T23NoCr(T313ReadFile("../coop-plugin/build.cpp"));
    if (bc.empty()) { Fail("t23 fold: build.cpp did not read"); return; }
    const char* const need[] = {
        "if (noted != 0 && r.nMats >= 0) BdHelpBaseSet(r, r.mats, r.nMats);",
        "== 0) { if (w->wkOwe == 0) g_pcrWkOwed.insert(piecerec::MakePieceRecId(key)); w->wkOwe = 1; return 0; }",
        "if (piecerec::PieceWorkOweKeep(wi->second.wkOwe, PcrOweWriterNow(*o, wi->second)) == 0)\n        { wi->second.wkOwe = 0; ++g_pcrWkOweDropped; g_pcrWkOwed.erase(o++); continue; }",
        "return piecerec::PieceWriterRule(me, owner, 0, ownerBit, holder);\n}\n/* fold 4 (review): THE EVENTS",
        "    g_pcrWkOwed.clear();  /* T-23 re-check fold */",
        "PcrWorkFlush(it->second, piecerec::kPsmUnload);",
        "if (piecerec::PieceWorkFlushNoPiece(w.wkFlush, pc != 0 ? 1 : 0) != 0) return 1;",
        "if (unloadLive == 0 && workFlush == 0 && (pc == 0 || WallWriterDecide(pc, &v) != 1))",
        "if (handRow != 0 && top == 1 && BdBuildingTownHandPod(pc, bh) != 0) engineId = TlHandSeedTownId(bh, row->second.ownerSlot);",
        "const unsigned link = piecerec::PieceTownWriteId((own != 0 || handRow != 0) ? 1 : 0, engineId, keptLink);",
        "return baseshow::IdOfHandOwnedBy(TlIdTowns(KeptMySlot()), w, owner);",
        "if (furn == 0) TlRestoredNote(m.key);" };
    for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); ++i)
        if (bc.find(need[i]) == std::string::npos) Fail(std::string("t23 fold wiring: build.cpp lacks ") + need[i]);
}
/* T-23 review fold 5: the hand-over survives a reload - the 'handed here' bit on the copy-list row in pp.build, and the choice made
   from the world server's record of the key */
/* T-23 fix (run T1963): help on a present owner's piece reaches its writer when the copy already shows the record, and the work the
   help cap kept off the copy is applied when this game becomes the writer (rules, then wiring) */
void t_t23_help_fix()
{
    using namespace coopbuild;
    /* (a) the record's help numbers: a copy whose owner's STATE was not read this session */
    if (BuildHelpTailFromRecord(1, 0, 0, 1, 0, 7u, 7u) != 1) Fail("t23 fix: a copy showing its record did not take the record's help numbers");
    if (BuildHelpTailFromRecord(1, 0, 0, 1, 0, 0u, 7u) != 1 || BuildHelpTailFromRecord(1, 0, 0, 1, 0, 7u, 0u) != 1) Fail("t23 fix: an unknown placement number refused the record's help numbers");
    if (BuildHelpTailFromRecord(1, 0, 0, 1, 1, 7u, 7u) != 0) Fail("t23 fix: the record's numbers replaced a STATE already read this session");
    if (BuildHelpTailFromRecord(0, 0, 0, 1, 0, 7u, 7u) != 0) Fail("t23 fix: a key with no standing copy row took the record's help numbers");
    if (BuildHelpTailFromRecord(1, 1, 0, 1, 0, 7u, 7u) != 0) Fail("t23 fix: an own piece took help numbers meant for a helper");
    if (BuildHelpTailFromRecord(1, 0, 1, 1, 0, 7u, 7u) != 0) Fail("t23 fix: a removal mark gave help numbers");
    if (BuildHelpTailFromRecord(1, 0, 0, 0, 0, 7u, 7u) != 0) Fail("t23 fix: a record whose STATE did not decode gave help numbers");
    if (BuildHelpTailFromRecord(1, 0, 0, 1, 0, 7u, 8u) != 0) Fail("t23 fix: another placement's record gave help numbers");
    /* (b) the retire: T1963's loom - the copy capped at 0.999 x 4 with the work far past it - completes */
    int capped = 0;
    const float cap = BuildHelpCopyCap(27.77f, 4.0f, 0, &capped);
    if (capped != 1 || BuildHelpRetireCompletes(cap, 4.0f, 27.77f - cap, 0, 0) != 1) Fail("t23 fix: the work the cap kept off a copy did not finish the piece at the retire (T1963)");
    if (BuildHelpRetireCompletes(cap, 4.0f, 0.01f, 0, 0) != 1) Fail("t23 fix: a copy at its cap with the last step over it did not finish");
    if (BuildHelpRetireCompletes(cap, 4.0f, 0.001f, 0, 0) != 0) Fail("t23 fix: work short of needed finished the piece");
    if (BuildHelpRetireCompletes(cap, 4.0f, 0.0f, 0, 0) != 0) Fail("t23 fix: a capped copy with no work over the cap finished");
    if (BuildHelpRetireCompletes(2.0f, 4.0f, 3.0f, 0, 0) != 0) Fail("t23 fix: a copy below its cap (every step on it) finished by a stale over-cap amount");
    if (BuildHelpRetireCompletes(cap, 4.0f, 5.0f, 1, 0) != 0) Fail("t23 fix: a finished piece was completed again");
    if (BuildHelpRetireCompletes(cap, 4.0f, 5.0f, 0, 1) != 0) Fail("t23 fix: a ruin was completed by help work");
    if (BuildHelpRetireCompletes(cap, 0.0f, 5.0f, 0, 0) != 0) Fail("t23 fix: an unread needed completed a piece");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (BuildHelpRetireCompletes(cap, 4.0f, nan, 0, 0) != 0 || BuildHelpRetireCompletes(cap, nan, 5.0f, 0, 0) != 0) Fail("t23 fix: a non-finite amount completed a piece");
    /* wiring: the sources, '\r' taken out first */
    const std::string bc = T23NoCr(T313ReadFile("../coop-plugin/build.cpp"));
    if (bc.empty()) { Fail("t23 fix wiring: a source file did not read"); return; }
    const char* const need[] = {
        "if (coopbuild::BuildHelpTailFromRecord(present == 1 ? copyRow : 0, own, removed, body, copyRow != 0 ? row->second.helpTailKnown : 1, k.h.placement, copyRow != 0 ? row->second.nonce : 0u) != 0)\n                BdHelpTailFromRecord(key, row->second, st);",
        "r.helpTailKnown = 1; r.helpTailSeq = t; r.helpTailLive = tl;\n    BdHelpLoad();",
        "BdHelpTailResync(key, r, h->second);\n        if (h->second.owed.size() < was)",
        "if (fromPlace == 0 && me >= 0 && h != g_help.end()) BdHelpTailResync(key, r, h->second);",
        "if (capped != 0) r.helpOverP += coopbuild::BuildHelpCapExtra(p0, p1, capTo);",
        "if (capped != 0 && cp + sP > np) overP = cp + sP - np;",
        "    r.helpOverP = overP;   /* T-23 fix */",
        "r.helpTailKnown = 0; r.helpTailSeq = 0; r.helpTailLive = 0; r.helpOverP = 0.0f;",
        "if (rd != 0 && coopbuild::BuildHelpRetireCompletes(cp, cn, over, cc, cx) != 0)",
        "const int ok = BdProgressPod(b, FLT_MAX);   /* the engine's own complete-now path */\n        if (ok) PowerCompletionPutBack(b, pwBefore);\n        --g_bdSelf;",
        "if (rd == 0 && fv != kObjKeyNone && (r.helpOverP > 0.0f || (r.helpBaseKnown != 0 && savedP > 0.0f)))",
        "BdWriterNoteRow(key, r, rd != 0 ? b : 0, 1);",
        "BdHelpRetireDrain();   /* T-23 fix:",
        "g_bdHarvestDue.empty() && g_helpRetireDue.empty()) return;",
        "|| BdCopyWriterHere(*it) == 0) { g_helpRetireDue.erase(it++); continue; }" };
    for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); ++i)
        if (bc.find(need[i]) == std::string::npos) Fail(std::string("t23 fix wiring: build.cpp lacks ") + need[i]);
    /* the retire's completion runs before the record is written, the drain after the K2 scope opens */
    const size_t pc = bc.find("const int ok = BdProgressPod(b, FLT_MAX);   /* the engine's own complete-now path */\n        if (ok) PowerCompletionPutBack(b, pwBefore);\n        --g_bdSelf;");
    const size_t pw = bc.find("BdWriterNoteRow(key, r, rd != 0 ? b : 0, 1);");
    if (pc == std::string::npos || pw == std::string::npos || pc > pw) Fail("t23 fix wiring: the retire writes the record before the completion");
    const size_t ks = bc.find("BdSelfScope selfScope;   /* help1: every engine call below is the mod's own");
    const size_t kd = bc.find("    BdHelpRetireDrain();   /* T-23 fix:");
    if (ks == std::string::npos || kd == std::string::npos || kd < ks) Fail("t23 fix wiring: the retire drain does not run inside the K2 safe point's scope");
    if (bc.find("BdHelpWriterRetire(it->first, h); continue; }") != std::string::npos) Fail("t23 fix wiring: the tick still retires off the K2 safe point");
}
/* T-23 review fold (help retire): the work over the cap is decided from the saved help work (a re-made or reloaded copy's count is 0),
   a completion that did not finish keeps the work and tries again (bounded), and one step's extra over the cap counts once */
void t_t23_review_help_retire()
{
    using namespace coopbuild;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    int capped = 0;
    const float cap = BuildHelpCopyCap(4.5f, 4.0f, 0, &capped);   /* 3.996 */
    /* (2) A reloads, B away: the owner's record at 1.0, 3.5 help saved, the copy at its cap, the count 0 - the piece completes */
    const float o = BuildHelpRetireOver(0.0f, 1, 1.0f, 3.5f, cap);
    if (!(o > 0.5f && o < 0.51f) || BuildHelpRetireCompletes(cap, 4.0f, o, 0, 0) != 1) Fail("t23 review 2: the saved help work did not complete a re-made copy at its cap");
    if (BuildHelpRetireOver(0.7f, 1, 1.0f, 3.5f, cap) != 0.7f) Fail("t23 review 2: a larger in-memory count was replaced");
    if (BuildHelpRetireOver(0.3f, 0, 1.0f, 3.5f, cap) != 0.3f) Fail("t23 review 2: an unknown base gave an extra");
    if (BuildHelpRetireOver(0.0f, 1, 1.0f, 0.0f, cap) != 0.0f) Fail("t23 review 2: no saved work gave an extra");
    if (BuildHelpRetireOver(0.0f, 1, 1.0f, 2.0f, cap) != 0.0f) Fail("t23 review 2: saved work the copy already shows gave an extra");
    if (BuildHelpRetireOver(0.0f, 1, nan, 3.5f, cap) != 0.0f || BuildHelpRetireOver(0.0f, 1, 1.0f, nan, cap) != 0.0f || BuildHelpRetireOver(nan, 0, 1.0f, 3.5f, cap) != 0.0f) Fail("t23 review 2: a non-finite amount gave an extra");
    if (BuildHelpRetireCompletes(cap, 4.0f, BuildHelpRetireOver(0.0f, 1, 1.0f, 2.99f, cap), 0, 0) != 0) Fail("t23 review 2: saved work short of needed completed the piece");
    /* (3) bounded retries */
    if (BuildHelpRetireRetry(0) != 1 || BuildHelpRetireRetry(kBuildHelpRetireTries - 1) != 1) Fail("t23 review 3: a failed completion did not keep its work for another try");
    if (BuildHelpRetireRetry(kBuildHelpRetireTries) != 0) Fail("t23 review 3: the retries are not bounded");
    /* (4) the cap's extra counts once */
    if (BuildHelpCapExtra(3.0f, 4.5f, cap) != 4.5f - cap) Fail("t23 review 4: the first step over the cap counted wrong");
    if (BuildHelpCapExtra(cap, 4.2f, cap) != 4.2f - cap) Fail("t23 review 4: a step from the cap counted wrong");
    if (BuildHelpCapExtra(4.5f, 4.8f, cap) != 4.8f - 4.5f) Fail("t23 review 4: a copy left above its cap counted its old extra again");
    if (BuildHelpCapExtra(3.0f, 3.5f, cap) != 0.0f || BuildHelpCapExtra(4.5f, 4.5f, cap) != 0.0f) Fail("t23 review 4: a step with no new extra counted");
    if (BuildHelpCapExtra(nan, 4.5f, cap) != 0.0f || BuildHelpCapExtra(3.0f, nan, cap) != 0.0f) Fail("t23 review 4: a non-finite step counted");
    /* wiring */
    const std::string bc = T23NoCr(T313ReadFile("../coop-plugin/build.cpp"));
    if (bc.empty()) { Fail("t23 review wiring: build.cpp did not read"); return; }
    const char* const need[] = {
        "if (capped != 0) r.helpOverP += coopbuild::BuildHelpCapExtra(p0, p1, capTo);",
        "if (coopbuild::BuildFloatOk(m.progress)) { r.helpBaseP = m.progress; r.helpBaseKnown = 1; }",
        "BdHelpLoad();\n    if (coopbuild::BuildFloatOk(st.progress)) { r.helpBaseP = st.progress; r.helpBaseKnown = 1; }",
        "coopbuild::BuildHelpSum(h.owed, &savedP, &sn, sd);",
        "const float over = (rd != 0) ? coopbuild::BuildHelpRetireOver(r.helpOverP, r.helpBaseKnown, r.helpBaseP, savedP, cp) : r.helpOverP;",
        "if (rd != 0 && coopbuild::BuildHelpRetireCompletes(cp, cn, over, cc, cx) != 0)",
        "if (coopbuild::BuildHelpRetireRetry(h.retireFails) != 0)",
        "h.retireTry = 0; h.retireWaitLogged = 0; h.retireFails = 0;" };
    for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); ++i)
        if (bc.find(need[i]) == std::string::npos) Fail(std::string("t23 review wiring: build.cpp lacks ") + need[i]);
    if (bc.find("r.helpOverP += p1 - capTo;") != std::string::npos) Fail("t23 review wiring: the cap's extra is still counted from the cap every step");
    /* a failed completion returns before the work is cleared; the base is never reset by a re-made copy */
    const size_t rt = bc.find("if (coopbuild::BuildHelpRetireRetry(h.retireFails) != 0)");
    const size_t rr = (rt == std::string::npos) ? rt : bc.find("                return;\n", rt);
    const size_t cl = (rt == std::string::npos) ? rt : bc.find("    h.owed.clear();\n    h.pendP = 0.0f;", rt);
    if (rr == std::string::npos || cl == std::string::npos || rr > cl) Fail("t23 review wiring: a failed completion does not keep its work");
    if (bc.find("r.helpBaseKnown = 0") != std::string::npos) Fail("t23 review wiring: the owner's base is cleared somewhere (a re-made copy keeps it)");
}
void t_t23_review_fold5()
{
    using namespace piecerec;
    {   /* the row: the bit round-trips in the same bytes; a row written before the bit reads 'not handed' */
        coopbuild::BuildCopyRec c;
        c.key = "Fabric Loom@12,40@1234.5,567.8"; c.sid = "12345-rebirth.mod"; c.slot = 2; c.nonce = 0x1234abcdu;
        c.pos[0] = 1234.5f; c.pos[1] = 10.0f; c.pos[2] = 567.8f;
        std::string rk0, rk1;
        std::vector<char> p0, s0, p1, s1;
        coopbuild::BuildCopyRec h = c;
        h.handed = 1;
        if (!coopbuild::BuildCopyRowMake(c, &rk0, &p0, &s0) || !coopbuild::BuildCopyRowMake(h, &rk1, &p1, &s1)) { Fail("t23 fold 5: a copy row was not made"); return; }
        if (rk0 != rk1 || p0 != p1 || s0.size() != s1.size()) Fail("t23 fold 5: the handed bit changed the row's key, PLACE or size");
        coopbuild::BuildCopyRec b0, b1;
        if (!coopbuild::BuildCopyRowRead(rk0, std::string(p0.begin(), p0.end()), std::string(s0.begin(), s0.end()), &b0) || b0.handed != 0)
            Fail("t23 fold 5: a row without the bit (as every build before it wrote) does not read as 'not handed'");
        if (!coopbuild::BuildCopyRowRead(rk1, std::string(p1.begin(), p1.end()), std::string(s1.begin(), s1.end()), &b1) || b1.handed != 1
            || !coopbuild::BuildCopyRecSame(b1, h))
            Fail("t23 fold 5: the handed bit does not round-trip");
        if (coopbuild::BuildCopyRecSame(b0, b1)) Fail("t23 fold 5: a handed entry compares the same as a plain copy (the owner's PLACE would not clear it)");
        coopbuild::BuildCopyRec m = h;
        m.removed = 1; m.alias = 1; m.emptyLoads = 2;
        std::string rk2; std::vector<char> p2, s2; coopbuild::BuildCopyRec b2;
        if (!coopbuild::BuildCopyRowMake(m, &rk2, &p2, &s2) || !coopbuild::BuildCopyRowRead(rk2, std::string(p2.begin(), p2.end()), std::string(s2.begin(), s2.end()), &b2)
            || b2.handed != 1 || b2.removed != 1 || b2.alias != 1 || b2.emptyLoads != 2)
            Fail("t23 fold 5: the handed bit and the rf1 facts disturb each other");
    }
    /* the choice: from the world server's record of the key, only once the area's list is complete */
    const unsigned int nonce = 77u;
    if (PieceHandLoadChoice(2, 1, 0, 0, 0u, nonce, 1) != kPhlOther) Fail("t23 fold 5: the owner's own copy here did not end the hand-over");
    if (PieceHandLoadChoice(0, 0, 0, 0, 0u, nonce, 1) != kPhlWait) Fail("t23 fold 5: decided before the area's record list was complete");
    if (PieceHandLoadChoice(0, 1, 1, 0, 5u, nonce, 1) != kPhlLanded) Fail("t23 fold 5: a live record of the key did not end the hand-over");
    if (PieceHandLoadChoice(0, 1, 1, 1, nonce, nonce, 1) != kPhlLanded) Fail("t23 fold 5: the owner's removal of this placement was seeded over (the piece would come back)");
    if (PieceHandLoadChoice(0, 1, 1, 1, 9u, 0u, 1) != kPhlLanded) Fail("t23 fold 5: a removal over a placement not known was seeded over");
    if (PieceHandLoadChoice(0, 1, 1, 1, 0u, nonce, 1) != kPhlLanded) Fail("t23 fold 5: a removal mark of a placement not known (the remover's row had placement 0) was seeded over");
    if (PieceHandLoadChoice(1, 1, 1, 1, 0u, nonce, 1) != kPhlLanded) Fail("t23 fold 5: the session's hand-over did not end at a removal mark of a placement not known");
    if (PieceHandLoadChoice(0, 1, 1, 1, 9u, nonce, 1) != kPhlRemake) Fail("t23 fold 5: an earlier placement's removal mark stopped this placement's hand-over");
    if (PieceHandLoadChoice(0, 1, 0, 0, 0u, nonce, 1) != kPhlRemake) Fail("t23 fold 5: no record after a reload - the hand-over seed was not written again");
    if (PieceHandLoadChoice(0, 1, 0, 0, 0u, nonce, 0) != kPhlWait) Fail("t23 fold 5: a hand-over was made again with no piece in the save");
    if (PieceHandLoadChoice(1, 1, 0, 0, 0u, nonce, 1) != kPhlSession) Fail("t23 fold 5: the session's own hand-over row was made again");
    if (PieceHandLoadChoice(1, 1, 1, 0, 3u, nonce, 1) != kPhlLanded) Fail("t23 fold 5: the session's hand-over that landed was not seen");
    /* the wiring */
    const std::string bc = T23NoCr(T313ReadFile("../coop-plugin/build.cpp"));
    if (bc.empty()) { Fail("t23 fold 5: build.cpp did not read"); return; }
    const char* const need[] = {
        "BdCopyListHandNote(std::string(key), r);   /* T-23 review fold 5",
        "else if (c.handed != 0 && c.alias == 0) g_handLoad[c.key] = 0u;",
        "    PcrHandLoadPass();   /* T-23 review fold 5",
        "int ask = piecerec::PieceHandLoadChoice(rowHere, listDone, haveRecord, recRemoved, recPlacement, rec.nonce, 1);",
        "r.via = 3; r.own = 0; r.handed = 1; r.ownerSlot = rec.slot; r.nonce = rec.nonce; r.gen = g_bdGen;",
        "PieceRecMark(key, piecerec::kPeAgain);   /* the hand-over road writes it (PcrHandSeedRow) */",
        "a.key = r.liveKey; a.alias = 1; a.removed = 0; a.emptyLoads = 0; a.handed = 0;",
        "g_copyList.clear(); g_copyRetire.clear(); g_copyDiagOwed = 0; g_handLoad.clear();" };
    for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); ++i)
        if (bc.find(need[i]) == std::string::npos) Fail(std::string("t23 fold 5 wiring: build.cpp lacks ") + need[i]);
    /* the remade row is placed BEFORE the drain's empty check, and the seed pass for own pieces stays own-only */
    const size_t hp = bc.find("    PcrHandLoadPass();   /* T-23 review fold 5"), ep = bc.find("    if (g_pcrW.empty()) return;\n    const int slot = StoreMySlot();");
    if (hp == std::string::npos || ep == std::string::npos || hp > ep) Fail("t23 fold 5 wiring: the hand-over pass does not run before the drain's empty check");
}
/* T-1222 (owner 1444: a rock is never owned). The claim skip: only the worker job set-up's call site, only a world resource node */
void t_t1222_claim_skip()
{
    using namespace coopbuild;
    const unsigned long long claim = 0x7000A0ull;   /* any image address: the row's, in the running image */
    if (BuildIsClaimSite(claim, claim) != 1) Fail("t1222 claim: the job set-up's return address is the claim site");
    if (BuildIsClaimSite(claim + 1, claim) != 0 || BuildIsClaimSite(0x7000B0ull, claim) != 0) Fail("t1222 claim: any other caller is not the claim site");
    if (BuildIsClaimSite(0, 0) != 0 || BuildIsClaimSite(claim, 0) != 0) Fail("t1222 claim: with no row nothing is the claim site");
    if (BuildClaimSkip(1, 1) != 1) Fail("t1222 claim: a world node's claim is skipped");
    if (BuildClaimSkip(1, 0) != 0) Fail("t1222 claim: the claim of a building that is no world node (a player's mine, any other foliage piece) runs");
    if (BuildClaimSkip(0, 1) != 0 || BuildClaimSkip(0, 0) != 0) Fail("t1222 claim: another setFaction call (a zone loading, a purchase, the mod's write) is never skipped");
    /* which buildings count: coopown::WorldNodeOf - an ore rock yes; a player's mine or well (no foliage mark), anything with a
       build record, inputs, no worker, or an unreadable read no */
    if (coopown::WorldNodeOf(1, 1, 0, 0, 1) != 1) Fail("t1222 claim: an ore rock is a world node");
    if (coopown::WorldNodeOf(1, 1, 0, 0, 0) != 0) Fail("t1222 claim: a player's mine (no foliage mark) is no world node");
    if (coopown::WorldNodeOf(1, 1, 0, 1, 1) != 0) Fail("t1222 claim: a piece with a build record is no world node");
    if (coopown::WorldNodeOf(1, 1, 2, 0, 1) != 0 || coopown::WorldNodeOf(1, 0, 0, 0, 1) != 0 || coopown::WorldNodeOf(0, 1, 0, 0, 1) != 0)
        Fail("t1222 claim: a machine with inputs, no worker, or no production building is no world node");
    if (coopown::WorldNodeOf(1, 1, 0, 0, -1) != 0 || coopown::WorldNodeOf(1, -1, 0, 0, 1) != 0 || coopown::WorldNodeOf(1, 1, -1, 0, 1) != 0)
        Fail("t1222 claim: an unreadable piece is no world node (its claim runs)");
}

/* T-1222: the click gate, the cursor and the request gate - a rock for anyone; an own machine and another player's machine under
   each base-access option as before */
void t_t1222_gate_answers()
{
    using namespace swaccess;
    /* the click gate / cursor */
    if (!NodeClickOpens(0, true, 1)) Fail("t1222 gate: a rock opens at the click sites");
    if (NodeClickOpens(0, false, 1)) Fail("t1222 gate: a rock at any other caller (save, navmesh, door panel) keeps the engine's answer");
    if (NodeClickOpens(0, true, 0)) Fail("t1222 gate: a machine that is no world node goes on to the existing rules");
    if (NodeClickOpens(1, true, 1)) Fail("t1222 gate: an engine 'own' answer is kept as it is, not re-answered");
    const int pols[4] = { kPolicyUnknown, kPolicyShared, kPolicyOwner, kPolicyLocked };
    for (int i = 0; i < 4; ++i)
    {
        const int p = pols[i];
        /* a rock: every player, every option, member or not */
        if (JudgeFirst(false, 1, p) != kJudgeWorldNode || !JudgeAllows(JudgeFirst(false, 1, p))) Fail("t1222 gate: a rock is taken from under every option");
        if (JudgeFirst(true, 1, p) != kJudgeMember) Fail("t1222 gate: a member's request keeps its own rule first");
        /* a machine (no world node) */
        const int f = JudgeFirst(false, 0, p);
        if (p == kPolicyShared || p == kPolicyUnknown)
        {
            if (f != kJudgeShared || !JudgeAllows(f)) Fail("t1222 gate: under shared (or not heard yet) any player's machine opens");
            continue;
        }
        if (f != kJudgeReadOwners || JudgeAllows(f)) Fail("t1222 gate: under owner / locked a machine needs its owner read");
        /* own machine: the requester is the owner */
        const int own = JudgeAfterReads(p, true, true, false);
        if (!JudgeAllows(own) || own != (p == kPolicyOwner ? kJudgeOwner : kJudgeLockedOwner)) Fail("t1222 gate: the owner always opens their own machine");
        /* another player's machine */
        const int other = JudgeAfterReads(p, true, false, false);
        if (p == kPolicyOwner && (other != kJudgeOwnerOnly || JudgeAllows(other))) Fail("t1222 gate: under owner another player's machine is refused");
        if (p == kPolicyLocked && (other != kJudgeLockedEngineLock || !JudgeAllows(other))) Fail("t1222 gate: under locked another player's machine goes to the game's own lock");
        if (p == kPolicyLocked && (JudgeAfterReads(p, true, false, true) != kJudgeLockedRefused || JudgeAllows(kJudgeLockedRefused)))
            Fail("t1222 gate: under locked, with the stand-in read as a player, another player's machine is refused");
        /* an NPC's building: the game's own rules */
        if (JudgeAfterReads(p, false, false, false) != kJudgeNotPlayerOwned || !JudgeAllows(kJudgeNotPlayerOwned)) Fail("t1222 gate: an NPC's building keeps the game's own rules");
    }
}

/* T-1222: the wiring - the row in both tables, the detour's claim skip before anything else, and the three gates */
void t_t1222_wiring()
{
    const std::string t65 = T313ReadFile("../coop-plugin/addresses/65D604D7-0232C000-B914BEE3-02303600.txt"), t68 = T313ReadFile("../coop-plugin/addresses/6602D59D-0232D000-BBA641C8-02304800.txt");
    if (t65.find("JobClaimSetFactionRet                          ret  0035B9E4 0035B9DE FF93A0000000") == std::string::npos
        || t68.find("JobClaimSetFactionRet                          ret  0035BA44 0035BA3E FF93A0000000") == std::string::npos)
        Fail("t1222 wiring: the claim row is missing from an address table");
    const std::string b = T313ReadFile("../coop-plugin/build.cpp");
    const size_t d = b.find("void detour_setfaction(void* b, void* f, void* platoon)");
    const size_t site = (d == std::string::npos) ? d : b.find("coopbuild::BuildIsClaimSite(ret, (kBdClaimSetFactionRet != 0) ? AddrAbs(kBdClaimSetFactionRet) : 0ull)", d);
    const size_t skip = (d == std::string::npos) ? d : b.find("coopbuild::BuildClaimSkip(1, BdWorldNodeLive(b, 0))", d);
    const size_t off = (d == std::string::npos) ? d : b.find("if (BdOnMain() == 0)", d);
    if (b.find("AddrReg kBdClaimSetFactionRet_reg(\"JobClaimSetFactionRet\"") == std::string::npos || d == std::string::npos
        || site == std::string::npos || skip == std::string::npos || off == std::string::npos || site > skip || skip > off)
        Fail("t1222 wiring: the setFaction detour does not skip the claim first (before the off-main branch)");
    if (b.find("int WorldNodeLiveAnyThread(void* b) { return BdWorldNodeLive(b, 0); }") == std::string::npos) Fail("t1222 wiring: the gates' world-node test");
    const std::string p = T313ReadFile("../coop-plugin/policy.cpp");
    const size_t click = p.find("char detour_OwnedByAPlayerFaction(void* building)");
    const size_t node = (click == std::string::npos) ? click : p.find("swaccess::NodeClickOpens(a, clickGate || clickHandler,", click);
    const size_t early = (click == std::string::npos) ? click : p.find("if (!inTeam && !shared) return a;", click);
    if (node == std::string::npos || early == std::string::npos || node > early) Fail("t1222 wiring: the click gate answers a rock before the team / policy early return");
    const size_t cur = p.find("unsigned int detour_UseableCursor(void* self)");
    if (cur == std::string::npos || p.find("swaccess::NodeClickOpens(0, a != kCursorTypeEightNoContainer,", cur) == std::string::npos) Fail("t1222 wiring: the cursor answers a rock");
    const size_t j = p.find("bool PolicyJudge(void* building, const std::string& requesterFactionSid, bool count, const char** rule)");
    if (j == std::string::npos || p.find("swaccess::JudgeFirst(member, member ? 0 : coop::WorldNodeLiveAnyThread(building), pol)", j) == std::string::npos
        || p.find("swaccess::JudgeAfterReads(pol, ownerIsPlayerSide, isOwner,", j) == std::string::npos)
        Fail("t1222 wiring: the request gate does not judge by the kit's rule order");
}

/* T-1222 (owner 1444): the left click's window on an ore rock - opened by the detour only for a built world node whose window the
   engine's own gate did not open; every combination of the three reads */
void t_t1222_left_open()
{
    using namespace swaccess;
    if (!NodeLeftOpens(1, 1, 0)) Fail("t1222 left: a built rock the engine opened nothing for opens");
    if (!NodeLeftOpens(1, 1, -1)) Fail("t1222 left: a built rock whose engine gate was unread opens (a second show only shows it again)");
    if (NodeLeftOpens(1, 1, 1)) Fail("t1222 left: a rock the engine's own gate opened is not opened a second time");
    if (NodeLeftOpens(0, 1, 0) || NodeLeftOpens(-1, 1, 0)) Fail("t1222 left: a building that is no world node (or unread) keeps the engine's select alone");
    if (NodeLeftOpens(1, 0, 0) || NodeLeftOpens(1, -1, 0)) Fail("t1222 left: an unbuilt rock (or its finished byte unread) opens nothing, as the engine");
    const int v[3] = { -1, 0, 1 };
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
            for (int c = 0; c < 3; ++c)
                if (NodeLeftOpens(v[a], v[b], v[c]) != (v[a] == 1 && v[b] == 1 && v[c] != 1))
                    Fail("t1222 left: the rule at worldNode " + I(v[a]) + " built " + I(v[b]) + " gate " + I(v[c]));
}

/* T-1222: the left click's wiring - both rows in both tables, the select virtual hooked (strict) and the window call only called,
   the engine's select first, then the world-node test, the rule and the call with the building + 0x58, installed by InstallPolicy */
void t_t1222_left_wiring()
{
    const std::string t65 = T313ReadFile("../coop-plugin/addresses/65D604D7-0232C000-B914BEE3-02303600.txt"), t68 = T313ReadFile("../coop-plugin/addresses/6602D59D-0232D000-BBA641C8-02304800.txt");
    if (t65.find("BuildingOnSelect                               code 0054FA10 0054FA10 4C8BDC574881EC8000000048C7442440") == std::string::npos
        || t68.find("BuildingOnSelect                               code 005504A0 005504A0 4C8BDC574881EC8000000048C7442440") == std::string::npos)
        Fail("t1222 left wiring: the select virtual's row is missing from an address table");
    if (t65.find("ForgottenGuiBuildingWindow                     code 006E5EA0 006E5EA0 48895C241048896C2418488974242057") == std::string::npos
        || t68.find("ForgottenGuiBuildingWindow                     code 006E6640 006E6640 48895C241048896C2418488974242057") == std::string::npos)
        Fail("t1222 left wiring: the window call's row is missing from an address table");
    const std::string p = T313ReadFile("../coop-plugin/policy.cpp");
    if (p.find("static AddrReg kBuildingOnSelectRva_reg(\"BuildingOnSelect\", &kBuildingOnSelectRva);") == std::string::npos)
        Fail("t1222 left wiring: the select virtual is registered strict (it is hooked)");
    if (p.find("static AddrReg kNodeWindowRva_reg(\"ForgottenGuiBuildingWindow\", &kNodeWindowRva, kAddrUseCallOnly);") == std::string::npos)
        Fail("t1222 left wiring: the window call is registered call-only");
    if (p.find("const size_t kNodeHandOff = 0x58;") == std::string::npos
        || p.find("((NodeWindowFn)(g_base + (uintptr_t)kNodeWindowRva))((void*)(g_base + (uintptr_t)kNodeGuiRva), (char*)b + kNodeHandOff)") == std::string::npos)
        Fail("t1222 left wiring: the window call takes the GUI object and the building + 0x58, as the select virtual passes them");
    const size_t d = p.find("void detour_BuildingOnSelect(void* b)");
    const size_t orig = (d == std::string::npos) ? d : p.find("orig_BuildingOnSelect(b);", d);
    const size_t node = (d == std::string::npos) ? d : p.find("const int node = coop::WorldNodeLiveAnyThread(b);", d);
    const size_t rule = (d == std::string::npos) ? d : p.find("if (!swaccess::NodeLeftOpens(node, BuiltBytePod(b), gate)) return;", d);
    const size_t call = (d == std::string::npos) ? d : p.find("const int shown = NodeWindowPod(b, &win);", d);
    if (d == std::string::npos || orig == std::string::npos || node == std::string::npos || rule == std::string::npos || call == std::string::npos
        || orig > node || node > rule || rule > call || p.find("const int gate = SelectGatePod(b);", d) > rule)
        Fail("t1222 left wiring: the detour runs the engine's select first, then the world-node test, the gate, the rule and the call");
    const size_t inst = p.find("\nvoid InstallWorldNodeLeftOpen()\r\n{") != std::string::npos ? p.find("\nvoid InstallWorldNodeLeftOpen()\r\n{") : p.find("\nvoid InstallWorldNodeLeftOpen()\n{");   /* the definition, not the declaration */
    if (inst == std::string::npos
        || p.find("coop::AddHook((void*)(g_base + kBuildingOnSelectRva), (void*)&detour_BuildingOnSelect, (void**)&orig_BuildingOnSelect)", inst) == std::string::npos)
        Fail("t1222 left wiring: the select virtual is hooked by InstallWorldNodeLeftOpen");
    const size_t ip = p.find("\nvoid InstallPolicy()\r\n{") != std::string::npos ? p.find("\nvoid InstallPolicy()\r\n{") : p.find("\nvoid InstallPolicy()\n{");
    if (ip == std::string::npos || p.find("    InstallWorldNodeLeftOpen();", ip) == std::string::npos || p.find("    InstallWorldNodeLeftOpen();", ip) > inst)
        Fail("t1222 left wiring: InstallPolicy installs it");
    if (p.find("basePolicyWorldNode[allowed,click,cursor,leftOpen,leftHook,leftOffMain,leftFault]=") == std::string::npos)
        Fail("t1222 left wiring: the report line counts the opens");
}

/* T-1222 (owner 1452): ore taken from a rock into this player's own things is no theft - 'stealing' is cleared only for a world node's
   window against this player's own (either order); the engine's 'not stealing' is kept; every other pair is left to the kit's rule */
void t_t1222_steal_take()
{
    using namespace swaccess;
    if (!NodeTakeClearsStealing(1, 1, kNotPlayer, 0, kOwn)) Fail("t1222 steal: the rock's window against this player's own is cleared");
    if (!NodeTakeClearsStealing(1, 0, kOwn, 1, kNotPlayer)) Fail("t1222 steal: the same with the windows the other way round");
    if (NodeTakeClearsStealing(0, 1, kNotPlayer, 0, kOwn)) Fail("t1222 steal: the engine's 'not stealing' is kept as it is");
    if (NodeTakeClearsStealing(1, 0, kNotPlayer, 0, kOwn) || NodeTakeClearsStealing(1, -1, kNotPlayer, 0, kOwn))
        Fail("t1222 steal: a box or machine that is no world node (or unread) keeps the engine's answer");
    if (NodeTakeClearsStealing(1, 1, kNotPlayer, 0, kTeam) || NodeTakeClearsStealing(1, 1, kNotPlayer, 0, kStranger)
        || NodeTakeClearsStealing(1, 1, kNotPlayer, 0, kNotPlayer))
        Fail("t1222 steal: a rock against anyone but this player's own (a teammate, a stranger, an NPC) keeps the engine's answer");
    const int nodes[3] = { -1, 0, 1 };
    const int whos[6] = { kOwn, kTeam, kFriendAllowed, kFriendRefused, kStranger, kNotPlayer };
    for (int e = 0; e < 2; ++e)
        for (int na = 0; na < 3; ++na)
            for (int wa = 0; wa < 6; ++wa)
                for (int nb = 0; nb < 3; ++nb)
                    for (int wb = 0; wb < 6; ++wb)
                    {
                        const bool want = e != 0 && ((nodes[na] == 1 && whos[wb] == kOwn) || (nodes[nb] == 1 && whos[wa] == kOwn));
                        if (NodeTakeClearsStealing(e, nodes[na], whos[wa], nodes[nb], whos[wb]) != want)
                            Fail("t1222 steal: the rule at engine " + I(e) + " node " + I(nodes[na]) + "/" + I(nodes[nb]) + " who " + I(whos[wa]) + "/" + I(whos[wb]));
                    }
}

/* T-1222: the stealing decision's wiring - both windows' owners read, the world-node rule and its counter before the kit table's
   early return (single player and an empty table clear it too), the world-node read only on the side facing this player's own, the
   counter on the report line */
void t_t1222_steal_wiring()
{
    const std::string p = T313ReadFile("../coop-plugin/policy.cpp");
    const size_t d = p.find("char detour_StealFlag(void* clicked, void* other)");
    const size_t first = (d == std::string::npos) ? d : p.find("if (a == 0 || clicked == other) return a;", d);
    const size_t owners = (d == std::string::npos) ? d : p.find("if (KitWindowOwnerPod(clicked, &oa, &fa) != 1 || KitWindowOwnerPod(other, &ob, &fb) != 1) return a;", d);
    const size_t na = (d == std::string::npos) ? d : p.find("const int na = (wb == swaccess::kOwn) ? coop::WorldNodeLiveAnyThread(oa) : 0;", d);
    const size_t nb = (d == std::string::npos) ? d : p.find("const int nb = (wa == swaccess::kOwn) ? coop::WorldNodeLiveAnyThread(ob) : 0;", d);
    const size_t rule = (d == std::string::npos) ? d : p.find("if (swaccess::NodeTakeClearsStealing(a, na, wa, nb, wb))", d);
    const size_t count = (d == std::string::npos) ? d : p.find("::InterlockedIncrement64(&g_stealWorldNode);", d);
    const size_t early = (d == std::string::npos) ? d : p.find("if (g_kitN == 0) return a;", d);
    const size_t kit = (d == std::string::npos) ? d : p.find("const int cls = swaccess::PairClass(wa, wb);", d);
    if (d == std::string::npos || first == std::string::npos || owners == std::string::npos || na == std::string::npos || nb == std::string::npos
        || rule == std::string::npos || count == std::string::npos || early == std::string::npos || kit == std::string::npos
        || first > owners || owners > na || na > rule || nb > rule || rule > count || count > early || early > kit)
        Fail("t1222 steal wiring: the world-node rule sits after the owner reads and before the kit table's early return");
    if (p.find("if (a == 0 || clicked == other || g_kitN == 0) return a;", d) != std::string::npos)
        Fail("t1222 steal wiring: the old early return (which skipped single player) is gone");
    if (p.find("*fac = ((KitGetFn)((*(void***)o)[0x58 / 8]))(o);") == std::string::npos || p.find("void* o = ((KitGetFn)((*(void***)win)[0x80 / 8]))(win);") == std::string::npos)
        Fail("t1222 steal wiring: the window's owner is its vt+0x80 and the faction that owner's vt+0x58, as the engine reads them");
    if (p.find(" kitStealWorldNode=\" + PN((long long)g_stealWorldNode)") == std::string::npos)
        Fail("t1222 steal wiring: the report line counts the cleared rock moves");
}

/* T-1222 TEST-ONLY lever claimskip: the claim skip is bypassed only while the DEV switch is off (default on); the switch, its only
   writer, its declaration and the verb exist in a DEV build only. Line ends are dropped first (CRLF-safe) */
static std::string T1222NoCr(const char* path)
{
    std::string s = T313ReadFile(path);
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
}
static bool T1222InDevBlock(const std::string& s, size_t at)   /* the nearest directive above `at` is #ifdef SW_DEV_BUILD */
{
    if (at == std::string::npos) return false;
    const size_t ifd = s.rfind("#ifdef SW_DEV_BUILD", at);
    const size_t end = s.rfind("#endif", at);
    const size_t els = s.rfind("#else", at);
    return ifd != std::string::npos && (end == std::string::npos || end < ifd) && (els == std::string::npos || els < ifd);
}
void t_t1222_claimskip_lever()
{
    const std::string b = T1222NoCr("../coop-plugin/build.cpp");
    if (b.find("volatile long g_ocClaimSkipOff = 0;") == std::string::npos) Fail("t1222 claimskip: the switch starts on (off flag 0)");
    size_t n = 0, w = 0;   /* its address taken once (the lever's exchange), assigned once (the declaration) */
    for (size_t i = b.find("&g_ocClaimSkipOff"); i != std::string::npos; i = b.find("&g_ocClaimSkipOff", i + 1)) ++n;
    for (size_t i = b.find("g_ocClaimSkipOff = "); i != std::string::npos; i = b.find("g_ocClaimSkipOff = ", i + 1)) ++w;
    if (n != 1 || w != 1)
        Fail("t1222 claimskip: the lever is the switch's only writer");
    const size_t d = b.find("void detour_setfaction(void* b, void* f, void* platoon)");
    const size_t site = (d == std::string::npos) ? d : b.find("coopbuild::BuildIsClaimSite(ret, (kBdClaimSetFactionRet != 0) ? AddrAbs(kBdClaimSetFactionRet) : 0ull) == 1", d);
    const size_t skip = (d == std::string::npos) ? d : b.find("&& coopbuild::BuildClaimSkip(1, BdWorldNodeLive(b, 0)) == 1)\n    {\n"
        "        if (g_ocClaimSkipOff == 0) { ::InterlockedIncrement64(&g_ocClaimSkipped); return; }\n"
        "        ::InterlockedIncrement64(&g_ocClaimSkipBypassed);", d);
    const size_t off = (d == std::string::npos) ? d : b.find("if (BdOnMain() == 0)", d);
    if (d == std::string::npos || site == std::string::npos || skip == std::string::npos || off == std::string::npos || site > skip || skip > off)
        Fail("t1222 claimskip: the detour returns (skips the claim) only while the switch is on, and with it off counts and goes on to the engine's own claim");
    const size_t lever = b.find("int ClaimSkipLever(int set)\n{");
    if (!T1222InDevBlock(b, lever) || b.find("if (set == 0 || set == 1) ::InterlockedExchange(&g_ocClaimSkipOff, set == 0 ? 1 : 0);", lever) == std::string::npos
        || b.find("[MINE] claimskip %s%s: %s; claimSkipped=%lld claimSkipBypassed=%lld", lever) == std::string::npos)
        Fail("t1222 claimskip: the lever (DEV build only) sets off = 1 for 0, on = 0 for 1, and writes one [MINE] line");
    if (b.find("suppressed,claimSkipped,claimSkipOff,claimSkipBypassed]") == std::string::npos || b.find("(g_ocClaimSkipOff != 0) ? 1 : 0,") == std::string::npos)
        Fail("t1222 claimskip: the p18 report line carries the switch state and the bypass count");
    const std::string h = T1222NoCr("../coop-plugin/policy.h");
    if (!T1222InDevBlock(h, h.find("int ClaimSkipLever(int set);"))) Fail("t1222 claimskip: the declaration is DEV-only");
    const std::string c = T1222NoCr("../coop-plugin/command_channel.cpp");
    const size_t v = c.find("else if (verb == \"claimskip\")");
    if (!T1222InDevBlock(c, v) || c.find("coop::ClaimSkipLever(a == \"off\" ? 0 : (a == \"on\" ? 1 : -1))", v) == std::string::npos)
        Fail("t1222 claimskip: the verb is DEV-only and maps off -> 0, on -> 1, show -> -1");
}

/* T-1191: the bfac line's pure form - nofac from the own field, the gate's answer from its two inputs, the names cleaned */
void t_t1191_bfac_text()
{
    rcgive::BFac f;
    if (rcgive::BFacNofac(f) != -1 || rcgive::BFacGateIn(f) != -1) Fail("all unread: nofac -1, gateIn -1");
    f.own = 0; f.resolved = 1; f.controller = 0; f.built = 1; f.local = 0; f.standIn = -1; f.id = "nofac-id"; f.name = "Nobody";
    if (rcgive::BFacNofac(f) != 1) Fail("own field empty, a faction resolved: nofac 1");
    if (rcgive::BFacGateIn(f) != 0) Fail("no controller: gateIn 0");
    f.own = 1; f.controller = 1; f.local = 1;
    if (rcgive::BFacNofac(f) != 0 || rcgive::BFacGateIn(f) != 1) Fail("own field set, finished, controller: nofac 0, gateIn 1");
    f.built = 0;
    if (rcgive::BFacGateIn(f) != 0) Fail("not finished: gateIn 0");
    f.built = -1;
    if (rcgive::BFacGateIn(f) != -1) Fail("finished unread, controller 1: gateIn -1");
    f.built = 1; f.resolved = 0; f.controller = -1;
    if (rcgive::BFacGateIn(f) != 0) Fail("no faction resolved: gateIn 0");
    f.own = 0;
    if (rcgive::BFacNofac(f) != -1) Fail("own empty but nothing resolved: nofac -1");
    rcgive::BFac g;
    g.own = 1; g.resolved = 1; g.local = 1; g.controller = 1; g.standIn = -1; g.built = 1; g.id = "123-x.mod"; g.name = "My 'Crew'\n";
    const std::string t = rcgive::BFacText(g);
    if (t != "faction 'My ?Crew??' id '123-x.mod' own=1 nofac=0 resolved=1 localPlayer=1 controller=1 standIn=-1 built=1 gateIn=1")
        Fail("the line: " + t);
    if (rcgive::BFacClean(std::string(100, 'a')).size() != 48) Fail("a name cut at 48");
}

/* T-1077: the hover's filter on this game's copy of an NPC another game drives - the copy's own event-1 (greeting) list is not a
   refusal (the copy's dialogue set follows its birth order in its squad, which differs from the NPC's game); dead and player faction
   still refuse here; the NPC's game's own answer keeps the full test. */
void t_t1077_copy_may_ask()
{
    using namespace cooptalk;
    if (TalkCursorCopyMayAsk(0, 0, 0) != 1) Fail("T-1077: a living non-player copy with NO greeting list is asked (Hive Trader)");
    if (TalkCursorCopyMayAsk(1, 0, 0) != 1) Fail("T-1077: a living non-player copy with a greeting list is asked");
    if (TalkCursorCopyMayAsk(0, 1, 0) != 0 || TalkCursorCopyMayAsk(1, 1, 0) != 0) Fail("T-1077: a dead copy is refused here");
    if (TalkCursorCopyMayAsk(0, 0, 1) != 0 || TalkCursorCopyMayAsk(1, 0, 1) != 0) Fail("T-1077: a player-faction copy is refused here");
    if (TalkCursorHover(TalkCursorCopyMayAsk(0, 0, 0), 1, kTalkCursorTalk) != kTalkHoverTalk)
        Fail("T-1077: a list-less copy whose game answered talk shows the talk cursor");
    if (TalkCursorUse(TalkCursorHover(TalkCursorCopyMayAsk(0, 0, 0), 1, kTalkCursorTalk), 1) != kTalkUseTrue)
        Fail("T-1077: ... and its click gives the talk order");
    if (TalkCursorHover(TalkCursorCopyMayAsk(0, 0, 0), 1, kTalkCursorNo) != kTalkHoverNo) Fail("T-1077: its game's no stays no");
    /* the NPC's game (and an NPC this game drives) keeps the list as a refusal */
    if (TalkCursorMayTalk(0, 0, 0) != 0 || TalkCursorNpcAnswer(0, 0, 0, 0, 1) != kTalkCursorNo)
        Fail("T-1077: the NPC's own game still refuses an NPC with no greeting list");
}

/* T-1077 wiring (CRLF-safe): TcHover filters with the copy rule, the NPC's game's answer and the preload keep the full test, an NPC
   this game drives never reaches TcHover, and the show lever mirrors the hover. */
void t_t1077_wiring()
{
    const std::string s = T1222NoCr("../coop-plugin/speech.cpp");
    const size_t h = s.find("bool TcHover(void* dlg, void* npc)\n{");
    const size_t hEnd = (h == std::string::npos) ? std::string::npos : s.find("\n}\n", h);
    if (h == std::string::npos || hEnd == std::string::npos) { Fail("t1077: TcHover not found"); return; }
    const std::string hv = s.substr(h, hEnd - h);
    const size_t never = hv.find("if (!TcNeverPod(dlg, npc, &hasList, &list, &dead, &pf)) { ::InterlockedIncrement64(&g_tcLocalKept); return orig_tcCanTalk(dlg); }");
    const size_t filt = hv.find("if (!cooptalk::TalkCursorCopyMayAsk(hasList, dead, pf)) { ++g_tcFilteredNever; return false; }");
    const size_t ask = hv.find("if (cooptalk::TalkCursorShouldAsk(fresh, have, TcPending(nu, tu))) TcQueue(nu, tu, 0);");
    if (never == std::string::npos || filt == std::string::npos || ask == std::string::npos || !(never < filt && filt < ask))
        Fail("t1077: TcHover reads the copy, filters with TalkCursorCopyMayAsk, then asks - in that order");
    if (hv.find("TalkCursorMayTalk(") != std::string::npos) Fail("t1077: TcHover no longer filters on the copy's greeting list");
    const size_t d = s.find("bool detour_tcCanTalk(void* dlg)");
    if (d == std::string::npos
        || s.find("if (npc == 0 || TkS6Owner(npc) != 0 || TkPeerTarget(npc)) return orig_tcCanTalk(dlg);", d) == std::string::npos
        || s.find("return TcHover(dlg, npc);", d) == std::string::npos)
        Fail("t1077: an NPC this game drives (or a player's character) keeps the engine's own test - TcHover only for another game's NPC");
    const size_t a = s.find("void TalkCursorApplyAsk(const cooptalk::TalkMsg& m, unsigned int peer)");
    if (a == std::string::npos || s.find("if (!parked && cooptalk::TalkCursorMayTalk(hasList, dead, pf))", a) == std::string::npos)
        Fail("t1077: the NPC's game answers with the full test on its own NPC");
    if (s.find("|| !cooptalk::TalkCursorMayTalk(hasList, dead, pf)) continue;") == std::string::npos)
        Fail("t1077: the preload keeps the copy's list test (the hover asks for the rest)");
    if (s.find("const int may = read ? ((own == 0 && !player) ? cooptalk::TalkCursorCopyMayAsk(hasList, dead, pf) : cooptalk::TalkCursorMayTalk(hasList, dead, pf)) : 0;") == std::string::npos)
        Fail("t1077: talkcursor show mirrors the hover's filter");
}

/* T-1061: A CHEST'S LOCK ON ITS RECORD (boxrec.h A CHEST'S LOCK) - the lock block with and without, an unknown flag, the order of the
   tails, the encoder's refusals; who lays what; where a pick goes; the writer's answer; the BOX_LOCK message; the protocol pins */
void t_t1061_lock_record()
{
    using namespace coopbrec;
    if (kProtocol != 145u || kBrFlagLock != 16u || kBrLockBytes != 10u) Fail("t1061: the lock block is not flag 16 / 10 bytes of world-server protocol 145");
    const std::string key = "12-box.mod@3,41@1003,-205,100";
    std::vector<cooppar::ParityItem> items;
    items.push_back(T823BoxItem("katana", 0, 0, 1));
    BoxHeader h; h.flags = kBrFlagSeed; h.generation = 3; h.writerSlot = 1; h.kind = kBrKindBox; h.ownerSlot = -1; h.key = key;
    std::vector<char> plain, locked;
    if (!EncodeBoxRecord(&plain, h, items) || plain.empty()) { Fail("t1061: a record with no lock did not encode"); return; }
    {   /* no lock block: the old form, read back with no lock */
        BoxHeader hd; std::vector<cooppar::ParityItem> back;
        if (DecodeBoxRecord(&plain[0], plain.size(), &hd, &back) != kBrOk || hd.hasLock != 0 || (hd.flags & kBrFlagLock) != 0) Fail("t1061: a record with no lock block read as one with a lock");
    }
    h.hasLock = 1; h.lock.locked = 1; h.lock.broken = 0; h.lock.level = 57; h.lock.hardness = 2.5f;
    if (!EncodeBoxRecord(&locked, h, items)) { Fail("t1061: a record with a lock did not encode"); return; }
    if (locked.size() != plain.size() + kBrLockBytes) Fail("t1061: the lock block is not 10 bytes at the record's end");
    {
        BoxHeader hd; std::vector<cooppar::ParityItem> back;
        if (DecodeBoxRecord(&locked[0], locked.size(), &hd, &back) != kBrOk || hd.hasLock != 1 || (hd.flags & kBrFlagLock) == 0
            || hd.lock.locked != 1 || hd.lock.broken != 0 || hd.lock.level != 57 || hd.lock.hardness != 2.5f || back.size() != 1)
            Fail("t1061: the lock block did not read back whole");
        BoxHeader hh;
        if (DecodeBoxHeader(&locked[0], locked.size(), &hh) != kBrOk || hh.hasLock != 0) Fail("t1061: the header alone (the world server's read) refused the lock flag or read a lock");
    }
    {   /* cut short, a byte too many, a value out of range: refused whole */
        std::vector<char> c(locked.begin(), locked.end() - 1);
        if (DecodeBoxRecord(&c[0], c.size(), 0, 0) != kBrBadLock) Fail("t1061: a lock block cut short was read");
        std::vector<char> t(locked); t.push_back(0);
        if (DecodeBoxRecord(&t[0], t.size(), 0, 0) != kBrTrailing) Fail("t1061: a byte after the lock block was read");
        std::vector<char> v(locked); v[v.size() - kBrLockBytes] = 2;
        if (DecodeBoxRecord(&v[0], v.size(), 0, 0) != kBrBadLock) Fail("t1061: a locked byte of 2 was read");
        if (std::string(BoxDecodeWhy(kBrBadLock)) == "malformed") Fail("t1061: the bad-lock result has no words");
    }
    {   /* an unknown flag (what a protocol-144 reader sees in flag 16): the record does not read at all */
        std::vector<char> u(plain);
        const size_t flagsAt = (size_t)kBrFixedBytes + key.size();
        unsigned int f = 0; std::memcpy(&f, &u[flagsAt], 4); f |= 32u; std::memcpy(&u[flagsAt], &f, 4);
        if (DecodeBoxRecord(&u[0], u.size(), 0, 0) != kBrBadHeader) Fail("t1061: a record with an unknown flag was read");
        BoxHeader old; old.key = key; old.flags = 32u;
        if (BoxHeaderValid(old)) Fail("t1061: the header check took an unknown flag");
    }
    {   /* the tails' order: undo list, ledger, lock block - a shop's record with all three reads back */
        BoxHeader s = h; s.kind = kBrKindShop; s.flags = 0;
        BoxUndoKey k; k.asker = 1; k.epoch = 2; k.id = 3; s.undos.push_back(k);
        BoxLedger led; led.line = 5ULL;
        std::vector<char> all;
        if (!EncodeBoxRecord(&all, s, items, &led)) { Fail("t1061: a shop's record with undos, ledger and lock did not encode"); return; }
        BoxHeader hd; BoxLedger lb;
        if (DecodeBoxRecord(&all[0], all.size(), &hd, 0, &lb) != kBrOk || hd.hasLock != 1 || hd.lock.level != 57 || hd.undos.size() != 1 || lb.line != 5ULL)
            Fail("t1061: the three tails did not read back in their order");
    }
    {   /* the encoder's refusals: a lock out of range, a lock on a cage */
        std::vector<char> o;
        BoxHeader b = h; b.lock.level = -1;
        if (EncodeBoxRecord(&o, b, items) || !o.empty()) Fail("t1061: a lock level of -1 was written");
        b = h; b.lock.locked = 2;
        if (EncodeBoxRecord(&o, b, items)) Fail("t1061: a locked byte of 2 was written");
        b = h; b.lock.hardness = std::numeric_limits<float>::quiet_NaN();
        if (EncodeBoxRecord(&o, b, items)) Fail("t1061: a hardness that is not a number was written");
        b = h; b.kind = kBrKindCage;
        if (EncodeBoxRecord(&o, b, items)) Fail("t1061: a cage's record carried a lock block (its lock is the prisoner road's)");
    }
}
void t_t1061_lock_rules()
{
    using namespace coopbrec;
    /* who lays what: (hasBlock, liveHasLock, liveSame, writer, needed, askPending) */
    if (BoxLockApplyRule(1, 0, 0, 0, 0, 0) != kBlkNothing || BoxLockApplyRule(1, 0, 0, 1, 1, 0) != kBlkNothing) Fail("t1061 rules: a box with no lock object was laid");
    if (BoxLockApplyRule(1, 1, 0, 0, 0, 0) != kBlkLay) Fail("t1061 rules: a game that does not write the box did not lay the record's lock");
    if (BoxLockApplyRule(1, 1, 1, 0, 0, 0) != kBlkNothing) Fail("t1061 rules: a lock already the record's was laid again");
    if (BoxLockApplyRule(1, 1, 0, 0, 0, 1) != kBlkNothing) Fail("t1061 rules: the record's lock was laid over this game's own pick waiting for the writer");
    if (BoxLockApplyRule(0, 1, 0, 0, 0, 0) != kBlkNothing) Fail("t1061 rules: a record with no lock block changed a lock on a game that does not write the box");
    if (BoxLockApplyRule(1, 1, 0, 1, 1, 0) != kBlkLay) Fail("t1061 rules: the writer's newer record did not beat the save's lock");
    if (BoxLockApplyRule(1, 1, 0, 1, 0, 0) != kBlkMark) Fail("t1061 rules: the writer's own unwritten lock change was not marked");
    if (BoxLockApplyRule(0, 1, 0, 1, 0, 0) != kBlkMark) Fail("t1061 rules: a record from before the lock block was not marked on its writer");
    if (BoxLockApplyRule(0, 1, 0, 1, 1, 0) != kBlkNothing) Fail("t1061 rules: the writer marked a box before its newer record went in");
    if (BoxLockApplyRule(1, 1, 1, 1, 0, 0) != kBlkNothing) Fail("t1061 rules: the writer marked a box whose lock is its record's");
    /* where a pick goes: (verdict, linked, sent, waitedMs, capMs) */
    if (BoxLockPickRoute(1, 1, 0, 0, 15000) != kBlrWrite || BoxLockPickRoute(1, 0, 0, 0, 15000) != kBlrWrite) Fail("t1061 route: the writer's own pick was not marked for its record");
    if (BoxLockPickRoute(2, 1, 0, 0, 15000) != kBlrAsk) Fail("t1061 route: a pick on another game's box was not asked of the writer");
    if (BoxLockPickRoute(2, 1, 1, 100, 15000) != kBlrWait || BoxLockPickRoute(2, 1, 1, 15000, 15000) != kBlrDone) Fail("t1061 route: an asked pick did not wait for the writer's answer, then end");
    if (BoxLockPickRoute(4, 1, 0, 100, 15000) != kBlrWait || BoxLockPickRoute(4, 1, 0, 15000, 15000) != kBlrRelock) Fail("t1061 route: a pick with no writer known did not wait, then relock");
    if (BoxLockPickRoute(3, 1, 0, 15000, 15000) != kBlrRelock) Fail("t1061 route: a pick on a box nobody writes was kept open");
    if (BoxLockPickRoute(2, 0, 0, 0, 15000) != kBlrNothing) Fail("t1061 route: a game alone asked a writer");
    if (BoxLockPickRoute(0, 1, 0, 0, 15000) != kBlrNothing || BoxLockPickRoute(-1, 1, 0, 0, 15000) != kBlrNothing) Fail("t1061 route: a lock that is not a box's went to the record road");
    /* the writer's answer */
    if (BoxLockAskTake(1, 1) != 1 || BoxLockAskTake(0, 1) != 0 || BoxLockAskTake(1, 0) != 0) Fail("t1061 ask: the writer took an ask it does not write, or refused its own box");
    /* fold 3, a new record re-arms the lock lay: (hasBlock, recWriter, mySlot, applyDone, haveMine, mineGen, gen) */
    if (BoxLockRearm(1, 2, 1, 1, 1, 5u, 6u) != 1) Fail("t1061 rearm: another writer's newer record did not re-arm the lay on a game whose apply was done");
    if (BoxLockRearm(1, 2, 1, 1, 0, 0u, 1u) != 1) Fail("t1061 rearm: another writer's record did not re-arm the lay on a box that holds no record yet");
    if (BoxLockRearm(1, 1, 1, 1, 1, 5u, 6u) != 0) Fail("t1061 rearm: this game's own write coming back re-armed the lay");
    if (BoxLockRearm(1, 2, 1, 1, 1, 6u, 6u) != 0 || BoxLockRearm(1, 2, 1, 1, 1, 7u, 6u) != 0) Fail("t1061 rearm: a record not newer than the box re-armed the lay");
    if (BoxLockRearm(0, 2, 1, 1, 1, 5u, 6u) != 0) Fail("t1061 rearm: a record with no lock block re-armed the lay");
    if (BoxLockRearm(1, 2, 1, 0, 1, 5u, 6u) != 0) Fail("t1061 rearm: a box whose apply is not done was re-armed (its ordinary apply lays the lock)");
    if (BoxLockRearm(1, -1, 1, 1, 1, 5u, 6u) != 0) Fail("t1061 rearm: a record naming no writer re-armed the lay");
    /* fold 1, the cage test the lock road shares with cagetest: a cage by its record's name, a chest is not one */
    if (!joblever::CageNameMatches("Prisoner Cage") || joblever::CageNameMatches("Storage Chest") || joblever::CageNameMatches("Shop counter"))
        Fail("t1061 cage: the lock road's cage test does not tell a cage from a chest");
}
void t_t1061_lock_msg()
{
    using namespace coopbrec;
    if (kMsgBoxLock != 85u || cooplive::kInnerBoxLock != 85u || kGameProtocolBoxLock != 207u || hovrec::kGameProtocol != 213u) Fail("t1061 msg: BOX_LOCK is not message 85 of game-to-game protocol 207");
    if (!cooplive::LiveInnerAccepted(cooplive::kInnerBoxLock) || !cooplive::LiveInnerWaitsForWorld(cooplive::kInnerBoxLock)) Fail("t1061 msg: BOX_LOCK is refused from the world server or dropped while the world loads");
    if (cooplive::AddrGroupOf(cooplive::kInnerBoxLock) != cooplive::kAddrGroupItem) Fail("t1061 msg: BOX_LOCK is not counted with the item road");
    BoxLockMsg m; m.kind = kBlmAsk; m.key = "12-box.mod@3,41@1003,-205,100"; m.lock.locked = 0; m.lock.level = 40; m.lock.hardness = 1.5f;
    std::vector<char> b;
    if (!EncodeBoxLockMsg(&b, m) || b.size() != 1u + 4u + m.key.size() + kBrLockBytes) { Fail("t1061 msg: an ASK did not encode to its bytes"); return; }
    BoxLockMsg r;
    if (!DecodeBoxLockMsg(&b[0], b.size(), &r) || r.kind != kBlmAsk || r.key != m.key || r.lock.level != 40 || r.lock.locked != 0 || r.lock.hardness != 1.5f) Fail("t1061 msg: an ASK did not read back");
    m.kind = kBlmNo; m.lock.locked = 1;
    if (!EncodeBoxLockMsg(&b, m) || !DecodeBoxLockMsg(&b[0], b.size(), &r) || r.kind != kBlmNo || r.lock.locked != 1) Fail("t1061 msg: a NO did not read back");
    std::vector<char> t(b); t.push_back(0);
    if (DecodeBoxLockMsg(&t[0], t.size(), 0)) Fail("t1061 msg: a byte after the lock was read");
    std::vector<char> s(b.begin(), b.end() - 1);
    if (DecodeBoxLockMsg(&s[0], s.size(), 0)) Fail("t1061 msg: a message cut short was read");
    std::vector<char> k(b); k[0] = 3;
    if (DecodeBoxLockMsg(&k[0], k.size(), 0)) Fail("t1061 msg: an unknown kind was read");
    BoxLockMsg bad = m; bad.key = "not a key";
    if (EncodeBoxLockMsg(&b, bad)) Fail("t1061 msg: a key that is not a position key was written");
    bad = m; bad.kind = 0;
    if (EncodeBoxLockMsg(&b, bad)) Fail("t1061 msg: kind 0 was written");
}
void t_t1061_wiring()
{
    const std::string it = T23NoCr(T313ReadFile("../coop-plugin/items.cpp"));
    const std::string se = T23NoCr(T313ReadFile("../coop-plugin/net/session.cpp"));
    const std::string dr = T23NoCr(T313ReadFile("../coop-plugin/doors.cpp"));
    const std::string sm = T23NoCr(T313ReadFile("../coop-store/store_main.cpp"));
    const std::string st = T23NoCr(T313ReadFile("../coop-plugin/store.cpp"));
    if (it.empty() || se.empty() || dr.empty() || sm.empty() || st.empty()) { Fail("t1061 wiring: a source file did not read"); return; }
    const char* const needIt[] = {
        "if (BoxLockReadLive(b, &h.lock) != 0) { h.hasLock = 1; ++g_blkRead; }",   /* the writer's every write carries its lock */
        "if (v != kBoxMine) BoxLockAtApply(id, key, kh, b, 0, 0);",                      /* a game that does not write the box lays the lock only */
        "BoxLockAtApply(id, key, kh, b, 1, 0);",                                          /* the writer's own unwritten change is marked */
        "BoxLockAtApply(id, key, kh, b, 1, 1);",                                          /* the writer's newer record beats the save */
        "BoxLockRecordIn(id, slot.h);",                                              /* a kept record ends a pick's wait and wakes the lay */
        "BoxLockPickDrain((DWORD)now);",                                             /* the picks, each tick */
        "net::SendBoxLockAsk(bytes, kp.sx, kp.sy)",
        "net::SendBoxLockNo(bytes, fromPeer)",
        "*cage = joblever::CageNameMatches(nm) != 0 ? 1 : 0;",                         /* fold 1: the cage test is the project's own, by name */
        "if (ItBoxInventory(owner) == 0) continue;\n        if (BdGetLockPod(owner) != lock || BoxLockReadLive(owner, &live) == 0) continue;",   /* fold 2 */
        "else if (w->second.lockDue == 0 && coopbrec::BoxLockRearm(h.hasLock, h.writerSlot, StoreMySlot(),",   /* fold 3: armed at the record's arrival */
        "if (w.lockDue != 0 && BoxLockDueLay(it->first, it->second.h) != 0) w.lockDue = 0;",                  /* fold 3: laid at the scan */
        "BoxLockSayOnce(id, \"laid \" + BoxLockWords(kh.lock), " };                        /* fold 7: a repeated lay prints once */
    for (size_t i = 0; i < sizeof(needIt) / sizeof(needIt[0]); ++i)
        if (it.find(needIt[i]) == std::string::npos) Fail(std::string("t1061 wiring: items.cpp lacks ") + needIt[i]);
    const char* const goneIt[] = { "kBlkFnLabel", "kBlkLockField" };   /* folds 1 and 2: no engine byte decides a cage or a picked box's lock */
    for (size_t i = 0; i < sizeof(goneIt) / sizeof(goneIt[0]); ++i)
        if (it.find(goneIt[i]) != std::string::npos) Fail(std::string("t1061 wiring: items.cpp still uses ") + goneIt[i]);
    if (dr.find("BoxLockPickNote(lock, own);") == std::string::npos) Fail("t1061 wiring: the pick tick does not hand a non-door lock to the box road");
    if (se.find("case MSG_BOX_LOCK:   OnBoxLock(m);    break;") == std::string::npos || se.find("const unsigned int kProtocolVersion = 213;") == std::string::npos)
        Fail("t1061 wiring: the session does not take BOX_LOCK or does not speak protocol 213");
    if (sm.find("const unsigned int kProtocol = 145;") == std::string::npos || st.find("const unsigned int kStoreProtocol = 145;") == std::string::npos)
        Fail("t1061 wiring: the world server and the game do not both speak world-server protocol 145");
}

/* T-1187: a forwarded conversation's end waits for ANY held line - also one with no NPC-side action (a hire: its contract is made
   by the end call 0x2732F0 the held run makes). TalkHoldEndDecide is fed as TkHoldEndFor feeds it: (hh >= 0, that hold's endHeld),
   hh = TkHoldLast's newest held line of the conversation or -1. Review fold 1 / 2: which held lines a lost answer, or a held run that
   did not happen, may list as not run (the talker's game refunds them). */
void t_t1187_hold_end()
{
    using namespace cooptalk;
    {   /* TkHoldEndFor's own call: TalkHoldEndDecide(hh >= 0 ? 1 : 0, hh >= 0 ? holds[hh].endHeld : 0) */
        const int endHeld[3] = { 0, 1, 0 };   /* three held lines; the second already holds an end */
        const int hhs[4] = { -1, 0, 1, 2 }, want[4] = { 0, 1, 2, 1 };
        for (int i = 0; i < 4; ++i)
        {
            const int hh = hhs[i];
            if (TalkHoldEndDecide(hh >= 0 ? 1 : 0, hh >= 0 ? endHeld[hh] : 0) != want[i])
                Fail("T-1187: TkHoldEndFor's decision (no held line runs the end now; a held line - also one with no NPC-side action - holds it; asked again is already held)");
        }
    }
    /* fold 1: listed as not run only with an NPC-side part to lose */
    if (TalkHoldMayListUnrun(0, 1) != 1) Fail("T-1187 fold 1: a contract-only line (hire: TAKE_MONEY, an 'AI contract') is listed - its contract never came");
    if (TalkHoldMayListUnrun(2, 0) != 1) Fail("T-1187 fold 1: a line with an NPC-side action is listed");
    if (TalkHoldMayListUnrun(1, 1) != 1) Fail("T-1187 fold 1: NPC-side action and a contract - listed");
    if (TalkHoldMayListUnrun(0, 0) != 0) Fail("T-1187 fold 1: a talker-only line (no NPC-side action, no 'change AI' / 'AI contract') is never listed - the talker keeps item and money");
    if (TalkHoldMayListUnrun(0, -1) != 1) Fail("T-1187 fold 1: lists unread - listed (the part cannot be ruled out)");
    /* fold 2: a held run that did not happen after APPLIED */
    if (TalkHeldRunUnrun(1, 0, 1, 0) != 0) Fail("T-1187 fold 2: a held run that ran is never listed");
    if (TalkHeldRunUnrun(-1, 0, 1, 0) != 1) Fail("T-1187 fold 2: a hire line whose run faulted is listed (refunded)");
    if (TalkHeldRunUnrun(0, 2, 0, 0) != 1) Fail("T-1187 fold 2: no trampoline - an NPC-side line is listed");
    if (TalkHeldRunUnrun(-2, 1, 0, 0) != 1) Fail("T-1187 fold 2: the line did not read - listed");
    if (TalkHeldRunUnrun(-1, 0, 0, 0) != 0) Fail("T-1187 fold 2: a talker-only line is not listed even when its run faulted");
    if (TalkHeldRunUnrun(-1, 1, 1, 1) != 0) Fail("T-1187 fold 2: a carried-person line is not listed (the take still follows APPLIED)");
}

/* T-1187 wiring (CRLF-safe): TkHoldEndFor decides with TalkHoldEndDecide and no longer lets a parts-0 hold through; the unrun list
   and the cap drop list a held line only through TalkHoldMayListUnrun (fold 1), the hold reads the line's lists; a held run that did
   not happen is noted as unrun (fold 2); the held run's own [TALK] line. */
void t_t1187_wiring()
{
    const std::string s = T1222NoCr("../coop-plugin/speech.cpp");
    const size_t f = s.find("static int TkHoldEndFor(void* dlg, int reason, const char* why, const char* whose)\n{");
    const size_t fEnd = (f == std::string::npos) ? std::string::npos : s.find("\n}\n", f);
    if (f == std::string::npos || fEnd == std::string::npos) { Fail("t1187: TkHoldEndFor not found"); return; }
    const std::string hf = s.substr(f, fEnd - f);
    if (hf.find("cooptalk::TalkHoldEndDecide(hh >= 0 ? 1 : 0, hh >= 0 ? g_tkActHolds[(size_t)hh].endHeld : 0)") == std::string::npos)
        Fail("t1187: TkHoldEndFor decides with TalkHoldEndDecide");
    if (hf.find("parts == 0") != std::string::npos) Fail("t1187: TkHoldEndFor lets no parts-0 hold end the conversation first");
    const size_t u = s.find("static std::vector<unsigned int> TkUnrunFor(unsigned int convId)");
    const size_t uEnd = (u == std::string::npos) ? std::string::npos : s.find("\n}\n", u);
    if (u == std::string::npos || uEnd == std::string::npos
        || s.substr(u, uEnd - u).find("if (g_tkActHolds[k].convId == convId && g_tkActHolds[k].takeOrders == 0\n"
                                      "            && cooptalk::TalkHoldMayListUnrun(g_tkActHolds[k].n, g_tkActHolds[k].aiRef))") == std::string::npos)
        Fail("t1187: the unrun list names a held line through TalkHoldMayListUnrun (fold 1)");
    if (s.find("if (g_tkActHolds[0].n > 0)") != std::string::npos) Fail("t1187: the cap drop decides by the action count alone");
    if (s.find("if (cooptalk::TalkHoldMayListUnrun(g_tkActHolds[0].n, g_tkActHolds[0].aiRef))\n        {\n            TkUnrunDrop d;") == std::string::npos)
        Fail("t1187: the cap drop lists a dropped line through TalkHoldMayListUnrun (fold 1)");
    if (s.find("h.aiRef = TkLineAiRefPod(line);") == std::string::npos) Fail("t1187: the hold reads the line's 'change AI' / 'AI contract' lists");
    if (s.find("static int TkLineAiRefPod(const void* line)") == std::string::npos
        || s.find("std::memcmp(txt, \"change AI\", 9)") == std::string::npos || s.find("std::memcmp(txt, \"AI contract\", 11)") == std::string::npos)
        Fail("t1187: TkLineAiRefPod reads both lists by name");
    const size_t r = s.find("static void TkApplyActResult(const cooptalk::TalkMsg& m, unsigned int peer)");
    const size_t run = (r == std::string::npos) ? std::string::npos : s.find("const int ran = TkRunViewPod(h.dlg, h.line, h.acts, h.n, 0);", r);
    const size_t note = (run == std::string::npos) ? std::string::npos : s.find("if (h.n == 0)", run);
    const size_t unrun = (run == std::string::npos) ? std::string::npos : s.find("if (cooptalk::TalkHeldRunUnrun(ran, h.n, h.aiRef, h.takeOrders))", run);
    const size_t push = (unrun == std::string::npos) ? std::string::npos : s.find("cooptalk::TalkPushCapped(&g_tkUnrunDropped, d, kTkUnrunDroppedCap, &g_tkUnrunNoteLost)", unrun);
    const size_t held = (run == std::string::npos) ? std::string::npos : s.find("if (h.endHeld && g_tkConvs.find(h.dlg) != g_tkConvs.end())", run);
    if (run == std::string::npos || note == std::string::npos || held == std::string::npos || !(note < held))
        Fail("t1187: the held run's no-action line prints after the run and before the held end");
    if (unrun == std::string::npos || push == std::string::npos || held == std::string::npos || !(push < held))
        Fail("t1187 fold 2: a held run that did not happen is noted as unrun before the held end runs (its END lists it)");
    if (s.find("held run with no NPC-side action ran here=") == std::string::npos) Fail("t1187: the held run's [TALK] line");
}
/* ---- T-1078 (owner 1475 b): the [BUILD] town mismatch line (townkeep.h TmMismatch / TmLinkWord / TmCapStep / TmChanged; build.cpp BdTwm*) ---- */
void t_t1078_town_mismatch()
{
    using namespace townkeep;
    /* top-level pieces: the own link and the lookup both own = no line */
    if (TmMismatch(1, kLinkOwnLive, 0, 1) != kTmNone) Fail("t1078: a piece in its own town is a mismatch");
    if (TmMismatch(1, kLinkOwnLive, 0, 0) != kTmLookup) Fail("t1078: an own link with a foreign lookup town is not a mismatch");
    if (TmMismatch(1, kLinkNone, 1, 1) != kTmNullLink || TmMismatch(1, kLinkNone, 1, 0) != kTmNullLink) Fail("t1078: a null link is not said as null");
    if (TmMismatch(1, kLinkNone, 0, 0) != kTmDeadLink || TmMismatch(1, kLinkNone, 0, 1) != kTmDeadLink) Fail("t1078: a dead link is not said as dead");
    if (TmMismatch(1, kLinkOtherLive, 0, 0) != kTmOtherLink || TmMismatch(1, kLinkOtherLive, 0, 1) != kTmOtherLink) Fail("t1078: another faction's town link is not said");
    if (TmMismatch(1, -1, 0, 1) != kTmNone) Fail("t1078: an unread link with an own lookup town is said");
    if (TmMismatch(1, -1, 0, 0) != kTmLookup) Fail("t1078: an unread link with a foreign lookup town is not said");
    if (TmMismatch(-1, kLinkNone, 0, 1) != kTmDeadLink || TmMismatch(-1, kLinkOwnLive, 0, 1) != kTmNone) Fail("t1078: an unread top-level read is not looked at as top-level");
    /* a piece inside a building keeps its host's town: only the lookup counts */
    if (TmMismatch(0, kLinkNone, 1, 1) != kTmNone || TmMismatch(0, kLinkOtherLive, 0, 1) != kTmNone) Fail("t1078: a piece inside a building is said for its own link");
    if (TmMismatch(0, kLinkOwnLive, 0, 0) != kTmHostLookup) Fail("t1078: a piece inside a building with a foreign lookup town is not said");
    if (std::strcmp(TmLinkWord(kLinkNone, 1), "null") != 0 || std::strcmp(TmLinkWord(kLinkNone, 0), "dead") != 0 || std::strcmp(TmLinkWord(kLinkOtherLive, 0), "other") != 0
        || std::strcmp(TmLinkWord(kLinkOwnLive, 0), "own") != 0 || std::strcmp(TmLinkWord(-1, 0), "unread") != 0) Fail("t1078: the link words");
    /* the session cap: kTmCap lines, the last with the cap line, then counted only */
    long long said = 0, held = 0, lastAt = -1;
    for (int i = 0; i < 200; ++i)
    {
        const int st = TmCapStep(said, kTmCap);
        if (st == kTmHold) ++held;
        else { ++said; if (st == kTmSayLast) { if (lastAt >= 0) Fail("t1078: the cap line said twice"); lastAt = said; } }
    }
    if (said != kTmCap || held != 200 - kTmCap || lastAt != kTmCap) Fail("t1078: the session cap is not kTmCap lines with one cap line at the last");
    if (TmCapStep(0, 1) != kTmSayLast || TmCapStep(5, 5) != kTmHold || TmCapStep(0, 0) != kTmHold) Fail("t1078: the cap's edges");
    /* once per piece per state change */
    std::map<std::string, unsigned long long> last;
    if (!TmChanged(last, "k1", 7)) Fail("t1078: a piece never said is not new");
    last["k1"] = 7;
    if (TmChanged(last, "k1", 7)) Fail("t1078: a piece is said again in the same state");
    if (!TmChanged(last, "k1", 8) || !TmChanged(last, "k2", 7)) Fail("t1078: a changed state (or another piece) is not said");
    last.erase("k1");   /* back in an own town */
    if (!TmChanged(last, "k1", 7)) Fail("t1078: a piece that went wrong again is not said");
    /* the wiring: release (no dev gate), the prefix, each site, nothing written */
    const std::string b = T313ReadFile("../coop-plugin/build.cpp");
    const size_t a = b.find("const char* const kTwmSiteName[kTwmSites] = { \"load\", \"check\", \"placed\", \"finished\", \"found\", \"relink\" };");
    const size_t e = (a == std::string::npos) ? std::string::npos : b.find("void BdTwmReport()", a);
    if (a == std::string::npos || e == std::string::npos) Fail("t1078: the BdTwm block");
    else
    {
        const std::string blk = b.substr(a, e - a);
        if (blk.find("SW_DEV_BUILD") != std::string::npos) Fail("t1078: the line is dev-only");
        if (blk.find("\"[BUILD] town mismatch #%lld site=%s key=") == std::string::npos) Fail("t1078: the line's prefix");
        if (blk.find("Set") != std::string::npos || blk.find("Send") != std::string::npos) Fail("t1078: the block writes or sends");
    }
    if (b.find("BdTwmDue(2);") == std::string::npos || b.find("if (r.complete != 0) BdTwmFinished(key);") == std::string::npos || b.find("BdTwmDue(4);") == std::string::npos
        || b.find("BdTwmPiece(5, b, pieceKey);") == std::string::npos || b.find("BdTwmTick();") == std::string::npos || b.find("BdTwmReport();   /* T-1078 */") == std::string::npos)
        Fail("t1078: a site of the line is missing");
}

/* T-1235: the [SPAWN] wander line (wanderspawn.h) - the nearest character of each player, the print / count decision and its cap,
   which creations are wandering groups - and its wiring in towngen.cpp */
void t_t1235_wander_line()
{
    namespace ws = wanderspawn;
    /* nearest per player: own slot 0 at 1000 and 3000, slot 1's copies at 500 and 4000, slot 2 one copy at 5000 */
    ws::Pc pcs[5];
    pcs[0].x = 1000; pcs[0].z = 0; pcs[0].slot = 0; pcs[0].own = 1;
    pcs[1].x = 3000; pcs[1].z = 0; pcs[1].slot = 0; pcs[1].own = 1;
    pcs[2].x = 0; pcs[2].z = 500; pcs[2].slot = 1; pcs[2].own = 0;
    pcs[3].x = 0; pcs[3].z = 4000; pcs[3].slot = 1; pcs[3].own = 0;
    pcs[4].x = 3000; pcs[4].z = 4000; pcs[4].slot = 2; pcs[4].own = 0;
    int sl[ws::kMaxSlots], ow[ws::kMaxSlots]; float d[ws::kMaxSlots]; float nearest = 0; int nearRow = 0, lost = 0;
    int nr = ws::NearestByPlayer(pcs, 5, 0.0f, 0.0f, sl, ow, d, ws::kMaxSlots, &nearest, &nearRow, &lost);
    if (nr != 3 || sl[0] != 0 || ow[0] != 1 || sl[1] != 1 || ow[1] != 0 || sl[2] != 2) Fail("t1235: one row per player in first-seen order");
    if (d[0] != 1000.0f || d[1] != 500.0f || d[2] != 5000.0f) Fail("t1235: each player's row is its NEAREST character on the ground");
    if (nearest != 500.0f || nearRow != 1 || lost != 0) Fail("t1235: the nearest of all is the other player's copy at 500");
    nr = ws::NearestByPlayer(pcs, 5, 0.0f, 0.0f, sl, ow, d, 2, &nearest, &nearRow, &lost);
    if (nr != 2 || lost != 1 || nearest != 500.0f) Fail("t1235: a full row list counts the players left out and keeps the nearest of those kept");
    nr = ws::NearestByPlayer(pcs, 0, 0.0f, 0.0f, sl, ow, d, ws::kMaxSlots, &nearest, &nearRow, &lost);
    if (nr != 0 || nearest != -1.0f || nearRow != -1) Fail("t1235: no character - no rows, nearest -1");
    ws::Pc same[2]; same[0].x = 10; same[0].z = 0; same[0].slot = 3; same[0].own = 1; same[1].x = 20; same[1].z = 0; same[1].slot = 3; same[1].own = 0;
    nr = ws::NearestByPlayer(same, 2, 0.0f, 0.0f, sl, ow, d, ws::kMaxSlots, &nearest, &nearRow, &lost);
    if (nr != 2) Fail("t1235: this game's own characters and a copy under the same slot number stay two rows");
    ws::Pc bad[1]; bad[0].x = std::numeric_limits<float>::infinity(); bad[0].z = 0; bad[0].slot = 0; bad[0].own = 1;
    nr = ws::NearestByPlayer(bad, 1, 0.0f, 0.0f, sl, ow, d, ws::kMaxSlots, &nearest, &nearRow, &lost);
    if (nr != 0 || nearest != -1.0f) Fail("t1235: a character whose distance is not finite is skipped");
    /* the height does not count for characters; the camera is the engine's 3-D measure */
    if (ws::GroundDist(0, 0, 3, 4) != 5.0f) Fail("t1235: the ground distance");
    const float a[3] = { 0, 0, 0 }, b[3] = { 2, 3, 6 };
    if (ws::Dist3(a, b) != 7.0f) Fail("t1235: the camera distance is 3-D, as the engine's squaredDistance");
    /* the decision and the cap */
    if (ws::Decide(-1.0f, 0) != ws::kNoCharacter) Fail("t1235: no character is counted, never printed");
    if (ws::Decide(2999.0f, 0) != ws::kPrint || ws::Decide(0.0f, 0) != ws::kPrint) Fail("t1235: a group within 3000 prints");
    if (ws::Decide(3000.0f, 0) != ws::kFar || ws::Decide(9000.0f, 0) != ws::kFar) Fail("t1235: a group at 3000 or more is only counted");
    if (ws::Decide(100.0f, ws::kPrintCap - 1) != ws::kPrint || ws::Decide(100.0f, ws::kPrintCap) != ws::kNearOverCap) Fail("t1235: at most 200 lines a session, the rest counted");
    if (ws::Decide(9000.0f, ws::kPrintCap) != ws::kFar) Fail("t1235: a far group over the cap is still counted as far");
    /* which creations are wandering groups: the spawner's (no building, param 9 the region) only */
    if (ws::IsWanderCreation(0, 1) != 1 || ws::IsWanderCreation(1, 1) != 0 || ws::IsWanderCreation(0, 0) != 0 || ws::IsWanderCreation(1, 0) != 0)
        Fail("t1235: a wandering group is a creation with no building and a region as param 9");
    /* the wiring */
    const std::string s = T1222NoCr("../coop-plugin/towngen.cpp");
    const size_t g = s.find("if (may == 1)   /* T-392 (owner 337 a): the notebook naming this game the holder is the only grant */");
    const size_t made = (g == std::string::npos) ? std::string::npos : s.find("void* np = CreatedTown(orig_create(", g);
    const size_t note = (made == std::string::npos) ? std::string::npos : s.find("if (np != 0 && wanderspawn::IsWanderCreation((p6 != 0 || inBuilding != 0) ? 1 : 0, p9 != 0 ? 1 : 0) != 0)\n            WanderNote(tp, fromSpawn, s, faction, p7,", made);
    const size_t ret = (made == std::string::npos) ? std::string::npos : s.find("return np;", made);
    if (note == std::string::npos || ret == std::string::npos || !(note < ret)) Fail("t1235: the grant notes each wandering group it made, before it returns");
    if (s.find("TownRefillTick(); WanderSnapFill(); WanderDrain(); /* T-1235 */ }") == std::string::npos) Fail("t1235: the main thread's tick fills the keep-away snapshot and drains the noted groups");
    if (s.find("DebugLog(WanderReportLine());") == std::string::npos) Fail("t1235: the report writes the final count");
    if (s.find("\"[SPAWN] wander made: group='%s' (%s) faction='%s' at %.0f,%.0f,%.0f area %d,%d camera=%.0f") == std::string::npos) Fail("t1235: the line's form");
    const size_t wn = s.find("static void WanderNote(");
    const size_t wnEnd = (wn == std::string::npos) ? std::string::npos : s.find("\n}\n", wn);
    if (wn == std::string::npos || wnEnd == std::string::npos) { Fail("t1235: WanderNote not found"); return; }
    const std::string wb = s.substr(wn, wnEnd - wn);
    if (wb.find("DebugLog") != std::string::npos || wb.find("PlayerCharSpots") != std::string::npos || wb.find("CampViewPoint") != std::string::npos || wb.find("getName") != std::string::npos)
        Fail("t1235: the any-thread note reads no characters, camera or faction name and writes no line");
}

/* T-1235 (owner decision 1478 b): THE KEEP-AWAY (wanderspawn::SpotVerdict) - a new wandering group is refused within 1382.4 on the
   ground of any live player character (own or copy, any slot), inside another player's base (0x925E80 at scale 1.0), or on a snapshot
   that is not usable; and its wiring in towngen.cpp (the refusal before the group is made, the hook reading only the snapshot) */
void t_t1235_keepaway()
{
    namespace ws = wanderspawn;
    float nearest = 0.0f; int idx = 0;
    if (ws::kKeepDist != 1382.4f) Fail("t1235k: the keep-away distance is the engine's 1382.4 (0x172F4F0)");
    /* one own character at the origin, slot 0 */
    ws::Pc one[1]; one[0].x = 0; one[0].z = 0; one[0].slot = 0; one[0].own = 1;
    if (ws::SpotVerdict(one, 1, 0, 0, 1, 0, 1000.0f, 0.0f, &nearest, &idx) != ws::kRefuseNear || nearest != 1000.0f || idx != 0)
        Fail("t1235k: a spot 1000 from this game's own character is refused, nearest 1000, that character");
    /* 2-D: 800 east and 600 north is 1000 on the ground - refused whatever the ground height (the spot carries none) */
    if (ws::SpotVerdict(one, 1, 0, 0, 1, 0, 800.0f, 600.0f, &nearest, &idx) != ws::kRefuseNear || nearest != 1000.0f)
        Fail("t1235k: the distance is the ground-plane (x, z) one");
    if (ws::SpotVerdict(one, 1, 0, 0, 1, 0, 1382.3f, 0.0f, &nearest, &idx) != ws::kRefuseNear) Fail("t1235k: just inside 1382.4 is refused");
    if (ws::SpotVerdict(one, 1, 0, 0, 1, 0, 1382.4f, 0.0f, &nearest, &idx) != ws::kAllow || idx != -1) Fail("t1235k: at 1382.4 the group is made");
    if (ws::SpotVerdict(one, 1, 0, 0, 1, 0, 0.0f, 5000.0f, &nearest, &idx) != ws::kAllow || nearest != 5000.0f) Fail("t1235k: a far spot is made, its nearest said");
    /* every player: own slot 0 far, a copy of slot 2 near - the copy refuses and is named */
    ws::Pc pl[3];
    pl[0].x = 9000; pl[0].z = 0; pl[0].slot = 0; pl[0].own = 1;
    pl[1].x = 0; pl[1].z = 9000; pl[1].slot = 1; pl[1].own = 0;
    pl[2].x = 100; pl[2].z = 100; pl[2].slot = 2; pl[2].own = 0;
    if (ws::SpotVerdict(pl, 3, 0, 0, 1, 0, 0.0f, 0.0f, &nearest, &idx) != ws::kRefuseNear || idx != 2 || pl[idx].own != 0 || pl[idx].slot != 2)
        Fail("t1235k: another player's character (its copy here) refuses as this game's own does, and is the one named");
    if (ws::SpotVerdict(pl, 3, 0, 0, 1, 0, 8000.0f, 0.0f, &nearest, &idx) != ws::kRefuseNear || idx != 0 || nearest != 1000.0f) Fail("t1235k: this game's own character refuses too");
    if (ws::SpotVerdict(pl, 3, 0, 0, 1, 0, 5000.0f, 5000.0f, &nearest, &idx) != ws::kAllow) Fail("t1235k: far from every player - made");
    /* the dead never refuse, and are not the nearest */
    pl[2].dead = 1;
    if (ws::SpotVerdict(pl, 3, 0, 0, 1, 0, 0.0f, 0.0f, &nearest, &idx) != ws::kAllow || !(nearest > 8999.0f)) Fail("t1235k: a dead character does not keep a group away");
    pl[0].dead = 1; pl[1].dead = 1;
    if (ws::SpotVerdict(pl, 3, 0, 0, 1, 0, 0.0f, 0.0f, &nearest, &idx) != ws::kRefuseStale) Fail("t1235k: a snapshot of only dead characters is not usable - refused");
    pl[0].dead = 0; pl[1].dead = 0; pl[2].dead = 0;
    /* a character whose distance is not finite is skipped (the others still decide) */
    ws::Pc nan2[2]; nan2[0].x = std::numeric_limits<float>::infinity(); nan2[0].z = 0; nan2[1].x = 5000; nan2[1].z = 0;
    if (ws::SpotVerdict(nan2, 2, 0, 0, 1, 0, 0.0f, 0.0f, &nearest, &idx) != ws::kAllow || nearest != 5000.0f) Fail("t1235k: a non-finite character is skipped");
    /* the stale snapshot refuses: never filled, too old, empty */
    if (ws::SpotVerdict(one, 1, 0, 0, 0, 0, 0.0f, 9000.0f, &nearest, &idx) != ws::kRefuseStale || nearest != -1.0f) Fail("t1235k: a snapshot never filled refuses");
    if (ws::SpotVerdict(one, 1, 0, 0, 1, ws::kStaleMs, 0.0f, 9000.0f, &nearest, &idx) != ws::kAllow) Fail("t1235k: a snapshot 2000 ms old is still used");
    if (ws::SpotVerdict(one, 1, 0, 0, 1, ws::kStaleMs + 1, 0.0f, 9000.0f, &nearest, &idx) != ws::kRefuseStale) Fail("t1235k: a snapshot older than 2000 ms refuses");
    if (ws::SpotVerdict(one, 0, 0, 0, 1, 0, 0.0f, 9000.0f, &nearest, &idx) != ws::kRefuseStale) Fail("t1235k: an empty snapshot (right after a load) refuses");
    if (ws::SnapUsable(1, 0, 1) != 1 || ws::SnapUsable(0, 0, 1) != 0 || ws::SnapUsable(1, 2001, 1) != 0 || ws::SnapUsable(1, 0, 0) != 0) Fail("t1235k: the usable-snapshot rule");
    if (ws::StaleWhy(0, 0, 1) != ws::kStaleNever || ws::StaleWhy(1, 2001, 1) != ws::kStaleOld || ws::StaleWhy(1, 0, 0) != ws::kStaleNoLive || ws::StaleWhy(1, 2000, 1) != 0)
        Fail("t1235k: the stale causes");
    /* fold 2: a base list not usable skips only the base check - the characters still decide, and the skip is said */
    if (ws::BasesUsable(0, 0) != 0 || ws::BasesUsable(1, 2001) != 0 || ws::BasesUsable(1, 2000) != 1) Fail("t1235k: the usable-base-list rule");
    {
        ws::Base sb[1]; sb[0].x = 20000; sb[0].z = 0; sb[0].r = 1500; sb[0].slot = 1;
        int skipped = 0;
        if (ws::SpotVerdictEx(one, 1, sb, 1, 0, 1, 0, 19000.0f, 0.0f, &nearest, &idx, &skipped) != ws::kAllow || skipped != 1)
            Fail("t1235k: a stale base list does not refuse - the base check is skipped and counted");
        if (ws::SpotVerdictEx(one, 1, sb, 1, 0, 1, 0, 500.0f, 0.0f, &nearest, &idx, &skipped) != ws::kRefuseNear || skipped != 0)
            Fail("t1235k: with a stale base list the characters still refuse");
        if (ws::SpotVerdictEx(one, 1, sb, 1, 1, 1, 0, 19000.0f, 0.0f, &nearest, &idx, &skipped) != ws::kRefuseBase || skipped != 0)
            Fail("t1235k: a usable base list refuses inside the base");
    }
    /* fold 2: the stale-run lines - start at the first stale refusal, stop after 10 s with none */
    {
        ws::StaleRun r;
        if (ws::StaleEdge(&r, 0, 1000) != 0) Fail("t1235k: no stale refusal - no line");
        if (ws::StaleEdge(&r, 3, 2000) != 1) Fail("t1235k: the first stale refusals start a run");
        if (ws::StaleEdge(&r, 5, 5000) != 0) Fail("t1235k: more during a run - no new line");
        if (ws::StaleEdge(&r, 5, 14999) != 0) Fail("t1235k: under 10 s quiet - still in the run");
        if (ws::StaleEdge(&r, 5, 15000) != 2 || r.seen - r.startN != 5) Fail("t1235k: 10 s quiet stops the run, its count 5");
        if (ws::StaleEdge(&r, 5, 99000) != 0) Fail("t1235k: a stopped run says nothing more");
        if (ws::StaleEdge(&r, 6, 100000) != 1) Fail("t1235k: a later stale refusal starts a new run");
    }
    if (ws::kMaxPcs < 512) Fail("t1235k: fold 1 - the character list keeps at least 512");
    /* other players' bases: 0x925E80 at scale 1.0 - the ground distance from the centre under the radius */
    ws::Base b[2];
    b[0].x = 20000; b[0].z = 0; b[0].r = 1500; b[0].slot = 1;
    b[1].x = -20000; b[1].z = 0; b[1].r = 0; b[1].slot = 2;
    if (ws::SpotVerdict(one, 1, b, 2, 1, 0, 19000.0f, 0.0f, &nearest, &idx) != ws::kRefuseBase || idx != 0) Fail("t1235k: a spot inside another player's base is refused, that base named");
    if (ws::SpotVerdict(one, 1, b, 2, 1, 0, 21499.0f, 0.0f, &nearest, &idx) != ws::kRefuseBase) Fail("t1235k: just inside the radius is refused");
    if (ws::SpotVerdict(one, 1, b, 2, 1, 0, 21500.0f, 0.0f, &nearest, &idx) != ws::kAllow) Fail("t1235k: at the radius the spot is outside (the engine's strict <)");
    if (ws::SpotVerdict(one, 1, b, 2, 1, 0, -20000.0f, 0.0f, &nearest, &idx) != ws::kAllow) Fail("t1235k: a base whose radius did not read (0) holds nothing");
    ws::Base bad; bad.x = 0; bad.z = 0; bad.r = std::numeric_limits<float>::infinity();
    if (ws::InBase(bad, 1.0f, 1.0f) != 0) Fail("t1235k: a non-finite radius holds nothing");
    /* a near character decides before a base; a stale snapshot before both */
    ws::Pc nearBase[1]; nearBase[0].x = 19500; nearBase[0].z = 0; nearBase[0].slot = 1; nearBase[0].own = 0;
    if (ws::SpotVerdict(nearBase, 1, b, 2, 1, 0, 19000.0f, 0.0f, &nearest, &idx) != ws::kRefuseNear) Fail("t1235k: a character within reach is named before the base");
    if (ws::SpotVerdict(nearBase, 1, b, 2, 1, 99999, 19000.0f, 0.0f, &nearest, &idx) != ws::kRefuseStale) Fail("t1235k: a stale snapshot refuses before any measure");
    /* the wiring: the refusal is the first step of the holder's grant, before the group is made, and returns 0 */
    const std::string s = T1222NoCr("../coop-plugin/towngen.cpp");
    const size_t g = s.find("if (may == 1)   /* T-392 (owner 337 a): the notebook naming this game the holder is the only grant */");
    const size_t ju = (g == std::string::npos) ? std::string::npos : s.find("const int wsV = WanderJudge(tp, &wsNear, &wsSlot, &wsOwn, &wsBaseR, &wsAge);", g);
    const size_t rf = (ju == std::string::npos) ? std::string::npos : s.find("wsNear, wsSlot, wsOwn, wsBaseR, wsAge);\n                return 0;", ju);
    const size_t mk = (g == std::string::npos) ? std::string::npos : s.find("void* np = CreatedTown(orig_create(", g);
    if (ju == std::string::npos || rf == std::string::npos || mk == std::string::npos || !(rf < mk)) Fail("t1235k: the keep-away refuses (return 0) before the holder makes the group");
    if (s.find("if (fromSpawn != 0 && wanderspawn::IsWanderCreation((p6 != 0 || inBuilding != 0) ? 1 : 0, p9 != 0 ? 1 : 0) != 0)\n        {\n            float wsNear", g == std::string::npos ? 0 : g) == std::string::npos)
        Fail("t1235k: only a wandering group with a read spot is judged");
    /* the hook's side reads only the snapshot: no engine call in WanderJudge or WanderRefuseNote */
    const char* anyThread[2] = { "static int WanderJudge(", "static void WanderRefuseNote(" };
    for (int i = 0; i < 2; ++i)
    {
        const size_t a = s.find(anyThread[i]);
        const size_t e = (a == std::string::npos) ? std::string::npos : s.find("\n}\n", a);
        if (a == std::string::npos || e == std::string::npos) { Fail(std::string("t1235k: not found: ") + anyThread[i]); continue; }
        const std::string body = s.substr(a, e - a);
        if (body.find("PlayerCharSpots") != std::string::npos || body.find("BdStandInBasesPod") != std::string::npos || body.find("DebugLog") != std::string::npos
            || body.find("CampViewPoint") != std::string::npos || body.find("getName") != std::string::npos)
            Fail(std::string("t1235k: the any-thread step calls no engine object and writes no line: ") + anyThread[i]);
    }
    const size_t jb = s.find("static int WanderJudge(");
    const size_t je = (jb == std::string::npos) ? std::string::npos : s.find("\n}\n", jb);
    if (jb == std::string::npos || je == std::string::npos || s.substr(jb, je - jb).find("::EnterCriticalSection(&g_tgLock);") == std::string::npos)
        Fail("t1235k: the hook reads the snapshot under g_tgLock");
    if (s.find("\"[SPAWN] wander refused: group='%s' (%s) faction='%s' at %.0f,%.0f,%.0f area %d,%d nearest=%.0f (slot %d %s)") == std::string::npos) Fail("t1235k: the refused line's form");
    if (s.find("[SPAWN] wander REPORT made=%lld refused=%lld refusedBase=%lld staleSnapshot=%lld") == std::string::npos) Fail("t1235k: the REPORT counts made / refused / refusedBase / staleSnapshot");
    /* the base list: build.cpp keeps only live stand-in towns */
    const std::string bd = T1222NoCr("../coop-plugin/build.cpp");
    const size_t bb = bd.find("int BdStandInBasesPod(");
    const size_t be = (bb == std::string::npos) ? std::string::npos : bd.find("\n}\n", bb);
    if (bb == std::string::npos || be == std::string::npos) { Fail("t1235k: BdStandInBasesPod not found"); return; }
    const std::string bbody = bd.substr(bb, be - bb);
    if (bbody.find("if (BdTownDeadPod(t) != 0) continue;") == std::string::npos || bbody.find("StandInSlotOf((::Faction*)fac)") == std::string::npos
        || bbody.find("if (slot < 0) continue;") == std::string::npos || bbody.find("BdTownRadiusPod(t)") == std::string::npos)
        Fail("t1235k: the base list is every live town of another player's stand-in faction, with its own radius");
}

/* T-1240: `buildtest cursor <sid> abs <x> <z> commit` - the word, the click's own refusal checks in its order, and the wiring */
struct T1240Ask { int f[buildnear::kFCount]; unsigned asked; };
static int T1240AskFn(void* c, int fact)
{
    T1240Ask* a = (T1240Ask*)c;
    a->asked |= 1u << fact;
    return a->f[fact];
}
static int T1240Run(T1240Ask& a) { a.asked = 0; return buildnear::ClickRefusal(&T1240AskFn, &a); }
void t_t1240_cursor_commit()
{
    namespace bn = buildnear;
    if (bn::CursorCommitWord("") != 0 || bn::CursorCommitWord(0) != 0 || bn::CursorCommitWord("commit") != 1) Fail("t1240: the commit word");
    if (bn::CursorCommitWord("commitx") != -1 || bn::CursorCommitWord("comm") != -1 || bn::CursorCommitWord("place") != -1 || bn::CursorCommitWord("COMMIT") != -1)
        Fail("t1240: another word after <z> is the usage");
    T1240Ask ok;
    for (int i = 0; i < bn::kFCount; ++i) ok.f[i] = 0;
    ok.f[bn::kFValid] = 1; ok.f[bn::kFB8b] = 1; ok.f[bn::kFVt28] = 1; ok.f[bn::kFVt60] = 1; ok.f[bn::kFB89] = 1;
    ok.f[bn::kFVt90] = 1; ok.f[bn::kFVt68] = 1; ok.f[bn::kFVt88] = 1; ok.f[bn::kFB8c] = 1;
    T1240Ask a = ok;
    if (T1240Run(a) != bn::kClickOk) Fail("t1240: every check passed - the placing road");
    a = ok; a.f[bn::kFTown90] = 1; a.f[bn::kFHostD8] = 1;
    if (T1240Run(a) != bn::kClickDontOwn) Fail("t1240: a town slot with a host (+0xD8) - don't own");
    if ((a.asked & (1u << bn::kFValid)) != 0) Fail("t1240: the don't-own answer asks nothing past it (no vtable call the click does not make)");
    a = ok; a.f[bn::kFTown90] = 1; a.f[bn::kFHostD0] = 1;
    if (T1240Run(a) != bn::kClickDontOwn) Fail("t1240: a town slot with a host (+0xD0) - don't own");
    a = ok; a.f[bn::kFTown90] = 1;
    if (T1240Run(a) != bn::kClickTooCloseTown) Fail("t1240: a town slot, no host - too close to another town");
    a = ok; a.f[bn::kFTown90] = 1; a.f[bn::kFHostD8] = 1; a.f[bn::kFInterior] = 1;
    if (T1240Run(a) != bn::kClickOk) Fail("t1240: inside-a-building mode skips the town slot and the outdoor checks");
    a = ok; a.f[bn::kFValid] = 0;
    if (T1240Run(a) != bn::kClickNoResources) Fail("t1240: not valid, no resource - mining pointless");
    a.f[bn::kFRes100] = 1;
    if (T1240Run(a) != bn::kClickCrop) Fail("t1240: not valid, a resource - the crop");
    a = ok; a.f[bn::kFB8b] = 0; a.f[bn::kFHostD8] = 1;
    if (T1240Run(a) != bn::kClickIncompleteHost) Fail("t1240: an incomplete host");
    a.f[bn::kFHostComplete] = 1;
    if (T1240Run(a) != bn::kClickMustInside) Fail("t1240: a complete host, +0x7C clear - must be inside");
    a.f[bn::kFB7c] = 1;
    if (T1240Run(a) != bn::kClickMustOutside) Fail("t1240: +0x7C set - outside or on a roof");
    a = ok; a.f[bn::kFB8b] = 0;
    if (T1240Run(a) != bn::kClickMustInside || (a.asked & (1u << bn::kFHostComplete)) != 0) Fail("t1240: no host - the host's vt+0x228 is not called");
    a = ok; a.f[bn::kFVt28] = 0;
    if (T1240Run(a) != bn::kClickBlueNodes) Fail("t1240: the blue nodes");
    a = ok; a.f[bn::kFVt60] = 0;
    if (T1240Run(a) != bn::kClickNoUsage) Fail("t1240: no usage nodes");
    a.f[bn::kFVt78] = 1;
    if (T1240Run(a) != bn::kClickTooCloseBuilding) Fail("t1240: too close to another building");
    a = ok; a.f[bn::kFB89] = 0;
    if (T1240Run(a) != bn::kClickCharacters) Fail("t1240: characters in the way");
    a = ok; a.f[bn::kFVt80] = 1;
    if (T1240Run(a) != bn::kClickBlocksUse) Fail("t1240: blocks use nodes");
    a = ok; a.f[bn::kFVt90] = 0;
    if (T1240Run(a) != bn::kClickInvalidGround) Fail("t1240: invalid ground");
    a = ok; a.f[bn::kFVt68] = 0;
    if (T1240Run(a) != bn::kClickFloor) Fail("t1240: the floor number");
    a = ok; a.f[bn::kFVt88] = 0;
    if (T1240Run(a) != bn::kClickLevel) Fail("t1240: ground level (vt+0x88)");
    a = ok; a.f[bn::kFB8c] = 0;
    if (T1240Run(a) != bn::kClickLevel) Fail("t1240: ground level (+0x8C)");
    if (std::string(bn::ClickRefusalText(bn::kClickDontOwn)) != "Can't place in a building you don't own."
        || std::string(bn::ClickRefusalText(bn::kClickTooCloseTown)) != "Can't build too close to another town." || std::string(bn::ClickRefusalText(99)) != "?")
        Fail("t1240: the engine's own texts");
    /* the wiring: the verb's word, the click's checks, the placing call, PlacementCommit through its hooked entry, the hook's count */
    const std::string b = T23NoCr(T313ReadFile("../coop-plugin/build.cpp"));
    if (b.find("const int cw = buildnear::CursorCommitWord(a5.c_str());") == std::string::npos || b.find("g_btCursorCommit = cw; kind = 12;") == std::string::npos
        || b.find("BdCursorPod(gd, pos, &r, g_btCursorCommit);") == std::string::npos)
        Fail("t1240: the cursor verb's commit word");
    const size_t p0 = b.find("void BdCursorPod(void* gd, const float* pos, BdCursorRead* r, int commit)");
    const size_t p1 = (p0 == std::string::npos) ? std::string::npos : b.find("void BdCursorCommitLines(", p0);
    if (p0 == std::string::npos || p1 == std::string::npos) { Fail("t1240: the BdCursorPod block"); return; }
    const std::string body = b.substr(p0, p1 - p0);
    const size_t c1 = body.find("r->reason = buildnear::ClickRefusal(&BdClickAsk, &cx);");
    const size_t c2 = body.find("((BdPrevTestFn)(*(void***)r->p)[kVtPrevPlace / 8])(r->p);");
    const size_t c3 = body.find("((BdCommitFn)(base + (uintptr_t)kBdCommitRva))(list);");
    if (c1 == std::string::npos || c2 == std::string::npos || c3 == std::string::npos || !(c1 < c2 && c2 < c3))
        Fail("t1240: the click's checks, then the placing call, then PlacementCommit");
    if (body.find("if (r->p == 0 || r->placing != 0) return;") == std::string::npos) Fail("t1240: a placed preview is never destroyed by the lever");
    if (b.find("const size_t kVtPrevPlace = 0xC0;") == std::string::npos || b.find("g_commitLastN = n;") == std::string::npos)
        Fail("t1240: the placing slot / the hook's piece count");
}

/* ---- T-1242: research Books in a teammate's bench (src/common/bookshare.h; store.cpp's T-1242 block) ---- */
void t_t1242_books_wire()
{

    if (bookshare::kMsgResearchBook != 88u || cooplive::kInnerResearchBook != 88u || bookshare::kGameProtocolBooks != 211u || hovrec::kGameProtocol != 213u)
        Fail("t1242 wire: RESEARCH_BOOK is not message 88 of game-to-game protocol 211");
    if (!cooplive::LiveInnerAccepted(88u) || !cooplive::LiveInnerWaitsForWorld(88u)) Fail("t1242 wire: the relay does not carry RESEARCH_BOOK or does not keep it for the world");
    bookshare::Msg m; m.op = bookshare::kOpAsk; m.reqId = 0x01000007u; m.askerSlot = 1; m.tech = "2025-gamedata.base";
    m.rows.push_back(bookshare::Row("1875-gamedata.base@43,11@547650", "16855-nodes_otto1.mod", 1));
    m.rows.push_back(bookshare::Row("1875-gamedata.base@43,11@550123", "16855-nodes_otto1.mod", 2));
    std::vector<char> b; bookshare::Msg d;
    if (!bookshare::Encode(m, &b) || !bookshare::Decode(&b[0], b.size(), &d) || d.op != bookshare::kOpAsk || d.reqId != m.reqId || d.askerSlot != 1u || d.tech != m.tech || d.rows.size() != 2
        || d.rows[1].benchKey != m.rows[1].benchKey || d.rows[1].itemSid != m.rows[1].itemSid || d.rows[1].count != 2)
        Fail("t1242 wire: an ASK does not come back as it went");
    std::vector<char> t = b; t.push_back(0);
    if (bookshare::Decode(&t[0], t.size(), &d)) Fail("t1242 wire: a trailing byte is accepted");
    for (size_t cut = 0; cut < b.size(); ++cut) if (bookshare::Decode(cut ? &b[0] : 0, cut, &d)) { Fail("t1242 wire: a cut message is accepted"); break; }
    bookshare::Msg bad = m; bad.rows.clear();
    if (bookshare::Encode(bad, &b)) Fail("t1242 wire: an ASK with no row encodes");
    bad = m; bad.rows[0].count = 0;
    if (bookshare::Encode(bad, &b)) Fail("t1242 wire: a row of 0 encodes");
    bad = m; bad.rows[0].count = bookshare::kMaxCount + 1;
    if (bookshare::Encode(bad, &b)) Fail("t1242 wire: a row over the bound encodes");
    bad = m; bad.rows.assign(bookshare::kMaxRows + 1, m.rows[0]);
    if (bookshare::Encode(bad, &b)) Fail("t1242 wire: too many rows encode");
    bad = m; bad.tech.clear();
    if (bookshare::Encode(bad, &b)) Fail("t1242 wire: an empty tech encodes");
    bad = m; bad.rows[0].benchKey = "a\tb";
    if (bookshare::Encode(bad, &b)) Fail("t1242 wire: a key with a tab encodes");
    bad = m; bad.op = 6;
    if (bookshare::Encode(bad, &b)) Fail("t1242 wire: op 6 encodes");
    /* fold 3: the holder's ACK of a GIVEBACK or a DONE - no rows, and only those two ops */
    bookshare::Msg ack; ack.op = bookshare::kOpAck; ack.reqId = 0x01000007u; ack.askerSlot = 1; ack.tech = "x"; ack.why = bookshare::kOpGiveBack;
    if (!bookshare::Encode(ack, &b) || !bookshare::Decode(&b[0], b.size(), &d) || d.op != bookshare::kOpAck || d.why != bookshare::kOpGiveBack || d.reqId != 0x01000007u)
        Fail("t1242 wire: an ACK does not come back as it went");
    ack.why = bookshare::kOpDone;
    if (!bookshare::Encode(ack, &b)) Fail("t1242 wire: an ACK of a DONE does not encode");
    b[7] = (char)bookshare::kOpAsk;   /* the why byte: op u8, reqId u32, slot u16, why */
    if (bookshare::Decode(&b[0], b.size(), &d)) Fail("t1242 wire: an ACK of an ASK decodes");
    ack.why = bookshare::kOpAnswer;
    if (bookshare::Encode(ack, &b)) Fail("t1242 wire: an ACK of an ANSWER encodes");
    ack.why = bookshare::kOpDone; ack.rows.push_back(bookshare::Row("A", "Book", 1));
    if (bookshare::Encode(ack, &b)) Fail("t1242 wire: an ACK with rows encodes");
    bad = m; bad.op = bookshare::kOpGiveBack;
    if (bookshare::Encode(bad, &b)) Fail("t1242 wire: a GIVEBACK with rows encodes");
    bad = m; bad.op = bookshare::kOpAnswer; bad.why = bookshare::kWhyShort;
    if (bookshare::Encode(bad, &b)) Fail("t1242 wire: a NO with rows encodes");
    bookshare::Msg no; no.op = bookshare::kOpAnswer; no.reqId = 9; no.askerSlot = 0; no.tech = "x"; no.why = bookshare::kWhyShort;
    if (!bookshare::Encode(no, &b) || !bookshare::Decode(&b[0], b.size(), &d) || d.why != bookshare::kWhyShort || !d.rows.empty()) Fail("t1242 wire: a NO does not come back as it went");
    bookshare::Msg yes = m; yes.op = bookshare::kOpAnswer; yes.why = bookshare::kWhyYes;
    if (!bookshare::Encode(yes, &b) || !bookshare::Decode(&b[0], b.size(), &d) || d.op != bookshare::kOpAnswer || d.rows.size() != 2) Fail("t1242 wire: a YES with what was taken does not come back");
    bookshare::Msg done; done.op = bookshare::kOpDone; done.reqId = 3; done.tech = "x";
    if (!bookshare::Encode(done, &b) || !bookshare::Decode(&b[0], b.size(), &d) || d.op != bookshare::kOpDone) Fail("t1242 wire: a DONE does not come back");
    b[0] = 9;
    if (bookshare::Decode(&b[0], b.size(), &d)) Fail("t1242 wire: op 9 decodes");
    for (unsigned char w = 0; w <= bookshare::kWhyUnknownItem; ++w) if (std::string(bookshare::WhyName(w)) == "?") Fail("t1242 wire: a reason has no words");
}
void t_t1242_books_plan()
{

    std::vector<bookshare::CostRow> rows; std::vector<bookshare::Share> sh;
    bookshare::CostRow c; c.sid = "Book"; c.need = 1; c.own = 1; c.mates.push_back(bookshare::MateBench(0, "A", 1));
    rows.push_back(c);
    if (bookshare::PlanShares(rows, &sh) != bookshare::kPlanOwn || !sh.empty()) Fail("t1242 plan: own Books that cover the row are not the engine's own road");
    rows[0].own = 0;
    if (bookshare::PlanShares(rows, &sh) != bookshare::kPlanShare || sh.size() != 1 || sh[0].slot != 0 || sh[0].row.benchKey != "A" || sh[0].row.count != 1) Fail("t1242 plan: a teammate's Book is not asked for");
    rows[0].need = 2; rows[0].own = 1; rows[0].mates[0] = bookshare::MateBench(0, "B", 2);
    if (bookshare::PlanShares(rows, &sh) != bookshare::kPlanShare || sh.size() != 1 || sh[0].row.count != 1) Fail("t1242 plan: own Books are not used first");
    rows[0].need = 3; rows[0].own = 0; rows[0].mates.clear(); rows[0].mates.push_back(bookshare::MateBench(0, "A", 1)); rows[0].mates.push_back(bookshare::MateBench(2, "C", 5));
    if (bookshare::PlanShares(rows, &sh) != bookshare::kPlanShare || sh.size() != 2 || sh[0].row.count != 1 || sh[1].slot != 2 || sh[1].row.count != 2) Fail("t1242 plan: two teammates' shares are wrong");
    const std::vector<int> sl = bookshare::SlotsOf(sh);
    if (sl.size() != 2 || sl[0] != 0 || sl[1] != 2 || bookshare::RowsFor(sh, 2).size() != 1 || !bookshare::RowsFor(sh, 1).empty()) Fail("t1242 plan: the games asked are wrong");
    rows[0].need = 2; rows[0].mates.clear(); rows[0].mates.push_back(bookshare::MateBench(0, "A", 1));
    if (bookshare::PlanShares(rows, &sh) != bookshare::kPlanShort || !sh.empty()) Fail("t1242 plan: a row the benches cannot cover is not short");
    rows.clear(); c = bookshare::CostRow(); c.sid = "Book"; c.need = 1; c.own = 0; c.mates.push_back(bookshare::MateBench(0, "A", 1));
    rows.push_back(c); rows.push_back(c);
    if (bookshare::PlanShares(rows, &sh) != bookshare::kPlanShort) Fail("t1242 plan: one Book counted for two rows");
    rows.clear();
    if (bookshare::PlanShares(rows, &sh) != bookshare::kPlanOwn) Fail("t1242 plan: a tech with no cost row is not the engine's own road");
    c.need = 0; rows.push_back(c);
    if (bookshare::PlanShares(rows, &sh) != bookshare::kPlanOwn) Fail("t1242 plan: a row of 0 is asked for");
    std::vector<bookshare::Row> taken; taken.push_back(bookshare::Row("A", "Book", 1)); taken.push_back(bookshare::Row("B", "Book", 2)); taken.push_back(bookshare::Row("A", "Other", 1));
    if (bookshare::RemoteOf(taken, "Book") != 3 || bookshare::RemoteOf(taken, "None") != 0) Fail("t1242 plan: the remote share is miscounted");
    if (bookshare::CommitNeed(2, 3) != 0 || bookshare::CommitNeed(3, 1) != 2 || bookshare::CommitNeed(1, -5) != 1) Fail("t1242 plan: the commit's own need is wrong");
    int x = -1, y = -1;
    if (!bookshare::KeyCell("1875-gamedata.base@43,11@547650", &x, &y) || x != 43 || y != 11) Fail("t1242 plan: a bench key's cell is not read");
    if (bookshare::KeyCell("abc", &x, &y) || bookshare::KeyCell("x@,1@", &x, &y) || bookshare::KeyCell("x@4a,1@", &x, &y) || bookshare::KeyCell("x@43,11", &x, &y)) Fail("t1242 plan: a key without a cell is read");
    if (!bookshare::CellNear(43, 11, 44, 12) || !bookshare::CellNear(43, 11, 42, 10) || bookshare::CellNear(43, 11, 45, 11) || bookshare::CellNear(43, 11, 43, 13)) Fail("t1242 plan: the range is not one cell");
}
void t_t1242_books_hold()
{

    const std::vector<bookshare::Row> none; std::vector<bookshare::Row> one; one.push_back(bookshare::Row("A", "Book", 1));
    bookshare::Hold h; h.on = true; h.reqId = 7; h.tech = "T"; h.asked.insert(0); h.asked.insert(2);
    if (bookshare::HoldAnswer(&h, 1, 7, true, one) != bookshare::kHoldIgnored) Fail("t1242 hold: an answer from a game not asked counts");
    if (bookshare::HoldAnswer(&h, 0, 8, true, one) != bookshare::kHoldIgnored) Fail("t1242 hold: an answer for another request counts");
    if (bookshare::HoldAnswer(&h, 0, 7, true, one) != bookshare::kHoldWait) Fail("t1242 hold: the first yes does not wait for the other");
    if (bookshare::HoldAnswer(&h, 0, 7, true, one) != bookshare::kHoldIgnored) Fail("t1242 hold: a second answer from one game counts");
    if (bookshare::HoldAnswer(&h, 2, 7, true, one) != bookshare::kHoldCommit || bookshare::TakenAll(h).size() != 2) Fail("t1242 hold: every yes does not commit");
    bookshare::Hold r; r.on = true; r.reqId = 7; r.asked.insert(0); r.asked.insert(2);
    bookshare::HoldAnswer(&r, 0, 7, true, one);
    if (bookshare::HoldAnswer(&r, 2, 7, false, none) != bookshare::kHoldRefuse) Fail("t1242 hold: a no does not refuse");
    std::vector<int> gb = bookshare::GiveBackSlots(r);
    if (gb.size() != 1 || gb[0] != 0) Fail("t1242 hold: the yes game is not told to give back");
    bookshare::Hold u; u.on = true; u.reqId = 7; u.asked.insert(0); u.asked.insert(2);
    if (bookshare::HoldAnswer(&u, 0, 7, false, none) != bookshare::kHoldRefuse) Fail("t1242 hold: a first no does not refuse at once");
    gb = bookshare::GiveBackSlots(u);
    if (gb.size() != 1 || gb[0] != 2) Fail("t1242 hold: an unanswered game is not told to give back");
    bookshare::Hold t; t.on = true; t.startMs = 1000;
    if (bookshare::HoldTimedOut(t, 1000 + bookshare::kAnswerWaitMs - 1) || !bookshare::HoldTimedOut(t, 1000 + bookshare::kAnswerWaitMs)) Fail("t1242 hold: the answer wait is not 15 s");
    t.startMs = 0xFFFFF000ul;
    if (bookshare::HoldTimedOut(t, 0x00002000ul)) Fail("t1242 hold: the clock's wrap ends a hold");
    bookshare::Hold off;
    if (bookshare::HoldTimedOut(off, 999999ul)) Fail("t1242 hold: no hold times out");
    if (!bookshare::LateYesGivesBack(off, 7, true) || bookshare::LateYesGivesBack(off, 7, false) || bookshare::LateYesGivesBack(h, 7, true) || !bookshare::LateYesGivesBack(h, 8, true))
        Fail("t1242 hold: a late yes is not given back");
}
void t_t1242_books_holder()
{

    std::vector<bookshare::Row> in; in.push_back(bookshare::Row("A", "Book", 1)); in.push_back(bookshare::Row("A", "Book", 1)); in.push_back(bookshare::Row("B", "Book", 2)); in.push_back(bookshare::Row("A", "Other", 1));
    const std::vector<bookshare::Row> g = bookshare::GroupRows(in);
    if (g.size() != 3 || g[0].count != 2 || g[1].benchKey != "B" || g[2].itemSid != "Other") Fail("t1242 holder: the asked rows are not summed per bench and item");
    if (bookshare::RowVerdict(0, 1, 5, 1) != bookshare::kWhyNoBench || bookshare::RowVerdict(-1, 1, 5, 1) != bookshare::kWhyNotOwn || bookshare::RowVerdict(1, 0, 5, 1) != bookshare::kWhyUnknownItem
        || bookshare::RowVerdict(1, 1, 1, 2) != bookshare::kWhyShort || bookshare::RowVerdict(1, 1, 2, 2) != bookshare::kWhyYes)
        Fail("t1242 holder: the verdict is wrong");
    bookshare::Book bk;
    bookshare::BookPut(&bk, 1, 5, in);
    bookshare::BookPut(&bk, 1, 6, std::vector<bookshare::Row>());
    if (bk.size() != 1 || !bookshare::BookTake(&bk, 1, 6).empty() || bookshare::BookTake(&bk, 1, 5).size() != 4 || !bookshare::BookTake(&bk, 1, 5).empty())
        Fail("t1242 holder: the record gives back twice, or not at all");
    /* fold 3: a GIVEBACK gives back once, a DONE gives nothing, a repeat or the other op after it finds no record */
    bookshare::BookPut(&bk, 1, 7, in); bookshare::BookPut(&bk, 1, 8, in); bookshare::BookPut(&bk, 2, 9, in);
    std::vector<bookshare::Row> back;
    if (bookshare::HolderClose(&bk, 1, 7, bookshare::kOpGiveBack, &back) != 1 || back.size() != 4) Fail("t1242 holder: a GIVEBACK does not give the record back");
    if (bookshare::HolderClose(&bk, 1, 7, bookshare::kOpGiveBack, &back) != 0 || !back.empty()) Fail("t1242 holder: a repeated GIVEBACK gives back twice");
    if (bookshare::HolderClose(&bk, 1, 8, bookshare::kOpDone, &back) != 1 || !back.empty()) Fail("t1242 holder: a DONE gives Books back");
    if (bookshare::HolderClose(&bk, 1, 8, bookshare::kOpGiveBack, &back) != 0 || !back.empty()) Fail("t1242 holder: a GIVEBACK after the DONE gives back");
    if (bookshare::HolderClose(&bk, 3, 9, bookshare::kOpGiveBack, &back) != 0 || bk.size() != 1) Fail("t1242 holder: another asker's GIVEBACK takes this asker's record");
    bk.clear();   /* the world teardown (StoreBookWorldTeardown): a GIVEBACK after a load finds no record */
    if (bookshare::HolderClose(&bk, 2, 9, bookshare::kOpGiveBack, &back) != 0 || !back.empty()) Fail("t1242 holder: a GIVEBACK after the load gives back");
    /* fold 7: the asker leaves the session - a record with no DONE is given back once, a DONE'd one gives nothing, a late GIVEBACK nothing more */
    std::vector<unsigned int> reqs; std::vector<std::vector<bookshare::Row> > all;
    bookshare::BookPut(&bk, 4, 20, in); bookshare::BookPut(&bk, 5, 21, in);
    if (bookshare::HolderAskerLeft(&bk, 4, &reqs, &all) != 1 || reqs.size() != 1 || reqs[0] != 20 || all.size() != 1 || all[0].size() != 4 || bk.size() != 1)
        Fail("t1242 holder: the asker's leave after a YES with no DONE does not give its record back, or takes another asker's");
    if (bookshare::HolderAskerLeft(&bk, 4, &reqs, &all) != 0 || !reqs.empty() || !all.empty()) Fail("t1242 holder: the asker's leave gives back twice");
    bookshare::BookPut(&bk, 6, 22, in);
    if (bookshare::HolderClose(&bk, 6, 22, bookshare::kOpDone, &back) != 1 || bookshare::HolderAskerLeft(&bk, 6, &reqs, &all) != 0 || !all.empty())
        Fail("t1242 holder: the asker's leave after its DONE gives Books back");
    if (bookshare::HolderClose(&bk, 4, 20, bookshare::kOpGiveBack, &back) != 0 || !back.empty() || bookshare::HolderClose(&bk, 4, 20, bookshare::kOpDone, &back) != 0 || bk.size() != 1)
        Fail("t1242 holder: a late GIVEBACK / DONE after the asker's leave gives more back");
}
/* T-1242 fold 3: the asker's owed GIVEBACK / DONE until the holder's ACK; its request ids */
void t_t1242_books_owed()
{
    bookshare::OwedList l;
    if (!bookshare::OwedAdd(&l, 0, 5, bookshare::kOpDone, "T") || !bookshare::OwedAdd(&l, 2, 5, bookshare::kOpDone, "T") || !bookshare::OwedAdd(&l, 0, 5, bookshare::kOpDone, "T") || l.size() != 2)
        Fail("t1242 owed: a DONE owed twice to one holder is kept twice, or one per holder is not kept");
    if (bookshare::OwedAck(&l, 0, 5, bookshare::kOpGiveBack) || bookshare::OwedAck(&l, 1, 5, bookshare::kOpDone) || bookshare::OwedAck(&l, 0, 6, bookshare::kOpDone) || l.size() != 2)
        Fail("t1242 owed: an ACK of another op, holder or request ends an owed message");
    if (!bookshare::OwedAck(&l, 0, 5, bookshare::kOpDone) || bookshare::OwedAck(&l, 0, 5, bookshare::kOpDone) || l.size() != 1 || l[0].slot != 2)
        Fail("t1242 owed: the ACK does not end exactly its message once");
    l.clear();
    for (unsigned int i = 0; i < (unsigned int)bookshare::kMaxOwed; ++i) if (!bookshare::OwedAdd(&l, 0, i, bookshare::kOpGiveBack, "T")) Fail("t1242 owed: the list is full early");
    if (bookshare::OwedAdd(&l, 0, 999, bookshare::kOpGiveBack, "T") || l.size() != bookshare::kMaxOwed || l[0].reqId != 1 || l.back().reqId != 999)
        Fail("t1242 owed: a full list does not drop its oldest");
    bookshare::Owed dr;   /* fold 7: the dropped one is named to the caller */
    if (bookshare::OwedAdd(&l, 2, 1000, bookshare::kOpDone, "T", &dr) || dr.reqId != 1 || dr.op != bookshare::kOpGiveBack || dr.slot != 0)
        Fail("t1242 owed: a full list does not name the one it dropped");
    unsigned int c = 0;
    const unsigned int a = bookshare::NextReqId(1, &c, 0x12345678ul), b = bookshare::NextReqId(1, &c, 0ul);
    if (a != 0x01345679u || b != 0x0134567Au) Fail("t1242 owed: the request id is not the slot over a clock-seeded counter");
    c = 0xFFFFFFu;
    if ((bookshare::NextReqId(0, &c, 0ul) & 0xFFFFFFu) == 0) Fail("t1242 owed: the counter wraps to 0");
    c = 0;
    if ((bookshare::NextReqId(3, &c, 0xFF000000ul) & 0xFFFFFFu) == 0 || (bookshare::NextReqId(3, &c, 0ul) >> 24) != 3u) Fail("t1242 owed: a clock seed of 0 gives id 0, or the slot is lost");
}
void t_t1242_books_wiring()
{
    const std::string st = T313ReadFile("../coop-plugin/store.cpp"), se = T313ReadFile("../coop-plugin/net/session.cpp"), tr = T313ReadFile("../coop-plugin/net/transport.h");
    const std::string tm = T313ReadFile("../coop-plugin/team.cpp"), it = T313ReadFile("../coop-plugin/items.cpp"), po = T313ReadFile("../coop-plugin/policy.cpp");
    const std::string t65 = T313ReadFile("../coop-plugin/addresses/65D604D7-0232C000-B914BEE3-02303600.txt");
    const std::string t68 = T313ReadFile("../coop-plugin/addresses/6602D59D-0232D000-BBA641C8-02304800.txt");
    const std::string sig = T313ReadFile("../coop-plugin/addresses/signatures.sig");
    if (st.empty() || se.empty() || t65.empty() || t68.empty() || sig.empty()) { Fail("t1242 wiring: a source file is not readable"); return; }
    if (se.find("const unsigned int kProtocolVersion = 213;") == std::string::npos || se.find("case MSG_RESEARCH_BOOK: OnResearchBook(m); break;") == std::string::npos
        || se.find("ItemSendTo(MSG_RESEARCH_BOOK, toSlot, &body[0], body.size(), 0u, 0)") == std::string::npos || tr.find("MSG_RESEARCH_BOOK = 88") == std::string::npos)
        Fail("t1242 wiring: the session does not speak 213 or does not carry RESEARCH_BOOK by slot");
    const char* const need[] = {
        "AddrReg kBkListRva_reg(\"ResearchBenchList\"", "AddrReg kBkHasRva_reg(\"ResearchHasItems\"", "AddrReg kBkCanPayRva_reg(\"ResearchCanPay\"", "AddrReg kBkPayRva_reg(\"ResearchPay\"",
        "    BookInstall(base);", "    BookReport();",
        "if (g_bkCountDepth <= 0 || g_bkMode == kBkCommit) return;",                       /* the take never sees a teammate's bench */
        "BkLektorSetPod(out, 0, g_bkForced)",                                               /* the holder's take: the one bench asked */
        "bookshare::CommitNeed(need, bookshare::RemoteOf(g_bkCommitTaken, sid))",         /* the commit asks own benches for the rest */
        "const int nf = PolicyTeammateFactionList(facs, 16);   /* the team as it is now */",
        "if (PolicyTeammateFactionList(facs, 16) <= 0) return orig_bkPay(research, tech);   /* no team: the engine's own road */",
        "if (plan == bookshare::kPlanOwn) { ++g_bkOwnRoad; return orig_bkPay(research, tech); }",
        "call = OwnResQueueCallPod(Base() + (uintptr_t)kOwnResQueueRva, r, gd, &queued);",
        "const std::string held = BookHoldWords(sid);",
        /* review folds 2-6 */
        "ItBenchesOwnedBy(facs, nf, &BkKeepBench, &c, 64);",                               /* 2: research bench and range filtered before the cap */
        "if (g_bkMateFrame != g_bkFrame)",                                                  /* 5: one scan per frame */
        "    ++g_bkFrame;   /* T-1242 fold 5",                                              /* 5: StoreTick counts the frames */
        " scans=\" + N(g_bkScans)",                                                         /* 5: the scan counter on the REPORT line */
        "return bookshare::CommitNeed(need, bookshare::RemoteOf(g_bkCommitTaken, sid));",  /* 4: the commit's take asks only the own share */
        "const int had = bookshare::HolderClose(&g_bkBook, from, m.reqId, m.op, &back);",  /* 3: idempotent close */
        "if (from >= 0) BookSend(from, a);   /* every GIVEBACK and DONE is acknowledged",  /* 3: always acknowledged */
        "if (bookshare::OwedAck(&g_bkOwed, from, m.reqId, m.why))",                       /* 3: an ACK ends an owed message */
        "BookOwe(*it, bookshare::kOpDone, h.reqId, h.tech, \"queued\");",                   /* 3: DONE owed until its ACK */
        "BookOwedSend(-1, welcomed ? \"this game's WELCOME\" : \"owed at the world teardown\");",   /* 3: resent at the WELCOME */
        "    StoreBookWorldTeardown();   /* T-1242 fold 6",                                 /* 6: cleared at the teardown (the load's) */
        "g_bkHold = bookshare::Hold(); g_bkBook.clear();" };
    for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); ++i)
        if (st.find(need[i]) == std::string::npos) Fail(std::string("t1242 wiring: store.cpp lacks ") + need[i]);
    if (st.find("BookDropAsker") != std::string::npos || st.find("the asker left the world before DONE") != std::string::npos)
        Fail("t1242 wiring: the pre-fold-3 asker-left road is back");
    if (st.find("const size_t k = bookshare::HolderAskerLeft(&g_bkBook, *it, &reqs, &all);") == std::string::npos
        || st.find("BookGiveBackRows(all[i], *it, reqs[i], \"the asker left the session with no DONE received\");") == std::string::npos
        || st.find("BookOwedDropped(dropped, \"the world teardown\");") == std::string::npos || st.find("BookOwedDropped(dropped, why);") == std::string::npos)
        Fail("t1242 wiring: the holder does not give back when its asker leaves (fold 7), or an owed drop is not logged");
    {   /* fold 4: the take hook lowers its need before the engine's own take; fold 2: the scan's filter runs before its cap */
        const size_t dr = it.find("void detour_researchConsume(void* research, void* data, int need)");
        const size_t lo = it.find("need = StoreBookTakeNeed(data, need);", dr == std::string::npos ? 0 : dr);
        const size_t og = it.find("orig_researchConsume(research, data, need);", dr == std::string::npos ? 0 : dr);
        if (dr == std::string::npos || lo == std::string::npos || og == std::string::npos || !(lo < og)) Fail("t1242 wiring: the take hook does not lower the commit's need before the engine's take");
        const size_t ob = it.find("int ItBenchesOwnedBy(void* const* facs, int nf, ItBenchKeepFn keep, void* ctx, int cap)");
        const size_t kp = it.find("if (dup || keep(b, k, ctx) == 0) continue;", ob == std::string::npos ? 0 : ob);
        const size_t cp = it.find("for (size_t i = 0; i < cand.size() && n < cap; ++i)", ob == std::string::npos ? 0 : ob);
        const size_t pn = (kp == std::string::npos) ? kp : it.find("kept.push_back(b); ++n;", kp), rn = (kp == std::string::npos) ? kp : it.find("return n;", kp);
        if (ob == std::string::npos || kp == std::string::npos || cp == std::string::npos || !(cp < kp) || pn == std::string::npos || rn == std::string::npos || pn > rn
            || it.find("++n", kp) != pn + std::string("kept.push_back(b); ").size())
            Fail("t1242 wiring: the bench scan counts toward its cap before the filter");
    }
    const size_t hp = st.find("(void*)&detour_bkPay, (void**)&orig_bkPay"), hl = st.find("(void*)&detour_bkList, (void**)&orig_bkList"), hh = st.find("(void*)&detour_bkHas, (void**)&orig_bkHas");
    if (hp == std::string::npos || hl == std::string::npos || hh == std::string::npos || !(hp < hl && hl < hh)) Fail("t1242 wiring: the pay is not hooked before the list and the count");
    size_t n = 0;
    for (size_t at = st.find("[RESEARCH] T-1242"); at != std::string::npos; at = st.find("[RESEARCH] T-1242", at + 1)) ++n;
    if (n < 12) Fail("t1242 wiring: the [RESEARCH] T-1242 lines are missing");
    if (tm.find("    StoreBookTick();") == std::string::npos || it.find("int ItResearchConsumeCall(void* research, void* data, int need)") == std::string::npos
        || po.find("int PolicyIsResearchBench(void* b)") == std::string::npos)
        Fail("t1242 wiring: the tick, the holder's take or the bench test is missing");
    const char* const rows65[] = { "ResearchBenchList                              code 0082D880 0082D880 48895C2408574881EC80000000488B0D",
                                   "ResearchCanPay                                 code 00831F80 00831F80 48895C2410564883EC30488BDA488BF1",
                                   "ResearchHasItems                               code 0082D940 0082D940 41544883EC4048C7442420FEFFFFFF48",
                                   "ResearchPay                                    code 008334F0 008334F0 405556574881EC8002000048C7442420" };
    const char* const rows68[] = { "ResearchBenchList                              code 0082E750", "ResearchCanPay                                 code 00832E50",
                                   "ResearchHasItems                               code 0082E810", "ResearchPay                                    code 008343C0" };
    for (int i = 0; i < 4; ++i)
    {
        if (t65.find(rows65[i]) == std::string::npos) Fail(std::string("t1242 wiring: the 1.0.65 table lacks ") + rows65[i]);
        if (t68.find(rows68[i]) == std::string::npos) Fail(std::string("t1242 wiring: the 1.0.68 table lacks ") + rows68[i]);
    }
    if (t65.find("!count 719") == std::string::npos || t68.find("!count 719") == std::string::npos) Fail("t1242 wiring: both address tables declare 719 rows (T-1242 + T-1150, 4 each; T-478 buying, 2)");
    if (sig.find("ResearchBenchList ") == std::string::npos || sig.find("ResearchCanPay ") == std::string::npos || sig.find("ResearchHasItems ") == std::string::npos || sig.find("ResearchPay ") == std::string::npos)
        Fail("t1242 wiring: a new row has no signature");
}

}   /* namespace suite */
