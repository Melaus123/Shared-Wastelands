// config.h - E38 / decisions 42 and 43: the one small text file that names this install's role and addresses.
//
// WHY A FILE AT ALL. Until now nothing in the plugin knew any address: the harness typed `host`, `join` and
// `store server` into the command channel after the world had loaded, so a real player could not start a session
// at all and every world load answered its first town gate with no notebook (review-p6q). Decision 43: a config
// file plus the existing commands first, an in-game panel later.
//
// THE ROLE IS THE ONE SWITCH EVERYTHING ELSE HANGS OFF.
//   single - the default, and what a missing file means. NO link of any kind is ever opened, the store's
//            LOAD-OVERRIDE and its notebook-people-pending refusal are disabled, the notebook index is not read,
//            and the shop-restock gate goes straight to the engine's own call above every counter. A lone game
//            is plain Kenshi (decision 43's own acceptance test).
//            DECISION 44 (approved after P6z was designed) NARROWS THIS: the rule is not "no file =
//            single-player, full stop" but "NO FILE **AND NO KEY** = single-player". A save that has ever been
//            part of a co-op world carries that world's key in `multiplayer-world.key` in its slot folder, and under
//            decision 32 a co-op world played offline becomes a second world that can never agree with the first
//            again - so role=single loading a KEYED save is refused, with a plain on-screen line, by the gate in
//            store.cpp (SaveManager::loadGame 0x373DC0). role=single still means no link and vanilla behaviour
//            for every save that was never co-op, which is all decision 43's acceptance test ever measured.
//   host   - opens the session host and the notebook link at the title screen, and forwards the notebook's
//            address to every client in the session WELCOME.
//   client - joins the host at the title screen; the notebook's address arrives in that WELCOME (the file's
//            `store=` is only a fallback for a host that has none).
//
// THREADING. RoleIsSingle() and ConfigRole() are read from detours that run on the engine's worker threads
// (towngen's creation hook is the live example), so the role is a plain aligned long written by startPlugin
// before any hook exists and by the command channel afterwards - an interlocked exchange, never a std::string
// compare on the hot path. Every other accessor here is MAIN THREAD ONLY: they return std::string by value and
// their callers (the store HELLO, the WELCOME encoder, the title arming) are all main-thread.
#pragma once
#include <string>

/* P8i (U1): the text layer of this very file format, PURE and shared with the offline suite.
   config.cpp parses through it, ConfigWrite below formats through it, and ui.cpp refuses what the player
   typed through it - so the panel and the parser cannot come to hold two ideas of <address>:<port>. */
#include "../common/cfgtext.h"

