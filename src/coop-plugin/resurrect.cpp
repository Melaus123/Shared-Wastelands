// resurrect.cpp - T-556: THE FALLEN LIST and the TEST-ONLY bring-back.
//
// AT A DEATH. The declareDead detour (worldstate.cpp) calls ResurrectNoteDeath after the engine's own call, on whatever thread
// the engine called it from (run reports show both: uniqueStateThreads declareDead m/o). It notes only the uid of a character
// this game drives, how the call was reached and the unique flag, in a fixed ring. ResurrectTick, on the main thread, takes the
// snapshot while the body is still there: a character of this game's own player faction (people, pets and pack animals alike);
// a copy of another game's character never enters (not driven here), nor does a character of any other faction. The snapshot
// (src/common/fallenwire.h) is template name, name, race, unique flag, animal flag and age, the 44 stats, the appearance record,
// the death position, wall-clock time and cause, the body's engine handle, a named character's string id, and the record name of
// the town nearest the death (the engine's TownList, zones.cpp TownDistances; fallenwire.h NearestTown). The snapshot goes to
// the world server as an ADD; the server keeps this player's list (the newest 40, newest first, fallen.txt) and sends it back as a
// TABLE, and this game's list is only that copy - refreshed on every TABLE, asked for again (ASK) after a world teardown, gone
// while the link is down. An ADD the server has not listed is sent again (at once on a new link, after swfallen::kRetryMs on the
// same one, at most swfallen::kAddTries times). A game that has never linked to a world server keeps nothing.
//
// THE LIST FOLLOWS THE SAVE (fallenwire.h). Keyed by the engine handle's serial - the one character identity a save keeps: the
// dead character's (handSerial, in the snapshot) and the brought-back character's (newSerial, in the TAKE). After a world load,
// with a TABLE from this link and the world running, every LIVING character of this player's faction is read (when a TABLE
// arrives and once after each load): a listed row whose character is alive in the loaded world goes (ALIVE - the save predates
// the death; never a named character's row, which follows the world server's unique state), and, once per world load, each of
// this player's pending bring-backs is SAVED when its new character is in the loaded world (by serial, or by name + template +
// race), UNDOne when it is not - a named character's only when the loaded world proves the save predates it (fallenwire.h
// Reconcile). When this game's own save of its profile finishes, the bring-backs made in this world since its last save are SAVED.
// TEST-ONLY `resurrect killlast`: the character this lever last brought back dies (the kill lever's death steps).
// TEST-ONLY `resurrect killother <uid>`: the first living character of this player's faction this game drives, other than <uid>
// and other than a character this lever brought back in this world, dies (the same death steps).
//
// THE BRING-BACK (TEST-ONLY lever `resurrect <n> [squad <i> | beside <uid>]`). The new character is made beside a squadmate:
// the character <uid> (`beside`), or else the first living member of the i-th of this player's squads (numbered in the order
// their first living member appears among the characters this game drives; default 0). Only the new character is placed; the
// camera and the player are never moved. The squadmate must be this game's own character of this player's faction, alive and
// free (not knocked out, carried, caged, chained or enslaved), with no enemy within 50 m (fallenwire.h PlaceVerdict); a refusal
// is one log line naming the reason, counted per reason on the REPORT line. The new character joins the squadmate's squad.
//   1. RootObjectFactory::create of the snapshot's template in the player's faction, into that squad (CreateOwnInSquad, a
//      fresh uid, owned here, not yet announced).
//   2. Every item it holds is destroyed (Inventory::clearAll(destroy) - what it carries and what it wears), under items.cpp's
//      own-write marker so nothing is published as an item move.
//   3. The name and the 44 stats are written (the engine's setName; the stats with the engine's recalculation).
//   4. The road: the two fields PlayerInterface::recruit writes for a hired member are read on the new character and on the
//      member it joins (fallenwire.h RoadDecide); when they differ, recruit(edit=false) is called, and a recruit that left the
//      character outside the chosen squad is followed by setFaction(player faction, chosen squad).
//   5. CONTEXT then SPAWN go out on the normal road (net::SendSpawn with the player faction's wire name; a SPAWN that does not
//      go out is left to the announce pass), then the appearance record is applied once the body can take it, and APPEARANCE
//      then CLOTHING (nothing worn) go out (appearance.cpp OwnLookQueue). With no record kept, or when that apply cannot be
//      made, the character's own look and its empty kit go out by the settle send, so no other game keeps the rolled kit.
//   6. The snapshot leaves this game's copy and a TAKE goes to the world server, which drops the row and sends the list back.
//      Until a TABLE no longer lists it, the row stays out of this game's copy, and a TABLE from a later link that still lists
//      it sends the TAKE again (fallenwire.h TableApply). The old body is not touched.
// A named character (owner 493): the TAKE of its row makes the world server mark it BROUGHT BACK in uniques.txt and tell every
// game, so the dead-is-final rule lets it stay alive everywhere; this game writes its own map ALIVE at once and holds off a DEAD
// from the server until the server confirms (worldstate.cpp WorldStateBroughtBack). A bring-back needs the world-server link.
// A new character has all four limbs: a fresh MedicalSystem has no RobotLimbs record (+0xC8 = 0); the final line reads it.
//
// THE FEE (owner 485, 491, 494, 497; src/common/resurrectfee.h). The host options `resurrect` (on/off, absent = off),
// `resurrectfee` (the amount) and `resurrectgrowth` (steady/steep) arrive in the world server's OPTIONS map (store.cpp's drain ->
// ResurrectOption). A bring-back is refused while resurrection is off. Otherwise, once the placement rules allow it, A is counted
// at that moment: this player's LIVING characters (driven here, of this player's faction) that a bring-back made - the back rows
// (bring-backs a finished save holds) and pending rows of the last TABLE, and this game's own TAKEs not yet answered
// (fallenwire.h CountBackAlive; a row whose brought-back character is known dead - the server's mark, a death the last TABLE
// lists or this game's own ADD not yet listed - matches by serial only). While kFallenKeep bring-backs already wait for a save,
// a bring-back is refused. The price (resurrectfee.h PriceFor) is checked against this player's purse - the player faction's
// Ownerships (Faction +0x80), its money at +0x88, read live; short money refuses, and so does a purse that does not read when
// the price is above 0. The price is taken only after the new character exists, through that same Ownerships' takeMoney
// (vtable slot 0, the chokepoint hire.cpp and items.cpp pay through); one log line gives A, the rule, the amount, the price and
// the money before and after.
// TEST-ONLY `resurrect host on|off | host fee <n> | host growth steady|steep`: the option goes to the world server as the host
// would set it (only the host's is accepted). TEST-ONLY `resurrect price`: A, the rule and the price now. TEST-ONLY
// `resurrect money <n>`: this player's purse is set to n.
//
// THE FALLEN TAB (fallentab.cpp) takes the same road: ResurrectBringBackFor is BringBack below, the lever's own function, with
// the row named by its uid, the squadmate the player picked and the price the confirm box showed (a different price refuses).
// ResurrectTabRead and ResurrectFreeMates are the tab's reads. A snapshot also keeps the in-game day (store.cpp StoreGameDay)
// and how the death came (fallenwire.h DeathWhy, from the character's MedicalSystem blood and hunger).
#include "resurrect.h"
#include "spawn.h"           // FindSpawned, FindSpawnedUid, MirrorSlot, SafeReadPosition, CreateOwnInSquad, NameOf, NameSetOwn
#include "store.h"           // EngineWritesBlocked
#include "net/session.h"     // IsUidMine, IsUidMineAnyThread, SendSpawn
#include "playerfaction.h"   // IsPlayerFaction, LocalPlayerFaction, WireFactionName
#include "appearance.h"      // GroundRaceName
#include "appearance_record.h"   // CaptureAppearanceRecord, SerialiseRecord, DeserialiseRecord, OwnLookQueue, OwnLookResult
#include "stats.h"           // StatsReadOwned, StatsApplyOwn
#include "combat.h"          // ReadAnimalAge01
#include "medical.h"         // DeathCallerCause
#include "hire.h"            // HireRecruitNoEdit, HireSetFaction
#include "items.h"           // ItemApplyingEnter / ItemApplyingLeave
#include "worldsync.h"       // NoteAnnouncedOnTake, NoteNotAnnounced
#include "worldstate.h"      // WorldStateBroughtBack - a named character brought back
#include "soak.h"            // GameplayRunning
#include "addresses.h"       // GameWorldPtr
#include "game/GameWorld.h"  // activeCharacters - the living characters of this player's faction after a load
#include "coop_log.h"
#include "game/Character.h"
#include "game/GameData.h"
#include "game/Inventory.h"
#include "game/Faction.h"     // getName - the faction named on the not-counted line
#include "zones.h"           // TownDistances
#include "relations.h"       // RelationsTagLevel - two players' standing (the enemy rule for another player's character)
#include "fallentab.h"       // FallenTabCommand - `resurrect tab ...`
#include "../common/nametag.h"
#include "../common/fallenwire.h"
#include "../common/resurrectfee.h"
#include "../common/statswire.h"
#include <Windows.h>
#include <cstring>
#include <ctime>
#include <locale>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <vector>

