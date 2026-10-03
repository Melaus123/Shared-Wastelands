// SharedWastelandsServer.exe - the standalone sleeping-record store (persistence service P2 item 1; decisions 8/14/17/18/21/22).
//
// A small console program with no game code: it holds the canonical record files (one engine-format file + one meta
// line per squad) in a folder beside the game's saves, accepts any number of games over ENet with the plugin's own
// 5-byte framing, and forwards every write to every other connected game. The first game to connect is the
// OFF-SCREEN AUTHORITY (it alone moves sleeping squads and publishes their positions) until it leaves, then the next.
//
//   SharedWastelandsServer.exe [--port 27016] [--world <name> | --dir <folder>] [--root <top folder>] [--owner <32 hex player id>] [--max-connected N] [--first-slot N (TEST ONLY)] [--parent <pid>]
//   --parent <pid> (owner decision 183): the Kenshi that started this helper (the plugin passes it); when that process ends, crashed or
//   not, this helper quits as on its close button (QuitDrain, then the process ends and the world lock with it). Without it: as before.
//   SharedWastelandsServer.exe --unmigrate          SharedWastelandsServer.exe --migrate-selftest <folder that does not exist yet>   (TEST ONLY)
//   W2a (decision 58; build/design-worlds.md sections 1 and 4): ONE FOLDER PER WORLD. --world <name> runs the folder
//   <top folder>\<name, lower-cased> (coopworld::WorldDirIn) - the top folder is --root, default %LOCALAPPDATA%\kenshi\Shared Wastelands\worlds (PP3); --dir is the exact-folder override
//   the harness uses; neither = world 'New World' (coopworld::kDefaultWorld). Without --dir, a start that finds the OLD single-folder layout (world files
//   loose in coop-store\) moves them into the world's folder first, journaled in coop-store\worlds.migrated.txt;
//   --unmigrate puts them back from that journal - for going back to an OLDER build (W2-f: its notes go to
//   coop-store\worlds.unmigrate.log; after play this build refuses to start until the world folder is moved away).
//   --owner names the OPERATOR (decision 49): the player this notebook is being run for, and therefore the
//   game whose OPTIONS, WEATHER and off-screen squad updates are accepted. It is kept in owner.txt, so a
//   notebook restarted without the argument still knows. Without it, the first game to link becomes the
//   operator and is written down on the spot (B13).
//
// STORE-FILE FORMAT 7 (B12, decision 52 - this is a FILE format version and NOT the wire protocol, which is
// 41). One record is two files: <id>.platoon (the engine's own container bytes) and <id>.meta, one
// tab-separated line:
//
//   v7 <writtenAt> <owner> <x> <y> <z> <squadSid> <factionName> <worldId> <posAt> <town> <len> <crc32> <seq>
//
// <seq> is THIS NOTEBOOK'S OWN SEQUENCE NUMBER for the record: a u64 stamped here, and only here, on every
// record this process accepts, monotonic for the life of the folder and carried on the wire so a game can
// tell whether a record it queued a write against has been changed by somebody else since. It is what
// decision 52's outage queue decides a conflict with, because writtenAt is whole seconds of wall clock from
// two different machines and cannot order two writes. A v6 line and below reads back as seq 0 and is
// upgraded in place at start with seq 0 (storemeta.h says why no other value is honest).
//
// <len> is <id>.platoon's byte length and <crc32> is CRC-32 (IEEE, reflected, poly 0xEDB88320) of those bytes
// in eight lowercase hex digits; len 0 / crc 00000000 means the record has NO payload - a position-only note -
// and is not a failure. The two fields are APPENDED, so v2/v3/v4 lines still parse and are upgraded in place at
// start. Every record write goes payload-first through a temp file and a rename, keeping ONE previous version
// as <id>.platoon.prev / <id>.meta.prev, and the meta line is the commit point. The format, its parser and the
// recovery rule live in src\common\storemeta.{h,cpp}, compiled into this process, the game plugin and the
// offline test exe.
//
// Wire (shared with src/coop-plugin/net/transport.h): [0] type u8, [1..4] payload length u32 LE, then the payload.
//   28 RECORD       worldId, squadSid, factionName (u32 len + bytes each), x y z (f32), writtenAt (u32 lo, u32 hi),
//                   owner (u32), bytes (u32 len + the engine's container file; empty = position-only update)
//   29 STORE_HELLO  slot (string), protocol (u32)          game -> store
//   30 STORE_WELCOME protocol (u32), authority (u32), recordCount (u32), slot (u32, 31), world (u32 len + bytes, 45)   store -> game, then every RECORD
//   31 STORE_AUTH   authority (u32)                        store -> game, when the authority changes
//   34 AREAS        count (u32), then count * (x, y) i32 pairs, then the SENDER'S OWN PLAYER SECTOR (x, y i32).
//                   The trailing pair is optional on the wire: a game that predates it sends nothing, and a game
//                   that does not know where its player is sends INT_MIN,INT_MIN. Both leave the last sector to
//                   expire on its own clock instead of being replaced by a guess.        game -> store
//   38 OPTIONS      count (u32), then count * (key string, value string)   BOTH DIRECTIONS.
//                   store -> game: the whole option map, once at WELCOME (right after it, before the bitmaps)
//                   and again to every game whenever the map changes. game -> store: a SET, accepted only from
//                   the game the store calls the authority (the front of the connection order) and only for
//                   keys and values this file knows; anything else is logged and dropped. The map is stored in
//                   options.txt the way uniques.txt is stored, so it survives every game restart and is still
//                   right when the host is away (decision 40).
//   35 AREAMAP      M9 (T-197; protocol 65): u16 seats, seats * (u8 seat, u16 slot), u16 rows, rows * (u8 x, u8 y, u16 owner
//                   slot (0xFFFF nobody), u8 n, n * u8 seat - or n = 0xFF and a 32-byte SEAT mask; M9f1) - src/common/areadrop.h. store -> every game, once a second.
//   37 PLAYERSECTORS count (u16), then count * (slot u16, x i32, y i32), then count ages (u8)  (M9: was u8/u8)  store -> every game, once a second, right
//                   after AREAMAP: where each game's own player is standing. decision 37 amended: this is what the
//                   ring-1 tie-break reads, because the session link's own answer is unavailable in the very
//                   window (H047) the tie-break exists for.
//   39 MSG_CLOCK    hours (f64), effectiveSpeed (f32), mode (u8: 0 fixed, 1 consensus), seedState (u8).
//                   BOTH DIRECTIONS.
//                   store -> game: THE WORLD'S CLOCK, broadcast once a second and once at WELCOME. `hours` is
//                   absolute in-game hours - the same quantity the game keeps at clock+0xA0 (day*24 + hours into
//                   the day) - and a NEGATIVE value means this world has no clock yet and is waiting to be
//                   seeded. `seedState` (P7j) says HOW this world got the clock it has: 0 none (unseeded),
//                   1 first (the first game to present one), 2 advanced (a later game inside the seed window
//                   was AHEAD and the notebook moved FORWARD to it), 4 restored (this world's clock came back
//                   out of clock.txt after a notebook restart - P7u / review-p7j M-3; 3 `late` is RETIRED,
//                   because the window-closed case is no longer passive). It is a property of the WORLD, not
//                   of the receiver, and it names the LAST seed event.
//                   game -> store: THIS GAME'S OWN CLOCK. P7u: sent ONCE before that game's join jump (the
//                   seed offer) and then ONCE A SECOND after it (the play-time report that lets this world be
//                   pulled FORWARD to a game running ahead of it, bounded at 5 in-game minutes). This, the
//                   HELLO-carried clock and the offer are three routes into ONE decision function.
//                   P7j: HELLO (29) also gained {f64 clockHours, f32 speed} after the world key - this
//                   game's OWN clock at the moment it links, negative when it has no world loaded (which at
//                   the title screen is every game under decision 42), and the speed its global holds. The
//                   clock is a seed offer; THE SPEED IS NOT A VOTE - see EffectiveSpeed's own note on why a
//                   joiner must not be able to set the world's pace merely by connecting.
//   40 MSG_SPEEDVOTE speed (f32: 0 paused, 1, 2 or 5).  game -> store only.
//                   ONE PLAYER'S OWN SETTING, not a command. Under `timemode fixed` only the authority game's
//                   vote is read; under `timemode consensus` the effective speed is the MINIMUM over every
//                   connected game's latest vote, pause included, so the slowest player sets the pace. A game
//                   that has not voted is not counted, and a game that disconnects has its vote dropped.
//   41 MSG_WEATHER  (protocol 44, P7s) nameLen (u32, 1..96), the region's FCS stringID (nameLen bytes),
//                   seasonIndex (u32), seasonEndDay (i32), weatherIndex (u32), then 16 raw 32-bit
//                   words - the WeatherInstance scalars in the order WeatherInstance::load 0x9DC710 reads its
//                   fourteen save keys (the wind direction being a Vector3, three of the sixteen).
//                   BOTH DIRECTIONS: accepted only from the authority game (weather has ONE owner in either
//                   time mode), stored here, forwarded to every other game, and pushed again at WELCOME.
//                   Weather is NOT derivable from the clock - the engine rolls three separate rand() draws for
//                   the weather, the wind angle and the strength - so it can only be shared as state.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>   /* restore1b1: SHFileOperationA - a pruned repair copy goes to the Recycle Bin (FOF_ALLOWUNDO) */
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")   /* restore1b1 fold 2: the Recycle Bin policy (registry) */
#pragma comment(lib, "user32.lib")   /* M14 fold: the hidden window that hears logoff/shutdown (QuitWindowMain) */
#include <direct.h>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>   /* P8d: options.txt, uniques.txt, clock.txt and the deleted-number bitmaps are PROBED once and then parsed out of the bytes in hand, so ONE reader answers absent / unreadable / readable for every file in this folder */
#include <map>
#include <string>
#include <vector>
#include "../coop-plugin/third_party/enet/include/enet/enet.h"
#include "../coop-plugin/third_party/enet/include/enet/time.h"   /* B13-c: ENET_TIME_DIFFERENCE - enet.h does not pull this header in; protocol.c includes it by hand too */
#include "../common/clockmath.h"   /* P7u: the SAME clock arithmetic the game and the offline test exe compile - one translation unit, so the three cannot hold different constants */
#include "../common/names.h"   /* the on-disk names (mod folder, files, window texts) */
#include "../common/deferredmerge.h"   /* P8l: the two DECISIONS a deferred load makes when its file finally opens - which unique row wins, and which clock - pure, header-only, and compiled into the offline suite as well, because a decision that can only be reproduced by locking a file is a decision nobody tests */
#include "../common/storemeta.h"   /* P8b: the SAME index line, checksum and recovery rule the game and the offline test exe compile - three programs cannot hold three ideas of one file format */
#include "../common/recruitmult.h"   /* recruit3: the recruitmult host option's values - the SAME header the plugin and the offline suite compile */
#include "../common/researchwire.h"   /* loot2b: RESEARCH_BOX and research_boxes.txt lines */
#include "../common/barwire.h"   /* refill1: TOWN_BAR and town_bars.txt lines */
#include "../common/worldrelwire.h"   /* par24: WORLD_REL and world_relations.txt lines */
#include "../common/gpoptions.h"   /* settings2 S2: the gp.* / gt.* keys and their ranges - the SAME table the plugin and the offline suite compile */
#include "../common/areadrop.h"   /* M9 (T-197; protocol 65): the seat book and the AREAMAP writer - the SAME header the game and the offline suite compile */
#include "../common/areaclaim.h"   /* B13: the area-claim rule, the slot rule and the three file lines - the SAME header the offline suite sweeps, so what this process writes is what it reads back */
#include "../common/weatherwire.h"   /* P7s (F553): MSG_WEATHER names its region by FCS stringID - the SAME encoder and decoder the game and the offline suite compile */
#include <io.h>   /* W2a: _commit - each migration journal line reaches the disk before its move is made */
#include "../common/datadir.h"   /* PP3: the default top folder, <data folder>\worlds */
#include "../common/diskformat.h"   /* owner 429: the world folder's format number */
#include "../common/worlddir.h"   /* W2a: WorldDir, WorldNameOk, the migration plan and its journal - the SAME header the plugin and the offline suite compile */
#include "../common/profiles.h"   /* prof1: profiles.txt, the lobby's wire and its decisions - the SAME header the plugin and the offline suite compile */
#include "../common/modlist.h"   /* settings5 S5: the world's mod list, compared at HELLO */
#include "../common/restoreguard.h"   /* restore1a: world.gen, WORLD_SAVED and the WELCOME tail - the SAME header the plugin and the offline suite compile */
#include "../common/looptick.h"   /* M15 (T-197): the once-a-second jobs' fixed schedule and the per-pass budget of message handling - pure, and swept by the offline suite */
#include "../common/sendbound.h"    /* M16 (T-197): what waits to be sent to each game is bounded - latest-value classes keep the newest, the rest wait in order and are never dropped */
#include "../common/sendbundle.h"   /* M13: BUNDLE (59) - what goes to one game during one pass of the loop leaves as one packet per lane, and a game's bundles are unpacked before handling - the SAME header the game and the offline suite compile */
#include "../common/writequeue.h"   /* M14 (T-197): the write queue's ORDER rules - one job per record key, one writer, strictly in turn - pure, and swept by the offline suite */
#include "../common/uidblock.h"   /* M4 fold (protocol 58): UID_BLOCK and uid_seats.txt - the SAME header the plugin and the offline suite compile */
#include "../common/uidtable.h"   /* M4 fold 2: LogRefusal - the first 5 and every 100th of a repeating refusal line, offline-tested */
#include "../common/joinstage.h"   /* M11a S1 (protocol 61): the join STAGE, PLAYERS (54), JOIN_STAGE (55), the HELLO's tail and the live-protocol rule */
#include "../common/liverelay.h"   /* M6 (T-197 piece 6, protocol 60): the AREA route, the delivery sets and CATCHUP (56) - the SAME header the plugin and the offline suite compile */
#include "../common/gamelink.h"   /* M11a S3 (manager decision 6(a)): link1's ENet timeout terms, set on every world-server connection */
#include "../common/peergone.h"   /* M8 (T-197 piece 8, protocol 62): PLAYER_GONE (57) - the SAME header the plugin and the offline suite compile */
#include "../common/liveenvelope.h"   /* M5a (T-197 piece 4, protocol 59): LIVE (53) - the envelope, the route rule and the relayed sender id - the SAME header the plugin and the offline suite compile */
#include "../common/preload.h"   /* T-346 slice 1 (protocol 67): the record INDEX and FETCH before a game's load - the SAME header the game and the offline suite compile */
#include "../common/recordfeed.h"   /* T-313 (protocol 63): RECORD_FEED (58) - the ASK / OFF / BEGIN / PAGE_END wire, who gets a live forward, the page walk - the SAME header the plugin and the offline suite compile */
#include "../common/storelink.h"   /* B10-b (review-b10 M-7): StoreHandshakeDecide and its words - the SAME decision the game makes, so the two sides of one handshake cannot disagree about when it is refused */

namespace {

const unsigned int kProtocol = 69;   /* the world-server protocol: raise it when any message's meaning changes; the reason goes in the commit message (owner 356, 2026-10-02) */
enum { MSG_RECORD = 28, MSG_STORE_HELLO = 29, MSG_STORE_WELCOME = 30, MSG_STORE_AUTH = 31, MSG_RECORD_GONE = 32, MSG_DELETED_BITS = 33, MSG_AREAS = 34, MSG_AREAMAP = 35, MSG_UNIQUE_STATE = 36, MSG_PLAYERSECTORS = 37, MSG_OPTIONS = 38, MSG_CLOCK = 39, MSG_SPEEDVOTE = 40, MSG_WEATHER = 41, MSG_STORE_REFUSE = 42, MSG_RECORD_SEQ = 43, MSG_RESEARCH_BOX = 44, MSG_PROFILES = 45, MSG_RESEARCH_TAKE = 46, MSG_TOWN_BAR = 47, MSG_WORLD_SAVED = 48, MSG_OWN_HIGH = 49, MSG_REPAIR = 50, MSG_WORLD_REL = 51, MSG_UID_BLOCK = 52, MSG_LIVE = 53, MSG_PLAYERS = 54, MSG_JOIN_STAGE = 55, MSG_CATCHUP = 56, MSG_PLAYER_GONE = 57, MSG_RECORD_FEED = 58, MSG_BUNDLE = 59 };   /* M13 (protocol 68): BUNDLE (59) - src/common/sendbundle.h */   /* M8 (protocol 62): PLAYER_GONE (57) {u16 slot} down to every remaining admitted game - src/common/peergone.h */   /* M6 (protocol 60): CATCHUP (56) - src/common/liverelay.h; 54 and 55 are left for M11a (PLAYERS, JOIN_STAGE) */   /* M5a (protocol 59): LIVE (53) - src/common/liveenvelope.h */   /* M4 fold (protocol 58): UID_BLOCK (52) - src/common/uidblock.h */   /* par24: WORLD_REL (51) - src/common/worldrelwire.h */   /* restore1c: REPAIR (50) */   /* restore1b1 (protocol 53 since its fold): OWN_HIGH (49) - src/common/restoreguard.h */   /* refill1 (protocol 50): TOWN_BAR (47) - src/common/barwire.h */   /* loot2c (protocol 49): RESEARCH_TAKE (46; 45 before the prof1 merge) - src/common/researchwire.h */   /* loot2b (protocol 47): RESEARCH_BOX (44) - src/common/researchwire.h */   /* B12-b (decision 52, protocol 41 - no further bump): RECORD_SEQ (43) - {str worldId, u32 seq lo, u32 seq hi}, sent to THE WRITER ALONE the moment this notebook has committed its record and stamped the number. It carries no payload and may not create a record on the receiving side; it exists so a game's cached seq for a key IT wrote is not one behind, which is what made a later queued change to that key read as another writer's. */   /* B10-b (review-b10 M-7): REFUSE (42) - {u32 this store's protocol, u32 the protocol the game sent}. It REPLACES the WELCOME for a game this store does not speak; no records follow it and the peer is disconnected. */   /* E40 / decision 45 */
typedef char M13BundleNumbersAgree[(MSG_BUNDLE == (int)coopbundle::kMsgBundle && MSG_STORE_HELLO == (int)coopbundle::kMsgStoreHello && MSG_STORE_WELCOME == (int)coopbundle::kMsgStoreWelcome
    && MSG_STORE_REFUSE == (int)coopbundle::kMsgStoreRefuse) ? 1 : -1];   /* a compile error here = the header and this file disagree */
/* M13: sendbundle.h's size budget is ENet's own fragment threshold - its copies of ENet's wire sizes must be these. */
typedef char M13EnetSizesAgree[(sizeof(ENetProtocolHeader) == coopbundle::kEnetProtocolHeader && sizeof(ENetProtocolSendFragment) == coopbundle::kEnetSendFragment
    && sizeof(ENetProtocolSendReliable) == coopbundle::kEnetSendReliable && sizeof(ENetProtocolSendUnsequenced) == coopbundle::kEnetSendUnsequenced
    && sizeof(ENetProtocolAcknowledge) == coopbundle::kEnetAcknowledge && (unsigned int)ENET_PROTOCOL_MINIMUM_MTU == coopbundle::kEnetMinimumMtu
    && (unsigned int)ENET_PROTOCOL_MAXIMUM_MTU == coopbundle::kEnetMaximumMtu) ? 1 : -1];
typedef char T313FeedNumbersAgree[(kProtocol == coopfeed::kFeedProtocol && MSG_RECORD_FEED == (int)coopfeed::kMsgRecordFeed && MSG_RECORD == (int)coopfeed::kMsgRecord && MSG_RECORD_GONE == (int)coopfeed::kMsgRecordGone && MSG_DELETED_BITS == (int)coopfeed::kMsgDeletedBits && MSG_UNIQUE_STATE == (int)coopfeed::kMsgUniqueState) ? 1 : -1];   /* T-313: a compile error here = the header and this file disagree */   /* M6 (protocol 60): CATCHUP (56) - src/common/liverelay.h; 54 and 55 are left for M11a (PLAYERS, JOIN_STAGE) */   /* M5a (protocol 59): LIVE (53) - src/common/liveenvelope.h */   /* M4 fold (protocol 58): UID_BLOCK (52) - src/common/uidblock.h */   /* par24: WORLD_REL (51) - src/common/worldrelwire.h */   /* restore1c: REPAIR (50) */   /* restore1b1 (protocol 53 since its fold): OWN_HIGH (49) - src/common/restoreguard.h */   /* refill1 (protocol 50): TOWN_BAR (47) - src/common/barwire.h */   /* loot2c (protocol 49): RESEARCH_TAKE (46; 45 before the prof1 merge) - src/common/researchwire.h */   /* loot2b (protocol 47): RESEARCH_BOX (44) - src/common/researchwire.h */   /* B12-b (decision 52, protocol 41 - no further bump): RECORD_SEQ (43) - {str worldId, u32 seq lo, u32 seq hi}, sent to THE WRITER ALONE the moment this notebook has committed its record and stamped the number. It carries no payload and may not create a record on the receiving side; it exists so a game's cached seq for a key IT wrote is not one behind, which is what made a later queued change to that key read as another writer's. */   /* B10-b (review-b10 M-7): REFUSE (42) - {u32 this store's protocol, u32 the protocol the game sent}. It REPLACES the WELCOME for a game this store does not speak; no records follow it and the peer is disconnected. */   /* E40 / decision 45 */

struct Record { std::string worldId, squadSid, factionName, town; float x, y, z; long long writtenAt, posAt; unsigned owner; bool hasFile; int gone; long long len; unsigned crc; unsigned long long seq; Record() : x(0), y(0), z(0), writtenAt(0), posAt(0), owner(0), hasFile(false), gone(0), len(0), crc(0), seq(0) {} };   /* B12 (decision 52): seq is stamped by THIS process on every record it accepts and is what a game's queued write is conflict-checked against */   /* P8b: len + crc = what the payload file IS, so a torn one can be told from a whole one */
std::map<std::string, Record> g_records;
/* P8d (review-p8c Q1(b) / M-2) - THE SECOND DOOR IS A QUEUE, NOT A DROP. A record whose payload could not
   be read at start is served exactly as the pre-P8b loader served it (its position now, its squad as soon
   as the file opens), and it carries hasFile = 1 with len = 0 to say so. Writing its index line while the
   file is still shut would say `len 0` - "this record has no squad" - about a file that is on disk, which
   is the whole loss this phase exists to remove. P8c refused the write and DROPPED the newer position:
   nothing was queued, nothing counted it, and only the next ordinary update after the file opened carried
   a position again. The update is now HELD here and retried on the 1 Hz tick until the probe says
   readable - design principle 4, a deferral that consumes its trigger is silent feature loss - bounded,
   counted three ways, and logged once when it is given up on. */
struct PendingPos { float x, y, z; long long posAt; double firstAt; long long defers;
                    PendingPos() : x(0), y(0), z(0), posAt(0), firstAt(0.0), defers(0) {} };
std::map<std::string, PendingPos> g_pendingPos;
const double kPendingPosMaxSec = 600.0;   /* ten minutes, then one line and the hold is dropped */
// P8b (design-save.md S1). THE WRITE-SAFETY COUNTERS. index[...] says what the folder was when this process
// read it and what has been refused since; write[...] says whether any write failed at all. Both go out on the
// periodic line every 60 s and once immediately after LoadIndex, so a short run still leaves the verdict.
long long g_helloRefusedNoId = 0;   /* B13: a v42 game whose HELLO carried no usable player id - refused, because this notebook cannot name it */
long long g_helloRefusedDupId = 0;  /* B13-b (review-b13 H-2): a second game carrying an id ALREADY connected - refused, because two peers on one id would each read as the writer of the other's records */
long long g_helloEvictedSilent = 0; /* B13-c (review-b13-b Q2): a HELLO on an id whose OLD connection had been silent past kDupIdLiveSec - the old connection was dropped and this one took the id (a re-dial, a crash, a network drop, none of which sends DISCONNECT) */
long long g_indexLoaded = 0, g_indexRefusedTorn = 0, g_indexRefusedCrc = 0, g_indexRefusedMissing = 0;
long long g_indexPromotedPrev = 0, g_indexUpgraded = 0, g_writeTempFailed = 0, g_writeRenameFailed = 0;
// P8c (review-p8b C-1 / H-1 / H-2). EVERY PATH THAT DROPS, DEFERS OR REWRITES A RECORD MOVES ONE OF THESE.
// `loaded` counts survivors only, so before P8c a reader could not even take a difference: a folder that had
// silently lost a squad and a folder that had lost nothing printed the same verdict line.
long long g_indexPositionOnly = 0;              // indexed, and carrying no squad (len 0) - the .prev net's coverage gap
long long g_indexUpgradedNoPayload = 0;         // a v2-v4 line whose payload is genuinely ABSENT -> position-only v5
/* B10 (design-e46-store 3.5) - STORE-FILE FORMAT 5 -> 6, THE MIGRATION`S OWN THREE NUMBERS.
   upgradedV5toV6 MUST be >= 1 on the first start against a pre-B10 folder and 0 on every later start:
   a migration that keeps migrating is review-p8b C-1`s shape. v6Deferred is a v5 line this start
   refused to rewrite because its record was refused - the migration is a WRITE, and a write may not
   rest on a folder we would not otherwise touch; it is migrated at a later start. */
long long g_indexUpgradedToV7 = 0, g_indexV7UpgradeFailed = 0, g_indexV7Deferred = 0;
/* B12: THE THREE ARE RENAMED, NOT REUSED. They counted a v5 -> v6 migration; WriteMeta now writes v7, so
   a v5 AND a v6 line both migrate through the same arm and the old names would be read off the new
   numbers as "how many v6 lines were there" - which is the mistake renaming exists to stop (B10's own
   zoneStaleCopyRefused precedent). upgradedToV7 MUST be >= 1 on the first start against a pre-B12 folder
   and 0 on every later start.
   B12 - THE NEXT SEQUENCE NUMBER THIS NOTEBOOK WILL STAMP. Raised past every seq the index load finds, so
   it is monotonic across a restart of this process without a counter file of its own: the index IS the
   persistence. seqStamped is how many records this run has stamped. */
unsigned long long g_seqNext = 1ULL;
long long g_seqStamped = 0;
long long g_seqEchoSent = 0;   /* B12-b: RECORD_SEQ messages sent back to a writer - one per accepted record */
/* review-p8l H-1: a rotation abandoned because the DESTINATION (<id>.meta.prev) is shut while
   <id>.meta is writable - nothing rotated and the WRITE WENT AHEAD, which is the repair. */
long long g_rotatePrevLocked = 0;
long long g_helloRefusedProto = 0;   /* B10-b (review-b10 M-7): handshakes REFUSED because the game speaks a different store protocol. It used to be a log line followed by a WELCOME and a full record push. */
/* review-p8l H-2: the payload half would not move and NOTHING had been rotated - there was no
   <id>.meta to roll back. It used to be booked as rolledBack, which claims a rotation that never was. */
long long g_rotateNothingRotated = 0;
/* review-p8l M-2: rows of the MERGED unique map put back on the wire after a deferred load recovered. */
long long g_uniquesRepublished = 0;
long long g_indexUpgradeDeferredUnreadable = 0; // ... whose payload EXISTS and could not be read -> nothing written
long long g_indexPromotedPrevOnUpgrade = 0;     // ... beaten by a validating v5 previous pair beside it
long long g_indexPayloadSetAside = 0;           // a payload kept as <id>.platoon.refused instead of overwritten
long long g_indexMetaEmpty = 0;                 // <id>.meta was READ and has no first line (zero-length)
long long g_indexMetaUnparsable = 0;            // its first line is no format this build knows
long long g_indexNoVersion = 0;                 // nothing on disk for that id validates - not indexed
// P8d (review-p8c C-1, C-2, H-1, H-3, M-1, M-3, M-5, M-6). ONE PROBE, ONE ROTATION, ONE WRITE PATH, ONE
// QUEUE - and a number behind each thing that can now happen. THE FOUR PROBE COUNTERS COUNT PROBE RESULTS,
// NOT RECORDS, and the difference is measurable rather than pedantic: a start probes <id>.meta twice for a
// record that reaches pass 2 (once in the walk, once in the decision) and <id>.meta.prev twice as well, so
// one locked file can read as 2 here. What they say is how many TIMES this start met a file of that name on
// disk and could not read it - which is the thing every deferral below is drawn from - and not how many
// records were affected. (6a lesson 1: name what the number measures, or it will be read as the other one.)
long long g_probeUnreadableMeta = 0, g_probeUnreadablePayload = 0;
long long g_probeUnreadableMetaPrev = 0, g_probeUnreadablePayloadPrev = 0;
long long g_indexMetaVanished = 0;              // the walk listed <id>.meta and the probe says it is not there
long long g_indexPayloadPrevPresent = 0;        // records walked that HAVE a <id>.platoon.prev (review-p8c M-3)
long long g_indexPayloadZeroLength = 0;         // <id>.platoon is ON DISK WITH NO BYTES IN IT (review-p8c M-1)
long long g_indexRefusedUnreadable = 0;         // the line claims a payload and that payload could not be read
long long g_indexRefusedFilesPresent = 0;       // <id>.platoon.refused files still in the folder (review-p8c M-5)
long long g_rotatePair = 0;                     // BOTH halves rotated together - the only maker of a .prev PAIR
long long g_rotateMetaOnly = 0;                 // a record with no squad: its index line is all it has to lose
long long g_rotateSkippedOrphan = 0;            // REFUSED: rotating the line alone would orphan <id>.platoon.prev
long long g_positionDeferredUnreadable = 0;     // position updates HELD because the payload could not be read
long long g_positionDeferredApplied = 0;        // ... and later written, once the file opened
long long g_positionDeferredExpired = 0;        // ... or given up on after the bound, and logged once
long long g_sendPayloadUnreadable = 0;          // a record pushed to a game with its position and no people
long long g_optionsDeferredUnreadable = 0;      // options.txt on disk and unreadable - the defaults NOT written
long long g_bitsDeferredUnreadable = 0;         // a deleted-number bitmap on disk and unreadable - NOT rewritten
/* P8l (review-p8d C-1 / H-1 / H-2). ONE DEFERRED-LOAD STATE PER FILE, AND EVERY WRITER OF THAT FILE OBEYS IT.
   P8d put the three-way probe in front of every READ and stopped there. The map a deferred read leaves behind
   is EMPTY, and the next ordinary message rewrites the file from it - so one moment's lock on uniques.txt plus
   one UNIQUE_STATE erased every unique-character state this world had, AND revived a character recorded
   permanently DEAD, because the DEAD-IS-TERMINAL guard was comparing against that empty map (review-p8d C-1,
   measured). options.txt and clock.txt are the same shape one step out (H-1, H-2). The deleted-number bitmaps
   were the one file already guarded, and their merge-then-write shape is what these three now have.

   THE RULE: while a file's load is DEFERRED, EVERY WRITER OF THAT FILE REFUSES; the message is still accepted
   into memory and still broadcast (a deferral that consumes its trigger is silent feature loss - design
   principle 4); the load is retried on the 1 Hz tick; and on the first successful load the deferred changes
   are MERGED ONTO WHAT WAS ACTUALLY ON DISK - with the DEAD-IS-TERMINAL guard applied against THAT map,
   never the empty one - and written ONCE. One line at the defer, one line at the recovery. */
int g_uniquesLoadDeferred = 0, g_optionsLoadDeferred = 0, g_clockLoadDeferred = 0;
int g_uniquesRefusalSaid = 0, g_optionsRefusalSaid = 0, g_clockRefusalSaid = 0;   /* the refusal is SAID ONCE PER EPISODE and COUNTED every time: WriteClock alone is called every 5 s */
long long g_uniquesWriteRefusedDeferred = 0;    // writes of uniques.txt refused because its load is deferred
long long g_optionsWriteRefusedDeferred = 0;    // ... of options.txt
long long g_clockWriteRefusedDeferred = 0;      // ... of clock.txt
long long g_uniquesDeferredLoadRecovered = 0;   // the deferred load succeeded, the changes were merged and written
long long g_optionsDeferredLoadRecovered = 0;
long long g_clockDeferredLoadRecovered = 0;
std::map<std::string, std::string> g_optionsDeferredChanges;   /* the keys the authority changed while the load was deferred - the file's own rows lose to these and to nothing else */
/* P8l (review-p8d C-2). A ROTATION THAT MOVED ONE HALF AND PUT THE OTHER BACK, AND ONE THAT COULD NOT. */
long long g_rotateRolledBack = 0;               // the payload half would not move, so the meta half was renamed BACK
long long g_rotateRollbackFailed = 0;           // ... and the rename back ALSO failed. The one state this cannot repair.
std::string g_dir;
FILE* g_log = 0;
/* W2a: which world this notebook runs, where its folder came from, and what the one-time migration did at start. */
std::string g_worldName;                   /* as given (--world), coopworld::kDefaultWorld ('New World') by default, empty under a bare --dir */
const char* g_worldDirSource = "default";  /* arg = --world, dir = --dir (wins over --world), default = neither */
const char* g_worldTxtState = "notChecked";/* created | matched | matchedOtherCase | mismatch | dir | unreadable | writeFailed | notChecked */
std::string g_worldId;                     /* T-490: the world's id (world.txt line 2), sent in every WELCOME; "" = none (world.txt unreadable) */
bool g_worldIdUpgraded = false;            /* T-490: the id was given to a world made before ids */
const char* g_worldFormatState = "notChecked";   /* owner 429: use | converted | convertFailed | notChecked */
std::string g_welcomeWorld;                /* W3: the world name this notebook puts last in its WELCOME (SetWelcomeWorld); never empty once set */
long long g_helloWorldDiffers = 0;         /* W3: HELLOs whose world is not this notebook's - logged and let in; the game follows the WELCOME (decision 59) */
long long g_migMoved = 0, g_migSkipped = 0, g_migFailed = 0;
std::vector<ENetPeer*> g_order;   // connection order: front = the off-screen authority
// decision 32 step one: the map of who holds which area lives HERE. A game's slot = its index in the connection order (0 = the
// first to connect). Each game reports the areas it has loaded (AREAS); the first reporter holds an area; a holder that stops
// reporting for kGraceSec loses it to another current reporter or to nobody; the finished map goes to every game once a second.
bool SbSendRel(ENetPeer* p, const std::vector<char>& buf);   /* M16: every reliable frame to one game goes through its bound - defined before OnLive */
void PutU32(std::vector<char>* b, unsigned v); bool GetU32(const std::vector<char>& p, size_t at, unsigned* out); bool SendMsg(ENetPeer* to, unsigned char type, const std::vector<char>& payload); std::string PeerName(ENetPeer* p); void Log(const std::string& s);
int ClockSeedWindowOpen(); void ClockSeedWindowClose(const std::string& why);   /* P7j: AreaTick, which is defined above the clock block, is what closes the seed window on the world's first real area map - declared here in the same place and the same way this file already declares Log and SendMsg for it */
const double kGraceSec = 10.0;
/* B13: the row's owner is now a PLAYER, and `owner` (the slot number, which is what goes out in the AREAMAP
   and what the games compare against their own slot) is that player's number. `restored` is 1 while the row
   came out of areas.txt and its owner has not reported since this process started - the one state the restore
   grace exists for. B13-b (H-1): its lease (lastSeenOwner) is stamped when that owner's HELLO lands, because
   0.0 against a performance counter reads as hours of silence; `keptCounted` makes areas[keptOnRestore] count ROWS DEFENDED rather than seconds. */
struct AreaRow { int owner; double lastSeenOwner; std::map<int, double> lastSeen; std::string ownerId; int restored; int keptCounted; double restoredAtSec; int unconfirmed; double assignedAt; AreaRow() : owner(-1), lastSeenOwner(0.0), restored(0), keptCounted(0), restoredAtSec(0.0), unconfirmed(0), assignedAt(0.0) {} };   /* area2 fold: unconfirmed = given to a ring-1 slot that has not listed it loaded yet (coopstore::UnconfirmedLeaseDecide) */
std::map<long long, AreaRow> g_areas;   // key = y*64 + x

double NowSec() { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart; }
/* M15 (T-197): the loop's schedule clock - the same performance counter, as whole microseconds (monotonic, no
   32-bit wrap; split into whole seconds and remainder so the multiply cannot overflow however long the PC is up). */
long long MonoUs()
{
    static long long freq = 0;
    if (freq == 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); freq = f.QuadPart; }
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (c.QuadPart / freq) * 1000000LL + (c.QuadPart % freq) * 1000000LL / freq;
}

/* ================= B13 - WHAT THIS NOTEBOOK REMEMBERS ACROSS A RESTART =================
   T239 (Confirmed): this process was killed and restarted mid-run. Every slot number, the whole area map
   and the authority flag were in memory and all three were re-derived from DIAL ORDER, so the game that
   reconnected first took slot 0 - the other game's number, which every record that game had ever written
   carries as RECORD.owner - and its first AREAS report claimed 43,11 "first there". The relay then told
   that game it was the sector's writer, and the other game's replayed move was refused as a collision.
   Three files beside the records fix it: WHO each player is (slots.txt), WHAT each player holds
   (areas.txt) and WHO the operator is (owner.txt). All three are written in clock.txt's shape - a temp
   file and a MoveFileExA rename, so a reader sees the whole old file or the whole new one - and all three
   are restored at start. The rule that decides whether a restored claim stands is
   coopstore::AreaClaimDecide, which the offline suite sweeps. */
std::string N(long long v);   /* B13: the number formatter, declared here the way this file already declares Log, SendMsg and PeerName above - it is defined with the other text helpers further down */
long long NowUnix() { return (long long)time(0); }
const double kRestoreGraceSec = 60.0;   /* a restored claim stands this long for an owner that has not dialled back in. A game re-dialling on B12-e's cadence needs about 17 s an attempt, so 60 covers three of them. After it, the ordinary kGraceSec silence lease is all there is. */
double g_restoredAtSec = 0.0;           /* when this process finished restoring - the restore grace runs from here */
std::map<ENetPeer*, std::string> g_peerId;      /* the live connection -> the player id its HELLO carried */
/* T-313 (protocol 63): THE RECORD FEED (src/common/recordfeed.h). A connection is SUBSCRIBED from its first RECORD_FEED ASK until
   its OFF or its disconnect (PeerGone). Only a subscribed connection is sent the live RECORD / RECORD_GONE / UNIQUE_STATE
   forwards (FeedForwardTo, at all four sites that send them to other games), and no WELCOME push carries the unique states or
   the records any more - OnRecordFeed pages them to the game that asked. A game waiting at the title never asks, so it is never
   flooded (its arrival queue used to reach 3/4 of 8 MB and refuse this link every 60 s). DELETED_BITS still go to everyone.
   feedWithheld[record,gone,unique] counts forwards NOT sent to a connected, unsubscribed game. */
std::map<ENetPeer*, int> g_feedOn;
long long g_preIndexAsks = 0, g_preIndexPages = 0, g_preIndexEntries = 0, g_preFetches = 0, g_preFetchKeys = 0, g_preFetchSent = 0, g_preFetchMissing = 0, g_preMalformed = 0, g_preNotAdmitted = 0, g_preFetchCut = 0;   /* T-346 slice 1: the records-before-the-load exchange (OnPreload) */
long long g_feedAsks = 0, g_feedPages = 0, g_feedOffs = 0, g_feedBegins = 0, g_feedEnds = 0, g_feedRecordsSent = 0, g_feedBytesSent = 0,
          g_feedMalformed = 0, g_feedNotAdmitted = 0;
long long g_feedWithheld[3] = { 0, 0, 0 };   /* record, gone, unique */
bool FeedOn(ENetPeer* p) { return g_feedOn.find(p) != g_feedOn.end(); }
bool FeedForwardTo(ENetPeer* p, ENetPeer* from, unsigned int type)
{
    const int d = coopfeed::FeedForwardDecide(type, p->state == ENET_PEER_STATE_CONNECTED ? 1 : 0, p == from ? 1 : 0, FeedOn(p) ? 1 : 0);
    if (d == coopfeed::kFwdWithheld) ++g_feedWithheld[type == coopfeed::kMsgRecord ? 0 : (type == coopfeed::kMsgRecordGone ? 1 : 2)];
    return d == coopfeed::kFwdSend;
}
/* prof1: g_peerId holds the PROFILE'S SLOT KEY (coopprof::SlotKey - the bare person id for profile 1). g_lobby holds the
   connections whose HELLO carried profile 0 - person id, no slot, no WELCOME yet. g_profiles is profiles.txt. */
std::map<ENetPeer*, std::string> g_lobby;
std::vector<coopprof::Row> g_profiles;
long long g_profAdopted = 0;   /* prof1 fold */
long long g_profFactionSet = 0, g_profFactionSame = 0, g_profFactionRefused = 0;   /* names2a: PROFILES kind 3 */
/* restore1a (design s1; fold review-restore1a): world.gen - {profile -> highest worldGen} (owner ruling 1: per profile), ONE
   world-wide seqHigh (never raised above this notebook's own index high-water from a game's claim) and the epoch (0 until
   restore1b); never steps back. g_wgFileState: 0 absent, 1 read/written, 2 unreadable (treated as absent). */
restoreguard::WorldBook g_wg;
int g_wgFileState = 0;
long long g_wgIn = 0, g_wgRaised = 0, g_wgStale = 0, g_wgNotOperator = 0, g_wgMalformed = 0, g_wgAnswered = 0, g_wgWriteFailed = 0, g_wgProfileRefused = 0;
std::vector<restoreguard::RepairRow> g_repairs;   /* restore1c: repair.txt, epoch order */
long long g_rpIn = 0, g_rpDone = 0, g_rpRefused = 0, g_rpRecycled = 0, g_rpReverted = 0, g_rpPutBack = 0, g_rpFailed = 0, g_rpMalformed = 0, g_rpUndoneStamps = 0, g_rpBadLines = 0;
restoreguard::OwnBook g_oh;   /* restore1b1: own_high.txt - (profile, records store) -> the highest committed own seq reported; never back */
long long g_ohIn = 0, g_ohRaised = 0, g_ohBehind = 0, g_ohPushed = 0, g_ohMalformed = 0, g_ohWriteFailed = 0, g_ohBadLines = 0;
coopuidblk::UidBlockBook g_ub;   /* M4 fold (protocol 58) / owner 205 A: uid_seats.txt - per SEAT, the highest uid counter ever handed out, for the world's life */
long long g_ubGranted = 0, g_ubExhausted = 0, g_ubNoSeat = 0, g_ubWriteFailed = 0, g_ubMalformed = 0, g_ubNotAdmitted = 0, g_ubRefusedUnreadable = 0; int g_ubBadLines = 0; bool g_ubUnreadable = false;   /* M4 fold */
std::map<ENetPeer*, unsigned int> g_seatOf; std::set<unsigned int> g_seatsHeld; std::set<ENetPeer*> g_seatRefusedSaid;   /* owner 205 A: the uid SEAT each connected game holds; the seats held; connections already told there is none */
long long g_seatTaken = 0, g_seatFreed = 0, g_seatRefusedFull = 0, g_seatMovedSpent = 0;   /* owner 205 A */
long long g_rcMade = 0, g_rcPruned = 0, g_rcBytes = 0, g_rcFailed = 0, g_wsBroadcast = 0, g_rcPruneNoBin = 0;   /* restore1b1: repair\<tag>\ copies and the WORLD_SAVED broadcast */
unsigned long long WorldSeqHighNow() { const unsigned long long idx = g_seqNext > 0ULL ? g_seqNext - 1ULL : 0ULL; return g_wg.seqHigh > idx ? g_wg.seqHigh : idx; }
long long g_profBadLines = 0, g_profListed = 0, g_profMade = 0, g_profDeleted = 0, g_profPicked = 0, g_profWriteFailed = 0;
int g_profReadOnly = 0; long long g_profWriteRefusedNewer = 0;   /* T-222: profiles.txt holds a newer build's lines - never rewritten by this build */
long long g_profRefusedCap = 0, g_profRefusedName = 0, g_profRefusedUnknown = 0, g_profRefusedInUse = 0, g_profRefusedOther = 0, g_profRefusedSamePerson = 0;
std::map<std::string, int> g_slotById;          /* EVERY id this world has ever named -> its slot number, connected or not */
std::set<int> g_slotsUsed;                        /* M1: the numbers in g_slotById, kept beside it so a new id's number is found in O(log n) */
/* M1 (decision 56(b)): the two named limits. kMaxConnected games linked at once (--max-connected lowers it for a
   test); the lifetime limit is coopstore::kSlotLifetimeMax. kRefuseSeats: design D1 - a seat to say no in words;
   ENet otherwise drops the CONNECT silently (protocol.c:331) when every peer slot is taken, and the game would
   see only a dial that never answers. */
const int kMaxConnected = 256;
const int kRefuseSeats = 8;
int g_maxConnected = kMaxConnected;
int g_firstSlot = 0;                              /* M1, TEST ONLY: --first-slot N - new ids take numbers from N up */
long long g_connAdmitted = 0, g_connRefusedFull = 0, g_connRefusedLifetime = 0, g_connHigh = 0;
/* M1-b (review-m1 M2) - THE HELLO DEADLINE. A connection that CONNECTs and is never admitted by a HELLO held one of
   the kRefuseSeats for ever. This is a TIMER, justified the way B13-c's kDupIdLiveSec is: the thing being waited
   for is a message that never arrives, and an absent message raises no event. ENet's own timeout (5 to 30 s) does
   not help - it closes a peer that stops ACKNOWLEDGING, and a live-but-silent peer acknowledges happily for ever.
   g_helloDueFrom holds the CONNECT time (NowSec) of each connection not yet admitted; the admitting HELLO and
   PeerGone (the DISCONNECT event and the eviction) take it out, and the 1 Hz tick closes whatever is left past the
   deadline. A refused game is already on enet_peer_disconnect_later and normally leaves within a round trip; if it
   lingers, this closes it too. */
const double kHelloDeadlineSec = 10.0;
std::map<ENetPeer*, double> g_helloDueFrom;
long long g_connHelloTimeout = 0;
std::map<std::string, long long> g_slotSeenAt;  /* ... and when it was last heard from (unix seconds), for a reader of the file */
/* B13-b (review-b13 H-1) - "KNOWN NOW" MEANS *REPORTED*, NOT "SAID HELLO", AND HERE IS WHY IT MATTERS.
   B13 set this at the HELLO. A restored row's lastSeenOwner is 0.0 and NowSec() is the performance counter
   since boot, so the moment the returning holder's HELLO landed, ownerKnown flipped to 1 and BOTH deciders
   measured now - 0.0 = hours of silence: past the 10 s lease, and every sector that game had held was
   transferred or released in the second before its first AREAS report arrived - T239's collision again, with
   areas[restored] printed as though the restore had worked. An id is in here once an AREAS REPORT from it has
   been seen, which is the only event that actually renews a claim. */
std::map<std::string, int> g_idReportedThisRun;
std::string g_ownerId;                          /* decision 49: the OPERATOR - the player this notebook was started for */
int g_ownerSource = coopstore::kOwnerSourceNone;
long long g_slotsKnown = 0, g_slotsRestored = 0, g_slotsAssignedNew = 0, g_slotsFileBadLines = 0, g_slotsFileWriteFailed = 0;
long long g_ownerFileWriteFailed = 0, g_pendingPosFileWriteFailed = 0;   /* B13-b (review-b13 M-5): these two writes were the only ones whose failure nothing counted - a notebook that silently stops writing owner.txt forgets its operator at the next restart, which is the defect this phase exists to end */
int g_ownerLinked = 0;              /* B13-b (M-4): has the operator named in owner.txt / --owner actually said HELLO this run? */
int g_ownerAbsentSaid = 0;          /* ... and the one line that says it has not, said once */
const double kOwnerAbsentSayAfterSec = 120.0;
long long g_areasRestored = 0, g_areasKeptOnRestore = 0, g_areasTransferredAfterGrace = 0, g_areasReleased = 0, g_areasFileWriteFailed = 0;
long long g_areasAssignRing1 = 0, g_areasAssignReporter = 0, g_areasUnconfirmed = 0, g_areasUnconfirmedConfirmed = 0, g_areasUnconfirmedExpired = 0;   /* P5t + area2 fold */
long long g_pendingPosDroppedAtRestart = 0;     /* held position updates this process's PREDECESSOR was still holding when it died - see PendingPosCountFile below */

std::string SlotsFile()      { return g_dir + "\\slots.txt"; }
std::string AreasFile()      { return g_dir + "\\areas.txt"; }
std::string OwnerFile()      { return g_dir + "\\owner.txt"; }
std::string PendingPosFile() { return g_dir + "\\pendingpos.txt"; }

std::string PeerIdOf(ENetPeer* p) { std::map<ENetPeer*, std::string>::const_iterator it = g_peerId.find(p); return it == g_peerId.end() ? std::string() : it->second; }
int IdReportedThisRun(const std::string& id) { return g_idReportedThisRun.count(id) ? 1 : 0; }
/* B13 / decision 49: is this connection the OPERATOR - the player this notebook was started for? It is
   asked here, above every caller, because the pace, the options, the weather and the WELCOME all ask it. */
bool PeerIsAuthority(ENetPeer* p)
{
    if (g_ownerId.empty()) return false;
    return coopprof::PersonOfKey(PeerIdOf(p)) == g_ownerId;   /* prof1: the operator is a PERSON - any of their profiles */
}

/* THE ONE WRITER, in clock.txt's shape (WriteClock, below): a temp file beside the real one, then
   MoveFileExA over it. Every caller counts its own failure, because a file that silently stops being
   written is this whole phase's defect wearing a different hat. */
bool WriteWholeFile(const std::string& path, const std::string& body)
{
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp.c_str(), std::ios::trunc);
        if (!f) { Log("B13: could not open " + tmp + " for writing - " + path + " is unchanged"); return false; }
        f << body;
        f.flush();
        if (!f) { Log("B13: the write to " + tmp + " failed - " + path + " is unchanged"); return false; }
    }
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    { Log("B13: could not replace " + path + " (MoveFileExA, GetLastError=" + N((long long)::GetLastError()) + ")"); return false; }
    return true;
}

/* THE READER. Whole file, split into lines; a file that is not there is not an error - a world that has
   never had one of these is the ordinary first start. */
bool ReadWholeFileLines(const std::string& path, std::vector<std::string>* out)
{
    std::ifstream f(path.c_str());
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) out->push_back(line);
    return true;
}

void WriteSlots()
{
    std::string body;
    for (std::map<std::string, int>::const_iterator it = g_slotById.begin(); it != g_slotById.end(); ++it)
    {
        std::map<std::string, long long>::const_iterator s = g_slotSeenAt.find(it->first);
        body += coopstore::SlotsLineFormat(it->first, it->second, s == g_slotSeenAt.end() ? 0 : s->second) + "\n";
    }
    if (!WriteWholeFile(SlotsFile(), body)) ++g_slotsFileWriteFailed;
}

void LoadSlots()
{
    std::vector<std::string> lines;
    if (!ReadWholeFileLines(SlotsFile(), &lines)) { Log("B13 slots: no slots.txt - every player that links is new to this world and takes the smallest free number"); return; }
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string id; int slot = -1; long long seen = 0;
        if (!coopstore::SlotsLineParse(lines[i], &id, &slot, &seen)) { if (!lines[i].empty()) ++g_slotsFileBadLines; continue; }
        /* B13-b (review-b13 H-2): TWO PLAYERS MAY NOT HOLD ONE NUMBER. A hand-edited or half-written file
           with the same slot on two lines would make both of them read as the writer of every record that
           number owns, and both as the holder of every area it holds. The LATER line loses and is counted;
           that player is treated as new and takes the smallest number no known id holds. */
        const int taken = g_slotsUsed.count(slot) ? 1 : 0;
        if (taken || g_slotById.count(id))
        {
            ++g_slotsFileBadLines;
            Log("B13 slots: slots.txt names " + std::string(taken ? "slot " + N((long long)slot) : "player '" + id + "'")
                + " twice - the later line is DROPPED. Two players holding one number would each read as the"
                " writer of the other's records and the holder of the other's areas; that player is treated as"
                " new and takes the smallest number no known id holds. Counted slots[fileBadLines].");
            continue;
        }
        g_slotById[id] = slot; g_slotsUsed.insert(slot); g_slotSeenAt[id] = seen; ++g_slotsRestored;
    }
    Log("B13 slots: " + N(g_slotsRestored) + " player(s) restored from slots.txt (" + N(g_slotsFileBadLines) + " line(s) DROPPED as"
        " unreadable - a slot line is never guessed at, because guessing one would hand a player's number, and"
        " therefore every record that player has ever written, to somebody else). A restored player gets its OWN"
        " number back whatever order the games dial in.");
}

void WriteOwner()
{
    if (g_ownerId.empty()) return;
    if (!WriteWholeFile(OwnerFile(), coopstore::OwnerLineFormat(g_ownerId, g_ownerSource) + "\n"))
    {
        ++g_ownerFileWriteFailed;
        Log("B13 owner: owner.txt could not be written - this world's operator is correct for THIS run and will"
            " be re-learned from --owner or from the first HELLO at the next start (decision 49). Counted"
            " owner[fileWriteFailed].");
    }
}

void LoadOwner()
{
    /* --owner on the command line WINS over the file: it is the player this notebook has just been started
       for, and the panel passes it every time. The file is what a restart with no argument reads. */
    if (!g_ownerId.empty())
    {
        Log(std::string("B13 owner: the operator is '") + g_ownerId + "', from --owner. Decision 49: the authority"
            " is the player this notebook was started FOR, not whoever connected first - which is what made T236a"
            " and T239's authority flip. It is written to owner.txt so a restart without the argument keeps it.");
        WriteOwner();
        return;
    }
    std::vector<std::string> lines;
    if (ReadWholeFileLines(OwnerFile(), &lines))
        for (size_t i = 0; i < lines.size(); ++i)
        {
            std::string id; int src = 0;
            if (!coopstore::OwnerLineParse(lines[i], &id, &src)) continue;
            g_ownerId = id; g_ownerSource = src;
            Log(std::string("B13 owner: the operator is '") + g_ownerId + "', RESTORED from owner.txt. No --owner was"
                " given this start, so the file is what keeps decision 49's answer across a restart.");
            return;
        }
    Log("B13 owner: this world has no operator yet (no --owner, no owner.txt) - the FIRST game to talk to it after its WELCOME"
        " becomes the operator and is written to owner.txt on the spot, so this is decided exactly once.");
}

/* g_pendingPos is NOT persisted and B13 does not make it so - the hold carries a squad position that is
   already stale by the time this process comes back, and reviving it would file an old place over a new
   one. What was wrong was that its loss was SILENT (design principle 4). The COUNT alone is written, once a
   second when it changes, so a restart can say how many held updates its predecessor lost. */
void WritePendingPosCount(size_t n)
{
    if (!WriteWholeFile(PendingPosFile(), "v1\t" + N((long long)n) + "\n")) ++g_pendingPosFileWriteFailed;
}

void LoadPendingPosCount()
{
    std::vector<std::string> lines;
    if (!ReadWholeFileLines(PendingPosFile(), &lines) || lines.empty()) return;
    std::vector<std::string> f;
    coopstore::AcFields(lines[0], &f);
    if (f.size() < 2 || f[0] != "v1") return;
    const long long n = (long long)atof(f[1].c_str());
    if (n <= 0) return;
    g_pendingPosDroppedAtRestart = n;
    Log("B13: the notebook that ran before this one was still HOLDING " + N(n) + " position update(s) when it"
        " stopped, and they are gone. A held update is a squad's place that could not be written yet; it is not"
        " revived here, because the place it names is older than anything the games have sent since. Counted"
        " pendingPosDroppedAtRestart, and said once - before B13 this loss had no number at all.");
}
/* areas.txt is rewritten on every OWNERSHIP CHANGE and on nothing else - a claim changes hands rarely, while
   the reports that renew it arrive once a second from every game, and writing the file on a report would turn
   a quiet map into a continuous disk write for no new fact. */
void WriteAreasNow()
{
    std::string body;
    for (std::map<long long, AreaRow>::const_iterator it = g_areas.begin(); it != g_areas.end(); ++it)
    {
        if (it->second.ownerId.empty()) continue;
        body += coopstore::AreasLineFormat((int)(it->first % 64), (int)(it->first / 64), it->second.ownerId,
                                           (long long)it->second.restoredAtSec) + "\n";
    }
    if (!WriteWholeFile(AreasFile(), body)) ++g_areasFileWriteFailed;
}

void LoadAreas()
{
    std::vector<std::string> lines;
    if (!ReadWholeFileLines(AreasFile(), &lines)) { Log("B13 areas: no areas.txt - this world's area map starts empty and the first reporter holds each sector, exactly as every start before B13 did"); return; }
    long long dropped = 0, unknown = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        int x = 0, y = 0; std::string ownerId; long long at = 0;
        if (!coopstore::AreasLineParse(lines[i], &x, &y, &ownerId, &at)) { if (!lines[i].empty()) ++dropped; continue; }
        AreaRow& r = g_areas[(long long)y * 64 + x];
        r.ownerId = ownerId; r.restored = 1; r.restoredAtSec = (double)at;
        /* The AREAMAP carries slot NUMBERS, so a restored row needs the number its owner holds. A row whose
           owner is not in slots.txt cannot be given one and stands as owned-by-nobody until somebody reports
           it - the alternative, inventing a number, would name a player who does not exist. */
        std::map<std::string, int>::const_iterator s = g_slotById.find(ownerId);
        if (s == g_slotById.end()) { ++unknown; r.owner = -1; }
        else r.owner = s->second;
        ++g_areasRestored;
    }
    Log("B13 areas: " + N(g_areasRestored) + " area(s) restored from areas.txt (" + N(dropped) + " line(s) dropped"
        " as unreadable, " + N(unknown) + " naming a player slots.txt does not know). Each restored claim stands"
        " for " + N((long long)kRestoreGraceSec) + " s whatever any other game reports, then falls to a current"
        " reporter or to nobody - a restored claim is a LEASE, not a fact, or a game that quit during the outage"
        " would hold its sectors for ever and the games that ARE here could never invent in them.");
}
// review-p4o HIGH-1: a slot is assigned once at HELLO and freed at disconnect - it never shifts when another
// game leaves. Slots are the names the area map and RECORD.owner use.
// B13 (T239): THE NUMBER BELONGS TO THE PLAYER, NOT TO THE CONNECTION. It used to be the smallest number no
// CONNECTED game held, keyed on the live ENetPeer*, so a restarted notebook renamed every player by dial
// order - and RECORD.owner carries that number, so it renamed every past writer with them. It is now keyed on
// the player id the HELLO carries and kept in slots.txt: a known id gets ITS number back whatever order the
// games dial in, and a new id gets the smallest number NO KNOWN ID holds (which is not the same as the
// smallest free one - a number belonging to a player who is merely offline is still that player's).
// coopstore::SlotAssignDecide is that rule, and the offline suite sweeps it. M1: -1 means a NEW id and a world
// that has admitted its lifetime limit (coopstore::kSlotLifetimeMax) - OnHello refuses that game in words.
std::map<ENetPeer*, int> g_slot;
int SlotOf(ENetPeer* p) { std::map<ENetPeer*, int>::const_iterator it = g_slot.find(p); return it == g_slot.end() ? -1 : it->second; }
/* ================= M11a S1 (T-197 piece #10; investigations/m11a-design-2026-09-30.md s2-s3; protocol 61) =================
   THE ONE DOOR. Each admitted connection has a STAGE (src/common/joinstage.h): TITLE at its WELCOME (slot + seat); LOADING and
   IN_WORLD when it says so (JOIN_STAGE 55 - a world-road game, TEST cfg joinvia=world) or, on the old session road, IN_WORLD at its
   first AREAS - so two-game runs are unchanged. Only an IN_WORLD game is a LIVE sender or destination (OnLive); a world-road game's
   AREAS before its IN_WORLD are refused and counted (areasBeforeInWorld). Every admitted game is sent the whole roster {slot, stage,
   operator, name, view distance} (PLAYERS 54) on every change: an admission, a stage change, a game gone. The STORE_HELLO's tail
   carries the game-to-game protocol, and a joiner whose number differs from the games already admitted is refused (REFUSE reason 7,
   liveProtoRefused; manager decision 1(a) - an empty world accepts the first). */
struct JoinConn { int stage; unsigned road, liveProto; std::string name; float viewDist; JoinConn() : stage(coopjoin::kStageConnected), road(0), liveProto(0), viewDist(0.0f) {} };
std::map<ENetPeer*, JoinConn> g_join;   /* admitted connections only - set at the admission in OnHello, erased in PeerGone */
long long g_rosterBroadcasts = 0, g_rosterFrames = 0, g_jsIn = 0, g_jsMoved = 0, g_jsRefused = 0, g_jsMalformed = 0;
long long g_areasBeforeInWorld = 0, g_liveProtoRefused = 0, g_liveNotInWorldFrom = 0, g_liveDestSkipped = 0;
long long g_liveProtoOperatorRefused = 0;   /* S1 fold (review M2): admitted games refused and disconnected because an arriving operator speaks another game-to-game protocol */
std::map<int, long long> g_liveFwdTo;   /* LIVE frames sent, per destination slot */
int StageOf(ENetPeer* p) { std::map<ENetPeer*, JoinConn>::const_iterator it = g_join.find(p); return it == g_join.end() ? (int)coopjoin::kStageConnected : it->second.stage; }
int JoinRoadOf(ENetPeer* p) { std::map<ENetPeer*, JoinConn>::const_iterator it = g_join.find(p); return it == g_join.end() ? (int)coopjoin::kRoadSession : (int)it->second.road; }   /* S1 fold (review M1) */
void RosterBroadcast(const char* why)
{
    std::vector<coopjoin::RosterRow> rows;
    for (std::map<ENetPeer*, std::string>::const_iterator it = g_peerId.begin(); it != g_peerId.end(); ++it)
    {
        const int s = SlotOf(it->first); if (s < 0) continue;
        coopjoin::RosterRow r; r.slot = (unsigned)s; r.op = PeerIsAuthority(it->first) ? 1u : 0u;
        std::map<ENetPeer*, JoinConn>::const_iterator j = g_join.find(it->first);
        if (j != g_join.end()) { r.stage = (unsigned)j->second.stage; r.name = j->second.name; r.viewDist = j->second.viewDist; }
        else r.stage = (unsigned)coopjoin::kStageTitle;
        rows.push_back(r);
    }
    coopjoin::RosterSortBySlot(&rows);
    std::vector<char> b;
    if (!coopjoin::RosterEncode(rows, &b)) { Log("PLAYERS roster NOT sent - over " + N((long long)coopjoin::kRosterMax) + " rows"); return; }
    ++g_rosterBroadcasts;
    long long sent = 0;
    for (std::map<ENetPeer*, std::string>::const_iterator it = g_peerId.begin(); it != g_peerId.end(); ++it)
    {
        if (it->first->state != ENET_PEER_STATE_CONNECTED) continue;
        SendMsg(it->first, MSG_PLAYERS, b); ++sent;
    }
    g_rosterFrames += sent;
    Log(std::string("PLAYERS roster (") + why + ") sent to " + N(sent) + " game(s): " + coopjoin::RosterText(rows, -1));
}
/* AREAS: the old road enters the world here, at its first AREAS that NAMES AREAS (S1 fold, review L8: a game's empty teardown report,
   zones.cpp, enters nobody); a world-road game's areas wait for its IN_WORLD. count = the report's area count. false = refused (counted). */
bool JoinAreasAdmit(ENetPeer* from, unsigned count)
{
    std::map<ENetPeer*, JoinConn>::iterator it = g_join.find(from);
    const int stage = it == g_join.end() ? (int)coopjoin::kStageConnected : it->second.stage;
    const int road = it == g_join.end() ? (int)coopjoin::kRoadSession : (int)it->second.road;
    const int next = coopjoin::StageStep(stage, coopjoin::kEvAreas, road);
    if (next < 0)
    {
        ++g_areasBeforeInWorld;
        if (g_areasBeforeInWorld <= 5 || g_areasBeforeInWorld % 100 == 0)
            Log("AREAS REFUSED from " + PeerName(from) + " - it is at stage " + coopjoin::StageName(stage) + (road == coopjoin::kRoadWorld ? " on the world road" : "")
                + "; its areas are taken only once it is in the world (M11a S1). areasBeforeInWorld=" + N(g_areasBeforeInWorld));
        return false;
    }
    if (it != g_join.end() && next != stage && count == 0)
        return true;   /* S1 fold (review L8): an EMPTY AREAS is taken as ever, but it moves nobody into the world */
    if (it != g_join.end() && next != stage)
    {
        it->second.stage = next;
        Log("stage: " + PeerName(from) + " slot " + N((long long)SlotOf(from)) + " " + coopjoin::StageName(stage) + " -> " + coopjoin::StageName(next) + " at its first AREAS (the session road, M11a S1)");
        RosterBroadcast("a game entered the world");
    }
    return true;
}
/* JOIN_STAGE (55, up): {u32 LOADING | IN_WORLD} from an admitted game. */
void OnJoinStage(ENetPeer* from, const std::vector<char>& payload)
{
    ++g_jsIn;
    std::map<ENetPeer*, JoinConn>::iterator it = g_join.find(from);
    if (it == g_join.end() || g_peerId.count(from) == 0)
    {
        ++g_jsRefused;
        if (g_jsRefused <= 5) Log("JOIN_STAGE REFUSED from " + PeerName(from) + " - that connection is not admitted (it waits in the lobby or never said HELLO)");
        return;
    }
    unsigned st = 0;
    if (!coopjoin::JoinStageDecode(payload.empty() ? 0 : &payload[0], payload.size(), &st))
    { ++g_jsMalformed; Log("malformed JOIN_STAGE from " + PeerName(from) + " (" + N((long long)payload.size()) + " bytes)"); return; }
    const int was = it->second.stage;
    const int next = coopjoin::StageStep(was, st == (unsigned)coopjoin::kStageLoading ? (int)coopjoin::kEvLoading : (int)coopjoin::kEvInWorld, (int)it->second.road);
    if (next < 0) { ++g_jsRefused; Log("JOIN_STAGE REFUSED from " + PeerName(from) + " - " + coopjoin::StageName((int)st) + " from stage " + coopjoin::StageName(was)); return; }
    if (next == was) return;
    it->second.stage = next; ++g_jsMoved;
    Log("stage: " + PeerName(from) + " slot " + N((long long)SlotOf(from)) + " " + coopjoin::StageName(was) + " -> " + coopjoin::StageName(next) + " (its JOIN_STAGE, M11a S1)");
    RosterBroadcast("a stage changed");
}
std::string JoinReportToken()
{
    long long t = 0, l = 0, w = 0;
    for (std::map<ENetPeer*, JoinConn>::const_iterator it = g_join.begin(); it != g_join.end(); ++it)
    {
        if (g_peerId.count(it->first) == 0) continue;
        if (it->second.stage == coopjoin::kStageTitle) ++t; else if (it->second.stage == coopjoin::kStageLoading) ++l; else if (it->second.stage == coopjoin::kStageInWorld) ++w;
    }
    std::string fwd;
    for (std::map<int, long long>::const_iterator f = g_liveFwdTo.begin(); f != g_liveFwdTo.end(); ++f)
        fwd += std::string(fwd.empty() ? "" : ",") + N((long long)f->first) + ":" + N(f->second);
    return " join[lobby=" + N((long long)g_lobby.size()) + ",title=" + N(t) + ",loading=" + N(l) + ",inWorld=" + N(w) + "]"
         + " roster[broadcasts,frames]=" + N(g_rosterBroadcasts) + "," + N(g_rosterFrames)
         + " joinStage[in,moved,refused,malformed]=" + N(g_jsIn) + "," + N(g_jsMoved) + "," + N(g_jsRefused) + "," + N(g_jsMalformed)
         + " areasBeforeInWorld=" + N(g_areasBeforeInWorld) + " liveProtoRefused=" + N(g_liveProtoRefused) + " liveProtoOperatorRefused=" + N(g_liveProtoOperatorRefused)
         + " liveStage[fromNotInWorld,destSkipped]=" + N(g_liveNotInWorldFrom) + "," + N(g_liveDestSkipped)
         + " liveFwdTo[" + fwd + "]";
}
int AssignSlotFor(ENetPeer* p, const std::string& playerId)
{
    if (g_slot.count(p)) return g_slot[p];
    const bool wasKnown = g_slotById.count(playerId) != 0;
    const int s = coopstore::SlotAssignDecide(g_slotById, g_slotsUsed, playerId, g_firstSlot);
    if (s < 0) return -1;   /* M1: the caller refuses in words and counts it - conn[refusedLifetime] */
    g_slotById[playerId] = s; g_slotsUsed.insert(s);
    g_slotSeenAt[playerId] = NowUnix();
    g_slot[p] = s;
    if (wasKnown) ++g_slotsKnown; else ++g_slotsAssignedNew;
    WriteSlots();
    return s;
}
// E25 / decision 37 amended (2026-09-04) - WHERE EACH GAME'S OWN PLAYER IS STANDING, kept HERE.
// The ring-1 presumption ("an unclaimed area beside my player is being loaded by this game and nobody else") has to
// yield when the other player is beside it too, or two games invent one town's crowd twice. As built, the plugin
// asked its own copies of the peer's characters where they were - and review-p5t HIGH-1 showed that stream is
// switched OFF for the whole of the window the presumption serves: after a teleport the announcing game's own
// zone-loaded flag is down for ~10-12 s, so it withdraws its characters and the receiver's copies are erased.
// This process hears from both games directly and is not affected by either one's zone loading, so it is the one
// place the question can be answered at all. A sector is kept per slot with the time it arrived, and every game is
// told every current sector once a second.
struct PlayerSec { int x, y; double at; PlayerSec() : x(-1), y(-1), at(0.0) {} };
std::map<int, PlayerSec> g_playerSec;   // key = the reporter's slot
const double kPlayerSecFreshSec = 5.0;  // a sector older than this is not republished: it is not where they are now
void LiveAreasReport(ENetPeer* from, int slot, const std::vector<char>& payload, unsigned int n, double now);   /* M6: defined beside OnLive */
void OnAreas(ENetPeer* from, const std::vector<char>& payload)
{
    const int slot = SlotOf(from); if (slot < 0) return;
    unsigned areasCount = 0; GetU32(payload, 0, &areasCount);   /* S1 fold (review L8): a short payload reads 0 - it enters nobody, and is refused as malformed below */
    if (!JoinAreasAdmit(from, areasCount)) return;   /* M11a S1: a world-road game's AREAS wait for its IN_WORLD; a session-road game enters the world here */
    const std::string rid = PeerIdOf(from);
    unsigned n = 0; if (!GetU32(payload, 0, &n) || payload.size() < 4 + (size_t)n * 8) { Log("malformed AREAS from " + PeerName(from)); return; }
    const double now = NowSec();
    /* B13-b (H-1): THIS is where an id becomes "known now" - a report is the only thing that renews a claim,
       and the belt-and-braces half is in OnHello, which stamps this id's rows the moment it links. */
    if (!rid.empty()) g_idReportedThisRun[rid] = 1;
    /* E25: THE TRAILING PLAYER SECTOR - P5t reads it FIRST, so the unclaimed-area choice below sees where this reporter's
       own player stands in THIS report. OPTIONAL BY LENGTH - a game built before E25 sends the pairs and stops, and the
       message is still a good AREAS report. An out-of-range pair (INT_MIN,INT_MIN = "I do not know") is not stored, so
       "no answer" is always the previous answer expiring rather than a wrong one arriving. */
    if (payload.size() >= 4 + (size_t)n * 8 + 8)
    {
        int px = 0, py = 0;
        memcpy(&px, &payload[4 + (size_t)n * 8], 4); memcpy(&py, &payload[8 + (size_t)n * 8], 4);
        if (px >= 0 && px < 64 && py >= 0 && py < 64) { PlayerSec& ps = g_playerSec[slot]; ps.x = px; ps.y = py; ps.at = now; }
        else g_playerSec.erase(slot);   /* area2 fold (review-area2 LOW): "I do not know" clears the row now instead of letting it age out */
    }
    /* P5t: every player sector this process holds, as coopstore::AreaAssignDecide takes them (it applies the 5 s freshness). */
    std::vector<int> psSlot, psX, psY; std::vector<double> psAt;
    for (std::map<int, PlayerSec>::const_iterator pit = g_playerSec.begin(); pit != g_playerSec.end(); ++pit)
    { psSlot.push_back(pit->first); psX.push_back(pit->second.x); psY.push_back(pit->second.y); psAt.push_back(pit->second.at); }
    int changed = 0;
    for (unsigned i = 0; i < n; ++i)
    {
        int x = 0, y = 0; memcpy(&x, &payload[4 + i * 8], 4); memcpy(&y, &payload[8 + i * 8], 4);
        if (x < 0 || x >= 64 || y < 0 || y >= 64) continue;
        AreaRow& r = g_areas[(long long)y * 64 + x]; r.lastSeen[slot] = now;
        /* B13. THE OWNER IS A PLAYER NOW, so "first there" only applies to a sector NO PLAYER holds - a
           restored claim is a holder, even before that holder has dialled back in. The three answers are
           coopstore::AreaClaimDecide's and the offline suite sweeps every one of them. */
        if (r.ownerId.empty())
        {
            /* P5t (T418): ONE RULE DECIDES WHO MAKES A TOWN AND WHO HOLDS THE AREA. Not "first there" any more: the lowest
               slot whose player is fresh and within ring 1 of the area - the winner of each game's own ring-1 tie-break -
               and the reporter only when no player is beside it. A chosen slot with no connected game to name falls back
               to the reporter. A CLAIMED area is untouched (decision 15: sticky). */
            int nRing1 = 0, why = coopstore::kAssignReporter;
            int to = coopstore::AreaAssignDecide(slot, x, y, (int)psSlot.size(), psSlot.empty() ? 0 : &psSlot[0], psX.empty() ? 0 : &psX[0],
                                                 psY.empty() ? 0 : &psY[0], psAt.empty() ? 0 : &psAt[0], now, kPlayerSecFreshSec, &nRing1, &why);
            std::string toId = rid;
            if (to != slot)
            {
                toId.clear();
                for (std::map<ENetPeer*, int>::const_iterator sp = g_slot.begin(); sp != g_slot.end(); ++sp)
                    if (sp->second == to) { toId = PeerIdOf(sp->first); break; }
                if (toId.empty()) { to = slot; toId = rid; why = coopstore::kAssignReporter; }
            }
            /* area2 fold (review-area2 MED): the assignee has not listed this area as loaded (not the reporter, and no report
               of it within the grace) - UNCONFIRMED: held kUnconfirmedLeaseSec, its lease stamped by its own report. */
            std::map<int, double>::const_iterator ls = r.lastSeen.find(to);
            const int assigneeLoaded = (to == slot || (ls != r.lastSeen.end() && now - ls->second <= kGraceSec)) ? 1 : 0;
            r.ownerId = toId; r.owner = to; r.restored = 0; r.restoredAtSec = (double)NowUnix(); changed = 1;
            r.unconfirmed = assigneeLoaded ? 0 : 1; r.assignedAt = now; r.lastSeenOwner = assigneeLoaded ? (to == slot ? now : ls->second) : 0.0;
            if (why == coopstore::kAssignRing1) ++g_areasAssignRing1; else ++g_areasAssignReporter;
            if (r.unconfirmed != 0) ++g_areasUnconfirmed;
            { char b[200];
              if (why == coopstore::kAssignRing1) _snprintf(b, 199, "[AREAS] assign %d,%d -> slot %d (ring-1 lowest of %d fresh players; reporter slot %d)%s", x, y, to, nRing1, slot,
                                                            r.unconfirmed ? " UNCONFIRMED - it has 12 s to report the area loaded" : "");
              else _snprintf(b, 199, "[AREAS] assign %d,%d -> slot %d (reporter)", x, y, to);
              b[199] = 0; Log(b); }
        }
        else if (r.ownerId == rid)
        {
            r.owner = slot; r.lastSeenOwner = now;
            if (coopstore::UnconfirmedLeaseDecide(r.unconfirmed, 1, now - r.assignedAt, coopstore::kUnconfirmedLeaseSec) == coopstore::kLeaseConfirm)
            { r.unconfirmed = 0; ++g_areasUnconfirmedConfirmed;
              { char b[128]; _snprintf(b, 127, "[AREAS] confirmed %d,%d: slot %d reported it loaded %.1fs after the assignment", x, y, slot, now - r.assignedAt); b[127] = 0; Log(b); } }
            if (r.restored)
            { r.restored = 0; changed = 1;
              { char b[128]; _snprintf(b, 127, "AREA %d,%d: the RESTORED holder (slot %d) is back and reporting - the claim is live again", x, y, slot); b[127] = 0; Log(b); } }
        }
        else
        {
            /* area2 fold: an UNCONFIRMED assignment is held for its assignee 12 s, then handed to the reporter (this one). */
            const int ua = coopstore::UnconfirmedLeaseDecide(r.unconfirmed, 0, now - r.assignedAt, coopstore::kUnconfirmedLeaseSec);
            if (ua == coopstore::kLeaseHold) continue;
            if (ua == coopstore::kLeaseHandOver)
            {
                { char b[176]; _snprintf(b, 175, "[AREAS] unconfirmed %d,%d: slot %d did not report it loaded within %.0fs -> slot %d (reporter)", x, y, r.owner, coopstore::kUnconfirmedLeaseSec, slot); b[175] = 0; Log(b); }
                r.ownerId = rid; r.owner = slot; r.lastSeenOwner = now; r.restored = 0; r.keptCounted = 0; r.restoredAtSec = (double)NowUnix(); r.unconfirmed = 0;
                ++g_areasUnconfirmedExpired; changed = 1;
                continue;
            }
            const int ownerKnown = IdReportedThisRun(r.ownerId);
            const double silent = ownerKnown ? (now - r.lastSeenOwner) : (now - g_restoredAtSec);
            const int act = coopstore::AreaClaimDecide(ownerKnown, silent, 0, kRestoreGraceSec, kGraceSec);
            if (act == coopstore::kAreaTransfer)
            {
                { char b[160]; _snprintf(b, 159, "AREA %d,%d slot %d -> %d (the holder has been silent %.0fs, past its %.0fs, and this game is reporting it)", x, y, r.owner, slot, silent, ownerKnown ? kGraceSec : kRestoreGraceSec); b[159] = 0; Log(b); }
                r.ownerId = rid; r.owner = slot; r.lastSeenOwner = now; r.restored = 0; r.keptCounted = 0; r.restoredAtSec = (double)NowUnix();
                ++g_areasTransferredAfterGrace; changed = 1;
            }
            else if (r.restored && !r.keptCounted)
            {
                r.keptCounted = 1; ++g_areasKeptOnRestore;
                { char b[192]; _snprintf(b, 191, "AREA %d,%d: KEPT for its restored holder (slot %d) although slot %d is reporting it - the holder has %.0fs of the restore grace left", x, y, r.owner, slot, kRestoreGraceSec - silent); b[191] = 0; Log(b); }
            }
        }
    }
    if (changed) WriteAreasNow();
    /* P5t: the trailing player sector (E25) is read at the TOP of this function now, before the area loop. */
    /* M6: this game's delivery set for AREA-routed LIVE, and the catch-up for what is new in it. M7a fold 4 (T760 V1): AFTER the area
       loop, so the map LiveAreasReport sends ahead of each CATCHUP ASK already carries this report's loaded-by marks and player sector. */
    LiveAreasReport(from, slot, payload, n, now);
}
/* P7v (design-noworld-queue 8.3) - TEST-ONLY: --stop-areamaps-after <sec>. Default off (-1). The relay keeps
   servicing its socket and every other message still goes out; only the AREAMAP broadcast stops. That is the
   ONLY way to produce the state the plugin's freshness term exists for - a relay that answers ENet and has
   stopped sending maps - and therefore the only way to separate "wedged" from "one lost second" as an
   experiment rather than as an argument. No shipping path sets it. */
double g_startedAt = 0.0;
double g_stopAreaMapsAfter = -1.0;
/* M9 (T-197; protocol 65): the SEAT BOOK - the slot each of the 256 loaded-by bits names (-1 free). This process's own numbering:
   a restart re-seats players in the order they report, and each game rekeys its carried bits (coopdrop::AreaEffectiveRekey). */
int g_areaSlotOfSeat[coopdrop::kAreaSeats];
struct AreaSeatBookInit { AreaSeatBookInit() { for (int i = 0; i < coopdrop::kAreaSeats; ++i) g_areaSlotOfSeat[i] = -1; } } g_areaSeatBookInit;
long long g_areaSeatsHeld = 0, g_areaSeatsTaken = 0, g_areaSeatsFreed = 0, g_areaSeatsFull = 0, g_areaMapBytesLast = 0; int g_areaSeatsFullSaid = 0;
int g_areaMapsStoppedLogged = 0;
/* M7a fold 4 (T760 V1) - THE AREA MAP AND THE PLAYER SECTORS, BUILT WHERE THEY CAN BE SENT FROM TWICE. AreaTick broadcasts both every
   second; LiveAreasReport sends both to a game it is about to send a CATCHUP ASK, ahead of the ask on the same reliable channel 0, so
   the answering game reads the asker as holding the asked sectors (its loaded-by bit) when it decides the answer. Before this the
   ask could beat the next broadcast and every character there was counted kept. The map carries this instant's marks; the grace
   releases and transfers stay AreaTick's. The seat book is assigned here, so either sender carries the same seats. */
bool AreaMapsStopped(double now) { return g_stopAreaMapsAfter >= 0.0 && g_startedAt > 0.0 && now - g_startedAt >= g_stopAreaMapsAfter; }
unsigned AreaMapBuild(double now, std::vector<char>* out)
{
    std::vector<char>& b = *out; b.clear(); unsigned count = 0;
    {
        std::set<int> fresh;
        for (std::map<long long, AreaRow>::const_iterator it = g_areas.begin(); it != g_areas.end(); ++it)
            for (std::map<int, double>::const_iterator s = it->second.lastSeen.begin(); s != it->second.lastSeen.end(); ++s)
                if (s->first >= 0 && now - s->second <= kGraceSec) fresh.insert(s->first);
        const std::vector<int> listed(fresh.begin(), fresh.end());
        int took = 0, freed = 0;
        const int unseated = coopdrop::AreaSeatAssign(g_areaSlotOfSeat, listed, &took, &freed);
        g_areaSeatsTaken += took; g_areaSeatsFreed += freed;
        g_areaSeatsHeld = 0; for (int j = 0; j < coopdrop::kAreaSeats; ++j) if (g_areaSlotOfSeat[j] >= 0) ++g_areaSeatsHeld;
        if (took > 0 || freed > 0)
            Log("areaSeats: " + N((long long)took) + " slot(s) took a loaded-by seat, " + N((long long)freed) + " gave one back (no fresh mark left) - "
                + N(g_areaSeatsHeld) + " of " + N((long long)coopdrop::kAreaSeats) + " seats held");
        if (unseated > 0)
        {
            ++g_areaSeatsFull;
            if (g_areaSeatsFullSaid == 0)
            {
                g_areaSeatsFullSaid = 1;
                Log("areaSeats: " + N((long long)unseated) + " slot(s) with loaded areas found NO seat - all 256 are held (a departed player keeps its"
                    " marks for the 10 s grace). Their bits are missing from the maps until a seat frees. Counted areaSeats[seatsFull].");
            }
        }
        else g_areaSeatsFullSaid = 0;
    }
    coopdrop::AreaMapPutSeats(&b, g_areaSlotOfSeat);
    std::map<int, int> seatOfSlot;
    for (int j = 0; j < coopdrop::kAreaSeats; ++j) if (g_areaSlotOfSeat[j] >= 0) seatOfSlot[g_areaSlotOfSeat[j]] = j;
    const size_t rowCountAt = b.size(); coopdrop::AreaPutU16(&b, 0u);
    for (std::map<long long, AreaRow>::const_iterator it = g_areas.begin(); it != g_areas.end(); ++it)
    {
        // T215: an entry is x, y, owner, loadedMask. M9: the mask's bit is the reporter's SEAT. An area with reporters but NO owner
        // (transient) still needs its mask, so the entry goes out whenever there is an owner OR any current reporter.
        unsigned mask[coopdrop::kAreaMaskWords]; coopdrop::AreaMaskClear(mask);
        for (std::map<int, double>::const_iterator s = it->second.lastSeen.begin(); s != it->second.lastSeen.end(); ++s)
            if (s->first >= 0 && now - s->second <= kGraceSec)
            {
                std::map<int, int>::const_iterator so = seatOfSlot.find(s->first);
                if (so != seatOfSlot.end()) coopdrop::AreaMaskSet(mask, so->second);
            }
        if (it->second.owner < 0 && !coopdrop::AreaMaskAny(mask)) continue;
        if (count >= (unsigned)coopdrop::kAreaMaxRows) break;
        coopdrop::AreaMapPutRow(&b, (int)(it->first % 64), (int)(it->first / 64), it->second.owner, mask); ++count;
    }
    b[rowCountAt] = (char)(unsigned char)(count & 0xFFu); b[rowCountAt + 1] = (char)(unsigned char)((count >> 8) & 0xFFu);
    g_areaMapBytesLast = (long long)b.size();
    return count;
}
void PlayerSectorsBuild(double now, std::vector<char>* out)
{
    std::vector<char>& pb = *out; pb.clear(); pb.push_back(0); pb.push_back(0); unsigned pcount = 0;   /* M9 (T-197; protocol 65): u16 count, then {u16 slot, i32 x, i32 y} rows */
    std::vector<char> pages;   /* area2 fold: each row's age, u8 tenths of a second (capped 254), appended after the rows - optional by length */
    for (std::map<int, PlayerSec>::const_iterator it = g_playerSec.begin(); it != g_playerSec.end(); ++it)
    {
        if (it->first < 0 || it->first >= (int)coopdrop::kAreaOwnerNone) continue;   /* M9 (T-197): the wire names a slot in two bytes (slots run 0..65519) */
        if (it->second.at <= 0.0 || now - it->second.at > kPlayerSecFreshSec) continue;
        if (pcount >= (unsigned)coopdrop::kPlayerSectorsMaxRows) break;   /* M9 (T-197); M9f1 (M9 review LOW): 512 rows, the game refuses more */
        const int psx = it->second.x, psy = it->second.y;
        const size_t pat = pb.size(); pb.resize(pat + 10);   /* M9 (T-197): u16 slot */
        pb[pat] = (char)(unsigned char)(it->first & 0xFF); pb[pat + 1] = (char)(unsigned char)((it->first >> 8) & 0xFF); memcpy(&pb[pat + 2], &psx, 4); memcpy(&pb[pat + 6], &psy, 4);
        { double ag = (now - it->second.at) * 10.0; if (ag < 0.0) ag = 0.0; if (ag > 254.0) ag = 254.0; pages.push_back((char)(unsigned char)(int)ag); }
        ++pcount;
    }
    pb.insert(pb.end(), pages.begin(), pages.end());
    pb[0] = (char)(unsigned char)(pcount & 0xFFu); pb[1] = (char)(unsigned char)((pcount >> 8) & 0xFFu);   /* M9 (T-197) */
}
void AreaTick(ENetHost* host)
{
    const double now = NowSec();
    int areasChanged = 0;
    if (AreaMapsStopped(now))
    {
        if (g_areaMapsStoppedLogged == 0)
        {
            g_areaMapsStoppedLogged = 1;
            Log("--stop-areamaps-after: AREAMAP broadcasts STOP HERE. The socket is still serviced and every"
                " other message still goes out, so ENet will NOT drop either game - which is the whole point:"
                " this produces a stale area picture with both links UP.");
        }
        return;
    }
    for (std::map<long long, AreaRow>::iterator it = g_areas.begin(); it != g_areas.end(); ++it)
    {
        AreaRow& r = it->second;
        if (r.ownerId.empty()) continue;
        /* B13: the sweep asks the SAME question OnAreas asks, with -1 for "nobody is reporting this right
           now" - the two must not hold two ideas of when a claim is over. An owner that has not dialled back
           in since the restart has been silent for as long as this process has been up, which is why its
           budget is the 60 s restore grace and not the 10 s lease. */
        int next = -1; double best = 0.0;
        for (std::map<int, double>::const_iterator s = r.lastSeen.begin(); s != r.lastSeen.end(); ++s) if (s->first != r.owner && now - s->second <= kGraceSec && s->second > best) { best = s->second; next = s->first; }
        const int ownerKnown = IdReportedThisRun(r.ownerId);
        const double silent = ownerKnown ? (now - r.lastSeenOwner) : (now - g_restoredAtSec);
        int act = coopstore::AreaClaimDecide(ownerKnown, silent, next >= 0 ? 0 : -1, kRestoreGraceSec, kGraceSec);
        /* area2 fold: an UNCONFIRMED assignment is not on the ordinary lease - held 12 s, then to the latest reporter (or released). */
        {
            const int ua = coopstore::UnconfirmedLeaseDecide(r.unconfirmed, 0, now - r.assignedAt, coopstore::kUnconfirmedLeaseSec);
            if (ua == coopstore::kLeaseHold) continue;
            if (ua == coopstore::kLeaseHandOver)
            {
                act = (next >= 0) ? coopstore::kAreaTransfer : coopstore::kAreaRelease; ++g_areasUnconfirmedExpired;
                { char b[176]; _snprintf(b, 175, "[AREAS] unconfirmed %d,%d: slot %d did not report it loaded within %.0fs -> slot %d", (int)(it->first % 64), (int)(it->first / 64), r.owner, coopstore::kUnconfirmedLeaseSec, next); b[175] = 0; Log(b); }
            }
        }
        if (act == coopstore::kAreaKeep) continue;
        { char b[176]; _snprintf(b, 175, "AREA %d,%d slot %d -> %d (holder silent > %.0fs%s)", (int)(it->first % 64), (int)(it->first / 64), r.owner, next, ownerKnown ? kGraceSec : kRestoreGraceSec, ownerKnown ? "" : " - it has not REPORTED an area since this notebook restarted"); b[175] = 0; Log(b); }
        r.owner = next; r.lastSeenOwner = next >= 0 ? best : 0.0;
        r.restored = 0; r.keptCounted = 0; r.restoredAtSec = (double)NowUnix(); r.unconfirmed = 0;
        r.ownerId.clear();
        if (act == coopstore::kAreaTransfer)
        {
            /* The number the row passes to is a slot; the FILE holds player ids, so the new holder has to be
               named. A slot with no connected peer cannot be named and the row is released instead. */
            for (std::map<ENetPeer*, int>::const_iterator sp = g_slot.begin(); sp != g_slot.end(); ++sp)
                if (sp->second == next) { r.ownerId = PeerIdOf(sp->first); break; }
            if (r.ownerId.empty()) { r.owner = -1; r.lastSeenOwner = 0.0; ++g_areasReleased; }
            else ++g_areasTransferredAfterGrace;
        }
        else ++g_areasReleased;
        areasChanged = 1;
    }
    if (areasChanged) WriteAreasNow();
    /* M9 (T-197; protocol 65; design-many D6 (a)): THE LOADED-BY MASK INDEXES A SEAT, 256 WIDE. A slot with a fresh mark anywhere
       in the map keeps (or takes, lowest free) a seat; one with none gives its seat back (coopdrop::AreaSeatAssign). Slots >= 32 now
       have a bit (T215's `s->first < 32` is gone). D7 (a) as before: the whole map to every game.
       M9f1 (M9 review M2): a row is VARIABLE LENGTH now - 5 + seats bytes (37 at most, areadrop.h), not a flat 36. It STAYS on
       channel 0, reliable: channel 1 is kept for M7a's MOVE (effort/m7a; the LIVE comment below), and PLAYERSECTORS goes right
       after the map it belongs to (E25) - ENet orders within a channel only, so a map on channel 1 could land after its
       PLAYERSECTORS. */
    std::vector<char> b; const unsigned count = AreaMapBuild(now, &b);   /* M7a fold 4: built by the function LiveAreasReport also sends from */
    {
        int sent = 0;
        for (size_t i = 0; i < host->peerCount; ++i) { ENetPeer* p = &host->peers[i]; if (p->state == ENET_PEER_STATE_CONNECTED) { SendMsg(p, MSG_AREAMAP, b); ++sent; } }
        /* P7w (R1) - THE AREA-MAP CLOSER IS GONE, and its removal is the fold rather than an oversight.
           It closed the seed window when the world's first non-empty area map had reached everyone. That
           made sense while the window was anchored at the first HELLO; it does not once the window is
           anchored at the FIRST OFFER, because an offer only ever comes from a game whose world is LIVE -
           which is exactly when a non-empty map is already going out. Keeping both would shut the window
           about a second after it opened and the "furthest of the first players" rule would never run,
           which is the defect this fold exists to remove. The one closer is 30 s from the first offer.
           `count` and `sent` are still computed above; nothing about the map broadcast changed. */
        (void)count; (void)sent;
    }
    // E25: and the player sectors, in the SAME tick and immediately after the map, so a game applies the sectors
    // that belong to the map it has just applied rather than to the previous one. {u8 count, [u8 slot, i32 x, i32 y]...}
    // Only slots that reported a sector within kPlayerSecFreshSec go out: a game that has stopped talking is not
    // standing anywhere as far as this process knows, and saying otherwise would freeze an inference (E25.7).
    {
        std::vector<char> pb; PlayerSectorsBuild(now, &pb);   /* M7a fold 4: built by the function LiveAreasReport also sends from */
        for (size_t i = 0; i < host->peerCount; ++i) { ENetPeer* p = &host->peers[i]; if (p->state == ENET_PEER_STATE_CONNECTED) SendMsg(p, MSG_PLAYERSECTORS, pb); }
    }
}

/* M14 (T-197): THE WRITER THREAD LOGS THROUGH HERE TOO - every failure line of a record write (RotatePair,
   SwapIn, WriteMeta) is now printed from it - so one line is written whole, to both outputs, before the next
   begins. Initialised before main() runs, so no Log() can come before it. */
struct LogLock { CRITICAL_SECTION cs; LogLock() { InitializeCriticalSection(&cs); } };
LogLock g_logLock;
void Log(const std::string& s)
{
    EnterCriticalSection(&g_logLock.cs);
    char ts[32]; time_t t = time(0); struct tm* lt = localtime(&t); strftime(ts, sizeof ts, "%H:%M:%S", lt);
    printf("%s %s\n", ts, s.c_str()); fflush(stdout);
    if (g_log) { fprintf(g_log, "%s %s\n", ts, s.c_str()); fflush(g_log); }
    LeaveCriticalSection(&g_logLock.cs);
}
std::string N(long long v) { char b[32]; sprintf(b, "%lld", v); return b; }
std::string F1(float v) { char b[32]; sprintf(b, "%.1f", v); return b; }
std::string Sanitize(const std::string& s)
{
    return coopwq::FileName(s);   /* M14 fold (finding 8): the one rule, shared with the write gate's key (coopwq::GateKey) */
}
// review-p6h LOW-2. EVERY STRING THAT CAME OFF THE WIRE GOES THROUGH HERE BEFORE IT REACHES A LOG LINE.
// A game that is not the session authority can put 64 key/value pairs of arbitrary bytes into one OPTIONS
// message, and RECORD, HELLO, DELETED_BITS, UNIQUE_STATE and RECORD_GONE all carry wire strings too. Left
// unfiltered, a newline in one of them forges a whole log line - and a forged line is indistinguishable from a
// real one to whoever reads store.log afterwards - while a long one bloats the file without limit.
// Printable ASCII only, one line, capped; and the cap ANNOUNCES ITSELF, so a truncated value can never be
// misread as a short one. (Sanitize above is a different function with a different job: it makes a FILENAME.)
std::string SanitizeForLog(const std::string& s)
{
    const size_t kMaxLogged = 96;
    const size_t n = s.size() < kMaxLogged ? s.size() : kMaxLogged;
    std::string o;
    for (size_t i = 0; i < n; ++i) { const unsigned char c = (unsigned char)s[i]; o += (c >= 0x20 && c <= 0x7E) ? (char)c : '?'; }
    if (s.size() > kMaxLogged) o += "...[+" + N((long long)(s.size() - kMaxLogged)) + " bytes]";
    return o;
}
/* ============================ P8d - THE ONE PLACE THIS PROCESS ASKS WHAT A FILE IS ====================
   THREE ANSWERS AND NO OTHERS (src\common\storemeta.h names them, because it is the DECISION that has to
   be able to refuse). P8b asked "did the read succeed?", which answers the same false for "there is no
   such file" and for "the file is there and I could not read it", and then wrote that answer back as an
   index line meaning "this record has no squad" - permanently, over an intact one (review-p8b C-1). P8c
   split the two for ONE file on ONE path; the v5 validation path, three of the upgrade inputs, options.txt
   and the deleted-number bitmaps all kept the old test (review-p8c C-2, H-1, H-3). Every reader in this
   file now comes through here, and the rule that makes structural is:
   A kFileUnreadable ANSWER NEVER REACHES A WRITE - the caller defers, counts it, names the file and the
   Win32 error, and leaves the record on disk exactly as it was.
   COST, stated rather than left to be discovered: a probe READS the file. That is deliberate - a test that
   only asks the folder is precisely the test that cannot tell "I know what this holds" from "the name is
   listed", and this file has paid for that twice. A meta line is ~92 bytes; a payload is 17 KB in the
   folder measured today, and only a PAYLOAD write probes one (that write already moves those bytes twice).
   ==================================================================================================== */
struct FileProbe
{
    int               kind;    /* coopstore::kFileAbsent | kFileUnreadable | kFileReadable */
    unsigned long     err;     /* the Win32 error behind kFileUnreadable. 0 means THE REFUSAL WAS OURS (the
                                  size cap) - a different thing to know, and one that would be invisible if
                                  it shared a code with a real error. */
    long long         attrLen; /* the size the FOLDER reports, available whenever the attribute query worked,
                                  so an unreadable file can still have its size named in a log line.
                                  review-p8c M-5: `EXISTS (-1 bytes)` told a reader nothing. -1 = unknown. */
    long long         len;     /* bytes in hand; meaningful only when kFileReadable */
    unsigned int      crc;     /* CRC-32 of those bytes; meaningful only when kFileReadable */
    std::vector<char> bytes;
    FileProbe() : kind(coopstore::kFileAbsent), err(0), attrLen(-1), len(0), crc(0) {}
};
const long long kProbeMaxBytes = 64LL << 20;
FileProbe ProbeFile(const std::string& path)
{
    FileProbe p;
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &d) == 0)
    {
        const DWORD e = ::GetLastError();
        p.err = (unsigned long)e;
        /* ONLY THESE TWO MEAN ABSENT, and the attribute query is what a FILE_SHARE_NONE open does not
           defeat - which is why the pre-P8b loader survived exactly this case. An access denial or a
           device error says "it may well be there", which is not the same claim. */
        p.kind = (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
                 ? coopstore::kFileAbsent : coopstore::kFileUnreadable;
        return p;
    }
    p.attrLen = (long long)((((unsigned long long)d.nFileSizeHigh) << 32) | d.nFileSizeLow);
    if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    { p.kind = coopstore::kFileUnreadable; p.err = (unsigned long)ERROR_DIRECTORY; p.attrLen = -1; return p; }
    {
        std::ifstream f(path.c_str(), std::ios::binary);
        if (!f)
        {
            /* The stream carries no Win32 error, so the open is repeated with a plain read share purely to
               get one for the log: "a scanner has it" (32) and "we are not allowed near it" (5) ask for
               different repairs, and a 0 here says it opens fine and the refusal was our own. */
            HANDLE h = CreateFileA(path.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
            p.err = (h == INVALID_HANDLE_VALUE) ? (unsigned long)::GetLastError() : 0ul;
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            p.kind = coopstore::kFileUnreadable;
            return p;
        }
        f.seekg(0, std::ios::end);
        const std::streamoff n = f.tellg();
        f.seekg(0);
        if (n < 0 || (long long)n > kProbeMaxBytes)
        { p.kind = coopstore::kFileUnreadable; p.err = 0; return p; }   /* our own cap, and the 0 says so */
        p.bytes.resize((size_t)n);
        if (n) f.read(&p.bytes[0], n);
        if (!f)
        { p.bytes.clear(); p.kind = coopstore::kFileUnreadable; p.err = (unsigned long)::GetLastError(); return p; }
        p.len = (long long)n;
    }
    /* A FILE OF ZERO BYTES IS READABLE WITH A LENGTH OF 0. That is a fact about the file, not a failure to
       read it, and Crc32 of no bytes is 0x00000000 by construction rather than by a special case. */
    p.crc = coopstore::Crc32(p.bytes.empty() ? 0 : &p.bytes[0], p.bytes.size());
    p.kind = coopstore::kFileReadable;
    return p;
}
/* The first line of a probed file, or false when there is none. A file of zero bytes HAS no first line -
   the post-power-cut shape of an <id>.meta - and that is a different fact from "the file could not be
   read". ReadFirstLine, which this replaces, answered the same false for both. */
bool FirstLineOf(const FileProbe& p, std::string* out)
{
    if (p.kind != coopstore::kFileReadable || p.bytes.empty()) return false;
    size_t i = 0;
    while (i < p.bytes.size() && p.bytes[i] != 0x0A) ++i;
    out->assign(&p.bytes[0], i);
    if (!out->empty() && (*out)[out->size() - 1] == 0x0D) out->erase(out->size() - 1);
    return true;
}
/* The whole of a probed text file, for the four small line-based files in this folder. */
std::string TextOf(const FileProbe& p) { return p.bytes.empty() ? std::string() : std::string(&p.bytes[0], p.bytes.size()); }
const char* StateWord(int st)
{
    switch (st)
    {
        case coopstore::kFileAbsent:     return "not there";
        case coopstore::kFileUnreadable: return "ON DISK AND UNREADABLE";
        case coopstore::kFileReadable:   return "readable";
        default:                         return "??";
    }
}
std::string FileOf(const std::string& id) { return g_dir + "\\" + Sanitize(id) + ".platoon"; }
std::string MetaOf(const std::string& id) { return g_dir + "\\" + Sanitize(id) + ".meta"; }
std::string PrevOf(const std::string& path) { return path + ".prev"; }   /* P8b: the one previous version, beside the current one */
// decision 30: a permanent per-faction bitmap of deleted group numbers
std::map<std::string, std::vector<char> > g_bits;
std::string BitsOf(const std::string& faction) { return g_dir + "\\deleted." + Sanitize(faction) + ".bits"; }
bool SplitGroupId(const std::string& id, std::string* faction, unsigned* n)
{
    const size_t u = id.rfind('_'); if (u == std::string::npos || u + 1 >= id.size()) return false;
    for (size_t i = u + 1; i < id.size(); ++i) if (id[i] < '0' || id[i] > '9') return false;
    *faction = id.substr(0, u); *n = (unsigned)atoi(id.c_str() + u + 1); return *n < 1048576u;
}
/* P8d: the header line and the bits out of ALREADY-PROBED bytes, so "there is no such bitmap" and "there
   is one and I could not read it" are two answers at the two call sites rather than one false here. */
static bool ParseBitsBytes(const FileProbe& p, std::string* faction, std::vector<char>* bits)
{
    if (p.kind != coopstore::kFileReadable || p.bytes.empty()) return false;
    size_t i = 0; while (i < p.bytes.size() && p.bytes[i] != 0x0A) ++i;
    std::string head(&p.bytes[0], i);
    if (!head.empty() && head[head.size() - 1] == 0x0D) head.erase(head.size() - 1);
    if (head.size() < 8 || head.compare(0, 8, "kcbits1\t") != 0) return false;
    *faction = head.substr(8);
    const size_t at = (i < p.bytes.size()) ? i + 1 : p.bytes.size();
    bits->assign(p.bytes.begin() + (std::vector<char>::difference_type)at, p.bytes.end());
    return true;
}
static bool ReadBitsFile(const std::string& path, std::string* faction, std::vector<char>* bits)
{
    const FileProbe p = ProbeFile(path);
    if (p.kind == coopstore::kFileUnreadable)
    {
        ++g_bitsDeferredUnreadable;
        Log("bits: " + path + " is ON DISK (" + N(p.attrLen) + " bytes) and could NOT BE READ (error "
            + N((long long)p.err) + ") - the deleted numbers it holds are not loaded and it is NOT written.");
        return false;
    }
    return ParseBitsBytes(p, faction, bits);
}
void WriteBits(const std::string& faction)
{
    std::vector<char>& b = g_bits[faction];
    /* P8d (the same rule as everywhere else in this file, and it was a live hole). This function MERGES the
       bitmap on disk into the one in memory and then rewrites the file. An unreadable read merged NOTHING
       and the rewrite went ahead - so one moment of a scanner holding the file open would drop every
       deleted number this process had not been told about in this session, permanently. An unreadable
       bitmap is now a REFUSAL to write: the numbers stay in memory and the file is rewritten the next time
       it can be read. */
    {
        const FileProbe bp = ProbeFile(BitsOf(faction));
        if (bp.kind == coopstore::kFileUnreadable)
        {
            ++g_bitsDeferredUnreadable;
            Log("bits: " + BitsOf(faction) + " is ON DISK (" + N(bp.attrLen) + " bytes) and could NOT BE READ"
                " (error " + N((long long)bp.err) + ") - it is NOT rewritten. Rewriting it from memory alone"
                " would drop every deleted group number this notebook has not been told about in this"
                " session. The numbers are kept in memory and the file is written the next time it opens.");
            return;
        }
        std::string fac2; std::vector<char> onDisk;
        if (ParseBitsBytes(bp, &fac2, &onDisk)) { if (b.size() < onDisk.size()) b.resize(onDisk.size(), 0); for (size_t i = 0; i < onDisk.size(); ++i) b[i] = (char)((unsigned char)b[i] | (unsigned char)onDisk[i]); }
    }
    const std::string tmp = BitsOf(faction) + ".tmp";
    { std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc); if (!f) return; f << "kcbits1\t" << faction << "\n"; if (!b.empty()) f.write(&b[0], (std::streamsize)b.size()); }
    MoveFileExA(tmp.c_str(), BitsOf(faction).c_str(), MOVEFILE_REPLACE_EXISTING);
}
void SetBit(const std::string& id) { std::string fac; unsigned n = 0; if (!SplitGroupId(id, &fac, &n)) return; std::vector<char>& b = g_bits[fac]; const size_t byte = n >> 3; if (byte >= b.size()) b.resize(byte + 1, 0); b[byte] = (char)((unsigned char)b[byte] | (1u << (n & 7))); WriteBits(fac); }
void LoadBits()
{
    WIN32_FIND_DATAA fd; const std::string pat = g_dir + "\\deleted.*.bits"; HANDLE h = FindFirstFileA(pat.c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return;
    do { std::string fac; std::vector<char> b; if (!ReadBitsFile(g_dir + "\\" + fd.cFileName, &fac, &b) || fac.empty()) continue; std::vector<char>& mine = g_bits[fac]; if (mine.size() < b.size()) mine.resize(b.size(), 0); for (size_t i = 0; i < b.size(); ++i) mine[i] = (char)((unsigned char)mine[i] | (unsigned char)b[i]); } while (FindNextFileA(h, &fd));
    FindClose(h);
}
void PutStr(std::vector<char>* b, const std::string& s); void PutU32(std::vector<char>* b, unsigned v);   /* defined below */
std::vector<char> EncodeBits(const std::string& faction) { std::vector<char> m; PutStr(&m, faction); const std::vector<char>& b = g_bits[faction]; PutU32(&m, (unsigned)b.size()); m.insert(m.end(), b.begin(), b.end()); return m; }

// decision 31(c): the unique-NPC states. One line per named character in uniques.txt, kept permanently for this world,
// rewritten whole through a temp file the way the deleted-number bitmaps are, and pushed in full at WELCOME.
struct UniqueRow { int state; int playerInvolved; UniqueRow() : state(1), playerInvolved(0) {} };
std::map<std::string, UniqueRow> g_uniques;
std::string UniquesFile() { return g_dir + "\\uniques.txt"; }
std::vector<char> EncodeUnique(const std::string& sid, const UniqueRow& r) { std::vector<char> m; PutStr(&m, sid); PutU32(&m, (unsigned)r.state); PutU32(&m, (unsigned)r.playerInvolved); return m; }
bool WriteUniques()
{
    /* P8l (review-p8d C-1). THE LOAD THAT NEVER HAPPENED IS NOT AN EMPTY FILE. This function rewrites
       uniques.txt WHOLE from g_uniques, so running it while the load is deferred writes a map holding only
       what has arrived since this process started - every other state in this world erased, and any
       character the file records as DEAD revived, permanently, because there is no re-publish path for a
       unique whose state never changes again. The change stays in memory, goes out to the games, and is
       merged onto the file by UniquesDeferredRetry the moment it opens. */
    if (g_uniquesLoadDeferred)
    {
        ++g_uniquesWriteRefusedDeferred;
        if (!g_uniquesRefusalSaid)
        {
            g_uniquesRefusalSaid = 1;
            Log("uniques: uniques.txt is NOT REWRITTEN while its load is deferred. The map in memory holds"
                " only what has arrived since this notebook started, so writing it now would erase every"
                " state this world already had and revive any character recorded DEAD. The change is kept"
                " in memory and broadcast to the games; the file is read again every second and the changes"
                " are merged onto it the moment it opens. Said once; counted every time"
                " (uniquesWriteRefusedDeferred).");
        }
        return false;
    }
    const std::string tmp = UniquesFile() + ".tmp";
    { std::ofstream f(tmp.c_str(), std::ios::trunc); if (!f) return false;
      for (std::map<std::string, UniqueRow>::const_iterator it = g_uniques.begin(); it != g_uniques.end(); ++it)
          f << "v1\t" << it->first << "\t" << it->second.state << "\t" << it->second.playerInvolved << "\n"; }
    return MoveFileExA(tmp.c_str(), UniquesFile().c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
/* P8l: the ROWS out of already-probed bytes, so the load and the deferred-load retry cannot hold two ideas
   of what uniques.txt says (6a lesson 11 - remove the hand that keeps two copies in step). */
int UniquesParseInto(const FileProbe& up, std::map<std::string, UniqueRow>* into)
{
    if (up.kind != coopstore::kFileReadable) return 0;
    std::istringstream f(TextOf(up));
    std::string line; int n = 0;
    while (std::getline(f, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        std::vector<std::string> fld; size_t at = 0;
        while (at <= line.size()) { size_t t = line.find('\t', at); if (t == std::string::npos) { fld.push_back(line.substr(at)); break; } fld.push_back(line.substr(at, t - at)); at = t + 1; }
        if (fld.size() < 4 || fld[0] != "v1" || fld[1].empty()) continue;
        UniqueRow r; r.state = atoi(fld[2].c_str()); r.playerInvolved = atoi(fld[3].c_str()) != 0 ? 1 : 0;
        if (r.state < 0 || r.state > 2) continue;
        (*into)[fld[1]] = r; ++n;
    }
    return n;
}
void LoadUniques()
{
    const FileProbe up = ProbeFile(UniquesFile());
    if (up.kind == coopstore::kFileUnreadable)
    {
        g_uniquesLoadDeferred = 1; g_uniquesRefusalSaid = 0;
        Log("uniques: uniques.txt is ON DISK (" + N(up.attrLen) + " bytes) and could NOT BE READ (error "
            + N((long long)up.err) + ") - the unique-character states this world already has are NOT loaded"
            " this session, and NOTHING is written over them. P8l (review-p8d C-1): THE LOAD IS DEFERRED,"
            " so every writer of this file now REFUSES until it opens - a UNIQUE_STATE arriving in the"
            " meantime is kept in memory and broadcast, and the file is retried every second.");
        return;
    }
    g_uniquesLoadDeferred = 0;
    if (up.kind != coopstore::kFileReadable) return;
    const int n = UniquesParseInto(up, &g_uniques);
    Log("uniques: " + N(n) + " states loaded from uniques.txt");
}
/* P8l (review-p8d C-1), the other half: THE RETRY, on the 1 Hz tick. What is in g_uniques here arrived
   while the load was deferred, so every row is a CHANGE - and the DEAD-IS-TERMINAL guard that could not
   fire against the empty map is applied now against the map that was actually on the file. A row the merge
   refuses is BROADCAST BACK, because that row was accepted and sent to every game while the file was shut,
   and nothing else would ever correct it. */
void UniquesDeferredRetry(ENetHost* host)
{
    if (!g_uniquesLoadDeferred) return;
    const FileProbe up = ProbeFile(UniquesFile());
    if (up.kind == coopstore::kFileUnreadable) return;   /* still shut - look again next second */
    std::map<std::string, UniqueRow> onDisk;
    const int onDiskRows = UniquesParseInto(up, &onDisk);
    const std::map<std::string, UniqueRow> deferred = g_uniques;
    long long took = 0, inserted = 0, keptDead = 0;
    std::vector<std::string> corrected;
    for (std::map<std::string, UniqueRow>::const_iterator it = deferred.begin(); it != deferred.end(); ++it)
    {
        std::map<std::string, UniqueRow>::const_iterator ex = onDisk.find(it->first);
        const int haveLoaded = (ex != onDisk.end()) ? 1 : 0;
        const int d = coopmerge::UniqueMergeDecide(haveLoaded, haveLoaded ? ex->second.state : 1, it->second.state);
        if (d == coopmerge::kMergeKeepLoaded)
        {
            ++keptDead; corrected.push_back(it->first);
            Log("UNIQUE " + SanitizeForLog(it->first) + " stays DEAD - state " + N((long long)it->second.state)
                + " was accepted while uniques.txt could not be read, and is REFUSED now that the file has"
                " been read (review-p5j H-2: nothing revives a dead entry). The stored state is sent back to"
                " every connected game.");
            continue;
        }
        if (haveLoaded) ++took; else ++inserted;
        onDisk[it->first] = it->second;
    }
    g_uniques = onDisk;
    g_uniquesLoadDeferred = 0;
    const long long changes = (long long)deferred.size();
    if (changes > 0 && !WriteUniques())
    {
        g_uniquesLoadDeferred = 1;
        Log("uniques: uniques.txt opened and the " + N(changes) + " held change(s) merged, but the merged file"
            " could NOT BE WRITTEN - the load stays deferred, memory keeps the merge, and this is retried"
            " next second.");
        return;
    }
    ++g_uniquesDeferredLoadRecovered;
    Log("uniques: uniques.txt READ AT LAST - " + N((long long)onDiskRows) + " state(s) were on disk and "
        + N(changes) + " change(s) had been held in memory. Merged (" + N(took) + " overwrote a stored row, "
        + N(inserted) + " new, " + N(keptDead) + " REFUSED as a revival of a DEAD character) and written"
        " once. deferredLoadRecovered(uniques)=" + N(g_uniquesDeferredLoadRecovered)
        + " uniquesWriteRefusedDeferred=" + N(g_uniquesWriteRefusedDeferred));
    /* review-p8l M-2. THE WHOLE MERGED MAP GOES BACK ON THE WIRE, NOT THE REFUSED SUBSET. P8l sent only
       the rows the merge refused, so the rows the FILE held - every unique state this world already had -
       were never sent, and a game that connected while the load was deferred had been handed an EMPTY
       unique map in its WELCOME and was never corrected (measured). options.txt and clock.txt already
       republish their merged state; this is uniques doing the same. `corrected` is kept for the log, which
       is the only thing the refusal is a fact for. Bounded by the size of the map, once per recovery. */
    {
        const std::set<std::string> refusedSet(corrected.begin(), corrected.end());
        for (std::map<std::string, UniqueRow>::const_iterator row = g_uniques.begin(); row != g_uniques.end(); ++row)
        {
            const int wasRefused = (refusedSet.find(row->first) != refusedSet.end()) ? 1 : 0;
            if (!coopmerge::UniqueRepublishRow(1, wasRefused)) continue;
            const std::vector<char> um = EncodeUnique(row->first, row->second);
            for (size_t p = 0; p < host->peerCount; ++p)
                if (FeedForwardTo(&host->peers[p], 0, MSG_UNIQUE_STATE)) SendMsg(&host->peers[p], MSG_UNIQUE_STATE, um);   /* T-313: subscribed games only - the others get the whole map at their first ASK */
            ++g_uniquesRepublished;
        }
        Log("uniques: the MERGED map has been REPUBLISHED IN FULL - " + N((long long)g_uniques.size())
            + " row(s) to every connected game, of which " + N((long long)corrected.size()) + " were the"
            " rows the merge refused. A game that connected while uniques.txt was shut was handed an EMPTY"
            " unique map in its WELCOME, and until now nothing ever corrected it (review-p8l M-2).");
    }
}

/* loot2b (docs/design-loot2.md 2A item 1, 4A row L2b'; T361): THE RESEARCH ROWS. research_boxes.txt holds one line per
   research item (item +0x180 "artifact") that the game holding a box's area LIFTED out of it when the box was opened, keyed
   by box key + item index (src/common/researchwire.h), in the uniques.txt shape: rewritten whole through a temp file, pushed
   in full at WELCOME, and each NEW box broadcast to every game. FIRST WRITER WINS PER BOX - a box already here is never
   replaced, and a game that knows the box never lifts it again. Any linked game may write: the lift is made by whichever
   game holds the area, so there is no authority test. */
coopres::ResearchTable g_research;
int g_researchLoadDeferred = 0;
long long g_researchBoxesIn = 0, g_researchBoxesStored = 0, g_researchBoxesDup = 0, g_researchWriteFailed = 0;
long long g_researchBadLines = 0, g_researchPushSkipped = 0, g_researchDeferredLoadRecovered = 0, g_researchDeferredLost = 0;   /* loot2b fold 2 */
std::string ResearchFile() { return g_dir + "\\research_boxes.txt"; }
bool WriteResearch()
{
    /* the file is on disk and could not be read at start: rewriting it whole from memory would erase its rows */
    if (g_researchLoadDeferred) return false;
    const std::string tmp = ResearchFile() + ".tmp";
    { std::ofstream f(tmp.c_str(), std::ios::trunc); if (!f) return false;
      for (coopres::ResearchTable::const_iterator it = g_research.begin(); it != g_research.end(); ++it) f << coopres::ResearchLines(it->first, it->second);
      if (!f) return false; }
    return MoveFileExA(tmp.c_str(), ResearchFile().c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
/* loot2b fold 2 (review-loot2b 3a): the rows out of already-probed bytes - one reader for the load and the retry. A line
   that does not fit the wire (length, control characters, over 1024 rows in a box) is skipped and counted. */
long long ResearchParseInto(const FileProbe& rp, coopres::ResearchTable* into)
{
    if (rp.kind != coopstore::kFileReadable) return 0;
    std::istringstream f(TextOf(rp));
    std::string line; long long bad = 0;
    while (std::getline(f, line)) { if (line.empty() || line == "\r") continue; if (coopres::ResearchParseLine(line, into) == 0) ++bad; }
    g_researchBadLines += bad;
    return bad;
}
void LoadResearch()
{
    const FileProbe rp = ProbeFile(ResearchFile());
    if (rp.kind == coopstore::kFileUnreadable)
    {
        g_researchLoadDeferred = 1;
        Log("research: research_boxes.txt is ON DISK (" + N(rp.attrLen) + " bytes) and could NOT BE READ (error " + N((long long)rp.err)
            + ") - it is NOT rewritten until it opens; new rows are kept in memory and broadcast, and the file is retried every second and merged (review-loot2b 3b)");
        return;
    }
    g_researchLoadDeferred = 0;
    if (rp.kind != coopstore::kFileReadable) { Log("research: no research_boxes.txt yet - no box has been lifted in this world"); return; }
    const long long bad = ResearchParseInto(rp, &g_research);
    Log("research: " + N((long long)g_research.size()) + " lifted boxes, " + N(coopres::ResearchRowCount(g_research))
        + " research rows loaded from research_boxes.txt (" + N(bad) + " unusable lines skipped - researchBadLines)");
}
/* loot2b fold 2 (review-loot2b 3b): THE RETRY, on the 1 Hz DeferredLoadTick, the uniques.txt way. What g_research holds
   while the load is deferred arrived this session; the file's own rows WIN (first writer - they are older), a box only in
   memory is kept, and every box only on disk goes to the games (they must not lift it again). Then the file is written. */
void ResearchDeferredRetry(ENetHost* host)
{
    if (!g_researchLoadDeferred) return;
    const FileProbe rp = ProbeFile(ResearchFile());
    if (rp.kind == coopstore::kFileUnreadable) return;
    coopres::ResearchTable onDisk;
    if (rp.kind == coopstore::kFileReadable) ResearchParseInto(rp, &onDisk);
    long long kept = 0, lost = 0;
    std::vector<coopres::ResearchBox> diskOnly;
    for (coopres::ResearchTable::const_iterator it = onDisk.begin(); it != onDisk.end(); ++it)
        if (g_research.find(it->first) == g_research.end()) { coopres::ResearchBox b; b.key = it->first; b.rows = it->second; diskOnly.push_back(b); }
    for (coopres::ResearchTable::const_iterator it = g_research.begin(); it != g_research.end(); ++it)
    {
        if (onDisk.find(it->first) == onDisk.end()) { onDisk[it->first] = it->second; ++kept; }
        else ++lost;   /* the file already had this key: its rows stand */
    }
    g_research = onDisk;
    g_researchLoadDeferred = 0;
    g_researchDeferredLost += lost;
    ++g_researchDeferredLoadRecovered;
    const bool wrote = WriteResearch();
    if (!wrote) ++g_researchWriteFailed;
    Log(std::string(wrote ? "research: research_boxes.txt OPENED - " : "RESEARCH WRITE FAILED after the deferred load opened - ") + N(kept)
        + " box(es) that arrived while it could not be read were merged, " + N(lost) + " lost to rows the file already had (first writer), "
        + N((long long)diskOnly.size()) + " box(es) only on disk sent to the games; tableBoxes=" + N((long long)g_research.size())
        + " tableRows=" + N(coopres::ResearchRowCount(g_research)) + " researchWriteFailed=" + N(g_researchWriteFailed));
    for (size_t at = 0; at < diskOnly.size(); at += 256)
    {
        std::vector<coopres::ResearchBox> chunk(diskOnly.begin() + at, diskOnly.begin() + (at + 256 < diskOnly.size() ? at + 256 : diskOnly.size()));
        std::vector<char> m;
        if (!coopres::EncodeResearchBoxes(&m, chunk)) { g_researchPushSkipped += (long long)chunk.size(); continue; }
        for (size_t p = 0; p < host->peerCount; ++p) { ENetPeer* q = &host->peers[p]; if (q->state == ENET_PEER_STATE_CONNECTED) SendMsg(q, MSG_RESEARCH_BOX, m); }
    }
}
/* every stored box to one game, in chunks of 256 boxes (after the unique states in the WELCOME push) */
void SendResearchPush(ENetPeer* to)
{
    std::vector<coopres::ResearchBox> chunk;
    coopres::ResearchTable::const_iterator it = g_research.begin();
    while (it != g_research.end())
    {
        coopres::ResearchBox b; b.key = it->first; b.rows = it->second; chunk.push_back(b); ++it;
        if (chunk.size() >= 256 || it == g_research.end())
        {
            std::vector<char> m;
            if (coopres::EncodeResearchBoxes(&m, chunk)) SendMsg(to, MSG_RESEARCH_BOX, m);
            else
            {
                /* loot2b fold 2 (3a): one bad box must not cost the chunk - each box alone, the unencodable ones skipped and counted */
                for (size_t c = 0; c < chunk.size(); ++c)
                {
                    std::vector<coopres::ResearchBox> solo(1, chunk[c]); std::vector<char> m1;
                    if (coopres::EncodeResearchBoxes(&m1, solo)) SendMsg(to, MSG_RESEARCH_BOX, m1);
                    else { ++g_researchPushSkipped; Log("research: box " + SanitizeForLog(chunk[c].key) + " does not fit RESEARCH_BOX - not sent (researchPushSkipped=" + N(g_researchPushSkipped) + ")"); }
                }
            }
            chunk.clear();
        }
    }
}
void OnResearchBox(ENetHost* host, ENetPeer* from, const std::vector<char>& payload)
{
    std::vector<coopres::ResearchBox> in;
    const int why = coopres::DecodeResearchBoxes(payload.empty() ? 0 : &payload[0], payload.size(), &in);
    if (why != coopres::kResearchDecodeOk) { Log("malformed RESEARCH_BOX from " + PeerName(from) + " (reason " + N((long long)why) + ") - ignored"); return; }
    std::vector<coopres::ResearchBox> fresh;
    for (size_t i = 0; i < in.size(); ++i)
    {
        ++g_researchBoxesIn;
        if (coopres::ResearchMerge(&g_research, in[i]) != 0)
        {
            ++g_researchBoxesStored; fresh.push_back(in[i]);
            Log("RESEARCH box " + SanitizeForLog(in[i].key) + " rows=" + N((long long)in[i].rows.size()) + " from " + PeerName(from)
                + " - stored (tableBoxes=" + N((long long)g_research.size()) + " tableRows=" + N(coopres::ResearchRowCount(g_research)) + ")");
        }
        else
        {
            ++g_researchBoxesDup;
            Log("RESEARCH box " + SanitizeForLog(in[i].key) + " from " + PeerName(from) + " is already in research_boxes.txt - first writer wins, ignored (researchBoxesDup=" + N(g_researchBoxesDup) + ")");
        }
    }
    if (fresh.empty()) return;
    if (!WriteResearch())
    {
        ++g_researchWriteFailed;
        /* loot2b fold: LOUD. The writer has already removed these items from the world, so rows that are only in memory are
           lost at the next notebook restart. Behaviour is unchanged in this step (kept in memory and broadcast); the failure is
           said on every occurrence and counted on the status line (researchWriteFailed). */
        Log("RESEARCH WRITE FAILED: research_boxes.txt could NOT be written" + std::string(g_researchLoadDeferred ? " (its load is deferred - the file on disk could not be read at start)" : "")
            + " - " + N((long long)fresh.size()) + " new box(es) from " + PeerName(from) + " stay in MEMORY ONLY and are broadcast; they are LOST if this notebook restarts before a later write succeeds (researchWriteFailed=" + N(g_researchWriteFailed) + ")");
    }
    std::vector<char> m;
    if (!coopres::EncodeResearchBoxes(&m, fresh)) { Log("research: the new boxes did not re-encode - not broadcast"); return; }
    for (size_t p = 0; p < host->peerCount; ++p) { ENetPeer* q = &host->peers[p]; if (q->state == ENET_PEER_STATE_CONNECTED) SendMsg(q, MSG_RESEARCH_BOX, m); }
}
/* refill1 (docs/design-refill1.md s2; store protocol 50): THE TOWN BAR ROWS. town_bars.txt holds one line per town, keyed by
   the town's FCS stringID (src/common/barwire.h): the bar's usual size, reported by the game holding the town at its
   first check-up after the town's first roll (the LARGEST ever reported wins - coopbar::BarMerge; review-refill1: one chance roll can come out small), and the in-game time the bar was last filled (the LATEST
   wins). The research_boxes.txt shape: rewritten whole through a temp file, pushed in full at WELCOME, and every row that
   CHANGED broadcast to every game. Any linked game may write: the holder of a town's area changes between players. */
coopbar::BarTable g_bars;
int g_barsLoadDeferred = 0;
long long g_barRowsIn = 0, g_barRowsChanged = 0, g_barRowsSame = 0, g_barsWriteFailed = 0;
std::string BarsFile() { return g_dir + "\\town_bars.txt"; }
bool WriteBars()
{
    /* the file is on disk and could not be read at start: rewriting it whole from memory would erase its rows */
    if (g_barsLoadDeferred) return false;
    const std::string tmp = BarsFile() + ".tmp";
    { std::ofstream f(tmp.c_str(), std::ios::trunc); if (!f) return false;
      for (coopbar::BarTable::const_iterator it = g_bars.begin(); it != g_bars.end(); ++it) f << coopbar::BarLine(it->first, it->second);
      if (!f) return false; }
    return MoveFileExA(tmp.c_str(), BarsFile().c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
void LoadBars()
{
    const FileProbe rp = ProbeFile(BarsFile());
    if (rp.kind == coopstore::kFileUnreadable)
    {
        g_barsLoadDeferred = 1;
        Log("bars: town_bars.txt is ON DISK (" + N(rp.attrLen) + " bytes) and could NOT BE READ (error " + N((long long)rp.err)
            + ") - it is NOT rewritten this session; new rows are kept in memory and broadcast only (barsWriteFailed counts them)");
        return;
    }
    g_barsLoadDeferred = 0;
    if (rp.kind != coopstore::kFileReadable) { Log("bars: no town_bars.txt yet - no bar has been recorded in this world"); return; }
    std::istringstream f(TextOf(rp));
    std::string line; long long bad = 0;
    while (std::getline(f, line)) { if (line.empty() || line == "\r") continue; if (coopbar::BarParseLine(line, &g_bars) == 0) ++bad; }
    Log("bars: " + N((long long)g_bars.size()) + " town bar rows loaded from town_bars.txt (" + N(bad) + " unusable lines)");
}
/* every stored row to one game, in chunks of 256 (after the research boxes in the WELCOME push) */
void SendBarsPush(ENetPeer* to)
{
    std::vector<coopbar::BarWireRow> chunk;
    coopbar::BarTable::const_iterator it = g_bars.begin();
    while (it != g_bars.end())
    {
        coopbar::BarWireRow w; w.sid = it->first; w.row = it->second; chunk.push_back(w); ++it;
        if (chunk.size() >= coopbar::kBarMaxRows || it == g_bars.end())
        {
            std::vector<char> m;
            if (coopbar::EncodeBarRows(&m, chunk)) SendMsg(to, MSG_TOWN_BAR, m);
            else Log("bars: a WELCOME chunk of " + N((long long)chunk.size()) + " rows did not encode - not sent");
            chunk.clear();
        }
    }
}
void OnTownBar(ENetHost* host, ENetPeer* from, const std::vector<char>& payload)
{
    std::vector<coopbar::BarWireRow> in;
    if (coopbar::DecodeBarRows(payload.empty() ? 0 : &payload[0], payload.size(), &in) == 0) { Log("malformed TOWN_BAR from " + PeerName(from) + " - ignored"); return; }
    std::vector<coopbar::BarWireRow> changed;
    for (size_t i = 0; i < in.size(); ++i)
    {
        ++g_barRowsIn;
        if (coopbar::BarMerge(&g_bars, in[i].sid, in[i].row) == 0) { ++g_barRowsSame; continue; }
        ++g_barRowsChanged;
        coopbar::BarWireRow w; w.sid = in[i].sid; w.row = g_bars[in[i].sid]; changed.push_back(w);
        Log("TOWN_BAR " + SanitizeForLog(w.sid) + " usual=" + (w.row.usualSet ? N((long long)w.row.usualPeople) + "/" + N((long long)w.row.usualSquads) : std::string("-"))
            + " lastFilled=" + F1(w.row.lastFilled) + "h from " + PeerName(from) + " (rows=" + N((long long)g_bars.size()) + ")");
    }
    if (changed.empty()) return;
    if (!WriteBars())
    {
        ++g_barsWriteFailed;
        Log("bars: town_bars.txt could NOT be written - the changed rows stay in memory and are broadcast (barsWriteFailed=" + N(g_barsWriteFailed) + ")");
    }
    std::vector<char> m;
    if (!coopbar::EncodeBarRows(&m, changed)) { Log("bars: the changed rows did not re-encode - not broadcast"); return; }
    for (size_t p = 0; p < host->peerCount; ++p) { ENetPeer* q = &host->peers[p]; if (q->state == ENET_PEER_STATE_CONNECTED) SendMsg(q, MSG_TOWN_BAR, m); }
}
/* par24 (parity P24; src/common/worldrelwire.h; decision 48): THE WORLD'S FACTION-VS-FACTION TABLE. world_relations.txt
   holds one line per directed NPC-faction pair (never a player or a stand-in - the header refuses '@' names). This process
   is the record: kind 1 SEED is taken only for a pair the table lacks (first writer wins - the first game into the world
   with a running world gives it its save's values; S2-62: while the OPERATOR is linked and has not ended its walk, another
   game's seed is held, then merged first-writer AFTER the operator's rows - re-check B: the world's own save sets the
   starting table, and the pairs it lacks are filled from the held rows), kind 2 CHANGE always replaces (the
   latest to arrive wins), every row taken goes to every OTHER admitted game in the order it was taken, and the sender gets
   one kind 4 ECHO row per CHANGE row (the table's row after the merge, taken or already held - review-par24 #2: the game
   holds the notebook's rows for a pair until its last send of it is answered), so two games that changed one pair at once
   both end on the row this table holds. The file is rewritten whole through a temp file, at most once a second
   (WorldRelFlushTick) - a battle can move pairs every frame and the table can hold ~10k rows. */
coopwrel::Table g_wrel;
int g_wrelLoadDeferred = 0, g_wrelDirty = 0;
long long g_wrelIn = 0, g_wrelTaken = 0, g_wrelSeedHeld = 0, g_wrelSame = 0, g_wrelRefused = 0, g_wrelMalformed = 0, g_wrelNotAdmitted = 0,
          g_wrelWrites = 0, g_wrelWriteFailed = 0, g_wrelLines = 0, g_wrelPushed = 0;
long long g_wrelLastReported = -1;
long long g_wrelEchoed = 0, g_wrelSeedHoldIn = 0, g_wrelSeedHoldDropped = 0, g_wrelSeedFallback = 0, g_wrelStandInDropped = 0, g_wrelSeedHoldAfterOp = 0;   /* re-check B: held rows merged after the operator's walk */
int g_wrelOpSeeded = 0;                            /* S2-62: the operator's game has ended its walk (SEED_END) this run */
std::vector<coopwrel::WireRow> g_wrelHeld;         /* S2-62: other games' seeds held for the operator's walk, in arrival order */
std::string WorldRelFile() { return g_dir + "\\world_relations.txt"; }
bool WriteWorldRel()
{
    if (g_wrelLoadDeferred) return false;   /* on disk and unreadable at start: rewriting it from memory would erase its rows */
    const std::string tmp = WorldRelFile() + ".tmp";
    { std::ofstream f(tmp.c_str(), std::ios::trunc); if (!f) return false;
      for (coopwrel::Table::const_iterator it = g_wrel.begin(); it != g_wrel.end(); ++it)
      { std::string a, b; if (coopwrel::SplitKey(it->first, &a, &b)) f << coopwrel::Line(a, b, it->second); }
      if (!f) return false; }
    return MoveFileExA(tmp.c_str(), WorldRelFile().c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
void LoadWorldRel()
{
    g_wrelDirty = 0;
    const FileProbe rp = ProbeFile(WorldRelFile());
    if (rp.kind == coopstore::kFileUnreadable)
    {
        g_wrelLoadDeferred = 1;
        Log("worldrel: world_relations.txt is ON DISK (" + N(rp.attrLen) + " bytes) and could NOT BE READ (error " + N((long long)rp.err)
            + ") - it is NOT rewritten this session; rows taken are kept in memory and sent only (worldRelWriteFailed counts the refused writes)");
        return;
    }
    g_wrelLoadDeferred = 0;
    if (rp.kind != coopstore::kFileReadable) { Log("worldrel: no world_relations.txt yet - the first game into this world with a running world seeds it from its save"); return; }
    std::istringstream f(TextOf(rp));
    std::string line; long long bad = 0, standIn = 0;
    while (std::getline(f, line)) { if (line.empty() || line == "\r") continue; int si = 0; if (coopwrel::ParseLine(line, &g_wrel, &si) == 0) { if (si) ++standIn; else ++bad; } }
    g_wrelStandInDropped = standIn;
    if (standIn > 0) g_wrelDirty = 1;   /* review-par24 #1: the next flush rewrites the file without them */
    Log("worldrel: " + N((long long)g_wrel.size()) + " faction-vs-faction rows loaded from world_relations.txt (" + N(bad) + " unusable lines, "
        + N(standIn) + " stand-in rows dropped - a stand-in's record id coop-p<n> / coop-peer is another player's standing, never a world row)");
}
void SendWorldRelChunks(ENetHost* host, ENetPeer* to, const std::vector<coopwrel::WireRow>& rows, int kind, ENetPeer* except = 0)   /* kind: 3 ROWS for the WELCOME push, 4 ECHO to the sender; a broadcast keeps the kind it was taken as (1 SEED / 2 CHANGE), so a game can tell another game's engine change from a seed; except: a CHANGE's sender (it gets the ECHO instead) */
{
    std::vector<coopwrel::WireRow> chunk;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        chunk.push_back(rows[i]);
        if (chunk.size() >= coopwrel::kWrMaxRows || i + 1 == rows.size())
        {
            std::vector<char> m;
            if (!coopwrel::Encode(&m, kind, chunk)) Log("worldrel: a chunk of " + N((long long)chunk.size()) + " rows did not encode - not sent");
            else if (to != 0) SendMsg(to, MSG_WORLD_REL, m);
            else for (size_t p = 0; p < host->peerCount; ++p) { ENetPeer* q = &host->peers[p]; if (q != except && q->state == ENET_PEER_STATE_CONNECTED && g_peerId.count(q) != 0) SendMsg(q, MSG_WORLD_REL, m); }
            chunk.clear();
        }
    }
}
/* every row to one game, in chunks of 256 (inside the WELCOME push, after the town bars) */
void SendWorldRelPush(ENetPeer* to)
{
    std::vector<coopwrel::WireRow> all;
    for (coopwrel::Table::const_iterator it = g_wrel.begin(); it != g_wrel.end(); ++it)
    { coopwrel::WireRow w; if (!coopwrel::SplitKey(it->first, &w.a, &w.b)) continue; w.row = it->second; all.push_back(w); }
    if (all.empty()) return;
    SendWorldRelChunks(0, to, all, coopwrel::kWrRows);
    g_wrelPushed += (long long)all.size();
    Log("worldrel: pushed " + N((long long)all.size()) + " faction-vs-faction rows to " + PeerName(to));
}
/* S2-62: is the OPERATOR's game (B13 / decision 49, PeerIsAuthority - the player whose saves number the world) admitted now? */
bool WorldRelOperatorLinked()
{
    for (std::map<ENetPeer*, std::string>::const_iterator it = g_peerId.begin(); it != g_peerId.end(); ++it)
        if (it->first != 0 && it->first->state == ENET_PEER_STATE_CONNECTED && PeerIsAuthority(it->first)) return true;
    return false;
}
/* S2-62: the held seeds through the first-writer merge, in arrival order - FALLBACK (the operator is not linked any more and
   has not ended its walk: first-in), or re-check B (the operator's walk has ended: its rows are in, so its values win and the
   held rows fill only the pairs its save lacks) */
void WorldRelReleaseHeld(ENetHost* host, const char* why, bool fallback)
{
    if (g_wrelHeld.empty()) return;
    std::vector<coopwrel::WireRow> held; held.swap(g_wrelHeld);
    std::vector<coopwrel::WireRow> taken;
    if (fallback) g_wrelSeedFallback += (long long)held.size(); else g_wrelSeedHoldAfterOp += (long long)held.size();
    coopwrel::MergeHeld(&g_wrel, held, &taken, &g_wrelSeedHeld, &g_wrelRefused);
    g_wrelTaken += (long long)taken.size();
    Log("WORLD_REL seed: " + N((long long)held.size()) + " held seed rows released " + (fallback ? "first-in" : "after the operator's walk") + " (" + why + "), " + N((long long)taken.size())
        + " taken (rows=" + N((long long)g_wrel.size()) + ")");
    if (taken.empty()) return;
    g_wrelDirty = 1;
    SendWorldRelChunks(host, 0, taken, coopwrel::kWrSeed);
}
void OnWorldRel(ENetHost* host, ENetPeer* from, const std::vector<char>& payload)
{
    if (g_peerId.count(from) == 0) { ++g_wrelNotAdmitted; return; }   /* a lobby connection has no world yet */
    int kind = 0; std::vector<coopwrel::WireRow> in;
    if (coopwrel::Decode(payload.empty() ? 0 : &payload[0], payload.size(), &kind, &in) == 0 || kind == coopwrel::kWrRows || kind == coopwrel::kWrEcho)
    { ++g_wrelMalformed; Log("malformed WORLD_REL from " + PeerName(from) + " - ignored"); return; }
    const bool seed = (kind == coopwrel::kWrSeed || kind == coopwrel::kWrSeedEnd);
    const int mergeKind = seed ? (int)coopwrel::kWrSeed : kind;
    int sd = coopwrel::kSdTake;
    if (seed)
    {
        const bool fromOp = PeerIsAuthority(from), opLinked = WorldRelOperatorLinked();
        sd = coopwrel::SeedDecision(fromOp, opLinked, g_wrelOpSeeded != 0);
        if (sd == coopwrel::kSdFallback) WorldRelReleaseHeld(host, "the operator is not linked", true);
        if (sd == coopwrel::kSdHold)
        {
            for (size_t i = 0; i < in.size(); ++i)
            {
                ++g_wrelIn;
                if (g_wrelHeld.size() < (size_t)coopwrel::kWrMaxTable) { g_wrelHeld.push_back(in[i]); ++g_wrelSeedHoldIn; } else ++g_wrelSeedHoldDropped;
            }
            Log("WORLD_REL seed from " + PeerName(from) + ": " + N((long long)in.size()) + " rows HELD (held=" + N((long long)g_wrelHeld.size())
                + ") - the operator's game is linked and has not ended its own walk (S2-62: the world's own save sets the starting table)");
            return;
        }
    }
    std::vector<coopwrel::WireRow> taken, echo;
    for (size_t i = 0; i < in.size(); ++i)
    {
        ++g_wrelIn;
        const int r = coopwrel::Merge(&g_wrel, in[i].a, in[i].b, in[i].row, mergeKind);
        if (kind == coopwrel::kWrChange)   /* review-par24 #2: every CHANGE row is answered to its sender - the table's row after the merge */
        {
            coopwrel::WireRow e = in[i];
            coopwrel::Table::const_iterator h = g_wrel.find(coopwrel::Key(in[i].a, in[i].b));
            if (h != g_wrel.end()) e.row = h->second;
            echo.push_back(e);
        }
        if (sd == coopwrel::kSdFallback) ++g_wrelSeedFallback;
        if (r < 0) { ++g_wrelRefused; continue; }
        if (r == 0) { if (seed) ++g_wrelSeedHeld; else ++g_wrelSame; continue; }
        ++g_wrelTaken; taken.push_back(in[i]);
        if (kind == coopwrel::kWrChange && g_wrelLines < 40)
        {
            ++g_wrelLines;
            Log("WORLD_REL change " + SanitizeForLog(in[i].a) + "->" + SanitizeForLog(in[i].b) + " relation=" + F1(in[i].row.relation)
                + " flags=" + N((long long)in[i].row.flags) + " from " + PeerName(from) + " (rows=" + N((long long)g_wrel.size()) + ")");
        }
    }
    if (seed)
        Log("WORLD_REL seed from " + PeerName(from) + ": " + N((long long)in.size()) + " offered, " + N((long long)taken.size()) + " taken (rows=" + N((long long)g_wrel.size()) + ")"
            + (sd == coopwrel::kSdFallback ? std::string(" - first-in: the operator's game is not linked (S2-62 fallback)") : std::string())
            + (kind == coopwrel::kWrSeedEnd ? std::string(" - the end of that game's walk") : std::string()));
    const bool opWalkEnded = (kind == coopwrel::kWrSeedEnd && PeerIsAuthority(from) && g_wrelOpSeeded == 0);
    if (opWalkEnded)
    {
        g_wrelOpSeeded = 1;
        Log("WORLD_REL seed: the operator's walk is complete - the world's own save set the starting table; " + N((long long)g_wrelHeld.size())
            + " held seed rows from other games merge after it, first writer (S2-62, re-check B: they fill only the pairs the operator's save lacks)");
    }
    if (!echo.empty()) { SendWorldRelChunks(host, from, echo, coopwrel::kWrEcho); g_wrelEchoed += (long long)echo.size(); }
    if (!taken.empty())
    {
        g_wrelDirty = 1;
        SendWorldRelChunks(host, 0, taken, seed ? (int)coopwrel::kWrSeed : kind, kind == coopwrel::kWrChange ? from : 0);
    }
    if (opWalkEnded) WorldRelReleaseHeld(host, "the operator's walk is complete", false);   /* re-check B: AFTER the operator's rows are in and sent */
}
/* the 1 Hz tick: the file, when a row changed since the last write; the counters once a minute when they moved */
void WorldRelFlushTick(ENetHost* host)
{
    if (!g_wrelHeld.empty() && g_wrelOpSeeded == 0 && !WorldRelOperatorLinked()) WorldRelReleaseHeld(host, "the operator's game is no longer linked", true);   /* S2-62 fallback */
    if (g_wrelDirty)
    {
        if (WriteWorldRel()) { ++g_wrelWrites; g_wrelDirty = 0; }
        else { ++g_wrelWriteFailed; if (g_wrelLoadDeferred) g_wrelDirty = 0; }
    }
    static int s = 0;
    if (++s < 60) return;
    s = 0;
    if (g_wrelIn == g_wrelLastReported) return;
    g_wrelLastReported = g_wrelIn;
    Log("worldrel: rows=" + N((long long)g_wrel.size()) + " in=" + N(g_wrelIn) + " taken=" + N(g_wrelTaken) + " seedHeld=" + N(g_wrelSeedHeld)
        + " same=" + N(g_wrelSame) + " refused=" + N(g_wrelRefused) + " malformed=" + N(g_wrelMalformed) + " notAdmitted=" + N(g_wrelNotAdmitted)
        + " pushed=" + N(g_wrelPushed) + " writes=" + N(g_wrelWrites) + " worldRelWriteFailed=" + N(g_wrelWriteFailed)
        + " echoed=" + N(g_wrelEchoed) + " opSeeded=" + N((long long)g_wrelOpSeeded) + " seedHoldIn=" + N(g_wrelSeedHoldIn) + " seedHoldNow=" + N((long long)g_wrelHeld.size())
        + " seedHoldDropped=" + N(g_wrelSeedHoldDropped) + " seedFallbackFirstIn=" + N(g_wrelSeedFallback) + " seedHoldAfterOp=" + N(g_wrelSeedHoldAfterOp) + " standInRowsDropped=" + N(g_wrelStandInDropped));
}
/* loot2c (docs/design-loot2.md section 2 steps 3-5, 4A row "L2c' show/take"; T362): THE TAKE ROWS. research_takes.txt holds
   one line per (table key, row index, player id): that player took their own copy of that research row, so their game never
   shows it again. The research_boxes.txt shape: rewritten whole through a temp file, pushed in full at WELCOME (right after
   the research boxes), each NEW row broadcast to every game. First writer wins per row. A row must name the SENDER'S OWN
   player id (the id its HELLO carried): one game cannot record a take for another player. */
coopres::ResearchTakeTable g_takes;
int g_takesLoadDeferred = 0;
long long g_takesIn = 0, g_takesStored = 0, g_takesDup = 0, g_takesIdMismatch = 0, g_takesWriteFailed = 0, g_takesBadLines = 0, g_takesDeferredRecovered = 0;
std::string TakesFile() { return g_dir + "\\research_takes.txt"; }
bool WriteTakes()
{
    if (g_takesLoadDeferred) return false;   /* the file is on disk and unread: a rewrite from memory would erase its rows */
    const std::string tmp = TakesFile() + ".tmp";
    { std::ofstream f(tmp.c_str(), std::ios::trunc); if (!f) return false;
      for (coopres::ResearchTakeTable::const_iterator it = g_takes.begin(); it != g_takes.end(); ++it) f << coopres::ResearchTakeLine(it->second);
      if (!f) return false; }
    return MoveFileExA(tmp.c_str(), TakesFile().c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
long long TakesParseInto(const FileProbe& rp, coopres::ResearchTakeTable* into)
{
    if (rp.kind != coopstore::kFileReadable) return 0;
    std::istringstream f(TextOf(rp));
    std::string line; long long bad = 0;
    while (std::getline(f, line))
    {
        if (line.empty() || line == "\r") continue;
        coopres::ResearchTake r;
        if (coopres::ResearchTakeParseLine(line, &r) == 0) { ++bad; continue; }
        coopres::ResearchTakeMerge(into, r);
    }
    g_takesBadLines += bad;
    return bad;
}
void LoadTakes()
{
    const FileProbe rp = ProbeFile(TakesFile());
    if (rp.kind == coopstore::kFileUnreadable)
    {
        g_takesLoadDeferred = 1;
        Log("research: research_takes.txt is ON DISK and could NOT BE READ (error " + N((long long)rp.err)
            + ") - it is NOT rewritten until it opens; new take rows are kept in memory and broadcast, and the file is retried every second and merged");
        return;
    }
    g_takesLoadDeferred = 0;
    if (rp.kind != coopstore::kFileReadable) { Log("research: no research_takes.txt yet - no player has taken a research copy in this world"); return; }
    const long long bad = TakesParseInto(rp, &g_takes);
    Log("research: " + N((long long)g_takes.size()) + " take rows loaded from research_takes.txt (" + N(bad) + " unusable lines skipped)");
}
void SendTakesTo(ENetPeer* to, const std::vector<coopres::ResearchTake>& all)
{
    for (size_t at = 0; at < all.size(); at += 1024)
    {
        std::vector<coopres::ResearchTake> chunk(all.begin() + at, all.begin() + (at + 1024 < all.size() ? at + 1024 : all.size()));
        std::vector<char> m;
        if (coopres::EncodeResearchTakes(&m, chunk)) SendMsg(to, MSG_RESEARCH_TAKE, m);
    }
}
void SendTakesPush(ENetPeer* to)
{
    std::vector<coopres::ResearchTake> all;
    for (coopres::ResearchTakeTable::const_iterator it = g_takes.begin(); it != g_takes.end(); ++it) all.push_back(it->second);
    SendTakesTo(to, all);
}
/* the research_boxes.txt retry, for research_takes.txt: rows are a set, so the merge is a union */
void TakesDeferredRetry(ENetHost* host)
{
    if (!g_takesLoadDeferred) return;
    const FileProbe rp = ProbeFile(TakesFile());
    if (rp.kind == coopstore::kFileUnreadable) return;
    coopres::ResearchTakeTable onDisk;
    if (rp.kind == coopstore::kFileReadable) TakesParseInto(rp, &onDisk);
    std::vector<coopres::ResearchTake> diskOnly;
    for (coopres::ResearchTakeTable::const_iterator it = onDisk.begin(); it != onDisk.end(); ++it)
        if (g_takes.find(it->first) == g_takes.end()) diskOnly.push_back(it->second);
    for (coopres::ResearchTakeTable::const_iterator it = g_takes.begin(); it != g_takes.end(); ++it) onDisk[it->first] = it->second;
    g_takes = onDisk;
    g_takesLoadDeferred = 0;
    ++g_takesDeferredRecovered;
    const bool wrote = WriteTakes();
    if (!wrote) ++g_takesWriteFailed;
    Log(std::string(wrote ? "research: research_takes.txt OPENED - merged; " : "RESEARCH TAKES WRITE FAILED after the deferred load opened - ")
        + N((long long)diskOnly.size()) + " row(s) only on disk sent to the games; takeRows=" + N((long long)g_takes.size()));
    for (size_t p = 0; p < host->peerCount; ++p) { ENetPeer* q = &host->peers[p]; if (q->state == ENET_PEER_STATE_CONNECTED) SendTakesTo(q, diskOnly); }
}
void OnResearchTake(ENetHost* host, ENetPeer* from, const std::vector<char>& payload)
{
    std::vector<coopres::ResearchTake> in;
    const int why = coopres::DecodeResearchTakes(payload.empty() ? 0 : &payload[0], payload.size(), &in);
    if (why != coopres::kResearchDecodeOk) { Log("malformed RESEARCH_TAKE from " + PeerName(from) + " (reason " + N((long long)why) + ") - ignored"); return; }
    std::map<ENetPeer*, std::string>::const_iterator me = g_peerId.find(from);
    std::vector<coopres::ResearchTake> fresh;
    for (size_t i = 0; i < in.size(); ++i)
    {
        ++g_takesIn;
        if (me == g_peerId.end() || me->second != in[i].player)
        {
            ++g_takesIdMismatch;
            Log("RESEARCH TAKE from " + PeerName(from) + " names another player than the sender - refused (takesIdMismatch=" + N(g_takesIdMismatch) + ")");
            continue;
        }
        if (coopres::ResearchTakeMerge(&g_takes, in[i]) == 0) { ++g_takesDup; continue; }
        ++g_takesStored; fresh.push_back(in[i]);
        Log("RESEARCH TAKE " + SanitizeForLog(in[i].key) + " index=" + N((long long)in[i].index) + " by " + PeerName(from)
            + " - stored (takeRows=" + N((long long)g_takes.size()) + ")");
    }
    if (fresh.empty()) return;
    if (!WriteTakes())
    {
        ++g_takesWriteFailed;
        Log("RESEARCH TAKES WRITE FAILED: research_takes.txt could NOT be written" + std::string(g_takesLoadDeferred ? " (its load is deferred)" : "")
            + " - " + N((long long)fresh.size()) + " new row(s) stay in MEMORY ONLY and are broadcast (takesWriteFailed=" + N(g_takesWriteFailed) + ")");
    }
    for (size_t p = 0; p < host->peerCount; ++p) { ENetPeer* q = &host->peers[p]; if (q->state == ENET_PEER_STATE_CONNECTED) SendTakesTo(q, fresh); }
}
// E36 / decision 40: THE SESSION'S OPTION MAP. One line per option in options.txt, in the uniques.txt shape
// (`v1<TAB>key<TAB>value`, rewritten whole through a temp file + MoveFileExA), pushed in full at WELCOME and
// re-broadcast whenever it changes. It lives HERE and not on the host because decision 40's whole point is
// that a base stays usable - and therefore governed - after its owner has logged off.
//
// ONLY KEYS THIS FILE KNOWS ARE STORED. The wire shape is a map so the next option needs no protocol bump,
// but an unchecked key would let one malformed message put a permanent line in this world's options.txt.
std::map<std::string, std::string> g_options;
std::string OptionsFile() { return g_dir + "\\options.txt"; }
// The legal values for each key, and the value a world that has never been told gets. A game must never have to
// guess: the store ALWAYS has a basepolicy row after LoadOptions, so "no answer" on the game side means "no
// notebook process", not "the notebook is quiet".
bool OptionValueOk(const std::string& key, const std::string& value)
{
    if (key == "basepolicy") return value == "shared" || value == "owner" || value == "locked";
    /* E40 / decision 45 - THE PACE IS A HOSTING OPTION, and it lives here with the base policy for the same
       reason: it is a rule about the WORLD, so it has to outlive whichever game happens to be running. */
    if (key == "timemode") return value == "fixed" || value == "consensus";
    /* settings2 S2 (docs/design-settings1.md section 2): THE WORLD'S ADVANCED OPTIONS (gp.*) and, accepted now so S4
       needs no notebook rebuild, its world-changing gameplay-tab values (gt.*). The authority alone may set them
       (OnOptions refuses every other game before this is asked), and it may REPLACE them whenever its own values
       change (user 2026-09-26) - so these rows are not write-once. Range-checked here so one bad message cannot put
       a value into options.txt that a game would feed to the engine. */
    /* recruit3 (user decision 2026-09-26): bar hire lists x players, cap x4 - a HOST option, so the authority alone sets it (OnOptions). Absent = auto. */
    if (key == "recruitmult") return coopr::RecruitMultValueOk(value);
    /* loot2b (T361): a HOST option - on lifts research items out of opened boxes into research_boxes.txt; off (absent = off until L2c' per-player copies land) leaves them as world items. */
    if (key == "researchmode") return value == "on" || value == "off";
    /* prof1 (D5): a HOST option - how many active profiles one person may have in this world, 1-16 (absent = 3). Lowering it
       deletes nothing: NEW stays refused until the person is under it. */
    if (key == "profilecap") return coopprof::CapValueOk(value) != 0;
    if (key.compare(0, 3, "gp.") == 0) return coopgp::GpValueOk(key, value, 0);
    if (key.compare(0, 3, "gt.") == 0) return coopgp::GtValueOk(key, value);
    return false;
}
std::vector<char> EncodeOptions()
{
    std::vector<char> m; PutU32(&m, (unsigned)g_options.size());
    for (std::map<std::string, std::string>::const_iterator it = g_options.begin(); it != g_options.end(); ++it) { PutStr(&m, it->first); PutStr(&m, it->second); }
    return m;
}
// review-p6h MEDIUM-1. THE OPTION FILE IS THE OPTION, so a write that fails and is not acted on hands every
// game a rule that is obeyed now and gone at the next relay start: `basepolicy locked` accepted, broadcast and
// enforced, with options.txt still saying `shared`. store.cpp:190 names that as the one direction this option
// must never fail in. Three failures were silent here - the `return` on a failed open (which left the caller's
// already-mutated map to be broadcast anyway), a discarded MoveFileExA result, and a stream never checked
// after the writes, so a full disk wrote nothing and said nothing.
//
// The map to write is PASSED IN rather than read from g_options, because the caller has not committed it yet -
// that is the whole point: on a failure there is nothing to undo.
//
// GetLastError is captured on the line after the failing call and before anything else runs. Log(), and every
// std::string built on the way to it, can and does clobber the thread's last error.
bool WriteOptionsMap(const std::map<std::string, std::string>& opts, std::string* why)
{
    /* P8l (review-p8d H-1). THE SAME RULE AS uniques.txt, on the same shape. LoadOptions defers correctly and
       leaves g_options holding the two DEFAULTS and nothing else; this function rewrites options.txt whole,
       so the next accepted option change puts `basepolicy shared` / `timemode fixed` on disk over whatever
       this world's rules actually were. Bounded at one lost row today only because there are two keys. */
    if (g_optionsLoadDeferred)
    {
        ++g_optionsWriteRefusedDeferred;
        *why = "options.txt is on disk and could not be read this session, so it is NOT rewritten - the map"
               " in memory holds the defaults plus whatever has been accepted since, and writing that would"
               " replace this world's rules permanently. The change is kept and broadcast; the file is"
               " merged and written the moment it opens.";
        if (!g_optionsRefusalSaid) { g_optionsRefusalSaid = 1; Log("options: " + *why + " Said once; counted every time (optionsWriteRefusedDeferred)."); }
        return false;
    }
    const std::string file = OptionsFile();
    const std::string tmp = file + ".tmp";
    {
        std::ofstream f(tmp.c_str(), std::ios::trunc);
        if (!f)
        {
            const unsigned long e = ::GetLastError();
            *why = "could not open " + tmp + " for writing (GetLastError=" + N((long long)e) + ")";
            Log("options: " + *why); return false;
        }
        for (std::map<std::string, std::string>::const_iterator it = opts.begin(); it != opts.end(); ++it)
            f << "v1\t" << it->first << "\t" << it->second << "\n";
        f.flush();
        if (!f)
        {
            const unsigned long e = ::GetLastError();
            *why = "the write to " + tmp + " failed, so " + file + " is unchanged (disk full? GetLastError=" + N((long long)e) + ")";
            Log("options: " + *why); return false;
        }
    }
    if (!MoveFileExA(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        const unsigned long e = ::GetLastError();
        *why = "could not replace " + file + " with " + tmp + " (MoveFileExA, GetLastError=" + N((long long)e) + ")";
        Log("options: " + *why); return false;
    }
    return true;
}
// The LoadOptions caller. There is no sender to answer here, and no earlier value to keep: the in-memory
// default stands for this session either way, because a world still has to run under a policy it can name.
// What must NOT happen is silence, so the failure is stated in the terms that matter to whoever reads the log.
void WriteOptions()
{
    std::string why;
    if (!WriteOptionsMap(g_options, &why))
        Log("options: the default basepolicy row could NOT be persisted - this session runs on the in-memory map, and the next start will default again");
}
// review-p6h MEDIUM-1, the sender's half. NO PROTOCOL CHANGE: this is an ordinary MSG_OPTIONS map carrying one
// pair, sent to the one game that asked and to nobody else, under the RESERVED KEY `@refused` - a name
// OptionValueOk can never accept and no option can ever be given, so it is never stored here and never
// re-broadcast. A build that does not know the key already names it and skips it (that is what the OPTIONS
// handler on the game side does with any key it does not know), so an older plugin is not confused by it.
void SendOptionsRefusal(ENetPeer* to, const std::string& why)
{
    std::vector<char> m; PutU32(&m, 1u); PutStr(&m, "@refused"); PutStr(&m, SanitizeForLog(why));
    SendMsg(to, MSG_OPTIONS, m);
}
/* P8l: the ROWS out of already-probed bytes, so the load and the deferred-load retry cannot hold two ideas
   of what options.txt says (6a lesson 11). */
void OptionsParseInto(const FileProbe& op, std::map<std::string, std::string>* into, int* n, int* bad)
{
    if (op.kind != coopstore::kFileReadable) return;
    std::istringstream f(TextOf(op));
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        std::vector<std::string> fld; size_t at = 0;
        while (at <= line.size()) { size_t t = line.find('\t', at); if (t == std::string::npos) { fld.push_back(line.substr(at)); break; } fld.push_back(line.substr(at, t - at)); at = t + 1; }
        if (fld.size() < 3 || fld[0] != "v1" || fld[1].empty()) { ++(*bad); continue; }
        if (!OptionValueOk(fld[1], fld[2])) { ++(*bad); Log("options: dropping '" + SanitizeForLog(fld[1]) + "' = '" + SanitizeForLog(fld[2]) + "' - this build does not know that key/value"); continue; }
        (*into)[fld[1]] = fld[2]; ++(*n);
    }
}
void LoadOptions()
{
    const FileProbe op = ProbeFile(OptionsFile());
    int n = 0, bad = 0;
    /* P8d (review-p8c H-3). THE WORLD'S RULES ARE NOT REWRITTEN FROM A READ THAT FAILED. This function used
       an ifstream open, so options.txt held open for one second read as an EMPTY MAP - and the two defaults
       below were then applied AND WRITTEN BACK, so a world set to `locked` / `consensus` ran the session on
       `shared` / `fixed` and had that put on disk. The file survived the measured run only because the
       rename failed for the same reason the read did. The session still needs rules it can name, so the
       in-memory defaults stand; what does not happen is the WRITE. */
    if (op.kind == coopstore::kFileUnreadable)
    {
        ++g_optionsDeferredUnreadable;
        g_optionsLoadDeferred = 1; g_optionsRefusalSaid = 0;
        if (g_options.find("basepolicy") == g_options.end()) g_options["basepolicy"] = "shared";
        if (g_options.find("timemode") == g_options.end()) g_options["timemode"] = "fixed";
        Log("options: options.txt is ON DISK (" + N(op.attrLen) + " bytes) and could NOT BE READ (error "
            + N((long long)op.err) + "). This session runs on basepolicy=" + g_options["basepolicy"]
            + " timemode=" + g_options["timemode"]
            + " and NOTHING IS WRITTEN - rewriting the file from a failed read would replace this world's"
              " rules with the defaults permanently, from one moment's lock. The file is left exactly as"
              " it is. P8l (review-p8d H-1): THE LOAD IS DEFERRED, so every writer of this file REFUSES"
              " until it opens - an accepted option change is kept in memory and broadcast, options.txt is"
              " not touched, and the file is retried every second; when it opens the held changes are merged"
              " onto what it actually held and written once.");
        return;
    }
    g_optionsLoadDeferred = 0;
    if (op.kind == coopstore::kFileReadable) OptionsParseInto(op, &g_options, &n, &bad);
    // The default is written to disk, not merely assumed, so `options.txt` is always a complete statement of
    // what this world's rules are and a player can read it without knowing what the code defaults to.
    if (g_options.find("basepolicy") == g_options.end()) { g_options["basepolicy"] = "shared"; WriteOptions(); Log("options: no basepolicy row - defaulted to 'shared' and written to options.txt"); }
    /* E40 / decision 45: the same rule as basepolicy - the default is WRITTEN, not merely assumed, so options.txt
       is always a complete statement of this world's rules and a player can read it without knowing the code. */
    if (g_options.find("timemode") == g_options.end()) { g_options["timemode"] = "fixed"; WriteOptions(); Log("options: no timemode row - defaulted to 'fixed' (the host sets the pace) and written to options.txt"); }
    Log("options: " + N(n) + " read from options.txt (" + N(bad) + " unusable lines), basepolicy=" + g_options["basepolicy"]);
}
/* P8l (review-p8d H-1), the other half: THE RETRY. The rows the file actually held are the base; the keys the
   authority changed while the load was deferred are laid on top of them and nothing else is; the two defaults
   fill any gap. Then it is written ONCE and the merged map goes out to every game, because the map they are
   running on is the one this process broadcast while the file was shut. */
void OptionsDeferredRetry(ENetHost* host)
{
    if (!g_optionsLoadDeferred) return;
    const FileProbe op = ProbeFile(OptionsFile());
    if (op.kind == coopstore::kFileUnreadable) return;   /* still shut - look again next second */
    std::map<std::string, std::string> merged;
    int n = 0, bad = 0;
    OptionsParseInto(op, &merged, &n, &bad);
    const long long held = (long long)g_optionsDeferredChanges.size();
    for (std::map<std::string, std::string>::const_iterator it = g_optionsDeferredChanges.begin(); it != g_optionsDeferredChanges.end(); ++it)
        merged[it->first] = it->second;
    if (merged.find("basepolicy") == merged.end()) merged["basepolicy"] = "shared";
    if (merged.find("timemode") == merged.end()) merged["timemode"] = "fixed";
    g_optionsLoadDeferred = 0;
    std::string why;
    if (!WriteOptionsMap(merged, &why))
    {
        g_optionsLoadDeferred = 1;
        Log("options: options.txt opened and the " + N(held) + " held change(s) merged, but the merged map"
            " could NOT BE WRITTEN (" + why + ") - the load stays deferred and this is retried next second.");
        return;
    }
    ++g_optionsDeferredLoadRecovered;
    const int changed = (g_options != merged) ? 1 : 0;
    g_options = merged;
    g_optionsDeferredChanges.clear();
    Log("options: options.txt READ AT LAST - " + N(n) + " row(s) were on disk (" + N(bad) + " unusable) and "
        + N(held) + " change(s) had been held in memory. Merged, written once, and now basepolicy="
        + g_options["basepolicy"] + " timemode=" + g_options["timemode"]
        + ". deferredLoadRecovered(options)=" + N(g_optionsDeferredLoadRecovered)
        + " optionsWriteRefusedDeferred=" + N(g_optionsWriteRefusedDeferred));
    if (changed)
    {
        const std::vector<char> m = EncodeOptions();
        for (size_t i = 0; i < host->peerCount; ++i)
            if (host->peers[i].state == ENET_PEER_STATE_CONNECTED) SendMsg(&host->peers[i], MSG_OPTIONS, m);
    }
}

// ---- payload helpers (same layout as the plugin's session.cpp) ----
void PutU32(std::vector<char>* b, unsigned v) { size_t at = b->size(); b->resize(at + 4); memcpy(&(*b)[at], &v, 4); }
bool GetU32(const std::vector<char>& b, size_t at, unsigned* v) { if (b.size() < at + 4) return false; memcpy(v, &b[at], 4); return true; }
void PutStr(std::vector<char>* b, const std::string& s) { PutU32(b, (unsigned)s.size()); b->insert(b->end(), s.begin(), s.end()); }
/* review-p6h LOW-3, the relay half (P6m fixed the plugin's own copy, GetStrS; same shape as review-p4o LOW-1).
   With n == 0 at the very end of a payload, &b[*at + 4] is std::vector::operator[] AT size() - undefined, and a
   debug-CRT assert. An empty string is representable on the wire, and OPTIONS is a caller that can reach it. */
bool GetStr(const std::vector<char>& b, size_t* at, std::string* s) { unsigned n = 0; if (!GetU32(b, *at, &n) || b.size() < *at + 4 + n) return false; if (n == 0) s->clear(); else s->assign(&b[*at + 4], n); *at += 4 + n; return true; }

// ===================== E40 / decision 45 - THE WORLD'S CLOCK, KEPT AND ADVANCED HERE =====================
//
// WHY HERE AND NOT ON THE HOST. A clock read out of one game is hostage to that game's frame rate, its alt-tab
// stalls, its loading screens and its quit. This process already has the machinery: a for(;;) loop with a 50 ms
// enet_host_service timeout and a 1 Hz block driven by NowSec(). So it keeps three numbers - absolute in-game
// hours, the effective speed, and the wall clock at the last advance - and moves the first by the other two.
//
// ONE IN-GAME HOUR IS 109.0909 REAL SECONDS AT 1x. That is not a guess: GameWorld::getLengthOfHourInRealSeconds
// 0x66C960 returns the float at RVA 0x16FC454, whose value was read out of the binary, and the sky multiplier
// constant at 0x16FC7F8 is its exact reciprocal.
//
// WHAT HAPPENS WITH NOBODY CONNECTED: THE CLOCK STOPS. Stated rather than left to fall out of the arithmetic. A
// notebook left running overnight with no game attached would otherwise advance the world by twenty in-game days
// and put every stamped deadline in the world into the past at once (read-e40 2b), which is a thing to do on
// purpose or not at all. It is not done here.
const double kRealSecondsPerGameHour = 109.0909;
double g_clockHours    = -1.0;   // absolute in-game hours; NEGATIVE means "this world has no clock yet"
float  g_clockSpeed    = 1.0f;   // the EFFECTIVE speed this process is advancing at, and what every game is told
double g_clockLastWall = 0.0;    // NowSec() at the last advance
double g_clockLastWrite = 0.0;   // NowSec() at the last clock.txt write
long long g_clockBroadcasts = 0, g_voteMsgs = 0, g_voteRefused = 0, g_seedRefused = 0, g_weatherMsgs = 0, g_weatherRefused = 0;
// P7o, folding review-p7e H-3. THE RESTING SPEED AND THE ADVANCE GUARD ARE TWO DIFFERENT RULES, AND THEY HAD
// BEEN COLLAPSED INTO ONE. EffectiveSpeed returned 0.0 with nobody connected; that value was assigned to
// g_clockSpeed, PERSISTED to clock.txt, and put on the wire - including in the CLOCK that OnHello sends to a
// joining game before the next 1 Hz recompute. So the FIRST CLOCK a joining game ever saw said "this world is
// paused", for up to a second, and every idle notebook wrote `speed 0.0` into clock.txt for the next restart to
// read back. The intent the header states - "with nobody connected the clock stops" - is delivered by the
// ADVANCE guard and by nothing else, so the advance guard is now the honest "is anybody connected" test and the
// PUBLISHED speed is the last real pace this world ran at (1.0 until it has run at one).
float     g_clockLastRealSpeed = 1.0f;   // the last effective speed computed with at least one game connected
int       g_clockResting = 0;            // 1 = g_order is empty and this world's clock is being held
long long g_clockRestingTicks = 0;       // 1 Hz ticks spent resting - the clock did not advance for this many seconds, and the
                                         // line logged when a game connects again prints it (a number nobody prints is not a measurement)
std::map<ENetPeer*, float> g_votes;   // each connected game's LATEST speed setting; absent = has not voted yet

// ---------------------- P7j / E40 fold - THE SEED WINDOW, AND WHY THERE IS ONE ----------------------
// read-e40 2c is the whole reason this exists: a clock set BACKWARD parks every stamped deadline in a world -
// shops stop restocking, duration tasks never expire, campaigns, medical and hunger updates stall - and
// nothing fires and nothing recovers on its own. P7e seeded this world from the first game to present a clock
// and hard-set every other game to it in either direction; TEST-PLAN S7 spells out what that does with the two
// saves this project has, four weeks apart in real time.
//
// So the seed is not "the first clock" but "the LATEST clock among the first games to link", and the window is
// how long "the first games" lasts. It closes at whichever comes first of:
//   * the first NON-EMPTY area map having been delivered to every currently connected game - the moment the
//     world is live and moving anyone's clock would be moving a running world's deadlines. NON-EMPTY is doing
//     real work in that sentence: AreaTick broadcasts an empty map every second from relay start, so "any area
//     map" would shut this window about one second after the first link and the rule would never fire;
//   * 30 seconds after the FIRST link.
// After it closes a later game takes this clock as it stands - forward or backward - which is decision 45's
// price, and the log says so with the delta and the direction rather than leaving it to be inferred.
/* P7u: kSeedLate (3) is RETIRED - the window-closed case is no longer passive, it is the bounded
   play-time forward pull below, and a state nothing can reach is a label that lies. kSeedRestored (4) is
   new (review-p7j M-3): before it, a notebook restart made both games print seed=none for a world that had
   had a clock for hours, and none's own gloss reads "nothing has seeded this world yet". */
enum { kSeedNone = 0, kSeedFirst = 1, kSeedAdvanced = 2, kSeedRestored = 4 };
unsigned char g_clockSeedState = (unsigned char)kSeedNone;
double g_clockSeedWindowStart = 0.0;      // NowSec() at the FIRST link; 0 = nobody has linked yet
int    g_clockSeedWindowClosed = 0;
long long g_clockSeedAdvanced = 0;        // times a later game inside the window moved this world's clock FORWARD
// P7u, folding review-p7j M-4: clockLateJoinerSet counted TWO different events under one name - a genuine
// late join and a link bounce - and the window-closed case it named is no longer passive. Three events,
// three names, three different repairs:
long long g_clockPullPlayForward = 0;      // a game AHEAD of this world during play pulled this world FORWARD to it
long long g_clockPullIgnoredBehind = 0;    // a game BEHIND this world reported its clock; this world does not move for it
long long g_clockPullRefusedOverBound = 0; // a game more than 24 in-game HOURS ahead - refused and logged (review-p7j H-3)
/* P7w (F583 / review-p7u H-2). THREE NUMBERS, THREE DIFFERENT THINGS TO KNOW.
   clockPullRateLimited - passes that hit the per-pass bound and advanced BY it. A rising count means a gap
                          is being closed at kClockPullBoundMinutes a pass; a count that stops rising while
                          pullPasses does means it closed.
   pullPasses           - every pass that MOVED this world forward for a game, whatever the arm.
   pullMinutesTotal     - the in-game minutes those passes added, in total. The name carries its units
                          (6a lesson 1): minutes of GAME time, not of real time. */
long long g_clockPullRateLimited = 0;
long long g_clockPullPasses = 0;
double    g_clockPullMinutesTotal = 0.0;
/* P7w: the seed route, and it is the number T235 would have needed. seedFromOffer counts worlds seeded by
   a game's own CLOCK offer - the decision-42 path, where the link comes up at the title screen and the
   world loads afterwards - as opposed to a HELLO that already carried a clock. T235 read 0 offers of any
   kind because the offer sat below ClockRunPass's no-shared-clock return. */
long long g_clockSeedFromOffer = 0;
double    g_clockSeedWindowClosedAt = -1.0;   // seconds AFTER the first offer at which the window closed; < 0 = still open
// The 24-hour mismatch width, in in-game minutes. Above this a pull is REFUSED rather than rate-limited:
// the diagnosis there is two different saves sharing one notebook folder (review-p7e H-4, still owed),
// and closing that gap by converging would move a world by a day on a guess.
const double kClockMismatchBoundMinutes = 24.0 * 60.0;
double g_clockPullLogLastSec = 0.0;        // the throttle on the two lines above: they can arrive once a second, per game
double g_clockPullBoundLogLastSec = 0.0;
// THE BOUND ON THE PLAY-TIME PULL, in in-game minutes. Decision 46 says the shared clock only ever moves
// forward and that a game never writes its own clock backward while a world runs. Those two together leave
// one case with no closer: a game that is AHEAD of this world during play cannot slow itself down and this
// world advances at the same rate, so the gap never closes on its own. It is small in the ordinary case -
// the round-trip window after a speed press, and float slop - but it is NOT small after this file's own
// dt > 60 clamp fires on a suspended or descheduled machine, which leaves every game permanently ahead.
// 5 in-game minutes is inside the games' own dead band times five and far under the 24-hour mismatch bound.
const double kClockPullBoundMinutes = 5.0;
/* P7y - AMENDMENT 46b (user, 2026-09-05). TWO WIDTHS, AND BOTH BECOME HOSTING OPTIONS LATER; these are the
   defaults and they are named here so the next reader changes one number rather than hunting a literal.
     kClockSeedBoundMinutes    SEVEN in-game days. A seed or join offer that would move this world's clock
                               forward by more than this is REFUSED - in or out of the seed window - and
                               logged as a world mismatch. The offering game keeps its own clock. The
                               rationale the user gave: by construction no game can DRIFT this far from the
                               others (there is no single-player mode here and no relative speed), so a gap
                               this wide almost certainly means a bug, a tampered save, or cheating.
     kClockMismatchWarnMinutes ONE in-game hour. Any forward move above this is LOGGED as a warning EVEN
                               WHEN IT IS APPLIED, naming this world's clock, the offering game's clock and
                               the offering slot - for the same reason: a legitimate drift never gets here. */
const double kClockSeedBoundMinutes    = 7.0 * 24.0 * 60.0;
const double kClockMismatchWarnMinutes = 60.0;
long long g_clockSeedRefusedOverBound = 0;   /* offers refused by kClockSeedBoundMinutes - NOTHING was written */
long long g_clockMismatchWarned = 0;         /* forward moves that WERE applied and were wider than the warn width */
int ClockSeedWindowOpen() { return g_clockSeedWindowClosed ? 0 : 1; }
void ClockSeedWindowClose(const std::string& why)
{
    if (g_clockSeedWindowClosed) return;
    g_clockSeedWindowClosed = 1;
    g_clockSeedWindowClosedAt = (g_clockSeedWindowStart > 0.0) ? (NowSec() - g_clockSeedWindowStart) : -1.0;
    /* N and not F3: F3 is DEFINED BELOW THIS FUNCTION in this file, and a whole-seconds figure is enough
       for a window whose only bound is 30 s. */
    Log("clock: the seed window is CLOSED (" + why + ") - from here a linking game takes this world's clock in one jump, and the only thing that still moves this world is a connected game running AHEAD of it, rate-limited to " + N((long long)kClockPullBoundMinutes) + " in-game minutes per pass and refused only past 24 in-game hours (seedWindowClosedAt=" + N((long long)g_clockSeedWindowClosedAt) + " s after the first offer, seedFromOffer=" + N(g_clockSeedFromOffer) + ", clockSeedAdvanced=" + N(g_clockSeedAdvanced) + ", clockPullPlayForward=" + N(g_clockPullPlayForward) + ", clockPullRateLimited=" + N(g_clockPullRateLimited) + ", clockPullRefusedOverBound=" + N(g_clockPullRefusedOverBound) + ", clockSeedRefusedOverBound=" + N(g_clockSeedRefusedOverBound) + ", clockMismatchWarned=" + N(g_clockMismatchWarned) + ")");
}
/* P7w - THE WINDOW IS ANCHORED ON THE FIRST OFFER THAT CARRIES A REAL CLOCK, NOT ON THE FIRST HELLO.
   T235's measured cause, in the notebook's own log: under decision 42 both games link at the TITLE SCREEN,
   both HELLOs carry clock=-1.000 ("nothing offered"), and the window - anchored at that first link - closed
   before either save had finished loading. A HELLO-anchored window can therefore NEVER seed on the ordinary
   path. Anchoring on the first offer makes the 30 s run from the moment a game with a WORLD first speaks,
   which is what "the furthest of the FIRST PLAYERS" was always meant to mean. An unseeded world is not
   governed by this window at all - ClockPullDecide's !seeded arm takes the first offer whenever it comes. */
void ClockSeedWindowNoteOffer()
{
    if (g_clockSeedWindowStart > 0.0) return;
    g_clockSeedWindowStart = NowSec();
    Log("clock: the seed window is OPEN (the first offer carrying a real clock has arrived) - for the next 30 s, a game whose clock is AHEAD of this world's moves this world's clock FORWARD to it, so this world ends up at the furthest of the first players' times. After that, a later game takes this world's clock in one jump and an ahead game is pulled forward at a bounded rate.");
}
std::map<std::string, std::vector<char> > g_weather;   // P7s: the region's FCS stringID -> the authority's last MSG_WEATHER payload (it was a region INDEX, which F553 showed means a different region on each game)
const size_t kWeatherRegionsMax = 4096;   // a bound on distinct names one authority can make this process hold
std::string ClockFile() { return g_dir + "\\clock.txt"; }
std::string TimeMode() { std::map<std::string, std::string>::const_iterator it = g_options.find("timemode"); return it == g_options.end() ? std::string("fixed") : it->second; }
unsigned char TimeModeByte() { return TimeMode() == "consensus" ? (unsigned char)1 : (unsigned char)0; }
std::string F3(double v) { char b[64]; sprintf(b, "%.3f", v); return b; }
// THE EFFECTIVE SPEED - decision 45's two modes, and nothing else decides the pace.
//   fixed     - the authority game's own setting (the front of the connection order, the same game OPTIONS and
//               WEATHER are accepted from). A host that has not touched the speed yet reads as 1x, which is what
//               the game defaults to anyway.
//   consensus - the MINIMUM over every connected game's latest vote, pause included, so the slowest player sets
//               the pace. A game that has never voted is not counted: a joiner must not be able to drag a 5x
//               session down to 1x merely by connecting.
float EffectiveSpeed()
{
    /* P7u: the body has MOVED to src/common/clockmath.cpp as EffectiveSpeedFrom, which takes the order and the
       votes as arguments and reads no global at all. NO BEHAVIOUR CHANGE - the map is keyed by pointer identity
       either way, and the copy is over at most eight peers once a second. What it buys is that review-p7e H-3's
       failing input is now one line in an offline test: EffectiveSpeedFrom({}, {}, 0, 2.0f) must return 2.0f.
       Before P7o it returned 0.0f, and that value was written into clock.txt and put on the wire in the CLOCK a
       joining game is handed - so the first CLOCK a joining game ever saw said the world was paused. */
    /* B13 / decision 49: `fixed` means THE AUTHORITY GAME'S OWN SETTING, and the authority is now the
       operator rather than the front of the connection order. EffectiveSpeedFrom reads the FRONT of the list
       it is handed, so the operator is put there; clockmath.cpp itself is unchanged (it is shared with the
       plugin and the offline suite and knows nothing about players). With no operator connected the order is
       what it always was. */
    std::vector<void*> order;
    order.reserve(g_order.size());
    for (size_t i = 0; i < g_order.size(); ++i) if (PeerIsAuthority(g_order[i])) order.push_back((void*)g_order[i]);
    for (size_t i = 0; i < g_order.size(); ++i) if (!PeerIsAuthority(g_order[i])) order.push_back((void*)g_order[i]);
    std::map<void*, float> votes;
    for (std::map<ENetPeer*, float>::const_iterator it = g_votes.begin(); it != g_votes.end(); ++it)
        votes[(void*)it->first] = it->second;
    return coopclock::EffectiveSpeedFrom(order, votes, TimeMode() == "consensus" ? 1 : 0, g_clockLastRealSpeed);
}
// clock.txt, in the uniques.txt / options.txt shape (`v1<TAB>...`, rewritten whole through a temp file and a
// MoveFileExA rename). THE MODE FIELD IS WRITTEN FOR A READER AND IGNORED ON LOAD: options.txt is the one
// authority for `timemode`, because that is the file the verb path writes, and two files that can disagree about
// the same rule is the failure this note exists to prevent.
bool WriteClock()
{
    /* P8l (review-p8d H-2). A deferred load leaves g_clockHours = -1; the first game to link then seeds this
       world's time from its own save and ClockTick writes that over clock.txt five seconds later. One
       second's lock at start therefore costs the world's recorded time. Nothing is written while the load
       is deferred; the clock still runs in memory, and the file is merged and written when it opens. */
    if (g_clockLoadDeferred)
    {
        ++g_clockWriteRefusedDeferred;
        if (!g_clockRefusalSaid)
        {
            g_clockRefusalSaid = 1;
            Log("clock: clock.txt is NOT WRITTEN while its load is deferred - this world's recorded time is"
                " on disk and could not be read, and writing now would replace it with whatever the first"
                " game to link happened to hold. The clock runs in memory, the file is read again every"
                " second, and the later of the two times is written when it opens. Said once; counted every"
                " time (clockWriteRefusedDeferred).");
        }
        return false;
    }
    const std::string tmp = ClockFile() + ".tmp";
    { std::ofstream f(tmp.c_str(), std::ios::trunc); if (!f) { Log("clock: could not open " + tmp + " for writing - this world's time is not being persisted"); return false; }
      /* P7u (review-p7j M-3): v2 adds the SEED STATE as a fifth field. The mode field is still written for a
         reader and still ignored on load - options.txt is the one authority for timemode. */
      f << "v2\t" << F3(g_clockHours) << "\t" << F1(g_clockSpeed) << "\t" << TimeMode() << "\t" << N((long long)(int)g_clockSeedState) << "\n";
      f.flush(); if (!f) { Log("clock: the write to " + tmp + " failed - " + ClockFile() + " is unchanged"); return false; } }
    if (!MoveFileExA(tmp.c_str(), ClockFile().c_str(), MOVEFILE_REPLACE_EXISTING))
    { Log("clock: could not replace " + ClockFile() + " (MoveFileExA, GetLastError=" + N((long long)::GetLastError()) + ")"); return false; }
    return true;
}
/* P8l: THE FIELDS of a clock.txt line, out of already-probed bytes and with no side effect, so the load and
   the deferred-load retry cannot hold two ideas of what that file says (6a lesson 11). Returns 0 - and says
   why - when the file carries no usable line. The speed comes back AS WRITTEN, 0 included: what 0 means is
   the adopting caller's decision and is stated there. */
int ClockFieldsFrom(const FileProbe& cp, double* hours, float* speed, int* wasState, std::string* why)
{
    std::string line;
    if (!FirstLineOf(cp, &line)) { *why = "clock.txt is empty - waiting for a game to seed this world's time"; return 0; }
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    std::vector<std::string> fld; size_t at = 0;
    while (at <= line.size()) { size_t t = line.find('\t', at); if (t == std::string::npos) { fld.push_back(line.substr(at)); break; } fld.push_back(line.substr(at, t - at)); at = t + 1; }
    if (fld.size() < 3 || !(fld[0] == "v1" || fld[0] == "v2")) { *why = "clock.txt is not a v1 or v2 line - ignored, and this world waits to be seeded"; return 0; }
    const double h = atof(fld[1].c_str());
    if (!(h >= 0.0 && h < 100000000.0)) { *why = "clock.txt holds an implausible time (" + SanitizeForLog(fld[1]) + ") - ignored"; return 0; }
    *hours = h;
    float sp = (float)atof(fld[2].c_str());
    if (!(sp == 0.0f || sp == 1.0f || sp == 2.0f || sp == 5.0f)) sp = 1.0f;
    *speed = sp;
    *wasState = (fld.size() >= 5) ? atoi(fld[4].c_str()) : -1;
    return 1;
}
/* P8l: ADOPTING the file's time, in one place, so the start-up load and the deferred-load retry cannot
   drift. `whence` names which of the two it was, because the second one happens mid-session and a reader of
   the log has to be able to tell a restart from a recovery. */
void ClockAdoptFileFields(double h, float sp, int wasState, const std::string& whence)
{
    g_clockHours = h; g_clockLastWall = NowSec(); g_clockSpeed = sp;
    /* P7o (review-p7e H-3): a clock.txt written by a build BEFORE this one recorded speed 0.0 for every idle
       notebook, because the resting effective speed was 0. Restoring that verbatim would hand the first game to
       link a paused world it never asked for, so 0 from the file is read as 1x and said once. A world genuinely
       paused by its players is a live vote, not a file: it is restored the moment those games link and vote. */
    if (g_clockSpeed == 0.0f)
    {
        g_clockSpeed = 1.0f;
        /* P7u, folding review-p7o M-1. The line that stood here said a 0 in this file could only have come
           from a pre-P7o idle notebook and NOT from a world its players had paused - and THIS build can
           still write it: two connected games both voting 0 makes the effective speed 0, which is assigned
           to g_clockLastRealSpeed, and the resting publish then persists that 0 five seconds after they
           disconnect. The behaviour is right either way and the message now says why. */
        Log("clock: clock.txt recorded speed 0 - either the last games connected were paused, or this is an idle pre-P7o notebook. Either way a pause is a LIVE VOTE and not a file, so this is read as 1x and the pause comes back the moment those games link and vote again.");
    }
    g_clockLastRealSpeed = g_clockSpeed;
    /* P7u (review-p7j M-3): the world HAS a clock now, and saying seed=none for it is a number that reads as
       one thing and reports another. The state this world was in when it was written is logged for context;
       what it IS now is "restored". */
    g_clockSeedState = (unsigned char)kSeedRestored;
    Log("clock: this world's seed state is RESTORED (the clock came back out of clock.txt); the seed event that produced it was recorded as "
        + (wasState < 0 ? std::string("(a v1 file, which did not carry one)") : N((long long)wasState)));
    Log("clock: restored from clock.txt (" + whence + ") - day " + N((long long)(h / 24.0)) + " hour " + N((long long)(h - (double)(long long)(h / 24.0) * 24.0)) + " (" + F3(h) + " absolute in-game hours), speed " + F1(g_clockSpeed) + ", timemode " + TimeMode());
}
void LoadClock()
{
    const FileProbe cp = ProbeFile(ClockFile());
    if (cp.kind == coopstore::kFileUnreadable)
    {
        g_clockLoadDeferred = 1; g_clockRefusalSaid = 0;
        Log("clock: clock.txt is ON DISK (" + N(cp.attrLen) + " bytes) and could NOT BE READ (error " + N((long long)cp.err) + ") - this world's time is NOT restored this session and nothing is written over the file. P8l (review-p8d H-2): THE LOAD IS DEFERRED, so WriteClock refuses until the file opens - otherwise the first game to link seeds this world's time from its own save and the 5 s clock write puts that over the world's recorded time. The file is retried every second.");
        return;
    }
    g_clockLoadDeferred = 0;
    if (cp.kind == coopstore::kFileAbsent) { Log("clock: no clock.txt - this world has no time yet; the first game to link seeds it from the save it has just loaded"); return; }
    double h = -1.0; float sp = 1.0f; int wasState = -1; std::string why;
    if (!ClockFieldsFrom(cp, &h, &sp, &wasState, &why)) { Log("clock: " + why); return; }
    ClockAdoptFileFields(h, sp, wasState, "at start");
}
/* P8l (review-p8d H-2), the other half: THE RETRY. Two times can exist by the time the file opens - the one
   the file held all along and the one a game seeded into memory - and decision 46's rule decides between
   them: this world's clock only ever moves FORWARD, so the LATER of the two stands and the earlier is said
   rather than silently dropped. Then it is written once. */
void ClockDeferredRetry()
{
    if (!g_clockLoadDeferred) return;
    const FileProbe cp = ProbeFile(ClockFile());
    if (cp.kind == coopstore::kFileUnreadable) return;   /* still shut - look again next second */
    double fileHours = -1.0; float sp = 1.0f; int wasState = -1; std::string why;
    const int parsed = (cp.kind == coopstore::kFileReadable) ? ClockFieldsFrom(cp, &fileHours, &sp, &wasState, &why) : 0;
    if (cp.kind == coopstore::kFileAbsent) why = "clock.txt is not there after all";
    const double mem = g_clockHours;
    g_clockLoadDeferred = 0;
    ++g_clockDeferredLoadRecovered;
    const double merged = coopmerge::ClockMergeHours(parsed ? fileHours : -1.0, mem);
    if (parsed && merged == fileHours && !(mem >= 0.0 && mem >= fileHours))
    {
        Log("clock: clock.txt READ AT LAST and its time is the later one - this world's recorded time ("
            + F3(fileHours) + " absolute in-game hours) is adopted over the "
            + (mem >= 0.0 ? std::string("time a game had seeded in the meantime (" + F3(mem) + ")") : std::string("nothing this session had"))
            + ". deferredLoadRecovered(clock)=" + N(g_clockDeferredLoadRecovered)
            + " clockWriteRefusedDeferred=" + N(g_clockWriteRefusedDeferred));
        ClockAdoptFileFields(fileHours, sp, wasState, "on the deferred-load retry");
    }
    else
    {
        Log("clock: clock.txt READ AT LAST - " + (parsed
                ? std::string("it holds " + F3(fileHours) + " absolute in-game hours, which is not later than the "
                              + F3(mem) + " this session is already running, so the running clock stands (decision 46: this world's clock only moves forward)")
                : std::string("it carries no usable time (" + why + "), so the running clock stands"))
            + ". deferredLoadRecovered(clock)=" + N(g_clockDeferredLoadRecovered)
            + " clockWriteRefusedDeferred=" + N(g_clockWriteRefusedDeferred));
    }
    if (g_clockHours >= 0.0) WriteClock();
}
/* P8l: THE ONE 1 Hz RETRY for all three deferred loads, so the tick's call list cannot say "two of the
   three" (6a lesson 11). Each of these is a no-op unless its own file's load is outstanding. */
void DeferredLoadTick(ENetHost* host)
{
    UniquesDeferredRetry(host);
    OptionsDeferredRetry(host);
    ClockDeferredRetry();
    ResearchDeferredRetry(host);   /* loot2b fold 2 (review-loot2b 3b): research_boxes.txt, the same retry and merge */
    TakesDeferredRetry(host);      /* loot2c: research_takes.txt, the same retry (a union) */
}
std::vector<char> EncodeClock()
{
    /* P7j: 14 bytes, not 13 - the trailing seedState says which of the four seed cases this world is in, so a
       readout can tell "the first game gave this world its time" from "a late joiner took it" without having to
       reconstruct either from timestamps. */
    std::vector<char> m; m.resize(14);
    const double h = g_clockHours; const float sp = g_clockSpeed; const unsigned char md = TimeModeByte();
    memcpy(&m[0], &h, 8); memcpy(&m[8], &sp, 4); m[12] = (char)md; m[13] = (char)g_clockSeedState;
    return m;
}
// ---------------------------------------------------------------------------------------------------
// P7u - THE ONE PLACE A GAME'S OWN CLOCK IS JUDGED. THREE routes reach it now and they are one event, so they
// cannot drift apart (6a lesson 11): the HELLO-carried clock (a game that links with a world already up), the
// PRE-JUMP offer (a game that linked at the title screen and then loaded a world, which under decision 42 is
// the ordinary case) and the POST-JUMP report (every second, from a game that has adopted this world's clock).
// The decision itself is ClockPullDecide in src/common/clockmath.cpp: pure, and tested offline.
//
// THE RULE, AND IT ONLY EVER MOVES THIS WORLD'S CLOCK FORWARD (decision 46):
//   no clock yet            -> seed it, whatever the window says. A world with no time is worse than any other
//                              outcome here, and this arm is what gives an empty notebook folder a clock.
//   behind or level         -> ignored. That game takes this world's clock in its own join jump.
//   window open, ahead      -> move this world's clock FORWARD to it: the furthest of the first games to link.
//   during play, ahead      -> move this world's clock FORWARD to it, bounded at kClockPullBoundMinutes. This is
//                              the ONLY thing that closes an ahead gap: the game will not write its own clock
//                              backward while a world is running, so the world has to come forward to meet it.
//   ahead past the bound    -> refused and logged. Nothing is written (review-p7j H-3).
/* P7y (amendment 46b). ONE WARNING, ONE DEFINITION, called from every arm that APPLIES a forward move, so
   the three arms cannot be kept in step by hand and drift apart (lesson 11). It decides nothing and it never
   refuses: the refusal is kPullSeedOverBound, and this is the line that fires when a move this wide was
   nevertheless the right thing to do. BOTH SIDES ARE NAMED - this world's clock before the move and the
   offering game's own - because "the clock jumped" without the two numbers cannot be told apart from "the
   clock was already wrong". */
void ClockWarnIfWideForwardMove(ENetPeer* from, const char* route, const char* arm,
                                double wasHours, double toHours, double fromGameHours, double deltaMinutes)
{
    if (!(deltaMinutes > kClockMismatchWarnMinutes)) return;
    ++g_clockMismatchWarned;
    Log(std::string("clockMismatchWarned (") + arm + ", " + route + ") from " + PeerName(from)
        + " slot=" + N((long long)SlotOf(from))
        + ": this world's clock moved FORWARD by " + F3(deltaMinutes) + " in-game minutes, which is wider than the "
        + F3(kClockMismatchWarnMinutes) + "-minute warning width - THE MOVE WAS APPLIED. thisWorldWas="
        + F3(wasHours) + " thisWorldNow=" + F3(toHours) + " thatGameSaid=" + F3(fromGameHours)
        + " (absolute in-game hours). Amendment 46b: no game can DRIFT this far from the others - there is no"
          " single-player mode and no relative speed - so a gap this wide is most likely a bug, a tampered"
          " save, or two different saves sharing this notebook folder. Anything wider than "
        + F3(kClockSeedBoundMinutes) + " in-game minutes is refused outright as clockSeedRefusedOverBound."
          " The running counts are clockMismatchWarned=" + N(g_clockMismatchWarned)
        + " clockSeedRefusedOverBound=" + N(g_clockSeedRefusedOverBound));
}
void ClockConsiderGameClock(ENetPeer* from, double h, const char* route)
{
    if (!(h >= 0.0 && h < 100000000.0))
    {
        /* A game with no world loaded sends a negative number on purpose - that is not a malformed message and
           it is not refused noisily; it is a game saying "I have no clock to offer yet". */
        if (h < 0.0) { Log(std::string("clock: ") + route + " from " + PeerName(from) + " carries no clock (this game has no world loaded yet) - nothing offered"); return; }
        ++g_seedRefused; Log(std::string("clock: ") + route + " REFUSED from " + PeerName(from) + " - " + F3(h) + " is not a plausible number of in-game hours"); return;
    }
    /* P7w: THE WINDOW OPENS HERE, on the first offer that carries a real clock, and not at the first
       HELLO. Below the plausibility test on purpose - a negative "I have no world yet" is not an offer. */
    ClockSeedWindowNoteOffer();
    {
        const double was = g_clockHours;
        const double deltaMinutes = (was >= 0.0) ? (h - was) * 60.0 : 0.0;
        double ns = 0.0;
        const int k = coopclock::ClockPullDecide(was, (was >= 0.0) ? 1 : 0, ClockSeedWindowOpen(), h,
                                                 kClockPullBoundMinutes, kClockMismatchBoundMinutes,
                                                 kClockSeedBoundMinutes, &ns);
        if (k == coopclock::kPullSeed)
        {
            g_clockHours = ns; g_clockLastWall = NowSec(); g_clockSeedState = (unsigned char)kSeedFirst; WriteClock();
            /* P7w: which ROUTE seeded it. A HELLO-carried clock means a game that linked with a world
               already up; an offer means the decision-42 path - link at the title, load, then offer -
               which is the route T235 proved was unreachable. */
            if (route != 0 && route[0] == 'C') ++g_clockSeedFromOffer;
            Log(std::string("clock SEEDED (") + route + ") from " + PeerName(from) + ": day " + N((long long)(ns / 24.0)) + ", " + F3(ns) + " absolute in-game hours - written to clock.txt, so this world keeps its time through every restart (seedFromOffer=" + N(g_clockSeedFromOffer) + ")");
            return;
        }
        if (k == coopclock::kPullWindowAdvance)
        {
            g_clockHours = ns; g_clockLastWall = NowSec(); g_clockSeedState = (unsigned char)kSeedAdvanced;
            ++g_clockSeedAdvanced; WriteClock();
            ClockWarnIfWideForwardMove(from, route, "clockSeedAdvanced", was, ns, h, deltaMinutes);
            Log(std::string("clockSeedAdvanced (") + route + ") from " + PeerName(from) + ": this world's clock moved FORWARD from " + F3(was) + " to " + F3(ns) + " absolute in-game hours (+" + F3(deltaMinutes) + " in-game minutes) because this game linked inside the seed window and its clock is the later one. Forward is the only direction a seed ever moves - read-e40 2c: a backward set parks every deadline in the world");
            return;
        }
        if (k == coopclock::kPullPlayForward)
        {
            /* g_clockLastWall is restamped so the 1 Hz accumulator cannot double-count what this pull just
               added. clock.txt is NOT written here: this arm can fire once a second per game and the 5 s
               persistence in ClockTick already covers it, at a cost of 2.7 in-game minutes on a restart. */
            g_clockHours = ns; g_clockLastWall = NowSec();
            ++g_clockPullPlayForward;
            ++g_clockPullPasses; g_clockPullMinutesTotal += (ns - was) * 60.0;
            ClockWarnIfWideForwardMove(from, route, "clockPullPlayForward", was, ns, h, deltaMinutes);
            if (NowSec() - g_clockPullLogLastSec >= 30.0)
            {
                g_clockPullLogLastSec = NowSec();
                Log(std::string("clockPullPlayForward (") + route + ") from " + PeerName(from) + ": that game is AHEAD of this world by " + F3(deltaMinutes) + " in-game minutes, so this world's clock moved FORWARD from " + F3(was) + " to " + F3(ns) + " to meet it (bound " + F3(kClockPullBoundMinutes) + " in-game minutes). A game never writes its OWN clock backward while a world is running - decision 46 - so this is the only thing that closes an ahead gap. Logged at most once per 30 s; the running count is " + N(g_clockPullPlayForward));
            }
            return;
        }
        if (k == coopclock::kPullRateLimited)
        {
            /* P7w (F583). BOUNDED PER PASS, UNBOUNDED IN TOTAL. The old code refused here and the gap
               stood open for the life of the session, because decision 46 also forbids that game from
               writing its own clock down. This world now walks forward by the bound every pass until it
               meets the game - the same per-write bound, a converging total. */
            g_clockHours = ns; g_clockLastWall = NowSec();
            ++g_clockPullRateLimited;
            ++g_clockPullPasses; g_clockPullMinutesTotal += (ns - was) * 60.0;
            ClockWarnIfWideForwardMove(from, route, "clockPullRateLimited", was, ns, h, deltaMinutes);
            if (NowSec() - g_clockPullBoundLogLastSec >= 30.0)
            {
                g_clockPullBoundLogLastSec = NowSec();
                Log(std::string("clockPullRateLimited (") + route + ") from " + PeerName(from) + ": that game is " + F3(deltaMinutes) + " in-game minutes AHEAD of this world, past the " + F3(kClockPullBoundMinutes) + "-minute PER-PASS bound, so this world moved forward BY the bound - from " + F3(was) + " to " + F3(ns) + " - and will do so again next pass until the gap closes. A game never writes its OWN clock backward while a world is running (decision 46), so this is the only thing that closes an ahead gap; before P7w this case was refused outright and nothing converged. Logged at most once per 30 s; clockPullRateLimited=" + N(g_clockPullRateLimited) + " pullPasses=" + N(g_clockPullPasses) + " pullMinutesTotal=" + F3(g_clockPullMinutesTotal));
            }
            return;
        }
        if (k == coopclock::kPullSeedOverBound)
        {
            /* P7y - AMENDMENT 46b's REFUSAL. Nothing is written: g_clockHours is untouched, clock.txt is
               untouched, and the offering game keeps its own clock, which is what the amendment says should
               happen. It is deliberately NOT under the 30 s throttle the two bound lines share - an offer
               this wide is not a per-second condition, it is a diagnosis, and one line per occurrence is
               what a readout needs. */
            ++g_clockSeedRefusedOverBound;
            Log(std::string("clockSeedRefusedOverBound (") + route + ") from " + PeerName(from)
                + " slot=" + N((long long)SlotOf(from))
                + ": that game offered a clock " + F3(deltaMinutes) + " in-game minutes AHEAD of this world -"
                  " past the " + F3(kClockSeedBoundMinutes) + "-minute (seven in-game day) SEED bound - so"
                  " NOTHING was written and this world stays at " + F3(was) + " while that game keeps "
                + F3(h) + " (absolute in-game hours). Amendment 46b: no game can drift seven days from the"
                  " others, so the likely causes are two different saves sharing this notebook folder, a"
                  " tampered save, or a bug. THIS BOUND APPLIES INSIDE THE SEED WINDOW TOO - before P7y the"
                  " window suspended every width test and a day-300 save could drag a day-1 world forward"
                  " 299 days on the first offer pair (F612). The running count is clockSeedRefusedOverBound="
                + N(g_clockSeedRefusedOverBound));
            return;
        }
        if (k == coopclock::kPullOverBound)
        {
            ++g_clockPullRefusedOverBound;
            if (NowSec() - g_clockPullBoundLogLastSec >= 30.0)
            {
                g_clockPullBoundLogLastSec = NowSec();
                Log(std::string("clockPullRefusedOverBound (") + route + ") from " + PeerName(from) + ": that game is " + F3(deltaMinutes) + " in-game minutes AHEAD of this world, past the 24-in-game-hour MISMATCH width, so NOTHING was written (review-p7j H-3). This world stays at " + F3(was) + " and that game stays where it is - it will not step itself back. A gap this size is not drift; the most likely cause is two different saves sharing one notebook folder, which this notebook still has no way to refuse (review-p7e H-4, owed). Anything smaller than this is now rate-limited forward instead of refused - P7w / F583. Logged at most once per 30 s; the running count is " + N(g_clockPullRefusedOverBound));
            }
            return;
        }
        /* kPullIgnore: that game is behind this world, or level with it. This is the ORDINARY case, once a
           second per game, and it is deliberately silent - a line here would bury every line that matters. */
        ++g_clockPullIgnoredBehind;
    }
}
/* P7o (review-p7e H-3): THE ONE PLACE THE EFFECTIVE SPEED IS RECOMPUTED AND ANNOUNCED. Two callers need it -
   the 1 Hz tick and OnHello, which must recompute BEFORE it encodes the CLOCK it hands a joining game, or that
   game is told the pace this world had before it joined. Written once and called twice rather than kept in step
   by hand (6a lesson 11). g_clockLastRealSpeed is updated only while at least one game is connected, which is
   what makes the resting published speed the last REAL one rather than a restatement of the resting rule. */
void ClockRecomputeSpeed(const char* why)
{
    const float eff = EffectiveSpeed();
    if (!g_order.empty()) g_clockLastRealSpeed = eff;
    if (eff == g_clockSpeed) return;
    Log("clock: effective speed " + F1(g_clockSpeed) + " -> " + F1(eff) + " (" + std::string(why) + ", timemode " + TimeMode() + ", "
        + N((long long)g_votes.size()) + " vote(s) from " + N((long long)g_order.size()) + " game(s))");
    g_clockSpeed = eff;
}
void ClockTick(ENetHost* host)
{
    const double now = NowSec();
    if (g_clockLastWall <= 0.0) g_clockLastWall = now;
    const double dt = now - g_clockLastWall;
    g_clockLastWall = now;
    /* dt is CLAMPED at 60 s on purpose. A suspended or descheduled machine must not hand this world an hour of
       game time in one step: a forward jump fires every deadline in the world at once and dailyUpdates fires
       exactly ONCE however many days are skipped (read-e40 2b), so a big jump is a lossy operation, not a
       catch-up. Time the notebook did not observe is time this world did not have. */
    /* P7o (review-p7e H-3): `!g_order.empty()` is the guard that stops the clock with nobody connected. It used
       to be `g_clockSpeed > 0.0f` doing that job as a side effect of EffectiveSpeed publishing 0 while resting -
       which is why the published speed could not be fixed without moving this test to the thing it is actually
       about. A world genuinely paused by its players (every vote 0) still does not advance, because that reads
       g_clockSpeed, which is still tested here. */
    if (g_clockHours >= 0.0 && dt > 0.0 && dt < 60.0 && g_clockSpeed > 0.0f && !g_order.empty())
        g_clockHours += dt * (double)g_clockSpeed / kRealSecondsPerGameHour;
    ClockRecomputeSpeed("1 Hz tick");
    {
        const int resting = g_order.empty() ? 1 : 0;
        if (resting) ++g_clockRestingTicks;
        if (resting != g_clockResting)
        {
            g_clockResting = resting;
            Log(resting
                ? std::string("clock: nobody is connected - this world's clock is HELD at " + F3(g_clockHours) + " absolute in-game hours and does not advance. The PUBLISHED speed stays at " + F1(g_clockSpeed) + " (P7o / review-p7e H-3: publishing 0 here told the next game to link that the world was paused, and wrote speed 0.0 into clock.txt)")
                : std::string("clock: a game is connected again - this world's clock advances from " + F3(g_clockHours) + " absolute in-game hours (it was held for " + N(g_clockRestingTicks) + " tick(s) in total this session)"));
        }
    }
    /* P7w: THE ONLY CLOSER. 30 seconds from the FIRST OFFER - a world that nobody has offered a clock to
       has not started its window at all, so an unseeded notebook cannot run out of window before any save
       has loaded, which is exactly what T235 measured. */
    if (ClockSeedWindowOpen() && g_clockSeedWindowStart > 0.0 && now - g_clockSeedWindowStart >= 30.0)
        ClockSeedWindowClose("30 s since the first offer carrying a real clock");
    const std::vector<char> m = EncodeClock();
    for (size_t i = 0; i < host->peerCount; ++i) { ENetPeer* p = &host->peers[i]; if (p->state == ENET_PEER_STATE_CONNECTED) SendMsg(p, MSG_CLOCK, m); }
    ++g_clockBroadcasts;
    /* clock.txt every 5 s rather than every second: the file is the guard against a relay restart losing this
       world's time, and 5 s of loss is 2.7 in-game minutes - inside the games' own dead band, so a restart costs
       nothing a player could see. */
    if (g_clockHours >= 0.0 && now - g_clockLastWrite >= 5.0) { g_clockLastWrite = now; WriteClock(); }
}
// A game reporting its OWN clock. P7u: this is no longer only a seed offer. A game sends its clock ONCE
// before its join jump (the offer the seed window reads) and then ONCE A SECOND after the jump has landed
// (the play-time report the bounded forward pull reads) - review-p7j H-2, whose whole point is that the old
// offer could only fire while this world had no clock at all and was therefore unreachable on the ordinary
// decision-42 path. All three routes go to one decision function.
void OnGameClock(ENetPeer* from, const std::vector<char>& payload)
{
    if (payload.size() < 8) { ++g_seedRefused; Log("malformed CLOCK from " + PeerName(from)); return; }
    { double h = 0.0; memcpy(&h, &payload[0], 8); ClockConsiderGameClock(from, h, "CLOCK report"); }
}
void OnSpeedVote(ENetHost* host, ENetPeer* from, const std::vector<char>& payload)
{
    if (payload.size() < 4) { ++g_voteRefused; Log("malformed SPEEDVOTE from " + PeerName(from)); return; }
    float v = 0.0f; memcpy(&v, &payload[0], 4);
    if (!coopclock::ClockSpeedIsValid(v)) { ++g_voteRefused; Log("SPEEDVOTE " + F1(v) + " REFUSED from " + PeerName(from) + " - the game's own buttons set 0, 1, 2 or 5 and nothing else"); return; }   /* P7u: the shared predicate, one place instead of four */
    ++g_voteMsgs;
    const float was = g_clockSpeed;
    g_votes[from] = v;
    Log("SPEEDVOTE " + F1(v) + " from " + PeerName(from) + " (slot " + N(SlotOf(from)) + ", timemode " + TimeMode() + ", authority=" + (PeerIsAuthority(from) ? "1" : "0") + ")");
    /* Applied and broadcast NOW rather than on the next second. A pause the player has to wait a second for
       reads as a dropped keypress, and the tick is idempotent - it advances by the real time that has actually
       elapsed, which is a fraction of a second here. */
    ClockTick(host);
    (void)was;
}
// WEATHER, from the authority game only. Weather has ONE owner in both time modes: two games each rolling their
// own rand() produce two different skies from the same clock, which is the whole reason this is state and not a
// deadline (read-e40 3).
void OnWeather(ENetHost* host, ENetPeer* from, const std::vector<char>& payload)
{
    const bool auth = PeerIsAuthority(from);   /* B13 / decision 49: the OPERATOR, out of owner.txt - not whoever dialled first */
    if (!auth) { ++g_weatherRefused; Log("WEATHER REFUSED from " + PeerName(from) + " - that game is not the session authority, and weather has one owner"); return; }
    /* P7s (F553): the region is NAMED, by its FCS stringID. One decoder, shared with the game and the offline suite;
       a protocol-43 (index-keyed) payload lands in its size-mismatch arm and is refused, never read as a name. */
    coopwx::WxWire wx;
    const int dr = coopwx::DecodeWeather(payload.empty() ? 0 : &payload[0], payload.size(), &wx);
    if (dr != coopwx::kWxDecodeOk) { ++g_weatherRefused; Log("malformed WEATHER from " + PeerName(from) + " (" + N((long long)payload.size()) + " bytes: " + coopwx::WeatherDecodeText(dr) + ")"); return; }
    const std::string region(wx.name, wx.nameLen);
    std::map<std::string, std::vector<char> >::iterator at = g_weather.find(region);
    if (at == g_weather.end() && g_weather.size() >= kWeatherRegionsMax) { ++g_weatherRefused; Log("WEATHER REFUSED from " + PeerName(from) + " - region '" + SanitizeForLog(region) + "' would be distinct region " + N((long long)g_weather.size() + 1) + ", over the bound of " + N((long long)kWeatherRegionsMax)); return; }
    const bool changed = at == g_weather.end() || at->second != payload;
    g_weather[region] = payload;
    ++g_weatherMsgs;
    if (changed)
        Log("WEATHER region '" + SanitizeForLog(region) + "': season " + N((long long)wx.seasonIdx) + " (ends day " + N((long long)wx.endDay) + "), weather " + N((long long)(int)wx.weatherIdx) + " - from the authority " + PeerName(from));
    std::vector<char> buf(5 + payload.size()); buf[0] = (char)MSG_WEATHER; unsigned int n = (unsigned int)payload.size(); memcpy(&buf[1], &n, 4); memcpy(&buf[5], &payload[0], payload.size());
    for (size_t i = 0; i < host->peerCount; ++i) { ENetPeer* p = &host->peers[i]; if (p->state == ENET_PEER_STATE_CONNECTED && p != from) SbSendRel(p, buf); }
}

/* M6 (T-197 piece 6; protocol 60) - PER-AREA DELIVERY AND THE CATCH-UP (owner decision 57; src/common/liverelay.h).
   Each game's AREAS report (about once a second; the ENGINE'S OWN loaded set, zones.cpp - never a guessed distance) is kept
   here per PERMANENT SLOT: `loaded` as reported and `delivery` = loaded widened by one ring (cooplive::kAreaMargin). A LIVE
   with route AREA names a sector (and the sector its subject has just left) and goes to every OTHER admitted game whose
   delivery set holds either. A report older than kGraceSec is no report. Keyed by slot in a map: no 32-slot width (the
   AREAMAP's 32-bit loadedMask is not read here).
   THE CATCH-UP: a report that puts sectors into a game's delivery set that were not in it (a walk, a load, a teleport, a
   join, this process restarting) sends CATCHUP ASK {requester slot, ask number, sectors} to every other admitted game whose
   LOADED set holds any of them (listing only those), and CATCHUP ASKED {owners asked, ask number, sectors} to the requester.
   Each asked game answers the requester directly with LIVE SLOT - the state of what it owns there (M7a) and last
   CATCHUP_END {ask number, count}. Nothing here grows past one row per connected slot. */
struct LiveAreaSet { std::set<int> loaded, delivery; std::map<int, double> stamps; double at; LiveAreaSet() : at(-1.0) {} };   /* fold 1: stamps = sector -> the last report that had it in the delivery set (cooplive::AreaReportNewlyIn) */
std::map<int, LiveAreaSet> g_liveAreas;   // key = slot
unsigned int g_catchupAskNo = 0;
long long g_playerGoneSent = 0, g_playerGoneTold = 0, g_playerGoneLoadedDropped = 0, g_playerGoneSendFailed = 0, g_playerGoneHeld = 0, g_playerGoneCancelledReturned = 0;   /* M8: PLAYER_GONE rounds, games told, (area, slot) loaded marks dropped at the send, sends refused; M8 review F1: closes held, holds cancelled because that player was admitted again */
struct PgHold { double at; std::string name, how; PgHold() : at(0.0) {} };
std::map<int, PgHold> g_playerGoneHolds;   /* M8 review F1: key = the closed player's permanent slot - at most one hold per slot */
long long g_liveAreaIn = 0, g_liveAreaFwd = 0, g_liveAreaNone = 0;
long long g_catchupReports = 0, g_catchupRounds = 0, g_catchupNewSectors = 0, g_catchupAsks = 0, g_catchupNoOwner = 0, g_catchupSendFailed = 0, g_catchupEmptyReports = 0;   /* fold 1: sendFailed = an encode refused OR SendMsg refused; emptyReports = AREAS with no sector (a game left its world) */
long long g_catchupOwnerNotInWorld = 0, g_liveAreaMissed = 0, g_liveAreaMissedRepeat = 0;   /* fold 1 item 7 [m7a2f-sm0]: a repeating kind's miss - the stamp KEPT */   /* M7a2 [m7a2-sm1]: item 4 - world-road owners not asked (not in the world, their answer would be refused); item 8 - stamps marked missed */
const std::set<int>* LiveAreaDeliveryOf(int slot, double now)
{
    std::map<int, LiveAreaSet>::const_iterator it = g_liveAreas.find(slot);
    if (it == g_liveAreas.end() || it->second.at < 0.0 || now - it->second.at > kGraceSec) return 0;
    return &it->second.delivery;
}
void LiveAreasReport(ENetPeer* from, int slot, const std::vector<char>& payload, unsigned int n, double now)
{
    if (slot < 0 || g_peerId.count(from) == 0) return;   /* a lobby connection holds no slot and is never a destination */
    ++g_catchupReports;
    std::set<int> loaded;
    for (unsigned int i = 0; i < n; ++i)
    {
        int x = 0, y = 0; memcpy(&x, &payload[4 + (size_t)i * 8], 4); memcpy(&y, &payload[8 + (size_t)i * 8], 4);
        const int k = cooplive::AreaKey(x, y); if (k >= 0) loaded.insert(k);
    }
    if (loaded.empty()) ++g_catchupEmptyReports;   /* fold 1: the game left its world (or its loaded set is down) - its sets are empty from now */
    LiveAreaSet& me = g_liveAreas[slot];
    std::vector<int> fresh;
    /* fold 1 (review of 49bac485, MED 1): newly delivered = out of the delivery set longer than cooplive::kCatchupRejoinSec (the
       stamps), not merely absent from the previous report - an edge flicker no longer asks for a round a second. */
    cooplive::AreaReportNewlyIn(&me.stamps, loaded, cooplive::kAreaMargin, now, cooplive::kCatchupRejoinSec, &me.delivery, &fresh);
    me.loaded.swap(loaded); me.at = now;
    if (fresh.empty()) return;
    ++g_catchupRounds; g_catchupNewSectors += (long long)fresh.size();
    const unsigned int askNo = ++g_catchupAskNo;
    unsigned int owners = 0;
    std::vector<char> areaMap, playerSecs; bool mapBuilt = false;   /* M7a fold 4 (T760 V1): built once a round, for the asked games only */
    for (std::map<ENetPeer*, int>::const_iterator it = g_slot.begin(); it != g_slot.end(); ++it)
    {
        ENetPeer* p = it->first;
        if (p == from || p->state != ENET_PEER_STATE_CONNECTED || g_peerId.count(p) == 0) continue;
        std::map<int, LiveAreaSet>::const_iterator o = g_liveAreas.find(it->second);
        if (o == g_liveAreas.end() || o->second.at < 0.0 || now - o->second.at > kGraceSec) continue;
        std::vector<int> keys; cooplive::AreaIntersect(fresh, o->second.loaded, &keys);
        if (keys.empty()) continue;
        if (JoinRoadOf(p) == (int)coopjoin::kRoadWorld && !coopjoin::LiveDestAllowed(StageOf(p))) { ++g_catchupOwnerNotInWorld; continue; }   /* M7a2 item 4 [m7a2-sm2]: OnLive would refuse its answer; its own IN_WORLD reports catch it up both ways */
        std::vector<char> b;
        if (!cooplive::CatchupEncode(&b, cooplive::kCatchupAsk, (unsigned int)slot, askNo, keys)) { ++g_catchupSendFailed; continue; }
        /* M7a fold 4 (T760 V1) - THE MAP GOES AHEAD OF THE ASK. The owner decides each character by its area map ("another game holds
           this sector"), and the asker's marks reach it only in the next once-a-second broadcast; an ask that beat it found nobody
           holding the sectors and kept every character, and its CATCHUP_END came before the announce pass's SPAWNs. The map and its
           player sectors go to this owner first, on the same reliable channel 0 as the ask, so it applies them before the ask
           (AreaMapBuild; nothing on the wire changes). Not while --stop-areamaps-after has stopped the maps. */
        if (!AreaMapsStopped(now))
        {
            if (!mapBuilt) { AreaMapBuild(now, &areaMap); PlayerSectorsBuild(now, &playerSecs); mapBuilt = true; }
            SendMsg(p, MSG_AREAMAP, areaMap); SendMsg(p, MSG_PLAYERSECTORS, playerSecs);   /* a refused send leaves the ask's own send to fail and be counted */
        }
        if (!SendMsg(p, MSG_CATCHUP, b)) { ++g_catchupSendFailed; continue; }   /* fold 1: SendMsg answers */
        ++owners; ++g_catchupAsks;
    }
    if (owners == 0) ++g_catchupNoOwner;
    std::vector<char> t;
    if (!cooplive::CatchupEncode(&t, cooplive::kCatchupAsked, owners, askNo, fresh) || !SendMsg(from, MSG_CATCHUP, t)) ++g_catchupSendFailed;
    if (owners != 0 || cooplive::LiveLogThis(g_catchupNoOwner))
        Log("CATCHUP #" + N((long long)askNo) + ": slot " + N((long long)slot) + " (" + PeerName(from) + ") newly has " + N((long long)fresh.size())
            + " sectors in its delivery area (first " + N((long long)cooplive::AreaKeyX(fresh[0])) + "," + N((long long)cooplive::AreaKeyY(fresh[0])) + ") - "
            + N((long long)owners) + " other games asked to re-send what they own there" + (owners == 0 ? " (catchup noOwner " + N(g_catchupNoOwner) + "; the first 5 and every 100th are logged)" : std::string()));
}

/* M5a (T-197 piece 4; protocol 59) - LIVE (53): THE NOTEBOOK CARRIES LIVE GAME-TO-GAME MESSAGES TO ANY NUMBER OF GAMES.
   A game sends {route, target, inner type, inner}; this process STAMPS THE SENDER with the PERMANENT slot it gave that
   player at HELLO (AssignSlotFor / slots.txt, B13 - manager decision 2026-09-30: the player's number, which M5b keys
   ownership on, not the connection's uid seat) and forwards {origin slot, inner type, inner} on channel 0, reliable
   (channel 1 is kept for M7a's MOVE). WORLD = every other admitted game IN THE WORLD; SLOT = the one admitted game in the world holding
   that slot; AREA (M6, protocol 60) = every other admitted game in the world with the named sector in its delivery area (LiveAreasReport
   above). M11a S1 (protocol 61): a game at the title or loading is never a destination (coopjoin::LiveDestAllowed), and a WORLD-ROAD game there is
   not a sender either (S1 fold, review M1: a session-road game's first RELSYNC, sent on its world's first frame, can beat its first AREAS,
   so a session-road sender is not judged by its stage).
   A connection waiting in the lobby holds no slot and is neither a sender nor a destination. The inner message is not
   read here - this is an envelope, and the receiving game decides which inner types it takes. Every LIVE lands in
   exactly one of relayed / noSlot / badRoute / badLen / noTarget / alone / areaNone / notInWorld (in = their sum; M6; M11a liveStage[fromNotInWorld]); fwd counts the frames sent.
   No table grows: one pass over the connected games per message. */
long long g_liveIn = 0, g_liveRelayed = 0, g_liveFwd = 0, g_liveNoSlot = 0, g_liveBadRoute = 0, g_liveBadLen = 0, g_liveNoTarget = 0, g_liveAlone = 0, g_liveSendFailed = 0;
long long g_liveBytesIn = 0, g_liveBytesOut = 0;
bool g_liveFirstLogged = false;
void LiveRefused(long long count, ENetPeer* from, const std::string& why)
{
    if (cooplive::LiveLogThis(count)) Log("LIVE REFUSED from " + PeerName(from) + " - " + why + " (" + N(count) + " for this reason so far; the first 5 and every 100th are logged)");
}
/* M16 (T-197) - A BOUND ON WHAT WAITS TO BE SENT TO EACH GAME (src/common/sendbound.h holds the rule). Before M16 every
   frame went straight into ENet's queue for that game, which has no limit. Now, per connected game: while its ENet queue
   is under coopsb::kNetCountMax packets / kNetBytesMax bytes, a reliable frame goes straight out while nothing waits (as
   before) and an unreliable relayed MOVE or STATE goes straight out past whatever waits (it promises no order);
   otherwise it WAITS here and SbDrainAll hands it over as the queue drains. A relayed LIVE MOVE or STATE keeps
   only the newest waiting value per {inner type, origin slot, character}; every other frame waits and is never dropped.
   The first congestion of a link and every 100th are logged, and each doubling of what waits from 1024. A game whose
   connection ends takes its waiting frames with it (ENet drops its own queue the same way) - counted. LOOP THREAD ONLY. */
struct SbPeer
{
    coopsb::Outbox box; unsigned int connectId; double at; long long netCount, netBytes;
    long long episode; bool logged;   /* M16 fold 4: this game's own congestion episode, and whether its CONGESTED line was logged */
    coopbundle::Collector col; bool bundle;   /* M13: this game's open bundle per lane, and whether it is sent bundles (after its WELCOME) */
    SbPeer() : connectId(0), at(-1.0), netCount(0), netBytes(0), episode(0), logged(false), bundle(false) {}
};
std::map<ENetPeer*, SbPeer> g_sb;
long long g_sbDirect = 0, g_sbWaited = 0, g_sbCoalesced = 0, g_sbEpisodes = 0, g_sbDropped = 0, g_sbSendFailed = 0, g_sbWaitMax = 0;
/* M13: bundle[...] on the REPORT line (BundleToken) - every ENet packet sent to a game, what the collectors did, bundles ENet
   refused, bundles received (and their messages, and those refused whole), games bundling was turned on for. */
coopbundle::Tally g_bnTally;
long long g_bnPackets = 0, g_bnSendFailed = 0, g_bnRecvBundles = 0, g_bnRecvBundled = 0, g_bnRecvBad = 0, g_bnLinksOn = 0, g_bnUdpPackets = 0, g_bnUdpBytes = 0;
const double kSbMeasureSec = 0.010;   /* ENet's queue is walked at most every 10 ms per game; frames sent meanwhile are added */
/* This game's ENet queue: packets and bytes not yet handed to the wire (both outgoing lists; a fragment counts as one). */
SbPeer& SbOf(ENetPeer* p)
{
    SbPeer& s = g_sb[p];
    if (s.connectId != p->connectID)
    {
        const long long n = s.box.Clear() + s.col.Clear(); s.box.Edge();   /* M13: an open bundle was waiting to go too */
        g_sbDropped += n;
        s.connectId = p->connectID; s.at = -1.0; s.logged = false; s.bundle = false;
    }
    const double now = NowSec();
    if (s.at < 0.0 || now - s.at >= kSbMeasureSec)
    {
        long long c = 0, b = 0;
        for (ENetListIterator i = enet_list_begin(&p->outgoingCommands); i != enet_list_end(&p->outgoingCommands); i = enet_list_next(i)) { ++c; b += (long long)((ENetOutgoingCommand*)i)->fragmentLength; }
        for (ENetListIterator i = enet_list_begin(&p->outgoingSendReliableCommands); i != enet_list_end(&p->outgoingSendReliableCommands); i = enet_list_next(i)) { ++c; b += (long long)((ENetOutgoingCommand*)i)->fragmentLength; }
        s.netCount = c + (long long)s.col.OpenPackets(); s.netBytes = b + (long long)s.col.OpenBytes(); s.at = now;   /* M13: and the open bundles it is about to get */
    }
    return s;
}
/* M16 fold 3 (T775): the game's two channels (net::Channel, src/coop-plugin/net/transport.h) - 0 reliable, 1 unreliable and
   unsequenced (a latest-wins MOVE or STATE). A LIVE goes on with the channel it ARRIVED on; everything else is reliable. */
enum { kChReliable = 0, kChUnreliable = 1 };
enet_uint32 SbFlagsOf(int ch) { return ch == kChUnreliable ? (enet_uint32)ENET_PACKET_FLAG_UNSEQUENCED : (enet_uint32)ENET_PACKET_FLAG_RELIABLE; }
bool SbSendRaw(ENetPeer* p, const char* data, size_t n, int ch)
{
    ENetPacket* pkt = enet_packet_create(data, n, SbFlagsOf(ch));
    if (pkt == 0) return false;
    if (enet_peer_send(p, (enet_uint8)(ch == kChUnreliable ? kChUnreliable : kChReliable), pkt) != 0) { if (pkt->referenceCount == 0) enet_packet_destroy(pkt); return false; }
    ++g_bnPackets;   /* M13 */
    return true;
}
/* M13: a sealed bundle (or the one message an open bundle held) to ENet; a refusal loses every message in it - counted
   bundle[sendFailed] and logged (the first 5 and every 100th). */
void SbBundleSend(ENetPeer* p, const std::vector<char>& frame, size_t entries, int lane)
{
    const int ch = lane == coopbundle::kLaneReliable ? kChReliable : kChUnreliable;
    if (!frame.empty() && SbSendRaw(p, &frame[0], frame.size(), ch)) return;
    g_bnSendFailed += (long long)entries;
    if (cooplive::LiveLogThis(g_bnSendFailed))
        Log("M13: ENet REFUSED a bundle of " + N((long long)entries) + " message(s), " + N((long long)frame.size()) + " bytes, on the "
            + std::string(lane == coopbundle::kLaneReliable ? "guaranteed" : "unreliable") + " lane for " + PeerName(p) + " - they are lost (counted bundle[sendFailed])");
}
/* M13: one lane's open bundle for this game to ENet now. */
void SbSealLane(ENetPeer* p, SbPeer& s, int lane)
{
    std::vector<char> f;
    const size_t n = s.col.Seal(lane, &f, &g_bnTally);
    if (n != 0) SbBundleSend(p, f, n, lane);
}
/* M13: ONE WHOLE FRAME TOWARD THE NETWORK for a connected game - where M16's bound hands a frame over. While the game is sent
   bundles it joins its lane's open bundle, which leaves when SbSealAll runs right before ENet transmits (once per pass of the
   loop); a bundle with no room left, and a frame too large to share a packet, go to ENet at once - the open bundle first, so
   the guaranteed lane keeps its order. Otherwise the frame goes to ENet as its own packet, as before. The bound's view of the
   game's queue (netCount / netBytes) moves with it. false = ENet refused the frame itself. */
bool SbToNet(ENetPeer* p, SbPeer& s, const char* data, size_t n, int ch)
{
    if (s.bundle)
    {
        const size_t openBefore = s.col.OpenPackets();
        std::vector<char> first; size_t firstEntries = 0;
        const int lane = coopbundle::LaneOf(ch);
        const int r = s.col.Offer(lane, data, n, coopbundle::Budget(p->mtu, p->host != 0 && p->host->checksum != 0), &first, &firstEntries, &g_bnTally);
        if (firstEntries != 0) SbBundleSend(p, first, firstEntries, lane);
        if (r == coopbundle::kTaken)
        {
            if (s.col.OpenPackets() > openBefore || firstEntries != 0) ++s.netCount;
            s.netBytes += (long long)n;
            return true;
        }
    }
    if (!SbSendRaw(p, data, n, ch)) return false;
    ++s.netCount; s.netBytes += (long long)n;
    return true;
}
/* M13: every game's open bundles to ENet - called right before ENet transmits (LoopService), so each game gets what this pass
   produced for it as one packet per lane. */
void SbSealAll()
{
    for (std::map<ENetPeer*, SbPeer>::iterator it = g_sb.begin(); it != g_sb.end(); ++it)
    {
        if (it->second.col.OpenEntries() == 0) continue;
        if (it->first->state != ENET_PEER_STATE_CONNECTED || it->first->connectID != it->second.connectId) continue;   /* SbDrainAll throws these away, counted */
        for (int lane = 0; lane < coopbundle::kLanes; ++lane) SbSealLane(it->first, it->second, lane);
    }
}
/* M13: from here on this game is sent bundles - it has been sent a WELCOME of this protocol, so it reads them. */
void SbBundleOn(ENetPeer* p)
{
    SbPeer& s = SbOf(p);
    if (s.bundle) return;
    s.bundle = true;
    if (cooplive::LiveLogThis(++g_bnLinksOn))
        Log("M13: " + PeerName(p) + " is now sent the messages of each pass as one packet per lane - bundles of at most "
            + N((long long)coopbundle::Budget(p->mtu, p->host != 0 && p->host->checksum != 0)) + " bytes (ENet's one-command limit at this link's MTU of "
            + N((long long)p->mtu) + "); larger messages and the handshake go alone (game " + N(g_bnLinksOn) + "; the first 5 and every 100th are logged)");
}
void SbEdgeLog(ENetPeer* p, SbPeer& s)
{
    const int e = s.box.Edge();
    if (e > 0) { s.episode = ++g_sbEpisodes; s.logged = cooplive::LiveLogThis(s.episode) != 0; }   /* M16 fold 4: per game */
    if (e > 0 && s.logged)
        Log("M16: the link to " + PeerName(p) + " is CONGESTED - " + N(s.netCount) + " packet(s) / " + N(s.netBytes) + " bytes wait in the network layer (full at "
            + N(coopsb::kNetCountMax) + " / " + N(coopsb::kNetBytesMax) + "). Reliable frames WAIT here in order; an unreliable relayed MOVE or STATE passes them and waits only while"
            " the network layer is full, newest per character; nothing reliable is dropped (episode " + N(g_sbEpisodes) + "; the first 5 and every 100th are logged; sendBound on the REPORT line)");
    else if (e < 0 && s.logged)   /* M16 fold 4: this game's own episode was logged, whatever the server-wide count is now */
        Log("M16: the link to " + PeerName(p) + " is no longer congested - every waiting frame has gone to the network (episode " + N(s.episode) + ")");
    if (s.box.GrowthMark())
        Log("M16: " + N(s.box.Count()) + " frame(s), " + N(s.box.Bytes()) + " bytes WAIT for " + PeerName(p) + " and it is still growing - none is dropped;"
            " if that game never drains its link, the link times out as before");
}
/* false = not sent and not waiting (no packet, or ENet refused it) - SendMsg's answer, unchanged. */
bool SbSend(ENetPeer* p, const std::vector<char>& buf, bool live, int ch)
{
    if (buf.size() < 5) return false;
    if (p->state != ENET_PEER_STATE_CONNECTED) return SbSendRaw(p, &buf[0], buf.size(), ch);   /* ENet refuses it, as before */
    SbPeer& s = SbOf(p);
    coopsb::Item it; it.type = (unsigned char)buf[0]; it.channel = ch;
    it.cls = live ? coopsb::ClassOf(it.type, &buf[5], buf.size() - 5, coopsb::kDirDown, &it.key) : (int)coopsb::kClassMust;
    bool superseded = false;
    if (s.box.Direct(s.netCount, s.netBytes, it, &superseded))   /* an unreliable frame passes what waits - sendbound.h */
    {
        if (superseded) ++g_sbCoalesced;   /* an older waiting value of this character was removed: this one replaces it */
        if (!SbToNet(p, s, &buf[0], buf.size(), ch)) return false;   /* M13: into this game's open bundle, or straight to ENet */
        ++g_sbDirect;
        return true;
    }
    it.bytes = buf;
    if (s.box.Put(it)) ++g_sbCoalesced;
    ++g_sbWaited;
    if (s.box.Count() > g_sbWaitMax) g_sbWaitMax = s.box.Count();
    SbEdgeLog(p, s);
    return true;
}
bool SbSendRel(ENetPeer* p, const std::vector<char>& buf) { return SbSend(p, buf, false, kChReliable); }
/* M16 fold 4: a waiting frame ENet refused - counted sendFailed; a must-deliver one is also logged (the first 5 and every 100th). */
long long g_sbMustRefused = 0;
void SbRefused(ENetPeer* p, const coopsb::Item& x)
{
    ++g_sbSendFailed;
    if (x.cls == coopsb::kClassMust && cooplive::LiveLogThis(++g_sbMustRefused))
        Log("M16: ENet REFUSED a waiting must-deliver frame (type " + N((long long)x.type) + ", " + N((long long)x.bytes.size()) + " bytes) for " + PeerName(p)
            + " - it is lost (refusal " + N(g_sbMustRefused) + "; the first 5 and every 100th are logged; counted sendBound[sendFailed])");
}
/* M16 fold 1: SbDrainAll throws away what waits for a game as soon as it is no longer CONNECTED, so a refusal or repair
   frame sent just before a disconnect-later would be lost. Waiting frames go to ENet FIRST, regardless of the bound
   (ENet delivers everything queued before it disconnects), as the game side's Disconnect does. Every disconnect-later
   of a game goes through here. */
void SbDisconnectLater(ENetPeer* p)
{
    std::map<ENetPeer*, SbPeer>::iterator it = g_sb.find(p);
    if (it != g_sb.end() && it->second.connectId == p->connectID && p->state == ENET_PEER_STATE_CONNECTED)
    {
        coopsb::Item x;
        while (it->second.box.TakeAny(&x)) { if (x.bytes.empty() || !SbToNet(p, it->second, &x.bytes[0], x.bytes.size(), x.channel)) SbRefused(p, x); }
        for (int lane = 0; lane < coopbundle::kLanes; ++lane) SbSealLane(p, it->second, lane);   /* M13: the open bundles go before the disconnect too */
        SbEdgeLog(p, it->second);
    }
    enet_peer_disconnect_later(p, 0);
}
/* Each pass of the loop: what waits goes to the network while each game's queue has room; a game whose connection ended
   (or was replaced in its slot) loses what waited for it, counted. */
void SbDrainAll()
{
    std::map<ENetPeer*, SbPeer>::iterator it = g_sb.begin();
    while (it != g_sb.end())
    {
        ENetPeer* p = it->first; SbPeer& s = it->second;
        if (p->state != ENET_PEER_STATE_CONNECTED || p->connectID != s.connectId)
        {
            const long long n = s.box.Clear() + s.col.Clear();   /* M13: an open bundle was waiting to go too */
            if (n > 0)
            {
                g_sbDropped += n;
                Log("M16: " + N(n) + " frame(s) waiting for " + PeerName(p) + " were thrown away with its connection (ENet drops its own queue the same way); counted sendBound[droppedAtClose]");
            }
            g_sb.erase(it++);
            continue;
        }
        if (!s.box.Empty())
        {
            SbOf(p);
            coopsb::Item x;
            while (s.box.Take(s.netCount, s.netBytes, &x))
            {
                if (x.bytes.empty() || !SbToNet(p, s, &x.bytes[0], x.bytes.size(), x.channel)) { SbRefused(p, x); continue; }   /* M16 fold 4: logged, capped; M13: SbToNet moves netCount / netBytes */
            }
        }
        SbEdgeLog(p, s);
        ++it;
    }
}
typedef char M16LiveNumberAgrees[(MSG_LIVE == (int)coopsb::kTypeLive) ? 1 : -1];

/* M7a2 item 8 [m7a2-sm3] - THE 'MISSED' STAMP: an AREA message not forwarded to a candidate game whose delivery set lacks its sector
   now but held it within the rejoin window erases that sector's stamp there (cooplive::AreaStampMissed), so its return is caught up. */
void LiveAreaMarkMissed(unsigned int target, const std::vector<int>& slots, const std::vector<size_t>& dests, unsigned int innerType)   /* fold 1 item 7 [m7a2f-sm1] */
{
    const bool oneOff = cooplive::AreaMissedIsOneOff(innerType);   /* SPAWN, DESPAWN, UNLOAD, SWING, COMBATMODE, CONTEXT, APPEARANCE, CLOTHING, TASK; never MOVE / STATE / INTENT */
    int key = -1, prev = -1;
    if (!cooplive::AreaTargetDecode(target, &key, &prev)) return;
    std::vector<char> isDest(slots.size(), 0);
    for (size_t i = 0; i < dests.size(); ++i) if (dests[i] < isDest.size()) isDest[dests[i]] = 1;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (isDest[i] != 0) continue;
        std::map<int, LiveAreaSet>::iterator it = g_liveAreas.find(slots[i]);
        if (it == g_liveAreas.end()) continue;
        if (!oneOff) { if (it->second.stamps.count(key) != 0 && it->second.delivery.count(key) == 0) ++g_liveAreaMissedRepeat; continue; }   /* [m7a2f-sm2] the next one replaces it */
        if (cooplive::AreaStampMissed(&it->second.stamps, &it->second.delivery, key)) ++g_liveAreaMissed;
    }
}
void OnLive(ENetPeer* from, const std::vector<char>& payload, int channel)   /* M16 fold 3: channel = the one it arrived on */
{
    const int ch = (channel == kChUnreliable) ? kChUnreliable : kChReliable;   /* forwarded exactly as reliably as it came */
    ++g_liveIn; g_liveBytesIn += (long long)payload.size();
    /* THE STAMP: the slot THIS PROCESS assigned, for an ADMITTED connection only. */
    const int origin = g_peerId.count(from) != 0 ? SlotOf(from) : -1;
    if (origin >= 0 && JoinRoadOf(from) == (int)coopjoin::kRoadWorld && !coopjoin::LiveDestAllowed(StageOf(from)))   /* M11a S1: a world-road game sends live messages only once in the world; S1 fold (review M1): the session road is not judged here - refusing its first RELSYNC (the proof copy) kept the pair on the direct link for the whole link */
    {
        ++g_liveNotInWorldFrom;
        LiveRefused(g_liveNotInWorldFrom, from, std::string("that world-road game is at stage ") + coopjoin::StageName(StageOf(from)) + " - a world-road game sends live messages only once it is in the world, and only a game in the world receives them (M11a)");
        return;
    }
    cooplive::LiveUp up;
    const int dr = cooplive::LiveUpDecode(payload.empty() ? 0 : &payload[0], payload.size(), &up);
    std::vector<int> slots; std::vector<ENetPeer*> peers;
    std::vector<const std::set<int>*> areaOf;   /* M6: each candidate's delivery set (0 = no fresh report), AREA only */
    const bool isArea = (dr == cooplive::kLiveFwd && up.route == (unsigned int)cooplive::kRouteArea);
    const double nowArea = isArea ? NowSec() : 0.0;
    if (origin >= 0 && dr == cooplive::kLiveFwd)
    {
        slots.reserve(g_slot.size()); peers.reserve(g_slot.size());
        for (std::map<ENetPeer*, int>::const_iterator it = g_slot.begin(); it != g_slot.end(); ++it)
        {
            ENetPeer* p = it->first;
            if (p == from || p->state != ENET_PEER_STATE_CONNECTED || g_peerId.count(p) == 0) continue;
            if (!coopjoin::LiveDestAllowed(StageOf(p))) { ++g_liveDestSkipped; continue; }   /* M11a S1: a game at the title (or loading) is never fed */
            slots.push_back(it->second); peers.push_back(p);
            if (isArea) areaOf.push_back(LiveAreaDeliveryOf(it->second, nowArea));
        }
    }
    std::vector<size_t> dests;
    const int v = (origin < 0) ? (int)cooplive::kLiveNoSlot : (dr != cooplive::kLiveFwd ? dr : (isArea ? cooplive::LiveAreaRouteDecide(up.target, origin, slots, areaOf, &dests) : cooplive::LiveRouteDecide(up.route, up.target, origin, slots, &dests)));
    if (v == cooplive::kLiveNoSlot) { ++g_liveNoSlot; LiveRefused(g_liveNoSlot, from, "that connection holds no slot (it waits in the lobby or was never admitted), so there is no sender to stamp"); return; }
    if (v == cooplive::kLiveBadLen) { ++g_liveBadLen; LiveRefused(g_liveBadLen, from, N((long long)payload.size()) + " bytes whose inner length disagrees with the frame, or is over " + N((long long)cooplive::kLiveInnerMax) + ", or whose WORLD target is not 0 (a WORLD_EXCEPT target past a u16), or whose AREA target names no sector"); return; }
    if (v == cooplive::kLiveBadRoute) { ++g_liveBadRoute; LiveRefused(g_liveBadRoute, from, "route " + N((long long)up.route) + " is not a route this notebook knows"); return; }
    if (v == cooplive::kLiveNoTarget) { ++g_liveNoTarget; LiveRefused(g_liveNoTarget, from, "slot " + N((long long)up.target) + " is not another connected game in the world"); return; }
    if (isArea) ++g_liveAreaIn;   /* fold 1: counted AFTER the decision - an AREA refused for its target is live badLen, one with no slot live noSlot; so liveArea in = relayed AREA + none */
    if (isArea) LiveAreaMarkMissed(up.target, slots, dests, up.innerType);   /* M7a2 item 8 [m7a2-sm4]; fold 1 item 7 [m7a2f-sm3]: one-off kinds only */
    if (dests.empty() && isArea) { ++g_liveAreaNone; return; }   /* M6: no other game has that sector in its delivery area - nobody to tell */
    if (dests.empty()) { ++g_liveAlone; return; }   /* WORLD with no other game connected - nothing to refuse, nobody to tell */
    std::vector<char> down;
    if (!cooplive::LiveDownEncode(&down, origin, up.innerType, up.innerLen != 0 ? &payload[up.innerAt] : 0, up.innerLen))
    { ++g_liveNoSlot; LiveRefused(g_liveNoSlot, from, "slot " + N((long long)origin) + " does not fit the envelope's u16"); return; }
    std::vector<char> buf(5 + down.size()); buf[0] = (char)MSG_LIVE; const unsigned int n = (unsigned int)down.size(); memcpy(&buf[1], &n, 4); memcpy(&buf[5], &down[0], down.size());
    ++g_liveRelayed;
    long long sent = 0;
    /* fold 1 (review of 23f4216a): ONE packet for every destination, reference-counted exactly as enet_host_broadcast does it -
       each queued send holds a reference, and a packet no send took is destroyed here. */
    /* M16: a game whose link is congested gets the frame through its bound (SbSend: a MOVE or STATE keeps only the newest
       per character there); the others share the one packet as before. An unreliable MOVE or STATE goes straight out
       past frames that wait for a game whenever that game's network queue has room (sendbound.h). */
    /* M13: a game sent bundles gets the frame in its open bundle (it leaves with everything else this pass produced for that
       game); a frame too large to share a packet still goes as the one shared packet, after that game's open bundle. */
    coopsb::Item probe; probe.type = MSG_LIVE; probe.channel = ch;
    probe.cls = coopsb::ClassOf(MSG_LIVE, &buf[5], buf.size() - 5, coopsb::kDirDown, &probe.key);
    ENetPacket* pkt = 0;
    for (size_t i = 0; i < dests.size(); ++i)
    {
        ENetPeer* d = peers[dests[i]];
        SbPeer& sb = SbOf(d);
        bool superseded = false;
        const bool direct = sb.box.Direct(sb.netCount, sb.netBytes, probe, &superseded);
        if (superseded) ++g_sbCoalesced;   /* an older waiting value of this character was removed: this one replaces it */
        if (!direct)
        {
            if (!SbSend(d, buf, true, ch)) { ++g_liveSendFailed; continue; }
            ++sent; ++g_liveFwdTo[slots[dests[i]]];
            continue;
        }
        if (sb.bundle && coopbundle::Fits(&buf[0], buf.size(), coopbundle::Budget(d->mtu, d->host != 0 && d->host->checksum != 0)))
        {
            if (!SbToNet(d, sb, &buf[0], buf.size(), ch)) { ++g_liveSendFailed; continue; }
            ++g_sbDirect; ++sent; ++g_liveFwdTo[slots[dests[i]]];
            continue;
        }
        if (sb.bundle) SbSealLane(d, sb, coopbundle::LaneOf(ch));   /* M13: what is open on this lane goes first - the order holds */
        if (pkt == 0) pkt = enet_packet_create(&buf[0], buf.size(), SbFlagsOf(ch));
        if (pkt == 0) { ++g_liveSendFailed; continue; }
        if (enet_peer_send(d, (enet_uint8)ch, pkt) != 0) { ++g_liveSendFailed; continue; }
        ++sb.netCount; sb.netBytes += (long long)buf.size(); ++g_sbDirect; ++g_bnPackets;
        if (sb.bundle) ++g_bnTally.alone;
        ++sent; ++g_liveFwdTo[slots[dests[i]]];   /* M11a S1: per destination slot */
    }
    if (pkt != 0 && pkt->referenceCount == 0) enet_packet_destroy(pkt);
    g_liveFwd += sent; g_liveBytesOut += sent * (long long)down.size(); if (isArea) g_liveAreaFwd += sent;
    if (sent > 0 && !g_liveFirstLogged)
    {
        g_liveFirstLogged = true;
        Log("LIVE: the first live message relayed - inner type " + N((long long)up.innerType) + " from slot " + N((long long)origin) + " (" + PeerName(from) + ") by route " + N((long long)up.route)
            + " to " + N(sent) + " game(s); every one is counted live[...] on the REPORT line");
    }
}

bool ParseRecord(const std::vector<char>& p, Record* r, std::vector<char>* bytes)
{
    size_t at = 0;
    if (!GetStr(p, &at, &r->worldId) || !GetStr(p, &at, &r->squadSid) || !GetStr(p, &at, &r->factionName) || p.size() < at + 12 + 12 + 4) return false;
    float v[3]; memcpy(v, &p[at], 12); at += 12; r->x = v[0]; r->y = v[1]; r->z = v[2];
    unsigned lo = 0, hi = 0, owner = 0, n = 0; GetU32(p, at, &lo); GetU32(p, at + 4, &hi); GetU32(p, at + 8, &owner); at += 12;
    r->writtenAt = (long long)(((unsigned long long)hi << 32) | lo); r->owner = owner;
    if (!GetU32(p, at, &n) || p.size() < at + 4 + n) return false;
    bytes->assign(p.begin() + at + 4, p.begin() + at + 4 + n);
    { size_t at2 = at + 4 + n; r->town.clear(); if (at2 < p.size()) GetStr(p, &at2, &r->town);
      /* B12 (protocol 41): the sender's seq rides after the home town. THIS NOTEBOOK IGNORES IT and stamps
         its own - a game may not choose the number that orders the folder's writes - so it is read only to
         be stepped over, and a message without it (a game one build behind, which protocol 42 refuses at
         the handshake anyway) simply leaves r->seq at 0. */
      r->seq = 0ULL;
      if (at2 + 8 <= p.size()) { unsigned slo = 0, shi = 0; GetU32(p, at2, &slo); GetU32(p, at2 + 4, &shi); r->seq = ((unsigned long long)shi << 32) | slo; } }
    return true;
}
std::vector<char> EncodeRecord(const Record& r, const std::vector<char>& bytes, long long stamp)
{
    std::vector<char> b; PutStr(&b, r.worldId); PutStr(&b, r.squadSid); PutStr(&b, r.factionName);
    size_t at = b.size(); b.resize(at + 12); memcpy(&b[at], &r.x, 4); memcpy(&b[at + 4], &r.y, 4); memcpy(&b[at + 8], &r.z, 4);
    PutU32(&b, (unsigned)(stamp & 0xFFFFFFFFu)); PutU32(&b, (unsigned)((unsigned long long)stamp >> 32)); PutU32(&b, r.owner);
    PutU32(&b, (unsigned)bytes.size()); b.insert(b.end(), bytes.begin(), bytes.end());
    PutStr(&b, r.town);   /* decision 34 */
    /* B12 (protocol 41): THIS NOTEBOOK'S SEQUENCE NUMBER FOR THE RECORD, last field. A game reads it into
       its own copy of the record and quotes it back as the seq it last saw, which is the whole of decision
       52's conflict rule: equal means nobody wrote since, higher means somebody did. */
    PutU32(&b, (unsigned)(r.seq & 0xFFFFFFFFULL)); PutU32(&b, (unsigned)(r.seq >> 32));
    return b;
}

/* P8d: ReadFileBytes is GONE. It answered the same false for "there is no such file" and for "the file is
   there and I could not read it", and that one collapsed answer is F674, review-p8c C-2 and review-p8c H-1
   - three data-loss findings from one bool. Every reader in this file uses ProbeFile above. */
std::string HX8(unsigned v) { char b[16]; sprintf(b, "%08x", v); return b; }
// P8b: the write-safety verdict, in the notebook's own words.
std::string WriteQueueToken();   /* M14: defined with the writer thread, below */
std::string SendBoundToken();    /* M16: defined after the write gate, below */
std::string BundleToken();       /* M13: defined after the write gate, below */
std::string TickToken();   /* M15: defined with the loop's wait, below */
void LogStoreCounters()
{
    Log("store: index[loaded=" + N(g_indexLoaded) + ",positionOnly=" + N(g_indexPositionOnly)
        + ",refusedTorn=" + N(g_indexRefusedTorn)
        + ",refusedCrc=" + N(g_indexRefusedCrc) + ",refusedMissing=" + N(g_indexRefusedMissing)
        + ",promotedPrev=" + N(g_indexPromotedPrev) + ",upgraded=" + N(g_indexUpgraded)
        + ",upgradedNoPayload=" + N(g_indexUpgradedNoPayload)
        + ",upgradedToV7=" + N(g_indexUpgradedToV7)
        + ",v7UpgradeFailed=" + N(g_indexV7UpgradeFailed) + ",v7Deferred=" + N(g_indexV7Deferred)
        + ",seqNext=" + N((long long)g_seqNext) + ",seqStamped=" + N(g_seqStamped)
        + ",seqEchoSent=" + N(g_seqEchoSent)
        + ",upgradeDeferredUnreadable=" + N(g_indexUpgradeDeferredUnreadable)
        + ",promotedPrevOnUpgrade=" + N(g_indexPromotedPrevOnUpgrade)
        + ",payloadSetAside=" + N(g_indexPayloadSetAside)
        + ",metaEmpty=" + N(g_indexMetaEmpty) + ",metaUnparsable=" + N(g_indexMetaUnparsable)
        + ",metaVanished=" + N(g_indexMetaVanished)
        + ",noVersion=" + N(g_indexNoVersion)
        + ",refusedUnreadable=" + N(g_indexRefusedUnreadable)
        + ",payloadPrevPresent=" + N(g_indexPayloadPrevPresent)
        + ",payloadZeroLength=" + N(g_indexPayloadZeroLength)
        + ",refusedFilesPresent=" + N(g_indexRefusedFilesPresent)
        + "] probeUnreadable[meta=" + N(g_probeUnreadableMeta) + ",payload=" + N(g_probeUnreadablePayload)
        + ",metaPrev=" + N(g_probeUnreadableMetaPrev) + ",payloadPrev=" + N(g_probeUnreadablePayloadPrev)
        + "] rotate[pair=" + N(g_rotatePair) + ",metaOnly=" + N(g_rotateMetaOnly)
        + ",skippedOrphan=" + N(g_rotateSkippedOrphan)
        + ",rolledBack=" + N(g_rotateRolledBack) + ",rollbackFailed=" + N(g_rotateRollbackFailed)
        + ",nothingRotated=" + N(g_rotateNothingRotated) + ",prevLocked=" + N(g_rotatePrevLocked)
        + "] positionDeferred[unreadable=" + N(g_positionDeferredUnreadable)
        + ",applied=" + N(g_positionDeferredApplied) + ",expired=" + N(g_positionDeferredExpired)
        + ",pending=" + N((long long)g_pendingPos.size())
        + ",pendingPosDroppedAtRestart=" + N(g_pendingPosDroppedAtRestart)
        + ",pendingPosFileWriteFailed=" + N(g_pendingPosFileWriteFailed)
        + "] other[sendPayloadUnreadable=" + N(g_sendPayloadUnreadable)
        + ",optionsDeferredUnreadable=" + N(g_optionsDeferredUnreadable)
        + ",bitsDeferredUnreadable=" + N(g_bitsDeferredUnreadable)
        + "] research[boxesIn=" + N(g_researchBoxesIn) + ",stored=" + N(g_researchBoxesStored) + ",dup=" + N(g_researchBoxesDup) + ",researchWriteFailed=" + N(g_researchWriteFailed) + ",badLines=" + N(g_researchBadLines) + ",pushSkipped=" + N(g_researchPushSkipped) + ",deferredLoadRecovered=" + N(g_researchDeferredLoadRecovered) + ",deferredLost=" + N(g_researchDeferredLost) + ",loadDeferred=" + N((long long)g_researchLoadDeferred)
        + "] takes[in=" + N(g_takesIn) + ",stored=" + N(g_takesStored) + ",dup=" + N(g_takesDup) + ",idMismatch=" + N(g_takesIdMismatch) + ",writeFailed=" + N(g_takesWriteFailed) + ",badLines=" + N(g_takesBadLines) + ",takeRows=" + N((long long)g_takes.size()) + ",loadDeferred=" + N((long long)g_takesLoadDeferred) + ",deferredRecovered=" + N(g_takesDeferredRecovered)
        + "] deferredLoad[uniquesWriteRefusedDeferred=" + N(g_uniquesWriteRefusedDeferred)
        + ",optionsWriteRefusedDeferred=" + N(g_optionsWriteRefusedDeferred)
        + ",clockWriteRefusedDeferred=" + N(g_clockWriteRefusedDeferred)
        + ",deferredLoadRecovered(uniques)=" + N(g_uniquesDeferredLoadRecovered)
        + ",uniquesRepublished=" + N(g_uniquesRepublished)
        + ",deferredLoadRecovered(options)=" + N(g_optionsDeferredLoadRecovered)
        + ",deferredLoadRecovered(clock)=" + N(g_clockDeferredLoadRecovered)
        + ",stillDeferred=" + std::string(g_uniquesLoadDeferred ? "uniques " : "")
        + (g_optionsLoadDeferred ? "options " : "") + (g_clockLoadDeferred ? "clock" : "")
        + "] write[tempFailed=" + N(g_writeTempFailed) + ",renameFailed=" + N(g_writeRenameFailed)
        + "] handshake[refusedProtocol=" + N(g_helloRefusedProto) + ",refusedNoPlayerId=" + N(g_helloRefusedNoId) + ",refusedDuplicateId=" + N(g_helloRefusedDupId) + ",evictedSilentPeer=" + N(g_helloEvictedSilent)
        /* B13. known = a player this world had already named came back and got ITS number; restored = rows read
           out of slots.txt at start; assignedNew = players this world had never seen. keptOnRestore counts AREAS
           DEFENDED (each row once), not seconds. A fileWriteFailed above 0 means this notebook will forget again
           at its next restart, and is the one number here that is a fault rather than a measurement. */
        /* M1: admitted = HELLOs let in; refusedFull = turned away at the connected limit; refusedLifetime = a new id
           turned away at the lifetime limit; high = the most games linked at once. lifetimeUsed = numbers named.
           M1-b: helloTimeout = connections closed because no HELLO admitted them within kHelloDeadlineSec. */
        + "] conn[admitted,refusedFull,refusedLifetime,high,helloTimeout]=" + N(g_connAdmitted) + "," + N(g_connRefusedFull) + "," + N(g_connRefusedLifetime) + "," + N(g_connHigh) + "," + N(g_connHelloTimeout)
        + " slots[known,restored,assignedNew,fileBadLines,fileWriteFailed,lifetimeUsed]=" + N(g_slotsKnown) + "," + N(g_slotsRestored) + "," + N(g_slotsAssignedNew) + "," + N(g_slotsFileBadLines) + "," + N(g_slotsFileWriteFailed) + "," + N((long long)g_slotById.size())
        + " areas[restored,keptOnRestore,transferredAfterGrace,released,fileWriteFailed]=" + N(g_areasRestored) + "," + N(g_areasKeptOnRestore) + "," + N(g_areasTransferredAfterGrace) + "," + N(g_areasReleased) + "," + N(g_areasFileWriteFailed) + " areasAssign[ring1,reporter,unconfirmed,confirmed,unconfirmedExpired]=" + N(g_areasAssignRing1) + "," + N(g_areasAssignReporter) + "," + N(g_areasUnconfirmed) + "," + N(g_areasUnconfirmedConfirmed) + "," + N(g_areasUnconfirmedExpired)
        /* B13-b (M-4): linked=0 with an owner named means nobody carrying that id has linked this run - the
           world has no authority at all, and the line above says so once by name. */
        + " owner[source=" + std::string(coopstore::OwnerSourceName(g_ownerSource)) + ",linked=" + N((long long)g_ownerLinked) + ",fileWriteFailed=" + N(g_ownerFileWriteFailed) + "]"
        /* W2a: the world folder and the one-time migration, repeated from the first line so a later report still says them. */
        + " worldDir[source=" + std::string(g_worldDirSource) + ",worldTxt=" + std::string(g_worldTxtState) + ",worldId=" + (g_worldId.empty() ? std::string("-") : g_worldId) + (g_worldIdUpgraded ? std::string("(upgraded)") : std::string()) + ",format=" + std::string(g_worldFormatState) + "]"
        /* W3: HELLOs naming another world than this notebook's - let in and logged; the game follows the WELCOME. */
        + " helloWorldDiffers=" + N(g_helloWorldDiffers)
        /* prof1: profiles.txt rows (active/deleted), the lobby's answers, and every refusal by reason. */
        + " profiles[rows,listed,made,deleted,picked,badLines,writeFailed,adopted]=" + N((long long)g_profiles.size()) + "," + N(g_profListed) + "," + N(g_profMade) + "," + N(g_profDeleted) + "," + N(g_profPicked) + "," + N(g_profBadLines) + "," + N(g_profWriteFailed) + "," + N(g_profAdopted)
        + " profilesReadOnly[newer,refused]=" + N((long long)g_profReadOnly) + "," + N(g_profWriteRefusedNewer)
        + " refused[samePerson]=" + N(g_profRefusedSamePerson) + " profileRefused[cap,name,unknown,inUse,other]=" + N(g_profRefusedCap) + "," + N(g_profRefusedName) + "," + N(g_profRefusedUnknown) + "," + N(g_profRefusedInUse) + "," + N(g_profRefusedOther)
        /* names2a: a playing game's faction name into its own profile's row (PROFILES kind 3). */
        + " profFaction[set,same,refused]=" + N(g_profFactionSet) + "," + N(g_profFactionSame) + "," + N(g_profFactionRefused)
        /* restore1a (+ fold): world.gen and the operator's WORLD_SAVED stamps (stampedAcked = answers sent; gen = the highest profile's). */
        + " world[gen=" + N((long long)restoreguard::MaxGen(g_wg)) + ",profiles=" + N((long long)restoreguard::NumberedCount(g_wg)) + ",seqHigh=" + N((long long)WorldSeqHighNow())
        + ",stampedAcked=" + N(g_wgAnswered) + ",stampsIn=" + N(g_wgIn) + ",raised=" + N(g_wgRaised) + ",stale=" + N(g_wgStale) + ",notOperator=" + N(g_wgNotOperator)
        + ",profileRefused=" + N(g_wgProfileRefused) + ",malformed=" + N(g_wgMalformed) + ",file=" + N((long long)g_wgFileState) + ",writeFailed=" + N(g_wgWriteFailed) + "]"
        /* restore1b1: the repair copies at each new operator world save, the broadcast, and the records high-water book. */
        + " repairCopy[made,pruned,bytes,failed,broadcasts,pruneNoBin]=" + N(g_rcMade) + "," + N(g_rcPruned) + "," + N(g_rcBytes) + "," + N(g_rcFailed) + "," + N(g_wsBroadcast) + "," + N(g_rcPruneNoBin)
        + " ownHigh[in,rows,raised,behind,pushed,malformed,writeFailed,badLines]=" + N(g_ohIn) + "," + N((long long)g_oh.high.size()) + "," + N(g_ohRaised)
        + "," + N(g_ohBehind) + "," + N(g_ohPushed) + "," + N(g_ohMalformed) + "," + N(g_ohWriteFailed) + "," + N(g_ohBadLines)
        /* M4 fold / owner 205 A: the uid seats (held now, taken, freed, refused because none was free, moved off a spent seat) and the blocks (seat rows, grants, EXHAUSTED / NO_SEAT answers, FAILED writes, malformed / not-admitted asks, refusals under an unreadable file) */
        + " seats[held,taken,freed,refusedFull,movedSpent]=" + N((long long)g_seatsHeld.size()) + "," + N(g_seatTaken) + "," + N(g_seatFreed) + "," + N(g_seatRefusedFull) + "," + N(g_seatMovedSpent)
        + " uidBlocks[seats,granted,exhausted,noSeat,writeFailed,malformed,notAdmitted,refusedUnreadable,badLines]=" + N((long long)g_ub.high.size()) + "," + N(g_ubGranted)
        + "," + N(g_ubExhausted) + "," + N(g_ubNoSeat) + "," + N(g_ubWriteFailed) + "," + N(g_ubMalformed) + "," + N(g_ubNotAdmitted) + "," + N(g_ubRefusedUnreadable) + "," + N((long long)g_ubBadLines)
        /* restore1c: the repairs */
        + " repair[epoch,restoreGen,restoreSeq,notebookRecycled,reverted,putBack,in,done,refused,failed,undoneStamps,malformed,badLines]=" + N((long long)g_wg.epoch)
        + "," + N((long long)(g_repairs.empty() ? 0u : g_repairs.back().gen)) + "," + N((long long)(g_repairs.empty() ? 0ULL : g_repairs.back().seq)) + "," + N(g_rpRecycled)
        + "," + N(g_rpReverted) + "," + N(g_rpPutBack) + "," + N(g_rpIn) + "," + N(g_rpDone) + "," + N(g_rpRefused) + "," + N(g_rpFailed) + "," + N(g_rpUndoneStamps)
        + "," + N(g_rpMalformed) + "," + N(g_rpBadLines)
        + " migrate[moved,skipped,failed]=" + N(g_migMoved) + "," + N(g_migSkipped) + "," + N(g_migFailed)
        /* M5a (T-197 piece 4): LIVE (53) - messages in; of them relayed to >= 1 game and each refusal (in = relayed + noSlot + badRoute + badLen + noTarget + alone + M6's liveArea none); fwd = frames sent; bytes = envelope payloads */
        + " live[in,relayed,fwd,noSlot,badRoute,badLen,noTarget,alone,sendFailed]=" + N(g_liveIn) + "," + N(g_liveRelayed) + "," + N(g_liveFwd) + "," + N(g_liveNoSlot) + "," + N(g_liveBadRoute)
        + "," + N(g_liveBadLen) + "," + N(g_liveNoTarget) + "," + N(g_liveAlone) + "," + N(g_liveSendFailed) + " liveBytes[in,out]=" + N(g_liveBytesIn) + "," + N(g_liveBytesOut)
        + " liveArea[in,fwd,none,missed,missedRepeat]=" + N(g_liveAreaIn) + "," + N(g_liveAreaFwd) + "," + N(g_liveAreaNone) + "," + N(g_liveAreaMissed) /*[m7a2-sm5]*/ + "," + N(g_liveAreaMissedRepeat) /*[m7a2f-sm4]*/   /* M6 */
        + " catchup[reports,rounds,newSectors,asks,noOwner,sendFailed,emptyReports,ownerNotInWorld]=" /*[m7a2-sm6]*/ + N(g_catchupReports) + "," + N(g_catchupRounds) + "," + N(g_catchupNewSectors)
        + "," + N(g_catchupAsks) + "," + N(g_catchupNoOwner) + "," + N(g_catchupSendFailed) + "," + N(g_catchupEmptyReports) + "," + N(g_catchupOwnerNotInWorld) /*[m7a2-sm7]*/ + " liveAreaSlots=" + N((long long)g_liveAreas.size())
        /* T-313 (protocol 63): the record feed - subscribed connections now; ASKs, pages, OFFs, first pages (BEGIN), last pages, records and bytes paged; and the live forwards NOT sent to a connected game that has not asked */
        + " feed[on,asks,pages,offs,begins,ends,recordsSent,bytesSent,malformed,notAdmitted]=" + N((long long)g_feedOn.size()) + "," + N(g_feedAsks) + "," + N(g_feedPages) + "," + N(g_feedOffs)
        + "," + N(g_feedBegins) + "," + N(g_feedEnds) + "," + N(g_feedRecordsSent) + "," + N(g_feedBytesSent) + "," + N(g_feedMalformed) + "," + N(g_feedNotAdmitted)
        + " preload[indexAsks,indexPages,indexEntries,fetches,fetchKeys,fetchSent,fetchMissing,malformed,notAdmitted,fetchCut]=" + N(g_preIndexAsks) + "," + N(g_preIndexPages) + "," + N(g_preIndexEntries)   /* T-346 slice 1 */
        + "," + N(g_preFetches) + "," + N(g_preFetchKeys) + "," + N(g_preFetchSent) + "," + N(g_preFetchMissing) + "," + N(g_preMalformed) + "," + N(g_preNotAdmitted) + "," + N(g_preFetchCut)   /* T-346 fold 1: answers cut at ~1 MB */
        + " feedWithheld[record,gone,unique]=" + N(g_feedWithheld[0]) + "," + N(g_feedWithheld[1]) + "," + N(g_feedWithheld[2])
        + " areaSeats[seatsHeld,seatsTaken,seatsFreed,seatsFull,bytes]=" + N(g_areaSeatsHeld) + "," + N(g_areaSeatsTaken) + "," + N(g_areaSeatsFreed) + "," + N(g_areaSeatsFull) + "," + N(g_areaMapBytesLast)   /* M9 (T-197) */
        + " playerGone[sent,told,loadedDropped,sendFailed,held,cancelledReturned]=" + N(g_playerGoneSent) + "," + N(g_playerGoneTold) + "," + N(g_playerGoneLoadedDropped) + "," + N(g_playerGoneSendFailed)
        + "," + N(g_playerGoneHeld) + "," + N(g_playerGoneCancelledReturned)   /* M8; M8 review F1: held, cancelledReturned */
        + JoinReportToken()   /* M11a S1: connections by stage, PLAYERS sends, JOIN_STAGE, AREAS refused before in-world, live-protocol refusals, LIVE by stage and per destination slot */
        /* M14 (T-197), rule (5): the writer thread's queue and the loop's longest stall - see WriteQueueToken. */
        + WriteQueueToken()
        /* M15 (T-197): how late the once-a-second jobs ran, and the largest pass of message handling - see TickToken. */
        + TickToken()
        /* M16 (T-197): what waits to be sent to each game, and the held position updates expired - see SendBoundToken. */
        + SendBoundToken()
        /* M13: what bundling did - see BundleToken. */
        + BundleToken());
}
/* P8d (review-p8c M-3), replacing P8c's sentence, whose arithmetic did not mean what it said. `loaded -
   positionOnly` counts every record whose LINE claims a squad - which includes every refused and every
   deferred one, none of which has a fallback of any kind - so the number it printed as "the net's reach"
   was measured at four when exactly one of those four had a <id>.platoon.prev. What the net reaches is now
   COUNTED during the walk, one probe per record, and the two kinds of record are stated apart. */
void LogIndexCoverage()
{
    Log("store: THE PREVIOUS-VERSION NET NOW COVERS BOTH KINDS OF RECORD, and here is how far. "
        + N(g_indexPositionOnly) + " of " + N(g_indexLoaded) + " indexed records carry a place in the world"
        " and no people; each of those keeps a fallback INDEX LINE (<id>.meta.prev), which is the only thing"
        " such a record has to lose. The other " + N(g_indexLoaded - g_indexPositionOnly) + " claim a squad,"
        " and " + N(g_indexPayloadPrevPresent) + " of the records walked at this start actually HAD a"
        " <id>.platoon.prev on disk - that count, and not the subtraction, is how many squads the net could"
        " recover today, because a payload fallback is made by a payload write and by nothing else. A record"
        " whose payload was refused or deferred has a line and no squad to fall back on. P8d: a position"
        " update on a record that CARRIES a squad no longer rotates its index line, so <id>.meta.prev and"
        " <id>.platoon.prev always describe ONE version (review-p8c C-1 - P8c orphaned the payload half on"
        " the first position update after every payload write).");
}
/* B13-b (review-b13 M-4) - AN OPERATOR WHO NEVER TURNS UP. A well-formed but WRONG --owner (a typo, or a
   copy of another install's id) is persisted and permanent, and the only symptom is that no game is ever the
   authority: no OPTIONS, no WEATHER, no off-screen updates for unheld squads, and nothing in any log saying
   why. The ownership rule is NOT changed here - guessing a new operator would be exactly the dial-order
   answer decision 49 replaced - but the notebook says so, once, by name, and the index line carries it. */
void OwnerWatchTick()
{
    if (g_ownerAbsentSaid || g_ownerLinked || g_ownerId.empty()) return;
    if (NowSec() - g_restoredAtSec < kOwnerAbsentSayAfterSec) return;
    g_ownerAbsentSaid = 1;
    Log("B13 owner: this world's operator is '" + g_ownerId + "' (source " + std::string(coopstore::OwnerSourceName(g_ownerSource))
        + ") and NO game carrying that id has linked in the " + N((long long)kOwnerAbsentSayAfterSec) + " s since"
        " this notebook started. Until one does there is no authority at all: no game's OPTIONS or WEATHER are"
        " accepted and no game runs the off-screen updates for squads nobody holds. Nothing is guessed here -"
        " handing the flag to whoever connected first is the very thing decision 49 replaced. If the id is"
        " wrong, stop this notebook, delete owner.txt in its folder and start it again with the right --owner."
        " Said once; the index line carries owner[linked=0] for as long as it is true.");
}

/* B13: the HELD position updates, counted onto disk once a second when the number changes, so the notebook
   that comes after this one can say how many its predecessor was still holding. */
void PendingPosCountTick()
{
    static size_t last = (size_t)-1;
    if (g_pendingPos.size() == last) return;
    last = g_pendingPos.size();
    WritePendingPosCount(last);
}
void StoreCountersTick()
{
    static double last = 0.0;
    const double now = NowSec();
    if (last == 0.0) { last = now; return; }
    if (now - last < 60.0) return;
    last = now;
    LogStoreCounters();
}
/* P8b (design-save.md S1). THE TWO WRITES THAT CARRY THE SQUADS CAN NO LONGER BE CAUGHT HALF-FINISHED.
   Until P8b both of them opened the REAL file with trunc and wrote into it, so a notebook killed mid-write
   left a truncated payload that the index still listed as good and handed to every game that connected - and
   the engine's reader clears its target BEFORE it discovers the failure, so that group woke up with nobody in
   it, permanently, with nothing in any log saying why (read-save-coverage; review-s7). The plugin's own writer
   has been temp+rename since review-p3a S5 and four other writers in THIS file (bits, uniques, options, clock)
   already had the shape; only the two carrying the squads were left behind. */
std::string TempOf(const std::string& path)
{
    char sfx[32]; sprintf(sfx, ".%lu.tmp", (unsigned long)::GetCurrentProcessId());
    return path + sfx;   // review-p3h H5: unique per process, so two writers never share a temp file
}
bool WriteTempFile(const std::string& tmp, const std::vector<char>& b)
{
    std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) { ++g_writeTempFailed; return false; }
    if (!b.empty()) f.write(&b[0], (std::streamsize)b.size());
    if (!f) { ++g_writeTempFailed; f.close(); DeleteFileA(tmp.c_str()); return false; }
    return true;
}
bool SwapIn(const std::string& tmp, const std::string& path)
{
    /* MOVEFILE_WRITE_THROUGH: the rename does not return until it is on the disk. It buys ATOMIC VISIBILITY,
       not durability of the file's CONTENTS - which is precisely why the payload carries a checksum and keeps
       one previous version (design-save part 6, residual 4). */
    if (MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) return true;
    ++g_writeRenameFailed;
    Log("write: could not replace " + path + " (MoveFileExA, GetLastError=" + N((long long)::GetLastError()) + ")");
    DeleteFileA(tmp.c_str());
    return false;
}
bool WriteFileBytes(const std::string& path, const std::vector<char>& b)
{
    const std::string tmp = TempOf(path);
    if (!WriteTempFile(tmp, b)) return false;
    return SwapIn(tmp, path);
}
/* ================= P8d - ONE PREVIOUS VERSION PER RECORD, MADE IN EXACTLY ONE PLACE ==================
   The poor man's journal (design-save 1.2(d)), and the ONLY thing in this process that makes a .prev file.
   P8b rotated inside the payload writer; P8c added a SECOND rotation on the position path, which fired for
   records that carry a squad as well and destroyed the only line describing <id>.platoon.prev - the review
   measured the same damage going from RECOVERED under P8b to "there is no previous version to fall back
   on" under P8c. Two rotations kept in step by hand is what this replaces (6a lesson 11).

   THE ORDER IS <id>.meta FIRST, THEN <id>.platoon, and it is load-bearing rather than a habit. The four
   things a single crash can leave, and what the next start makes of each:
     * before either rename   - the current pair is whole; it is served (kChooseCurrent).
     * between the two        - no <id>.meta; <id>.meta.prev describes the payload STILL SITTING under the
                                current name, so that version is recovered (kChoosePrevMetaCurrent).
     * after both             - neither current name exists and the two .prev files describe ONE version,
                                which is promoted (kChoosePrevPair).
     * after the payload swap - the NEW payload is in place under a line that never landed; <id>.meta.prev
                                does not match it and DOES match <id>.platoon.prev, so the last committed
                                version is promoted and the uncommitted write is correctly discarded.
   Payload-first would leave the good bytes in <id>.platoon.prev with the only line describing them still
   under the CURRENT name - a pair that cannot be validated, and a squad lost in an ordinary crash.

   P8l (review-p8d C-2) REPLACES THE RULE THAT STOOD HERE. It said: "a .prev rename that fails is counted and
   does NOT stop the write - losing the fallback copy is worse than nothing and much better than losing the
   write it was protecting." That reasoning holds for the PAYLOAD half and is false for the META half,
   because the condition that fails the meta rename - something holding <id>.meta open - fails WriteMeta at
   the end of the same commit. The write it was protecting fails ANYWAY, and the payload half has already
   rotated: the last committed squad ends up in <id>.platoon.prev with no line describing it, the next start
   serves that group with nobody in it, and rotate[pair] counts a rotation in which one half did not move.
   The review reproduced that twice on separate folders.
   THE RULE NOW: ALL OR NOTHING, and the caller is told.
     the meta half will not move    -> count, LOG the file and the error, return false. Nothing is rotated,
                                       nothing is swapped, the record on disk is exactly as it was, and the
                                       next RECORD for that group retries.
     the payload half will not move -> the meta half is renamed BACK, the failure is counted and logged, and
                                       the rotation is counted as rotate[rolledBack] rather than as a pair.
                                       A rollback that itself fails has its own counter and its own line: it
                                       is the one state this cannot repair and it must not read as clean.
   This is not a history (decision 21) - never reported, never queried, never grows, overwritten by the next
   write.
   ==================================================================================================== */
enum { kRotateBoth = 0, kRotateMetaOnly = 1, kRotateNone = 2 };
bool RotatePair(const std::string& id, int mode)
{
    if (mode == kRotateNone) return true;
    const std::string meta = MetaOf(id), payload = FileOf(id);
    if (mode == kRotateMetaOnly)
    {
        /* THE INVARIANT THAT CLOSES review-p8c C-1, ENFORCED HERE INSTEAD OF AT EVERY CALL SITE.
           <id>.meta.prev is the ONLY line that describes <id>.platoon.prev. Rotating the meta half alone
           while a payload fallback is on disk overwrites that description, and the squad it points at
           becomes unreachable by construction - PayloadCheck refuses the pair for ever after. When a
           payload fallback exists, NOTHING is rotated and the record keeps the pair it has. */
        const FileProbe pp = ProbeFile(PrevOf(payload));
        if (pp.kind != coopstore::kFileAbsent) { ++g_rotateSkippedOrphan; return true; }
    }
    int metaMoved = 0;
    {
        const FileProbe mp = ProbeFile(meta);
        if (mp.kind != coopstore::kFileAbsent)
        {
            if (MoveFileExA(meta.c_str(), PrevOf(meta).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
            {
                const unsigned long e = ::GetLastError();
                /* review-p8l H-1. WHICH SIDE OF THE RENAME IS SHUT, because the two ask for opposite answers
                   and P8l gave them the same one - see storemeta.h`s RotateFailCause for the whole argument.
                   The probe of the source is `mp`, taken a few lines above; the destination is asked now,
                   only on the failure path, so the ordinary write pays nothing for it. */
                const FileProbe pv = ProbeFile(PrevOf(meta));
                const int cause = coopstore::RotateMetaFailCause(mp.kind, pv.kind);
                if (coopstore::RotateMetaFailAction(cause) == coopstore::kRotActionProceedNoRotate)
                {
                    ++g_rotatePrevLocked;
                    Log("write: could not rotate " + meta + " to " + PrevOf(meta) + " (MoveFileExA,"
                        " GetLastError=" + N((long long)e) + ") - THE DESTINATION IS THE FILE THAT IS SHUT: "
                        + PrevOf(meta) + " is ON DISK AND UNREADABLE while " + meta + " is readable, so the"
                        " index line at the end of this write will land. NOTHING IS ROTATED and the write"
                        " GOES AHEAD. This record keeps the .prev pair it already has - an older whole"
                        " version whose two halves still describe each other, which a crash mid-write still"
                        " recovers from - and the update is not refused, which is what P8l did here"
                        " (review-p8l H-1). Only the FRESHNESS of the fallback is lost. Counted"
                        " rotate[prevLocked].");
                    return true;
                }
                ++g_writeRenameFailed;
                /* B10-b (review-b10 M-4). EACH CAUSE STATES WHAT THAT CAUSE ESTABLISHED, AND NOTHING ELSE.
                   One sentence used to serve both refusing causes: "the same lock would fail the index line
                   at the end of this write". That is TRUE of a locked SOURCE - the file the index line is
                   about to be written to is the file we just failed to move - and it is a GUESS about
                   kRotCauseUnknown, where NEITHER file came back unreadable and we therefore know nothing
                   about what the next write will meet. This is H-1's own defect one arm further over: a
                   reason an operator would act on, asserting a fact the probe did not produce. */
                const std::string why = (cause == coopstore::kRotCauseSourceLocked)
                    ? (std::string("THE SOURCE IS THE FILE THAT IS SHUT: ") + meta + " is on disk and could"
                       " not be read, so the same lock would fail the index line at the end of this write.")
                    : std::string("NEITHER FILE COULD BE PROBED as locked - a permission, a device or a race."
                                  " What stopped the rename is not known, so nothing is claimed about what the"
                                  " index line at the end of this write would meet: the write is refused"
                                  " rather than guessed at.");
                Log("write: could not rotate " + meta + " to " + PrevOf(meta) + " (MoveFileExA, GetLastError="
                    + N((long long)e) + ") - THE RECORD IS LEFT EXACTLY AS IT WAS. Nothing is rotated and"
                    " nothing is swapped in. " + why + " " + (mode == kRotateMetaOnly
                        /* review-p8l H-1, the second half: THE REASON MUST BE TRUE OF THIS CASE. The text
                           here recited the payload-orphan argument for a mode that has no payload half to
                           orphan - a sentence an operator would have acted on, about a file that is not
                           in play. */
                        ? std::string("This record carries NO SQUAD, so there is no payload half to strand:"
                                      " the refusal is here because a write whose own index line cannot be"
                                      " written is not a write, and the next RECORD for this group retries.")
                        : std::string("Rotating the payload half alone leaves the last committed squad in ")
                          + PrevOf(payload) + " with no line describing it (review-p8d C-2). The next RECORD"
                            " for this group retries."));
                return false;
            }
            metaMoved = 1;
        }
    }
    if (mode == kRotateBoth)
    {
        const FileProbe fp = ProbeFile(payload);
        if (fp.kind != coopstore::kFileAbsent &&
            MoveFileExA(payload.c_str(), PrevOf(payload).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
        {
            const unsigned long e = ::GetLastError();
            ++g_writeRenameFailed;
            Log("write: could not rotate " + payload + " to " + PrevOf(payload) + " (MoveFileExA,"
                " GetLastError=" + N((long long)e) + ") - the index line that had already been rotated is"
                " being put BACK, so this record's CURRENT pair is exactly as it was and nothing is swapped"
                " in. Stated rather than left to be discovered: the OLD " + PrevOf(meta) + " was overwritten"
                " by that first rename and cannot be brought back, so a " + PrevOf(payload) + " on disk is"
                " described by nothing until the next successful write. That is a lost FALLBACK; a payload"
                " half that moved without its line is a lost COMMITTED SQUAD (review-p8d C-2), which is why"
                " this is the direction the failure is taken in.");
            /* review-p8l H-2. THE COUNTER IS BOOKED INSIDE THE BRANCH IT REPORTS, AND EXACTLY ONE OF
               THE THREE MOVES. ++g_rotateRolledBack sat below this block, so a rollback that ITSELF failed
               printed rolledBack=1,rollbackFailed=1 - the one state that must not read clean reading as an
               ordinary rollback - and it fired when metaMoved was 0, i.e. when NOTHING had been rotated and
               there was nothing to roll back. That is review-p8d C-2`s own counter defect, inside its fix. */
            {
                int rollbackOk = 1;
                if (metaMoved && MoveFileExA(PrevOf(meta).c_str(), meta.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
                {
                    rollbackOk = 0;
                    const unsigned long e2 = ::GetLastError();
                    ++g_writeRenameFailed;
                    Log("write: THE ROLLBACK ALSO FAILED - " + PrevOf(meta) + " could not be renamed back to "
                        + meta + " (MoveFileExA, GetLastError=" + N((long long)e2) + "). This record now has"
                        " its index line under the .prev name and no current one; the next start reads that"
                        " as crash window 2 and recovers from it. Counted rotate[rollbackFailed] and NOT"
                        " rotate[rolledBack] - it is the one state this write path cannot repair.");
                }
                const int book = coopstore::RotateFailBooking(metaMoved, rollbackOk);
                if      (book == coopstore::kRotBookRollbackFailed) ++g_rotateRollbackFailed;
                else if (book == coopstore::kRotBookRolledBack)     ++g_rotateRolledBack;
                else                                                ++g_rotateNothingRotated;
            }
            return false;
        }
        ++g_rotatePair;
    }
    else ++g_rotateMetaOnly;
    return true;
}
/* B12-b (decision 52): TELL THE WRITER THE NUMBER ITS RECORD GOT. To the writer and to nobody else - the
   other games learn every record's seq from the push-down at their own WELCOME, and a broadcast here would
   put a record's number on games that have no record for it. Called only after the commit has SUCCEEDED,
   so a number that never reached the disk is never announced. */
void SendSeqEcho(ENetPeer* to, const std::string& worldId, unsigned long long seq)
{
    if (to == 0) return;
    std::vector<char> b;
    PutStr(&b, worldId);
    PutU32(&b, (unsigned)(seq & 0xFFFFFFFFULL));
    PutU32(&b, (unsigned)(seq >> 32));
    SendMsg(to, MSG_RECORD_SEQ, b);
    ++g_seqEchoSent;
}
void MetaFromRecord(const Record& r, coopstore::MetaLine* m)
{
    m->version = 7;   /* B12: STORE-FILE FORMAT 7 - see storemeta.h */
    m->seq = r.seq;
    m->writtenAt = r.writtenAt; m->posAt = r.posAt; m->owner = r.owner;
    m->x = r.x; m->y = r.y; m->z = r.z;
    m->squadSid = r.squadSid; m->factionName = r.factionName; m->worldId = r.worldId; m->town = r.town;
    m->len = r.len; m->crc = r.crc;
}
void RecordFromMeta(const coopstore::MetaLine& m, Record* r)
{
    r->writtenAt = m.writtenAt; r->posAt = m.posAt;
    /* STORE-FILE FORMAT 5 -> 6 (B10, design-e46-store 3.5) - THE ONE PLACE A ROLE BECOMES A SLOT, so
       that no caller can forget and no second copy can drift. Every line below v6 holds a SESSION ROLE
       in `owner` (1 = the host wrote it, 2 = the client did) and a role CANNOT be mapped to a slot:
       which slot the host held is not knowable from the file, and under decision 32 the host holds no
       particular slot. 0 would be a guess that reads as fact, so the answer is kOwnerUnknown - which
       is the fail-safe direction: for one session after the upgrade every migrated zone record
       translates, and the first write by a real slot fixes it permanently. */
    r->owner = (int)coopstore::OwnerAfterUpgrade(m.version, m.owner);
    r->x = m.x; r->y = m.y; r->z = m.z;
    r->squadSid = m.squadSid; r->factionName = m.factionName; r->worldId = m.worldId; r->town = m.town;
    r->len = m.len; r->crc = m.crc;
    r->seq = m.seq;   /* B12: 0 for a v6 line and below - the notebook stamped nothing before format 7 */
    r->gone = 0;        // decision 28: a v3 line from the superseded build is read as live
    r->hasFile = false; // set by the caller once the payload has been judged
}
const char* RefusalWord(int v)
{
    switch (v)
    {
        case coopstore::kPayloadMissing: return "missing";
        case coopstore::kPayloadTorn:    return "the wrong length (a half-finished write)";
        case coopstore::kPayloadCrcBad:  return "the right length with the wrong contents (the checksum failed)";
        case coopstore::kPayloadUnreadable: return "ON DISK AND UNREADABLE (not missing, and not ruled out)";
        default:                         return "refused";
    }
}
/* v5 (P8b): the line gained the payload's byte LENGTH and its CRC-32, appended, so a reader can tell a whole
   record from a torn one. The encoder and the parser are in src\common\storemeta.cpp, shared with the plugin
   and the offline test exe. Returns false - and writes NOTHING - when the line cannot be encoded or cannot be
   swapped in, and every caller acts on that (design-save 1.2(a)): a void WriteMeta that swallowed its failures
   is how memory and disk were allowed to disagree with nothing saying so. */
bool WriteMeta(const Record& r)
{
    coopstore::MetaLine m; MetaFromRecord(r, &m);
    std::string line;
    if (!coopstore::MetaEncodeV7(m, &line))   /* B12: this notebook writes FORMAT 7 and nothing else */
    {
        Log("index: REFUSED to write a meta line for " + SanitizeForLog(r.worldId) + " - a field carries a tab or a newline");
        return false;
    }
    const std::vector<char> b(line.begin(), line.end());
    return WriteFileBytes(MetaOf(r.worldId), b);
}
/* ==================== P8d - ONE WRITE PATH FOR A RECORD, AND ALL THREE WRITERS TAKE IT ===============
   temp payload -> RotatePair -> rename the temp in -> write the index line (temp + rename). The line is
   the COMMIT POINT: a crash between the payload and the line leaves a good payload under a stale line,
   which the next write for that group simply overwrites.
   `payload == 0` means THIS WRITE CARRIES NO SQUAD BYTES. The record's line still says what its payload IS
   (r.len / r.crc), which is exactly what lets a position update on a squad-carrying record leave
   <id>.platoon AND BOTH .prev FILES where they are - review-p8c C-1.
   The two RECOVERY arms in LoadIndex are deliberately NOT callers: they rename a previous version into
   place and then write the line for it, and rotating there would push the line they have just REFUSED over
   the pair they have just promoted. That is the one rotation that is always wrong, so it is not reachable
   from here. */
bool CommitRecord(const Record& r, const std::vector<char>* payload, int rotate)
{
    const std::string path = FileOf(r.worldId);
    std::string tmp;
    if (payload != 0)
    {
        tmp = TempOf(path);
        if (!WriteTempFile(tmp, *payload)) return false;
    }
    /* P8l (review-p8d C-2): THE ROTATION IS A GATE. A rotation that could not complete leaves the record on
       disk exactly as it was, so this write is abandoned here - before the payload is swapped in and before
       the line is written - and the temp payload goes with it. */
    if (!RotatePair(r.worldId, rotate))
    {
        if (payload != 0) DeleteFileA(tmp.c_str());
        return false;
    }
    if (payload != 0 && !SwapIn(tmp, path)) return false;
    return WriteMeta(r);
}
/* THE ONE PREDICATE BEHIND "does a position write rotate?", so its two call sites - the message and the
   retry tick - cannot be kept in step by hand and drift (6a lesson 11). A record that CARRIES a squad must
   not rotate, because its <id>.meta.prev is the only description of its <id>.platoon.prev; a record with
   no squad may, and RotatePair still refuses if a payload fallback is somehow there. */
int PositionRotateMode(const Record& r) { return r.len > 0 ? kRotateNone : kRotateMetaOnly; }

/* ================== M14 (T-197): THE WRITER THREAD - THE LOOP NEVER WAITS ON A RECORD WRITE ==================
   Until M14 every RECORD was committed ON THE LOOP - temp payload, the two .prev rotations, the swap and the
   index line, up to four write-through renames - while every other player's traffic waited behind it
   (.modding/investigations/server-load-read.md 4b; build/design-many.md M14). Now the loop decides and
   builds each write EXACTLY as before (same checks, same stamped seq, same rotate mode) and hands it to ONE
   writer thread, which makes the very same calls (CommitRecord, and the five deletes of a RECORD_GONE). The
   effects that follow a successful commit - the record in memory, the seq echo to its writer, the RECORD
   line, the dropped hold, the forward to every other game - run on the loop when that job's completion is
   applied (WriterApplyOne), never before: a number that never reached the disk is still never announced
   (B12-b), and memory still changes only once the line is on disk (P8b).
   THE RULES, each enforced where a comment naming it stands:
     (1) ORDER PER FILE: WriterQueue + coopwq::KeyGate (one job per record key out at a time; a message for
         that key is HELD in arrival order) + coopwq::Fifo (one writer, one job at a time, queue order). The
         key is coopwq::GateKey(id) - the FILE's name, lower-cased (M14 fold, finding 8).
     (2) A READ SEES THE QUEUED BYTES: every loop-side read of a record file either runs only while that key
         has no job out (OnRecordFrom's probe, PendingPosTick) or drains the queue first (WriterFlushAll -
         OnHello's push, OnRepair, OnWorldSaved).
     (3) QUIT DRAINS THE QUEUE: QuitDrain, from StoreCtrlHandler (close button, Ctrl+C, Ctrl+Break) and from the
         hidden window's WM_ENDSESSION (logoff, shutdown) - this process has no other quit path, its loop never ends.
     (4) A FAILURE IS LOGGED AS TODAY AND LOSES NOTHING AFTER IT: WriterMain / WriterApplyOne.
     (5) THE REPORT: WriteQueueToken, on the 60 s counters line.
   THE COUNTERS THE WRITER BUMPS (write[tempFailed,renameFailed], rotate[...]) are touched by that thread
   alone once it runs - LoadIndex's own increments happen before it starts - and the loop only READS them for
   the report line (aligned 64-bit loads on x64).
   STILL ON THE LOOP (M14 LEFTOVER): the whole-file rewrites of the small non-record files (options, uniques,
   bits, clock, areas, slots, ...), whose callers act on the result at once, and OnRepair's own moves. */
enum { kJobCommit = 0, kJobDelete = 1 };
enum { kFromSquad = 0, kFromPosition = 1, kFromHeldPosition = 2, kFromDelete = 3 };
struct WriteJob
{
    int kind, from; std::string key, gate;   /* key = the record id (its files); gate = coopwq::GateKey(key), what the per-file gate is keyed on (M14 fold) */
    Record rec; std::vector<char> payload; bool hasPayload; int rotate;                /* what the writer does */
    ENetPeer* peer; unsigned peerConnect; std::string peerName; Record fwd;              /* what the loop does once it has landed */
    long long stamp;                                                                     /* a delete's stamp, for its DELETE line (M14 fold) */
    bool ok;
    WriteJob() : kind(kJobCommit), from(kFromSquad), hasPayload(false), rotate(kRotateNone), peer(0), peerConnect(0), stamp(0), ok(false) {}
};
/* M14 fold (finding 9): peerName is the sender's name AS IT WAS WHEN THE MESSAGE ARRIVED - the slot may belong to another game by the time it is handled. */
struct HeldMsg { unsigned char type; ENetPeer* peer; unsigned connect; std::string peerName; std::vector<char> payload; HeldMsg() : type(0), peer(0), connect(0) {} };
struct WqLock { CRITICAL_SECTION cs; WqLock() { InitializeCriticalSection(&cs); } };
WqLock g_wqLock;                        /* guards g_wq, g_wqDone and g_wqClosing - nothing else */
coopwq::Fifo<WriteJob*> g_wq;
std::deque<WriteJob*> g_wqDone;         /* finished jobs, in the order they finished (= queue order) */
int g_wqClosing = 0;
HANDLE g_wqWork = 0, g_wqIdle = 0, g_wqSpace = 0;
coopwq::KeyGate<HeldMsg> g_wqGate;      /* LOOP THREAD ONLY */
/* M16 (T-197): A HELD POSITION UPDATE EXPIRES. Held positions already merge (same connection, newer stamp, right behind),
   and the loop's backpressure bounds how many messages wait; a position that has waited kHeldPosMaxSec behind a write that
   has not finished (a stuck disk) is DROPPED and counted - its owner's next position update or record supersedes it, the
   rule g_pendingPos already follows (kPendingPosMaxSec). Records and deletes never expire. Checked once a second. */
const double kHeldPosMaxSec = 600.0;
double g_sbExpireDue = 0.0;
long long g_sbExpireLines = 0;   /* M16 fold 4: expiry lines so far - the first 5 and every 100th are logged; heldExpired counts every update */
void SbHeldExpireTick()
{
    const double now = NowSec();
    if (now < g_sbExpireDue) return;
    g_sbExpireDue = now + 1.0;
    const long long n = g_wqGate.ExpireHeld(coopwq::kHeldPosition, now - kHeldPosMaxSec);
    if (n > 0 && cooplive::LiveLogThis(++g_sbExpireLines))   /* M16 fold 4: capped */
        Log("M16: " + N(n) + " held position update(s) waited over " + N((long long)kHeldPosMaxSec) + " s behind a record write that has not finished - DROPPED"
            " (each group's next position or record supersedes it; records and deletes are never dropped); counted heldExpired");
}
/* sendBound[waitNow,waitBytesNow,waitMax,congestedNow,direct,waited,coalesced,episodes,droppedAtClose,sendFailed] - frames waiting for
   all games now / their bytes / the most waiting for one game since the previous report line / games with frames waiting now;
   since start: frames sent straight to ENet, frames that waited, waiting MOVE/STATE replaced by a newer one, congestion
   episodes, frames thrown away with a closed connection, waiting frames ENet refused. heldExpired = held position updates
   dropped after kHeldPosMaxSec, since start. */
std::string SendBoundToken()
{
    long long waitNow = 0, waitBytes = 0, congested = 0, waitOne = 0;
    for (std::map<ENetPeer*, SbPeer>::const_iterator it = g_sb.begin(); it != g_sb.end(); ++it)
    {
        waitNow += it->second.box.Count(); waitBytes += it->second.box.Bytes();
        if (!it->second.box.Empty()) ++congested;
        if (it->second.box.Count() > waitOne) waitOne = it->second.box.Count();
    }
    const long long waitMax = g_sbWaitMax; g_sbWaitMax = waitOne;   /* M16 fold 4: restarts at what waits now, like the game side */
    return " sendBound[waitNow,waitBytesNow,waitMax,congestedNow,direct,waited,coalesced,episodes,droppedAtClose,sendFailed]=" + N(waitNow) + "," + N(waitBytes)
         + "," + N(waitMax) + "," + N(congested) + "," + N(g_sbDirect) + "," + N(g_sbWaited) + "," + N(g_sbCoalesced) + "," + N(g_sbEpisodes)
         + "," + N(g_sbDropped) + "," + N(g_sbSendFailed) + " heldExpired=" + N(g_wqGate.Expired());
}
ENetHost* g_wqHost = 0;
/* M13: bundle[packets,bundles,bundled,bytesSaved,single,alone,sendFailed,recvBundles,recvBundled,recvBad,gamesOn,udpPackets,udpBytes],
   since start - see src/common/sendbundle.h. packets = ENet packets sent to games (each bundle, each frame that went on its own,
   each shared relay packet once per game); bundles / bundled = bundles sent and the messages inside them; bytesSaved = their
   SavedBytes; single = open bundles that held one message (sent as its own frame); alone = frames too large to share a packet,
   or the handshake; sendFailed = messages in bundles ENet refused; recvBundles / recvBundled = bundles received from games and
   the messages in them; recvBad = bundles refused whole; gamesOn = games bundling was turned on for; udpPackets / udpBytes =
   UDP datagrams and bytes ENet put on the wire (ENet's own counters, harvested and reset here at each report). */
std::string BundleToken()
{
    if (g_wqHost != 0)
    {
        g_bnUdpPackets += (long long)g_wqHost->totalSentPackets; g_bnUdpBytes += (long long)g_wqHost->totalSentData;
        g_wqHost->totalSentPackets = 0; g_wqHost->totalSentData = 0;
    }
    return " bundle[packets,bundles,bundled,bytesSaved,single,alone,sendFailed,recvBundles,recvBundled,recvBad,gamesOn,udpPackets,udpBytes]="
         + N(g_bnPackets) + "," + N(g_bnTally.bundles) + "," + N(g_bnTally.bundled) + "," + N(g_bnTally.saved) + "," + N(g_bnTally.single)
         + "," + N(g_bnTally.alone) + "," + N(g_bnSendFailed) + "," + N(g_bnRecvBundles) + "," + N(g_bnRecvBundled) + "," + N(g_bnRecvBad)
         + "," + N(g_bnLinksOn) + "," + N(g_bnUdpPackets) + "," + N(g_bnUdpBytes);
}
double g_loopGapMaxMs = 0.0, g_loopBusyFromMs = -1.0;   /* LOOP THREAD ONLY */
/* THE BOUND (design-many M14: "a bounded queue"). With one job per record key out, the depth is at most the
   number of records being written at once; past this the loop waits for the writer to take one - the one
   wait on the disk left on the loop, and it shows in loopGapMaxMs. */
const long long kWriteQueueMax = 4096;
/* M14 fold (review finding 3): THE HELD MESSAGES' BOUND. A message for a record whose write is out is held (the
   gate), and before this fold nothing capped them. Now: position-only updates merge (coopwq::HeldMergeable),
   and past kHeldMax messages or kHeldBytesMax bytes held the loop STOPS HANDLING NEW NETWORK EVENTS
   (HeldBackpressure) - applying completions, which is what drains them - until they are down to the RESUME
   marks. M14 fold 2 (re-check A): the network is still SERVICED meanwhile (acknowledgements, pings - every
   game's link stays alive), and every event taken waits, unhandled and in arrival order, in g_deferred; once
   it is off they are handled first, before anything newly read. Nothing is dropped or reordered. Logged once
   when it starts and once when it ends. */
const long long kHeldMax = 2048, kHeldResume = 1024;
const long long kHeldBytesMax = 64LL * 1024 * 1024, kHeldBytesResume = 32LL * 1024 * 1024;
int g_heldBp = 0; long long g_heldBpEpisodes = 0; double g_heldBpFrom = 0.0;   /* LOOP THREAD ONLY */
/* M14 fold 2 (re-check A): the events taken while backpressure is on (see LoopService). Past kDeferredMax events or
   kDeferredBytesMax bytes the network is not serviced (as before this fold) until it is off - nothing is dropped. */
coopwq::DeferQueue<ENetEvent> g_deferred;   /* LOOP THREAD ONLY */
const long long kDeferredMax = 8192, kDeferredBytesMax = 64LL * 1024 * 1024;
int g_deferCapped = 0; long long g_deferRefused = 0, g_deferDiscarded = 0;   /* LOOP THREAD ONLY */
void WriterFlushAll();
void WriterApplyDone();

/* WRITER THREAD. The calls the loop made before M14, unchanged, so every failure is counted and logged by the
   same lines (WriteTempFile / RotatePair / SwapIn / WriteMeta; Log() is locked for this). */
void RunJob(WriteJob* j)
{
    if (j->kind == kJobDelete)
    {
        /* decision 28, and P8c (review-p8b M-2): the fallback copy goes with the record it was a copy of -
           see OnRecordGone. */
        DeleteFileA(FileOf(j->key).c_str()); DeleteFileA(MetaOf(j->key).c_str());
        DeleteFileA(PrevOf(FileOf(j->key)).c_str()); DeleteFileA(PrevOf(MetaOf(j->key)).c_str());
        DeleteFileA((FileOf(j->key) + ".refused").c_str());
        j->ok = true;   /* as before M14, a delete's results are not checked */
        return;
    }
    j->ok = CommitRecord(j->rec, j->hasPayload ? &j->payload : 0, j->rotate);
}
DWORD WINAPI WriterMain(LPVOID)
{
    for (;;)
    {
        WaitForSingleObject(g_wqWork, INFINITE);
        for (;;)
        {
            WriteJob* j = 0;
            EnterCriticalSection(&g_wqLock.cs);
            const bool got = g_wq.Pop(&j);   /* rule (1): strictly in queue order, one at a time */
            if (!got) SetEvent(g_wqIdle);    /* rule (3): IDLE = nothing queued AND nothing in hand, set under the lock */
            LeaveCriticalSection(&g_wqLock.cs);
            if (!got) break;
            RunJob(j);
            /* rule (4): A FAILED JOB IS COUNTED AND HANDED BACK FOR ITS OWN LOG LINE, and the next job is taken
               exactly as after a success - nothing queued behind a failure is dropped or reordered. */
            EnterCriticalSection(&g_wqLock.cs);
            g_wq.Done(j->ok);
            g_wqDone.push_back(j);
            LeaveCriticalSection(&g_wqLock.cs);
            SetEvent(g_wqSpace);
        }
    }
}
bool WriterStart(ENetHost* host)
{
    g_wqHost = host;
    g_wqWork = CreateEventA(0, FALSE, FALSE, 0);
    g_wqIdle = CreateEventA(0, TRUE, TRUE, 0);
    g_wqSpace = CreateEventA(0, FALSE, FALSE, 0);
    if (g_wqWork == 0 || g_wqIdle == 0 || g_wqSpace == 0) return false;
    DWORD tid = 0;
    HANDLE t = CreateThread(0, 0, WriterMain, 0, 0, &tid);
    if (t == 0) return false;
    CloseHandle(t);
    return true;
}
/* LOOP THREAD. rule (1), THE LOOP'S HALF: a key with a job out never gets a second one - every caller checked
   g_wqGate.Busy and HELD its message instead - so two writes to one record's files are never queued together,
   and the one writer takes the queue strictly in order. */
void WriterQueue(WriteJob* j)
{
    if (!g_wqGate.Start(j->gate))
        Log("M14: INTERNAL - a second write was queued for " + SanitizeForLog(j->key) + " while one was out; queued behind it (the writer still takes them in order)");
    EnterCriticalSection(&g_wqLock.cs);
    while (g_wqClosing || g_wq.Depth() >= kWriteQueueMax)
    {
        /* rule (3): ONCE THE QUIT DRAIN HAS BEGUN, NOTHING NEW IS QUEUED. The loop stops here and the process
           ends under it; this write's effects (echo, forward, memory) were never applied, so nothing was
           announced that did not reach the disk. */
        if (g_wqClosing) { LeaveCriticalSection(&g_wqLock.cs); for (;;) Sleep(INFINITE); }
        LeaveCriticalSection(&g_wqLock.cs);
        WaitForSingleObject(g_wqSpace, INFINITE);
        EnterCriticalSection(&g_wqLock.cs);
    }
    g_wq.Push(j);
    ResetEvent(g_wqIdle);
    LeaveCriticalSection(&g_wqLock.cs);
    SetEvent(g_wqWork);
}
/* peerName: the sender's name when its message ARRIVED (M14 fold, finding 9) - empty for the loop's own writes. */
void WriterQueueCommit(int fromKind, const Record& rec, const std::vector<char>* payload, int rotate, ENetPeer* peer, unsigned connect, const Record& fwd, const std::string& peerName)
{
    WriteJob* j = new WriteJob;
    j->kind = kJobCommit; j->from = fromKind; j->key = rec.worldId; j->gate = coopwq::GateKey(rec.worldId); j->rec = rec;
    if (payload != 0) { j->payload = *payload; j->hasPayload = true; }
    j->rotate = rotate;
    j->peer = peer; j->peerConnect = connect; j->fwd = fwd;
    j->peerName = peerName;
    WriterQueue(j);
}
/* M14 fold (finding 2): the delete carries what its completion needs - the stamp and sender for the DELETE line,
   and the message itself (payload) for the broadcast - because both now happen once the files are gone. */
void WriterQueueDelete(const std::string& wid, long long stamp, ENetPeer* peer, unsigned connect, const std::string& peerName, const std::vector<char>& msg)
{
    WriteJob* j = new WriteJob;
    j->kind = kJobDelete; j->from = kFromDelete; j->key = wid; j->gate = coopwq::GateKey(wid);
    j->stamp = stamp; j->peer = peer; j->peerConnect = connect; j->peerName = peerName; j->payload = msg;
    WriterQueue(j);
}
void WriterHold(const std::string& id, unsigned char type, ENetPeer* peer, unsigned connect, const std::string& peerName, const std::vector<char>& payload, int heldKind, long long stamp)
{
    HeldMsg m; m.type = type; m.peer = peer; m.connect = connect; m.peerName = peerName; m.payload = payload;
    coopwq::HeldTag t; t.kind = heldKind; t.sender = peer; t.connect = connect; t.stamp = stamp; t.bytes = (long long)payload.size();
    t.id = id;   /* M14 fold 2 (re-check C): a merge needs the same RECORD, not only the same gate key */
    t.at = NowSec();   /* M16: a held position update expires after kHeldPosMaxSec (SbHeldExpireTick) */
    g_wqGate.Hold(coopwq::GateKey(id), m, t);   /* true = it replaced an older held position (counted: heldMerged) */
}
/* rule (3): QUIT DRAINS THE QUEUE - QuitDrain, through two doors:
     - StoreCtrlHandler: the console's close button, Ctrl+C and Ctrl+Break (on a thread of the system's).
     - the hidden window (QuitWindowMain): LOGOFF and SHUTDOWN. M14 fold (review finding 1): this exe loads
       SHELL32 (the Recycle Bin, restore1b1), and SHELL32 loads USER32; Windows does NOT call a console handler
       with CTRL_LOGOFF_EVENT / CTRL_SHUTDOWN_EVENT in a process that has loaded USER32 or GDI32 (Read: the
       SetConsoleCtrlHandler documentation). Such a process hears a session end as WM_QUERYENDSESSION /
       WM_ENDSESSION on its top-level windows - and a message-only window gets neither - so this is an
       ordinary top-level window that is never shown, on a thread of its own.
   This process has no other way to end - its loop never returns. New writes are refused from this moment
   (WriterQueue), every queued one is written, and only then does the process end. THE WAIT IS BOUNDED
   (kQuitWaitMs, M14 fold finding 10): a writer stuck on a dead disk does not hold the process for ever, and
   what was left is logged. Windows may cut either door short (Inferred: about 5 s for the close button, a few
   seconds at a session end unless the user waits on the "still running" screen); a queue that takes longer is
   cut off there. Nothing cut off was ever announced: echoes, forwards and delete broadcasts follow a landed job. */
const DWORD kQuitWaitMs = 60000;
void QuitDrain(const std::string& why)
{
    EnterCriticalSection(&g_wqLock.cs);
    g_wqClosing = 1;
    const long long left = g_wq.Depth();
    LeaveCriticalSection(&g_wqLock.cs);
    Log("quit (" + why + "): " + N(left) + " write(s) queued - every one is written before this process ends (waiting at most " + N((long long)(kQuitWaitMs / 1000)) + " s)");
    if (g_wqIdle == 0) { Log("quit: the writer never started - nothing was queued; exiting"); return; }
    if (WaitForSingleObject(g_wqIdle, kQuitWaitMs) == WAIT_OBJECT_0) { Log("quit: the write queue is empty - every accepted write is on disk; exiting"); return; }
    EnterCriticalSection(&g_wqLock.cs);
    const long long still = g_wq.Depth();
    LeaveCriticalSection(&g_wqLock.cs);
    Log("quit: WRITER STUCK - " + N(still) + " write(s) still queued or being written after " + N((long long)(kQuitWaitMs / 1000))
        + " s (the one in hand is waiting on the disk); exiting WITHOUT them. None of them was echoed, forwarded or broadcast, so no game was told they landed");
}
/* owner decision 183: THE PARENT KENSHI'S WATCH - a thread of its own waits on the parent's process handle (SYNCHRONIZE) and, when it
   is signalled (the process ended, however), quits through QuitDrain like the close button does and ends the process. */
unsigned long g_parentPid = 0;   /* 0 = no --parent: not watched, as before */
void ParentGoneQuit()
{
    Log("[STORE] parent Kenshi (pid " + N((long long)g_parentPid) + ") exited - closing");
    QuitDrain("parent Kenshi exited");
    ExitProcess(0);
}
DWORD WINAPI ParentWatchMain(LPVOID p)
{
    const DWORD w = WaitForSingleObject((HANDLE)p, INFINITE);
    if (w != WAIT_OBJECT_0)
    {
        Log("[STORE] parent Kenshi (pid " + N((long long)g_parentPid) + ") watch FAILED (wait result " + N((long long)w) + ", GetLastError="
            + N((long long)::GetLastError()) + ") - this helper no longer closes with it");
        return 1;
    }
    ParentGoneQuit();
    return 0;
}
void ParentWatchStart()
{
    if (g_parentPid == 0) return;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)g_parentPid);
    const DWORD err = h == 0 ? ::GetLastError() : 0;
    const int v = coopprof::ParentWatchDecide(h != 0, (unsigned long)err);
    if (v == coopprof::kParentGone) ParentGoneQuit();
    if (v == coopprof::kParentNotWatched)
    {
        Log("[STORE] parent Kenshi (pid " + N((long long)g_parentPid) + ") could NOT be watched (OpenProcess GetLastError=" + N((long long)err)
            + ") - this helper runs as a standalone one and does not close with it");
        return;
    }
    DWORD tid = 0;
    HANDLE t = CreateThread(0, 0, ParentWatchMain, h, 0, &tid);
    if (t == 0)
    {
        Log("[STORE] parent Kenshi (pid " + N((long long)g_parentPid) + ") could NOT be watched (CreateThread GetLastError=" + N((long long)::GetLastError())
            + ") - this helper runs as a standalone one and does not close with it");
        CloseHandle(h);
        return;
    }
    CloseHandle(t);   /* the handle h stays open for the thread's whole life */
    Log("[STORE] parent Kenshi (pid " + N((long long)g_parentPid) + ") watched - this helper closes when it exits");
}
BOOL WINAPI StoreCtrlHandler(DWORD type)
{
    QuitDrain("console event " + N((long long)type));
    return FALSE;   /* the default handler ends the process */
}
LRESULT CALLBACK QuitWindowProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_QUERYENDSESSION)
    {
        /* Say why the session waits, where Windows has the call (Vista and later - looked up, so this XP-level
           build still links). The session end is never vetoed: the drain runs at WM_ENDSESSION. */
        typedef BOOL (WINAPI *ReasonFn)(HWND, LPCWSTR);
        const ReasonFn f = (ReasonFn)GetProcAddress(GetModuleHandleA("user32.dll"), "ShutdownBlockReasonCreate");
        if (f != 0) f(h, SW_SERVER_TITLE_W L": saving the world");
        return TRUE;
    }
    if (msg == WM_ENDSESSION)
    {
        if (wp != 0) QuitDrain(((unsigned long long)lp & 0x80000000ULL) != 0 ? "session end - logoff" : "session end - shutdown");
        return 0;   /* the system ends the process after this returns */
    }
    return DefWindowProcA(h, msg, wp, lp);
}
DWORD WINAPI QuitWindowMain(LPVOID)
{
    WNDCLASSA wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = QuitWindowProc; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = swnames::kServerQuitClass;
    if (RegisterClassA(&wc) == 0) { Log("M14: could not register the shutdown window (GetLastError=" + N((long long)::GetLastError()) + ") - a logoff or shutdown may lose queued writes"); return 1; }
    /* top-level (NOT HWND_MESSAGE - a message-only window hears no session end), and never shown */
    HWND h = CreateWindowExA(0, swnames::kServerQuitClass, swnames::kServerTitle, WS_OVERLAPPED, 0, 0, 0, 0, 0, 0, wc.hInstance, 0);
    if (h == 0) { Log("M14: could not create the shutdown window (GetLastError=" + N((long long)::GetLastError()) + ") - a logoff or shutdown may lose queued writes"); return 1; }
    MSG m;
    while (GetMessageA(&m, 0, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageA(&m); }
    return 0;
}
bool QuitWindowStart()
{
    DWORD tid = 0;
    HANDLE t = CreateThread(0, 0, QuitWindowMain, 0, 0, &tid);
    if (t == 0) return false;
    CloseHandle(t);
    return true;
}
/* M14 fold (finding 3): THE BACKPRESSURE SWITCH - see kHeldMax. On past either limit, off at both resume marks. */
bool HeldBackpressure()
{
    const long long held = g_wqGate.HeldCount(), bytes = g_wqGate.HeldBytes();
    if (!g_heldBp && (held > kHeldMax || bytes > kHeldBytesMax))
    {
        g_heldBp = 1; ++g_heldBpEpisodes; g_heldBpFrom = NowSec();
        Log("M14: BACKPRESSURE ON - " + N(held) + " message(s), " + N(bytes) + " bytes, are held behind writes still out (limits " + N(kHeldMax) + " / "
            + N(kHeldBytesMax) + " bytes). The network is still serviced (links stay alive), but every event taken is DEFERRED, unhandled and in arrival order, until the held ones are down to " + N(kHeldResume) + " / " + N(kHeldBytesResume)
            + " bytes - nothing is dropped or reordered");
    }
    else if (g_heldBp && held <= kHeldResume && bytes <= kHeldBytesResume)
    {
        g_heldBp = 0;
        Log("M14: BACKPRESSURE OFF after " + N((long long)((NowSec() - g_heldBpFrom) * 1000.0 + 0.5)) + " ms - " + N(held) + " message(s), " + N(bytes) + " bytes held; " + N(g_deferred.Count()) + " deferred event(s) are handled first, in arrival order");
    }
    return g_heldBp != 0;
}
/* THE LOOP'S ONE WAIT, and where its stalls are measured. loopGapMaxMs = the longest stretch the loop spent
   AWAY from the socket (one message, the once-a-second jobs, applying completions) since the previous report
   line. Completions are applied here, before every wait - on the loop's own passes, never on a timer of
   their own. M15 (T-197): waitMs = the longest this call may block (PassBudget::WaitMs - what is left of the
   current pass, which never reaches past the moment the once-a-second jobs fall due). */
int LoopService(ENetHost* host, ENetEvent* ev, int waitMs)
{
    WriterApplyDone();
    const double now = NowSec() * 1000.0;
    if (g_loopBusyFromMs >= 0.0 && now - g_loopBusyFromMs > g_loopGapMaxMs) g_loopGapMaxMs = now - g_loopBusyFromMs;
    /* M14 fold (finding 3): BACKPRESSURE - too much is held; completions are waited for (and applied on the next
       call), and what they sent goes out. M14 fold 2 (re-check A): the network is STILL SERVICED (timeout 0), so
       acknowledgements, pings and resends keep every game's link alive (Read: ENet ends a link whose reliable
       traffic goes unacknowledged past its timeout, 5 s at the least), but EVERY event taken is DEFERRED
       (g_deferred), unhandled, in arrival order - a RECEIVE keeps its packet, and CONNECT and DISCONNECT go into
       the same queue because their handlers touch the loop's per-connection state (the HELLO deadline, PeerGone).
       A CONNECT into a slot that still has an older connection's events deferred is REFUSED (the game connects
       again): handled later, those events would otherwise send their replies to the new game. Past kDeferredMax
       events or kDeferredBytesMax bytes the network is not serviced, as before this fold. g_loopBusyFromMs is NOT
       reset here, so the whole stretch shows in loopGapMaxMs. Returns 0, so the once-a-second jobs keep running. */
    if (HeldBackpressure())
    {
        ENetEvent e;
        while (g_deferred.Count() < kDeferredMax && g_deferred.Bytes() < kDeferredBytesMax && enet_host_service(host, &e, 0) > 0)
        {
            if (e.type == ENET_EVENT_TYPE_CONNECT && g_deferred.HasFor(e.peer))
            {
                ++g_deferRefused;
                Log("M14: a new connection from " + PeerName(e.peer) + " arrived in a slot whose previous connection still has events deferred"
                    " - refused (the game connects again), so those events never answer it. Counted deferred[refused].");
                enet_peer_disconnect_now(e.peer, 0);   /* resets the peer with NO event */
                continue;
            }
            g_deferred.Push(e, e.peer, (e.type == ENET_EVENT_TYPE_RECEIVE && e.packet != 0) ? (long long)e.packet->dataLength : 0);
        }
        if (!g_deferCapped && (g_deferred.Count() >= kDeferredMax || g_deferred.Bytes() >= kDeferredBytesMax))
        {
            g_deferCapped = 1;
            Log("M14: " + N(g_deferred.Count()) + " deferred event(s), " + N(g_deferred.Bytes()) + " bytes - at the limit (" + N(kDeferredMax) + " / "
                + N(kDeferredBytesMax) + " bytes): the network is NOT serviced until backpressure is off (nothing is dropped; links may time out)");
        }
        SbDrainAll(); SbHeldExpireTick();   /* M16: waiting frames still go out, and stuck held positions expire, while backpressure is on */
        SbSealAll();   /* M13: and every game's open bundles, before the flush */
        enet_host_flush(host);
        WaitForSingleObject(g_wqSpace, (DWORD)waitMs);   /* M15: no longer a fixed 50 ms - never past the jobs' due time */
        return 0;
    }
    g_deferCapped = 0;
    /* M14 fold 2: what was deferred is handed out first, in arrival order, one per call - nothing new is read
       until it is all handled (backpressure coming back on defers new events behind the rest). */
    if (g_deferred.Pop(ev))
    {
        g_loopBusyFromMs = NowSec() * 1000.0;
        return 1;
    }
    /* M14 fold (finding 5): while any write is out, a SHORT wait, so its completion (echo, forward) is picked up
       within a few ms instead of up to 50.
       M15 (T-197), M14's leftover checked: NO TRUE WAKE IS NEEDED. BusyCount counts a key from WriterQueue
       (g_wqGate.Start) until its completion is APPLIED (WriterApplyOne: g_wqGate.Finish) - so from the moment a
       write is queued until its echo/forward has gone out, every wait here is 2 ms at most; the loop never sits
       in a long wait with a save landed and unannounced. Otherwise the wait is what is left of the pass. */
    SbDrainAll(); SbHeldExpireTick();   /* M16: what waits for each game goes out as its queue drains */
    /* M13: an event ENet already holds is handed out without transmitting anything (enet_host_service would return it the same
       way, before sending). Only when none is left are the open bundles sealed and the service run - its first step is to
       transmit - so everything this pass produced for one game leaves as one packet per lane. */
    int got = enet_host_check_events(host, ev);
    if (got == 0)
    {
        SbSealAll();
        got = enet_host_service(host, ev, (enet_uint32)((g_wqGate.BusyCount() > 0 && waitMs > 2) ? 2 : waitMs));
    }
    g_loopBusyFromMs = NowSec() * 1000.0;
    return got;
}
/* rule (5): writeQueue[depth,maxDepth,jobs,failed,held,heldMax] - depth = queued + being written now; maxDepth =
   the deepest since the previous report line; jobs / failed = every job the writer has finished / those whose
   write failed, since start; held = messages held behind a write that is out, now; heldMax = the most held at
   once since the previous report line (M14 fold). heldMerged = held position updates replaced by a newer one
   from the same connection, since start; heldBackpressure[episodes,on,deferred,deferredMax] = times the loop stopped handling the
   network because too much was held, since start, and whether it is stopped now; deferred = network events taken meanwhile and not yet handled, now;
   deferredMax = the most at once since the previous report line (fold 2). deferred[total,refused,discarded] = every
   event deferred / new connections refused into a slot with events deferred / events of a connection this server
   closed itself, thrown away as the network layer would have, since start. loopGapMaxMs = the loop's
   longest stall since the previous report line (a backpressure stretch included). */
std::string WriteQueueToken()
{
    EnterCriticalSection(&g_wqLock.cs);
    const long long depth = g_wq.Depth(), maxDepth = g_wq.TakeMaxDepth(), jobs = g_wq.Jobs(), failed = g_wq.Failed();
    LeaveCriticalSection(&g_wqLock.cs);
    const long long gap = (long long)(g_loopGapMaxMs + 0.5);
    g_loopGapMaxMs = 0.0;
    return " writeQueue[depth,maxDepth,jobs,failed,held,heldMax]=" + N(depth) + "," + N(maxDepth) + "," + N(jobs) + "," + N(failed)
         + "," + N(g_wqGate.HeldCount()) + "," + N(g_wqGate.TakeHeldMax())
         + " heldMerged=" + N(g_wqGate.Merged()) + " heldBackpressure[episodes,on,deferred,deferredMax]=" + N(g_heldBpEpisodes) + "," + N((long long)g_heldBp)
         + "," + N(g_deferred.Count()) + "," + N(g_deferred.TakeMax())
         + " deferred[total,refused,discarded]=" + N(g_deferred.Total()) + "," + N(g_deferRefused) + "," + N(g_deferDiscarded)
         + " loopGapMaxMs=" + N(gap);
}
/* M15 (T-197): THE ONCE-A-SECOND JOBS ON A FIXED SCHEDULE, AND A BUDGET FOR EACH PASS OF MESSAGE HANDLING
   (src/common/looptick.h; build/design-many.md M15). Before M15 the jobs ran only after the network wait came
   back empty - 50 ms with no message at all - which stops happening at ~30-50 players (server-load-read.md 4c).
   Now they are due every kTickPeriodUs on MonoUs's grid, and main's loop checks after EVERY event whether the
   pass is over: at kPassBudgetUs from its start, or when the jobs fall due, whichever is first; the wait inside
   a pass never reaches past that end either. So under a flood the jobs run at most ONE message late.
   THE BUDGET, 50 ms: the same 50 ms the loop always used as its idle wait, so a quiet pass and a busy pass have
   one length; it only matters for eventsPerPassMax (events handled in one 50 ms stretch = the burst the loop
   actually saw) and gives the loop's top a turn at least every 50 ms. On-time jobs come from the due check,
   not from the budget - a shorter budget would not make them any more punctual. */
const long long kTickPeriodUs = 1000000, kPassBudgetUs = 50000;
const int kIdleWaitMs = 50;
cooptick::FixedSchedule g_tick(kTickPeriodUs);   /* LOOP THREAD ONLY */
cooptick::PassBudget g_pass(kPassBudgetUs);      /* LOOP THREAD ONLY */
/* tick[late_ms_max,late_ms_avg,passes,eventsPerPassMax] - since the previous report line: how late the latest-
   running pass of the once-a-second jobs started against its fixed slot, and the average (ms, one decimal);
   how many passes ran (about 60 on a 60 s line - fewer means whole seconds were skipped, and each skip was
   logged); the most network events one pass handled. */
std::string MsTenths(long long us) { char b[32]; sprintf(b, "%.1f", (double)us / 1000.0); return b; }
std::string TickToken()
{
    const cooptick::TickStats t = g_tick.Take();
    return " tick[late_ms_max,late_ms_avg,passes,eventsPerPassMax]=" + MsTenths(t.lateMaxUs) + "," + MsTenths(t.passes > 0 ? t.lateSumUs / t.passes : 0)
         + "," + N(t.passes) + "," + N(g_pass.TakeMax());
}

/* THE HELD POSITION - see the note on g_pendingPos at the top of this file.
   The three counters are printed ON EVERY ONE OF THESE LINES and not only on the 60 s verdict token: a lock
   episode is usually over inside a few seconds, so a short run would otherwise print the events and a token
   of all zeros, and the two would have to be reconciled by hand. */
std::string PositionDeferredToken()
{
    return " positionDeferred[unreadable=" + N(g_positionDeferredUnreadable)
         + ",applied=" + N(g_positionDeferredApplied)
         + ",expired=" + N(g_positionDeferredExpired)
         + ",pending=" + N((long long)g_pendingPos.size()) + "]";
}
void HoldPosition(const std::string& id, const Record& from, const FileProbe& fp)
{
    PendingPos& q = g_pendingPos[id];
    const int first = (q.defers == 0) ? 1 : 0;
    if (first) q.firstAt = NowSec();
    q.x = from.x; q.y = from.y; q.z = from.z; q.posAt = from.posAt; ++q.defers;
    ++g_positionDeferredUnreadable;
    /* COUNTED BEFORE THE LINE IS PRINTED, so the token in it includes this event. Printed the other way
       round it says `unreadable=0` on the very line announcing the first deferral. */
    if (first)
    {
        Log("RECORD " + SanitizeForLog(id) + " position - HELD. Its record file is ON DISK (" + N(fp.attrLen)
            + " bytes) and cannot be read (error " + N((long long)fp.err) + "), and this notebook will not"
            " write an index line saying a group has no squad while that file is sitting there. THE NEWER"
            " POSITION IS KEPT and written as soon as the file opens - retried every second for "
            + N((long long)kPendingPosMaxSec) + " s. Before P8d it was DROPPED and nothing counted it."
            + PositionDeferredToken());
    }
}
void DropPendingIfSuperseded(const std::string& id, long long posAt)
{
    std::map<std::string, PendingPos>::iterator it = g_pendingPos.find(id);
    if (it != g_pendingPos.end() && it->second.posAt <= posAt) g_pendingPos.erase(it);
}
void PendingPosTick()
{
    if (g_pendingPos.empty()) return;
    const double now = NowSec();
    std::map<std::string, PendingPos>::iterator it = g_pendingPos.begin();
    while (it != g_pendingPos.end())
    {
        const std::string id = it->first;
        std::map<std::string, Record>::iterator rec = g_records.find(id);
        if (rec == g_records.end()) { g_pendingPos.erase(it++); continue; }   /* the record went away; so does its held update */
        /* M14 rule (2): A JOB FOR THIS RECORD IS OUT, so its file is not read now - it may be half way through
           its rotation. It is looked at again next second, after that job has landed. Nothing waits. */
        if (g_wqGate.Busy(coopwq::GateKey(id))) { ++it; continue; }
        const FileProbe fp = ProbeFile(FileOf(id));
        if (fp.kind == coopstore::kFileUnreadable)
        {
            if (now - it->second.firstAt < kPendingPosMaxSec) { ++it; continue; }   /* still shut - look again next second */
            const long long gaveUpAfter = it->second.defers;
            g_pendingPos.erase(it++);
            ++g_positionDeferredExpired;
            Log("RECORD " + SanitizeForLog(id) + " position - GIVEN UP after " + N((long long)kPendingPosMaxSec)
                + " s and " + N(gaveUpAfter) + " update(s): its record file has been on disk and"
                " unreadable that whole time (error " + N((long long)fp.err) + "). The position this notebook"
                " serves for that group stays at the last one it was able to WRITE, and the next update after"
                " the file opens carries a new one. Said once, here." + PositionDeferredToken());
            continue;
        }
        Record upd = rec->second;
        upd.x = it->second.x; upd.y = it->second.y; upd.z = it->second.z; upd.posAt = it->second.posAt;
        /* A LIVE READ AT THE MOMENT OF COMMITMENT (design principle 2): the length and checksum come from
           the file as it is NOW, not from what it was when the update arrived. */
        if (fp.kind == coopstore::kFileReadable && fp.len > 0) { upd.hasFile = true; upd.len = fp.len; upd.crc = fp.crc; }
        else { upd.hasFile = false; upd.len = 0; upd.crc = 0; }
        /* M14: WRITTEN ON THE WRITER THREAD. The record in memory, the dropped hold, its counter and its line
           follow in WriterApplyOne once the line is on disk - exactly what stood here. A failed write leaves
           the hold in place and it is retried next second, as before. */
        WriterQueueCommit(kFromHeldPosition, upd, 0, PositionRotateMode(upd), 0, 0, upd, std::string());
        ++it;
    }
}

/* ============ P8d - THE FOUR FILES OF ONE RECORD, PROBED ONCE, AND WHAT THEY MEAN ====================
   P8b gathered one fact per file ("did the read succeed") and P8c added a probe for one of the four. All
   four now carry their full three-way state into the pure decision, which is what lets ChooseVersion and
   UpgradeDecide refuse to write on an answer nobody has. Reading each payload to checksum it is the whole
   cost of validation at start: 33 KB across this folder today, 3.4 MB read once at 200 squads of 17 KB
   (design-save 1.2(c)). */
struct RecordProbes { FileProbe meta, payload, prevMeta, prevPayload; };
void ProbeRecord(const std::string& id, RecordProbes* p)
{
    p->meta        = ProbeFile(MetaOf(id));
    p->payload     = ProbeFile(FileOf(id));
    p->prevMeta    = ProbeFile(PrevOf(MetaOf(id)));
    p->prevPayload = ProbeFile(PrevOf(FileOf(id)));
}
/* ONE PLACE, so the four names cannot drift from the four files. Also the honest count of what the
   previous-version net actually reaches (review-p8c M-3) and of the zero-byte payloads (M-1). */
void CountProbes(const RecordProbes& p)
{
    if (p.meta.kind        == coopstore::kFileUnreadable) ++g_probeUnreadableMeta;
    if (p.payload.kind     == coopstore::kFileUnreadable) ++g_probeUnreadablePayload;
    if (p.prevMeta.kind    == coopstore::kFileUnreadable) ++g_probeUnreadableMetaPrev;
    if (p.prevPayload.kind == coopstore::kFileUnreadable) ++g_probeUnreadablePayloadPrev;
    if (p.prevPayload.kind == coopstore::kFileReadable)   ++g_indexPayloadPrevPresent;
    if (p.payload.kind == coopstore::kFileReadable && p.payload.len == 0) ++g_indexPayloadZeroLength;
}
std::string UnreadableList(const RecordProbes& p)
{
    std::string o;
    if (p.meta.kind        == coopstore::kFileUnreadable) o += (o.empty() ? "" : ", ") + std::string("<id>.meta (error " + N((long long)p.meta.err) + ", " + N(p.meta.attrLen) + " bytes)");
    if (p.payload.kind     == coopstore::kFileUnreadable) o += (o.empty() ? "" : ", ") + std::string("<id>.platoon (error " + N((long long)p.payload.err) + ", " + N(p.payload.attrLen) + " bytes)");
    if (p.prevMeta.kind    == coopstore::kFileUnreadable) o += (o.empty() ? "" : ", ") + std::string("<id>.meta.prev (error " + N((long long)p.prevMeta.err) + ", " + N(p.prevMeta.attrLen) + " bytes)");
    if (p.prevPayload.kind == coopstore::kFileUnreadable) o += (o.empty() ? "" : ", ") + std::string("<id>.platoon.prev (error " + N((long long)p.prevPayload.err) + ", " + N(p.prevPayload.attrLen) + " bytes)");
    return o.empty() ? std::string("(none)") : o;
}
void FactsFromProbes(const std::string& id, const RecordProbes& p, const coopstore::MetaLine& cur,
                     bool haveCurrentMeta, coopstore::FolderFacts* ff)
{
    ff->metaState = p.meta.kind;
    if (haveCurrentMeta) { ff->haveMeta = 1; ff->meta = cur; }
    ff->prevMetaState = p.prevMeta.kind;
    {
        std::string pl; coopstore::MetaLine pm;
        /* The id guard is not decoration: the promotion RENAMES files derived from the line it adopts, so
           a .meta.prev naming some other record would move the wrong file. It is created by renaming
           <id>.meta and can only name <id> - which is exactly why it is cheap to check. */
        if (FirstLineOf(p.prevMeta, &pl) && coopstore::MetaParse(pl, &pm) && pm.worldId == id) { ff->havePrevMeta = 1; ff->prevMeta = pm; }
    }
    ff->payloadState = p.payload.kind;
    if (p.payload.kind == coopstore::kFileReadable) { ff->payloadLen = p.payload.len; ff->payloadCrc = p.payload.crc; }
    ff->prevPayloadState = p.prevPayload.kind;
    if (p.prevPayload.kind == coopstore::kFileReadable) { ff->prevPayloadLen = p.prevPayload.len; ff->prevPayloadCrc = p.prevPayload.crc; }
}
/* True when this name ends in `suffix`, case-insensitively. FindFirstFileA("*.meta") also matches
   <id>.meta.prev through a generated 8.3 short name, and FindFirstFileA("*.meta.prev") matches nothing else,
   so BOTH walks below test the real suffix rather than trusting the pattern. */
bool NameEndsWith(const std::string& name, const char* suffix)
{
    const size_t sn = strlen(suffix);
    if (name.size() <= sn) return false;
    return _stricmp(name.c_str() + (name.size() - sn), suffix) == 0;
}
/* P8c: the one place a record enters the index, so the position-only count cannot drift from `loaded`.
   POSITION-ONLY means len 0 - the line says there is no payload and that is not a failure. A record whose
   payload was REFUSED has len > 0 and is counted by refusedTorn/Crc/Missing instead, and a record whose
   upgrade was DEFERRED is entered directly below, without this counter, because its file is on disk. */
void PutInIndex(const Record& r)
{
    g_records[r.worldId] = r;
    if (r.len <= 0) ++g_indexPositionOnly;
    /* B12 (decision 52): THE ONE PLACE A RECORD ENTERS THE INDEX IS THE ONE PLACE THE SEQUENCE COUNTER IS
       RAISED, so a restart cannot re-issue a number some record already carries - which would make two
       different writes compare equal and let a replay win a conflict it must lose. */
    if (r.seq >= g_seqNext) g_seqNext = r.seq + 1ULL;
}
/* B12: THE STAMP. One number per accepted record, never reused, never handed out twice. */
unsigned long long NextSeq() { ++g_seqStamped; return g_seqNext++; }
/* restore1a (design s1; fold review-restore1a): world.gen, read once at start (after the index, so the index high-water is
   known) and written on every raise - the temp file FLUSHED, then moved over with MOVEFILE_WRITE_THROUGH (review LOW). */
std::string WorldGenFile() { return g_dir + "\\world.gen"; }
bool WriteWorldGen()
{
    const std::string path = WorldGenFile(), tmp = path + ".tmp", body = restoreguard::BookFormat(g_wg);
    HANDLE h = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) { Log("world.gen: could not open " + tmp + " (GetLastError=" + N((long long)::GetLastError()) + ")"); return false; }
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, body.data(), (DWORD)body.size(), &wrote, 0);
    if (ok && wrote != (DWORD)body.size()) ok = FALSE;
    if (ok) ok = FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) { Log("world.gen: the write to " + tmp + " failed - " + path + " is unchanged"); return false; }
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    { Log("world.gen: could not replace " + path + " (MoveFileExA, GetLastError=" + N((long long)::GetLastError()) + ")"); return false; }
    return true;
}
void LoadWorldGen()
{
    std::ifstream f(WorldGenFile().c_str());
    if (!f) { g_wgFileState = 0; Log("world.gen: none in this folder - no profile is numbered yet (a save with no world save number loads: noGen)"); return; }
    std::string text, line;
    while (std::getline(f, line)) text += line + "\n";
    restoreguard::WorldBook b;
    if (!restoreguard::BookParse(text, &b)) { g_wgFileState = 2; Log("world.gen: UNREADABLE - treated as absent; the next WORLD_SAVED rewrites it"); return; }
    g_wg = b; g_wgFileState = 1;
    if (g_wg.seqHigh >= g_seqNext) g_seqNext = g_wg.seqHigh + 1ULL;   /* review-restore1a LOW: a number world.gen has seen is never issued again */
    Log("world.gen: " + N((long long)restoreguard::NumberedCount(g_wg)) + " numbered profile(s), highest worldGen=" + N((long long)restoreguard::MaxGen(g_wg))
        + " seqHigh=" + N((long long)g_wg.seqHigh) + " epoch=" + N((long long)g_wg.epoch) + " (seqNext now " + N((long long)g_seqNext) + ")");
}
/* restore1b1 (design s2 Q1, s4 first bullet): own_high.txt (temp + rename, like world.gen), OWN_HIGH, the repair copies and the
   WORLD_SAVED broadcast. */
std::string OwnHighFile() { return g_dir + "\\own_high.txt"; }
bool WriteOwnHigh()
{
    const std::string path = OwnHighFile(), tmp = path + ".tmp", body = restoreguard::OwnBookFormat(g_oh);
    HANDLE h = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) { Log("own_high.txt: could not open " + tmp + " (GetLastError=" + N((long long)::GetLastError()) + ")"); return false; }
    DWORD wrote = 0;
    BOOL ok = body.empty() ? TRUE : WriteFile(h, body.data(), (DWORD)body.size(), &wrote, 0);
    if (ok && wrote != (DWORD)body.size()) ok = FALSE;
    if (ok) ok = FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) { Log("own_high.txt: the write to " + tmp + " failed - " + path + " is unchanged"); return false; }
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    { Log("own_high.txt: could not replace " + path + " (MoveFileExA, GetLastError=" + N((long long)::GetLastError()) + ")"); return false; }
    return true;
}
void LoadOwnHigh()
{
    std::ifstream f(OwnHighFile().c_str());
    if (!f) { Log("own_high.txt: none in this folder - no player's records have a number yet (a game is never refused for them: fail open)"); return; }
    std::string text, line;
    while (std::getline(f, line)) text += line + "\n";
    int bad = 0; restoreguard::OwnBookParse(text, &g_oh, &bad); g_ohBadLines = bad;
    Log("own_high.txt: " + N((long long)g_oh.high.size()) + " row(s)" + (bad != 0 ? ", " + N((long long)bad) + " unreadable line(s) skipped" : std::string()));
}
void OnOwnHigh(ENetPeer* from, const std::vector<char>& payload)
{
    restoreguard::OwnHighMsg in;
    if (!restoreguard::DecodeOwnHigh(payload.empty() ? 0 : &payload[0], payload.size(), &in) || in.kind != restoreguard::kOhUp)
    { ++g_ohMalformed; Log("malformed OWN_HIGH from " + PeerName(from) + " (" + N((long long)payload.size()) + " bytes) - ignored"); return; }
    const std::string prof = coopprof::ProfileIdOfKey(PeerIdOf(from));
    if (!restoreguard::ProfileIdOk(prof)) { ++g_ohMalformed; Log("OWN_HIGH from " + PeerName(from) + ", which plays no admitted profile - ignored"); return; }
    ++g_ohIn;
    const unsigned long long before = restoreguard::OwnHighOf(g_oh, prof, in.store);
    if (in.high < before)
    {
        ++g_ohBehind;
        Log("own_high: " + PeerName(from) + " profile " + prof + " store " + in.store + " reports records up to seq=" + N((long long)in.high)
            + ", BELOW the " + N((long long)before) + " this notebook remembers - an old records folder was put back; the answer makes that game refuse itself");
    }
    if (restoreguard::MergeOwnHigh(&g_oh, prof, in.store, in.high))
    {
        ++g_ohRaised;
        if (!WriteOwnHigh()) { ++g_ohWriteFailed; Log("own_high.txt: WRITE FAILED - the raised number holds in memory until the next report"); }
    }
    restoreguard::OwnHighMsg a; a.kind = restoreguard::kOhDown; a.store = in.store; a.high = restoreguard::OwnHighOf(g_oh, prof, in.store);
    std::vector<char> b; restoreguard::EncodeOwnHigh(&b, a);
    SendMsg(from, MSG_OWN_HIGH, b);
}
void SendOwnHighPush(ENetPeer* to)
{
    const std::vector<std::pair<std::string, unsigned long long> > rows = restoreguard::OwnRowsOf(g_oh, coopprof::ProfileIdOfKey(PeerIdOf(to)));
    for (size_t i = 0; i < rows.size(); ++i)
    {
        restoreguard::OwnHighMsg a; a.kind = restoreguard::kOhDown; a.store = rows[i].first; a.high = rows[i].second;
        std::vector<char> b; restoreguard::EncodeOwnHigh(&b, a);
        SendMsg(to, MSG_OWN_HIGH, b); ++g_ohPushed;
    }
}
/* M4 fold (review 2026-09-29 H1, protocol 58) + OWNER DECISION 205 A: SEATS, UID_BLOCK (52) and uid_seats.txt
   (src/common/uidblock.h). A uid's 10-bit part is the SEAT its making game holds among the games connected NOW: the lowest free
   seat whose counters are not spent, taken right after the game's WELCOME is sent (SeatAssign; again at an ASK from a game
   holding none) and freed in PeerGone - a clean unlink, an ENet timeout, an eviction (B13-c) and the HELLO deadline all end
   there. Per seat this notebook keeps the highest uid counter it has ever handed out, for the world's life, and grants blocks
   above it, so a new player on a freed seat and a restarted game never repeat a uid. The file is written (temp + rename, like
   own_high.txt) BEFORE a grant is sent: a block that is not on disk is never handed out. It is NOT one of
   restoreguard::RepairCopyFiles on purpose - a repair must never step this number back. An unreadable file grants nothing
   (FAILED, counted uidBlocks[refusedUnreadable]) rather than guess a number that could repeat uids. */
std::string UidBlocksFile() { return g_dir + "\\uid_seats.txt"; }
bool WriteUidBlocks(const coopuidblk::UidBlockBook& book)
{
    const std::string path = UidBlocksFile(), tmp = path + ".tmp", body = coopuidblk::UidBlockBookFormat(book);
    HANDLE h = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) { Log("uid_seats.txt: could not open " + tmp + " (GetLastError=" + N((long long)::GetLastError()) + ")"); return false; }
    DWORD wrote = 0;
    BOOL ok = body.empty() ? TRUE : WriteFile(h, body.data(), (DWORD)body.size(), &wrote, 0);
    if (ok && wrote != (DWORD)body.size()) ok = FALSE;
    if (ok) ok = FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) { Log("uid_seats.txt: the write to " + tmp + " failed - " + path + " is unchanged"); return false; }
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    { Log("uid_seats.txt: could not replace " + path + " (MoveFileExA, GetLastError=" + N((long long)::GetLastError()) + ")"); return false; }
    return true;
}
void LoadUidBlocks()
{
    std::ifstream f(UidBlocksFile().c_str());
    if (!f)
    {
        if (::GetFileAttributesA(UidBlocksFile().c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            g_ubUnreadable = true;
            Log("uid_seats.txt: EXISTS BUT COULD NOT BE OPENED - no uid block is granted until it can be read (a guessed number"
                " could repeat uids); make the file readable and restart this notebook");
            return;
        }
        Log("uid_seats.txt: none in this folder - every seat's uid counters start at 1 (a new world, or one from before the M4 fold)");
        return;
    }
    std::string text, line;
    while (std::getline(f, line)) text += line + "\n";
    int bad = 0; coopuidblk::UidBlockBookParse(text, &g_ub, &bad); g_ubBadLines = bad;
    if (bad != 0)
    {
        g_ubUnreadable = true;
        Log("uid_seats.txt: " + N((long long)bad) + " UNREADABLE line(s) - no uid block is granted until the file is repaired (a"
            " guessed number could repeat uids); each line is v1<TAB>seat<TAB>highest counter handed out");
    }
    Log("uid_seats.txt: " + N((long long)g_ub.high.size()) + " seat row(s)");
}
int SeatOf(ENetPeer* p)
{
    std::map<ENetPeer*, unsigned int>::const_iterator it = g_seatOf.find(p);
    return it == g_seatOf.end() ? -1 : (int)it->second;
}
void SeatTake(ENetPeer* p, unsigned int seat, const std::string& why)
{
    g_seatOf[p] = seat; g_seatsHeld.insert(seat); ++g_seatTaken;
    Log("seat: " + PeerName(p) + " takes uid seat " + N((long long)seat) + " (" + why + "); its counters continue above "
        + N((long long)coopuidblk::UidBlockHighOf(g_ub, seat)) + ", " + N((long long)g_seatsHeld.size()) + " seat(s) held");
}
/* the connection's seat, taking the lowest free one with counters left when it holds none; -1 = none free (counted and said once
   per connection) */
int SeatAssign(ENetPeer* p, const std::string& why)
{
    const int have = SeatOf(p);
    if (have >= 0) return have;
    const int s = coopuidblk::SeatPick(g_seatsHeld, g_ub);
    if (s < 0)
    {
        if (g_seatRefusedSaid.insert(p).second)
        {
            ++g_seatRefusedFull;
            Log("seat: " + PeerName(p) + " gets NO uid seat - all 1,024 are held by connected games (or every free one is spent). It"
                " plays on, but makes no new synced characters until a seat frees (it asks again every 5 s). Counted seats[refusedFull].");
        }
        return -1;
    }
    SeatTake(p, (unsigned int)s, why);
    return s;
}
void SeatFree(ENetPeer* p, const std::string& why)
{
    g_seatRefusedSaid.erase(p);
    std::map<ENetPeer*, unsigned int>::iterator it = g_seatOf.find(p);
    if (it == g_seatOf.end()) return;
    const unsigned int seat = it->second;
    g_seatsHeld.erase(seat); g_seatOf.erase(it); ++g_seatFreed;
    Log("seat: uid seat " + N((long long)seat) + " freed (" + PeerName(p) + ": " + why + "); its high-water "
        + N((long long)coopuidblk::UidBlockHighOf(g_ub, seat)) + " stays - the next holder continues above it");
}
void OnUidBlock(ENetPeer* from, const std::vector<char>& payload)
{
    coopuidblk::UidBlockMsg in;
    if (!coopuidblk::DecodeUidBlock(payload.empty() ? 0 : &payload[0], payload.size(), &in) || in.kind != (unsigned int)coopuidblk::kUbAsk)
    { ++g_ubMalformed; Log("malformed UID_BLOCK from " + PeerName(from) + " (" + N((long long)payload.size()) + " bytes) - ignored"); return; }
    if (SlotOf(from) < 0)
    { ++g_ubNotAdmitted; Log("UID_BLOCK ask from " + PeerName(from) + ", which is not admitted to this world - ignored"); return; }
    coopuidblk::UidBlockMsg a;
    int seat = SeatAssign(from, "at its UID_BLOCK ask - it held none");
    if (seat < 0)
    {
        ++g_ubNoSeat; a.kind = coopuidblk::kUbNoSeat;
        std::vector<char> nb; coopuidblk::EncodeUidBlock(&nb, a);
        SendMsg(from, MSG_UID_BLOCK, nb);
        return;
    }
    coopuidblk::UidBlockBook next = g_ub; unsigned int lo = 0, hi = 0;
    int r = g_ubUnreadable ? -2 : coopuidblk::UidBlockGrant(&next, (unsigned int)seat, coopuidblk::kUidBlockSize, &lo, &hi);
    if (r == 0)
    {   /* owner 205 A: its seat is spent - it moves to the lowest free seat with counters left, when there is one */
        const int other = coopuidblk::SeatPick(g_seatsHeld, g_ub);
        if (other >= 0)
        {
            SeatFree(from, "moved off - the seat's counters are spent");
            SeatTake(from, (unsigned int)other, "moved: its old seat's counters are spent");
            ++g_seatMovedSpent; seat = other;
            r = coopuidblk::UidBlockGrant(&next, (unsigned int)seat, coopuidblk::kUidBlockSize, &lo, &hi);
        }
    }
    a.slot = (unsigned int)seat;
    if (r == -1) { ++g_ubMalformed; return; }
    if (r == -2)
    {
        ++g_ubRefusedUnreadable; a.kind = coopuidblk::kUbFailed;
        /* M4 fold 2 (re-check M-A): every ask is answered FAILED while the file is unreadable - logged for the first 5 and every
           100th; the count is uidBlocks[refusedUnreadable] on the REPORT line */
        if (coopuid::LogRefusal(g_ubRefusedUnreadable))
            Log("uid block: " + PeerName(from) + " seat " + N((long long)seat) + " asked - NOT granted: uid_seats.txt is unreadable (see the start of this log; "
                + N(g_ubRefusedUnreadable) + " such answers so far, logged for the first 5 and every 100th)");
    }
    else if (r == 0)
    {
        ++g_ubExhausted; a.kind = coopuidblk::kUbExhausted;
        Log("uid block: " + PeerName(from) + " seat " + N((long long)seat) + " asked - EXHAUSTED: its seat's 4,194,303 counters are handed out and no free seat has any left");
    }
    else if (!WriteUidBlocks(next))
    {
        ++g_ubWriteFailed; a.kind = coopuidblk::kUbFailed;
        if (coopuid::LogRefusal(g_ubWriteFailed))   /* M4 fold 2 (M-A): the same limit; uidBlocks[writeFailed] on the REPORT line */
            Log("uid block: " + PeerName(from) + " seat " + N((long long)seat) + " asked - NOT granted: uid_seats.txt could not be written (the game asks again; "
                + N(g_ubWriteFailed) + " such answers so far, logged for the first 5 and every 100th)");
    }
    else
    {
        g_ub = next; ++g_ubGranted; a.kind = coopuidblk::kUbGrant; a.lo = lo; a.hi = hi;
        Log("uid block: " + PeerName(from) + " seat " + N((long long)seat) + " granted counters " + N((long long)lo) + ".." + N((long long)hi)
            + " (written to uid_seats.txt first; never granted again)");
    }
    std::vector<char> b; coopuidblk::EncodeUidBlock(&b, a);
    SendMsg(from, MSG_UID_BLOCK, b);
}
/* fold (review item 6): the drive's Recycle Bin can take this (flat) folder - asked BEFORE any recycle, so nothing is ever
   deleted for good and no prompt stalls this notebook. */
/* restore1b1 fold 2 (recheck item 1): THE DRIVE'S RECYCLE BIN POLICY. SHQueryRecycleBinA answers even for a bin set to "delete
   immediately", where FOF_WANTNUKEWARNING would raise a modal prompt. root = "X:\". False = the policy cannot be read. */
bool BinPolicyRead(const std::string& root, unsigned long* nuke, unsigned long* maxMb)
{
    char vol[MAX_PATH]; vol[0] = 0;
    if (!::GetVolumeNameForVolumeMountPointA(root.c_str(), vol, MAX_PATH)) return false;
    const std::string guid = restoreguard::VolumeGuidOf(vol);
    if (guid.empty()) return false;
    const std::string base = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\BitBucket";
    HKEY k = 0;
    if (::RegOpenKeyExA(HKEY_CURRENT_USER, (base + "\\Volume\\" + guid).c_str(), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD t = 0, v = 0, m = 0, n = sizeof(v);
    bool ok = ::RegQueryValueExA(k, "NukeOnDelete", 0, &t, (LPBYTE)&v, &n) == ERROR_SUCCESS && t == REG_DWORD;
    n = sizeof(m);
    ok = ok && ::RegQueryValueExA(k, "MaxCapacity", 0, &t, (LPBYTE)&m, &n) == ERROR_SUCCESS && t == REG_DWORD;
    ::RegCloseKey(k);
    if (!ok) return false;
    *nuke = v; *maxMb = m;
    if (::RegOpenKeyExA(HKEY_CURRENT_USER, base.c_str(), 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS)   /* the older, global setting */
    {
        DWORD g = 0; n = sizeof(g);
        if (::RegQueryValueExA(k, "NukeOnDelete", 0, &t, (LPBYTE)&g, &n) == ERROR_SUCCESS && t == REG_DWORD && g != 0) *nuke = 1;
        ::RegCloseKey(k);
    }
    return true;
}
bool RepairBinCanTake(const std::string& dirIn)   /* fold 2 (recheck item 1): the full path, a drive root, and the bin's policy first */
{
    char full[MAX_PATH]; full[0] = 0;
    const DWORD fn = GetFullPathNameA(dirIn.c_str(), MAX_PATH, full, 0);
    if (fn == 0 || fn >= MAX_PATH) return false;
    const std::string dir(full);
    if (dir.size() < 3 || dir[1] != ':' || (dir[2] != '\\' && dir[2] != '/')) return false;
    const std::string root = dir.substr(0, 2) + "\\";
    SHQUERYRBINFO qi; memset(&qi, 0, sizeof(qi)); qi.cbSize = sizeof(qi);
    const bool fixed = GetDriveTypeA(root.c_str()) == DRIVE_FIXED;
    const bool bin = fixed && SUCCEEDED(SHQueryRecycleBinA(root.c_str(), &qi));
    ULARGE_INTEGER fr, tot, all; fr.QuadPart = tot.QuadPart = all.QuadPart = 0;
    if (bin && !GetDiskFreeSpaceExA(root.c_str(), &fr, &tot, &all)) tot.QuadPart = 0;
    unsigned long long bytes = 0ULL;
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do { if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) bytes += ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    unsigned long nuke = 1, maxMb = 0;
    if (!restoreguard::BinPolicyAllows(BinPolicyRead(root, &nuke, &maxMb), nuke, maxMb, bytes)) return false;   /* fold 2 */
    return restoreguard::RecycleBinCanTake(fixed, bin, bytes, tot.QuadPart);
}
/* THE PLUGIN'S RECYCLE CALL, here too: FO_DELETE with FOF_ALLOWUNDO - the Recycle Bin, never a hard delete. */
bool RepairRecycle(const std::string& dir)
{
    std::vector<char> from(dir.begin(), dir.end()); from.push_back(0); from.push_back(0);
    SHFILEOPSTRUCTA op; memset(&op, 0, sizeof(op));
    op.wFunc = FO_DELETE; op.pFrom = &from[0];
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationA(&op) == 0 && !op.fAnyOperationsAborted;
}
/* A NEW operator world save: the small non-record files into repair\<tag>\ (through <tag>.part and a rename), then the prune. */
void RepairCopyMake(const std::string& profile, unsigned int gen, unsigned long long seq)
{
    const std::string tag = restoreguard::CheckpointTagWorld(gen, seq, profile);
    if (tag.empty()) { ++g_rcFailed; Log("repair copy: profile '" + SanitizeForLog(profile) + "' cannot name a folder - no copy for gen=" + N((long long)gen)); return; }
    const std::string root = g_dir + "\\repair", done = root + "\\" + tag, part = done + ".part";
    if (GetFileAttributesA(done.c_str()) != INVALID_FILE_ATTRIBUTES) { Log("repair copy: repair\\" + tag + " is already there - kept"); return; }
    CreateDirectoryA(root.c_str(), 0);
    if (GetFileAttributesA(part.c_str()) != INVALID_FILE_ATTRIBUTES && RepairBinCanTake(part)) RepairRecycle(part);   /* fold: only when the bin can take it */
    if (!CreateDirectoryA(part.c_str(), 0)) { ++g_rcFailed; Log("repair copy: could not make " + part + " (GetLastError=" + N((long long)::GetLastError()) + ") - no copy"); return; }
    const std::vector<std::string> files = restoreguard::RepairCopyFiles();
    long long bytes = 0; int copied = 0; bool ok = true;
    for (size_t i = 0; i < files.size() && ok; ++i)
    {
        const std::string src = g_dir + "\\" + files[i], dst = part + "\\" + files[i];
        if (GetFileAttributesA(src.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        if (!CopyFileA(src.c_str(), dst.c_str(), FALSE)) { ok = false; break; }
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExA(dst.c_str(), GetFileExInfoStandard, &fa)) bytes += ((long long)fa.nFileSizeHigh << 32) | (long long)fa.nFileSizeLow;
        ++copied;
    }
    if (ok && !MoveFileExA(part.c_str(), done.c_str(), 0)) ok = false;
    if (!ok) { ++g_rcFailed; if (RepairBinCanTake(part)) RepairRecycle(part); Log("repair copy: repair\\" + tag + " FAILED (GetLastError=" + N((long long)::GetLastError()) + ") - the partial copy went to the Recycle Bin"); return; }
    ++g_rcMade; g_rcBytes += bytes;
    Log("repair copy MADE: repair\\" + tag + " - " + N((long long)copied) + " file(s), " + N(bytes) + " bytes");
    std::vector<std::string> names;
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((root + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do { const std::string n = fd.cFileName; if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && n != "." && n != "..") names.push_back(n); } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    const std::vector<std::string> out = restoreguard::PruneSelect(names, restoreguard::kCheckpointCap);
    bool binTakes = true;   /* fold (review item 6) */
    for (size_t i = 0; i < out.size() && binTakes; ++i) if (!RepairBinCanTake(root + "\\" + out[i])) binTakes = false;
    if (!binTakes)
    {
        ++g_rcPruneNoBin;
        static int s_noBinLogged = 0;   /* fold 2: said once, counted always */
        if (s_noBinLogged++ == 0)
            Log("repair copy: prune SKIPPED - " + N((long long)out.size()) + " older repair folder(s) left where they are: this drive's Recycle Bin cannot take them (no bin, not a local fixed drive, too big, set to delete immediately, or its setting cannot be read); nothing is deleted. Said once; pruneNoBin counts every skip");
    }
    for (size_t i = 0; i < out.size() && binTakes; ++i)
    {
        if (RepairRecycle(root + "\\" + out[i])) { ++g_rcPruned; Log("repair copy: repair\\" + out[i] + " moved to the Recycle Bin (beyond the newest " + N((long long)restoreguard::kCheckpointCap) + ")"); }
        else { ++g_rcFailed; Log("repair copy: repair\\" + out[i] + " could NOT be moved to the Recycle Bin - left where it is"); }
    }
}
/* Every linked game - the operator's too - is told: checkpoint your own records under this world save's tag. */
void WorldSavedBroadcast(const restoreguard::WorldMsg& stamp)
{
    std::vector<char> b; restoreguard::EncodeWorldBroadcast(&b, stamp);
    for (size_t i = 0; i < g_order.size(); ++i) { SendMsg(g_order[i], MSG_WORLD_SAVED, b); ++g_wsBroadcast; }
}
void ProfilePlayedMark(ENetPeer* from, unsigned num, const char* why);   /* T-201 PP6' (owner 175) - below, with the profiles */
void OnWorldSaved(ENetPeer* from, const std::vector<char>& payload)
{
    restoreguard::WorldMsg in;
    if (!restoreguard::DecodeWorldMsg(payload.empty() ? 0 : &payload[0], payload.size(), &in))
    { ++g_wgMalformed; Log("malformed WORLD_SAVED from " + PeerName(from) + " (" + N((long long)payload.size()) + " bytes) - ignored"); return; }
    if (!PeerIsAuthority(from))
    {
        ++g_wgNotOperator;
        Log("world.gen: WORLD_SAVED gen=" + N((long long)in.gen) + " from " + PeerName(from) + " is not the operator's - ignored (only the operator's saves number the world)");
        return;
    }
    const std::string prof = coopprof::ProfileIdOfKey(PeerIdOf(from));
    if (!restoreguard::ProfileIdOk(in.profile) || in.profile != prof)
    {
        ++g_wgProfileRefused;
        Log("world.gen: WORLD_SAVED names profile '" + SanitizeForLog(in.profile) + "' and this connection plays '" + prof + "' - ignored (profileRefused)");
        return;
    }
    if (restoreguard::UndoneBy(g_repairs, in.epoch, in.seq))   /* restore1c: a stamp from a timeline a repair undid never numbers the world again */
    {
        ++g_rpUndoneStamps;
        Log("world.gen: WORLD_SAVED gen=" + N((long long)in.gen) + " nbSeq=" + N((long long)in.seq) + " epoch=" + N((long long)in.epoch) + " from " + PeerName(from)
            + " is from a timeline a repair undid - ignored (undoneStamps)");
        return;
    }
    /* M14 fold (finding 6): the seqHigh this answers and broadcasts is g_seqNext - 1, which counts every number
       STAMPED - including writes still queued. Every write out lands first, so the number names only what is on disk.
       M14 fold 2 (re-check B): AFTER the malformed, operator, profile and undone checks - a refused message waits for no write. */
    ProfilePlayedMark(from, coopprof::NumOfKey(PeerIdOf(from)), "WORLD_SAVED");   /* T-201 PP6' (owner 175): the operator's finished save plays its profile */
    WriterFlushAll();
    ++g_wgIn;
    const unsigned long long idx = g_seqNext > 0ULL ? g_seqNext - 1ULL : 0ULL;
    const int ch = restoreguard::MergeBook(&g_wg, prof, in.gen, idx);   /* review-restore1a d: the game's claimed nbSeq never raises seqHigh */
    if ((ch & 1) == 0) ++g_wgStale;
    if (ch != 0)
    {
        if ((ch & 1) != 0) ++g_wgRaised;
        if (WriteWorldGen()) g_wgFileState = 1;
        else { ++g_wgWriteFailed; Log("world.gen: WRITE FAILED - the raised numbers hold in memory until the next WORLD_SAVED"); }
    }
    if ((ch & 1) != 0)   /* restore1b1: a NEW world save - the notebook's repair copy (world.gen already written), then every linked game checkpoints its records */
    {
        RepairCopyMake(prof, in.gen, in.seq);
        restoreguard::WorldMsg bc = restoreguard::RowOf(g_wg, prof); bc.gen = in.gen; bc.seq = in.seq;
        WorldSavedBroadcast(bc);
    }
    Log("world.gen: WORLD_SAVED profile " + prof + " gen=" + N((long long)in.gen) + " nbSeq=" + N((long long)in.seq) + " from the operator (" + PeerName(from)
        + ") -> worldGen=" + N((long long)restoreguard::GenOf(g_wg, prof)) + " seqHigh=" + N((long long)WorldSeqHighNow())
        + ((ch & 1) == 0 ? " (not newer - kept, never steps back)" : ""));
    restoreguard::WorldMsg a = restoreguard::RowOf(g_wg, prof); a.seq = WorldSeqHighNow();
    std::vector<char> b; restoreguard::EncodeWorldMsg(&b, a);
    SendMsg(from, MSG_WORLD_SAVED, b); ++g_wgAnswered;
}
/* ======================= restore1c (design s4 "Repair a broken world"): THE NOTEBOOK'S HALF =======================
   repair.txt (temp + rename, like world.gen). OnRepair: the operator's REPAIR {gR, sR}, checked (the operator, its own profile, numbers
   this notebook has seen, repair\<tag>\ present, the drive's Recycle Bin able to take the files); then IN ORDER (1) the repair.txt row
   (E+1, gR, sR); (2) every record with seq > sR to the Recycle Bin, falling back to its previous version only when that version's seq
   is <= sR; (3) the non-record files put back from repair\<tag>\ and re-read, the records numbers (own_high.txt) cleared, the
   repairing profile's number lowered to gR and the epoch raised - only then does the host's load of R pass the gate. Every linked
   game gets REPAIR down; then every link is dropped so each game re-links and reads the repaired notebook. Nothing is hard-deleted. */
std::string RepairListFile() { return g_dir + "\\repair.txt"; }
bool WriteRepairList()
{
    const std::string path = RepairListFile(), tmp = path + ".tmp", body = restoreguard::RepairsFormat(g_repairs);
    HANDLE h = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) { Log("repair.txt: could not open " + tmp + " (GetLastError=" + N((long long)::GetLastError()) + ")"); return false; }
    DWORD wrote = 0;
    BOOL ok = body.empty() ? TRUE : WriteFile(h, body.data(), (DWORD)body.size(), &wrote, 0);
    if (ok && wrote != (DWORD)body.size()) ok = FALSE;
    if (ok) ok = FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) { Log("repair.txt: the write to " + tmp + " failed - " + path + " is unchanged"); return false; }
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    { Log("repair.txt: could not replace " + path + " (MoveFileExA, GetLastError=" + N((long long)::GetLastError()) + ")"); return false; }
    return true;
}
void LoadRepairList()
{
    std::ifstream f(RepairListFile().c_str());
    if (!f) { Log("repair.txt: none in this folder - this world has never been repaired (epoch " + N((long long)g_wg.epoch) + ")"); return; }
    std::string text, line;
    while (std::getline(f, line)) text += line + "\n";
    g_rpBadLines = restoreguard::RepairsParse(text, &g_repairs);
    const unsigned int top = g_repairs.empty() ? 0u : g_repairs.back().epoch;
    if (top > g_wg.epoch) { g_wg.epoch = top; if (WriteWorldGen()) g_wgFileState = 1; Log("world.gen: epoch raised to " + N((long long)top) + " from repair.txt (a repair stopped before its last step)"); }
    Log("repair.txt: " + N((long long)g_repairs.size()) + " repair(s), epoch " + N((long long)g_wg.epoch) + (g_rpBadLines != 0 ? ", " + N(g_rpBadLines) + " unreadable line(s) skipped" : std::string()));
}
bool RepairFileThere(const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
/* ONE Recycle Bin call for several files (the plugin's recycle flags: FO_DELETE + FOF_ALLOWUNDO, never a prompt). */
bool RepairRecycleFiles(const std::vector<std::string>& files)
{
    if (files.empty()) return true;
    std::vector<char> from;
    for (size_t i = 0; i < files.size(); ++i) { from.insert(from.end(), files[i].begin(), files[i].end()); from.push_back(0); }
    from.push_back(0);
    SHFILEOPSTRUCTA op; memset(&op, 0, sizeof(op));
    op.wFunc = FO_DELETE; op.pFrom = &from[0];
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationA(&op) == 0 && !op.fAnyOperationsAborted;
}
long long RepairUnixNow()
{
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    const unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return t > 116444736000000000ULL ? (long long)((t - 116444736000000000ULL) / 10000000ULL) : 0;
}
void OnRepair(ENetPeer* from, const std::vector<char>& payload)
{
    restoreguard::RepairMsg in;
    if (!restoreguard::DecodeRepairMsg(payload.empty() ? 0 : &payload[0], payload.size(), &in) || in.kind != restoreguard::kRpUp)
    { ++g_rpMalformed; Log("malformed REPAIR from " + PeerName(from) + " (" + N((long long)payload.size()) + " bytes) - ignored"); return; }
    ++g_rpIn;
    const std::string prof = coopprof::ProfileIdOfKey(PeerIdOf(from));
    restoreguard::RepairMsg a = in; a.kind = restoreguard::kRpDown; a.recycled = 0; a.epoch = g_wg.epoch;
    int res = restoreguard::kRpOk;
    if (!PeerIsAuthority(from)) res = restoreguard::kRpNotOperator;
    else if (!restoreguard::ProfileIdOk(in.profile) || in.profile != prof) res = restoreguard::kRpProfile;
    if (res == restoreguard::kRpOk)
    {
        /* M14 rule (2), M14 fold (finding 7): EVERY WRITE OUT LANDS FIRST - before the checks that read the seq
           high-water (RepairValidate) and the records folder (RepairBinCanTake), and before the repair reads each
           record's .prev line and moves its files. M14 fold 2 (re-check B): AFTER the permission checks (the
           operator, its own profile) - a refused REPAIR waits for no write. */
        WriterFlushAll();
        res = restoreguard::RepairValidate(restoreguard::GenOf(g_wg, prof), WorldSeqHighNow(), in.gen, in.seq);
    }
    const std::string tag = restoreguard::CheckpointTagWorld(in.gen, in.seq, prof), copy = g_dir + "\\repair\\" + tag;
    if (res == restoreguard::kRpOk && (tag.empty() || !RepairFileThere(copy))) res = restoreguard::kRpNoCopy;
    if (res == restoreguard::kRpOk && !RepairBinCanTake(g_dir)) res = restoreguard::kRpNoBin;
    if (res != restoreguard::kRpOk)
    {
        ++g_rpRefused; a.result = (unsigned int)res;
        Log("REPAIR from " + PeerName(from) + " (profile " + SanitizeForLog(in.profile) + ", worldGen=" + N((long long)in.gen) + " nbSeq=" + N((long long)in.seq)
            + ") REFUSED: " + restoreguard::RepairResultName((unsigned int)res) + " - nothing changed");
        std::vector<char> b; restoreguard::EncodeRepairMsg(&b, a);
        SendMsg(from, MSG_REPAIR, b);
        return;
    }
    /* (1) the row */
    restoreguard::RepairRow row; row.epoch = g_wg.epoch + 1; row.gen = in.gen; row.seq = in.seq; row.profile = prof; row.at = RepairUnixNow();
    g_repairs.push_back(row);
    if (!WriteRepairList()) { ++g_rpFailed; Log("repair.txt: WRITE FAILED - the row holds in memory and the repair goes on"); }
    /* (2) the records */
    long long recycled = 0, reverted = 0, failed = 0;
    std::vector<std::string> ids;
    for (std::map<std::string, Record>::const_iterator it = g_records.begin(); it != g_records.end(); ++it) if (it->second.seq > in.seq) ids.push_back(it->first);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        const std::string& id = ids[i];
        const Record cur = g_records[id];
        bool havePrev = false; Record pr;
        {
            std::ifstream f(PrevOf(MetaOf(id)).c_str()); std::string line; coopstore::MetaLine pm;
            if (f && std::getline(f, line))
            {
                if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
                if (coopstore::MetaParse(line, &pm) && pm.worldId == id) { RecordFromMeta(pm, &pr); havePrev = true; }
            }
        }
        const int act = restoreguard::RepairRecordAction(cur.seq, havePrev, pr.seq, in.seq);
        std::vector<std::string> files;
        if (act == restoreguard::kNbRevert)
        {
            if (RepairFileThere(FileOf(id))) files.push_back(FileOf(id));
            if (RepairFileThere(MetaOf(id))) files.push_back(MetaOf(id));
            if (!RepairRecycleFiles(files)) { ++failed; Log("repair: " + SanitizeForLog(id) + " could NOT be moved to the Recycle Bin - left as it is"); continue; }
            if (RepairFileThere(PrevOf(FileOf(id)))) MoveFileExA(PrevOf(FileOf(id)).c_str(), FileOf(id).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            if (!MoveFileExA(PrevOf(MetaOf(id)).c_str(), MetaOf(id).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) ++failed;
            g_records[id] = pr; ++reverted; ++recycled;
        }
        else if (act == restoreguard::kNbRecycle)
        {
            const std::string c[5] = { FileOf(id), MetaOf(id), PrevOf(FileOf(id)), PrevOf(MetaOf(id)), FileOf(id) + ".refused" };
            for (int k = 0; k < 5; ++k) if (RepairFileThere(c[k])) files.push_back(c[k]);
            if (!RepairRecycleFiles(files)) { ++failed; Log("repair: " + SanitizeForLog(id) + " could NOT be moved to the Recycle Bin - left as it is"); continue; }
            g_records.erase(id); ++recycled;
        }
    }
    /* (3) the non-record files, then their tables re-read */
    long long putBack = 0;
    const std::vector<std::string> pb = restoreguard::RepairPutBackFiles();
    for (size_t i = 0; i < pb.size(); ++i)
    {
        const std::string dst = g_dir + "\\" + pb[i], src = copy + "\\" + pb[i];
        if (RepairFileThere(dst))
        {
            std::vector<std::string> one(1, dst);
            if (!RepairRecycleFiles(one)) { ++failed; Log("repair: " + pb[i] + " could NOT be moved to the Recycle Bin - kept, not put back"); continue; }
        }
        if (RepairFileThere(src)) { if (CopyFileA(src.c_str(), dst.c_str(), FALSE)) ++putBack; else ++failed; }
    }
    g_uniques.clear(); LoadUniques();
    g_research = coopres::ResearchTable(); LoadResearch();
    g_takes = coopres::ResearchTakeTable(); LoadTakes();
    g_bars = coopbar::BarTable(); LoadBars();
    g_wrel = coopwrel::Table(); LoadWorldRel();   /* par24: world_relations.txt */
    g_options.clear(); LoadOptions();
    g_clockHours = -1.0; LoadClock();
    g_oh = restoreguard::OwnBook();   /* only the repair lowers the records numbers: every game reports again */
    if (!WriteOwnHigh()) ++failed;
    g_wg.epoch = row.epoch; g_wg.gen[prof] = in.gen;   /* the book steps back HERE only */
    if (WriteWorldGen()) g_wgFileState = 1; else ++failed;
    ++g_rpDone; g_rpRecycled += recycled; g_rpReverted += reverted; g_rpPutBack += putBack; g_rpFailed += failed;
    Log("REPAIR DONE by " + PeerName(from) + " (profile " + prof + "): the WHOLE world back to worldGen=" + N((long long)in.gen) + " nbSeq=" + N((long long)in.seq)
        + " - epoch " + N((long long)row.epoch) + "; " + N(recycled) + " record(s) newer than the restore point to the Recycle Bin (" + N(reverted)
        + " of them back to their previous version), " + N(putBack) + " file(s) put back from repair\\" + tag + ", records numbers cleared, "
        + N(failed) + " step(s) failed; every linked game is told and every link is dropped");
    a.epoch = row.epoch; a.result = restoreguard::kRpOk; a.recycled = (unsigned int)recycled;
    std::vector<char> b; restoreguard::EncodeRepairMsg(&b, a);
    for (size_t i = 0; i < g_order.size(); ++i) SendMsg(g_order[i], MSG_REPAIR, b);
    for (size_t i = 0; i < g_order.size(); ++i) SbDisconnectLater(g_order[i]);
}
/* ONE RECORD, from whichever of its two meta lines was found. `isCurrentMeta` is false when the walk found only
   <id>.meta.prev - crash windows 2 and 3, where <id>.meta does not exist at all. `sourceName` is the file the
   line came out of: review-p8b L-2 found the drop line naming the id FROM the line rather than the file an
   operator would have to go and look at, which for a mis-named .meta.prev are different names. Returns true
   when the record was put in the index. */
bool IndexRecordFromMeta(const coopstore::MetaLine& m, bool isCurrentMeta, const std::string& sourceName)
{
    Record r; RecordFromMeta(m, &r);
    {
        std::string fac; unsigned gnum = 0;
        if (SplitGroupId(r.worldId, &fac, &gnum))
        {
            std::map<std::string, std::vector<char> >::const_iterator bt = g_bits.find(fac);
            if (bt != g_bits.end() && (gnum >> 3) < bt->second.size() && (((unsigned char)bt->second[gnum >> 3] >> (gnum & 7)) & 1))
            {
                DeleteFileA(FileOf(r.worldId).c_str()); DeleteFileA(MetaOf(r.worldId).c_str());
                DeleteFileA(PrevOf(FileOf(r.worldId)).c_str()); DeleteFileA(PrevOf(MetaOf(r.worldId)).c_str());   /* P8b: the fallback copy goes with the record it was a copy of */
                DeleteFileA((FileOf(r.worldId) + ".refused").c_str());   /* P8c: and the copy the upgrade set aside */
                Log("index: " + SanitizeForLog(r.worldId) + " dropped - its number is marked deleted"); return false;
            }
        }
    }   /* review-p4k H3 */
    /* P8d. THE FOUR FILES ARE PROBED ONCE, HERE, and every arm below decides from those three-way answers:
       the v2-v4 upgrade, the v5 validation, and the log lines that used to assert things the folder
       contradicted (review-p8c M-4). `isCurrentMeta` is passed to the upgrade too - P8c passed an
       unconditional true, so a pass-2 record was told it had a current index line when it had none
       (review-p8c L-2). */
    RecordProbes pr; ProbeRecord(r.worldId, &pr);
    CountProbes(pr);
    coopstore::FolderFacts ff;
    FactsFromProbes(r.worldId, pr, m, isCurrentMeta, &ff);
    if (m.version < 5)
    {
        /* MIGRATION (design-save 1.3), P8b -> P8c -> P8d.
           P8b asked one question here - "did the read succeed?" - and wrote the answer back as a v5 line.
           That made a file held open for a second by a scanner indistinguishable from a file that is not
           there, and the line it then wrote (`len 0 / crc 00000000`) MEANS "this record has no payload and
           that is not a failure", so nothing ever looked at the intact squad beside it again (review-p8b
           C-1). P8c split the two - FOR <id>.platoon ONLY. The other three inputs to this decision were
           still read with the collapsing test AND THIS BRANCH WRITES, so a <id>.meta.prev held open for one
           start let a five-byte torn payload be checksummed and blessed as truth, permanently, because the
           next start then found a line that validated (review-p8c H-1).
           P8d: ALL FOUR of this record's files were probed above, and UpgradeDecide defers on any one of
           them being on disk and unreadable. There is nothing left here to gather. */
        const std::string payloadPath = FileOf(r.worldId);
        const coopstore::UpgradeChoice u = coopstore::UpgradeDecide(ff);

        {   /* review-p8b M-4: this pass runs over EVERY record of an existing folder, once, before
               `listening`, and measured 16.6 s over 2 000 records with the socket open and nothing answered.
               A line every 200 records does not make it shorter; it makes it legible while it happens. */
            static long long s_seen = 0;
            if ((++s_seen % 200) == 0)
                Log("index: still upgrading v2-v4 lines - " + N(s_seen) + " so far. The socket is open and"
                    " nothing is answered until this pass finishes.");
        }

        if (u.kind == coopstore::kUpgradeDefer)
        {
            /* THE C-1 REPAIR, WIDENED TO ALL FOUR FILES (review-p8c H-1). Nothing is written. A v2-v4 line
               left alone is still parsed, still upgraded at any later start, and in the meantime the record
               behaves exactly as it did before P8b - which is the behaviour a locked file never broke. */
            ++g_indexUpgradeDeferredUnreadable;
            Log("index: " + SanitizeForLog(r.worldId) + " - its v" + N(m.version) + " line is LEFT EXACTLY AS"
                " IT IS and will be upgraded at a later start, because one of the four files this decision"
                " reads is ON DISK AND COULD NOT BE READ: " + UnreadableList(pr) + ". The upgrade is a WRITE,"
                " and writing `no payload` about a file that is on disk - or blessing a payload while the"
                " previous version that would have refused it is shut - loses that squad for good.");
            r.hasFile = true; r.len = 0; r.crc = 0;   /* served as the pre-P8b loader served it: the squad as soon as the file opens */
            g_records[r.worldId] = r;                 /* deliberately not PutInIndex: it is not position-only */
            return true;
        }
        if (u.kind == coopstore::kUpgradePromotePrev)
        {
            /* THE H-2 REPAIR. A v2-v4 line cannot tell a torn payload from a whole one; a v5 previous PAIR
               can, and it validates. NOTHING IS DELETED: the payload that was not trusted is moved aside
               under a name that matches neither folder walk, so the choice is reversible by hand. */
            Record p; RecordFromMeta(ff.prevMeta, &p);
            p.hasFile = u.promote.hasFile != 0;
            if (pr.payload.kind != coopstore::kFileAbsent)
            {
                const std::string aside = payloadPath + ".refused";
                if (MoveFileExA(payloadPath.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) ++g_indexPayloadSetAside;
                else ++g_writeRenameFailed;
            }
            if (ff.prevPayloadState == coopstore::kFileReadable)
            {
                if (MoveFileExA(PrevOf(payloadPath).c_str(), payloadPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) ++g_writeRenameFailed;
            }
            if (!WriteMeta(p))
                Log("index: " + SanitizeForLog(p.worldId) + " - the promoted line could not be written; the"
                    " payload has been promoted on disk and the index line will be rewritten at the next start");
            ++g_indexPromotedPrevOnUpgrade;
            Log("index: " + SanitizeForLog(p.worldId) + " is a v" + N(m.version) + " line and a VALIDATED"
                " PREVIOUS VERSION was sitting beside it (" + N(p.len) + " bytes, crc " + HX8(p.crc)
                + ", written " + N(p.writtenAt) + "). The previous version has been promoted; the payload the"
                " old line pointed at is kept as " + SanitizeForLog(Sanitize(p.worldId)) + ".platoon.refused"
                " rather than overwritten, because a v" + N(m.version) + " line carries no length and no"
                " checksum and cannot tell a torn write from a newer one.");
            PutInIndex(p);
            return true;
        }
        if (u.kind == coopstore::kUpgradeFromPayload)
        { r.hasFile = true; r.len = ff.payloadLen; r.crc = ff.payloadCrc; }
        else
        {
            r.hasFile = false; r.len = 0; r.crc = 0;
            ++g_indexUpgradedNoPayload;
            /* P8d (review-p8c M-1): "no record file" and "a record file with NO BYTES IN IT" both end up
               here, and they are different facts about the folder even though the line that describes them
               is the same one. The line is right either way - a file with no bytes carries no squad - and
               the sentence now says which of the two it was instead of asserting the first. P8c claimed in
               two documents that `len 0` was never written for a file on disk, and wrote it here. */
            Log("index: " + SanitizeForLog(r.worldId) + " is a v" + N(m.version) + " line and it is upgraded"
                " as a POSITION-ONLY line - it keeps its place in the world and no squad, exactly as it does"
                " today. " + (pr.payload.kind == coopstore::kFileAbsent
                    ? std::string("THE FOLDER SAYS THERE IS NO RECORD FILE for it (an attribute query, not a"
                                  " failed open).")
                    : std::string("Its record file IS on disk and has NO BYTES IN IT, which carries no squad"
                                  " either - counted as payloadZeroLength.")));
        }
        /* P8d: through the one write path. kRotateNone - rotating here would push a v2-v4 line, which can
           validate nothing, over a good v5 <id>.meta.prev. */
        if (CommitRecord(r, 0, kRotateNone)) ++g_indexUpgraded;
        else Log("index: " + SanitizeForLog(r.worldId) + " - the upgraded v5 line could NOT be written; the"
                 " record is served from memory this session and its v" + N(m.version) + " line stays on disk"
                 " for the next start to try again");
        PutInIndex(r);
        return true;
    }
    const coopstore::Choice c = coopstore::ChooseVersion(ff);
    if (c.refusal == coopstore::kPayloadTorn)            ++g_indexRefusedTorn;
    else if (c.refusal == coopstore::kPayloadCrcBad)     ++g_indexRefusedCrc;
    else if (c.refusal == coopstore::kPayloadMissing)    ++g_indexRefusedMissing;
    else if (c.refusal == coopstore::kPayloadUnreadable) ++g_indexRefusedUnreadable;   /* P8d: its own name - "we refused a damaged file" and "we could not look" ask for different repairs */
    if (c.kind == coopstore::kChooseCurrent)
    {
        r.hasFile = c.hasFile != 0;
    }
    else if (c.kind == coopstore::kChoosePrevMetaCurrent || c.kind == coopstore::kChoosePrevPair)
    {
        RecordFromMeta(ff.prevMeta, &r);
        r.hasFile = c.hasFile != 0;
        if (c.kind == coopstore::kChoosePrevPair && ff.prevPayloadState == coopstore::kFileReadable)
        {
            if (MoveFileExA(PrevOf(FileOf(r.worldId)).c_str(), FileOf(r.worldId).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) ++g_writeRenameFailed;
        }
        /* P8c (review-p8b M-5): the result is READ. A promotion whose index line did not land is a record
           that recovers again at the next start, and a silent one is a folder that reports itself clean. */
        if (!WriteMeta(r))
            Log("index: " + SanitizeForLog(r.worldId) + " - the promoted line could not be written; the"
                " promotion stands on disk and the line will be rewritten at the next start");
        ++g_indexPromotedPrev;
        Log("index: " + SanitizeForLog(r.worldId) + " - the current record is " + (isCurrentMeta ? RefusalWord(c.refusal) : "MISSING ITS INDEX LINE")
            + "; THE PREVIOUS VERSION HAS BEEN PROMOTED (" + N(r.len) + " bytes, crc " + HX8(r.crc) + ", written " + N(r.writtenAt) + ")");
    }
    else if (c.kind == coopstore::kChooseCurrentRefused)
    {
        r.hasFile = false;
        /* P8d (review-p8c M-4, both halves). Every claim in this sentence is a claim about the FOLDER, so
           it is made from the folder rather than from the verdict. "Missing" needed no re-check any more -
           kPayloadUnreadable is now its own verdict - and "there is no previous version to fall back on"
           was printed by P8c while a perfectly good <id>.platoon.prev was sitting in the folder, which is
           the one sentence an operator would have acted on. */
        Log("index: " + SanitizeForLog(r.worldId) + " - its record file is " + RefusalWord(c.refusal)
            + (pr.payload.kind == coopstore::kFileUnreadable
               ? std::string(" (" + N(pr.payload.attrLen) + " bytes on disk, error " + N((long long)pr.payload.err) + ")")
               : std::string(""))
            + ". " + (c.refusal == coopstore::kPayloadUnreadable
               /* AND NOTHING ELSE WAS EVEN LOOKED AT, which is a different sentence from "nothing else
                  validates" and must not be printed as if it were: there may well be a .prev pair here that
                  validates perfectly, and refusing to prefer it is the whole repair (review-p8c C-2). */
               ? std::string("NO OTHER VERSION WAS CONSULTED while that file is shut: a payload we cannot"
                             " read is a payload we cannot RULE OUT, so nothing may be preferred over it -"
                             " not even a <id>.meta.prev / <id>.platoon.prev pair that validates on its own."
                             " Preferring one there renamed the previous payload over an intact committed one"
                             " at the next start (review-p8c C-2). For the record, <id>.meta.prev is ")
                 + StateWord(pr.prevMeta.kind) + " and <id>.platoon.prev is "
                 + StateWord(pr.prevPayload.kind) + "."
               : std::string("No other version validates either: <id>.meta.prev is ")
                 + StateWord(pr.prevMeta.kind)
                 + (ff.havePrevMeta ? " and its line does not match either payload file"
                                    : " and no usable line for this record came out of it")
                 + ", <id>.platoon.prev is " + StateWord(pr.prevPayload.kind) + ".")
            + " The group keeps its position and NO SQUAD, which is what this notebook has always served for"
            " a record with no file - it is no longer handed out as if it were whole. NOTHING IS WRITTEN and"
            " every file is left exactly where it is, so the next start decides again.");
    }
    else if (c.kind == coopstore::kChoosePrevRefused)
    {
        /* P8c (review-p8b M-1). Found only as <id>.meta.prev, and nothing it names validates. Pass 1 keeps
           such a record on its position; pass 2 used to drop it, which loses the group outright. */
        r.hasFile = false;
        Log("index: " + SanitizeForLog(r.worldId)
            + (pr.meta.kind == coopstore::kFileUnreadable
               ? std::string(" has an index line of its own that could NOT BE READ - <id>.meta is on disk ("
                             + N(pr.meta.attrLen) + " bytes, error " + N((long long)pr.meta.err) + "), and a"
                               " line we could not read is not an absent one, so NOTHING is promoted over it"
                               " and nothing is written")
               : std::string(" has no index line of its own and the payload its previous line names is ")
                 + RefusalWord(c.refusal))
            + " (from " + SanitizeForLog(sourceName)
            + "). The group KEEPS ITS POSITION and no squad - the same answer pass one gives for the same"
            " damage; it is no longer dropped from the index altogether.");
    }
    else
    {
        ++g_indexNoVersion;
        Log("index: " + SanitizeForLog(r.worldId) + " - nothing usable on disk for it (no index line that"
            " validates); it is NOT INDEXED. The file that named it is " + SanitizeForLog(sourceName)
            + " - that is the one to look at, and it need not be named after this record.");
        return false;
    }
    /* ============ STORE-FILE FORMAT -> 7, IN PLACE AT START (B12, decision 52; B10's v5 -> v6 shape) ====
       B12 ADDS ONE FIELD, seq, and a line below v7 reads back as seq 0 - which is what the upgrade writes,
       because a sequence number cannot be invented for a record written before there were any. Everything
       below is B10's migration unchanged except the version it tests for and the numbers it books.
       The FIELDS do not change and RecordFromMeta has already replaced the role in `owner` with
       kOwnerUnknown, so all that is owed here is the WRITE that makes it stick: without it every later
       start would migrate the same line again, which is review-p8b C-1`s shape and is exactly what
       upgradedV5toV6 = 0 on the second start is the verdict for.
       ONLY THE ARMS THAT REACHED A CLEAN LINE ARE MIGRATED. The two REFUSED arms deliberately write
       nothing and leave every file where it is, and a migration is a write: a v5 line whose payload is
       torn, missing or shut stays exactly as it is and is migrated at a later start once the damage
       clears. The two PROMOTE arms have already written their line through WriteMeta, which now encodes
       v6, so they are counted rather than written a second time. */
    {
        const int lineVersion = ((c.kind == coopstore::kChoosePrevMetaCurrent) || (c.kind == coopstore::kChoosePrevPair))
                                    ? ff.prevMeta.version : m.version;
        if (lineVersion >= 7) { /* already format 7 - nothing owed, and this is the steady state */ }
        else if (c.kind == coopstore::kChooseCurrent)
        {
            if (WriteMeta(r)) ++g_indexUpgradedToV7;
            else
            {
                ++g_indexV7UpgradeFailed;
                Log("index: " + SanitizeForLog(r.worldId) + " - its v" + N((long long)lineVersion) + " line"
                    " could NOT be rewritten as v7. The record is served this session with owner UNKNOWN"
                    " and seq 0, which is what the migration would have written anyway, and the line on"
                    " disk is migrated at the next start.");
            }
        }
        else if ((c.kind == coopstore::kChoosePrevMetaCurrent) || (c.kind == coopstore::kChoosePrevPair)) ++g_indexUpgradedToV7;
        else ++g_indexV7Deferred;
    }
    PutInIndex(r);
    return true;
}
void LoadIndex()
{
    LoadBits();   /* decision 30 */
    LoadUniques();   /* decision 31(c) */
    LoadResearch();   /* loot2b: research_boxes.txt */
    LoadTakes();      /* loot2c: research_takes.txt */
    LoadBars();   /* refill1: town_bars.txt */
    LoadWorldRel();   /* par24: world_relations.txt */
    LoadOptions();   /* E36 / decision 40: the session's option map, ALWAYS complete after this call */
    LoadClock();     /* E40 / decision 45: this world's time, if it has been given one; below LoadOptions because it logs the time mode */
    int n = 0;
    /* PASS 1 - every <id>.meta. */
    {
        WIN32_FIND_DATAA fd; const std::string pat = g_dir + "\\*.meta";
        HANDLE h = FindFirstFileA(pat.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                const std::string name = fd.cFileName;
                if (!NameEndsWith(name, ".meta")) continue;
                std::string line;
                /* P8d. THE WALK LISTED THE NAME; THE PROBE SAYS WHAT IT IS. P8c counted "zero-length" and
                   "could not be read" under one name and said so in one sentence - two different states of
                   the world asking for two different repairs (one is a damaged file to look at, the other
                   is a lock to wait out), which is the very collapse this effort exists to remove. */
                const FileProbe mp = ProbeFile(g_dir + "\\" + name);
                if (mp.kind == coopstore::kFileUnreadable)
                {
                    ++g_probeUnreadableMeta;
                    Log("index: " + SanitizeForLog(name) + " is ON DISK (" + N(mp.attrLen) + " bytes) and"
                        " could NOT BE READ (error " + N((long long)mp.err) + "). NOTHING IS WRITTEN for that"
                        " record and no other version is promoted over it - a line we could not read is not"
                        " an absent one. It is read again at the next start.");
                    continue;
                }
                if (mp.kind == coopstore::kFileAbsent)
                {
                    ++g_indexMetaVanished;
                    Log("index: " + SanitizeForLog(name) + " was listed by the folder walk and is NOT THERE"
                        " now - it was deleted between the two. Nothing is written; the second pass below"
                        " picks up its previous line if it has one.");
                    continue;
                }
                if (!FirstLineOf(mp, &line))
                {
                    ++g_indexMetaEmpty;
                    Log("index: " + SanitizeForLog(name) + " was READ and HAS NO FIRST LINE - it is"
                        " zero-length, which is the shape a power cut leaves. This record is NOT in the index"
                        " from that file; its previous line, if it has one, is picked up by the second pass"
                        " below.");
                    continue;
                }
                coopstore::MetaLine m;
                if (!coopstore::MetaParse(line, &m))
                {
                    ++g_indexMetaUnparsable;
                    Log("index: " + SanitizeForLog(name) + " - its first line is not any index format this"
                        " build knows (v2 to v5), so this record is NOT in the index from that file. The file"
                        " is left exactly where it is.");
                    continue;
                }
                if (IndexRecordFromMeta(m, true, name)) ++n;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    /* PASS 2 - AND THIS PASS IS THE POINT OF THE PREVIOUS VERSION. Crash windows 2 and 3 leave NO <id>.meta
       at all, only <id>.meta.prev; a walk of *.meta alone never visits that record and the group vanishes from
       the index entirely - which is a worse outcome than the torn file this phase exists to refuse. A record
       already served by pass 1 is skipped: pass 1 has already offered its previous version to ChooseVersion. */
    {
        WIN32_FIND_DATAA fd; const std::string pat = g_dir + "\\*.meta.prev";
        HANDLE h = FindFirstFileA(pat.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                const std::string name = fd.cFileName;
                if (!NameEndsWith(name, ".meta.prev")) continue;
                std::string line;
                const FileProbe pmp = ProbeFile(g_dir + "\\" + name);
                if (pmp.kind == coopstore::kFileUnreadable)
                {
                    ++g_probeUnreadableMetaPrev;
                    Log("index: " + SanitizeForLog(name) + " is ON DISK (" + N(pmp.attrLen) + " bytes) and"
                        " could NOT BE READ (error " + N((long long)pmp.err) + ") - the fallback copy for that"
                        " record is shut, so nothing is promoted from it and nothing is written. It is read"
                        " again at the next start.");
                    continue;
                }
                if (!FirstLineOf(pmp, &line))
                {
                    ++g_indexMetaEmpty;
                    Log("index: " + SanitizeForLog(name) + " HAS NO FIRST LINE - the fallback copy for that"
                        " record is zero-length, so there is nothing to promote from it.");
                    continue;
                }
                coopstore::MetaLine m;
                if (!coopstore::MetaParse(line, &m))
                {
                    ++g_indexMetaUnparsable;
                    Log("index: " + SanitizeForLog(name) + " - its first line is not any index format this"
                        " build knows (v2 to v5), so there is nothing to promote from it.");
                    continue;
                }
                if (g_records.find(m.worldId) != g_records.end()) continue;
                Log("index: " + SanitizeForLog(m.worldId) + " has no index line of its own - reading its previous one");
                if (IndexRecordFromMeta(m, false, name)) ++n;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    /* P8d, the cheap half of review-p8c M-5. A <id>.platoon.refused is a payload a v2-v4 upgrade chose not
       to trust and KEPT rather than deleted. It is still never swept - sweeping it needs a rule, not a
       broom, and it is the only copy of those bytes - but at the second start P8c said nothing about it at
       all, so a folder full of them read as clean. Counted and named once, here. */
    {
        WIN32_FIND_DATAA fd; const std::string pat = g_dir + "\\*.platoon.refused";
        HANDLE h = FindFirstFileA(pat.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do { if (NameEndsWith(fd.cFileName, ".platoon.refused")) ++g_indexRefusedFilesPresent; } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        if (g_indexRefusedFilesPresent > 0)
            Log("index: " + N(g_indexRefusedFilesPresent) + " <id>.platoon.refused file(s) are in this"
                " folder - squad files a v2-v4 upgrade would not trust and kept instead of overwriting."
                " They match neither walk and nothing reads them; they are there so a payload can be put"
                " back by hand, and they are not swept.");
    }
    g_indexLoaded = n;
    Log("index: " + N(n) + " records from " + g_dir);
    LogStoreCounters();
    LogIndexCoverage();   /* P8c: what the previous-version net does and does not reach, in words */
}

/* M6 fold 1 (review of 49bac485, LOW): SendMsg ANSWERS - false = no packet, or ENet refused it (a peer not connected, a bad
   channel, a packet too large). A refused packet is not ENet's, so it is freed here (it leaked before). Every caller before
   this fold ignores the answer (all are statements - Read), so none of them changes. */
bool SendMsg(ENetPeer* to, unsigned char type, const std::vector<char>& payload)
{
    std::vector<char> buf(5 + payload.size());
    buf[0] = (char)type; unsigned n = (unsigned)payload.size(); memcpy(&buf[1], &n, 4);
    if (!payload.empty()) memcpy(&buf[5], &payload[0], payload.size());
    return SbSendRel(to, buf);   /* M16: through this game's bound - same answer */
}
size_t SendRecordTo(ENetPeer* to, const Record& r)   /* T-313: returns the bytes it put on the wire - the feed page's budget */
{
    /* P8d (review-p8c M-6). The read's result was DISCARDED here, so a record whose payload was locked or
       deferred went out as a MSG_RECORD with an empty payload - which is the wire's position-only shape,
       i.e. the F661 symptom arriving over the wire, uncounted and unlogged. It still goes out with the
       position (that is better than not being in the game's index at all, and the next RECORD carries the
       squad), but it is now COUNTED and said. */
    std::vector<char> bytes;
    if (r.hasFile)
    {
        const FileProbe fp = ProbeFile(FileOf(r.worldId));
        if (fp.kind == coopstore::kFileReadable) bytes = fp.bytes;
        else
        {
            ++g_sendPayloadUnreadable;
            Log("push " + SanitizeForLog(r.worldId) + " - its record file is " + StateWord(fp.kind)
                + " (error " + N((long long)fp.err) + "), so this group goes to " + PeerName(to)
                + " with its POSITION and nobody in it. Nothing on disk is changed; the group gets its"
                " people from the next RECORD for it, or from the next start.");
        }
    }
    const std::vector<char> whole = EncodeRecord(r, bytes, r.hasFile ? r.writtenAt : r.posAt);
    SendMsg(to, MSG_RECORD, whole);
    size_t out = whole.size();
    if (r.hasFile && r.posAt > r.writtenAt) { std::vector<char> none; const std::vector<char> pos = EncodeRecord(r, none, r.posAt); SendMsg(to, MSG_RECORD, pos); out += pos.size(); }
    return out;
}
std::string PeerName(ENetPeer* p) { char b[64]; sprintf(b, "%u.%u.%u.%u:%u", p->address.host & 255, (p->address.host >> 8) & 255, (p->address.host >> 16) & 255, (p->address.host >> 24) & 255, p->address.port); return b; }
/* B13 / decision 49 - THE AUTHORITY IS THE OPERATOR, NOT THE FRONT OF THE CONNECTION ORDER. What stood
   here was `g_order[0] == from`: whoever CONNECTED first, which authority-model section 11 already names as
   the cause of T236a, and which made T239's authority flip - the relay was killed, the other game dialled
   first, and OPTIONS, WEATHER and the off-screen squad updates moved with it. The operator is the player
   this notebook was started for (--owner, from the panel or the harness), or the first player it ever heard
   from; either way it is in owner.txt and survives a restart. A world with no operator yet has no
   authority, which is a truthful 0 rather than a guess. */
void AnnounceAuthority(ENetHost* host)
{
    for (size_t i = 0; i < g_order.size(); ++i)
    {
        std::vector<char> b; PutU32(&b, PeerIsAuthority(g_order[i]) ? 1u : 0u);
        SendMsg(g_order[i], MSG_STORE_AUTH, b);
    }
    (void)host;
    Log("authority: the operator is '" + (g_ownerId.empty() ? std::string("(nobody yet)") : g_ownerId) + "' (decision 49)");
}

void OnDeletedBits(ENetHost* host, ENetPeer* from, const std::vector<char>& payload)
{
    std::string fac; size_t at = 0; unsigned n = 0;
    if (!GetStr(payload, &at, &fac) || !GetU32(payload, at, &n) || payload.size() < at + 4 + n) { Log("malformed DELETED_BITS from " + PeerName(from)); return; }
    std::vector<char>& b = g_bits[fac]; if (b.size() < n) b.resize(n, 0);
    for (size_t i = 0; i < n; ++i) b[i] = (char)((unsigned char)b[i] | (unsigned char)payload[at + 4 + i]);
    WriteBits(fac); Log("DELETED-BITS '" + SanitizeForLog(fac) + "' " + N((long long)n) + " bytes from " + PeerName(from));
    std::vector<char> buf(5 + payload.size()); buf[0] = (char)MSG_DELETED_BITS; unsigned len = (unsigned)payload.size(); memcpy(&buf[1], &len, 4); memcpy(&buf[5], &payload[0], payload.size());
    for (size_t i = 0; i < host->peerCount; ++i) { ENetPeer* p = &host->peers[i]; if (p->state == ENET_PEER_STATE_CONNECTED && p != from) SbSendRel(p, buf); }
}
void OnUniqueState(ENetHost* host, ENetPeer* from, const std::vector<char>& payload)
{
    std::string sid; size_t at = 0; unsigned st = 0, pi = 0;
    if (!GetStr(payload, &at, &sid) || !GetU32(payload, at, &st) || !GetU32(payload, at + 4, &pi) || sid.empty() || st > 2) { Log("malformed UNIQUE_STATE from " + PeerName(from)); return; }
    // review-p5j HIGH-2: DEAD IS TERMINAL. Nothing in the engine revives a dead entry (the shared setter refuses to
    // raise a stored 0 at all), so a later 1 or 2 for the same id can only be a game whose engine ERASED the entry -
    // and last-writer-wins would let that overwrite the permanent record every future joiner is given. Not stored,
    // not forwarded.
    std::map<std::string, UniqueRow>::const_iterator ex = g_uniques.find(sid);
    if (ex != g_uniques.end() && ex->second.state == 0 && st != 0)
    {
        Log("UNIQUE " + SanitizeForLog(sid) + " stays DEAD (ignored state " + N((long long)st) + " from " + PeerName(from) + ")");
        return;
    }
    UniqueRow r; r.state = (int)st; r.playerInvolved = pi != 0 ? 1 : 0;
    g_uniques[sid] = r; WriteUniques();
    Log("UNIQUE " + SanitizeForLog(sid) + " -> state " + N((long long)st) + " (from " + PeerName(from) + ")");
    std::vector<char> buf(5 + payload.size()); buf[0] = (char)MSG_UNIQUE_STATE; unsigned n = (unsigned)payload.size(); memcpy(&buf[1], &n, 4); if (!payload.empty()) memcpy(&buf[5], &payload[0], payload.size());
    for (size_t i = 0; i < host->peerCount; ++i) { ENetPeer* p = &host->peers[i]; if (FeedForwardTo(p, from, MSG_UNIQUE_STATE)) SbSendRel(p, buf); }
}
// E36 / decision 40 - a game asks for an option to change. REFUSED unless this is the game the store calls the
// authority, which is the front of the connection order and exactly the bit OnHello reports. The accepted map
// goes back to EVERY connected game INCLUDING THE SENDER, so no game ever acts on a value it set itself rather
// than on the value that was stored: there is one writer of the option on each game, and it is this broadcast.
void OnOptions(ENetHost* host, ENetPeer* from, const std::vector<char>& payload)
{
    unsigned n = 0; size_t at = 4;
    if (!GetU32(payload, 0, &n) || n > 64) { Log("malformed OPTIONS from " + PeerName(from)); return; }
    const bool auth = PeerIsAuthority(from);   /* B13 / decision 49: the OPERATOR, out of owner.txt - not whoever dialled first */
    int changed = 0, refusedKey = 0;
    std::vector<std::string> changedKeys;   /* P8l: WHICH keys moved, so a deferred write can be merged rather than replayed whole */
    /* review-p6h MEDIUM-1: the accepted pairs go into a CANDIDATE map. g_options is not touched until the
       candidate has actually reached the disk, so a failed write leaves the store exactly as it was - which is
       what makes "nothing was stored, nothing was broadcast" a true statement rather than a hope. */
    std::map<std::string, std::string> pending = g_options;
    for (unsigned i = 0; i < n; ++i)
    {
        std::string k, v;
        if (!GetStr(payload, &at, &k) || !GetStr(payload, &at, &v)) { Log("malformed OPTIONS from " + PeerName(from)); return; }
        if (!auth) { Log("OPTIONS '" + SanitizeForLog(k) + "' = '" + SanitizeForLog(v) + "' REFUSED from " + PeerName(from) + " - that game is not the session authority"); continue; }
        if (!OptionValueOk(k, v)) { ++refusedKey; Log("OPTIONS '" + SanitizeForLog(k) + "' = '" + SanitizeForLog(v) + "' REFUSED from " + PeerName(from) + " - unknown key, or a value this store does not accept"); continue; }
        std::map<std::string, std::string>::const_iterator have = pending.find(k);
        if (have != pending.end() && have->second == v) { Log("OPTIONS '" + SanitizeForLog(k) + "' already '" + SanitizeForLog(v) + "' - stored map unchanged"); continue; }
        pending[k] = v; ++changed; changedKeys.push_back(k);
        Log("OPTIONS '" + SanitizeForLog(k) + "' -> '" + SanitizeForLog(v) + "' (accepted from the authority " + PeerName(from) + ", not yet stored)");
    }
    if (changed == 0) { (void)refusedKey; return; }
    std::string why;
    /* P8l (review-p8d H-1). WHILE options.txt CANNOT BE READ, THE CHANGE IS KEPT AND BROADCAST AND THE FILE
       IS NOT TOUCHED. Refusing outright would be the deferral-that-consumes-its-trigger failure P8d removed
       from the position path; writing would put the defaults on disk over this world's real rules. The key
       is remembered so the merge can lay it back on top of whatever the file turns out to hold. */
    if (g_optionsLoadDeferred)
    {
        ++g_optionsWriteRefusedDeferred;
        for (size_t ck = 0; ck < changedKeys.size(); ++ck) g_optionsDeferredChanges[changedKeys[ck]] = pending[changedKeys[ck]];
        Log("OPTIONS: " + N((long long)changed) + " accepted change(s) are HELD IN MEMORY and broadcast -"
            " options.txt is on disk and could not be read this session, so it is not rewritten from a map"
            " that is mostly defaults. The change is merged onto the file the moment it opens."
            " optionsWriteRefusedDeferred=" + N(g_optionsWriteRefusedDeferred));
    }
    else if (!WriteOptionsMap(pending, &why))
    {
        /* The stored map is untouched, nothing is broadcast, and the game that asked is TOLD. The alternative
           is the failure this whole fold exists to remove: a policy every game obeys today and no game obeys
           after the next relay restart, with nothing in either log to say why. */
        SendOptionsRefusal(from, why);
        Log("OPTIONS: " + N((long long)changed) + " accepted change(s) DISCARDED - options.txt could not be written, so the stored map is unchanged, nothing was broadcast, and " + PeerName(from) + " has been told");
        return;
    }
    g_options = pending;
    const std::vector<char> m = EncodeOptions();
    for (size_t i = 0; i < host->peerCount; ++i) { ENetPeer* p = &host->peers[i]; if (p->state == ENET_PEER_STATE_CONNECTED) SendMsg(p, MSG_OPTIONS, m); }
    Log(g_optionsLoadDeferred
        ? std::string("OPTIONS map HELD IN MEMORY (options.txt is not written while its load is deferred) and broadcast to every connected game")
        : std::string("OPTIONS map stored and broadcast to every connected game"));
}
/* M14 fold (finding 9): fromName is the sender's name when the message ARRIVED; `connect` its connection then. */
void OnRecordGoneFrom(ENetHost* host, ENetPeer* from, unsigned connect, const std::string& fromName, const std::vector<char>& payload)
{
    std::string wid; size_t at = 0; unsigned lo = 0, hi = 0;
    if (!GetStr(payload, &at, &wid) || !GetU32(payload, at, &lo) || !GetU32(payload, at + 4, &hi)) { Log("malformed RECORD_GONE from " + fromName); return; }
    const long long stamp = (long long)(((unsigned long long)hi << 32) | lo);
    /* M14 rule (1): a job for this record's files is out - the delete waits behind it, in arrival order, and is
       decided when that write has landed (WriterApplyDone). A delete is never merged. */
    if (g_wqGate.Busy(coopwq::GateKey(wid))) { WriterHold(wid, MSG_RECORD_GONE, from, connect, fromName, payload, coopwq::kHeldOther, stamp); return; }
    std::map<std::string, Record>::iterator it = g_records.find(wid);
    if (it != g_records.end() && it->second.writtenAt > stamp) { Log("DELETE " + SanitizeForLog(wid) + " older than the record here - ignored"); return; }
    if (it != g_records.end()) g_records.erase(it);
    /* decision 28: the notebook lists the living. M14: the five deletes (RunJob) are a job on the writer thread,
       queued behind nothing (the gate above) and ahead of any later write to this id.
       P8c (review-p8b M-2). THE FALLBACK COPY GOES WITH THE RECORD IT WAS A COPY OF. A .prev pair left behind
       is a complete, validating record: the second pass of LoadIndex finds it and puts the deleted group back.
       The deleted-number bitmap normally stops that, but SetBit is a no-op for any id SplitGroupId rejects and
       WriteBits discards its own failures - so the bitmap is a second line of defence, not the only one.
       (The .prev pair and the .refused copy are deleted in RunJob, in the same job as the record.) */
    WriterQueueDelete(wid, stamp, from, connect, fromName, payload);
    g_pendingPos.erase(wid);   /* P8d: a held position for a record that no longer exists is not written */
    /* M14 fold (finding 2): THE DELETED-NUMBER BIT, THE DELETE LINE AND THE BROADCAST WAIT FOR THE FILES TO BE
       GONE - they run in WriterApplyOne when this job has landed, not here. Leaving the record out of memory
       now is safe: the gate holds every later message for this record until then. */
}
void OnRecordGone(ENetHost* host, ENetPeer* from, const std::vector<char>& payload) { OnRecordGoneFrom(host, from, from->connectID, PeerName(from), payload); }
/* M14: `connect` is the ENet connection id `from` had when the message ARRIVED - a held message is handled
   later, and the echo it earns must not reach a different game that has since taken the same peer slot. */
void OnRecordFrom(ENetHost* host, ENetPeer* from, unsigned connect, const std::string& fromName, const std::vector<char>& payload)
{
    Record r; std::vector<char> bytes;
    if (!ParseRecord(payload, &r, &bytes)) { Log("malformed RECORD from " + fromName); return; }
    /* M14 rule (1): A JOB FOR THIS RECORD IS OUT. This message is HELD, in arrival order, and decided when that
       write has landed - against what the disk then holds, which is what every decision below reads. So no
       message is ever decided against a write that has not happened yet, and no second job for one file is
       ever queued. */
    if (g_wqGate.Busy(coopwq::GateKey(r.worldId)))
    {
        /* M14 fold (finding 3): a position-only update with a newer stamp REPLACES the position-only update held
           right before it from this same connection (coopwq::HeldMergeable; its stamp is writtenAt, the field
           the not-newer refusal below compares). Full records and deletes are always appended. */
        WriterHold(r.worldId, MSG_RECORD, from, connect, fromName, payload, bytes.empty() ? coopwq::kHeldPosition : coopwq::kHeldOther, r.writtenAt);
        return;
    }
    { std::string fac; unsigned n = 0; if (SplitGroupId(r.worldId, &fac, &n)) { std::map<std::string, std::vector<char> >::const_iterator bt = g_bits.find(fac); if (bt != g_bits.end() && (n >> 3) < bt->second.size() && (((unsigned char)bt->second[n >> 3] >> (n & 7)) & 1)) { Log("RECORD " + SanitizeForLog(r.worldId) + " refused - its number is marked deleted"); return; } } }   /* review-p4h M2 */
    std::map<std::string, Record>::iterator it = g_records.find(r.worldId);
    if (bytes.empty())
    {
        // position-only update. P8b: the new state is assembled in a local and committed to g_records only
        // once the line has reached the disk, so memory and the folder cannot disagree with nothing saying so
        // (the rule review-p6h MEDIUM-1 set for options.txt, applied to the records).
        Record upd;
        if (it == g_records.end()) { upd = r; upd.posAt = r.writtenAt; upd.writtenAt = 0; upd.hasFile = false; upd.len = 0; upd.crc = 0; }
        else { if (it->second.posAt >= r.writtenAt) return; upd = it->second; upd.x = r.x; upd.y = r.y; upd.z = r.z; upd.posAt = r.writtenAt; }
        /* P8d (THE SECOND DOOR, review-p8c Q1(b) / M-2). A record whose upgrade was deferred carries
           hasFile = 1 with no length, and that pair of values is unique to the deferral. Writing it out as
           a v5 line would say `len 0` - "this record has no squad" - about a file that is on disk. The
           length and checksum are therefore taken from the file AT THE MOMENT OF THE WRITE (design
           principle 2), and while it still cannot be read the newer POSITION IS HELD AND RETRIED rather
           than dropped: P8c returned here, the position was gone, and no counter moved. */
        if (upd.hasFile && upd.len <= 0)
        {
            /* M14 rule (2): this read of the record file is safe from the writer - no job for this key is out
               (the gate at the top of this function), so nothing is rotating or swapping this file now. */
            const FileProbe fp = ProbeFile(FileOf(r.worldId));
            if (fp.kind == coopstore::kFileUnreadable) { HoldPosition(r.worldId, upd, fp); return; }
            if (fp.kind == coopstore::kFileReadable && fp.len > 0) { upd.len = fp.len; upd.crc = fp.crc; }
            else { upd.hasFile = false; upd.len = 0; upd.crc = 0; }   /* absent, or on disk with no bytes in it */
        }
        /* THE C-1 REPAIR OF review-p8c: a record that CARRIES a squad does NOT rotate its index line here.
           <id>.meta.prev is the only description of <id>.platoon.prev, and P8c overwrote it on every
           position update, which orphaned the squad the previous-version net exists to recover. */
        upd.seq = NextSeq();   /* B12 (decision 52): a position update IS a write this notebook accepted, so it advances the record's sequence number and a game holding an older seq for this key loses its queued replay to it */
        r.seq = upd.seq;   /* B12-c (review-b12 H-1): the forward carries the same stamped number */
        /* M14: THE COMMIT IS A JOB ON THE WRITER THREAD. Its failure line, the record in memory, the seq echo
           (B12-b), the dropped hold and the forward all follow in WriterApplyOne once the line is on disk -
           in that order, as they stood here - and not one of them before. */
        WriterQueueCommit(kFromPosition, upd, 0, PositionRotateMode(upd), from, connect, r, fromName);
        return;
    }
    else
    {
        if (it != g_records.end() && it->second.writtenAt >= r.writtenAt) { Log("RECORD " + SanitizeForLog(r.worldId) + " not newer - ignored"); return; }
        r.posAt = r.writtenAt; r.hasFile = true; r.gone = 0;
        r.len = (long long)bytes.size(); r.crc = coopstore::Crc32(&bytes[0], bytes.size());   /* P8b: the index line carries what the payload IS, not merely that a file exists */
        r.seq = NextSeq();   /* B12 (decision 52): stamped HERE, after the not-newer refusal above, so a refused message never consumes a number - the sender's own seq in the message is ignored */
        /* PAYLOAD FIRST, THEN META, and the meta line is the COMMIT POINT: a crash between the two leaves a
           good payload with a stale index line, which the next record for this group simply overwrites. */
        /* M14: THE COMMIT IS A JOB ON THE WRITER THREAD. Its failure line, the record in memory, the seq echo
           (B12-b), the dropped hold, the RECORD line and the forward all follow in WriterApplyOne once the
           line is on disk - in that order, as they stood here - and not one of them before. */
        WriterQueueCommit(kFromSquad, r, &bytes, kRotateBoth, from, connect, r, fromName);
    }
}
/* M14: THE FORWARD, MOVED OUT OF OnRecord UNCHANGED. It runs when the write has landed (WriterApplyOne); `from`
   is the writer's peer while it is still that connection, else 0 - a game that has since taken the slot is
   another game, and it gets the forward like every other. */
void RecordForward(ENetHost* host, ENetPeer* from, const Record& r, const std::vector<char>& bytes)
{
    /* B12-c (review-b12 H-1). THE FORWARD IS RE-ENCODED WITH THE STAMPED SEQUENCE, and it used to be the
       sender's payload byte for byte. A GAME ALWAYS SENDS 0 IN THAT FIELD (it may not choose the number
       that orders this folder's writes), so every live forward handed the other games a seq of 0 for a
       record this notebook had just stamped - and their cached number for that key was then nothing, so
       their own next queued change to it compared 0 against a real seq and was dropped as a foreign
       writer's. Re-encoding is faithful: ParseRecord read exactly the fields EncodeRecord writes, in the
       same order, and the only value that differs is the one this notebook owns. */
    const std::vector<char> fwd = EncodeRecord(r, bytes, r.writtenAt);
    std::vector<char> buf(5 + fwd.size()); buf[0] = (char)MSG_RECORD; unsigned n = (unsigned)fwd.size(); memcpy(&buf[1], &n, 4); if (!fwd.empty()) memcpy(&buf[5], &fwd[0], fwd.size());
    for (size_t i = 0; i < host->peerCount; ++i)
    {
        ENetPeer* p = &host->peers[i];
        if (!FeedForwardTo(p, from, MSG_RECORD)) continue;   /* T-313: connected, not the writer, and SUBSCRIBED (it asked - it has a world) */
        SbSendRel(p, buf);   /* M16: through this game's bound (and a refused packet no longer leaks) */
    }
}
void OnRecord(ENetHost* host, ENetPeer* from, const std::vector<char>& payload) { OnRecordFrom(host, from, from->connectID, PeerName(from), payload); }
/* M14: the peer a job or a held message came from, while it is STILL THAT CONNECTION. ENet hands a peer slot
   to the next game that connects, and an echo meant for the old one must not land on the new one. */
ENetPeer* SamePeer(ENetPeer* p, unsigned connect)
{
    return (p != 0 && p->state == ENET_PEER_STATE_CONNECTED && p->connectID == connect) ? p : 0;
}
/* LOOP THREAD. What followed each commit before M14, moved here verbatim and run once the job has landed.
   rule (4): a failed write is logged with the same line as before, and memory, echo and forward are skipped
   exactly as they were - the next RECORD for that group retries. */
void WriterApplyOne(WriteJob* j)
{
    ENetPeer* peer = SamePeer(j->peer, j->peerConnect);
    const std::string& id = j->key;
    if (j->from == kFromSquad)
    {
        if (!j->ok) { Log("RECORD " + SanitizeForLog(id) + " - could not write " + FileOf(id) + " and " + MetaOf(id) + "; the index in memory is left unchanged"); return; }
        const Record& r = j->rec;
        g_records[id] = r;
        SendSeqEcho(peer, id, r.seq);   /* B12-b: the writer learns the number its record got - now that it is on disk */
        DropPendingIfSuperseded(id, r.posAt);
        Log("RECORD " + SanitizeForLog(r.worldId) + " squad='" + SanitizeForLog(r.squadSid) + "' faction='" + SanitizeForLog(r.factionName) + "' pos=" + F1(r.x) + "," + F1(r.y) + "," + F1(r.z) + " bytes=" + N((long long)j->payload.size()) + " crc=" + HX8(r.crc) + " seq=" + N((long long)r.seq) + " from " + j->peerName);
        RecordForward(g_wqHost, peer, j->fwd, j->payload);
    }
    else if (j->from == kFromPosition)
    {
        if (!j->ok) { Log("RECORD " + SanitizeForLog(id) + " position - could not write " + MetaOf(id) + "; the index in memory is left unchanged"); return; }
        g_records[id] = j->rec;
        SendSeqEcho(peer, id, j->rec.seq);   /* B12-b: the writer learns the number its position update got */
        DropPendingIfSuperseded(id, j->rec.posAt);
        const std::vector<char> none;
        RecordForward(g_wqHost, peer, j->fwd, none);
    }
    else if (j->from == kFromHeldPosition)
    {
        if (!j->ok) return;   /* as before M14: the write's own lines said why; the hold stays and is retried next second */
        g_records[id] = j->rec;
        /* THE HOLD IS DROPPED BEFORE THE LINE IS PRINTED, so `pending` in that line is the count AFTER this
           record left the queue. Printed the other way round it reads as one still waiting. */
        long long defers = 0, waited = 0;
        std::map<std::string, PendingPos>::iterator it = g_pendingPos.find(id);
        if (it != g_pendingPos.end()) { defers = it->second.defers; waited = (long long)(NowSec() - it->second.firstAt); g_pendingPos.erase(it); }
        ++g_positionDeferredApplied;
        Log("RECORD " + SanitizeForLog(id) + " position - the HELD update has been written (" + N(defers)
            + " update(s) waited " + N(waited) + " s for the file to open; the"
            " record now carries " + N(j->rec.len) + " bytes, crc " + HX8(j->rec.crc) + ")." + PositionDeferredToken());
    }
    else if (j->from == kFromDelete)
    {
        /* M14 fold (finding 2): THE FILES ARE GONE NOW (RunJob), so the number is marked deleted, the DELETE line
           printed and every other game told - never before. (Memory dropped the record when the delete was
           queued; the gate held every later message for it until this point.) The broadcast skips the sender
           only while it is still that connection: a game that has since taken its slot is told like any other. */
        SetBit(id);   // decision 30: permanent
        Log("DELETE " + SanitizeForLog(id) + " stamp=" + N(j->stamp) + " from " + j->peerName);
        const std::vector<char>& payload = j->payload;
        std::vector<char> buf(5 + payload.size()); buf[0] = (char)MSG_RECORD_GONE; unsigned n = (unsigned)payload.size(); memcpy(&buf[1], &n, 4); if (!payload.empty()) memcpy(&buf[5], &payload[0], payload.size());
        ENetHost* host = g_wqHost;
        for (size_t i = 0; i < host->peerCount; ++i) { ENetPeer* p = &host->peers[i]; if (FeedForwardTo(p, peer, MSG_RECORD_GONE)) SbSendRel(p, buf); }   /* T-313: subscribed games only - a game at the title learns the deletion from the DELETED_BITS its sender also sent, and its next snapshot no longer holds the record */
    }
}
/* LOOP THREAD. The completions, in the order the writer finished them - which is queue order. Each key is
   released and its held messages handled at once, oldest first, until one of them queues a job again
   (TakeHeld refuses a busy key) - so every key's messages keep their arrival order (rule 1). */
void WriterApplyDone()
{
    std::deque<WriteJob*> done;
    EnterCriticalSection(&g_wqLock.cs);
    done.swap(g_wqDone);
    LeaveCriticalSection(&g_wqLock.cs);
    for (size_t i = 0; i < done.size(); ++i)
    {
        WriteJob* j = done[i];
        const std::string key = j->gate;
        g_wqGate.Finish(key);
        WriterApplyOne(j);
        delete j;
        HeldMsg m;
        while (g_wqGate.TakeHeld(key, &m))
        {
            if (m.type == MSG_RECORD) OnRecordFrom(g_wqHost, m.peer, m.connect, m.peerName, m.payload);
            else OnRecordGoneFrom(g_wqHost, m.peer, m.connect, m.peerName, m.payload);
        }
    }
}
/* rule (2), THE DRAINING HALF: every write out lands and is applied - and every message held behind one is
   handled and ITS write lands too - before the caller reads record files or hands the index out. It waits on
   the disk, so it is called only where a whole-folder read follows (a HELLO's push, a REPAIR), never per
   message. */
void WriterFlushAll()
{
    if (g_wqIdle == 0) return;   /* the writer has not started: nothing can be out */
    for (;;)
    {
        WaitForSingleObject(g_wqIdle, INFINITE);
        WriterApplyDone();
        EnterCriticalSection(&g_wqLock.cs);
        const bool drained = g_wq.Idle() && g_wqDone.empty();
        LeaveCriticalSection(&g_wqLock.cs);
        if (drained && g_wqGate.BusyCount() == 0) return;
    }
}

/* T-313 (protocol 63): THE RECORD FEED'S HANDLER. OFF unsubscribes. ASK subscribes FIRST - so no live forward handled after
   this line can be missed - and then sends, on reliable channel 0 in this one pass: for the first page ("" after-key) BEGIN and
   every unique state; then records strictly after the key until ~1 MB (coopfeed::FeedPageWalk); then PAGE_END. Every live
   forward queued after this pass therefore arrives after the page. Every write out lands first (M14 rule (2)), exactly as
   the WELCOME push this replaces did. */
/* T-346 slice 1 (protocol 67; owner 255-256): THE RECORDS BEFORE A GAME'S LOAD (src/common/preload.h). INDEX_ASK is answered with
   one INDEX_PAGE (key, file time, position time of every record after the key, ascending, paged); FETCH with the named records -
   SendRecordTo, the feed's own send - and then FETCH_END, in this one pass on reliable channel 0, so the records arrive first.
   Neither subscribes the game to the feed (it has no world yet). Every write out lands first (M14 rule (2)), as for the feed. */
struct PreFetchSender   /* T-346 fold 1: SendRecordTo, adding up the bytes of one FETCH answer */
{
    ENetPeer* to;
    size_t bytes;
    explicit PreFetchSender(ENetPeer* p) : to(p), bytes(0) {}
    size_t operator()(const Record& r) { const size_t b = SendRecordTo(to, r); bytes += b; return b; }
};
struct PreStampRec
{
    void operator()(const Record& r, unsigned long long* f, unsigned long long* p) const
    {
        *f = (r.hasFile && r.writtenAt > 0) ? (unsigned long long)r.writtenAt : 0ULL;   /* the stamp SendRecordTo's full RECORD carries */
        *p = r.posAt > 0 ? (unsigned long long)r.posAt : 0ULL;
    }
};
void OnPreload(ENetPeer* from, const std::vector<char>& payload)
{
    cooppre::PreMsg m;
    if (payload.empty() || !cooppre::PreDecode(&payload[0], payload.size(), &m) || (m.kind != (unsigned)cooppre::kPreIndexAsk && m.kind != (unsigned)cooppre::kPreFetch))
    {
        ++g_preMalformed;
        Log("malformed (or down-only) PRELOAD kind from " + PeerName(from) + " - ignored (preload[malformed])");
        return;
    }
    if (g_peerId.find(from) == g_peerId.end())
    {
        ++g_preNotAdmitted;
        Log("PRELOAD from " + PeerName(from) + ", a connection that has not been welcomed - ignored (preload[notAdmitted])");
        return;
    }
    if (m.kind == (unsigned)cooppre::kPreIndexAsk)
    {
        ++g_preIndexAsks;
        if (m.key.empty()) WriterFlushAll();
        std::vector<cooppre::IndexEntry> es; std::string lastKey; PreStampRec st;
        bool last = cooppre::PreIndexWalk(g_records, m.key, cooppre::kPreIndexPageEntries, cooppre::kPreIndexPageBytes, st, &es, &lastKey);
        std::vector<char> b;
        if (!cooppre::PreEncodeIndexPage(&b, m.token, last, es))
        {   /* the walk leaves out any key over the bound, so this is not reached - but an ask is ALWAYS answered */
            ++g_preMalformed; b.clear(); es.clear(); last = true; cooppre::PreEncodeIndexPage(&b, m.token, true, es);
        }
        SendMsg(from, MSG_RECORD_FEED, b);
        ++g_preIndexPages; g_preIndexEntries += (long long)es.size();
        Log("PRELOAD index page to " + PeerName(from) + " (token " + N((long long)m.token) + "): " + N((long long)es.size()) + " rows after '" + SanitizeForLog(m.key)
            + "' through '" + SanitizeForLog(lastKey) + "'" + (last ? " - the LAST page; this world holds " + N((long long)g_records.size()) + " records" : std::string(" - more follow when it asks")));
        return;
    }
    ++g_preFetches;
    /* T-346 fold 1 (review 2026-09-30, MED): one answer is about coopfeed::kFeedPageBytes (at least one key), read from disk on this
       thread like a feed page, and FETCH_END says how many of the keys it covered - the game asks again (resume) for the rest. The
       queue is written out on a batch's FIRST fetch only; its rest reads what that flush put on disk, as the feed's later pages do. */
    if (m.resume == 0) WriterFlushAll();
    unsigned int sent = 0, missing = 0; PreFetchSender snd(from);
    const unsigned int answered = cooppre::PreFetchWalk(g_records, m.keys, coopfeed::kFeedPageBytes, snd, &sent, &missing);   /* missing: deleted since the index - its RECORD_GONE reaches every game */
    std::vector<char> b; cooppre::PreEncodeFetchEnd(&b, m.token, answered, sent);
    SendMsg(from, MSG_RECORD_FEED, b);
    const bool cut = answered < (unsigned int)m.keys.size();
    g_preFetchKeys += (long long)answered; g_preFetchSent += (long long)sent; g_preFetchMissing += (long long)missing; if (cut) ++g_preFetchCut;
    Log("PRELOAD fetch from " + PeerName(from) + " (token " + N((long long)m.token) + (m.resume ? ", the rest of a batch" : "") + "): " + N((long long)m.keys.size()) + " asked, "
        + N((long long)answered) + " answered (" + N((long long)sent) + " sent, " + N((long long)snd.bytes) + " bytes"
        + (cut ? std::string(" - cut at the ~1 MB cap; the game asks again for the rest)") : std::string(")")) + ", then FETCH_END");
}
struct FeedPageSender
{
    ENetPeer* to;
    size_t bytes;
    explicit FeedPageSender(ENetPeer* p) : to(p), bytes(0) {}
    size_t operator()(const std::string& key, const Record& r) { (void)key; const size_t b = SendRecordTo(to, r); bytes += b; return b; }
};
void OnRecordFeed(ENetPeer* from, const std::vector<char>& payload)
{
    if (!payload.empty() && cooppre::IsPreKind(&payload[0], payload.size())) { OnPreload(from, payload); return; }   /* T-346 slice 1: kinds 5-8 are the records-before-the-load exchange */
    coopfeed::FeedMsg m;
    if (payload.empty() || !coopfeed::FeedDecode(&payload[0], payload.size(), &m) || (m.kind != (unsigned)coopfeed::kFeedAsk && m.kind != (unsigned)coopfeed::kFeedOff))
    {
        ++g_feedMalformed;
        Log("malformed RECORD_FEED from " + PeerName(from) + " - ignored (feed[malformed])");
        return;
    }
    if (g_peerId.find(from) == g_peerId.end())
    {
        ++g_feedNotAdmitted;
        Log("RECORD_FEED from " + PeerName(from) + ", a connection that has not been welcomed - ignored (feed[notAdmitted])");
        return;
    }
    if (m.kind == (unsigned)coopfeed::kFeedOff)
    {
        const bool was = FeedOn(from);
        g_feedOn.erase(from); ++g_feedOffs;
        Log("FEED OFF from " + PeerName(from) + (was ? " - its world is going away; it is sent no live record or unique state until it asks again (deletes still go to it)"
                                                      : " - it was not subscribed"));
        return;
    }
    ++g_feedAsks;
    if (m.key.empty()) WriterFlushAll();   /* review L5: the first page only */
    g_feedOn[from] = 1;
    const unsigned long long seqHigh = WorldSeqHighNow();
    if (m.key.empty())
    {
        std::vector<char> bg; coopfeed::FeedEncodeBegin(&bg, (unsigned)g_records.size(), (unsigned)g_uniques.size(), seqHigh);
        SendMsg(from, MSG_RECORD_FEED, bg); ++g_feedBegins;
        for (std::map<std::string, UniqueRow>::const_iterator ut = g_uniques.begin(); ut != g_uniques.end(); ++ut) SendMsg(from, MSG_UNIQUE_STATE, EncodeUnique(ut->first, ut->second));   /* decision 31(c): the whole state map, before the records */
        Log("FEED BEGIN to " + PeerName(from) + ": " + N((long long)g_records.size()) + " records, " + N((long long)g_uniques.size()) + " unique states, seqHigh " + N((long long)seqHigh));
    }
    FeedPageSender snd(from); std::string lastKey; unsigned int sent = 0;
    bool last = coopfeed::FeedPageWalk(g_records, m.key, coopfeed::kFeedPageBytes, coopfeed::kFeedPageRecords, snd, &lastKey, &sent);   /* review L3: bytes AND records */
    std::vector<char> pe;
    if (!coopfeed::FeedEncodePageEnd(&pe, lastKey, sent, last, seqHigh))
    {   /* review L2: an ASK is ALWAYS answered, or that game's feed stalls for the link - an empty LAST page */
        ++g_feedMalformed; pe.clear(); coopfeed::FeedEncodePageEnd(&pe, std::string(), 0, true, seqHigh); last = true;
        Log("FEED: a key over the wire's bound - " + PeerName(from) + " is answered with an empty LAST page (feed[malformed])");
    }
    SendMsg(from, MSG_RECORD_FEED, pe);
    ++g_feedPages; if (last) ++g_feedEnds; g_feedRecordsSent += sent; g_feedBytesSent += (long long)snd.bytes;
    Log("FEED page to " + PeerName(from) + ": " + N((long long)sent) + " records (" + N((long long)snd.bytes) + " bytes) after '" + SanitizeForLog(m.key)
        + "' through '" + SanitizeForLog(lastKey) + "'" + (last ? " - the LAST page; this world holds " + N((long long)g_records.size()) + " records" : std::string(" - more follow when it asks")));
}

/* B13-c: A CONNECTION IS GONE - by its own leave (the ENet DISCONNECT event) or because its player came back
   on a new socket and the notebook evicted the silent old one (OnHello). One routine, so the two paths cannot
   drift: the connection loses its place in the order, its slot and its vote; the PLAYER keeps its number and
   its areas (g_slotById, slots.txt and g_areas are not touched, which is B13's point). Returns whether the
   departed connection was the operator's: the DISCONNECT path re-tells everyone; the eviction path need not,
   because the same id is about to be welcomed and PeerIsAuthority is decided by id. */
/* ---- settings5 S5 + fold (review-settings5 1a/1b): THE WORLD'S MOD LIST (mods.txt beside options.txt) ----
   Only the operator's game writes it (at its greeting, or when AppointFirstOperator names it). Every admitted game's
   greeting list is kept in g_peerMods, so after EVERY write the games already in are re-checked, and one that differs
   is refused and disconnected - a provisional admit (no record yet) is never let stay on a list it could not match. */
long long g_helloRefusedMods = 0, g_helloRefusedModsUnreadable = 0, g_modsMatched = 0, g_modsRecorded = 0, g_modsNotChecked = 0,
          g_modsProvisional = 0, g_modsRecheckRefused = 0, g_modsAdmittedDiffers = 0, g_modsRecheckKept = 0;   /* T-246: the warn policy's two counts */
std::map<ENetPeer*, coopmods::ModList> g_peerMods;   /* an admitted connection -> the list its greeting carried */
std::set<ENetPeer*> g_modsProvisionalPeers;   /* T-246 fold 2 (item 2): admitted PROVISIONALLY, not yet re-checked (ModsRecheckProvisional) */
std::string ModsFile() { return g_dir + "\\mods.txt"; }
bool ModsRecordRead(coopmods::ModList* out)
{
    std::vector<std::string> lines;
    *out = coopmods::ModList();
    if (!ReadWholeFileLines(ModsFile(), &lines)) return false;
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i) text += lines[i] + "\n";
    return coopmods::ModListFromText(text, out);
}
void ModsRefuseSend(ENetPeer* to, unsigned gameProto, const coopmods::ModList& world)
{
    std::vector<char> rb; PutU32(&rb, kProtocol); PutU32(&rb, gameProto); PutU32(&rb, (unsigned)coopstore::kRefuseMods);
    PutU32(&rb, 0u); PutU32(&rb, 0u);
    coopmods::ModListEncode(world, &rb);
    SendMsg(to, MSG_STORE_REFUSE, rb);
}
/* After a write: every connected game other than the writer, judged against the new record. */
void ModsRecheckAll(ENetPeer* writer, const coopmods::ModList& record)
{
    std::vector<ENetPeer*> out;
    for (std::map<ENetPeer*, coopmods::ModList>::const_iterator it = g_peerMods.begin(); it != g_peerMods.end(); ++it)
        if (it->first != writer && g_modsProvisionalPeers.count(it->first) == 0 && coopmods::ModsRecheckRefuses(record, it->second)) out.push_back(it->first);   /* T-246 fold 2: provisional games are ModsRecheckProvisional's */
    for (size_t i = 0; i < out.size(); ++i)
    {
        const coopmods::ModDiff d = coopmods::ModListDiff(record, g_peerMods[out[i]]);
        if (!coopmods::ModsRecheckRefusesPolicy(record, g_peerMods[out[i]], coopmods::kModsMismatchPolicyNow))
        {   /* T-246 (owner 199 b): under the warn policy the re-check only logs - the game stays (it is not told: LEFTOVER of T-246) */
            ++g_modsRecheckKept;
            Log("[MODS] differs, kept in: re-checked after mods.txt was written - " + PeerName(out[i]) + " id='" + PeerIdOf(out[i])
                + "' is connected and its mods differ from the list just written (it came in before this list: with no list yet, matching the earlier list, or let in with differing mods). " + SanitizeForLog(coopmods::ModDetailText(d))
                + ". The refusal is off (owner 199 b; T-247), so it stays; it is not told in the game (there is no message for that yet). modsRecheckKept=" + N(g_modsRecheckKept));
            continue;
        }
        ++g_modsRecheckRefused;
        ModsRefuseSend(out[i], kProtocol, record);
        Log("[MODS] refused: re-checked after mods.txt was written - " + PeerName(out[i]) + " id='" + PeerIdOf(out[i])
            + "' is connected and its mods differ from the list just written (it came in before this list: with no list yet, matching the earlier list, or let in with differing mods). " + SanitizeForLog(coopmods::ModDetailText(d))
            + ". Refused and disconnected. modsRecheckRefused=" + N(g_modsRecheckRefused));
        SbDisconnectLater(out[i]);
    }
    if (out.empty()) Log("[MODS] re-check after the write: every connected game with a readable list matches (" + N((long long)(g_peerMods.size())) + " connected"
                         + (g_modsProvisionalPeers.empty() ? std::string(")") : std::string("; the provisional ones are re-checked next)")));
}
/* T-246 fold 2 (item 2): the games let in PROVISIONALLY, re-checked once the world's list is decided - after every write, and at the
   first operator's appointment whether or not its list was written. `op` is the operator's own connection (never judged). */
void ModsRecheckProvisional(ENetPeer* op, bool haveWorld, const coopmods::ModList& world, const std::string& why)
{
    std::vector<ENetPeer*> list(g_modsProvisionalPeers.begin(), g_modsProvisionalPeers.end());
    g_modsProvisionalPeers.clear();
    for (size_t i = 0; i < list.size(); ++i)
    {
        ENetPeer* p = list[i];
        if (p == op) continue;
        std::map<ENetPeer*, coopmods::ModList>::const_iterator pm = g_peerMods.find(p);
        if (pm == g_peerMods.end()) continue;
        const int a = coopmods::ModsProvisionalRecheck(haveWorld ? 1 : 0, world, pm->second, coopmods::kModsMismatchPolicyNow);
        const std::string who = PeerName(p) + " id='" + PeerIdOf(p) + "'";
        if (a == coopmods::kMprAdmitDiffers)
        {
            ++g_modsAdmittedDiffers;
            Log("[MODS] differs, let in: " + who + " was let in PROVISIONALLY and is re-checked now (" + why + "): its mods differ from the world's list. "
                + SanitizeForLog(coopmods::ModDetailText(coopmods::ModListDiff(world, pm->second)))
                + ". The refusal is off (owner 199 b; T-247), so it stays; it is not told in the game (there is no message for that yet). modsAdmittedDiffers=" + N(g_modsAdmittedDiffers));
        }
        else if (a == coopmods::kMprRefused)
        {
            ++g_modsRecheckRefused;
            ModsRefuseSend(p, kProtocol, world);
            Log("[MODS] refused: " + who + " was let in PROVISIONALLY and is re-checked now (" + why + "): its mods differ from the world's list. "
                + SanitizeForLog(coopmods::ModDetailText(coopmods::ModListDiff(world, pm->second))) + ". Refused and disconnected. modsRecheckRefused=" + N(g_modsRecheckRefused));
            SbDisconnectLater(p);
        }
        else if (a == coopmods::kMprMatch)
            Log("[MODS] world list matches: " + who + " was let in PROVISIONALLY and is re-checked now (" + why + ")");
        else
            Log("[MODS] not checked: " + who + " was let in PROVISIONALLY and is re-checked now (" + why + "): "
                + std::string(haveWorld ? "its own list is unreadable" : "the world has no list") + " - nothing to compare");
    }
}
/* THE ONE WRITER of mods.txt, then the re-check. `who` names the operator's game for the log. */
bool ModsRecordWrite(ENetPeer* writer, const coopmods::ModList& m, const std::string& who, bool replacing)
{
    if (!WriteWholeFile(ModsFile(), coopmods::ModListToText(m)))
    { Log("[MODS] mods.txt could NOT be written (" + who + ") - the world has no new list"); return false; }
    ++g_modsRecorded;
    Log("[MODS] mods.txt written from " + who + " (" + N((long long)m.active.size()) + " mods, " + N((long long)m.plugins.size())
        + " plugin DLLs)" + (replacing ? "; it REPLACES the earlier list (the operator's mods changed)" : "") + ". modsRecorded=" + N(g_modsRecorded));
    ModsRecheckAll(writer, m);
    ModsRecheckProvisional(writer, true, m, "against the list just written");   /* T-246 fold 2 (item 2) */
    return true;
}

/* M8 (T-197 piece 8; protocol 62; owner decisions 53, 54) - A PLAYER LEAVING IS ONE SLOT LEAVING. M8 review F1: AND A PLAYER WHOSE
   WORLD-SERVER LINK BLIPPED HAS NOT LEFT (T240). PeerGone HOLDS the news (PlayerGoneHoldStart, while the connection still holds its
   slot and id - a lobby connection or one never admitted holds no player's rows on any game and is not announced) and it is decided
   by cooppg::PlayerGoneHoldDecide: that player admitted again within kGraceSec - a re-dial, or an eviction whose new connection is
   that very player - CANCELS it (PlayerGoneHoldReturned, at the admission) and nothing is sent; past kGraceSec with the player still
   away, the 1 Hz PlayerGoneHoldTick sends it (PlayerGoneTell) to every remaining admitted game. An eviction is held like any close
   rather than never announced: its new HELLO may pick another profile (another slot) or be refused, and then the evicted player HAS
   gone; when it is that same player, its admission in that same HELLO cancels the hold at once. The loaded marks
   (AreaRow::lastSeen[slot], the AREAMAP's loadedMask bit) follow the same rule - erased from every area row at the SEND, not at the
   close: at the close they would take a blipping player's areas out of the next maps (and pass its leases on) while it is still
   playing; by the send they are older than kGraceSec, so no map lists them any more and the erase only frees the rows. The delivery
   set (g_liveAreas) still goes at the close (M6: a returning game reports afresh and is caught up). The area OWNERSHIP rows are not
   touched: a holder's lease is the B13 grace's business. The uid seat is freed by SeatFree in PeerGone, as before. */
void PlayerGoneHoldStart(ENetPeer* gone, int slot, const char* how)
{
    if (slot < 0 || g_peerId.count(gone) == 0) return;
    PgHold& h = g_playerGoneHolds[slot];
    h.at = NowSec(); h.name = PeerName(gone); h.how = how;
    ++g_playerGoneHeld;
    Log("PLAYER_GONE slot " + N((long long)slot) + " (" + h.name + ", " + how + "): HELD for " + N((long long)kGraceSec)
        + " s - cancelled if that player is admitted again within it (a world-server link blip is not a leave, T240; playerGone held " + N(g_playerGoneHeld) + ")");
}
/* the player of `slot` is admitted again (OnHello, once the connection holds its slot and id): its hold, if any, is cancelled */
void PlayerGoneHoldReturned(int slot, ENetPeer* back)
{
    std::map<int, PgHold>::iterator it = g_playerGoneHolds.find(slot);
    if (it == g_playerGoneHolds.end()) return;
    const double now = NowSec();
    if (cooppg::PlayerGoneHoldDecide(it->second.at, now, kGraceSec, true) != cooppg::kPgHoldCancel) return;
    ++g_playerGoneCancelledReturned;
    Log("PLAYER_GONE slot " + N((long long)slot) + " CANCELLED: that player is back on " + PeerName(back) + " " + F1((float)(now - it->second.at))
        + " s after its close (" + it->second.how + ") - nothing sent, its loaded marks kept (playerGone cancelledReturned " + N(g_playerGoneCancelledReturned) + ")");
    g_playerGoneHolds.erase(it);
}
void PlayerGoneTell(int slot, const PgHold& h, double now)
{
    long long cells = 0;
    for (std::map<long long, AreaRow>::iterator it = g_areas.begin(); it != g_areas.end(); ++it) cells += (long long)it->second.lastSeen.erase(slot);
    g_playerGoneLoadedDropped += cells;
    std::vector<char> b;
    if (!cooppg::PlayerGoneEncode(&b, (unsigned int)slot))
    {
        ++g_playerGoneSendFailed;
        Log("PLAYER_GONE: slot " + N((long long)slot) + " (" + h.name + ") does not fit a u16 - not sent (playerGone sendFailed " + N(g_playerGoneSendFailed) + ")");
        return;
    }
    long long told = 0;
    for (std::map<ENetPeer*, int>::const_iterator it = g_slot.begin(); it != g_slot.end(); ++it)
    {
        ENetPeer* q = it->first;
        if (it->second == slot || q->state != ENET_PEER_STATE_CONNECTED || g_peerId.count(q) == 0) continue;
        if (SendMsg(q, MSG_PLAYER_GONE, b)) ++told; else ++g_playerGoneSendFailed;
    }
    ++g_playerGoneSent; g_playerGoneTold += told;
    Log("PLAYER_GONE slot " + N((long long)slot) + " (" + h.name + ", " + h.how + "): sent to " + N(told) + " remaining games after its "
        + F1((float)(now - h.at)) + " s hold (the player was not admitted again); " + N(cells) + " loaded marks dropped (M8)");
}
/* M8 review F1: the 1 Hz tick decides every hold - past kGraceSec it is sent; a player found admitted again (a backstop to
   PlayerGoneHoldReturned) cancels it. The decided slots are collected first and erased after the walk. */
void PlayerGoneHoldTick()
{
    if (g_playerGoneHolds.empty()) return;
    const double now = NowSec();
    std::vector<int> done;
    for (std::map<int, PgHold>::const_iterator it = g_playerGoneHolds.begin(); it != g_playerGoneHolds.end(); ++it)
    {
        bool back = false;
        for (std::map<ENetPeer*, int>::const_iterator q = g_slot.begin(); q != g_slot.end(); ++q) if (q->second == it->first && g_peerId.count(q->first) != 0) { back = true; break; }
        const int act = cooppg::PlayerGoneHoldDecide(it->second.at, now, kGraceSec, back);
        if (act == cooppg::kPgHoldWait) continue;
        if (act == cooppg::kPgHoldSend) PlayerGoneTell(it->first, it->second, now);
        else
        {
            ++g_playerGoneCancelledReturned;
            Log("PLAYER_GONE slot " + N((long long)it->first) + " CANCELLED at the tick: that player is connected again " + F1((float)(now - it->second.at))
                + " s after its close (" + it->second.how + ") - nothing sent (playerGone cancelledReturned " + N(g_playerGoneCancelledReturned) + ")");
        }
        done.push_back(it->first);
    }
    for (size_t i = 0; i < done.size(); ++i) g_playerGoneHolds.erase(done[i]);
}

bool PeerGone(ENetPeer* p, const char* how)
{
    Log(std::string(how) + ": " + PeerName(p));
    g_peerMods.erase(p);   /* settings5 fold */
    g_modsProvisionalPeers.erase(p);   /* T-246 fold 2 */
    g_feedOn.erase(p);   /* T-313: the subscription goes with the connection - a returning game asks again */
    const bool wasAuth = PeerIsAuthority(p);   /* B13: the operator left. Nobody inherits the flag - it is that player's until owner.txt says otherwise - but everyone is re-told, so no game is left believing it is still the authority */
    for (size_t i = 0; i < g_order.size(); ++i) if (g_order[i] == p) { g_order.erase(g_order.begin() + i); break; }
    { const int goneSlot = SlotOf(p); if (goneSlot >= 0) { g_playerSec.erase(goneSlot); g_liveAreas.erase(goneSlot); PlayerGoneHoldStart(p, goneSlot, how); } }   /* M8 review F1: the news is HELD for kGraceSec and cancelled if that player is admitted again; PlayerGoneHoldTick sends it and drops the loaded marks then */   /* M6: the delivery set goes with the connection - a returning game reports afresh and is caught up */   /* E25: the player sector goes with the slot - the freshness window would drop it anyway, but a slot handed to the NEXT game to connect must not inherit where the last one's player stood */
    /* B13: the CONNECTION loses its number; the PLAYER keeps it. g_slotById and slots.txt are not
       touched here, which is the whole point - a player who logs off and comes back gets the same
       number, and the areas and records that carry it still name them. */
    { const std::string goneId = PeerIdOf(p); if (!goneId.empty()) { g_slotSeenAt[goneId] = NowUnix(); WriteSlots(); } }
    const bool wasAdmittedJ = g_peerId.count(p) != 0;   /* M11a S1: a roster change only when an admitted game leaves */
    g_join.erase(p);
    g_peerId.erase(p);
    SeatFree(p, how);   /* owner 205 A: the uid seat is freed with the connection (unlink, ENet timeout, eviction, HELLO deadline) - its high-water stays */
    g_slot.erase(p);   /* review-p4o HIGH-1: the number is freed; nobody else's moves */
    g_votes.erase(p);   /* E40 / decision 45: A DEPARTED GAME DOES NOT VOTE. Under consensus a left-behind 0 would hold the whole world paused for everyone still playing, with nothing in any log to say why */
    g_helloDueFrom.erase(p);   /* M1-b (review-m1 M2): a connection that has gone owes no HELLO */
    g_lobby.erase(p);   /* prof1: nor does it wait in the lobby */
    if (wasAdmittedJ) RosterBroadcast("a game left");   /* M11a S1: the rest hear it (M8's PLAYER_GONE will say whose copies to drop) */
    return wasAuth;
}

/* M14 fold 2: the loop CLOSED this connection itself (PeerGone done, reset with no event). Its deferred events
   belong to a connection that no longer exists: before fold 2 they would have waited in the network layer and
   been thrown away by that reset, so they are thrown away here too - handled later they would re-admit a dead
   connection (a HELLO) or tear down a later one (a DISCONNECT). Counted deferred[discarded]. */
void DeferDiscard(ENetPeer* p)
{
    std::deque<ENetEvent> gone;
    const long long n = g_deferred.Discard(p, &gone);
    for (size_t i = 0; i < gone.size(); ++i) if (gone[i].type == ENET_EVENT_TYPE_RECEIVE && gone[i].packet != 0) enet_packet_destroy(gone[i].packet);
    if (n <= 0) return;
    g_deferDiscarded += n;
    Log("M14: " + N(n) + " deferred event(s) of " + PeerName(p) + ", a connection this server has just closed, thrown away (counted deferred[discarded])");
}
/* M1-b (review-m1 M2): THE HELLO DEADLINE, on the 1 Hz tick - see kHelloDeadlineSec for why it is a timer. The
   late ones are collected first because PeerGone erases from the map being walked. */
void HelloDeadlineTick()
{
    const double now = NowSec();
    std::vector<ENetPeer*> late;
    for (std::map<ENetPeer*, double>::const_iterator it = g_helloDueFrom.begin(); it != g_helloDueFrom.end(); ++it)
        if (now - it->second >= kHelloDeadlineSec) late.push_back(it->first);
    for (size_t i = 0; i < late.size(); ++i)
    {
        ENetPeer* p = late[i];
        ++g_connHelloTimeout;
        Log("M1-b: " + PeerName(p) + " connected " + F1((float)(now - g_helloDueFrom[p])) + " s ago and no HELLO has admitted it"
            " (it sent none, sent a malformed one, or was refused and has not left) - it is closed so it does not hold a seat."
            " Counted conn[helloTimeout].");
        PeerGone(p, "closed (no HELLO within the deadline)");   /* the DISCONNECT event's work, because none will come... */
        enet_peer_disconnect_now(p, 0);                          /* ...this resets the peer with NO event */
        DeferDiscard(p);                                         /* M14 fold 2 */
    }
}

/* ======================= prof1 (docs/design-profiles1.md s1, s4 step 4) - PROFILES IN THE WORLD =======================
   A profile is one crew of one person in this world, and to everything below the HELLO it is a separate PLAYER: its own slot
   (keyed coopprof::SlotKey - profile 1 is the bare person id, so a world from before profiles hands each person's slot, areas
   and operator flag to their profile 1), its own save folder on its player's PC, its own faction name. profiles.txt holds one
   line per profile and keeps deleted ones for good: a deleted profile's number and slot are RETIRED (its key stays in
   slots.txt and nothing binds it again), because reuse would hand a new profile the old one's records and base.
   THE LOBBY: a HELLO carrying profile 0 is answered with PROFILES instead of a WELCOME; the game then asks NEW / DELETE
   (PROFILES up) or picks by saying HELLO again with a number. The host's own game goes through the same door. */
std::string ProfilesFile() { return g_dir + "\\profiles.txt"; }
unsigned ProfileCap()
{
    std::map<std::string, std::string>::const_iterator it = g_options.find("profilecap");
    return coopprof::CapFromOption(it == g_options.end() ? std::string() : it->second);
}
void LoadProfiles()
{
    g_profiles.clear();
    std::vector<std::string> lines;
    if (!ReadWholeFileLines(ProfilesFile(), &lines)) { Log("profiles: no profiles.txt - a person's first profile is made when their game first joins"); return; }
    int newer = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (coopprof::RowLineNewer(lines[i])) ++newer;   /* T-222 */
        if (!lines[i].empty() && !coopprof::RowsAddLine(&g_profiles, lines[i])) ++g_profBadLines;   /* T-201 PP6': the rule the host's CHANGE DELETE reads with */
    }
    Log("profiles: " + N((long long)g_profiles.size()) + " profile(s) restored from profiles.txt (" + N(g_profBadLines) + " line(s) dropped as unreadable or repeated)");
    if (newer > 0)
    {
        g_profReadOnly = 1;
        Log("profiles: ERROR - " + N((long long)newer) + " line(s) of profiles.txt have more fields than this build knows (a newer build wrote them). The file is"
            " READ-ONLY to this build: changes hold in memory and are never written, so those profiles are not erased (T-222).");
    }
}
void WriteProfiles()
{
    if (g_profReadOnly != 0)   /* T-222: a newer build's file - rewriting it would drop the lines this build cannot read */
    {
        ++g_profWriteRefusedNewer;
        Log("profiles: ERROR - NOT written (" + ProfilesFile() + " holds lines from a newer build; read-only to this build). refusedNewer=" + N(g_profWriteRefusedNewer));
        return;
    }
    const std::string body = coopprof::RowsFormatFile(g_profiles);   /* T-201 PP6': the shape the host's CHANGE DELETE writes */
    if (!WriteWholeFile(ProfilesFile(), body)) { ++g_profWriteFailed; Log("profiles: WRITE FAILED for " + ProfilesFile() + " - the change holds in memory until the next write"); }
}
/* prof1 fold (review-prof1 8): the slot table's keys for this person with no profiles.txt line become active profiles, so no number is
   ever given out twice (a lost write, or a world played before profiles - whose profile 1 a TEST profile=2 must not strand). */
void ProfilesAdopt(const std::string& person)
{
    const int n = coopprof::AdoptSlotKeys(&g_profiles, person, g_slotById, g_slotSeenAt, NowUnix());
    if (n <= 0) return;
    g_profAdopted += n;
    WriteProfiles();
    Log("profiles: took in " + N((long long)n) + " profile(s) of " + person + " from slots.txt that profiles.txt did not hold (a world from before"
        " profiles, or a lost write) - their numbers stay theirs. adopted=" + N(g_profAdopted));
}
void ProfilesSay(const std::string& person)
{
    Log("profiles: " + person + " has " + N((long long)coopprof::CountActive(g_profiles, person)) + " active (cap " + N((long long)ProfileCap()) + ")");
}
void ProfilesAnswer(ENetPeer* to, const std::string& person, int answers, int verdict, unsigned subject)
{
    coopprof::Answer a; a.answers = answers; a.verdict = verdict; a.cap = ProfileCap(); a.subject = subject;
    a.hasWorld = true; a.world = g_welcomeWorld; a.worldId = g_worldId; a.worldIdUpgraded = g_worldIdUpgraded;   /* T-490: judged by a game in the lobby */
    for (size_t i = 0; i < g_profiles.size(); ++i) if (g_profiles[i].person == person && g_profiles[i].active) a.rows.push_back(g_profiles[i]);
    std::vector<char> m; coopprof::EncodeAnswer(&m, a);
    SendMsg(to, MSG_PROFILES, m);
    if (verdict == coopprof::kRefusedCap) ++g_profRefusedCap;
    else if (verdict == coopprof::kRefusedName || verdict == coopprof::kRefusedNameTaken) ++g_profRefusedName;
    else if (verdict == coopprof::kRefusedUnknown) ++g_profRefusedUnknown;
    else if (verdict == coopprof::kRefusedInUse) ++g_profRefusedInUse;
    else if (verdict != coopprof::kOk) ++g_profRefusedOther;
}
bool ProfileBeingPlayed(const std::string& key)
{
    for (std::map<ENetPeer*, std::string>::const_iterator it = g_peerId.begin(); it != g_peerId.end(); ++it) if (it->second == key) return true;
    return false;
}
/* The HELLO that picked a profile has been given its slot: the profile's line records it. T-201 PP6' (owner 175): admitted is not played -
   the row is played at its first FINISHED save (ProfilePlayedMark). */
void ProfileBound(ENetPeer* from, const std::string& key, int slot)
{
    const std::string person = coopprof::PersonOfKey(key);
    const int i = coopprof::FindRow(g_profiles, person, coopprof::NumOfKey(key));
    if (i < 0) return;   /* not reached: the HELLO admits only a picked, active profile */
    coopprof::Row& r = g_profiles[(size_t)i];
    const bool first = r.slot < 0;
    r.slot = slot; r.lastPlayed = NowUnix();
    WriteProfiles();
    ++g_profPicked;
    Log("slot " + N((long long)slot) + " bound to " + coopprof::ProfileIdOfKey(key) + " ('" + r.name + "', faction '" + r.faction + "') - "
        + PeerName(from) + (first ? " is admitted to it for the first time (played once a save of it finishes)" : " plays it again"));
    ProfilesSay(person);
}
std::string ProfilePersonOf(ENetPeer* p)
{
    std::map<ENetPeer*, std::string>::const_iterator it = g_lobby.find(p);
    if (it != g_lobby.end()) return it->second;
    const std::string k = PeerIdOf(p);
    return k.empty() ? std::string() : coopprof::PersonOfKey(k);
}
/* names2a (investigations/names2-design.md Q3): PROFILES kind 3 - a PLAYING game's player faction name for its own profile's row
   (the lobby's faction column). Only the connection admitted under that very profile may write it. No answer is sent. */
void OnProfileFaction(ENetPeer* from, unsigned num, const std::string& name)
{
    const std::string key = PeerIdOf(from);
    if (!key.empty()) ProfilesAdopt(coopprof::PersonOfKey(key));   /* an old world's profile 1 gets its row first */
    int i = -1; std::string fac;
    const int v = coopprof::FactionDecide(g_profiles, key, num, name, &i, &fac);
    if (v == coopprof::kFacSame) { ++g_profFactionSame; return; }   /* the once-per-world send of an unchanged name */
    if (v != coopprof::kFacSet)
    {
        ++g_profFactionRefused;
        Log("profiles: faction name '" + SanitizeForLog(name) + "' for profile number " + N((long long)num) + " from " + PeerName(from)
            + " (admitted as '" + key + "') refused[" + coopprof::FactionVerdictName(v) + "] - the row is unchanged");
        return;
    }
    coopprof::Row& r = g_profiles[(size_t)i];
    const std::string old = r.faction;
    r.faction = fac;
    WriteProfiles();
    ++g_profFactionSet;
    Log("profile " + coopprof::ProfileIdOfKey(key) + " faction '" + old + "' -> '" + fac + "'"
        + (fac != name ? " (the game's name '" + SanitizeForLog(name) + "' was trimmed or cut to fit the row)" : std::string()));
}
/* T-201 PP6' (owner 175): A PROFILE COUNTS AS PLAYED ONLY AFTER ITS FIRST FINISHED SAVE. PROFILES kind 4 SAVED (any game, after its own
   save of that profile finished) and the operator's WORLD_SAVED both land here; only the connection admitted under that very profile may
   mark it. Until it is marked, HOST / JOIN of it opens NEW GAME (coopprof::AutoLoadDecide) - so quitting NEW GAME before a save, CANCEL
   after the admit, a failed load or a crash before the first save leave it never played. No answer is sent. */
long long g_profPlayedMarked = 0, g_profPlayedSame = 0, g_profPlayedRefused = 0;
void ProfilePlayedMark(ENetPeer* from, unsigned num, const char* why)
{
    const std::string key = PeerIdOf(from);
    if (!key.empty()) ProfilesAdopt(coopprof::PersonOfKey(key));   /* an old world's profile 1 gets its row first */
    int i = -1;
    const int v = coopprof::SavedDecide(g_profiles, key, num, &i);
    if (v == coopprof::kSavedSame) { ++g_profPlayedSame; return; }
    if (v != coopprof::kSavedSet)
    {
        ++g_profPlayedRefused;
        Log(std::string("profiles: ") + why + " for profile number " + N((long long)num) + " from " + PeerName(from) + " (admitted as '" + key
            + "') refused[" + coopprof::SavedVerdictName(v) + "] - the row is unchanged");
        return;
    }
    g_profiles[(size_t)i].played = 1;
    WriteProfiles();
    ++g_profPlayedMarked;
    Log("profile " + coopprof::ProfileIdOfKey(key) + " '" + g_profiles[(size_t)i].name + "' is PLAYED now - its first finished save reached this notebook ("
        + why + "); HOST / JOIN of it loads its save from here on");
}
/* PROFILES up: NEW or DELETE, from a connection in the lobby or one already playing (a person may tidy their other profiles). */
void OnProfiles(ENetHost* host, ENetPeer* from, const std::vector<char>& payload)
{
    (void)host;
    int kind = 0; unsigned num = 0; std::string name;
    if (!coopprof::DecodeRequest(payload, &kind, &num, &name))
    {
        ++g_profRefusedOther;
        Log("profiles: malformed PROFILES from " + PeerName(from) + " (" + N((long long)payload.size()) + " bytes) - ignored");
        return;
    }
    if (kind == coopprof::kReqFaction) { OnProfileFaction(from, num, name); return; }   /* names2a */
    if (kind == coopprof::kReqSaved) { ProfilePlayedMark(from, num, "PROFILES SAVED"); return; }   /* T-201 PP6' (owner 175) */
    const std::string person = ProfilePersonOf(from);
    const int answers = kind == coopprof::kReqNew ? coopprof::kAnsNew : coopprof::kAnsDelete;
    if (person.empty())
    {
        ProfilesAnswer(from, person, answers, coopprof::kRefusedNotReady, num);
        Log("profiles: PROFILES from " + PeerName(from) + " before any HELLO - refused[notReady]");
        return;
    }
    ProfilesAdopt(person);   /* prof1 fold */
    if (kind == coopprof::kReqNew)
    {
        unsigned made = 0;
        const int v = coopprof::NewDecide(g_profiles, person, name, ProfileCap(), num, &made);
        if (v == coopprof::kOk)
        {
            coopprof::Row r; r.person = person; r.num = made; r.slot = -1; r.created = NowUnix(); r.lastPlayed = 0; r.active = 1; r.name = name; r.faction = name;
            g_profiles.push_back(r);
            WriteProfiles();
            ++g_profMade;
            Log("profiles: made " + coopprof::ProfileId(person, made) + " '" + name + "' for " + PeerName(from) + " - it has no slot until it is played");
        }
        else Log("profiles: NEW '" + SanitizeForLog(name) + "' from " + PeerName(from) + " (person '" + person + "') refused["
                 + coopprof::VerdictName(v) + "]: " + coopprof::VerdictText(v, ProfileCap()));
        ProfilesAnswer(from, person, answers, v, v == coopprof::kOk ? made : num);
    }
    else
    {
        const std::string key = coopprof::SlotKey(person, num);
        const int v = coopprof::DeleteApply(&g_profiles, person, num, ProfileBeingPlayed(key) ? 1 : 0);   /* T-201 PP6' (owner 178a): the one delete - the host's CHANGE applies it too */
        if (v == coopprof::kOk)
        {
            const coopprof::Row& r = g_profiles[(size_t)coopprof::FindRow(g_profiles, person, num)];
            WriteProfiles();
            ++g_profDeleted;
            Log("profile " + coopprof::ProfileId(person, num) + " deleted, slot " + (r.slot >= 0 ? N((long long)r.slot) + " retired" : std::string("none (never played)"))
                + " - its number and its slot are never given out again; its buildings stay; the player's game moves its save folder to the Recycle Bin");
        }
        else Log("profiles: DELETE " + coopprof::ProfileId(person, num) + " from " + PeerName(from) + " refused[" + coopprof::VerdictName(v) + "]: "
                 + coopprof::VerdictText(v, ProfileCap()));
        ProfilesAnswer(from, person, answers, v, num);
    }
    ProfilesSay(person);
}

/* W3-f (review-w3 item 2): THE FIRST OPERATOR IS APPOINTED AT A GAME'S FIRST MESSAGE AFTER ITS WELCOME (decision 49), before
   that message is handled. Written down at once, so a restart keeps the answer; everyone is re-told, because the WELCOME
   this game got said authority=0. */
void AppointFirstOperator(ENetHost* host, ENetPeer* from, unsigned type)
{
    const std::string id = PeerIdOf(from);
    if (id.empty()) return;
    g_ownerId = coopprof::PersonOfKey(id); g_ownerSource = coopstore::kOwnerSourceFirst; g_ownerLinked = 1;   /* prof1: the operator is the PERSON */
    WriteOwner();
    /* settings5 fold (review-settings5 1a): the operator's list IS the world's - written now from its greeting, and every game
       already in (admitted provisionally, with no record to match) re-checked against it. */
    {
        std::map<ENetPeer*, coopmods::ModList>::const_iterator pm = g_peerMods.find(from);
        coopmods::ModList had; const bool haveRecord = ModsRecordRead(&had);
        if (pm != g_peerMods.end() && pm->second.known)
        {
            if (!haveRecord || !coopmods::ModListSame(had, pm->second)) ModsRecordWrite(from, pm->second, "the newly appointed operator " + PeerName(from), haveRecord);
            else
            {   /* T-246 fold 2 (item 2): not written, and the provisional games are re-checked all the same */
                Log("[MODS] the newly appointed operator's list is the saved one - mods.txt is not rewritten");
                ModsRecheckProvisional(from, true, had, "at the first operator's appointment, against the saved list, which is the operator's");
            }
        }
        else
        {
            Log(std::string("[MODS] the newly appointed operator's greeting carried no readable mod list - mods.txt is not written")
                + (haveRecord ? "; the world keeps its saved list" : ""));
            ModsRecheckProvisional(from, haveRecord, had, "at the first operator's appointment, against the saved list: the operator's own list is unreadable");   /* T-246 fold 2 (item 2) */
        }
    }
    Log("B13 owner: no --owner and no owner.txt, so the first game to talk to this notebook after its WELCOME is this"
        " world's operator: '" + id + "' (message type " + N((long long)type) + " from " + PeerName(from) + "). It is in"
        " owner.txt now, so a restart keeps this answer instead of asking again (decision 49). W3-f: not at the HELLO,"
        " where a game that then refuses this notebook's world would have been appointed.");
    AnnounceAuthority(host);
}

/* settings5 fold: the mods.txt helpers are above PeerGone now (g_peerMods) */
void OnHello(ENetPeer* from, const std::vector<char>& payload)
{
    size_t at = 0; std::string slot, world; unsigned proto = 0;
    if (!GetStr(payload, &at, &slot) || !GetU32(payload, at, &proto)) { Log("malformed HELLO from " + PeerName(from)); return; }
    /* E38 / decision 43 - THE WORLD KEY. Every game in one world must name the same key; two different saves
       sharing one notebook folder is the mistake it exists to make visible. This build LOGS it and nothing more:
       refusing a mismatched game is a rule about whose world this folder is, and that decision belongs with the
       user, not with a parser. Absent (a shorter payload) reads as "" and is not an error - the field is the last
       one, so an old game is still readable up to the protocol test that follows. */
    size_t wat = at + 4; const bool haveWorld = GetStr(payload, &wat, &world);
    if (!haveWorld) world.clear();
    /* P7j: and after the world key, this game's OWN clock and speed. Absent (a shorter payload, i.e. a game one
       protocol behind) reads as "no clock carried" and is not an error, exactly as the world key does.
       helloHours is NEGATIVE whenever the sending game has no world loaded - which under E38 / decision 42 is
       every game at the moment it links - so this route seeds only a game that links with a world already up,
       and the title-screen case is served by the CLOCK offer that follows its load. Both go to one decision. */
    double helloHours = -1.0; float helloSpeed = -1.0f; bool helloHasClock = false;
    if (haveWorld && payload.size() >= wat + 12)
    { memcpy(&helloHours, &payload[wat], 8); memcpy(&helloSpeed, &payload[wat + 8], 4); helloHasClock = true; }
    /* B10-b (review-b10 M-7). A GAME THIS STORE DOES NOT SPEAK IS REFUSED IN WORDS, AND BEFORE ANYTHING ELSE
       HAPPENS TO IT. What stood here logged "MISMATCH", then assigned the game a SLOT, put it in g_order -
       where it could become the AUTHORITY - sent it a WELCOME and pushed it every record in the folder. The
       refusal existed only as a sentence. This returns before the slot, before g_order and before the
       WELCOME, tells the game which two numbers disagreed, and drops the connection. The decision and the
       words are coopstore::StoreHandshakeDecide / StoreHandshakeRefusalText, shared with the plugin and swept
       by the offline suite, so the two sides cannot disagree about when a handshake is refused. */
    if (coopstore::StoreHandshakeDecide(kProtocol, proto) == coopstore::kHandshakeRefuse)
    {
        ++g_helloRefusedProto;
        /* B13-b (review-b13 M-1): the REFUSE payload's third word is the REASON, so the game prints the
           sentence that fits rather than the protocol one for all three. */
        std::vector<char> rb; PutU32(&rb, kProtocol); PutU32(&rb, proto); PutU32(&rb, (unsigned)coopstore::kRefuseProtocol);
        SendMsg(from, MSG_STORE_REFUSE, rb);
        Log("REFUSED " + PeerName(from) + " slot='" + SanitizeForLog(slot) + "' world='" + SanitizeForLog(world)
            + "': " + coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseProtocol, kProtocol, proto)
            + ". No WELCOME, no slot, no records - this game is not part of this world's notebook. The plugin"
              " and SharedWastelandsServer.exe change together (the RECORD owner field and the store file format) and"
              " must be deployed together. Counted handshake[refusedProtocol].");
        SbDisconnectLater(from);
        return;
    }
    /* B13: THE PLAYER ID, the last field of the HELLO, and the whole reason the protocol moved to 42. It is
       NOT optional-by-length the way the world key and the clock are: a game this process cannot NAME cannot
       be given its slot number, its areas or the operator flag back, so a payload without one is refused in
       the same words a protocol mismatch is. The protocol gate above has already refused every build that
       does not send one, so reaching this arm means a v42 game sent something malformed. */
    std::string playerId;
    { size_t pat = wat + (helloHasClock ? 12 : 0); if (!GetStr(payload, &pat, &playerId)) playerId.clear(); }
    if (!coopstore::PlayerIdOk(playerId))
    {
        ++g_helloRefusedNoId;
        std::vector<char> rb; PutU32(&rb, kProtocol); PutU32(&rb, proto); PutU32(&rb, (unsigned)coopstore::kRefuseNoPlayerId);
        SendMsg(from, MSG_STORE_REFUSE, rb);
        Log("REFUSED " + PeerName(from) + " slot='" + SanitizeForLog(slot) + "' id='" + SanitizeForLog(playerId) + "': "
            + coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseNoPlayerId, kProtocol, proto)
            + " No WELCOME, no slot, no records. Counted handshake[refusedNoPlayerId].");
        SbDisconnectLater(from);
        return;
    }
    /* B13-b (review-b13 H-2) - ONE ID, ONE GAME. Two installs sharing a copied shared_wastelands.cfg would share
       one slot number, so each would read as the writer of every record the other filed and the holder of
       every area the other held - and if the id were the operator's, both would be told they are the
       authority. It is refused in words naming the actual cause, because "deploy them together" would send
       the player to rebuild a mod that is not the problem.
       B13-c (review-b13-b Q2) - BUT THE SAME PLAYER BACK ON A NEW SOCKET IS NOT A COPY. This map is cleared
       only by the DISCONNECT event, which a game that re-dialled after a non-graceful drop (B12-f's Abort),
       crashed, or lost its network never sends; ENet notices for it only when its link times out - 30 to 60 s on link1's terms
       since M11a S3 (coopgl::kGameLinkTimeout*, set on every peer here). Until then the player's
       own re-dial carried its own id into this loop and was refused as a duplicate of itself - and told to
       delete the playerid line from "one of the two" settings files it does not have. The old connection's
       silence tells the two apart (coopstore::DuplicateIdDecide, areaclaim.h, says how and why): a live
       connection has acknowledged something within kDupIdLiveSec; one the game abandoned has not. */
    for (std::map<ENetPeer*, std::string>::const_iterator dp = g_peerId.begin(); dp != g_peerId.end(); ++dp)
    {
        if (dp->first == from || coopprof::PersonOfKey(dp->second) != playerId) continue;   /* prof1: the same PERSON, on any of their profiles */
        ENetPeer* old = dp->first;
        /* lastReceiveTime is ENet's own stamp of the last ACKNOWLEDGEMENT this peer sent (protocol.c,
           enet_protocol_handle_acknowledge) - the one proof that the far end is there. The connect handshake
           sets it, so it is never 0 for a connected peer. */
        const double silentSec = (double)ENET_TIME_DIFFERENCE(enet_time_get(), old->lastReceiveTime) / 1000.0;
        if (coopstore::DuplicateIdDecide(silentSec, coopstore::kDupIdLiveSec) == coopstore::kDupIdRefuse)
        {
            ++g_helloRefusedDupId;
            ++g_profRefusedSamePerson;   /* prof1: one person plays one profile at a time */
            std::vector<char> rb; PutU32(&rb, kProtocol); PutU32(&rb, proto); PutU32(&rb, (unsigned)coopstore::kRefuseSamePerson);
            SendMsg(from, MSG_STORE_REFUSE, rb);
            Log("REFUSED " + PeerName(from) + " id='" + playerId + "' refused[samePerson] (the live game plays " + coopprof::ProfileIdOfKey(dp->second) + "): "
                + coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseSamePerson, kProtocol, proto)
                + " It is already connected on " + PeerName(old) + ", which acknowledged the notebook " + F1((float)silentSec)
                + " s ago (live within " + F1((float)coopstore::kDupIdLiveSec) + " s). No WELCOME, no slot, no records."
                  " Counted handshake[refusedDuplicateId].");
            SbDisconnectLater(from);
            return;
        }
        ++g_helloEvictedSilent;
        Log("B13-c: id '" + playerId + "' is already on " + PeerName(old) + ", but that connection last acknowledged the notebook "
            + F1((float)silentSec) + " s ago (a live one answers within " + F1((float)coopstore::kDupIdLiveSec)
            + " s) - it is a connection this player left behind (a re-dial, a crash, a dropped network), so it is dropped"
              " and this HELLO takes its place. Counted handshake[evictedSilentPeer].");
        PeerGone(old, "evicted (its player is back on a new connection)");   /* the DISCONNECT event's work, because none will come... */
        enet_peer_disconnect_now(old, 0);                                    /* ...this tells the far end if it is somehow there, then resets the peer with NO event */
        DeferDiscard(old);                                                   /* M14 fold 2 */
        break;   /* dp is dead after the erase inside PeerGone; an id is on at most one other connection, by this very loop */
    }
    /* M1 (decisions 54 / 56(b)) - THE TWO NAMED LIMITS, both refused in words and counted, and both BEFORE this
       game is written anywhere (g_peerId, g_order, owner.txt, slots.txt). The duplicate check above runs first
       so a player back on a new socket frees its old seat before it is counted. */
    {
        const int connectedNow = (int)g_peerId.size() - (g_peerId.count(from) ? 1 : 0);
        if (coopstore::ConnectAdmitDecide(connectedNow, g_maxConnected) == coopstore::kConnectRefuseFull)
        {
            ++g_connRefusedFull;
            std::vector<char> rb; PutU32(&rb, kProtocol); PutU32(&rb, proto); PutU32(&rb, (unsigned)coopstore::kRefuseFull);
            PutU32(&rb, (unsigned)connectedNow); PutU32(&rb, (unsigned)g_maxConnected);
            SendMsg(from, MSG_STORE_REFUSE, rb);
            Log("REFUSED " + PeerName(from) + " id='" + playerId + "': "
                + coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseFull, kProtocol, proto, (unsigned)connectedNow, (unsigned)g_maxConnected)
                + " No WELCOME, no slot, no records. Counted conn[refusedFull].");
            SbDisconnectLater(from);
            return;
        }
    }
    /* settings5 S5 (design section 4; user 2026-09-26: EXACT, no allow-list) - THE MOD CHECK, before this game is written
       anywhere. The world's list is mods.txt. The operator's game records it (and replaces it when the operator's own list
       changes - the host's mods ARE the world's); a world with no operator yet takes the first game's list; every other
       game must match it exactly or is refused with the world's list, so it can name the difference to its player. */
    coopmods::ModList helloMods, worldMods;
    unsigned helloProfile = 0;   /* prof1: the u32 after the mod list - 0 = the lobby, n = pick profile n */
    coopjoin::HelloTail helloTail; bool helloTailOk = false;   /* M11a S1 (protocol 61): {live protocol, road, name, view distance} after the profile */
    { size_t mat = wat + (helloHasClock ? 12 : 0) + 4 + playerId.size(); if (!coopmods::ModListDecode(payload, &mat, &helloMods)) helloMods = coopmods::ModList(); else if (!GetU32(payload, mat, &helloProfile)) helloProfile = 0; else helloTailOk = coopjoin::HelloTailDecode(&payload[0], payload.size(), mat + 4, &helloTail); }
    unsigned int modsVerdict = (unsigned int)coopstore::kModsNotChecked;
    int modsDecided = -1; std::string modsAdmitLine;   /* T-246 fold (item 7): the verdict and its "let in" line, logged once the game is admitted */
    {
        const bool haveRecord = ModsRecordRead(&worldMods);
        const bool isOperator = !g_ownerId.empty() && playerId == g_ownerId;
        const int v = coopmods::ModsHelloDecideWorld(helloMods.known, haveRecord ? 1 : 0, isOperator ? 1 : 0,
                                                      (haveRecord && coopmods::ModListSame(worldMods, helloMods)) ? 1 : 0,
                                                      coopmods::kModsMismatchPolicyNow, g_ownerId.empty() ? 0 : 1);   /* settings5 fold: one pure rule, swept offline; T-246: under the owner's policy constant (199 b: warn); T-246 fold (item 8): with no operator named yet, the first game in is not warned against a list its own will replace */
        const std::string counts = N((long long)helloMods.active.size()) + " mods, " + N((long long)helloMods.plugins.size()) + " plugin DLLs";
        if (v == coopmods::kModsHelloAdmitUnchecked)
        {
            ++g_modsNotChecked;
            Log("[MODS] not checked: " + PeerName(from) + " id='" + playerId + "' sent no readable mod list and " + std::string(haveRecord ? "this world has no operator yet (no --owner, no owner.txt; T-246 fold item 8: not warned)" : "this world has none recorded") + " - let in unchecked. modsNotChecked=" + N(g_modsNotChecked));
        }
        else if (v == coopmods::kModsHelloMatch)
        {
            ++g_modsMatched; modsVerdict = (unsigned int)coopstore::kModsMatch;
            Log("[MODS] world list matches (" + counts + "): " + PeerName(from) + " id='" + playerId + "'" + (isOperator ? " (the operator)" : "") + " modsMatched=" + N(g_modsMatched));
        }
        else if (v == coopmods::kModsHelloRecord)
        {
            if (ModsRecordWrite(from, helloMods, "the operator's game " + PeerName(from) + " id='" + playerId + "'", haveRecord))
            {
                modsVerdict = (unsigned int)coopstore::kModsRecorded;
                Log("[MODS] world list matches (" + counts + "): recorded from the operator's game " + PeerName(from));
            }
            else { ++g_modsNotChecked; Log("[MODS] not checked: mods.txt could not be written - " + PeerName(from) + " let in unchecked"); }
            worldMods = helloMods;
        }
        else if (v == coopmods::kModsHelloOperatorUnreadable)
        {   /* T-246 fold 2 (item 1; manager decision - owner 201 is about joiners): the host is never shown the mods notice */
            ++g_modsNotChecked;
            Log("[MODS] not checked: the operator's game " + PeerName(from) + " id='" + playerId + "' sent no readable mod list; the world keeps its recorded list ("
                + N((long long)worldMods.active.size()) + " mods, " + N((long long)worldMods.plugins.size()) + " plugin DLLs) and the operator is let in"
                  " without the notice (T-246 fold 2: the host is never shown MODS DON'T MATCH). modsNotChecked=" + N(g_modsNotChecked));
        }
        else if (v == coopmods::kModsHelloAdmitDiffers || v == coopmods::kModsHelloAdmitUnreadable)
        {   /* T-246 (owner 199 b; fold, owner 201: an unreadable own list too): let in and warned - the WELCOME carries the world's
               list. T-246 fold (item 7): the "differs, let in" line and the count wait for the admission below (after the lobby,
               the profile pick and the lifetime limit). */
            modsVerdict = (unsigned int)coopstore::kModsDiffers; modsDecided = v;
            const coopmods::ModDiff d = coopmods::ModListDiff(worldMods, helloMods);
            const std::string worldCounts = N((long long)worldMods.active.size()) + " mods, " + N((long long)worldMods.plugins.size()) + " plugin DLLs";
            modsAdmitLine = (v == coopmods::kModsHelloAdmitUnreadable)
                ? "sent no readable mod list and the world has one (" + worldCounts + ") - it cannot be compared. The refusal is off (owner 201; T-247):"
                  " its WELCOME carries the world's list and the game shows its player the plain notice once."
                : "joined with mods that differ from the world's list (" + counts + "; the world has " + worldCounts + "). "
                  + SanitizeForLog(coopmods::ModDetailText(d)) + ". The refusal is off (owner 199 b; T-247): its WELCOME"
                  " carries the world's list and the game tells its player once.";
        }
        else if (v == coopmods::kModsHelloProvisional)
        {
            ++g_modsProvisional; modsDecided = v;   /* T-246 fold 2: remembered as provisional once admitted */
            Log("[MODS] provisional: " + std::string(haveRecord ? "this world has no operator yet (no --owner, no owner.txt): the first game to talk after its WELCOME becomes it and its list replaces mods.txt (T-246 fold item 8), so it is not warned against the old list"
                                                     : "this world has no mod list yet, and only its operator's game records one") + " - " + PeerName(from)
                + " id='" + playerId + "' (" + counts + ") is let in PROVISIONALLY and re-checked against the world's list once it is decided: when"
                  " the operator's list is written or - with no operator yet - when the first operator is appointed (against its list, or the saved"
                  " one when its list is the same or unreadable). modsProvisional=" + N(g_modsProvisional));
        }
        else
        {
            const bool unreadable = (v == coopmods::kModsHelloRefuseUnreadable);
            if (unreadable) ++g_helloRefusedModsUnreadable; else ++g_helloRefusedMods;
            const coopmods::ModDiff d = coopmods::ModListDiff(worldMods, helloMods);
            ModsRefuseSend(from, proto, worldMods);
            Log(std::string("[MODS] refused: HELLO refused: ") + (unreadable ? "its own mod list is unreadable" : "mods differ") + " - " + PeerName(from)
                + " id='" + playerId + "' (" + counts + "; the world has "
                + N((long long)worldMods.active.size()) + " mods, " + N((long long)worldMods.plugins.size()) + " plugin DLLs). "
                + (unreadable ? std::string("A game that cannot read its own list cannot be shown to match the world's.") : SanitizeForLog(coopmods::ModDetailText(d)))
                + ". " + coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseMods, kProtocol, proto)
                + " No WELCOME, no slot, no records. Counted helloRefusedMods=" + N(g_helloRefusedMods) + " helloRefusedModsUnreadable=" + N(g_helloRefusedModsUnreadable));
            SbDisconnectLater(from);
            return;
        }
    }
    /* M11a S1 (manager decision 1(a)) - THE GAME-TO-GAME PROTOCOL. This process forwards LIVE inner messages unread, so two games that
       speak different game-to-game protocols would misread each other through it: the number rides the HELLO's tail and a joiner whose
       number differs is refused in words (an empty world accepts the first). A 61 HELLO always carries it; an unreadable tail is refused
       the same way. Before the lobby, so a lobby wait is never offered to a game that cannot play.
       S1 fold (review M2) - THE HOST'S VERSION WINS (coopjoin::LiveProtoJudge): the OPERATOR is never refused for differing (one
       out-of-date game idling at the title must not lock the operator out of its own world); anyone else is judged against the
       operator's number while the operator is admitted, else against the admitted games in the world, else those at the title. The
       games already admitted whose number differs from an arriving operator's are refused and disconnected at its admission (below). */
    {
        const bool helloIsOperator = !g_ownerId.empty() && playerId == g_ownerId;   /* playerId is still the PERSON here (prof1) */
        std::vector<unsigned> inWorldProtos, otherProtos;
        unsigned operatorProto = 0;
        for (std::map<ENetPeer*, JoinConn>::const_iterator jp = g_join.begin(); jp != g_join.end(); ++jp)
        {
            if (jp->first == from || g_peerId.count(jp->first) == 0) continue;
            if (PeerIsAuthority(jp->first)) operatorProto = jp->second.liveProto;
            if (coopjoin::LiveDestAllowed(jp->second.stage)) inWorldProtos.push_back(jp->second.liveProto); else otherProtos.push_back(jp->second.liveProto);
        }
        const unsigned mine = helloTailOk ? helloTail.liveProto : 0u;
        unsigned theirs = 0;
        if (coopjoin::LiveProtoJudge(mine, helloIsOperator, operatorProto, inWorldProtos, otherProtos, &theirs) != coopjoin::kLiveProtoAdmit)
        {
            ++g_liveProtoRefused;
            std::vector<char> rb; PutU32(&rb, kProtocol); PutU32(&rb, proto); PutU32(&rb, (unsigned)coopstore::kRefuseLiveProtocol);
            PutU32(&rb, theirs); PutU32(&rb, mine);
            SendMsg(from, MSG_STORE_REFUSE, rb);
            Log("REFUSED " + PeerName(from) + " id='" + playerId + "': "
                + coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseLiveProtocol, kProtocol, proto, theirs, mine)
                + (helloTailOk ? std::string() : std::string(" (its HELLO carried no readable tail)"))
                + " No WELCOME, no slot, no records. Counted liveProtoRefused=" + N(g_liveProtoRefused));
            SbDisconnectLater(from);
            return;
        }
    }
    /* prof1 - THE LOBBY, AND THE PICK. Everything above judged the PERSON (the id, the same person live elsewhere, the
       connected limit, the mods). A HELLO with profile 0 is answered with this person's profiles and waits here - no slot,
       no areas, no WELCOME. A HELLO with a number picks that profile, and from here on the player IS the profile: playerId
       becomes its slot key, which is what the slot, the areas and g_peerId are keyed on. */
    {
        const std::string person = playerId;
        ProfilesAdopt(person);   /* prof1 fold */
        if (helloProfile == 0)
        {
            g_lobby[from] = person;
            g_helloDueFrom.erase(from);   /* it said who it is; choosing may take a person a while (prof3's screen) */
            ++g_profListed;
            ProfilesAnswer(from, person, coopprof::kAnsList, coopprof::kOk, 0);
            Log("profiles: " + PeerName(from) + " (person '" + person + "') is in the LOBBY - sent its "
                + N((long long)coopprof::CountActive(g_profiles, person)) + " active profile(s), cap " + N((long long)ProfileCap())
                + "; it has no slot until it picks one");
            return;
        }
        const int pv = coopprof::PickDecide(g_profiles, person, helloProfile);
        if (pv != coopprof::kOk)
        {
            g_lobby[from] = person;
            g_helloDueFrom.erase(from);
            ProfilesAnswer(from, person, coopprof::kAnsPick, pv, helloProfile);
            Log("profiles: " + PeerName(from) + " picked " + coopprof::ProfileId(person, helloProfile) + " - refused[" + coopprof::VerdictName(pv) + "]: "
                + coopprof::VerdictText(pv, ProfileCap()) + ". It stays in the lobby.");
            return;
        }
        g_lobby.erase(from);
        playerId = coopprof::SlotKey(person, helloProfile);
    }
    {
        const int s = AssignSlotFor(from, playerId);
        if (s < 0)
        {
            ++g_connRefusedLifetime;
            std::vector<char> rb; PutU32(&rb, kProtocol); PutU32(&rb, proto); PutU32(&rb, (unsigned)coopstore::kRefuseLifetimeFull);
            PutU32(&rb, (unsigned)coopstore::kSlotLifetimeMax);
            SendMsg(from, MSG_STORE_REFUSE, rb);
            Log("REFUSED " + PeerName(from) + " id='" + playerId + "': "
                + coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseLifetimeFull, kProtocol, proto, (unsigned)coopstore::kSlotLifetimeMax)
                + " No WELCOME, no slot, no records. Counted conn[refusedLifetime].");
            SbDisconnectLater(from);
            return;
        }
        char sb[96]; _snprintf(sb, 95, "slot %d -> ", s); sb[95] = 0; Log(std::string(sb) + PeerName(from) + " (player '" + playerId + "')");
        ProfileBound(from, playerId, s);   /* prof1: slot N bound to <profile id> */
    }
    g_peerId[from] = playerId;
    PlayerGoneHoldReturned(SlotOf(from), from);   /* M8 review F1: this player is back - a PLAYER_GONE held for it is cancelled, never sent */
    { JoinConn jc; jc.stage = coopjoin::kStageTitle; jc.road = helloTail.road; jc.liveProto = helloTail.liveProto; jc.name = coopworld::DisplayNameOk(helloTail.name, 0) ? helloTail.name : std::string(); jc.viewDist = helloTail.viewDist; g_join[from] = jc; }   /* M11a S1: admitted = TITLE (slot + seat); a LIVE destination only once in the world. S1 fold (review L1): a name that fails coopworld::DisplayNameOk is stored empty */
    if (PeerIsAuthority(from))   /* S1 fold (review M2): THE HOST'S VERSION WINS - an admitted game whose game-to-game protocol differs from the arriving operator's is refused with reason 7 (its existing sentence) and disconnected, as ModsRecheckAll does to a game whose mods no longer match: left in, it would misread the operator's live messages once both are in the world */
    {
        std::vector<ENetPeer*> out;
        for (std::map<ENetPeer*, JoinConn>::const_iterator jp = g_join.begin(); jp != g_join.end(); ++jp)
            if (jp->first != from && g_peerId.count(jp->first) != 0 && jp->second.liveProto != helloTail.liveProto) out.push_back(jp->first);
        for (size_t i = 0; i < out.size(); ++i)
        {
            const unsigned theirProto = g_join[out[i]].liveProto;
            ++g_liveProtoOperatorRefused;
            std::vector<char> rb; PutU32(&rb, kProtocol); PutU32(&rb, kProtocol); PutU32(&rb, (unsigned)coopstore::kRefuseLiveProtocol);
            PutU32(&rb, helloTail.liveProto); PutU32(&rb, theirProto);
            SendMsg(out[i], MSG_STORE_REFUSE, rb);
            Log("REFUSED " + PeerName(out[i]) + " id='" + PeerIdOf(out[i]) + "' (already admitted): the operator's game was just admitted and the host's version wins - "
                + coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseLiveProtocol, kProtocol, kProtocol, helloTail.liveProto, theirProto)
                + " Disconnected. Counted liveProtoOperatorRefused=" + N(g_liveProtoOperatorRefused));
            SbDisconnectLater(out[i]);
        }
    }
    g_peerMods[from] = helloMods;   /* settings5 fold: kept for the re-check after every mods.txt write */
    if (modsDecided == coopmods::kModsHelloProvisional) g_modsProvisionalPeers.insert(from); else g_modsProvisionalPeers.erase(from);   /* T-246 fold 2 (item 2) */
    g_helloDueFrom.erase(from);   /* M1-b (review-m1 M2): admitted - the HELLO deadline no longer applies */
    ++g_connAdmitted;
    if (coopmods::ModsCountAdmittedDiffers(modsDecided, 1))   /* T-246 fold (item 7): only now is the game in */
    {
        ++g_modsAdmittedDiffers;
        Log("[MODS] differs, let in: " + PeerName(from) + " id='" + playerId + "' " + modsAdmitLine + " modsAdmittedDiffers=" + N(g_modsAdmittedDiffers));
    }
    if ((long long)g_peerId.size() > g_connHigh) g_connHigh = (long long)g_peerId.size();
    /* B13-b (H-1), the belt-and-braces half: a holder that has LINKED but has not reported yet keeps its
       lease from this moment. Without it a returning holder's rows would be measured against a lastSeenOwner
       of 0.0 - the performance counter is seconds since boot, so that reads as hours of silence - and every
       sector it held would change hands in the second before its first AREAS report. The other half is in
       OnAreas: an id is "known now" once it has REPORTED, which is the only event that renews a claim. */
    {
        const double stampNow = NowSec();
        int stamped = 0;
        for (std::map<long long, AreaRow>::iterator ar = g_areas.begin(); ar != g_areas.end(); ++ar)
            if (ar->second.ownerId == playerId) { ar->second.lastSeenOwner = stampNow; ++stamped; }
        if (stamped > 0) Log("B13: " + N((long long)stamped) + " restored area(s) held by this player have had their"
                             " lease stamped at its HELLO - they stand for the ordinary " + N((long long)kGraceSec)
                             + " s from now, and its first AREAS report renews them.");
    }
    if (coopprof::PersonOfKey(playerId) == g_ownerId) g_ownerLinked = 1;   /* prof1: the operator is the PERSON */

    /* Decision 49 / W3-f (review-w3 item 2): a world that has never been told who its operator is takes the first player
       that TALKS to it after its WELCOME - not the first HELLO. A game that follows another world (or speaks another
       protocol) says HELLO, reads the WELCOME and refuses the link itself, sending nothing more (every send of the plugin
       is behind LinkUp()); appointing at the HELLO made such a game this world's operator for good. The slot assigned
       above stays at the HELLO. The appointment is AppointFirstOperator, reached from the receive loop through
       coopstore::OwnerAppointOnMessage, and it is written to owner.txt on the spot, as before. */

    bool known = false; for (size_t i = 0; i < g_order.size(); ++i) if (g_order[i] == from) known = true;
    if (!known) g_order.push_back(from);   /* M1: the slot was assigned above, where a full world can still refuse */
    const bool auth = PeerIsAuthority(from);   /* B13 / decision 49: the OPERATOR, out of owner.txt - not whoever dialled first */
    Log("HELLO from " + PeerName(from) + " player='" + playerId + "' slot='" + SanitizeForLog(slot) + "' world='" + SanitizeForLog(world) + "' protocol=" + N(proto) + (proto == kProtocol ? "" : " (MISMATCH - store speaks " + N(kProtocol) + ")") + " authority=" + (auth ? "1" : "0")
        + (helloHasClock ? (" clock=" + F3(helloHours) + " speed=" + F1(helloSpeed)) : std::string(" clock=(not carried - this game speaks an older protocol)"))
        + " live=" + N((long long)helloTail.liveProto) + " road=" + (helloTail.road == (unsigned)coopjoin::kRoadWorld ? "world" : "session") + " name='" + coopjoin::JsLogSafe(helloTail.name) + "'"
        + ((helloTail.name.empty() || coopworld::DisplayNameOk(helloTail.name, 0)) ? std::string() : std::string(" (not a usable display name - the roster carries none for it)")));   /* M11a S1; S1 fold (review L1) */
    /* P7j: the window opens at the FIRST link, and this game's own clock is judged before the CLOCK message
       below goes out, so a game that has just advanced this world's clock is told the advanced value and not
       the value it replaced. THE SPEED IS DELIBERATELY NOT A VOTE - EffectiveSpeed's own note says why: a
       joiner must not be able to set the world's pace merely by connecting, and a game linking at the title
       screen would be offering whatever the title screen leaves in the speed global. */
    /* P7w: the HELLO no longer anchors the seed window - ClockConsiderGameClock does, and only for a
       clock that is real. Under decision 42 a HELLO arrives at the title screen carrying -1. */
    /* W3 (decision 59): A HELLO FOR ANOTHER WORLD IS LET IN AND SAID. The game follows the world the WELCOME below names -
       or, when it has already opened another world's folder, refuses the link itself - so this notebook never refuses it. */
    if (haveWorld && !world.empty() && !g_welcomeWorld.empty() && !coopworld::WorldNamesCollide(world, g_welcomeWorld))
    {
        ++g_helloWorldDiffers;
        Log("W3: " + PeerName(from) + " said HELLO for world '" + SanitizeForLog(world) + "' and this notebook keeps world '"
            + g_welcomeWorld + "' - let in; its WELCOME names this notebook's world and the game follows it (decision 59)."
            " Counted helloWorldDiffers.");
    }
    if (helloHasClock) ClockConsiderGameClock(from, helloHours, "HELLO");
    /* T-313 (protocol 63): the records are no longer pushed below (OnRecordFeed pages them at the game's ASK and flushes there too);
       the flush stays so the WELCOME's informational record count is what is on disk. */
    /* M14 rule (2): THE PUSH BELOW READS EVERY RECORD FILE (SendRecordTo). Every write out - and every message
       held behind one - lands and is applied first, so the WELCOME's count and the push hand out exactly what
       is on disk and a joining game never gets a half-rotated record. M14 fold (finding 4): here, after every
       refusal above, so a HELLO that is refused (or waits in the lobby) never waits on the disk. Joins are rare;
       this is the one per-join wait on the disk, and it shows in loopGapMaxMs. */
    WriterFlushAll();
    coopstore::StoreWelcome wel; wel.protocol = kProtocol; wel.authority = auth ? 1u : 0u; wel.recordCount = (unsigned)g_records.size(); wel.slot = (unsigned)SlotOf(from);   /* decision 32: the game's slot */
    wel.hasMods = true; wel.modsVerdict = modsVerdict; wel.modsActive = (unsigned)worldMods.active.size(); wel.modsPlugins = (unsigned)worldMods.plugins.size();   /* settings5 S5 */
    wel.modsWorld = worldMods;   /* T-246 (protocol 57): encoded only when modsVerdict is kModsDiffers */
    wel.hasWorld = true; wel.world = g_welcomeWorld;
    wel.hasWorldId = !g_worldId.empty(); wel.worldId = g_worldId; wel.worldIdUpgraded = g_worldIdUpgraded;   /* T-490 (protocol 69): which world of that name */   /* W3 (protocol 45): and this notebook's world, last - the game follows it (decision 59) */
    { restoreguard::WorldMsg wm = restoreguard::RowOf(g_wg, coopprof::ProfileIdOfKey(PeerIdOf(from))); wm.seq = WorldSeqHighNow(); wel.hasWorldGen = true; restoreguard::EncodeWorldMsg(&wel.worldTail, wm); restoreguard::EncodeRepairRows(&wel.worldTail, g_repairs); }   /* restore1c: the repair rows after the world numbers */   /* restore1a (protocol 52; fold: this connection's PROFILE row): the world guard's numbers, after the mods */
    std::vector<char> b; coopstore::StoreWelcomeEncode(wel, &b);
    SendMsg(from, MSG_STORE_WELCOME, b);
    SbBundleOn(from);   /* M13: it speaks this protocol and has its WELCOME (which went alone) - everything after it may ride in bundles */
    SeatAssign(from, "at its WELCOME");   /* owner 205 A: THE UID SEAT - the lowest free one with counters left among the games connected now; the game learns it in its first GRANT */
    SendOwnHighPush(from);   /* restore1b1: this profile's records numbers, right behind the WELCOME (inside the game's opening push) */
    SendMsg(from, MSG_OPTIONS, EncodeOptions());   /* E36 / decision 40: the option map lands BEFORE any bitmap, unique state or record, so nothing is judged under an unknown policy that could have been known */
    /* P7o (review-p7e H-3): recompute BEFORE encoding. g_order.push_back(from) happened at the top of this
       function, but g_clockSpeed was only ever recomputed on the 1 Hz tick - so the FIRST CLOCK a joining game
       saw carried the speed from before it joined, and with this game the only one connected that speed was the
       resting 0.0: "the world is paused", for up to a second, to a game that has just loaded a world. */
    ClockRecomputeSpeed("a game linked");
    SendMsg(from, MSG_CLOCK, EncodeClock());       /* E40 / decision 45: and the CLOCK lands before the weather and before any record - a weather record whose end time is compared against a clock this game has not yet corrected fires the moment it arrives */
    for (std::map<std::string, std::vector<char> >::const_iterator wt = g_weather.begin(); wt != g_weather.end(); ++wt) SendMsg(from, MSG_WEATHER, wt->second);   /* E40: every region's sky, as the authority last published it - P7s: every stored NAME, so a joining game installs each on its own region of that name */
    for (std::map<std::string, std::vector<char> >::const_iterator bt = g_bits.begin(); bt != g_bits.end(); ++bt) SendMsg(from, MSG_DELETED_BITS, EncodeBits(bt->first));   /* decision 30: each faction bitmap before the notes */
    /* T-313 (protocol 63): the unique states are NOT pushed here any more - they open the first page of the record feed, sent when this game ASKS (it has a world). */
    SendResearchPush(from);   /* loot2b: every lifted box, after the unique states (the game counts RESEARCH_BOX inside its opening push) */
    SendTakesPush(from);      /* loot2c: every take row, right after the boxes (inside the opening push too) */
    SendWorldRelPush(from);   /* par24: every faction-vs-faction row, inside the opening push (the game counts WORLD_REL in it) */
    SendBarsPush(from);   /* refill1: every town bar row, after the research boxes and the take rows (the game counts TOWN_BAR inside its opening push) */
    /* T-313 (protocol 63): NO RECORDS HERE. They are paged by OnRecordFeed when this game asks - which it does once it has a world -
       so a game waiting at the title is not handed records it cannot apply. The WELCOME's record count stays, informational. */
    Log("welcomed " + PeerName(from) + " - its " + N((long long)g_records.size()) + " records and " + N((long long)g_uniques.size())
        + " unique states wait for its RECORD_FEED ASK (T-313)");
    RosterBroadcast("a game was admitted");   /* M11a S1: after its WELCOME (T-313: no record push here now) - every admitted game (this one too) hears the roster */
}

/* ================= W2a: ONE FOLDER PER WORLD - THE NOTEBOOK'S SIDE (decision 58; design-worlds sections 1 and 4) =====
   The plan (which file moves where) is coopworld::PlanMigration's; this is only its EXECUTOR. It runs before the log
   file is opened, because coop-store.log is itself one of the files it moves, so what it says is printed at once and
   kept in g_migLines, and written into the log the moment the log opens. */
std::vector<std::string> g_migLines;
void MigNote(const std::string& s)
{
    char ts[32]; time_t t = time(0); struct tm* lt = localtime(&t); strftime(ts, sizeof ts, "%H:%M:%S", lt);
    const std::string line = std::string(ts) + " " + s;
    printf("%s\n", line.c_str()); fflush(stdout);
    g_migLines.push_back(line);
}
void FlushMigNotes(FILE* f)
{
    if (f == 0) return;
    for (size_t i = 0; i < g_migLines.size(); ++i) fprintf(f, "%s\n", g_migLines[i].c_str());
    fflush(f);
    g_migLines.clear();
}
bool PathThere(const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::vector<coopworld::DirEntry> ListStoreRoot(const std::string& storeRoot)
{
    std::vector<coopworld::DirEntry> out;
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((storeRoot + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do
    {
        const std::string n = fd.cFileName;
        if (n == "." || n == "..") continue;
        out.push_back(coopworld::DirEntry(n, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

int CountMetaIn(const std::string& dir)
{
    int n = 0;
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((dir + "\\*.meta").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do { if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) ++n; } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

/* A small text file whole; false when it is absent or cannot be read (the caller says which it needed). */
bool ReadSmallText(const std::string& path, std::string* out)
{
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    if (!f) return false;
    out->assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return !f.bad();
}

/* THE ONE-TIME MIGRATION, into the folder of `world`. True = the notebook may start (nothing to do, or every move
   made). False = it must not: a move failed (nothing is ever overwritten - MoveFileExA WITHOUT
   MOVEFILE_REPLACE_EXISTING), the journal could not be written, or the target already holds another world's records. */
bool MigrateOldLayout(const std::string& storeRoot, const std::string& world)
{
    const std::vector<coopworld::DirEntry> listing = ListStoreRoot(storeRoot);
    if (!coopworld::NeedsMigration(listing, world)) return true;
    const coopworld::MigrationPlan plan = coopworld::PlanMigration(listing, world);
    const std::string folder = coopworld::WorldFolderName(world);
    const std::string target = storeRoot + "\\" + folder;
    const std::string journalPath = storeRoot + "\\" + coopworld::kMigrationJournal;
    const char* const tail = " The notebook will NOT start. Nothing was overwritten. Start it again once the cause is"
                             " fixed and it carries on from where it stopped, or run SharedWastelandsServer.exe --unmigrate to put"
                             " back the files already moved.";

    const std::string marker = target + "\\" + coopworld::kMigratedMarker;   /* W2-f (review-w2 H) */
    std::string jtext; bool resuming = false;
    if (ReadSmallText(journalPath, &jtext)) resuming = coopworld::MigrationResuming(coopworld::MigrationJournalParse(jtext), folder, PathThere(marker));
    const int metaThere = CountMetaIn(target);
    if (metaThere > 0 && !resuming)
    {
        ++g_migFailed;
        MigNote("migrate: REFUSED - the old single notebook folder " + storeRoot + " still has " + N((long long)plan.moves.size())
                + " world files loose in it, but " + target + " already holds " + N((long long)metaThere) + " records of its own"
                " and the journal shows no unfinished migration into it (a finished one leaves " + std::string(coopworld::kMigratedMarker)
                + "), so the two would be mixed into one world." + tail);
        return false;
    }
    MigNote("migrate: the old single notebook folder " + storeRoot + " has " + N((long long)plan.moves.size())
            + " world files loose in it; moving them into " + target + " (a rename on the same disk - nothing is copied)"
            + (resuming ? ". RESUMING: the journal shows this migration was started before." : "."));
    for (size_t i = 0; i < plan.leftInPlace.size(); ++i)
    {
        ++g_migSkipped;
        MigNote("migrate: left in place: " + plan.leftInPlace[i]
                + (plan.leftInPlace[i].compare(0, 7, "mirror-") == 0 ? " (a closed game's mirror copy)" : " (not this world's file)"));
    }
    if (!PathThere(target) && !CreateDirectoryA(target.c_str(), 0))
    {
        ++g_migFailed;
        MigNote("migrate: FAILED - could not create the world folder " + target + " (Windows error " + N((long long)GetLastError()) + ")." + tail);
        return false;
    }
    FILE* j = fopen(journalPath.c_str(), "ab");
    if (j == 0)
    {
        ++g_migFailed;
        MigNote("migrate: FAILED - could not open the journal " + journalPath + " to write in it." + tail);
        return false;
    }
    for (size_t i = 0; i < plan.moves.size(); ++i)
    {
        const coopworld::Move& m = plan.moves[i];
        const std::string line = coopworld::MigrationJournalLine(m);
        /* The line FIRST, on the disk, then the move: a crash between the two leaves a line whose move was not made,
           which --unmigrate finds nothing to move back for; the other order would leave a move no journal names. */
        if (fputs(line.c_str(), j) < 0 || fflush(j) != 0 || _commit(_fileno(j)) != 0)
        {
            ++g_migFailed; fclose(j);
            MigNote("migrate: FAILED - could not write the journal line for " + m.from + " into " + journalPath + "." + tail);
            return false;
        }
        const std::string from = storeRoot + "\\" + m.from, to = storeRoot + "\\" + m.to;
        if (!MoveFileExA(from.c_str(), to.c_str(), 0))
        {
            const DWORD e = GetLastError();
            ++g_migFailed; fclose(j);
            MigNote("migrate: FAILED to move " + m.from + " -> " + m.to + " (Windows error " + N((long long)e)
                    + (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS ? ": the world folder already has a file of that name" : "")
                    + (e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED ? ": something has the file open - is a game or another notebook running?" : "")
                    + ")." + tail);
            return false;
        }
        ++g_migMoved;
        MigNote("migrate: moved " + m.from + " -> " + m.to);
    }
    fclose(j);
    /* W2-f (review-w2 H): every move made (a failed one returned above), so the folder says the migration FINISHED -
       loose files that appear after this are not the rest of it (MigrationResuming). */
    {
        FILE* mk = fopen(marker.c_str(), "wb");
        const bool ok = mk != 0 && fputs("migration finished\n", mk) >= 0;
        const bool closed = mk != 0 && fclose(mk) == 0;
        if (!ok || !closed) MigNote("migrate: could NOT write " + marker + " (Windows error " + N((long long)GetLastError())
                                    + ") - loose world files that appear later would be taken for the rest of this migration.");
    }
    MigNote("migrate: done - " + N(g_migMoved) + " moved, " + N(g_migSkipped) + " left in place; journal " + journalPath);
    return true;
}

/* SharedWastelandsServer.exe --unmigrate: the journal's moves backwards, last first, never overwriting. It is for going back to an
   OLDER build of the mod (W2-f, review-w2 I). The world folders' migrated.done markers go first; on success the journal
   is renamed worlds.migrated.txt.undone.<unix>. After play the world folder holds records written since the migration,
   so the next start of THIS build refuses (loose world files beside a folder with records) until that folder is moved
   away. On a stop the journal is left as it was, so running --unmigrate again carries on. 0 = done (or nothing to
   undo), 3 = stopped. Its notes go to coop-store\worlds.unmigrate.log (WriteUnmigrateLog), never a loose log. */
int Unmigrate(const std::string& storeRoot)
{
    const std::string journalPath = storeRoot + "\\" + coopworld::kMigrationJournal;
    int rc = 0;
    std::string text;
    if (!ReadSmallText(journalPath, &text))
    {
        MigNote("unmigrate: there is no readable " + journalPath + " - nothing to undo.");
        return PathThere(journalPath) ? 3 : 0;
    }
    const coopworld::JournalRead jr = coopworld::MigrationJournalParse(text);
    if (jr.badLines > 0)
    {
        MigNote("unmigrate: REFUSED - " + journalPath + " has " + N((long long)jr.badLines) + " line(s) this program never"
                " writes; nothing was moved. Check the file by hand.");
        return 3;
    }
    if (jr.cutOffTail) MigNote("unmigrate: the journal's last line was cut off mid-write (its move was never made) - ignored.");
    /* W2-f (review-w2 H): the finished-migration marker of every world folder the journal moved into goes FIRST, so a
       stop part-way leaves no folder that claims a finished migration it no longer holds. */
    {
        std::set<std::string> folders;
        for (size_t i = 0; i < jr.moves.size(); ++i) folders.insert(coopcfg::CfgLower(jr.moves[i].to.substr(0, jr.moves[i].to.find('\\'))));
        for (std::set<std::string>::const_iterator it = folders.begin(); it != folders.end(); ++it)
        {
            const std::string mk = storeRoot + "\\" + *it + "\\" + coopworld::kMigratedMarker;
            if (!PathThere(mk)) continue;
            if (DeleteFileA(mk.c_str())) { MigNote("unmigrate: removed " + mk); continue; }
            ++g_migFailed;
            MigNote("unmigrate: STOPPED - could not remove " + mk + " (Windows error " + N((long long)GetLastError()) + "). Nothing was moved.");
            return 3;
        }
    }
    const std::vector<coopworld::Move> back = coopworld::ReverseMoves(jr.moves);
    for (size_t i = 0; i < back.size() && rc == 0; ++i)
    {
        const std::string from = storeRoot + "\\" + back[i].from, to = storeRoot + "\\" + back[i].to;
        switch (coopworld::UnmigrateStepDecide(PathThere(from), PathThere(to)))
        {
        case coopworld::kUnmigrateMove:
            if (MoveFileExA(from.c_str(), to.c_str(), 0)) { ++g_migMoved; MigNote("unmigrate: moved " + back[i].from + " -> " + back[i].to); }
            else { ++g_migFailed; rc = 3; MigNote("unmigrate: FAILED to move " + back[i].from + " -> " + back[i].to + " (Windows error " + N((long long)GetLastError()) + "). Stopped; run --unmigrate again once it is fixed."); }
            break;
        case coopworld::kUnmigrateAlreadyBack:
            ++g_migSkipped; MigNote("unmigrate: " + back[i].to + " is already back");
            break;
        case coopworld::kUnmigrateMissing:
            ++g_migSkipped; MigNote("unmigrate: neither " + back[i].from + " nor " + back[i].to + " is there - nothing to move back");
            break;
        case coopworld::kUnmigrateConflict:
            ++g_migFailed; rc = 3;
            MigNote("unmigrate: STOPPED - both " + back[i].from + " and " + back[i].to + " are there; moving would overwrite one. Nothing was overwritten.");
            break;
        }
    }
    if (rc == 0)
    {
        const std::string undone = storeRoot + "\\" + coopworld::MigrationJournalUndoneName((long long)time(0));
        if (MoveFileExA(journalPath.c_str(), undone.c_str(), 0))
            MigNote("unmigrate: done; the journal is now " + undone + ". The old single-folder layout is back for an OLDER build"
                    " of the mod. THIS build will refuse to start while the world folder holds records written since the"
                    " migration - move that folder away first if you start this build again.");
        else { ++g_migFailed; rc = 3; MigNote("unmigrate: every file is back, but the journal could not be renamed to " + undone + " (Windows error " + N((long long)GetLastError()) + ")"); }
    }
    MigNote("unmigrate: migrate[moved,skipped,failed]=" + N(g_migMoved) + "," + N(g_migSkipped) + "," + N(g_migFailed));
    return rc;
}

/* The log the migration's own lines belong in when the notebook refuses to start: the world folder's coop-store.log if
   the move reached it, else the loose one. Never a NEW file in the world folder - that would make the loose log's move
   fail on the next try. */
void WriteMigNotesBeside(const std::string& storeRoot, const std::string& worldDir)
{
    const std::string inWorld = worldDir + "\\coop-store.log", loose = storeRoot + "\\coop-store.log";
    FILE* f = fopen((PathThere(inWorld) ? inWorld : loose).c_str(), "a");
    if (f) { FlushMigNotes(f); fclose(f); }
}

/* W2-f (review-w2 F): --unmigrate's notes go to coop-store\worlds.unmigrate.log - never to a loose coop-store.log, which
   is a world file: the next start would take it for the old layout, and a rerun of --unmigrate would stop on it. */
void WriteUnmigrateLog(const std::string& storeRoot)
{
    FILE* f = fopen((storeRoot + "\\" + coopworld::kUnmigrateLog).c_str(), "a");
    if (f) { FlushMigNotes(f); fclose(f); }
}

/* A small text file written whole: a temp file beside it, then MoveFileEx over it, so a reader never sees half a file. */
bool WriteSmallTextAtomic(const std::string& path, const std::string& text)
{
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (f == 0) return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size() && fflush(f) == 0 && _commit(_fileno(f)) == 0;   /* on the disk before the rename */
    const bool closed = fclose(f) == 0;
    if (!ok || !closed || !::MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        ::DeleteFileA(tmp.c_str());
        return false;
    }
    return true;
}
/* T-490: the random part of a new world id - this process's performance counter, process id and tick count, mixed. */
unsigned int NewWorldIdMix()
{
    LARGE_INTEGER pc; pc.QuadPart = 0; ::QueryPerformanceCounter(&pc);
    return coopworld::WorldIdMix((unsigned long long)pc.QuadPart, (unsigned long long)::GetCurrentProcessId(), (unsigned long long)::GetTickCount());
}
/* The last part of the folder path (a bare --dir's world name when its world.txt names none). */
std::string DirLeafName(const std::string& dir)
{
    std::string d(dir);
    while (!d.empty() && (d[d.size() - 1] == '\\' || d[d.size() - 1] == '/')) d.erase(d.size() - 1);
    const std::string::size_type cut = d.find_last_of("\\/");
    return (cut == std::string::npos) ? d : d.substr(cut + 1);
}
/* T-490: the oldest time (creation or last write) of this folder's record files (*.meta, *.platoon), unix seconds; 0 = none. */
long long OldestRecordUnix()
{
    long long best = 0;
    const char* pats[2] = { "\\*.meta", "\\*.platoon" };
    for (int k = 0; k < 2; ++k)
    {
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((g_dir + pats[k]).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const FILETIME ts[2] = { fd.ftCreationTime, fd.ftLastWriteTime };
            for (int t = 0; t < 2; ++t)
            {
                const long long u = coopworld::FileTimeToUnix(((unsigned long long)ts[t].dwHighDateTime << 32) | (unsigned long long)ts[t].dwLowDateTime);
                if (u > 0 && (best == 0 || u < best)) best = u;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return best;
}
/* world.txt (coopworld::WorldTxtFormatWithId): line 1 v1<TAB><name as given><TAB><created>, line 2 id<TAB><world id><TAB><born|upgraded>.
   A folder without one gets both lines at its first start, its id born now - under a bare --dir too (the harness), named by the
   folder, so every world the server runs has an id - unless the folder already holds records: that world is older than this start, so
   both lines carry the oldest record's time and the id is marked upgraded. A world.txt from before ids keeps line 1 as it is and gains
   line 2, its id made from line 1's created time (or the oldest record's, when that is earlier) and marked upgraded
   (coopworld::WorldIdCreatedChoose). A world.txt naming another world than --world is said in the log only. */
void CheckWorldTxt()
{
    const bool bare = g_worldName.empty();
    const std::string name = bare ? DirLeafName(g_dir) : g_worldName;
    const std::string path = g_dir + "\\world.txt";
    const long long oldest = OldestRecordUnix();
    if (!PathThere(path))
    {
        if (!coopworld::WorldNameOk(name, 0))
        { g_worldTxtState = "notChecked"; Log("world: no world.txt in " + g_dir + " and '" + name + "' cannot be a world name - none written, this world has no id"); return; }
        bool upgradedNew = false;
        const long long created = coopworld::WorldIdCreatedChoose(false, 0, oldest, (long long)time(0), &upgradedNew);
        const std::string id = coopworld::WorldIdMake(created, NewWorldIdMix());
        if (!WriteSmallTextAtomic(path, coopworld::WorldTxtFormatWithId(name, created, id, upgradedNew)))
        { g_worldTxtState = "writeFailed"; Log("world: could not write " + path); return; }
        Log("world: " + std::string(upgradedNew ? "no world.txt in a folder that already holds records" : "first start") + " of world '" + name + "' in this folder"
            + (bare ? std::string(" (--dir)") : std::string()) + " - world.txt written, id " + id
            + (upgradedNew ? " (upgraded: created = the oldest record's time " + N(created) + ")" : std::string(" (born)")));
    }
    std::string text, stored; long long created = 0;
    if (!ReadSmallText(path, &text) || !coopworld::WorldTxtParse(text, &stored, &created))
    { g_worldTxtState = "unreadable"; Log("world: " + path + " cannot be read as a world.txt - this folder's world is not confirmed and has no id"); return; }
    std::string id; bool upgraded = false;
    if (!coopworld::WorldTxtParseId(text, &id, &upgraded))
    {
        bool upgradedOld = true;
        const long long idCreated = coopworld::WorldIdCreatedChoose(true, created, oldest, (long long)time(0), &upgradedOld);
        const std::string newId = coopworld::WorldIdMake(idCreated, NewWorldIdMix());
        const std::string line1 = text.substr(0, text.find_first_of("\r\n"));
        if (WriteSmallTextAtomic(path, line1 + "\n" + coopworld::WorldTxtIdLine(newId, true)))
        {
            id = newId; upgraded = true;
            Log("world: '" + stored + "' had no world id (world.txt from before ids) - given id " + id + " (upgraded; created " + N(idCreated)
                + (idCreated != created ? std::string(" = the oldest record's time, earlier than line 1's ") + N(created) : std::string(" from line 1")) + "); line 1 is unchanged");
        }
        else Log("world: could not add the id line to " + path + " - this world has no id this run");
    }
    g_worldId = id; g_worldIdUpgraded = upgraded;
    if (bare) { g_worldTxtState = "dir"; Log("world: '" + stored + "' (world.txt under --dir, created " + N(created) + ", id " + (id.empty() ? std::string("-") : id) + ")"); return; }
    if (stored == g_worldName) { g_worldTxtState = "matched"; Log("world: '" + stored + "' (world.txt, created " + N(created) + ", id " + (id.empty() ? std::string("-") : id) + ")"); }
    else if (coopworld::WorldNamesCollide(stored, g_worldName))
    { g_worldTxtState = "matchedOtherCase"; Log("world: started as '" + g_worldName + "'; world.txt spells it '" + stored + "' - the same world (folders ignore case)"); }
    else
    { g_worldTxtState = "mismatch"; Log("world: MISMATCH - started as world '" + g_worldName + "' but " + path + " names '" + stored + "'. Running anyway."); }
}

/* owner 429: THE WORLD FOLDER'S FORMAT NUMBER (swformat, kind world). Read only - decided before anything in the folder is
   written, so a folder a newer build wrote is left exactly as it is. *found = its number (0 = no file). */
int WorldFormatDecide(unsigned int* found, std::string* why)
{
    const std::string p = g_dir + "\\" + swnames::kFormatFile;
    const bool exists = PathThere(p);
    std::string text;
    if (exists && !ReadSmallText(p, &text)) text.clear();
    const int rs = swformat::FormatParse(exists, text, swformat::kKindWorld, found, why);
    return swformat::FormatDecide(rs, *found, swformat::kWorldFolderFormat);
}
/* Each step from the found number to this build's, in order, then format.txt last. Step 0 -> 1: world.txt carries its id line
   (CheckWorldTxt adds it before this runs; a world without one is not converted and is tried again at the next start). */
bool ConvertWorldFolder(unsigned int from)
{
    const std::vector<unsigned int> steps = swformat::FormatSteps(from, swformat::kWorldFolderFormat);
    for (size_t i = 0; i < steps.size(); ++i)
    {
        if (steps[i] == 0 && g_worldId.empty())
        { Log("format: world folder NOT converted from " + N((long long)from) + ": world.txt has no id line - tried again at the next start"); return false; }
    }
    const std::string p = g_dir + "\\" + swnames::kFormatFile;
    if (!WriteSmallTextAtomic(p, swformat::FormatText(swformat::kKindWorld, swformat::kWorldFolderFormat)))
    { Log("format: could not write " + p + " - the world folder is converted again at the next start"); return false; }
    Log("format: world folder converted from format " + N((long long)from) + " to " + N((long long)swformat::kWorldFolderFormat) + " (" + p + " written last)");
    return true;
}

/* W3 (decisions 58, 59): THE NAME THIS NOTEBOOK PUTS LAST IN ITS WELCOME, set once at start after CheckWorldTxt. --world's
   name as given (world.txt keeps it as typed; the folder is its lower case). Under a bare --dir (the harness) the name that
   folder's world.txt carries, else the folder's own name. Never empty. */
void SetWelcomeWorld()
{
    const char* from = "--world";
    if (!g_worldName.empty()) g_welcomeWorld = g_worldName;
    else
    {
        std::string text, stored; long long created = 0;
        if (ReadSmallText(g_dir + "\\world.txt", &text) && coopworld::WorldTxtParse(text, &stored, &created) && !stored.empty())
        { g_welcomeWorld = stored; from = "world.txt under --dir"; }
        else
        {
            g_welcomeWorld = DirLeafName(g_dir);
            from = "the --dir folder's name";
        }
    }
    if (g_welcomeWorld.empty()) { g_welcomeWorld = coopworld::kDefaultWorld; from = "the default"; }
    Log("W3: this notebook names world '" + g_welcomeWorld + "' id " + (g_worldId.empty() ? std::string("-") : g_worldId)
        + (g_worldIdUpgraded ? std::string(" (upgraded)") : std::string()) + " in its WELCOME (" + std::string(from) + "); a game whose"
        " settings name another world follows this one (decision 59).");
}

/* SharedWastelandsServer.exe --migrate-selftest <folder>: the executor above, run for real on files it makes in <folder> (which must
   not exist yet, so it can never be a real notebook folder), checked, then deleted. 0 = passed. */
int g_selfFails = 0;
void SelfCheck(bool ok, const std::string& what) { if (!ok) { ++g_selfFails; printf("SELFTEST FAIL: %s\n", what.c_str()); } }
bool SelfWrite(const std::string& p, const std::string& text) { FILE* f = fopen(p.c_str(), "wb"); if (!f) return false; fputs(text.c_str(), f); return fclose(f) == 0; }
std::string SelfRead(const std::string& p) { std::string t; ReadSmallText(p, &t); return t; }
void SelfDeleteTree(const std::string& dir)
{
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            const std::string n = fd.cFileName;
            if (n == "." || n == "..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) SelfDeleteTree(dir + "\\" + n); else DeleteFileA((dir + "\\" + n).c_str());
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}
int MigrateSelfTest(const std::string& scratch)
{
    if (scratch.empty() || PathThere(scratch)) { printf("--migrate-selftest needs a folder that does not exist yet: '%s'\n", scratch.c_str()); return 2; }
    if (!CreateDirectoryA(scratch.c_str(), 0)) { printf("cannot create %s\n", scratch.c_str()); return 2; }
    const std::string root = scratch + "\\coop-store";
    CreateDirectoryA(root.c_str(), 0);
    const char* const loose[] = { "slots.txt", "a1.meta", "a1.platoon", "coop-store.log", "queue-Coop.txt", "queue-other.txt", "notes.txt" };
    for (size_t i = 0; i < 7; ++i) SelfWrite(root + "\\" + loose[i], std::string("OLD ") + loose[i]);
    CreateDirectoryA((root + "\\mirror-9").c_str(), 0);

    /* 1. the migration: five world files move (the queue becomes queue.txt), three names stay and are listed */
    SelfCheck(MigrateOldLayout(root, "Coop"), "the first migration must succeed");
    SelfCheck(g_migMoved == 5 && g_migSkipped == 3 && g_migFailed == 0, "moved,skipped,failed = 5,3,0, read " + N(g_migMoved) + "," + N(g_migSkipped) + "," + N(g_migFailed));
    SelfCheck(SelfRead(root + "\\coop\\slots.txt") == "OLD slots.txt" && !PathThere(root + "\\slots.txt"), "slots.txt moved into coop\\");
    SelfCheck(SelfRead(root + "\\coop\\queue.txt") == "OLD queue-Coop.txt", "queue-Coop.txt became coop\\queue.txt");
    SelfCheck(PathThere(root + "\\queue-other.txt") && PathThere(root + "\\notes.txt") && PathThere(root + "\\mirror-9"), "other queues, unknown files and mirrors stay");
    const coopworld::JournalRead j1 = coopworld::MigrationJournalParse(SelfRead(root + "\\worlds.migrated.txt"));
    SelfCheck(j1.moves.size() == 5 && j1.badLines == 0 && !j1.cutOffTail, "the journal holds the five moves");
    SelfCheck(PathThere(root + "\\coop\\migrated.done"), "a finished migration leaves coop\\migrated.done (W2-f H)");
    /* 2. a second start finds nothing to do */
    SelfCheck(MigrateOldLayout(root, "Coop") && g_migMoved == 5, "a second start moves nothing");
    /* 3. another world's migration into a folder with records and no journal of its own is refused, untouched */
    CreateDirectoryA((root + "\\other").c_str(), 0);
    SelfWrite(root + "\\other\\x.meta", "X");
    SelfCheck(!MigrateOldLayout(root, "other") && g_migFailed == 1, "a migration into a folder that holds another world's records is refused");
    SelfCheck(PathThere(root + "\\queue-other.txt") && !PathThere(root + "\\other\\queue.txt"), "the refused migration moved nothing");
    DeleteFileA((root + "\\other\\x.meta").c_str()); RemoveDirectoryA((root + "\\other").c_str());
    /* 4. W2-f (H): a loose world file appearing AFTER the finished migration is the mixing refusal - nothing moves */
    SelfWrite(root + "\\slots.txt", "NEW slots.txt");
    SelfCheck(!MigrateOldLayout(root, "Coop") && g_migFailed == 2, "a loose file after a finished migration is refused");
    SelfCheck(SelfRead(root + "\\coop\\slots.txt") == "OLD slots.txt" && SelfRead(root + "\\slots.txt") == "NEW slots.txt", "the refusal overwrote nothing");
    SelfCheck(coopworld::MigrationJournalParse(SelfRead(root + "\\worlds.migrated.txt")).moves.size() == 5, "the refusal journaled nothing");
    /* 4b. without the marker (an interrupted migration) the same file is a move onto a file that is already there: it
       fails, stops, and overwrites nothing */
    DeleteFileA((root + "\\coop\\migrated.done").c_str());
    SelfCheck(!MigrateOldLayout(root, "Coop") && g_migFailed == 3, "a move onto an existing file fails and stops");
    SelfCheck(SelfRead(root + "\\coop\\slots.txt") == "OLD slots.txt" && SelfRead(root + "\\slots.txt") == "NEW slots.txt", "neither slots.txt was overwritten");
    DeleteFileA((root + "\\slots.txt").c_str());
    /* 5. --unmigrate puts every file back (the journaled-but-failed move included) and retires the journal */
    SelfWrite(root + "\\coop\\migrated.done", "migration finished\n");   /* W2-f (H): unmigrate removes it first */
    g_migMoved = g_migSkipped = g_migFailed = 0;
    SelfCheck(Unmigrate(root) == 0, "unmigrate succeeds");
    SelfCheck(!PathThere(root + "\\coop\\migrated.done"), "unmigrate removed coop\\migrated.done");
    for (size_t i = 0; i < 5; ++i) SelfCheck(SelfRead(root + "\\" + loose[i]) == std::string("OLD ") + loose[i], std::string("back in place: ") + loose[i]);
    SelfCheck(!PathThere(root + "\\coop\\slots.txt") && !PathThere(root + "\\coop\\queue.txt"), "nothing is left in coop\\");
    SelfCheck(!PathThere(root + "\\worlds.migrated.txt"), "the journal is renamed");
    SelfCheck(g_migMoved == 5 && g_migSkipped == 1 && g_migFailed == 0, "unmigrate moved,skipped,failed = 5,1,0, read " + N(g_migMoved) + "," + N(g_migSkipped) + "," + N(g_migFailed));
    SelfCheck(coopworld::NeedsMigration(ListStoreRoot(root), "Coop"), "after unmigrate the old layout is back");
    /* 6. W2-f (F): unmigrate's notes go to worlds.unmigrate.log, never into a loose coop-store.log */
    WriteUnmigrateLog(root);
    SelfCheck(SelfRead(root + "\\coop-store.log") == "OLD coop-store.log", "unmigrate's notes did not go into the loose coop-store.log");
    SelfCheck(SelfRead(root + "\\" + coopworld::kUnmigrateLog).find("unmigrate: done") != std::string::npos, "unmigrate's notes are in worlds.unmigrate.log");
    DeleteFileA((root + "\\coop-store.log").c_str());
    SelfCheck(Unmigrate(root) == 0, "a rerun of unmigrate has nothing to undo");
    WriteUnmigrateLog(root);
    SelfCheck(!PathThere(root + "\\coop-store.log"), "a rerun of unmigrate leaves no loose coop-store.log");

    SelfDeleteTree(scratch);
    SelfCheck(!PathThere(scratch), "the scratch folder is deleted");
    printf("SELFTEST %s (%d failed)\n", g_selfFails == 0 ? "PASSED" : "FAILED", g_selfFails);
    return g_selfFails == 0 ? 0 : 1;
}

} // namespace

/* M15 (T-197): one pass of the once-a-second jobs, on the fixed schedule (g_tick). How late it started is recorded
   first (the counters line below reads it); a pass a whole second or more late skips the slots it missed - no
   catch-up burst - and says so here, once per such pass. */
void PeriodicPass(ENetHost* host)
{
    long long skipped = 0;
    const long long late = g_tick.Ran(MonoUs(), &skipped);
    if (skipped > 0)
        Log("M15: the once-a-second jobs ran " + N((late + 500) / 1000) + " ms late - " + N(skipped) + " slot(s) skipped, not caught up"
            " (one piece of work held the loop that long; loopGapMaxMs on the next counters line)");
    AreaTick(host); ClockTick(host); PendingPosTick(); PendingPosCountTick(); OwnerWatchTick(); DeferredLoadTick(host); WorldRelFlushTick(host); StoreCountersTick(); HelloDeadlineTick(); PlayerGoneHoldTick();   /* E40 / decision 45: the world's clock advances on THIS process's wall clock, not on any game's frame rate */
}
/* ONE MESSAGE FROM A GAME, by its type - a frame's own, or each message inside a BUNDLE in its order. `channel` is the one the
   packet arrived on (a bundle's messages share it). */
void OnGameMessage(ENetHost* host, ENetPeer* peer, unsigned char type, const std::vector<char>& payload, int channel)
{
    if (coopstore::OwnerAppointOnMessage(!g_ownerId.empty(), g_peerId.count(peer) != 0, type == MSG_STORE_HELLO))
        AppointFirstOperator(host, peer, type);   /* W3-f (review-w3 item 2): the first operator, at its first message after the WELCOME */
    if (type == MSG_RECORD) OnRecord(host, peer, payload);
    else if (type == MSG_RECORD_GONE) OnRecordGone(host, peer, payload);
    else if (type == MSG_DELETED_BITS) OnDeletedBits(host, peer, payload);
    else if (type == MSG_AREAS) OnAreas(peer, payload);
    else if (type == MSG_UNIQUE_STATE) OnUniqueState(host, peer, payload);
    else if (type == MSG_RESEARCH_BOX) OnResearchBox(host, peer, payload);   /* loot2b */
    else if (type == MSG_TOWN_BAR) OnTownBar(host, peer, payload);   /* refill1 */
    else if (type == MSG_WORLD_REL) OnWorldRel(host, peer, payload);   /* par24 */
    else if (type == MSG_WORLD_SAVED) OnWorldSaved(peer, payload);   /* restore1a */
    else if (type == MSG_OWN_HIGH) OnOwnHigh(peer, payload);   /* restore1b1 */
    else if (type == MSG_UID_BLOCK) OnUidBlock(peer, payload);   /* M4 fold */
    else if (type == MSG_LIVE) OnLive(peer, payload, channel);   /* M5a: a live game-to-game message, stamped and forwarded; M16 fold 3: as reliably as it came */
    else if (type == MSG_JOIN_STAGE) OnJoinStage(peer, payload);   /* M11a S1: a world-road game's LOADING / IN_WORLD */
    else if (type == MSG_RECORD_FEED) OnRecordFeed(peer, payload);   /* T-313: ASK (subscribe + a page) / OFF */
    else if (type == MSG_REPAIR) OnRepair(peer, payload);   /* restore1c */
    else if (type == MSG_PROFILES) OnProfiles(host, peer, payload);   /* prof1 */
    else if (type == MSG_RESEARCH_TAKE) OnResearchTake(host, peer, payload);   /* loot2c */
    else if (type == MSG_OPTIONS) OnOptions(host, peer, payload);
    else if (type == MSG_CLOCK) OnGameClock(peer, payload);          /* P7u: a game's own clock - the pre-jump offer, or its once-a-second play-time report */
    else if (type == MSG_SPEEDVOTE) OnSpeedVote(host, peer, payload);  /* E40 / decision 45: one player's own speed setting */
    else if (type == MSG_WEATHER) OnWeather(host, peer, payload);      /* E40: the authority's sky, forwarded to everyone else */
    else if (type == MSG_STORE_HELLO) OnHello(peer, payload);
    else Log("unknown message type " + N(type) + " from " + PeerName(peer));
}
/* M13: A BUNDLE FROM A GAME (src/common/sendbundle.h) - unpacked and each message handled in order, exactly as if it had come
   alone on the same channel. A malformed one is refused whole (none of it is handled), counted and logged (the first 5 and
   every 100th). */
void OnBundle(ENetHost* host, ENetPeer* peer, const std::vector<char>& payload, int channel)
{
    std::vector<coopbundle::Entry> es;
    if (!coopbundle::Decode(payload.empty() ? 0 : &payload[0], payload.size(), &es))
    {
        ++g_bnRecvBad;
        if (cooplive::LiveLogThis(g_bnRecvBad))
            Log("M13: a malformed bundle of " + N((long long)payload.size()) + " bytes from " + PeerName(peer) + " - none of its messages is handled (counted bundle[recvBad])");
        return;
    }
    ++g_bnRecvBundles; g_bnRecvBundled += (long long)es.size();
    for (size_t i = 0; i < es.size(); ++i)
    {
        const std::vector<char> one(payload.begin() + es[i].at, payload.begin() + es[i].at + es[i].len);
        OnGameMessage(host, peer, (unsigned char)es[i].type, one, channel);
    }
}
int main(int argc, char** argv)
{
    unsigned short port = 27016;
    const char* la = getenv("LOCALAPPDATA");
    /* PP3 (manager 2026-09-27): the top folder is <data folder>\\worlds - %LOCALAPPDATA%\\kenshi\\Shared Wastelands\\worlds by
       default, OUT of Kenshi's save folder so LOAD GAME never lists it. --root names it exactly (the plugin's panel always passes
       its own, which honours the TEST keys datadir= / storedir=); --dir still names one world's folder exactly (the harness). */
    std::string storeRoot;
    {
        std::string dataDir;
        coopdata::DataDirChoose("", la, getenv("USERPROFILE"), &dataDir);
        storeRoot = coopdata::WorldsDir(dataDir);   /* "" when there is no data folder: refused below unless --root or --dir names one */
    }
    std::string dirArg, worldArg, selfTestDir;
    bool dirGiven = false, worldGiven = false, unmigrate = false, selfTest = false;
    std::vector<std::string> argNotes;   /* M1-b: what the option parse has to say, logged once the log is open */
    for (int i = 1; i < argc; i += 2)
    {
        if (!strcmp(argv[i], "--unmigrate")) { unmigrate = true; i -= 1; continue; }   /* W2a: the one option with no value */
        if (i + 1 >= argc) break;
        if (!strcmp(argv[i], "--port")) port = (unsigned short)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--dir")) { dirArg = argv[i + 1]; dirGiven = true; }
        else if (!strcmp(argv[i], "--root")) storeRoot = coopdata::TrimSep(argv[i + 1]);   /* PP3 */
        else if (!strcmp(argv[i], "--world")) { worldArg = argv[i + 1]; worldGiven = true; }
        else if (!strcmp(argv[i], "--migrate-selftest")) { selfTestDir = argv[i + 1]; selfTest = true; }
        else if (!strcmp(argv[i], "--stop-areamaps-after")) g_stopAreaMapsAfter = atof(argv[i + 1]);
        /* M1-b (review-m1 H1): M1 named these two TEST options and never read them, so g_maxConnected and
           g_firstSlot always stayed at their defaults. A value out of range is clamped into it; one that is not
           a whole number is ignored. Either way it is said in the log, below. */
        else if (!strcmp(argv[i], "--max-connected") || !strcmp(argv[i], "--first-slot"))
        {
            const bool isMax = !strcmp(argv[i], "--max-connected");
            const long lo = isMax ? 1L : 0L, hi = isMax ? (long)kMaxConnected : (long)(coopstore::kSlotLifetimeMax - 1);
            char* end = 0; const long v = strtol(argv[i + 1], &end, 10);
            if (end == argv[i + 1] || *end != 0) { argNotes.push_back(std::string(argv[i]) + " '" + argv[i + 1] + "' is not a whole number - IGNORED"); continue; }
            const long c = v < lo ? lo : (v > hi ? hi : v);
            if (c != v) argNotes.push_back(std::string(argv[i]) + " " + argv[i + 1] + " is outside " + N((long long)lo) + ".." + N((long long)hi) + " - CLAMPED to " + N((long long)c));
            if (isMax) g_maxConnected = (int)c; else g_firstSlot = (int)c;
        }
        /* B13 / decision 49: the player this notebook is being started FOR. The MULTIPLAYER panel passes the
           local player's id when it starts one; the harness passes A's. A malformed one is refused here rather
           than written into owner.txt, where it would name a player no HELLO can ever match. */
        else if (!strcmp(argv[i], "--parent"))   /* owner decision 183 */
        {
            unsigned long pid = 0;
            if (coopprof::ParentPidParse(argv[i + 1], &pid) == 1) g_parentPid = pid;
            else argNotes.push_back(std::string("--parent '") + argv[i + 1] + "' is not a process id - IGNORED (this helper does not close with any Kenshi)");
        }
        else if (!strcmp(argv[i], "--owner"))
        {
            const std::string want = argv[i + 1];
            if (coopstore::PlayerIdOk(want)) { g_ownerId = want; g_ownerSource = coopstore::kOwnerSourceArg; }
            else g_ownerSource = coopstore::kOwnerSourceNone;
        }
    }
    /* W2a: TEST ONLY - the migration executor run on files it makes in a folder that does not exist yet; nothing else. */
    if (selfTest) return MigrateSelfTest(selfTestDir);
    /* owner 244: no folder is invented. Without a data folder (LOCALAPPDATA and USERPROFILE both unusable) and without
       --root or --dir there is nowhere to keep a world, so the server does not start (the panel always passes --root). */
    if (storeRoot.empty() && !dirGiven)
    {
        printf("The world server will not start: no data folder (LOCALAPPDATA and USERPROFILE are not usable) and no --root or --dir.\n");
        return 2;
    }
    std::string whyNot;
    if (worldGiven && !coopworld::WorldNameOk(worldArg, &whyNot))
    {
        printf("coop-store will not start: '%s' cannot be a world name. %s\n", worldArg.c_str(), whyNot.c_str());
        return 2;
    }
    if (unmigrate)
    {
        if (dirGiven || worldGiven) { printf("coop-store --unmigrate takes no --dir or --world: it undoes the move of the old notebook folder %s, from its journal.\n", storeRoot.c_str()); return 2; }
        const int rc = Unmigrate(storeRoot);
        WriteUnmigrateLog(storeRoot);   /* W2-f (review-w2 F) */
        return rc;
    }
    /* THE FOLDER: --dir is the exact folder (the harness; it wins over --world and never migrates); --world is that
       world's folder; neither is coopworld::kDefaultWorld ('New World'). */
    g_worldName = worldGiven ? worldArg : (dirGiven ? std::string() : std::string(coopworld::kDefaultWorld));
    if (dirGiven) { g_dir = dirArg; g_worldDirSource = "dir"; }
    else
    {
        g_dir = coopworld::WorldDirIn(storeRoot, g_worldName);   /* PP3 */
        g_worldDirSource = worldGiven ? "arg" : "default";
    }
    /* owner 429: A WORLD FOLDER WRITTEN BY A NEWER BUILD IS NOT OPENED - decided before the migration, any folder creation, the log or the
       lock, so the folder stays exactly as that build left it (a folder that does not exist yet has no format.txt: made at this build's
       number). The panel shows the host refusal for this exit code. */
    unsigned int formatFound = 0; std::string formatWhy;
    const int formatVerdict = WorldFormatDecide(&formatFound, &formatWhy);
    if (swformat::FormatRefuses(formatVerdict))
    {
        if (formatVerdict == swformat::kFormatRefuseUnreadable) printf("format.txt unreadable: %s\n", formatWhy.c_str());
        printf("The world server will not start: %s\\%s says format %u (%s); this build writes format %u of a world folder. Nothing in the folder was changed.\n",
               g_dir.c_str(), swnames::kFormatFile, formatFound, swformat::FormatVerdictName(formatVerdict), swformat::kWorldFolderFormat);
        return swformat::kServerExitFormatRefused;
    }
    if (!dirGiven)
    {
        /* THE ONE-TIME MIGRATION - before the log opens, because the log is one of the files it moves. */
        if (!MigrateOldLayout(storeRoot, g_worldName))
        {
            WriteMigNotesBeside(storeRoot, g_dir);
            return 3;
        }
        _mkdir(storeRoot.c_str());
    }
    _mkdir(g_dir.c_str());
    g_log = fopen((g_dir + "\\" + swnames::kServerLog).c_str(), "a");   /* the world server's own log (an old world's coop-store.log is only moved, never written) */
    FlushMigNotes(g_log);   /* W2a: what the migration said */
    Log("coop-store starting: port " + N(port) + " dir " + g_dir + " protocol " + N(kProtocol) + " store-file format 7"
        + (g_ownerId.empty() ? std::string(" (no --owner given)") : " owner '" + g_ownerId + "' from --owner")
        + " max-connected " + N((long long)g_maxConnected) + " first-slot " + N((long long)g_firstSlot)
        + " world '" + g_worldName + "' worldDir[source=" + g_worldDirSource + "]"
        + " migrate[moved,skipped,failed]=" + N(g_migMoved) + "," + N(g_migSkipped) + "," + N(g_migFailed));
    /* T-220 PP6 fold 2 - THE WORLD LOCK: <world folder>\server.lock, open with NO sharing for this process's whole life (never closed
       here; Windows closes it when the process ends, however it ends). The host's CHANGE DELETE edits profiles.txt only while it can take
       the same lock, so it never edits under a running helper - this game's or another process's. A second helper for this world cannot
       take it either, and stops here: two helpers writing one world's files is the same hazard. */
    {
        const std::string lockPath = g_dir + "\\" + coopprof::kWorldLockFile;
        HANDLE lock = ::CreateFileA(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        const DWORD err = lock == INVALID_HANDLE_VALUE ? ::GetLastError() : 0;
        const int lv = coopprof::WorldLockDecide(lock != INVALID_HANDLE_VALUE, (unsigned long)err);
        if (lv != coopprof::kLockTaken)
        {
            Log("coop-store will not start: ERROR - the world lock " + lockPath + " is " + coopprof::WorldLockVerdictName(lv) + " (Windows error " + N((long long)err)
                + (lv == coopprof::kLockHeld ? ") - another coop-store is running this world" : ")"));
            printf("coop-store will not start: the world lock %s is %s (Windows error %lu).\n", lockPath.c_str(), coopprof::WorldLockVerdictName(lv), (unsigned long)err);
            return 4;
        }
        Log("world lock taken: " + lockPath + " (held until this process ends)");
    }
    CheckWorldTxt();
    SetWelcomeWorld();   /* W3: the WELCOME's world name, after world.txt is settled */
    if (formatVerdict == swformat::kFormatConvert) g_worldFormatState = ConvertWorldFolder(formatFound) ? "converted" : "convertFailed";
    else g_worldFormatState = "use";
    Log("format: world folder format " + N((long long)formatFound) + " (" + swformat::FormatVerdictName(formatVerdict) + "); this build writes "
        + N((long long)swformat::kWorldFolderFormat) + " - " + g_worldFormatState);
    for (size_t an = 0; an < argNotes.size(); ++an) Log("option: " + argNotes[an]);   /* M1-b: a bad --max-connected / --first-slot */
    if (enet_initialize() != 0) { Log("enet_initialize failed"); return 1; }
    ENetAddress addr; addr.host = ENET_HOST_ANY; addr.port = port;
    ENetHost* host = enet_host_create(&addr, (size_t)(kMaxConnected + kRefuseSeats), 2, 0, 0);   /* M1: design D1 - the connected limit plus seats to refuse in words */
    if (host == 0) { Log("enet_host_create failed (port busy?)"); return 1; }
    LoadIndex();
    /* B13: WHO, WHAT AND WHO'S IN CHARGE, before the socket is serviced - a HELLO that arrived before the
       restore would be given a new number and told it holds nothing, which is the defect itself. */
    LoadSlots();
    LoadProfiles();   /* prof1 */
    LoadAreas();
    LoadOwner();
    LoadWorldGen();   /* restore1a: after LoadIndex, so the index's sequence high-water is known */
    LoadOwnHigh();   /* restore1b1 */
    LoadUidBlocks();   /* M4 fold */
    LoadRepairList();   /* restore1c */
    LoadPendingPosCount();
    /* M14 (T-197): THE WRITER THREAD STARTS HERE - after the startup load, whose recovery writes are made on
       this thread before any message is served - and the console's quit events drain it (rule 3,
       StoreCtrlHandler). There is no fallback to writing on the loop: a notebook that cannot start its
       writer does not serve. */
    if (!WriterStart(host)) { Log("M14: could not start the writer thread (GetLastError=" + N((long long)::GetLastError()) + ") - not serving"); return 1; }
    if (!SetConsoleCtrlHandler(StoreCtrlHandler, TRUE)) Log("M14: could not install the quit handler (GetLastError=" + N((long long)::GetLastError()) + ") - closing this window may lose queued writes");
    if (!QuitWindowStart()) Log("M14: could not start the shutdown-window thread (GetLastError=" + N((long long)::GetLastError()) + ") - a logoff or shutdown may lose queued writes");   /* M14 fold (finding 1) */
    ParentWatchStart();   /* owner decision 183: after the writer and both quit doors - its quit is the same QuitDrain */
    g_restoredAtSec = NowSec();
    g_startedAt = NowSec();
    Log("M1 limits: " + N((long long)g_maxConnected) + " connected at once" + (g_maxConnected != kMaxConnected ? " (--max-connected)" : "")
        + ", " + N((long long)coopstore::kSlotLifetimeMax) + " players in the world's lifetime (" + N((long long)g_slotById.size()) + " named so far)"
        + (g_firstSlot > 0 ? ", new ids from slot " + N((long long)g_firstSlot) + " (--first-slot, TEST ONLY)" : std::string()));
    if (g_stopAreaMapsAfter >= 0.0)
        Log("--stop-areamaps-after " + N((long long)g_stopAreaMapsAfter) + " s: this relay will STOP BROADCASTING"
            " AREA MAPS after that many seconds and keep servicing its socket. TEST ONLY.");
    Log("listening");
    g_tick.Start(MonoUs());   /* M15: the first pass of the jobs is due at once */
    for (;;)
    {
        ENetEvent ev;
        if (g_tick.Due(MonoUs())) PeriodicPass(host);   /* M15: on its fixed schedule - never waiting for a quiet network */
        { const long long passFrom = MonoUs(); g_pass.Begin(passFrom, g_tick.UsUntilDue(passFrom)); }
        while (LoopService(host, &ev, g_pass.WaitMs(MonoUs(), kIdleWaitMs)) > 0)   /* M14: the loop's one wait - completions applied and stalls measured there; M15: never past the pass's end */
        {
            g_pass.Count();
            switch (ev.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
                enet_peer_timeout(ev.peer, coopgl::kGameLinkTimeoutLimit, coopgl::kGameLinkTimeoutMinimumMs, coopgl::kGameLinkTimeoutMaximumMs);   /* M11a S3 (decision 6(a)): the game's end sets the same */
                Log("connected: " + PeerName(ev.peer) + " - link1's timeout terms set (limit " + N((long long)coopgl::kGameLinkTimeoutLimit) + ", "
                    + N((long long)coopgl::kGameLinkTimeoutMinimumMs) + "-" + N((long long)coopgl::kGameLinkTimeoutMaximumMs) + " ms; M11a S3)");
                g_helloDueFrom[ev.peer] = NowSec();   /* M1-b (review-m1 M2): the HELLO deadline starts here */
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
            {
                if (PeerGone(ev.peer, "disconnected")) AnnounceAuthority(host);   /* B13-c: the event's work lives in PeerGone, shared with the eviction in OnHello */
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
            {
                if (ev.packet->dataLength >= 5)
                {
                    unsigned char type = ev.packet->data[0]; unsigned n = 0; memcpy(&n, &ev.packet->data[1], 4);
                    if (5 + n == ev.packet->dataLength)
                    {
                        std::vector<char> payload(ev.packet->data + 5, ev.packet->data + 5 + n);
                        if (type == MSG_BUNDLE) OnBundle(host, ev.peer, payload, (int)ev.channelID);   /* M13: its messages, in order, each as if it came alone */
                        else OnGameMessage(host, ev.peer, type, payload, (int)ev.channelID);
                    }
                    else Log("bad frame from " + PeerName(ev.peer));
                }
                enet_packet_destroy(ev.packet);
                break;
            }
            default: break;
            }
            if (g_pass.Spent(MonoUs())) break;   /* M15: the pass is over (its budget, or the jobs are due) - checked after EVERY event */
        }
    }
}