namespace coop {

enum RoleId { kRoleSingle = 0, kRoleHost = 1, kRoleClient = 2 };

// startPlugin, once, BEFORE InstallStore - the slot has to be known before any HELLO can be sent and the role
// before the record index would otherwise be read.
void ConfigLoad();

// The first TitleScreen::update tick performs the arming the file asked for. Not startPlugin: a fault during
// preload deadlocks the process silently (F032), and this opens sockets.
void ConfigTitleTick();

int         ConfigRole();        // ANY THREAD
bool        RoleIsSingle();      // ANY THREAD - the single-player guarantee's one test
const char* ConfigRoleName();    // ANY THREAD (a literal)
bool        ConfigFileSeen();    // was there a file at all

// The command channel's `host` / `join` / `store server` verbs override the file at run time (decision 43: the
// file is a default, not a lock). Logged, and leaving `single` is what makes the store read its index.
void ConfigSetRole(int role, const char* why);

std::string    ConfigHostAddr();    // MAIN THREAD
unsigned short ConfigHostPort();
std::string    ConfigStoreAddr();   // MAIN THREAD - what a host forwards in its WELCOME (never g_linkAddr)
unsigned short ConfigStorePort();
std::string    ConfigSlot();        // MAIN THREAD - the store HELLO's slot string
std::string    ConfigWorldKey();    // MAIN THREAD - the shared world's name; every game in one world must match
/* P8J-B2 (bench 2, F738): shared_wastelands.cfg's saveroot= / storedir=, as read at the FIRST ConfigLoad of this process and
   never changed after (the folders are named from them once). "" = the key is absent = today's folder. */
std::string    ConfigSaveRoot();
int            ConfigProfileTest();          /* prof1 TEST ONLY: shared_wastelands.cfg profile=<n>, 0 = absent */
void           ConfigSetSlotForProfile(const std::string& slot);   /* prof1: the picked profile's save folder, IN MEMORY ONLY (never written to shared_wastelands.cfg). MAIN THREAD */
int            ConfigModFingerprintSalt();   /* settings5 S5 TEST ONLY: 1 = shared_wastelands.cfg says modfingerprint_salt=1 */
std::string    ConfigStoreDir();
/* PP3: the player's DATA FOLDER (coopdata::DataDirChoose): datadir= from the DLL-side TEST file, else
   %LOCALAPPDATA%\kenshi\Shared Wastelands. Named at the first ConfigLoad, never changed after. "" = none usable. */
std::string    ConfigDataDir();
int            ConfigJoinViaWorld();   /* M11a S1 TEST ONLY: 1 = shared_wastelands.cfg says joinvia=world (a client joins through the world server alone) */
int            ConfigRouterPort();   /* routerport= in shared_wastelands.cfg: 1 = the HOST press asks the router (absent = 1), 0 = it does not */
int            ConfigOwnSave();   /* mmo1: ownsave= in shared_wastelands.cfg, 1 = on; absent = 1 (T-251: on by default; ownsave=off switches it off) */
/* B13 - THIS INSTALL'S STABLE NAME IN THE NOTEBOOK. 32 lowercase hex characters, kept in
   shared_wastelands.cfg, generated by ConfigLoad the first time the file is read without one (and the file is
   written back on the spot). The store HELLO carries it, and the notebook keys the slot number, the area
   map and the operator on it, so a notebook killed and restarted gives every player back what it had
   instead of re-deriving it from dial order (T239).
   B13-b (review-b13 M-2): IT IS NEVER EMPTY once anything has asked for it. A game with no settings file at
   all used to get none - ConfigReadFileInto returned before the generator ran - so its HELLO carried an empty
   id for ever (which the notebook refuses) and the first setup the MULTIPLAYER panel saved wrote an empty
   playerid line. The id is generated IN MEMORY whenever this would otherwise be empty, with or without a
   file; the settings write and the panel's Save are what make it survive a restart. */
std::string ConfigPlayerId();          // MAIN THREAD - generates one if this install has none yet
long long   ConfigPlayerIdGenerated(); // how many ids this session MADE (0 or 1); writing it down may still have failed
long long   ConfigPlayerIdLoaded();    // ... and how many times it read one that was already there
// PP3d (owner-approved 2026-09-27): 0 normal, 1 UNREADABLE, 2 DAMAGED (coopdata::IdentitySessionOnlyKindOf, set at start). While
// it is not 0 the MULTIPLAYER panel refuses HOST and JOIN and shows the identity box instead.
int         IdentitySessionOnlyKind();
// PP3d: CONTINUE AS NEW PLAYER - the ONLY path that replaces an identity (kind 2 only). 1 = replaced, the kind is 0 now. MAIN THREAD.
int         IdentityReplaceByChoice();

// P7f (review-p6z L-4): it is the HOST'S key, arriving in the client's WELCOME - the sentence was inverted and
// the body below was always right.
// The WELCOME this client received carried the HOST'S world key. W3 (decisions 58/59): a client whose world differs ADOPTS it
// here as an early hint (ConfigAdoptWorld, in memory only); the notebook's own store WELCOME still decides (M4). Counted in
// ConfigWorldKeyMismatches either way.
void ConfigNoteRemoteWorldKey(const std::string& key);
// W3 (decisions 58/59; M4, M5): FOLLOW ANOTHER WORLD, IN MEMORY ONLY. Sets what ConfigWorldKey returns - the store HELLO,
// the record-index folder, the save's world key and the load gate all read it - and never writes shared_wastelands.cfg. A panel
// re-arm keeps it for every co-op role (W3-f; coopworld::WorldForRole). MAIN THREAD. `why` is said in the one log line.
bool ConfigAdoptWorld(const std::string& name, const char* why);   // W3-g: false = the name is shared_wastelands.cfg's own world (confirmed, not adopted)
std::string ConfigAdoptedWorld();       // the adopted world, or empty
std::string ConfigFilePlayerName();     // mp1: the playername= shared_wastelands.cfg holds, "" = none. MAIN THREAD - the panel's Your name box
bool ConfigWritePlayerName(const std::string& name, std::string* err);   // mp1: the file rewritten with this name, every other kept line carried; no write when unchanged. MAIN THREAD
std::string ConfigFileWorld();          // W3-f (M5): the world shared_wastelands.cfg NAMES (never an adopted one), "" = none. MAIN THREAD - the panel's world box
bool        ConfigPanelPrefill(coopcfg::CfgFields* out);   // PP3b: session.cfg's fields for the panel's boxes (it arms nothing at start); false = no file. MAIN THREAD
std::string ConfigLastJoinAddr();                          // PP3b: player.cfg's last JOINED <address>:<port>, "" = none (never 0.0.0.0). MAIN THREAD
long long   ConfigWorldAdoptedEarly();  // adoptions made from the host's session WELCOME (the early hint)

// ------------------------------------------------------------------------------------------------
// P8i (U1) - THE WRITER.  There was none: ConfigLoad reads this file and nothing in the plugin has ever
// produced one (tools/write-cfg.ps1 did, from outside the game).  The MULTIPLAYER panel needs one.
//
// MAIN THREAD ONLY (it touches the same std::string globals every accessor above returns).
//
// IT WRITES THE FILE AND NOTHING ELSE.  No global here is updated, no role is set, no link is opened and
// no latch is cleared - so a game that saves settings mid-session behaves for the rest of that session
// exactly as it did before, which is what lets the panel say `this takes effect the next time you start
// Kenshi` and be telling the truth.
//
// ATOMIC: the text goes to `shared_wastelands.cfg.new` in the same folder and is moved onto the real name with
// MoveFileExA(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH).  A half-written config is precisely the
// `half a configuration` state FallBackToSingle exists to catch, and a torn file served back as good is
// the damage class the notebook's own S1 ladder is repairing.
//
// Returns false and fills `err` (may be null) with a sentence a player can read.
bool ConfigWrite(const coopcfg::CfgFields& fields, std::string* err);

// PP3: where that file is now - `<data folder>\session.cfg` ("" = no data folder).  MAIN THREAD.
std::string ConfigFilePath();

// ------------------------------------------------------------------------------------------------
// U2 (design-ui-panel phase 2) - THE PANEL ACTS NOW, THROUGH THE ARMING PATH THAT ALREADY EXISTS.
//
// There is exactly ONE piece of code in this plugin that opens the two links, and it is
// ConfigTitleTick above: it reads these file statics, calls net::SessionHost or net::SessionJoin and
// SetStoreServer, and then latches so it never runs again.  F574 saw that path bring both games up at
// the title screen before either world loaded.  U2 does NOT write a second one beside it - the design
// asks for "one arming path, two triggers" and this is how it is got: the panel writes the settings
// file, then calls ConfigRearmFromFile, which RE-READS that file into these statics and CLEARS THE
// LATCH.  The very next title tick performs the arming, logs the same `[STORE] E38 link at title` line,
// and every accessor below then reports the same live state a file-configured game reports.
//
// MAIN THREAD ONLY (the title pump).  It touches the same std::string globals every accessor does.
//
// Returns false and fills `err` (may be null) with a sentence a player can read: no file, or a file
// whose role does not name everything that role needs (in which case the game is left single-player by
// FallBackToSingle, exactly as it would be at startup).
//
// F725 R4 is the reason this is U2's and not U1's: nothing may call ConfigSetRole from the panel until
// the stale-file-statics hazard is closed, and re-reading the file IS the closing - the statics and the
// file cannot disagree afterwards because one was just made from the other.
bool ConfigRearmFromFile(const char* why, std::string* err);
// T-201 N1: true (and *err = the player's sentence) when `fileWorld` is another world than the one whose record index this
// game already read (W2b: one world per game start). ConfigRearmFromFile asks it of the file; the panel asks it BEFORE it
// writes the file, so a refused press writes nothing.
bool ConfigWorldSwitchRefused(const std::string& fileWorld, const char* why, std::string* err);
// T-201 N1 - LEAVE WITHOUT A RESTART. net::SessionLeave (BYE), the store link closed (SetStoreServer("", 0)), the role back
// to single, ConfigTitleTick's latch and the dial count reset - so the next HOST / JOIN press arms cleanly. CANCEL, every
// failure, a new press and BACK on the HOSTING screen call it. The world server process is left running. MAIN THREAD.
void ConfigLeave(const char* why);

// How many times ConfigTitleTick has performed the arming in this process.  0 = never.  T-201 N1: the panel reads a
// change of it as "the title tick has opened the links since the press"; a second press leaves first (ConfigLeave).
long long ConfigArmGen();
// A joining game's world-server dials at the title that went unanswered (ConfigTitleTick's re-dial), and their bound: the
// JOIN press shows CAN'T JOIN when the count reaches the bound with no answer from the world server.
long long ConfigDialCount();
long long ConfigDialCap();
// How many times the host's session WELCOME named a different world from this install's (see
// ConfigNoteRemoteWorldKey), and the last such name.
long long   ConfigWorldKeyMismatches();
std::string ConfigRemoteWorldKey();

}