namespace coop {

long long ItInvItemCount(::Inventory* inv);   // items.cpp: every item in every section, -1 unreadable

namespace {

const int kRing = 256;   /* deaths noted between two ticks: a whole squad wiped at one moment fits (the list keeps 40) */
volatile LONG64 g_ring[kRing];   // 0 = free; else uid | (cause << 32) | (unique << 40)
const size_t kMedicalOff = 0x458;        // Character -> MedicalSystem (spawn.cpp / medical.cpp kMedicalOffset)
const size_t kRobotLimbsOff = 0xC8;      // MedicalSystem -> RobotLimbs* (0 = all four limbs whole; clothing.cpp)
const size_t kLimbStateOff = 0x10;       // RobotLimbs -> LimbState[4] (int each; limbwire.h)
const size_t kMoveOff = 0x640;           // Character -> movement object; recruit writes its +0x20 = 2 (answers-recruit1.md:72)
const size_t kMoveField = 0x20;
const size_t kAiOff = 0x1A0;             // Character -> object whose +0xDC recruit writes = 9 (answers-recruit1.md:72)
const size_t kAiField = 0xDC;
const size_t kSlaveSbOff = 0x1A0;        // Character -> StateBroadcastData*, whose int +0x00 is the slave state (spawn.cpp kSlaveSbOff);
                                         // the same pointer as kAiOff: recruit writes that object's +0xDC
const size_t kInSomethingOff = 0x2F8;    // Character int inSomething: 1 bed, 2 cage (spawn.cpp kCarryInSomethingOff)
const size_t kChainedOff = 0x320;        // Character chained byte, setChainedMode's first write (items.cpp kCapChainedOff)
const size_t kBeingCarriedOff = 0x3D4;   // Character bool _isBeingCarried, which getPickedUp sets (spawn.cpp kBeingCarriedOff)
const size_t kVtIsEnemyOf = 0x3E8;       // Character vtable: isEnemyOf(other, factorInDisguises) (combat.cpp kRsVtIsEnemy)
std::vector<swfallen::Fallen> g_list;    // this game's copy of the world server's list: newest first, at most swfallen::kFallenKeep
std::vector<swfallen::Taken> g_taken;    // rows brought back (or reported ALIVE) here that the last TABLE still listed; kept across a world teardown
std::vector<swfallen::PendWire> g_pendWire;   // the last TABLE's pending rows (brought back, not yet in a finished save)
struct AddWait { swfallen::Fallen f; int gen; unsigned int sentMs; int tries; AddWait() : gen(-1), sentMs(0), tries(0) {} };
std::vector<AddWait> g_addWait;          // ADDs the world server has not listed yet (sent again until it does, or kAddTries)
std::vector<unsigned int> g_unsaved;     // rows brought back in this world since this game's last finished save
std::vector<unsigned int> g_savedOwed;   // bring-backs a finished save holds, until a TABLE no longer shows them pending
int g_savedOwedGen = -1;                 // the link the owed SAVED last went out on
unsigned int g_savedOwedMs = 0;         // and when (sent again after swfallen::kRetryMs until a TABLE no longer shows them pending)
std::vector<swfallen::Fallen> g_table;   // the last TABLE's listed rows as the world server sent them
bool g_reconWanted = true;               // the list's checks are owed: a TABLE arrived, or a world was loaded
unsigned int g_lastBackUid = 0;          // the character the lever last brought back (TEST: resurrect killlast)
int g_worldEpoch = 0, g_reconEpoch = -1; // world loads seen / the one whose pending bring-backs were settled
long long g_aliveSent = 0, g_savedSent = 0, g_undoSent = 0, g_addsResent = 0, g_addsDropped = 0, g_reconRuns = 0;
int g_tableGen = -1, g_askedGen = -1;    // StoreLinkGen() of the link the copy came from / an ASK went on since it was cleared (-1 none)
std::string g_tableSaid;
long long g_adds = 0, g_addsNotSent = 0, g_tables = 0, g_tableMalformed = 0, g_takesSent = 0, g_takesNotSent = 0, g_takesResent = 0, g_asks = 0, g_backUniques = 0;
std::set<unsigned int> g_seen;           // uids snapshotted (or refused) in this world: one snapshot per uid (FallenNoteOnce)
volatile LONG64 g_noted = 0, g_ringFull = 0, g_notMine = 0;
long long g_snaps = 0, g_snapGone = 0, g_snapNotOwnFaction = 0, g_snapNoStats = 0, g_snapNoLook = 0;
long long g_snapAnimals = 0, g_snapUniques = 0, g_backs = 0, g_backRefused = 0, g_backRecruit = 0, g_backLookApplied = 0;
long long g_backLookFailed = 0, g_snapRepeat = 0, g_snapBlocked = 0, g_snapFault = 0;
long long g_besideRefused[swfallen::kBesideCodes] = { 0 };   // bring-backs refused by the placement rules, per reason
long long g_enemyUnable = 0, g_enemyListBad = 0;   // near enemies that could not fight (not counted) / enemy walks refused: no readable list
long long g_besideAllowed = 0, g_besideNamed = 0, g_enemyUnknown = 0, g_placeKnown = 0, g_placeNone = 0, g_placeNoList = 0, g_killOther = 0;
int g_notOwnLogged = 0;
std::vector<swfallen::BackRow> g_back;   // the last TABLE's back rows (bring-backs a finished save holds)
std::set<unsigned int> g_backHere;       // characters this lever brought back in this world (killother passes them over)
// the host options (resurrectfee.h), from the world server's OPTIONS map; g_optSeen: the keys the map being read named (1 on, 2 fee, 4 growth)
int g_optOn = 0, g_optGrowth = swfee::kDefaultGrowth, g_optSeen = 0;
long long g_optAmount = swfee::kDefaultAmount;
long long g_feeCharged = 0, g_feeTotal = 0, g_feeFree = 0, g_feeRefusedOff = 0, g_feeRefusedShort = 0, g_feeMoneyUnread = 0, g_feeUnpaid = 0;
long long g_feeOptionsSent = 0, g_feeLastPrice = -1, g_feePendingFull = 0, g_feePriceChanged = 0;
swfallen::BackCount g_feeLast;           // the last bring-back's (or `resurrect price`'s) count
const size_t kFacOwnershipsOff = 0x80;   // Faction -> Ownerships (hire.cpp kFacOwnershipsOff, Confirmed bytes 0x682865)
const size_t kOwnMoneyOff = 0x88;        // Ownerships money (hire.cpp kOwnMoneyOff, items.cpp, Confirmed)

struct Pending { unsigned int uid; unsigned int deadUid; std::string name, raceBefore, tmpl; int road; int animal; DWORD at;
                 int statsHave; unsigned int stats[swfallen::kStatsCount]; };
std::vector<Pending> g_pending;   // bring-backs whose look waits (MAIN THREAD)

std::string N(long long v) { std::ostringstream o; o << v; return o.str(); }
std::string F1(float v) { std::ostringstream o; o.imbue(std::locale::classic()); o.setf(std::ios::fixed); o.precision(1); o << v; return o.str(); }
std::string X(const void* p) { std::ostringstream o; o << p; return o.str(); }

// ---- POD reads (no objects with destructors in a __try frame: C2712) ----
int IntAtPod(const void* c, size_t objOff, size_t fieldOff)   // *( *(c+objOff) + fieldOff ), -1 unreadable
{
    __try
    {
        const char* o = *(const char* const*)((const char*)c + objOff);
        if (o == 0) return -1;
        return *(const int*)(o + fieldOff);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// 0..4 limbs whole (4 when there is no RobotLimbs record), -1 unreadable. *record = 1 when a RobotLimbs record exists.
int LimbsWholePod(const void* c, int* record)
{
    *record = 0;
    __try
    {
        const char* med = (const char*)c + kMedicalOff;
        const char* rl = *(const char* const*)(med + kRobotLimbsOff);
        if (rl == 0) return 4;
        *record = 1;
        int whole = 0;
        for (int i = 0; i < 4; ++i) if (*(const int*)(rl + kLimbStateOff + 4 * i) == 0) ++whole;
        return whole;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
::Inventory* InventoryPod(::Character* c)
{
    __try { return c->inventory; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int ClearAllPod(::Inventory* inv)   // 1 called, 0 faulted
{
    __try { inv->clearAll(true, false); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* SquadPod(::Character* c)
{
    __try { return (void*)c->getSquad(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int HandPod(::Character* c, unsigned int* index, unsigned int* serial)
{
    __try { const hand& h = c->getHandle(); *index = h.index; *serial = h.serial; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// The squadmate's own states (fallenwire.h MateState: dead .. slaveState). 0 = a fault (every state is then unreadable).
int MateStatesPod(::Character* c, swfallen::MateState* m)
{
    __try
    {
        m->dead = c->hasDied() ? 1 : 0;
        m->unconscious = c->isUnconscious() ? 1 : 0;
        m->carried = *((const unsigned char*)c + kBeingCarriedOff) != 0 ? 1 : 0;
        m->inSomething = *(const int*)((const char*)c + kInSomethingOff);
        m->chained = *((const unsigned char*)c + kChainedOff) != 0 ? 1 : 0;
        const char* sb = *(const char* const*)((const char*)c + kSlaveSbOff);
        /* no slave-state record: never a slave (free); a value outside the engine's range does not read (SlaveSendFor's check) */
        const int st = sb != 0 ? *(const int*)sb : 0;
        m->slaveState = (st >= 0 && st <= coopslave::kSlaveStateMax) ? st : -1;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { m->dead = m->unconscious = m->carried = m->inSomething = m->chained = m->slaveState = -1; return 0; }
}
// One other character beside the squadmate: *enemy = the squadmate's own isEnemyOf(o, true): 1 / 0, -1 a fault.
typedef char (*IsEnemyOfFn)(::Character*, ::Character*, char);
void EnemyOfPod(::Character* mate, ::Character* o, int* enemy)
{
    *enemy = -1;
    __try
    {
        const IsEnemyOfFn f = *(const IsEnemyOfFn*)(*(const char* const*)mate + kVtIsEnemyOf);
        *enemy = f(mate, o, 1) != 0 ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *enemy = -1; }
}
// The character's MedicalSystem blood and hunger (fallenwire.h DeathWhy). 0 = a fault.
int MedPod(::Character* c, float* blood, float* hunger)
{
    __try { *blood = c->medical.blood; *hunger = c->medical.hunger; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *blood = 0.0f; *hunger = 0.0f; return 0; }
}
const size_t kApPlatoonOff = 0x78;       // ActivePlatoon -> Platoon* (items.cpp kApPlatoon)
// The squad's name: its Platoon's name (a RootObjectBase, name at +0x18 as a character's - spawn.cpp NameOf). "" unread.
void* PlatoonPod(void* squad)
{
    __try { return squad != 0 ? *(void**)((char*)squad + kApPlatoonOff) : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
::Faction* FactionPod(::Character* c)
{
    __try { return c->getOwnerFactionDirect(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// The squadmate read for the placement rules (fallenwire.h MateState).
void ReadMate(::Character* c, unsigned int uid, swfallen::MateState* m)
{
    *m = swfallen::MateState();
    if (c == 0) return;
    m->found = 1;
    m->mine = (uid != 0 && net::IsUidMine(uid)) ? 1 : 0;
    m->ownFaction = IsPlayerFaction(c->getOwnerFactionDirect()) ? 1 : 0;
    MateStatesPod(c, m);
}
// The enemy rule's rows: every other living loaded character within twice the radius of `at` (the squadmate's position), its
// ground distance, whether the squadmate calls it an enemy and whether it can fight (its states, read as the squadmate's;
// fallenwire.h AbleToFight). An enemy is what fallenwire.h EnemyVerdict says: the squadmate's own isEnemyOf, or, for another
// player's character (a stand-in faction), the two players' standing at hostile (relations.cpp RelationsTagLevel, the name tag's
// judgement). *nearName = the nearest counted enemy's name and "(uid <n>, isEnemyOf=<a>, playersStanding=<level>)". *unableText = each enemy within the radius that
// cannot fight (at most kUnableShown): its name, uid, why (fallenwire.h UnableWhy), distance and the six states. *skippedText =
// the nearest kUnableShown characters of other factions within the radius NOT counted, nearest first, each with why (fallenwire.h NotCountedWhy),
// its faction, isEnemyOf's answer and the players' standing; *skipped = how many there were. false = the loaded characters
// cannot be walked (no world, or a list of implausible size): the rule cannot be judged.
const size_t kUnableShown = 4;
struct NearSeen { float dist; size_t row; bool operator<(const NearSeen& o) const { return dist < o.dist; } };
const char* PairWord(int level)
{
    return level == nametag::kHostile ? "hostile" : level == nametag::kNeutral ? "neutral" : level == nametag::kFriendly ? "friendly"
         : level == nametag::kUnknown ? "unread" : "-";
}
bool EnemiesNear(::Character* mate, const Ogre::Vector3& at, swfallen::EnemyCount* out, std::string* nearName, std::string* unableText,
                 std::string* skippedText, int* skipped)
{
    nearName->clear(); unableText->clear(); skippedText->clear(); *skipped = 0;
    *out = swfallen::EnemyCount();
    if (coop::GameWorldPtr() == 0) return false;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return false;
    std::vector<swfallen::NearRow> rows; std::vector< ::Character*> who; std::vector<swfallen::MateState> states;
    std::vector<int> charEnemy, pairLevel;   /* per row: isEnemyOf's answer, and the players' standing (-2 not another player's) */
    ::Faction* mine = FactionPod(mate);
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* o = *it;
        if (o == 0 || o == mate) continue;
        Ogre::Vector3 p(0.0f, 0.0f, 0.0f);
        if (!SafeReadPosition(o, &p)) continue;
        const float dx = p.x - at.x, dz = p.z - at.z, d = std::sqrt(dx * dx + dz * dz);
        if (!(d <= 2.0f * swfallen::kEnemyNearUnits)) continue;
        swfallen::MateState st;
        MateStatesPod(o, &st);
        if (st.dead == 1) continue;
        swfallen::NearRow r; r.dist = d;
        int ce = -1;
        EnemyOfPod(mate, o, &ce);
        int level = -2, hostile = -1;
        ::Faction* theirs = FactionPod(o);
        if (theirs != 0 && theirs != mine && !IsPlayerFaction(theirs) && IsStandInFaction(theirs))
        {
            int reads = 0;
            level = RelationsTagLevel(mine, theirs, &reads);
            hostile = level == nametag::kHostile ? 1 : (reads > 0 ? 0 : -1);
        }
        r.enemy = swfallen::EnemyVerdict(ce, hostile);
        r.able = swfallen::AbleToFight(st);
        rows.push_back(r); who.push_back(o); states.push_back(st); charEnemy.push_back(ce); pairLevel.push_back(level);
    }
    const swfallen::EnemyCount e = swfallen::CountEnemiesNear(rows, swfallen::kEnemyNearUnits);
    if (e.nearestRow >= 0)
    {
        NameOf(who[(size_t)e.nearestRow], nearName);
        const size_t nr = (size_t)e.nearestRow;
        *nearName += " (uid " + N(FindSpawnedUid(who[nr])) + ", isEnemyOf=" + N(charEnemy[nr]) + ", playersStanding="
                   + (pairLevel[nr] == -2 ? std::string("-") : std::string(PairWord(pairLevel[nr]))) + ")";
    }
    size_t shown = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].enemy != 1 || rows[i].able != 0 || !(rows[i].dist <= swfallen::kEnemyNearUnits)) continue;
        if (shown++ >= kUnableShown) { *unableText += " ..."; break; }
        const swfallen::MateState& s = states[i];
        std::string nm; NameOf(who[i], &nm);
        *unableText += std::string(shown > 1 ? ";" : "") + " '" + nm + "' uid=" + N(FindSpawnedUid(who[i])) + " " + swfallen::UnableWhy(s)
                     + " at " + F1(rows[i].dist / 10.0f) + " m dead/ko/carried/inSomething/chained/slave=" + N(s.dead) + "/" + N(s.unconscious)
                     + "/" + N(s.carried) + "/" + N(s.inSomething) + "/" + N(s.chained) + "/" + N(s.slaveState);
    }
    std::vector<NearSeen> notCounted;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (*swfallen::NotCountedWhy(rows[i], swfallen::kEnemyNearUnits) == 0 || FactionPod(who[i]) == mine) continue;   /* the squadmate's own faction: never an enemy, not listed */
        NearSeen ns; ns.dist = rows[i].dist; ns.row = i; notCounted.push_back(ns);
    }
    std::sort(notCounted.begin(), notCounted.end());
    *skipped = (int)notCounted.size();
    for (size_t k = 0; k < notCounted.size() && k < kUnableShown; ++k)
    {
        const size_t i = notCounted[k].row;
        std::string nm; NameOf(who[i], &nm);
        ::Faction* f = FactionPod(who[i]);
        std::string fn = f != 0 ? f->getName() : std::string("?");
        *skippedText += std::string(k ? " |" : "") + " '" + nm + "' uid=" + N(FindSpawnedUid(who[i])) + " at " + F1(rows[i].dist / 10.0f)
                      + " m faction='" + fn + "' why=" + swfallen::NotCountedWhy(rows[i], swfallen::kEnemyNearUnits)
                      + " isEnemyOf=" + N(charEnemy[i]) + " playersStanding=" + (pairLevel[i] == -2 ? std::string("-") : std::string(PairWord(pairLevel[i])));
    }
    *out = e;
    return true;
}
// The record name of the town nearest (x, z) - fallenwire.h NearestTown over zones.cpp TownDistances; "" none.
std::string TownNear(float x, float z)
{
    std::vector<std::pair<std::string, float> > raw; unsigned faults = 0;
    if (!TownDistances(x, z, &raw, &faults)) { ++g_placeNoList; return std::string(); }
    std::vector<swfallen::TownDist> rows(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) { rows[i].name = raw[i].first; rows[i].dist = raw[i].second; }
    const int k = swfallen::NearestTown(rows);
    return k >= 0 ? rows[(size_t)k].name : std::string();
}

// The characters this game drives in its own player faction, in the mirror's order (live and dead). The squad rows feed
// swfallen::PickSquadAnchor; chars[i] is row i's character.
void OwnFactionRows(std::vector<swfallen::SquadRow>* rows, std::vector< ::Character*>* chars, int* alive, int* dead)
{
    *alive = 0; *dead = 0;
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int uid = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c) || c == 0 || !net::IsUidMine(uid)) continue;
        if (!IsPlayerFaction(c->getOwnerFactionDirect())) continue;
        swfallen::SquadRow r;
        r.squad = (unsigned long long)(uintptr_t)SquadPod(c);
        r.alive = c->hasDied() ? 0 : 1;
        if (r.alive) ++*alive; else ++*dead;
        rows->push_back(r);
        chars->push_back(c);
    }
}

std::string StatsWords(int have, const unsigned int* raw)
{
    if (!have) return "unread";
    float v[3]; std::memcpy(&v[0], &raw[0], 4); std::memcpy(&v[1], &raw[coopstats::kStatsAthleticsIndex], 4); std::memcpy(&v[2], &raw[39], 4);
    return "strength=" + F1(v[0]) + " athletics=" + F1(v[1]) + " attack=" + F1(v[2]);
}

__declspec(noinline) void TakeSnapshot(unsigned int uid, int cause, int unique)
{
    if (!swfallen::FallenNoteOnce(&g_seen, uid)) { ++g_snapRepeat; return; }
    ::Character* c = FindSpawned(uid);
    if (c == 0) { ++g_snapGone; DebugLog("[FALLEN] uid=" + N(uid) + " died and was gone before its snapshot (snapGone=" + N(g_snapGone) + ")"); return; }
    ::Faction* f = c->getOwnerFactionDirect();
    if (!IsPlayerFaction(f))
    {
        ++g_snapNotOwnFaction;
        if (g_notOwnLogged < 10) { ++g_notOwnLogged; DebugLog("[FALLEN] uid=" + N(uid) + " died - not of this player's faction, not kept (snapNotOwnFaction=" + N(g_snapNotOwnFaction) + ")"); }
        return;
    }
    swfallen::Fallen s;
    s.uid = uid;
    ::GameData* gd = c->getRecordDirect();
    s.templateName = (gd != 0) ? gd->name : std::string();
    if (s.templateName.empty()) { ++g_snapGone; DebugLog("[FALLEN] uid=" + N(uid) + " died - its template record is unreadable, not kept"); return; }
    NameOf(c, &s.name);
    s.race = GroundRaceName(c);
    s.unique = unique ? 1 : 0;
    if (s.unique) s.sid = gd->stringID;   /* the key the world server keeps a named character's state under */
    float age = 0.0f;
    s.animal = ReadAnimalAge01(c, &age) ? 1 : 0;
    s.age = s.animal ? age : 0.0f;
    s.statsHave = StatsReadOwned(c, s.stats) ? 1 : 0;
    if (!s.statsHave) ++g_snapNoStats;
    RecordCopy rec;
    if (CaptureAppearanceRecord(c, &rec)) SerialiseRecord(rec, &s.look); else ++g_snapNoLook;
    Ogre::Vector3 pos(0.0f, 0.0f, 0.0f);
    SafeReadPosition(c, &pos);
    s.x = pos.x; s.y = pos.y; s.z = pos.z;
    s.diedUnix = (unsigned long long)std::time(0);
    s.cause = cause;
    s.place = TownNear(s.x, s.z);
    if (s.place.empty()) ++g_placeNone; else ++g_placeKnown;
    s.day = StoreGameDay();
    float blood = 0.0f, hunger = 0.0f;
    const int medRead = MedPod(c, &blood, &hunger);
    s.why = swfallen::DeathWhy(cause, medRead, blood, hunger);
    HandPod(c, &s.handIndex, &s.handSerial);
    std::vector<swfallen::SquadRow> rows; std::vector< ::Character*> chars; int alive = 0, dead = 0;
    OwnFactionRows(&rows, &chars, &alive, &dead);
    const std::string squadText = X(SquadPod(c));
    // Every read is done: from here on only this snapshot is touched, so a fault above sends nothing.
    std::vector<char> bytes;
    swfallen::EncodeFallen(s, &bytes);
    const bool queued = StoreLinkGen() > 0;   /* a game that has never linked to a world server keeps no list */
    bool added = false;
    if (queued)
    {
        AddWait w; w.f = s;
        std::vector<char> up; swfallen::EncodeAdd(&up, s);
        added = StoreSendFallen(up);
        if (added) { ++g_adds; w.gen = StoreLinkGen(); w.sentMs = ::GetTickCount(); w.tries = 1; } else ++g_addsNotSent;
        if ((int)g_addWait.size() >= swfallen::kFallenKeep) g_addWait.erase(g_addWait.begin());
        g_addWait.push_back(w);
    }
    ++g_snaps;
    if (s.animal) ++g_snapAnimals;
    if (s.unique) ++g_snapUniques;
    DebugLog("[FALLEN] snapshot #" + N(g_snaps) + " uid=" + N(uid) + " name='" + s.name + "' template='" + s.templateName
             + "' race='" + s.race + "' unique=" + N(s.unique) + (s.unique ? " sid='" + s.sid + "'" : std::string()) + " animal=" + N(s.animal) + " age=" + F1(s.age)
             + " stats=" + StatsWords(s.statsHave, s.stats) + " look=" + N(rec.Entries()) + " entries"
             + " at=" + F1(s.x) + "," + F1(s.y) + "," + F1(s.z) + " place='" + swfallen::PlaceWords(s.place) + "'"
             + " cause=" + swfallen::CauseWord(s.cause) + " day=" + N(s.day) + " how='" + swtab::CauseWords(s.why) + "' (blood=" + F1(blood)
             + " hunger=" + F1(hunger) + (medRead ? std::string() : std::string(" unread")) + ")"
             + " hand=" + N(s.handIndex) + "/" + N(s.handSerial) + " bytes=" + N((long long)bytes.size())
             + (added ? " -> ADD sent to the world server (the list is its)" : queued ? " -> ADD waits for the world-server link (sent when it is up)"
                      : " -> NOT kept: this game has no world server (it keeps no fallen list)")
             + " | after the death: still in this player's faction=1 squad=" + squadText
             + " own-faction characters here alive/dead=" + N(alive) + "/" + N(dead));
}

// The snapshot's reads (the character, its record, faction, appearance record, stats) under the fault guard. No C++ object
// lives in this frame (C2712); a fault is counted and the death is not kept.
int TakeSnapshotGuarded(unsigned int uid, int cause, int unique)
{
    __try { TakeSnapshot(uid, cause, unique); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void FinishPending()
{
    for (size_t i = 0; i < g_pending.size(); )
    {
        Pending& p = g_pending[i];
        const int r = OwnLookResult(p.uid);   // appearance.cpp gives a final answer within 60 s
        if (r == 0) { ++i; continue; }
        ::Character* c = FindSpawned(p.uid);
        std::string raceAfter = "?", statsAfter = "unread", limbs = "unreadable", items = "unreadable";
        int statsMatch = -1;
        if (c != 0)
        {
            raceAfter = GroundRaceName(c);
            unsigned int now[swfallen::kStatsCount];
            if (StatsReadOwned(c, now))
            {
                statsAfter = StatsWords(1, now);
                statsMatch = (p.statsHave && std::memcmp(now, p.stats, sizeof(now)) == 0) ? 1 : 0;
            }
            int record = 0;
            const int whole = LimbsWholePod(c, &record);
            if (whole >= 0) limbs = N(whole) + " of 4 whole" + (record ? " (limb record present)" : " (no limb record)");
            ::Inventory* inv = InventoryPod(c);
            if (inv != 0) items = N(ItInvItemCount(inv));
        }
        if (r == 1 || r == 2) ++g_backLookApplied; else ++g_backLookFailed;
        const char* lookWords = r == 1 ? "applied, APPEARANCE + CLOTHING sent"
                              : r == 2 ? "applied, a send FAILED - own look and empty kit handed to the settle send"
                              : r == 3 ? "LATE: not applied in 60 s - own look and empty kit handed to the settle send; the saved look stays queued and is applied and sent when the body passes the gates"
                              : r == 4 ? "the apply FAILED - own look and empty kit handed to the settle send"
                              : "NOT applied (character gone)";
        DebugLog("[FALLEN] bring-back done uid=" + N(p.uid) + " (was uid " + N(p.deadUid) + ") name='" + p.name + "' template='"
                 + p.tmpl + "' road=" + swfallen::RoadWord(p.road) + " look=" + lookWords
                 + " race before death='" + p.raceBefore + "' after='" + raceAfter + "' (" + (p.raceBefore == raceAfter ? "same" : "DIFFERENT") + ")"
                 + " stats before=" + StatsWords(p.statsHave, p.stats) + " after=" + statsAfter
                 + " all44Equal=" + N(statsMatch) + " limbs=" + limbs + " items=" + items);
        g_pending.erase(g_pending.begin() + (long)i);
    }
}

// The ADDs, TAKEs and ALIVEs the world server has not acted on, and an owed SAVED: sent again when due (fallenwire.h RetryDue).
// A TAKE or ALIVE made in an earlier world waits for Reconcile to judge it against this one first.
void PumpWaiting(int gen)
{
    const unsigned int now = ::GetTickCount();
    for (size_t i = 0; i < g_addWait.size(); )
    {
        AddWait& w = g_addWait[i];
        if (!swfallen::RetryDue(w.gen, w.sentMs, gen, now)) { ++i; continue; }
        if (w.tries >= swfallen::kAddTries)
        {
            ++g_addsDropped;
            ErrorLog("[FALLEN] the ADD of uid " + N(w.f.uid) + " '" + w.f.name + "' was sent " + N(w.tries) + " times and the world server never listed it - dropped (addsDropped=" + N(g_addsDropped) + ")");
            g_addWait.erase(g_addWait.begin() + (long)i);
            continue;
        }
        std::vector<char> up; swfallen::EncodeAdd(&up, w.f);
        if (!StoreSendFallen(up)) return;
        ++w.tries; ++g_addsResent; w.gen = gen; w.sentMs = now;
        DebugLog("[FALLEN] ADD of uid " + N(w.f.uid) + " '" + w.f.name + "' sent again (send " + N(w.tries) + ") - the world server has not listed it");
        ++i;
    }
    for (size_t i = 0; i < g_taken.size(); ++i)
    {
        swfallen::Taken& t = g_taken[i];
        if (t.epoch != g_worldEpoch || !swfallen::RetryDue(t.gen, t.sentMs, gen, now)) continue;
        std::vector<char> b;
        if (t.kind == swfallen::kUpAlive) swfallen::EncodeAlive(&b, t.uid); else swfallen::EncodeTake(&b, t.uid, t.newSerial, t.takenUnix);
        if (!StoreSendFallen(b)) return;
        ++g_takesResent; t.gen = gen; t.sentMs = now;
        DebugLog("[FALLEN] " + std::string(t.kind == swfallen::kUpAlive ? "ALIVE" : "TAKE") + " of uid " + N(t.uid) + " sent again - the world server's list still holds it");
    }
    if (!g_savedOwed.empty() && swfallen::RetryDue(g_savedOwedGen, g_savedOwedMs, gen, now))
    {
        std::vector<char> b; swfallen::EncodeSaved(&b, g_savedOwed);
        if (StoreSendFallen(b))
        {
            const bool again = g_savedOwedGen == gen;
            g_savedOwedGen = gen; g_savedOwedMs = now; ++g_savedSent;
            DebugLog("[FALLEN] SAVED of " + N((long long)g_savedOwed.size()) + " bring-back(s) sent" + (again ? " again - the world server still holds them pending" : " on this link"));
        }
    }
}
// One living character of this player's faction: its handle serial and its record (0 / null otherwise or unreadable).
unsigned int LiveOwnPod(::Character* c, ::GameData** gd)
{
    *gd = 0;
    __try
    {
        if (c->hasDied() || !IsPlayerFaction(c->getOwnerFactionDirect())) return 0;
        *gd = c->getRecordDirect();
        return c->getHandle().serial;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *gd = 0; return 0; }
}
// Every living character of this player's faction in the loaded world, read for the list's checks (fallenwire.h LiveChar).
void ReadLive(std::vector<swfallen::LiveChar>* live)
{
    live->clear();
    if (coop::GameWorldPtr() == 0) return;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        if (*it == 0) continue;
        ::GameData* gd = 0;
        const unsigned int sr = LiveOwnPod(*it, &gd);
        if (sr == 0) continue;
        swfallen::LiveChar lc;
        lc.serial = sr;
        lc.templateName = gd != 0 ? gd->name : std::string();
        NameOf(*it, &lc.name);
        lc.race = GroundRaceName(*it);
        live->push_back(lc);
    }
}
// Every living character of this player's faction THIS GAME DRIVES (the mirror's own uids): the characters the price counts.
// A copy of another game's character is never one, whatever its handle serial reads here.
void ReadOwnLive(std::vector<swfallen::LiveChar>* live)
{
    live->clear();
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int uid = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c) || c == 0 || !net::IsUidMine(uid)) continue;
        ::GameData* gd = 0;
        const unsigned int sr = LiveOwnPod(c, &gd);
        if (sr == 0) continue;
        swfallen::LiveChar lc;
        lc.serial = sr;
        lc.templateName = gd != 0 ? gd->name : std::string();
        NameOf(c, &lc.name);
        lc.race = GroundRaceName(c);
        live->push_back(lc);
    }
}
// A brought-back character's death this game knows of beyond the server's mark: a row the last TABLE lists, or this game's own
// ADD the server has not listed yet, with that handle serial.
bool DeathKnown(unsigned int serial)
{
    if (swfallen::SerialDied(serial, g_table)) return true;
    for (size_t i = 0; i < g_addWait.size(); ++i) if (serial != 0 && g_addWait[i].f.handSerial == serial) return true;
    return false;
}
// Every bring-back this game knows of (fallenwire.h BackKey): the last TABLE's back rows and pending rows (by name only while
// not known dead), and this game's own TAKEs the server has not answered (their serial is this session's, so by serial only).
void CollectKeys(std::vector<swfallen::BackKey>* keys)
{
    keys->clear();
    for (size_t i = 0; i < g_back.size(); ++i)
    {
        swfallen::BackKey k; k.newSerial = g_back[i].newSerial; k.origSerial = g_back[i].origSerial;
        k.byName = (g_back[i].diedUid == 0 && !DeathKnown(g_back[i].newSerial)) ? 1 : 0;
        k.name = g_back[i].name; k.templateName = g_back[i].templateName; k.race = g_back[i].race;
        keys->push_back(k);
    }
    for (size_t i = 0; i < g_pendWire.size(); ++i)
    {
        swfallen::BackKey k; k.newSerial = g_pendWire[i].newSerial; k.origSerial = g_pendWire[i].row.handSerial;
        k.byName = (g_pendWire[i].diedUid == 0 && !DeathKnown(g_pendWire[i].newSerial)) ? 1 : 0;
        k.name = g_pendWire[i].row.name; k.templateName = g_pendWire[i].row.templateName; k.race = g_pendWire[i].row.race;
        keys->push_back(k);
    }
    for (size_t i = 0; i < g_taken.size(); ++i)
    {
        if (g_taken[i].kind != swfallen::kUpTake) continue;
        swfallen::BackKey k; k.newSerial = g_taken[i].newSerial;
        keys->push_back(k);
    }
}
// A, read now: this player's brought-back characters alive at this moment (names: who was counted, in the order counted).
swfallen::BackCount AliveNow(std::vector<std::string>* names = 0)
{
    std::vector<swfallen::BackKey> keys; std::vector<swfallen::LiveChar> live; std::vector<size_t> counted;
    CollectKeys(&keys);
    ReadOwnLive(&live);
    const swfallen::BackCount c = swfallen::CountBackAlive(keys, live, &counted);
    if (names != 0)
    {
        names->clear();
        for (size_t i = 0; i < counted.size(); ++i) names->push_back(live[counted[i]].name);
    }
    return c;
}
// THE PURSE: the player faction's Ownerships (hire.cpp OwnershipsPod / MoneyPod / TakeMoneyPod) - read, taken from and (TEST) set
// through the one pointer. 0 = no purse. A pointer is used only when it is plausible (hire.cpp Plaus: above 64 KiB, below the top
// of user space).
bool PursePlaus(const void* p) { return p != 0 && (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x00007FFFFFFFFFFFull; }
void* PursePod()
{
    ::Faction* f = LocalPlayerFaction();
    if (!PursePlaus(f)) return 0;
    __try { void* o = *(void**)((char*)f + kFacOwnershipsOff); return PursePlaus(o) ? o : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int PurseMoneyPod(void* own, int* out)   // 1 read, 0 no purse / a fault
{
    if (own == 0) return 0;
    __try { *out = *(const int*)((const char*)own + kOwnMoneyOff); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int PurseSetPod(void* own, int v)   // TEST: 1 written, 0 no purse / a fault
{
    if (own == 0) return 0;
    __try { *(int*)((char*)own + kOwnMoneyOff) = v; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// Ownerships::takeMoney (vtable slot 0): 1 paid, 0 refused (short - nothing taken), -1 no purse or a fault.
typedef bool (*OwnTakeMoneyFn)(void* ownerships, int amount);
int PurseTakePod(void* own, int amount)
{
    if (!PursePlaus(own)) return -1;
    __try
    {
        void** vt = *(void***)own;
        if (!PursePlaus(vt) || !PursePlaus(vt[0])) return -1;
        const OwnTakeMoneyFn fn = (OwnTakeMoneyFn)vt[0];
        return fn(own, amount) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// bring-backs waiting for a save: the last TABLE's pending rows and this game's TAKEs the server has not listed as pending yet
int PendingNow()
{
    int n = (int)g_pendWire.size();
    for (size_t i = 0; i < g_taken.size(); ++i)
    {
        if (g_taken[i].kind != swfallen::kUpTake) continue;
        bool listed = false;
        for (size_t k = 0; k < g_pendWire.size(); ++k) if (g_pendWire[k].uid == g_taken[i].uid) { listed = true; break; }
        if (!listed) ++n;
    }
    return n;
}
std::string FeeWords(const swfee::Price& p, const swfallen::BackCount& c)
{
    return "A=" + N(c.alive) + " (by serial " + N(c.bySerial) + ", by name " + N(c.byName) + ") rule=" + swfee::GrowthName(p.growth)
         + " amount=" + N(p.amount) + " price=" + N(p.price) + " (" + swfee::RuleWords(p) + ")";
}
std::string RowName(unsigned int uid)
{
    for (size_t k = 0; k < g_table.size(); ++k) if (g_table[k].uid == uid) return g_table[k].name;
    for (size_t k = 0; k < g_pendWire.size(); ++k) if (g_pendWire[k].uid == uid) return g_pendWire[k].row.name;
    return std::string();
}
// The list follows the save (fallenwire.h Reconcile). Runs when a TABLE arrives and once after each world load (g_reconWanted),
// with this link's TABLE and a running world that has at least one living character of this player's faction (an empty walk is
// not yet a loaded world - looked at again next tick, the request kept).
void Reconcile(int gen)
{
    if (!g_reconWanted || g_tableGen != gen || !GameplayRunning()) return;
    std::vector<swfallen::LiveChar> live;
    ReadLive(&live);
    if (live.empty()) return;
    g_reconWanted = false;
    const bool checkPending = g_reconEpoch != g_worldEpoch;
    const unsigned int now = ::GetTickCount();
    // the TAKEs and ALIVEs an earlier world left unanswered, judged against this one (fallenwire.h TakenStillTrue)
    bool dropped = false;
    for (size_t i = 0; i < g_taken.size(); )
    {
        swfallen::Taken& t = g_taken[i];
        if (t.epoch == g_worldEpoch) { ++i; continue; }
        if (swfallen::TakenStillTrue(t, live)) { t.epoch = g_worldEpoch; ++i; continue; }
        DebugLog("[FALLEN] the unanswered " + std::string(t.kind == swfallen::kUpAlive ? "ALIVE" : "TAKE") + " of uid " + N(t.uid) + " '" + RowName(t.uid)
                 + "' is not true in this loaded world - dropped (" + (t.kind == swfallen::kUpAlive ? "the character is not alive here" : "the brought-back character is not here; the row stays the world server's") + ")");
        g_taken.erase(g_taken.begin() + (long)i);
        dropped = true;
    }
    if (dropped) swfallen::TableApply(g_table, &g_taken, &g_list);
    std::vector<swfallen::PendWire> pend;   /* a bring-back made in this world is not the load's question */
    for (size_t i = 0; i < g_pendWire.size(); ++i)
        if (std::find(g_unsaved.begin(), g_unsaved.end(), g_pendWire[i].uid) == g_unsaved.end()) pend.push_back(g_pendWire[i]);
    swfallen::Reconciled out;
    swfallen::Reconcile(g_list, pend, live, checkPending, &out);
    for (size_t i = 0; i < out.alive.size(); ++i)
    {
        bool held = false;
        for (size_t t = 0; t < g_taken.size(); ++t) if (g_taken[t].uid == out.alive[i]) { held = true; break; }
        if (held) continue;
        std::vector<char> b; swfallen::EncodeAlive(&b, out.alive[i]);
        swfallen::Taken tk; tk.uid = out.alive[i]; tk.kind = swfallen::kUpAlive; tk.epoch = g_worldEpoch;
        for (size_t k = 0; k < g_list.size(); ++k) if (g_list[k].uid == out.alive[i]) { tk.rowSerial = g_list[k].handSerial; g_list.erase(g_list.begin() + (long)k); break; }
        if (StoreSendFallen(b)) { tk.gen = gen; tk.sentMs = now; ++g_aliveSent; }
        g_taken.push_back(tk);
        DebugLog("[FALLEN] uid " + N(out.alive[i]) + " '" + RowName(out.alive[i]) + "' is ALIVE in this game's loaded world (its save predates the death) - ALIVE sent, the row leaves the list");
    }
    if (!checkPending) return;
    if (!out.saved.empty())
    {
        std::vector<char> b; swfallen::EncodeSaved(&b, out.saved);
        if (!StoreSendFallen(b)) { g_reconWanted = true; return; }
        ++g_savedSent;
        for (size_t i = 0; i < out.saved.size(); ++i)
            DebugLog("[FALLEN] the bring-back of uid " + N(out.saved[i]) + " '" + RowName(out.saved[i]) + "' is in this game's loaded world - SAVED sent (final)");
    }
    for (size_t i = 0; i < out.undo.size(); ++i)
    {
        std::vector<char> b; swfallen::EncodeUndo(&b, out.undo[i]);
        if (!StoreSendFallen(b)) { g_reconWanted = true; return; }
        ++g_undoSent;
        DebugLog("[FALLEN] the bring-back of uid " + N(out.undo[i]) + " '" + RowName(out.undo[i]) + "' is NOT in this game's loaded world (no save had it) - UNDO sent, the row goes back on the list");
    }
    for (size_t i = 0; i < out.held.size(); ++i)
        DebugLog("[FALLEN] the bring-back of named uid " + N(out.held[i]) + " '" + RowName(out.held[i]) + "' is not found in this loaded world, and nothing shows the save predates it - kept pending (a save or a later load settles it)");
    ++g_reconRuns;
    g_reconEpoch = g_worldEpoch;
    DebugLog("[FALLEN] after the world load: " + N((long long)live.size()) + " living character(s) of this player's faction read; "
             + N((long long)out.alive.size()) + " listed row(s) alive in the save, " + N((long long)out.saved.size()) + " bring-back(s) saved, "
             + N((long long)out.undo.size()) + " undone, " + N((long long)out.held.size()) + " named kept pending");
}

} // namespace

void ResurrectSaveFinished(const std::string& saveName)
{
    if (g_unsaved.empty()) return;
    for (size_t i = 0; i < g_unsaved.size(); ++i)
        if (std::find(g_savedOwed.begin(), g_savedOwed.end(), g_unsaved[i]) == g_savedOwed.end()) g_savedOwed.push_back(g_unsaved[i]);
    const size_t n = g_unsaved.size();
    g_unsaved.clear();
    g_savedOwedGen = -1;   /* owed again on this link: PumpWaiting sends it */
    const bool linked = StoreWelcomedThisLink() != 0;
    if (linked) PumpWaiting(StoreLinkGen());
    DebugLog("[FALLEN] this game's save '" + saveName + "' finished with " + N((long long)n) + " bring-back(s) in it - SAVED "
             + (linked && g_savedOwedGen >= 0 ? "sent" : "owed (sent when the link is up)"));
}

void ResurrectNoteDeath(void* character, unsigned long long callerRet, int unique)
{
    const unsigned int uid = FindSpawnedUid(character);
    if (uid == 0 || !net::IsUidMineAnyThread(uid)) { if (uid != 0) ::InterlockedIncrement64(&g_notMine); return; }
    // The detour calls this for every let-through death call. The engine sets the death flag before it calls (medicalUpdate,
    // and the kill lever), so "was it alive before the call" cannot be read here; one snapshot per uid is kept on the main
    // thread instead (FallenNoteOnce).
    const LONG64 v = (LONG64)uid | ((LONG64)(DeathCallerCause(callerRet) & 0xFF) << 32) | ((LONG64)(unique ? 1 : 0) << 40);
    for (int i = 0; i < kRing; ++i)
        if (::InterlockedCompareExchange64(&g_ring[i], v, 0) == 0) { ::InterlockedIncrement64(&g_noted); return; }
    ::InterlockedIncrement64(&g_ringFull);
}

void ResurrectTick()
{
    const bool blocked = EngineWritesBlocked();   // a world loading or closing: its characters are not read
    for (int i = 0; i < kRing; ++i)
    {
        const LONG64 v = ::InterlockedExchange64(&g_ring[i], 0);
        if (v == 0) continue;
        if (blocked) { ++g_snapBlocked; continue; }
        if (!TakeSnapshotGuarded((unsigned int)(v & 0xFFFFFFFF), (int)((v >> 32) & 0xFF), (int)((v >> 40) & 1)))
        {
            ++g_snapFault;
            ErrorLog("[FALLEN] uid=" + N((long long)(v & 0xFFFFFFFF)) + " died - reading it for the snapshot FAULTED, not kept (snapFault=" + N(g_snapFault) + ")");
        }
    }
    if (!g_pending.empty()) FinishPending();
    // The list is asked for once per link and per clearing when no TABLE has come from this link (a world was torn down while
    // the link stayed); a new link's WELCOME brings it anyway.
    if (blocked || StoreWelcomedThisLink() == 0) return;
    const int gen = StoreLinkGen();
    PumpWaiting(gen);
    Reconcile(gen);
    if (g_tableGen == gen || g_askedGen == gen) return;
    g_askedGen = gen;
    std::vector<char> b; swfallen::EncodeAsk(&b);
    ++g_asks;
    DebugLog(std::string("[FALLEN] no list from this link since this game's copy was cleared - ") + (StoreSendFallen(b) ? "asked the world server for it" : "the ASK was NOT sent (no link)"));
}

void ResurrectArrive(const std::vector<char>& payload)
{
    std::vector<swfallen::Fallen> table;
    std::vector<swfallen::PendWire> pend;
    std::vector<swfallen::BackRow> back;
    if (!swfallen::DecodeTable(payload.empty() ? 0 : &payload[0], payload.size(), &table, &pend, &back))
    {
        ++g_tableMalformed;
        if (g_tableMalformed <= 5) ErrorLog("[FALLEN] a FALLEN message of " + N((long long)payload.size()) + " bytes did not decode - ignored (malformed " + N(g_tableMalformed) + ")");
        return;
    }
    ++g_tables;
    const int gen = StoreLinkGen();
    swfallen::TableApply(table, &g_taken, &g_list);
    g_table = table;
    g_reconWanted = true;
    g_pendWire = pend;
    g_back.swap(back);
    g_tableGen = gen;
    /* an ADD is done once the server lists it (or holds it as pending: it was listed, then taken) */
    for (size_t i = 0; i < g_addWait.size(); )
    {
        bool known = false;
        for (size_t k = 0; k < table.size() && !known; ++k) if (table[k].uid == g_addWait[i].f.uid) known = true;
        for (size_t k = 0; k < pend.size() && !known; ++k) if (pend[k].uid == g_addWait[i].f.uid) known = true;
        if (known) g_addWait.erase(g_addWait.begin() + (long)i); else ++i;
    }
    /* an owed SAVED is done once the server no longer holds those bring-backs as pending */
    for (size_t i = 0; i < g_savedOwed.size(); )
    {
        bool still = false;
        for (size_t k = 0; k < pend.size(); ++k) if (pend[k].uid == g_savedOwed[i]) { still = true; break; }
        if (still) ++i; else g_savedOwed.erase(g_savedOwed.begin() + (long)i);
    }
    std::string t = N((long long)table.size()) + " row(s)";
    for (size_t i = 0; i < table.size(); ++i) t += std::string(i ? ", " : " [") + N((long long)i) + " '" + table[i].name + "'" + (table[i].unique ? " named" : "") + (i + 1 == table.size() ? "]" : "");
    long long backDied = 0;
    for (size_t i = 0; i < g_back.size(); ++i) if (g_back[i].diedUid != 0) ++backDied;
    t += "; " + N((long long)pend.size()) + " pending (brought back, not yet saved); " + N((long long)g_back.size()) + " back (brought back, in a save; "
         + N(backDied) + " known dead); this game's copy " + N((long long)g_list.size()) + " row(s), "
         + N((long long)g_taken.size()) + " taken here and not yet dropped, " + N((long long)g_addWait.size()) + " ADD(s) not yet listed";
    if (t != g_tableSaid) { g_tableSaid = t; DebugLog("[FALLEN] list from the world server: " + t); }
    if (!EngineWritesBlocked() && StoreWelcomedThisLink() != 0) Reconcile(gen);
}

void ResurrectLinkLost()
{
    if (!g_list.empty() || g_tableGen >= 0) DebugLog("[FALLEN] the world-server link went down - this game's copy of the list (" + N((long long)g_list.size()) + " row(s)) cleared");
    g_list.clear(); g_table.clear(); g_pendWire.clear(); g_back.clear(); g_tableGen = -1; g_askedGen = -1; g_tableSaid.clear();
}

void ResurrectOptionsMapBegin() { g_optSeen = 0; }
bool ResurrectOption(const std::string& key, const std::string& value)
{
    if (!swfee::IsOptionKey(key)) return false;
    if (key == swfee::kKeyOn)
    {
        g_optSeen |= 1;
        const int c = swfee::OnCode(value);
        if (c < 0) { ErrorLog("[FALLEN] OPTIONS: the world server sent resurrect='" + value + "', which this build cannot read - IGNORED, this game keeps '" + (g_optOn ? "on" : "off") + "'"); return true; }
        if (c != g_optOn) DebugLog(std::string("[FALLEN] OPTIONS: resurrection is now ") + (c ? "ON" : "OFF") + " (the host's option)");
        g_optOn = c;
    }
    else if (key == swfee::kKeyAmount)
    {
        g_optSeen |= 2;
        const long long a = swfee::AmountCode(value);
        if (a < 0) { ErrorLog("[FALLEN] OPTIONS: the world server sent resurrectfee='" + value + "', which this build cannot read - IGNORED, this game keeps " + N(g_optAmount)); return true; }
        if (a != g_optAmount) DebugLog("[FALLEN] OPTIONS: the resurrection fee amount is now " + N(a) + " (was " + N(g_optAmount) + ")");
        g_optAmount = a;
    }
    else
    {
        g_optSeen |= 4;
        const int g = swfee::GrowthCode(value);
        if (g < 0) { ErrorLog("[FALLEN] OPTIONS: the world server sent resurrectgrowth='" + value + "', which this build cannot read - IGNORED, this game keeps '" + swfee::GrowthName(g_optGrowth) + "'"); return true; }
        if (g != g_optGrowth) DebugLog(std::string("[FALLEN] OPTIONS: the resurrection fee grows ") + swfee::GrowthName(g) + " (was " + swfee::GrowthName(g_optGrowth) + ")");
        g_optGrowth = g;
    }
    return true;
}
void ResurrectOptionsMapEnd(bool complete)
{
    if (!complete) return;
    if (!(g_optSeen & 1) && g_optOn != 0) { g_optOn = 0; DebugLog("[FALLEN] OPTIONS: the world's map names no resurrect - resurrection is OFF (the default)"); }
    if (!(g_optSeen & 2) && g_optAmount != swfee::kDefaultAmount) { g_optAmount = swfee::kDefaultAmount; DebugLog("[FALLEN] OPTIONS: the world's map names no resurrectfee - the amount is " + N(swfee::kDefaultAmount) + " (the default)"); }
    if (!(g_optSeen & 4) && g_optGrowth != swfee::kDefaultGrowth) { g_optGrowth = swfee::kDefaultGrowth; DebugLog(std::string("[FALLEN] OPTIONS: the world's map names no resurrectgrowth - the fee grows ") + swfee::GrowthName(swfee::kDefaultGrowth) + " (the default)"); }
}

// THE BRING-BACK ROAD (the TEST lever's `resurrect <n> ...` and the FALLEN tab): row n of this game's copy comes back beside the
// character besideUid, or else the first living member of this player's squad `want`. src names the caller in the log lines
// ("TEST" for the lever). expectPrice >= 0: refused when the price read now differs. *out: the result the tab shows.
static std::string BringBack(const char* src, const std::string& args, int n, int want, unsigned int besideUid, long long expectPrice, BringBackOut* out)
{
    *out = BringBackOut();
    if (n >= 0 && n < (int)g_list.size()) out->name = g_list[(size_t)n].name.empty() ? g_list[(size_t)n].templateName : g_list[(size_t)n].name;
    const std::string tag = std::string("bring-back (") + src + ") ";
    int ref = swtab::kRefNone;
    const char* why = 0;
    if (!g_optOn) { why = "resurrection is off in the host options"; ref = swtab::kRefOff; ++g_feeRefusedOff; }
    else if (EngineWritesBlocked()) { why = "the world is loading or closing"; ref = swtab::kRefNoWorld; }
    else if (StoreWelcomedThisLink() == 0) { why = "no world-server link (the list and the bring-back are the world server's)"; ref = swtab::kRefNoWorld; }
    else if (n < 0 || n >= (int)g_list.size()) { why = "no such entry on the fallen list"; ref = swtab::kRefFailed; }
    std::vector<swfallen::SquadRow> rows; std::vector< ::Character*> chars; int alive = 0, dead = 0, squads = 0;
    ::Character* anchor = 0;
    unsigned int anchorUid = besideUid;
    if (why == 0)
    {
        OwnFactionRows(&rows, &chars, &alive, &dead);
        if (besideUid != 0) anchor = FindSpawned(besideUid);
        else
        {
            const int anchorRow = swfallen::PickSquadAnchor(rows, want, &squads);
            if (anchorRow < 0) { why = "no such squad with a living member of this player"; ref = swtab::kRefNoMate; }
            else { anchor = chars[(size_t)anchorRow]; anchorUid = FindSpawnedUid(anchor); }
        }
        if (anchor != 0) NameOf(anchor, &out->mate);
    }
    // the placement rules (fallenwire.h PlaceVerdict): the squadmate's own states, then no enemy within 50 m
    Ogre::Vector3 at(0.0f, 0.0f, 0.0f);
    int rule = -1;
    swfallen::MateState mate;
    swfallen::EnemyCount enemies;
    std::string enemyName, unableText, skippedText;
    int skipped = 0;
    if (why == 0)
    {
        ReadMate(anchor, anchorUid, &mate);
        rule = swfallen::BesideVerdict(mate);
        if (rule == swfallen::kBesideOk)
        {
            if (!SafeReadPosition(anchor, &at)) { why = "the squadmate's position is unreadable"; ref = swtab::kRefMateGone; }
            else if (!EnemiesNear(anchor, at, &enemies, &enemyName, &unableText, &skippedText, &skipped))
            {
                ++g_enemyListBad;
                rule = swfallen::kBesideUnreadable;
                DebugLog("[FALLEN] " + tag + args + ": the loaded characters cannot be walked (no world, or an implausible list) - the enemy rule does not read (enemyListBad=" + N(g_enemyListBad) + ")");
            }
            else
            {
                g_enemyUnknown += enemies.unknown;
                g_enemyUnable += enemies.unable;
                rule = swfallen::PlaceVerdict(mate, enemies);
                DebugLog("[FALLEN] " + tag + args + ": within 50 m of uid " + N(anchorUid) + ", " + N(skipped)
                         + " character(s) of other factions NOT counted as an enemy" + (skipped > 0 ? " (nearest " + N(skipped < (int)kUnableShown ? skipped : (int)kUnableShown) + "):" + skippedText : std::string()));
            }
        }
        if (why == 0 && rule != swfallen::kBesideOk) { ++g_besideRefused[rule]; why = swfallen::BesideWord(rule); ref = swtab::RefusalOfBeside(rule); }
    }
    ActivePlatoon* squad = (why == 0) ? (ActivePlatoon*)SquadPod(anchor) : 0;
    if (why == 0 && squad == 0) { why = "the squadmate has no squad"; ref = swtab::kRefMateGone; }
    // the fee (resurrectfee.h): A and this player's money read now; the price is taken only once the new character exists
    swfallen::BackCount cnt;
    swfee::Price fee;
    int moneyBefore = 0;
    std::string feeDetail;
    void* purse = 0;
    int moneyRead = 0;
    if (why == 0 && PendingNow() >= swfallen::kFallenKeep)
    {
        ++g_feePendingFull;
        why = "40 bring-backs already wait for this player's save - the world server keeps no more until a save finishes";
        ref = swtab::kRefFailed;
    }
    if (why == 0)
    {
        cnt = AliveNow();
        fee = swfee::PriceFor(g_optAmount, g_optGrowth, cnt.alive);
        g_feeLast = cnt; g_feeLastPrice = fee.price;
        purse = PursePod();
        moneyRead = PurseMoneyPod(purse, &moneyBefore);
        feeDetail = " | fee " + FeeWords(fee, cnt) + " money=" + (moneyRead == 1 ? N(moneyBefore) : std::string("unread"));
        out->price = fee.price; out->money = moneyRead == 1 ? moneyBefore : 0;
        if (moneyRead != 1 && fee.price > 0) { why = "this player's money does not read"; ref = swtab::kRefFailed; ++g_feeMoneyUnread; }
        else if (expectPrice >= 0 && fee.price != expectPrice)
        {
            why = "the price changed since it was shown"; ref = swtab::kRefPriceChanged; ++g_feePriceChanged;
            feeDetail += " shown=" + N(expectPrice);
        }
        else if (moneyRead == 1 && (long long)moneyBefore < fee.price) { why = "not enough money for the price"; ref = swtab::kRefShort; ++g_feeRefusedShort; }
    }
    if (why != 0)
    {
        ++g_backRefused;
        std::string detail;
        if (rule > swfallen::kBesideOk)
        {
            detail = " | rule=" + std::string(swfallen::BesideTag(rule)) + " beside uid=" + N(anchorUid)
                   + " states dead/ko/carried/inSomething/chained/slave=" + N(mate.dead) + "/" + N(mate.unconscious) + "/" + N(mate.carried)
                   + "/" + N(mate.inSomething) + "/" + N(mate.chained) + "/" + N(mate.slaveState) + " mine=" + N(mate.mine) + " ownFaction=" + N(mate.ownFaction);
            if (rule == swfallen::kBesideEnemyNear)
                detail += " enemies within 50 m=" + N(enemies.within) + " nearest='" + enemyName + "' at " + F1(enemies.nearest / 10.0f) + " m"
                        + " (enemies near that cannot fight=" + N(enemies.unable) + (unableText.empty() ? std::string() : ":" + unableText) + ")";
        }
        detail += feeDetail;
        out->refusal = ref != swtab::kRefNone ? ref : swtab::kRefFailed;
        DebugLog("[FALLEN] " + tag + args + " refused: " + why + detail + " | tab words: " + swtab::RefusalTag(out->refusal));
        return std::string("error resurrect refused: ") + why + detail;
    }
    if (besideUid != 0) ++g_besideNamed;
    ++g_besideAllowed;
    const swfallen::Fallen s = g_list[(size_t)n];
    Ogre::Vector3 pos(at.x + swfallen::kBesideUnits, at.y, at.z);
    const int countBefore = alive;

    // 1. create
    unsigned int uid = 0;
    if (!CreateOwnInSquad(s.templateName, pos, squad, s.age, &uid))
    {
        ++g_backRefused;
        out->refusal = swtab::kRefFailed;
        DebugLog("[FALLEN] " + tag + "entry " + N(n) + " '" + s.name + "' FAILED: the engine did not create '" + s.templateName + "'");
        return "error resurrect: create failed for template '" + s.templateName + "'";
    }
    ::Character* c = FindSpawned(uid);
    if (c == 0)
    {
        ++g_backRefused;
        out->refusal = swtab::kRefFailed;
        DebugLog("[FALLEN] " + tag + "entry " + N(n) + " FAILED: uid " + N(uid) + " was created but is not registered here");
        return "error resurrect: the created character is not registered";
    }
    // the price, now that the person exists
    int moneyAfter = moneyBefore, paid = 0;
    if (fee.price > 0)
    {
        paid = PurseTakePod(purse, (int)fee.price);
        PurseMoneyPod(purse, &moneyAfter);
        if (paid == 1) { ++g_feeCharged; g_feeTotal += fee.price; }
        else
        {
            ++g_feeUnpaid;
            ErrorLog("[FALLEN] " + tag + "uid=" + N(uid) + ": the price " + N(fee.price) + " was NOT taken (takeMoney answered " + N(paid)
                     + ") - the character is made, unpaid (feeUnpaid=" + N(g_feeUnpaid) + ")");
        }
    }
    else ++g_feeFree;
    out->ok = 1;
    out->paid = (fee.price > 0 && paid == 1) ? fee.price : 0;
    DebugLog("[FALLEN] fee (" + std::string(src) + " bring-back) uid=" + N(uid) + " (was uid " + N(s.uid) + ") '" + s.name + "' " + FeeWords(fee, cnt)
             + " money before=" + (moneyRead == 1 ? N(moneyBefore) : std::string("unread")) + " after=" + (moneyRead == 1 ? N(moneyAfter) : std::string("unread"))
             + (fee.price == 0 ? " - free" : paid == 1 ? " - taken" : " - NOT taken"));
    // 2. no items
    ::Inventory* inv = InventoryPod(c);
    const long long itemsRolled = inv ? ItInvItemCount(inv) : -1;
    const long prevApplying = ItemApplyingEnter();
    const int cleared = inv ? ClearAllPod(inv) : 0;
    ItemApplyingLeave(prevApplying);
    const long long itemsAfterStrip = inv ? ItInvItemCount(inv) : -1;
    // 3. name and stats
    const bool named = !s.name.empty() && NameSetOwn(c, s.name);
    int statsRc = 0;
    const bool statsSet = s.statsHave && StatsApplyOwn(c, s.stats, &statsRc);
    // 4. the road (probe L3)
    const int newMove = IntAtPod(c, kMoveOff, kMoveField), newAi = IntAtPod(c, kAiOff, kAiField);
    const int refMove = IntAtPod(anchor, kMoveOff, kMoveField), refAi = IntAtPod(anchor, kAiOff, kAiField);
    std::vector<swfallen::SquadRow> r1; std::vector< ::Character*> c1; int alive1 = 0, dead1 = 0;
    OwnFactionRows(&r1, &c1, &alive1, &dead1);
    const int road = swfallen::RoadDecide(s.animal, newMove, newAi, refMove, refAi);
    int recruitRc = 0, squadRestored = 0;
    if (road == swfallen::kRoadCreateRecruit)
    {
        ++g_backRecruit;
        recruitRc = HireRecruitNoEdit(c);
        if (SquadPod(c) != (void*)squad) squadRestored = HireSetFaction(c, LocalPlayerFaction(), squad) == 1 ? 1 : -1;
    }
    const int moveAfter = IntAtPod(c, kMoveOff, kMoveField), aiAfter = IntAtPod(c, kAiOff, kAiField);
    // 5. the SPAWN on the normal road
    ::Faction* f = c->getOwnerFactionDirect();
    SendContextFor(uid, c);   // CONTEXT before SPAWN, as every announce: the copy is built in the squad's context
    const bool sent = net::SendSpawn(uid, s.templateName, pos.x, pos.y, pos.z, WireFactionName(f), false, c);
    if (sent) NoteAnnouncedOnTake(uid); else NoteNotAnnounced(uid);
    RecordCopy look;
    const bool haveLook = !s.look.empty() && DeserialiseRecord(s.look, 0, &look);
    if (haveLook) OwnLookQueue(uid, look);
    else WatchLocalRoll(uid);   // no record kept: its own look and its empty kit go out by the settle send
    // 6. off the list: out of this game's copy, and the TAKE to the world server
    swfallen::Fallen gone;
    swfallen::FallenTake(&g_list, n, &gone);
    unsigned int newIndex = 0, newSerial = 0;
    HandPod(c, &newIndex, &newSerial);
    swfallen::Taken tk; tk.uid = s.uid; tk.newSerial = newSerial; tk.kind = swfallen::kUpTake; tk.takenUnix = (unsigned long long)std::time(0);   /* the clock a snapshot's diedUnix uses */
    std::vector<char> tb; swfallen::EncodeTake(&tb, s.uid, newSerial, tk.takenUnix);
    const bool takeSent = StoreSendFallen(tb);
    tk.epoch = g_worldEpoch;
    if (takeSent) { ++g_takesSent; tk.gen = StoreLinkGen(); tk.sentMs = ::GetTickCount(); } else ++g_takesNotSent;
    g_lastBackUid = uid;
    g_backHere.insert(uid);
    g_taken.push_back(tk);
    g_unsaved.push_back(s.uid);
    std::string uniqueWords;
    if (s.unique)
    {
        ++g_backUniques;
        uniqueWords = s.sid.empty() ? std::string(" | named character: NO string id kept - the world server cannot mark it brought back")
                                    : " | named character '" + s.sid + "': " + WorldStateBroughtBack(s.sid);
    }
    ++g_backs;
    Pending p;
    p.uid = uid; p.deadUid = s.uid; p.name = s.name; p.raceBefore = s.race; p.tmpl = s.templateName; p.road = road;
    p.animal = s.animal; p.at = ::GetTickCount(); p.statsHave = s.statsHave;
    std::memcpy(p.stats, s.stats, sizeof(p.stats));
    if (haveLook) g_pending.push_back(p);
    std::vector<swfallen::SquadRow> r2; std::vector< ::Character*> c2; int alive2 = 0, dead2 = 0;
    OwnFactionRows(&r2, &c2, &alive2, &dead2);
    const std::string line = "entry " + N(n) + " uid=" + N(uid) + " (was uid " + N(s.uid) + ") name='" + s.name + "' template='"
        + s.templateName + "' animal=" + N(s.animal) + " place='" + swfallen::PlaceWords(s.place) + "'"
        + (besideUid != 0 ? std::string(" squad of the named squadmate") : " squad " + N(want < 0 ? 0 : want) + " of " + N(squads)) + "=" + X(squad)
        + " beside uid=" + N(anchorUid) + " rule=allowed (free, enemies within 50 m=0, enemies near that cannot fight=" + N(enemies.unable)
        + (unableText.empty() ? std::string() : ":" + unableText)
        + ", unreadable near=" + N(enemies.unknown) + ")"
        + " at=" + F1(pos.x) + "," + F1(pos.y) + "," + F1(pos.z)
        + " items rolled=" + N(itemsRolled) + " cleared=" + N(cleared) + " after=" + N(itemsAfterStrip)
        + " named=" + N(named ? 1 : 0) + " stats=" + N(statsSet ? 1 : 0) + "(rc " + N(statsRc) + ")"
        + " | probe L3 after create: own-faction characters " + N(countBefore) + " -> " + N(alive1)
        + " move+0x20 new/member=" + N(newMove) + "/" + N(refMove) + " +0x1A0+0xDC new/member=" + N(newAi) + "/" + N(refAi)
        + " dialogue package=not read -> road=" + swfallen::RoadWord(road)
        + (road == swfallen::kRoadCreateRecruit ? " recruit=" + N(recruitRc) + " squadRestored=" + N(squadRestored) : std::string())
        + " after: move+0x20=" + N(moveAfter) + " +0x1A0+0xDC=" + N(aiAfter) + " squad=" + X(SquadPod(c))
        + " own-faction characters=" + N(alive2) + " playerFaction=" + N(IsPlayerFaction(f) ? 1 : 0)
        + " | CONTEXT+SPAWN sent=" + N(sent ? 1 : 0) + (sent ? std::string() : std::string(" (left to the announce pass)"))
        + " look=" + (haveLook ? "waits for the body" : "none kept - own look and empty kit by the settle send")
        + " | TAKE (new serial " + N(newSerial) + ") " + (takeSent ? "sent to the world server - final once this game's save has it" : "NOT sent (no link) - sent when the link is up")
        + " list=" + N((long long)g_list.size()) + uniqueWords
        + " | fee " + FeeWords(fee, cnt) + " money " + N(moneyBefore) + " -> " + N(moneyAfter);
    DebugLog("[FALLEN] " + tag + line);
    return std::string(sent ? "ok" : "error") + " resurrect " + line;
}

std::string ResurrectLever(const std::string& args)
{
    std::istringstream is(args);
    std::string first; is >> first;
    const char* const usage = "error resurrect usage: resurrect list | resurrect <n> [squad <i> | beside <uid>] | resurrect killlast | resurrect killother <uid>"
                              " | resurrect host on|off | resurrect host fee <amount> | resurrect host growth steady|steep | resurrect price | resurrect money <n>"
                              " | resurrect tab [open | select <row> | mate <i> | press | confirm | cancel | report]";
    std::string extra;
    if (first == "host")   /* TEST: a host option to the world server, as the host's options screen would send it */
    {
        std::string a, b; is >> a;
        std::string key, value;
        if (a == "on" || a == "off") { key = swfee::kKeyOn; value = a; }
        else if (a == "fee" && (is >> b)) { key = swfee::kKeyAmount; value = b; }
        else if (a == "growth" && (is >> b)) { key = swfee::kKeyGrowth; value = b; }
        else return usage;
        if (is >> extra) return usage;
        if (!swfee::OptionValueOk(key, value)) return "error resurrect host: '" + value + "' is not a legal value for " + key;
        if (!StoreSendOption(key, value)) return "error resurrect host: no world-server link - the option is kept by the world server, not by this game";
        ++g_feeOptionsSent;
        DebugLog("[FALLEN] host option (TEST) " + key + "=" + value + " sent to the world server (it keeps it only from the host)");
        return "ok resurrect host " + key + "=" + value + " sent";
    }
    if (first == "price")   /* TEST: A, the rule and the price as a bring-back would read them now */
    {
        if (is >> extra) return usage;
        const swfallen::BackCount cnt = AliveNow();
        const swfee::Price p = swfee::PriceFor(g_optAmount, g_optGrowth, cnt.alive);
        int money = 0;
        const int mr = PurseMoneyPod(PursePod(), &money);
        g_feeLast = cnt; g_feeLastPrice = p.price;
        const std::string line = std::string("resurrection=") + (g_optOn ? "on" : "off") + " " + FeeWords(p, cnt) + " money=" + (mr == 1 ? N(money) : std::string("unread"))
                               + " backRows=" + N((long long)g_back.size()) + " pending=" + N((long long)PendingNow());
        DebugLog("[FALLEN] price (TEST) " + line);
        return "ok resurrect price " + line;
    }
    if (first == "money")   /* TEST: this player's money field is set */
    {
        std::string v;
        if (!(is >> v) || (is >> extra)) return usage;
        const long long m = swfee::AmountCode(v);
        if (m < 0) return "error resurrect money: 0 .. 2147483647";
        int before = 0;
        void* own = PursePod();
        if (PurseMoneyPod(own, &before) != 1 || PurseSetPod(own, (int)m) != 1) return "error resurrect money: this player's purse does not read";
        DebugLog("[FALLEN] money (TEST): " + N(before) + " -> " + N(m));
        return "ok resurrect money " + N(m) + " was=" + N(before);
    }
    if (first == "killlast")   /* TEST: the character this lever last brought back dies (the engine's death steps, as `kill`) */
    {
        if (is >> extra) return usage;
        if (g_lastBackUid == 0) return "error resurrect killlast: nothing brought back in this game yet";
        DebugLog("[FALLEN] killlast (TEST): uid " + N(g_lastBackUid) + ", the last character brought back here");
        return KillLever(g_lastBackUid);
    }
    if (first == "killother")   /* TEST: the first living character of this player's faction driven here, other than <uid>, dies */
    {
        unsigned int keep = 0;
        if (!(is >> keep) || keep == 0 || (is >> extra)) return usage;
        std::vector<swfallen::SquadRow> kr; std::vector< ::Character*> kc; int ka = 0, kd = 0;
        OwnFactionRows(&kr, &kc, &ka, &kd);
        for (size_t i = 0; i < kr.size(); ++i)
        {
            const unsigned int u = FindSpawnedUid(kc[i]);
            if (!kr[i].alive || u == 0 || u == keep || g_backHere.count(u) != 0) continue;
            ++g_killOther;
            DebugLog("[FALLEN] killother (TEST): uid " + N(u) + ", the first living character of this player's faction other than uid " + N(keep));
            return KillLever(u);
        }
        return "error resurrect killother: no other living character of this player's faction here that this game did not bring back";
    }
    if (first == "tab")   /* TEST: the FALLEN tab's own controls, fired as a click (fallentab.cpp) */
    {
        std::string rest; std::getline(is, rest);
        return FallenTabCommand(rest);
    }
    if (first == "list" || first.empty())
    {
        if (is >> extra) return usage;
        std::string out = "ok resurrect list count=" + N((long long)g_list.size()) + " (from " + (g_tableGen < 0 ? std::string("no table") : g_tableGen == StoreLinkGen() ? std::string("the world server, this link") : std::string("an earlier link")) + ")";
        for (size_t i = 0; i < g_list.size(); ++i)
        {
            const swfallen::Fallen& s = g_list[i];
            const std::string row = N((long long)i) + ": uid=" + N(s.uid) + " name='" + s.name + "' template='" + s.templateName
                + "' race='" + s.race + "' unique=" + N(s.unique) + (s.unique ? " sid='" + s.sid + "'" : std::string()) + " animal=" + N(s.animal) + " died=" + N((long long)s.diedUnix)
                + " at=" + F1(s.x) + "," + F1(s.z) + " place='" + swfallen::PlaceWords(s.place) + "' cause=" + swfallen::CauseWord(s.cause)
                + " day=" + N(s.day) + " how='" + swtab::CauseWords(s.why) + "'";
            DebugLog("[FALLEN] list " + row);
            out += " | " + row;
        }
        return out;
    }
    std::istringstream ns(first);
    int n = -1; ns >> n;
    int want = -1;
    unsigned int besideUid = 0;
    std::string kw;
    if (is >> kw)
    {
        if (kw == "squad") { if (!(is >> want) || want < 0) return usage; }
        else if (kw == "beside") { if (!(is >> besideUid) || besideUid == 0) return usage; }
        else return usage;
        if (is >> extra) return usage;
    }
    if (ns.fail() || n < 0) return usage;
    BringBackOut out;
    return BringBack("TEST", args, n, want, besideUid, -1, &out);
}

BringBackOut ResurrectBringBackFor(unsigned int deadUid, unsigned int besideUid, long long expectPrice)
{
    BringBackOut out;
    int n = -1;
    for (size_t i = 0; i < g_list.size(); ++i) if (g_list[i].uid == deadUid) { n = (int)i; break; }
    if (n < 0 || besideUid == 0)
    {
        out.refusal = swtab::kRefFailed;
        DebugLog("[FALLEN] bring-back (tab) uid " + N(deadUid) + " beside uid " + N(besideUid) + " refused: "
                 + (n < 0 ? "the row is no longer on this game's copy of the list" : "no squadmate picked") + " | tab words: failed");
        return out;
    }
    BringBack("tab", "row uid=" + N(deadUid) + " beside " + N(besideUid) + " shown price " + N(expectPrice), n, -1, besideUid, expectPrice, &out);
    return out;
}

void ResurrectTabRead(FallenTabRead* out)
{
    *out = FallenTabRead();
    out->on = g_optOn; out->amount = g_optAmount; out->growth = g_optGrowth;
    const swfallen::BackCount cnt = AliveNow(&out->alive);
    out->price = swfee::PriceFor(g_optAmount, g_optGrowth, cnt.alive).price;
    int money = 0;
    out->moneyRead = PurseMoneyPod(PursePod(), &money) == 1 ? 1 : 0;
    out->money = out->moneyRead ? money : 0;
    for (size_t i = 0; i < g_list.size(); ++i) out->rows.push_back(swtab::RowFor(g_list[i]));
}

/* PlayerInterface::isObjectSelected 0x7F4180 (bool, PlayerInterface* this, RootObject*): a character of the player's faction
   is looked up by its hand in the player's selection (PlayerInterface+0x208); the PlayerInterface is GameWorld+0x580
   (handoff.cpp FocusCameraPod). 1 selected, 0 not, -1 unread (no table row, no world, a fault). */
static unsigned long long kRsIsSelectedRva = 0; static coop::AddrReg kRsIsSelectedRva_reg("PlayerInterfaceIsObjectSelected", &kRsIsSelectedRva);
typedef bool (*IsObjectSelectedFn)(void* playerInterface, void* object);
static int SelectedInGamePod(void* character)
{
    if (kRsIsSelectedRva == 0 || character == 0) return -1;
    __try
    {
        void* gw = (void*)coop::GameWorldPtr();
        if (gw == 0) return -1;
        void* pi = *(void**)((char*)gw + 0x580);
        if (pi == 0) return -1;
        return ((IsObjectSelectedFn)coop::AddrAbs(kRsIsSelectedRva))(pi, character) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

void ResurrectFreeMates(std::vector<FallenMate>* out)
{
    out->clear();
    std::vector<swfallen::SquadRow> rows; std::vector< ::Character*> chars; int alive = 0, dead = 0;
    OwnFactionRows(&rows, &chars, &alive, &dead);
    std::vector<unsigned long long> order;   /* squads in the order their first member appears */
    for (size_t i = 0; i < rows.size(); ++i) if (std::find(order.begin(), order.end(), rows[i].squad) == order.end()) order.push_back(rows[i].squad);
    for (size_t q = 0; q < order.size(); ++q)
    {
        std::string squadName;
        bool squadRead = false;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].squad != order[q] || !rows[i].alive) continue;
            const unsigned int uid = FindSpawnedUid(chars[i]);
            swfallen::MateState m;
            ReadMate(chars[i], uid, &m);
            if (swfallen::BesideVerdict(m) != swfallen::kBesideOk) continue;
            if (!squadRead)
            {
                squadRead = true;
                void* pl = PlatoonPod((void*)(uintptr_t)order[q]);
                if (pl != 0) NameOf(pl, &squadName);
            }
            FallenMate fm; fm.uid = uid; fm.squad = squadName;
            fm.selected = SelectedInGamePod(chars[i]) == 1 ? 1 : 0;
            NameOf(chars[i], &fm.name);
            out->push_back(fm);
        }
    }
}

void ResurrectWorldTeardown()
{
    for (int i = 0; i < kRing; ++i) ::InterlockedExchange64(&g_ring[i], 0);
    g_list.clear(); g_table.clear(); g_pendWire.clear(); g_back.clear(); g_backHere.clear();
    g_reconWanted = true;
    g_unsaved.clear();   /* this world's bring-backs no finished save holds: the next load settles them (UNDO or SAVED) */
    ++g_worldEpoch;      /* g_taken, g_addWait and g_savedOwed stay: the world server has not answered them yet */
    g_tableGen = -1; g_askedGen = -1; g_tableSaid.clear();
    g_seen.clear();
    g_pending.clear();
    OwnLookWorldTeardown();
}

// The placement counters for the REPORT line: allowed (and how many named their squadmate), refusals per reason, near characters
// whose enemy test did not read, and the snapshots' town (known / none / no town list).
static std::string BesideReport()
{
    std::string r = " besideAllowed=" + N(g_besideAllowed) + " besideNamed=" + N(g_besideNamed) + " besideRefused[";
    for (int i = 1; i < swfallen::kBesideCodes; ++i) r += std::string(i > 1 ? "," : "") + swfallen::BesideTag(i);
    r += "]=";
    for (int i = 1; i < swfallen::kBesideCodes; ++i) r += std::string(i > 1 ? "," : "") + N(g_besideRefused[i]);
    return r + " enemyUnknown=" + N(g_enemyUnknown) + " enemyUnable=" + N(g_enemyUnable) + " enemyListBad=" + N(g_enemyListBad) + " place[known,none,noList]=" + N(g_placeKnown) + "," + N(g_placeNone) + "," + N(g_placeNoList)
             + " killOther=" + N(g_killOther);
}

// The fee counters for the REPORT line: the host options as this game holds them, bring-backs charged / free, the total taken,
// refusals (off, short money, money unread), prices not taken after the create, host-option sends, the last count and price, and
// the back rows of the last TABLE.
static std::string FeeReport()
{
    return " fee[on,amount,growth]=" + N(g_optOn) + "," + N(g_optAmount) + "," + swfee::GrowthName(g_optGrowth)
         + " feeCharged=" + N(g_feeCharged) + " feeTotal=" + N(g_feeTotal) + " feeFree=" + N(g_feeFree)
         + " feeRefused[off,short,moneyUnread,pendingFull,priceChanged]=" + N(g_feeRefusedOff) + "," + N(g_feeRefusedShort) + "," + N(g_feeMoneyUnread) + "," + N(g_feePendingFull) + "," + N(g_feePriceChanged)
         + " feeUnpaid=" + N(g_feeUnpaid) + " hostOptionsSent=" + N(g_feeOptionsSent)
         + " lastAlive[all,bySerial,byName]=" + N(g_feeLast.alive) + "," + N(g_feeLast.bySerial) + "," + N(g_feeLast.byName)
         + " lastPrice=" + N(g_feeLastPrice) + " backRows=" + N((long long)g_back.size());
}

std::string ResurrectReportLine()
{
    return "[FALLEN] REPORT noted=" + N(g_noted) + " ringFull=" + N(g_ringFull) + " notMine=" + N(g_notMine)
         + " snapshots=" + N(g_snaps) + " animals=" + N(g_snapAnimals) + " uniques=" + N(g_snapUniques)
         + " gone=" + N(g_snapGone) + " notOwnFaction=" + N(g_snapNotOwnFaction) + " noStats=" + N(g_snapNoStats)
         + " noLook=" + N(g_snapNoLook) + " adds=" + N(g_adds) + " addsNotSent=" + N(g_addsNotSent) + " tables=" + N(g_tables)
         + " tableMalformed=" + N(g_tableMalformed) + " asks=" + N(g_asks) + " list=" + N((long long)g_list.size())
         + " takesSent=" + N(g_takesSent) + " takesNotSent=" + N(g_takesNotSent) + " takesResent=" + N(g_takesResent)
         + " takenWaiting=" + N((long long)g_taken.size()) + " namedBack=" + N(g_backUniques) + " addsResent=" + N(g_addsResent)
         + " addsWaiting=" + N((long long)g_addWait.size()) + " addsDropped=" + N(g_addsDropped) + " aliveSent=" + N(g_aliveSent)
         + " savedSent=" + N(g_savedSent) + " undoSent=" + N(g_undoSent) + " unsaved=" + N((long long)g_unsaved.size())
         + " savedOwed=" + N((long long)g_savedOwed.size()) + " pending=" + N((long long)g_pendWire.size()) + " settledLoads=" + N(g_reconRuns)
         + " broughtBack=" + N(g_backs) + " refused=" + N(g_backRefused) + " recruitRoad=" + N(g_backRecruit)
         + " lookApplied=" + N(g_backLookApplied) + " lookFailed=" + N(g_backLookFailed) + " repeat=" + N(g_snapRepeat)
         + " blocked=" + N(g_snapBlocked) + " fault=" + N(g_snapFault) + BesideReport() + FeeReport();
}

} // namespace coop
