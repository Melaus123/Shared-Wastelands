// store.h - the shared sleeping-record store, phase 1 (decisions 8/14/15/17/18/19; resolves F447; design in
// docs/persistence-service.md, engine facts in .modding/03-systems/platoon-lifecycle.md, F451-F454).
//
// What it does: when this engine puts a populated squad it OWNS to sleep, the squad's record container (the engine's
// own snapshot of every character, one GameDataContainer per platoon for life) is written with the engine's own
// file format and its position, then sent to the peer. When this engine wakes a squad, a NEWER record from the store
// is loaded into that container and the sleeping position set before the engine builds the characters. A record
// arriving for a squad that is asleep here moves its sleeping position, so the engine wakes it where the last owner
// left it (that is how a squad the other player walked into our area appears - the sticky-ownership flow).
// Phase 1 limits: the host holds the canonical files (one shared PC); squads unknown locally are counted, not created
// (P1b: addUnloadedSquad); facing is not carried; the file save's `moreData` is null (measured on the first run).
#pragma once
#include <string>
#include <vector>
#include <utility>
#include "../common/townrefill.h"   /* townrefill::Tally - StoreTownRecordTally */
class Faction;
namespace coop {
void InstallStore();                         // hooks Platoon::deactivate (post) and Platoon::activate (pre)
void StoreTick();                            // MAIN THREAD, 1 Hz: link-up edge -> index load + push to the peer
// E24.1 (review-p5o HIGH-1): has the notebook finished handing this game its opening picture of the world?
// 0 = no relay link, or the WELCOME's push is still arriving; 1 = the push is over and the notebook has said
// everything it was going to say unprompted. Until it is 1, "the notebook does not know this id" is unanswerable.
int  StoreWelcomePushDone();
// T-313 (world-server protocol 63): has THIS WORLD's record-feed snapshot - every unique state and every record the notebook
// held when this game asked, then the last page's END marker - been applied by the drain? 0 at the title, through a load and
// until then; reset at every load, quit to menu and new link. Until it is 1, "the notebook does not know this id" is unanswerable.
int  StoreSnapshotApplied();
void ReportStore();
// T-239 (owner 191 A / 192): true while the player may not save, load or start a new game by hand - the title is down AND this is a
// multiplayer world (SaveRedirectWanted). Every hand-save road asks this one function (store.cpp: the save funnel, the quickload
// hook, the new-game pump refusal; ui.cpp: the pause menu greying). MAIN THREAD.
bool HandSaveBlocked();
// review-session S6: the session peer's link went down. Logs what the store keeps and why; resets
// nothing, because nothing it holds is a fact about the peer (see the definition).
void StorePeerGone();
void SetStoreOn(bool on);
void SetZoneStateOn(bool on);   // piece 3a: zone capture + load override (default OFF until piece 3b)
/* E47-S2 (P8f re-prepared): the 30-second heartbeat for this game's AWAKE world groups (F660). `heartbeat on|off`; the
   publisher ships OFF and the registry and its counters run either way. The dirty marks are MAIN THREAD (a worker's is
   dropped - the 30 s floor covers it). cause: kHbCauseItem / kHbCauseWake. */
void SetHeartbeatOn(bool on);
void StoreMarkSquadDirty(void* platoon, int cause);
void StoreMarkSquadDirtyByUid(unsigned int uid, int cause);   /* uid 0 = a storage box (zone state): a silent no-op */
const int kHbCauseItem = 0, kHbCauseWake = 1;
// P2 item 1: connect to SharedWastelandsServer.exe (empty address = back to the in-game store). `store server <addr> <port>`.
bool SetStoreServer(const std::string& address, unsigned short port);
// Harness lever: move the awake platoon named worldId by (dx, dz) - `platoonnudge <id> <dx> <dz>`.
bool PlatoonNudge(const std::string& worldId, float dx, float dz);
// The client's context-built platoon carries the host's platoon id (CONTEXT). Also marks it persistent so this engine
// puts it to sleep instead of destroying it.
void StoreBindPlatoon(void* platoon, const std::string& worldId);
void StoreLinkFromWelcome(const std::string& address, unsigned short port);   /* E38 / decision 42: the notebook address the host published in its session WELCOME - opened at the title screen, before the world loads */
void StoreRoleLeftSingle();                         /* E38 / decision 43: a `join` or `store server` verb took this game out of single-player - read the record index that was skipped at start */
/* a group made here from a world record that carries no home key (written before the key existed) - its
   platoon and its record's position (posKnown 0 = the record has none). */
struct KeylessGroup { void* platoon; float x, z; int posKnown; KeylessGroup() : platoon(0), x(0.0f), z(0.0f), posKnown(0) {} };
int StoreKeylessRecordGroups(const std::string& townSid, KeylessGroup* out, int cap);   /* MAIN THREAD - those still bound here whose record names this town or names none ("" = every town); -1 = more than cap. One whose record has gained a key since leaves the list and its home is owed (StoreGiveOwedHomes) */
int StoreGiveOwedHomes(const char* buildingKey, void* building);   /* MAIN THREAD - the groups made from their world records while this building (its position key) was not loaded here are given it as their home now (towngen.cpp TownGenGiveHomeTo); returns how many the building's residents hand now names */
std::string StoreHomeReport();   /* the home[...] counts for the [TG] report line (plain counter reads) */
int StoreTownPeoplePending(const char* townSid, int askSx, int askSy, char* holderOut, int holderCap);   /* decision 34 / T-580, ANY thread: does the notebook hold this town's creation back - townpending::kTownNotListed / kTownElsewhere / kTownHeld (src/common/townpending.h). (askSx, askSy) = the area the creation is for (-1 unread); holderOut (may be 0) names the note that holds it */
int StoreTownRecordTally(const std::string& townSid, townrefill::Tally* out);   /* MAIN THREAD - what this world's records say about one town's groups (deleted, or living: placed here, waiting here, elsewhere, unplaceable, creation failed); 1 = counted */
std::string StoreWorldStamp();   /* MAIN THREAD - the world this game is in now (townrefill::WorldStamp of its world name and the world id it holds); an unsent REFILLED or RECORD_GONE waits with it */
std::string StoreGoneTownReport();   /* the goneTown[...] counts for the [TG] report line (plain counter reads) */
int StoreTownGoneUnsent(const std::string& townSid);   /* MAIN THREAD - this game's gones naming this town not sent to the world server yet */
int StoreRecordsHomedAt(const std::string& buildingKey);   /* MAIN THREAD - living world records whose group's home building is this key (the town refill's home test) */
int StoreTownGoneThisLaunch(const std::string& townSid);   /* MAIN THREAD - groups of this town this game deleted since it started */
std::string StorePendNoteCommand(const std::string& op, const std::string& town, float x, float z, const std::string& kind = std::string());   /* TEST-ONLY (pendnote verb, T-580), main thread: test notes in decision 34's pending set; kind "" / stale / asleep picks the note's id */
std::string StoreTestCommand(const std::string& arg);   /* TEST-ONLY (storetest verb), main thread: storetest nopos <worldId or part of one> | storetest nopos off */
void NoteTownPeople(const std::string& worldId, const std::string& town);   /* main thread: a received note names its home town ("" = none) */
std::string StoreWorldIdOf(void* platoon);   /* P4p: the group's id string (Platoon+0x78), "" if unreadable */
// P1b (b): the platoon is being destroyed by us (a live announcement superseded a sleeping copy) - forget it.
void StoreUnbindPlatoon(void* platoon);
// P1c: a SLEEPING platoon of this faction with this world id, from the engine's own list (0 if none).
void* FindSleepingPlatoon(::Faction* f, const std::string& worldId);
void* FindAwakePlatoon(::Faction* f, const std::string& worldId);      // P3: the faction's ACTIVE list, by id (0 if none)
long StoreWorldGenNow();            /* inv7e2 fold: this game's world-load generation (g_worldGen) */
void StoreNoteLinkDown();           /* inv7e2 fold: the game link went down / the session was left - remember the world-load generation. Any thread. */
long StoreWorldGenAtLinkDown();     /* inv7e2 fold: -1 until the first link down */
int PlatoonSessionMemberUids(void* platoon, unsigned int* out, int cap);   /* inv7e2: the session uids of the live members; -1 = unreadable or more than cap */
int StorePlatoonMineMembers(void* platoon);   /* M7a3f4 item 1 [m7a3f4-sh0]: live members this game runs (retired-before-destroy rows too); -1 = unreadable */
int PlatoonHasSessionMembers(void* platoon);                          // section 13: 1 if any live member is a session uid
int StoreIdBlockOwner(const std::string& worldId, int* mySlot);      /* inv7a3: the relay slot that minted this id (-1 = no answer); *mySlot = this game's slot (-1 = no relay) */
int StorePlatoonActiveChars(void* platoon);                           /* inv7a3: the platoon's live member count, -1 if unreadable */
int StoreRetireLocalCopy(void* platoon);
void StoreOrphanEmptiedSquad(void* faction, void* active);   /* T-300 fix 2, MAIN THREAD: the orphan purge removed people of this ActivePlatoon - another game's file-built squad in an area another game holds is thrown away once empty; a world NPC squad's copy forgets its record stamp once empty (squadwriter.h NpcPurgeForgetStamp) */
bool PlatoonWipe(const std::string& worldId);
// E30-3 (P6p) lever, MAIN THREAD: quit through the GAME'S OWN pause-menu exit path, so a harness run
// exercises the P6l hook on the confirm dialog's accept path without a human pressing Escape (F010: the
// engine ignores injected input, so there is no other way to reach it). Returns false and logs WHY if it
// refuses - the hook did not arm, no world is running, the quit is already latched, or this is not the
// main thread; it never calls the engine on a refusal. true means the engine's exit path was entered and
// the game is on its way out.
bool QuitViaMenu();
bool KillNamed(const std::string& nameSubstring);   /* E21 (T223) lever, MAIN THREAD: declare dead the first LOADED character whose game-data name contains this substring and whose Character::isUnique says it is a named character. `platoonkill nearest` picks by distance and T223 got a passing group; this picks by name. */
bool PlatoonKill(const std::string& worldId);       // T203 lever: every member declared dead through the engine (the natural wipe-out chain)       // T199 lever: destroy a group as the engine would after a wipe-out (the delete path end to end)
int  StoreRecordCreated(void* platoon);            // P4e: a group created asleep -> the notebook now (1 ok, 0 off, -1 failed)
void StoreAdminDestroyBegin();                     // P4e: our own destroys (retire/supersede) are not "gone"
void StoreAdminDestroyEnd();
/* M2 (decisions 32/44/54): ApplyRemoteGone and StoreWorldListed were the session link's RECORD_GONE / WORLD_LISTED entries - deleted. */
void ApplyDeletedBits(const std::string& faction, const std::vector<char>& bits, const char* from);
bool StoreSendAreas(const int* xy, int count, int playerX, int playerY);   // decision 32: my loaded areas (x,y pairs) to the relay, and (decision 37 amended) THIS game's own player sector after them - INT_MIN,INT_MIN when it is not known, which the relay stores for nobody
bool StoreProfileFactionName(const std::string& name, const char* why, int rename);   /* names2a: MAIN THREAD - PROFILES kind 3 (a load) or 5 (rename != 0: the player renamed), my faction's name for my profile's notebook row; false = not sent now (no profile / link down) */
bool StoreProfileFactionSeen(unsigned num, const std::string& name);   /* T-368, MAIN THREAD: PROFILES kind 6 - this game has the FACTION answer naming `name` for profile num; false = not sent (another pick, link down) */
bool StoreFactionNameTakenInWorld(const std::string& name);   /* T-368, MAIN THREAD: another profile of this world (connected or not) has this faction name (coopprof::FactionTakenIn over the world's TAKEN list; false in single player) */
std::string StoreTakenFactionsText();                          /* T-368, MAIN THREAD: that list and its counters, for a log line */
bool StoreProfileRequest(int kind, unsigned num, const std::string& name);   /* prof1: MAIN THREAD - PROFILES up (1 NEW <name>, 2 DELETE <num>); false = the notebook link is down */
/* prof3 (design-mpmenu1 section 8) - THE YOUR PROFILES SCREEN'S HALF IN THE STORE.  MAIN THREAD, all of them: the PROFILES
   answer is read by PumpLink (main thread), and ui.cpp reads these from its title tick (main thread). */
struct StoreProfRow { unsigned num; std::string name, faction; long long lastPlayed; StoreProfRow() : num(0), lastPlayed(0) {} };
void StoreProfilesByPanel();       /* the panel's Host / Join armed this connection: its profile list waits for the player's pick */
int  StoreProfilesPanelDriven();   /* 1 = armed by the panel AND shared_wastelands.cfg has no TEST profile= (else the automatic pick runs, as before) */
void StoreProfilesPanelGone();     /* review-prof3 D1: the panel latched itself off - the automatic rule takes the profile pick */
int  StoreProfilesWaiting();       /* 1 = panel-driven, a list has come, this game is in the world's waiting room and has picked nothing yet */
int  StoreLobbyAnswerGen();        /* T552, MAIN THREAD: the link generation the world's lobby list came on (still in the lobby, link up), -1 none */
long long StoreProfilesPanel(std::vector<StoreProfRow>* rows, unsigned* cap, unsigned* select, std::string* say, int* verdict);   /* the last answer handed to the screen; returns its change number (0 = none yet) */
int  StoreOpenSaveName(char* out, int cap);   /* PP6 fold-2 F2, MAIN THREAD: SaveFileSystem+0x120, the open save's name (the PP5 redirect's read) - 1 = read into out, 0 = unreadable */
int  StoreAutoLoadFacts(std::string* folder, int* found, int* quickSave, int* neverPlayed, std::string* profName);   /* T-201 PP6': MAIN THREAD - 1 = the pick is admitted and its folder decided */
int  StoreNewGameReshowTake();
int  StoreNewGameReshowPending();   /* 1 = a refused NEW GAME's window is shown again on the next title tick (not taken) - MAIN THREAD */
int  StoreSaveRequestCode(const void* saveMgr);   /* T-201 PP6': SaveManager+0xA0, the pending request code (-1 unreadable) - MAIN THREAD */   /* T-201 PP6' fold (F1): 1 once after a refused NEW GAME - the title pump shows the NEW GAME window again */
bool StoreProfilePickFromPanel(unsigned num);   /* PLAY: ProfilePick(num), the same pick the automatic rule makes; false = nothing waits for a pick */
/* M5a (T-197 piece 4; store protocol 59) - THE LIVE ROAD THROUGH THE NOTEBOOK, MAIN THREAD. StoreLiveReady: this game's
   notebook link is up, welcomed and speaks this build's store protocol (a WELCOME of another protocol is no link), so a
   live game-to-game message can go through it. StoreSendLive: one LIVE (53) up - {route, target, inner type, inner payload}
   (src/common/liveenvelope.h); false = not sent (live[...,sendFailed] on the [STORE] REPORT line). A caller takes EXACTLY ONE
   road per message and never retries a refused one on the session link. StoreLiveGen: the welcomed notebook link's
   generation while StoreLiveReady(), else 0 - it changes once per new welcomed link. M16 fold 3 (T775): `reliable` picks
   the link's channel - true = reliable (channel 0), false = unreliable and unsequenced (channel 1, the latest-wins MOVE and
   STATE, exactly as on the session link); the world server forwards a LIVE with the reliability it arrived with. */
bool StoreLiveReady();
bool StoreSendLive(unsigned int route, unsigned int target, unsigned int innerType, const std::vector<char>& inner, bool reliable);
long StoreLiveGen();
/* MAIN THREAD: what the link to the world server still holds undelivered (net::ITransport::SendQueued) - a bulk sender that
   can wait sends only while it is nearly idle. false = no welcomed link, or the link cannot say. */
bool StoreLiveSendQueued(long long* packets, long long* bytes);
/* M11a S3 (T-197; manager decisions 4(a), 7(a), 2026-09-30), MAIN THREAD. StoreSendHostClosingLive: the world's operator's
   SESSION_CLOSING, ALSO sent LIVE route WORLD (net/session.cpp SessionSendClosing); false = this game is not the operator on a
   welcomed link. StoreWorldRoadPing: a world-road game's PING to the operator by LIVE SLOT (the `ping` verb when no session link is
   up); false = not on the world road, no operator known, or no link. */
bool StoreSendHostClosingLive(const char* why);
bool StoreWorldRoadPing(const char* why);
/* M6 (T-197 piece 6, store protocol 60): StoreLiveProbe - MAIN THREAD, the `liveprobe` command (TEST ONLY): one LIVE AREA
   probe for this game's player sector; the status line. StoreNoteCatchupAnswer - the catch-up answer's counters.
   WorldsyncCatchupAsk (defined in worldsync.cpp) - MAIN THREAD, the drain: answer one CATCHUP ASK. */
std::string StoreLiveProbe();
void StoreNoteCatchupAnswer(bool decoded, bool sent, long long chars);
void WorldsyncCatchupAsk(const std::vector<char>& payload);
bool StoreSendTeam(const std::vector<char>& payload);   /* T-546 step 3: MAIN THREAD - an encoded TEAM request to the world server; false = the link is down */
bool StoreSendFallen(const std::vector<char>& payload);   /* T-556: MAIN THREAD - an encoded FALLEN request (ADD / TAKE / ASK) to the world server; false = the link is down */
bool StoreSendWorldRel(const std::vector<char>& payload);   /* par24: MAIN THREAD - an encoded WORLD_REL (SEED or CHANGE) to the notebook; false = the link is down */
bool StoreSendTownBar(const std::vector<char>& payload);   /* refill1: MAIN THREAD - encoded TOWN_BAR rows to the notebook; false = the link is down */
bool StoreSendOwed(const std::vector<char>& payload);   /* T-581: MAIN THREAD - one encoded OWED up message (src/common/owedpop.h); false = the link is down */
bool StoreSendTownLoss(const std::vector<char>& payload);   /* MAIN THREAD - one encoded TOWN_LOSS up message (REFILLED, src/common/townrefill.h); false = the link is down */
long StoreSaveRequestSeq();   /* ANY THREAD - how many save requests the engine has accepted in this process (the save hook counts them) */
long StoreSaveInFlightSeq(long upTo);   /* MAIN THREAD - the highest request number, not above upTo (0 = no bound), among the saves still being watched to their finish (0 = none) */
bool StoreSendResearch(const std::vector<char>& payload);   /* loot2b: MAIN THREAD - an encoded RESEARCH_BOX (one lifted box) to the notebook */
bool StoreSendResearchTake(const std::vector<char>& payload);   /* loot2c: MAIN THREAD - an encoded RESEARCH_TAKE (one take row) to the notebook */
bool StoreSendUniqueState(const std::string& sid, int state, int playerInvolved);   /* E5 / decision 31(c): MAIN THREAD - a named character's state to the notebook */
unsigned long StoreMainThreadId();   /* review-p5j MEDIUM-2, ANY thread: the id the store captured at its first tick, or 0 while it is not yet known (which a caller must read as "not the main thread") */
int KillNamedIsUniquePod(void* c);   /* towns2: Character::isUnique 0x505ED0 under SEH (the killnamed lever's own call) - 1 unique, 0 ordinary, -1 fault; MAIN THREAD */
int StoreIsDeadPod(void* character);        /* par6 fold (review-par6 #3): Character::hasDied under SEH - 1 dead, 0 alive, -1 fault */
int StoreDeclareDeadPod(void* character);   /* review-p5j HIGH-2, MAIN THREAD: Character::declareDead 0x7A5660 under SEH - the platoonkill lever's own path, now shared with the world-state apply */
std::string StoreModsRefusal(long long* seq);   // settings5 S5, MAIN THREAD: the player-facing mods refusal ("" = none) and its change number
int StoreWrongWorldRefusal(std::string* opened, std::string* theirs);   // T-201 N1 fold, MAIN THREAD: 1 = a WELCOME was refused as another world since the last leave
void StoreLeftAtTitle();                       // T-201 N1, MAIN THREAD: ConfigLeave - the refusal and the profile lobby belong to the link just closed
/* M11a S1 (store protocol 61; src/common/joinstage.h) - THE WORLD ROAD (every joining game). MAIN THREAD. */
bool StoreJoinViaWorld();                                  // role=client: every joining game takes the world road
int  StoreJoinGateVerdict();                               // coopjoin::JoinGate - 0 = a load may go ahead
int  StoreJoinGateRefuses(const char* where);              // the world road only: 1 = refused (said and counted), 0 = go ahead / the session road
int  StoreJoinGateAsk(const char* where, int holdKind, const std::string& name);   // as StoreJoinGateRefuses; 2 = refused for the operator alone and HELD (coopjoin::JoinHoldKind)
int  StoreJoinHoldPoll(int holdKind);                      // a held load of that kind: 0 = none (never held, or dropped), 1 = still held, 2 = start it now (the hold ends here)
int  StoreRosterSlotInWorld(int slot);   // M7a fold F1, MAIN THREAD: 1 = that slot is IN_WORLD on THIS link's PLAYERS roster, 0 = it is not, -1 = no roster on this link / no slot
int  StoreRosterNames(std::vector<std::string>* others);   // the other players on THIS link's PLAYERS roster; -1 = no roster on this link
int  StoreWorldPlayerCount();                              // MAIN THREAD: every player number the world server ever gave in this world (THIS link's PLAYERS roster); -1 = no roster on this link
int  StoreRosterNameOf(int slot, std::string* name);   // MAIN THREAD: the player name the last PLAYERS roster gives that slot; 1 = a name, 0 = listed without one or not listed, -1 = no roster yet
/* M11 C1 (T-197, to-do M11; src/common/presence.h): is another player in this world - the old game-to-game link up, OR this link's
   PLAYERS roster shows another slot IN_WORLD. The seven single-writer sites ask this instead of the old link alone. */
bool PlayersPresent(int site = -1);      // ANY THREAD (the old link: read live on the main thread, StorePresenceOldLinkTS's per-frame copy on any other - review-p5d CRASH-1; + the roster's copy below); site = mppresence::Site, counted
void StorePresenceTick();                // MAIN THREAD, every frame (WorldGenTick): copies the roster's answers for any thread; [PRESENCE] logged on a change
int  StoreRosterOtherInWorldTS();        // ANY THREAD: 1 = another slot IN_WORLD on this link's roster, 0 = none, -1 = no roster (as of the last tick)
int  StoreRosterLowestInWorldTS();       // ANY THREAD: the lowest IN_WORLD slot on this link's roster (this game's own included); -1 = none / no roster
int  StorePresenceOldLinkTS();           // ANY THREAD: the old game-to-game link was up at the last tick (the zones workers' copy)
void StorePresenceTieNote(int byRoster); // ANY THREAD: the area tie-break's no-table arm was decided by the roster (1) or by the session role (0)
/* M11 C2 (T-197, row M11; src/common/arrivals.h): WHICH OTHER PLAYER HAS JUST ENTERED THE WORLD. The old link coming up is the
   session peer's arrival; the roster's IN_WORLD for the same slot is the same arrival (merged, either order); any other slot reaching
   IN_WORLD while this game is in the world is a new one. Every function below: MAIN THREAD (off it, the record is not fed). */
enum ArrivalServeSite { kArrServeDoors = 0, kArrServeBuild = 1, kArrServeParity = 2, kArrServeInvNet = 3, kArrServeSettings = 4,
                        kArrServeOrphan = 5, kArrServeCrime = 6, kArrServeRelations = 7, kArrServeCount = 8 };
void StoreArrivalTick();                 // feeds the old link into the arrival record (StorePresenceTick, and every ask below)
long StorePeerEpoch();                   // the "generation changed" sites' key: moves once per arrival and once per old-link down (two games: as SessionLinkGen)
int  StoreArrivalsSince(int site, long* cursor, int* serveTo);   // arrivals since *cursor (moved to now); *serveTo: the one slot to address, -1 = everyone; counted per site
void StoreArrivalNoteServed(int site);   // a "generation changed" site re-sent (counted per site)
int  StorePeerHere(unsigned int peer);   // ONE PARTICULAR PLAYER: 1 = here - the session peer by the old link (as before), another by this link's roster
long StorePeerGen(unsigned int peer);    // that player's epoch - the session peer: SessionLinkGen(); another: its own arrival count (negative)
int  StorePeerByLink(unsigned int peer);  // 1 = that player is judged by the old link now (mparrive::PeerByLink); a raw link id always is
int  StorePeerHereAs(unsigned int peer, int byLink);   // StorePeerHere judged the way a hold chose when it was made (byLink: StorePeerByLink then)
long StorePeerGenAs(unsigned int peer, int byLink);    // StorePeerGen judged that way - one key per hold (mparrive::PeerGenIn)
int  StoreOtherBeyondLinkInWorld();      // 1 = this link's roster shows another slot IN_WORLD that is not the old link's peer - with that link never up in this process, any other slot IN_WORLD (mparrive::OthersInWorldBeyondLink)
int  StorePlayerReachable(int slot);     // 1 = a send meant for that player (-1 = any other game) can go now: the session peer by the old link, another by the world-server road (mparrive::Reachable); session link up -> 1 for -1. Off the main thread: the old link's per-frame copy
int  StoreArrivalRoundTarget(int to, int owedOrActive);   // whom a round owed for an arrival goes to: `to`, or -1 = every game (mparrive::RoundTarget)
int StoreRefusedReason();                      // mp1, MAIN THREAD: why the notebook refuses this game (coopstore::StoreRefuseReason), -1 = not refused
int  StoreMySlot();                                      // decision 32: my slot in the relay's order (-1 unknown)
/* B9 (design-e46-store 2.2, decision 44). ANY THREAD: how many milliseconds this game has been unable to
   see the notebook process. 0 while the link is up. The clock starts at the store's FIRST tick, so a game
   that never reaches the notebook at all still ages - "we have never had one" is an outage and not an
   exemption. Read by the writer ladder's R4 and by nothing else. */
long long StoreNotebookDownMs();
/* THE GRACE R4 REFUSES AFTER. One named constant so the user can move it after one run: past this, a game
   with no notebook stops writing boxes and shop stock and says so out loud, rather than guessing quietly. */
const long long kNoNotebookRefuseMs = 60000;
// P6j (verify-p6c HIGH-2), ANY thread: the last slot this game was ever given, which SURVIVES a link drop while
// StoreMySlot() does not. It exists for exactly one question - "is this row of the notebook's player-sector table
// my own echo?" - which a game with no current slot cannot otherwise answer, because the relay broadcasts every
// row to every game including its author. -1 = no slot has ever been assigned in this process.
int  StoreLastKnownSlot();
// P6q (review-p6j MEDIUM-1), ANY thread: THE LINK GENERATION. A slot belongs to the numbering that issued it, and
// every link-up edge is a new numbering. (Since B13 / prof1 the notebook keys slots by PROFILE in slots.txt and never
// hands a slot to anybody else - the old "first-free index in HELLO order" reason is gone. A slot is still read only for
// the link whose WELCOME gave it: M4 fold L1/L2 - g_mySlot comes only from a matching WELCOME, and a uid is minted only
// under this link's WELCOME.)
// StoreLinkGen() counts those edges (it starts at 0 and the first connection makes it 1). StoreWelcomedThisLink()
// is 1 only once the WELCOME of the CURRENT generation has been processed, which is the first moment this game can
// recognise its own row in the notebook's player-sector table. Both are interlocked reads of a plain aligned long.
// P6x (review-p6q LOW-2): StoreWelcomedThisLink reads the WELCOME generation FIRST and the link generation second,
// which is what makes "a link-up edge landing between the two reads answers 0" true. The other order left both
// reads holding the old generation, so they agreed and the answer was 1 for a link whose WELCOME had not arrived.
int  StoreLinkGen();
int  StoreWelcomedThisLink();
/* U2, MAIN THREAD ONLY: is the notebook link actually up RIGHT NOW?  StoreWelcomedThisLink() compares
   generations and can still answer 1 for a link that has since dropped (see the paragraph above), so the
   MULTIPLAYER panel's "connected" test is the AND of the two.  This dereferences g_link, which the main
   thread deletes in SetStoreServer - off-thread callers must read StoreRelayLinked() instead. */
bool StoreLinkIsUp();
/* The link to the world server is up and not refused, whether or not its WELCOME has come: a player with no profile picked
   waits in the profile lobby, which holds the WELCOME back until the pick. MAIN THREAD ONLY. */
bool StoreLinkConnected();
/* MAIN THREAD ONLY: the world-server link's state as coopstore::StoreSocketState (src/common/storelink.h) - down / connecting /
   up and welcomed / up and not welcomed / up and refused. config.cpp's title re-dial reads it. */
int  StoreLinkSockState();
/* MAIN THREAD ONLY: one re-dial of the world-server link - the link closed (quietly), then, when dialAgain, dialled at addr:port.
   SetStoreServer alone keeps a non-null link to the same address, so a re-dial is always this pair. Used by StoreRedialTick in a
   world and by config.cpp's title re-dial. */
void RedialClearAndDial(const std::string& addr, unsigned short port, int dialAgain);
/* TEST-ONLY `worldaway <seconds 5-600>`, MAIN THREAD: this game's world-server link (up and welcomed, a world running) is closed
   here - the world server sees this player leave - and the re-dial is held for that long; then StoreRedialTick dials it again at
   once, as after any outage (this game's world stays loaded: its next WELCOME brings the tables, the rows owed and the records).
   Returns the status. */
std::string StoreWorldAwayLever(const std::string& args);
/* loot2c part B, MAIN THREAD: one line on the game's own message system (ShowGameMessagePod). 1 shown, 0 declined, -1 raised. */
int StoreShowPlayerLine(const std::string& line);
/* T-556 MAIN THREAD: the game clock's day number (the clock's own int, as the money bar's day is counted); -1 when the clock does
   not read. */
int StoreGameDay();
/* U2 / W2b (decision 58), MAIN THREAD (it reads ConfigWorldKey): this game's world folder,
   %LOCALAPPDATA%\kenshi\save\coop-store\<world>, built by coopworld::WorldDir - the same construction
   SharedWastelandsServer.exe uses for its --world, so the plugin and the notebook name the same folder by construction
   (6a lesson 11). The panel passes the WORLD NAME, not this path; it names the folder in its log line. */
std::string StoreNotebookDir();
/* mp3, MAIN THREAD: the shared-world helper's TOP folder - storedir= when present, else <save root>\coop-store - with one
   folder per world inside it. The MULTIPLAYER panel's Host a game screen lists, makes and deletes worlds there. */
std::string StoreTopDir();
/* MAIN THREAD (the title panel): the world's saves on this computer to the Recycle Bin - every save folder in Kenshi's save root whose
   key file names worldName (any profile; coopprof::WorldLeftoversFind) and each matching records store in <data>\\players. Called by
   DELETE WORLD only, after the world folder went. The save folder this game plays from, its profile's records stores and any store open
   now stay (in use). A folder Windows could not recycle is kept, never deleted. Returns how many could NOT be moved (the in-use ones are
   not counted); *summary = what was recycled, how many stayed in use and how many could not be moved. */
int StoreWorldRecycleLeftovers(const std::string& worldName, const std::string& worldId, std::string* summary);   /* T-490: worldId = world.txt line 2 ("" = none: nothing recycled) */
/* T-201 PP6' (owner 178a): DELETE on HOST GAME -> CHANGE, the world not running - its profiles.txt edited here (coopprof::DeleteApply) and the
   profile's save folder to the Recycle Bin as the DELETE answer does. MAIN THREAD. coopprof::kOk = deleted; *say = the PROFILES line. */
int  StoreProfileDeleteOffline(const std::string& folder, const std::string& worldKey, unsigned num, std::string* say, int* box);   /* T-220: box 1 = the CAN'T DELETE box (say empty) */
/* MAIN THREAD: this player's id as the world in <worlds>\<folder> knows it - ConfigPlayerId() through that world's aliases.txt (itself when none). */
std::string StoreWorldPlayerId(const std::string& folder);
/* loot2c x prof1 merge, MAIN THREAD: the id the notebook knows THIS connection by - coopprof::SlotKey(person, picked profile)
   (profile 1 = the bare person id, n >= 2 = "<person>.<n>"; the notebook's g_peerId), or the person id while no profile is
   picked. A research take row must name it, or the notebook refuses the row (takesIdMismatch). */
std::string StorePlayerKey();
/* W2b, MAIN THREAD: the folder name of the world whose record index this game has read, "" while none is read.
   A settings re-read naming another world is refused while this is set (config.cpp ConfigRearmFromFile). */
std::string StoreIndexWorld();
void StoreNoteWorldSwitchRefused();   /* W2b: counts that refusal - worldSwitchNeedsRestart on the store report */
// E36 / decision 40. The value is one of policy.h's kPolicy* and is written ONLY by store.cpp's MSG_OPTIONS
// handler; `unknown` means no notebook process has told us and is never silently read as `shared`.
int BasePolicyValue();
const char* BasePolicyName();
const char* BasePolicyNameOf(int v);
// E40 / decision 45, MAIN THREAD: record this player's own speed setting (0 = pause, or any pace up to
// coopclock::kClockSpeedMax - the buttons' 1, 2, 5 or a speed mod's) as a VOTE and send it
// to the notebook. It never sets this game's own speed - under decision 45 the only thing that does is the
// notebook's broadcast coming back, so a `speedvote 0` here is a request to pause the world (granted outright
// under `consensus`, and only from the host under `fixed`), not a local pause. false = NaN, negative or above
// kClockSpeedMax.
bool SpeedVoteCommand(float speed);
// A test-only lever, MAIN THREAD: `gamespeed <x>` writes x straight into the engine's speed global as a speed mod
// does (no button, key or setter call); *was = the value before. 0 written, -1 no speed global, -2 faulted.
int GameSpeedTestWrite(float speed, float* was);
// The same lever's sweep form: the write repeated every everyMs from `from` to `to` by `step`. 0 started, -1 no speed global.
int GameSpeedTestSweep(float from, float to, float step, unsigned int everyMs);
// MAIN THREAD, from the clock tick: the sweep's next write, when it is due.
void GameSpeedSweepTick();
// E40 / decision 45, MAIN THREAD: "fixed", "consensus", or "unknown (no notebook clock yet)". The value is the
// mode byte carried in the notebook's CLOCK message, which the notebook derives from its own options.txt row -
// one authority for the rule, never two that can disagree.
const char* TimeModeName();
// MAIN THREAD: ask the notebook process to store an option. The relay accepts it only from the game it calls
// the authority and broadcasts the stored map back to everyone; false here means only that it could not be
// SENT (no relay link), never that it was refused - a refusal is the relay's to log and to not broadcast.
bool StoreSendOption(const std::string& key, const std::string& value);
// settings2 S2, MAIN THREAD: several keys in one OPTIONS message (same contract: false = not sent).
bool StoreSendOptions(const std::vector<std::pair<std::string, std::string> >& kv);
// settings2 S2, MAIN THREAD: the notebook calls this game the world's authority on the current link.
bool StoreIsWorldAuthority();
int  StoreTearingDown();                                 // P6c (p5z MEDIUM-2), ANY thread: the engine is tearing the world down - nothing may be invented while it is. P6q (review-p6j LOW-1): an INTERLOCKED read, because the callers that matter are worker threads and a stale `false` here means "invent into a world the engine is freeing right now". P6x (review-p6q MEDIUM-2): the flag behind it is a DEPTH (raised in TeardownBroadcast, lowered in TeardownBroadcastLateFlags) and this answers depth > 0, so a nested teardown's lower cannot open the gate while an outer teardown is still freeing the world
/* ============ P7v (design-noworld-queue 6) - THREE CAUSES, THREE ANSWERS, ONE VERDICT ============
   StoreWorldSettled() was ONE BOOL carrying THREE causes, and review-p7p H-1 is what that cost: the arm has one
   answer (kBoxMine) and P7p gave it a second cause (a stale map during LIVE PLAY), which turned a stale
   SINGLE-writer state into a TWO-writer state about ten seconds after a relay wedge. One bool cannot carry three
   causes. ANY THREAD (an interlocked read of one cached long, refreshed once per StoreTick), read LIVE at each
   commitment.
     kPicLoading    - EngineWritesBlocked(), or a world is running and NO area map has arrived since THIS world's
                      load. ANSWER: MINE. Refusing here is review-p6z H-3 / review-p6o H-4 - a client whose whole
                      world loads with empty shops, because RootObjectFactory::createRandomSquad's initial
                      stocking runs inside this window.
     kPicNoNotebook - no session link, or no notebook link. ANSWER: the designed no-relay routing, untouched.
     kPicStale      - a world is running, both links are up, a map DID arrive for this world, and it is older
                      than kAreaMapStaleMs. ANSWER: FALL TO THE SAME SINGLE-WRITER ROUTING kPicNoNotebook uses -
                      not MINE and not REFUSE. Staleness is SYMMETRIC (the relay broadcasts to every peer from
                      one loop), so MINE means two writers on every box and shop, and REFUSE means NOBODY writes:
                      every box move reverted at every box on both machines, and a createRandomSquad during live
                      play stocked by nobody. net::SessionIsHost() is written once at StartHost/StartClient and
                      never again - exactly one game answers 1 and BOTH AGREE AT EVERY INSTANT - so the routing
                      has neither harm. That is review-p7p H-1's own repair shape, in its own words.
     kPicFresh      - otherwise. The holder rule, untouched.
   THE PRIMARY SIGNAL FOR "THE NOTEBOOK IS NOT ANSWERING" IS ENet'S OWN LINK DROP - an event, not the timer. The
   relay broadcasts an AREAMAP to every peer every second from inside the same accept loop as enet_host_service,
   so a relay that has stopped sending maps has also stopped servicing ENet and the peer drops inside ENet's own
   5-30 s window. kAreaMapStaleMs is sized at the TOP of that window precisely so the freshness never fires ahead
   of the event; its only job is the case the event cannot see - a relay that services its socket and stops
   broadcasting. P7p's 10 s fired BEFORE the drop in the common case, which is mechanically how it made live play
   worse. */
const int kPicLoading = 0, kPicNoNotebook = 1, kPicStale = 2, kPicFresh = 3;
int StoreAreaPictureVerdict();
/* ====== P7v (build/design-noworld-queue.md) - ONE ARRIVAL QUEUE, ONE MAY-I-WRITE QUESTION ======
   THE CLASS OF DEFECT THIS REPLACES, not the three bugs. P7f, P7h and P7p each added a second, unaware drop
   policy, and each made one arm answer two causes. So: every inbound thing that could touch engine memory - the
   notebook's records, gone-marks and unique states, the peer's records and gone-marks, every session message the
   welcome-only pump used to defer, and the two teardown ACTIONS (SessionLeave and OnPeerGone) - is appended to
   ONE FIFO arrival queue in one arrival order across both links and is never dispatched anywhere else. ONE
   predicate is asked at exactly one place, the drain, live, at the top of every entry. NOTHING is dropped to
   make room. Entries are removed by NO edge-driven walk: an edge bumps a generation and the drain discards stale
   entries lazily when it reaches them, and any gone-mark discarded on any path sets decision 30's permanent bit
   FIRST - so a delete can never be lost behind a surviving record whatever order the queue is in. */
/* THE ORIGIN of an entry. The two message-number spaces OVERLAP (net::MSG_DELETED_BITS is 36 and the store
   link's UNIQUE_STATE is 36), which is why type and origin are SEPARATE FIELDS and never one merged kind:
   P7p's kDeferRecordSession made one number mean two things. */
const int kOriginNotebook = 0, kOriginSession = 1, kOriginLocal = 2, kOriginRelay = 3;   /* M5a (T-197 piece 4): RELAY = a live game-to-game message another game sent THROUGH THE NOTEBOOK (LIVE, 53). It arrives on the notebook link, so it carries and is judged by that link's generation and its saturation stops polling that link; it is dispatched by the session layer's own handler, with the sender id 0x80000000 | the origin's slot (src/common/liveenvelope.h), never a transport peer id. Its own origin, so its bound, its back-pressure and its discards are counted apart. */
/* THE SCOPE, and it is stated per KIND, not per origin. A world-generation bump is the right key for live-world
   session traffic (it names a uid of an engine world instance that no longer exists) and the WRONG key for
   persistent records: under decision 32 the world is the notebook's, a local reload rebuilds the same world, and
   the queued index is exactly what should be applied to it. Dropping the notebook's opening push because the
   player loaded a save would discard the thing the load gate exists to wait for. */
const int kScopeSave = 0, kScopeWorld = 1;
/* LEVEL = superseded by the next one for the same subject, so it COLLAPSES IN PLACE at enqueue (keeping the
   EARLIER slot, so a level can never overtake an edge that preceded it) and the queue does not grow. EDGE = a
   state change with no periodic reconciliation anywhere in this build, so a lost edge is a permanent divergence
   (review-p7h H-3) and an edge is NEVER dropped to make room. THE DEFAULT IS EDGE. */
const int kClassEdge = 0, kClassLevel = 1;
/* The three ACTION kinds. They ride kOriginLocal, so they cannot collide with any message number. */
const int kActPeerGone = 1, kActSessionLeave = 2, kActResendHello = 3;
/* P7w (F599): the drain tells the session layer when it DISCARDS one of the three, because two of them
   carry state that outlives the entry - kActSessionLeave's latch holds the session poll loop shut and is
   cleared only inside SessionLeave. Declared here beside the kinds it takes. */
/* THE ONE PREDICATE FOR "AN ENGINE WRITE MUST NOT HAPPEN RIGHT NOW". ANY THREAD. Renamed from
   NoWorldForRecords because it now gates ACTIONS as well as records, and it gains the engine-teardown depth.
     !GameplayRunning()   F337's frame-counter test, the only honest "is there a world" this build has. P7v
                          lifts its one writer to the top of the frame (SoakRefreshRunning), so it is no longer
                          a frame stale for readers inside the session pump.
     g_loadDepth != 0     raised at the top of detour_resetGame, of detour_worldTeardown and (P7v) of THE WHOLE
                          of detour_loadGame, lowered in each one's __finally. A save loaded from a RUNNING
                          world leaves GameplayRunning() reading TRUE for the entire load.
     StoreTearingDown()   the engine-teardown DEPTH. At HEAD this term adds nothing - both callers of
                          TeardownBroadcast already hold the load depth across it - so it is a DEFENSIVE term
                          against a third caller and it is written down as such rather than claimed as a fix.
   ONE PREDICATE, NEVER A COPY, AND THE ENUMERATION NOW NAMES THE ACTIONS (review-p7p M-7): the enqueue's
   deferred stamp, the drain (per entry), both no-world detectors, the clock/speed/weather applies, the picture
   verdict's loading term, and the two teardown ACTION kinds that used to run straight out of the welcome-only
   pump - review-p7h H-1's engine-touching path inside the window this header used to declare closed. */
bool EngineWritesBlocked();
/* T-546 step 5: a load's own-record restore (money, research, map, faction) is armed and has not run yet - the team's standing
   writes wait for it, so the save's record does not overwrite them. MAIN THREAD. */
bool StoreOwnRestorePending();
/* T-546 step 6 (shared research; team.cpp, src/common/teamresearch.h). MAIN THREAD unless said.
   StoreResearchTakeOwnFinished: the techs (stringIDs) the Research::setResearched hook saw finished on this game since the last
   take - this game's own player's finishes - and how many were dropped past the list's cap.
   StoreResearchFinished: this game's finished techs (Research::save's "finished<i>") and its queue's length; false = unreadable (*why).
   StoreResearchLoadUnion: the techs of `team` this game lacks and knows, appended to its own record (every finished tech taken
   out of its queue, the rest of the queue kept) and loaded ONCE through Research::load (no toast); *added = how many; "" = done
   (also when nothing was missing), else why not.
   StoreResearchApply: one tech, by the road this game's queue allows at that moment (teamresearch.h ResearchApplyRoute; *route):
   the engine's setResearched (its "Research complete" toast and sound) - asOwn: through the hook, as this player's own finish
   (the TEST-ONLY lever, refused for a tech queued behind the front); else as a teammate's, which the hook does not report - or,
   for a teammate's tech queued behind the front, one Research::load (no toast). 1 = applied, 0 = not (*why), -1 = fault.
   A fault in either switches the research category OFF for the process (StoreResearchOff; logged once).
   StoreResearchQueue: this game's research queue now, front first (stringID, progress), and as text. StoreResearchQueueTech:
   TEST-ONLY - the engine's own Research::addToQueue (the research screen's "add"); "" = queued, else why not (*detail: the
   research level). StoreResearchNameOf: a tech's name in this game's data (the stringID when it has none). StoreResearchLevel:
   this game's research level now (Research+0x170, what the queue's "add" checks a tech's level against), -1 = unreadable. */
int StoreResearchLevel();
void StoreResearchTakeOwnFinished(std::vector<std::string>* out, long long* dropped);
bool StoreResearchFinished(std::vector<std::string>* sids, long long* queued, std::string* why);
std::string StoreResearchLoadUnion(const std::vector<std::string>& team, int* added, std::string* detail);
int StoreResearchApply(const std::string& sid, bool asOwn, std::string* name, std::string* why, int* route);
bool StoreResearchOff();
bool StoreResearchQueue(std::vector<std::pair<std::string, float> >* q, std::string* text, std::string* why);
std::string StoreResearchQueueTech(const std::string& sid, std::string* name, std::string* detail);
std::string StoreResearchNameOf(const std::string& sid);
/* crime6 / crime10: this game's own absolute world hours - the double at clock+0xA0 the bounty map's times are measured
   against (crime9 answer 1) - -1 when unreadable. crime.cpp logs it and clamps every bounty time it writes to it.
   MAIN THREAD. */
double LocalWorldHours();
// save2 (user decision 2026-09-25): a test save through SaveManager::save (the Save menu's door, our detour included), isAutosave
// = 1. 1 accepted, 0 another save pending (retry), -1 unreadable, -3 refused by the profile-folder redirect (T-251 fold item 2:
// never retried). MAIN THREAD. The caller checks the name.
int StoreRequestTestSave(void* saveMgr, const std::string& name);
// Owner 204 TEST-ONLY (`handkey save|load`): the engine's own SaveManager::save("quicksave", 0) (F5) or load("quicksave") (F9)
// through the hooked entry - nothing else. 1 called, -2 that hook is not installed (not called), -1 no SaveManager. MAIN THREAD.
int StoreHandKey(void* saveMgr, int load);
// 1 = the save left the request pump, the SaveFileSystem copy finished and <slot>\quick.save exists; 2 = finished but no
// quick.save (failed); 0 running; -1 unreadable. -2 from StoreRequestTestSave = the save hook is not installed.
int StoreTestSaveDone(const void* saveMgr, const std::string& name, std::string* dirOut, unsigned long long notBeforeFt = 0);   /* mmo4 fold: notBeforeFt (a FILETIME, 0 = unchecked) - 3 = quick.save exists but is older than that (2 s slack) */
/* THE ARRIVAL QUEUE'S ONE ENTRY POINT. MAIN THREAD. `type` is the message number as it arrived (or an action
   kind), `origin` says which numbering it belongs to, `subject` is a LEVEL's key (a uid; 0 for edges) and
   `peer` is the sender's peer id for session entries. */
void InQueueEnqueue(int type, int origin, int scope, int cls, unsigned int subject, unsigned int peer, const std::vector<char>& payload);
/* THE ONE DRAIN. MAIN THREAD. Called twice a frame - from CommandChannelTick just below net::SessionTick and
   from StoreTick just below PumpLink - which is ONE function over ONE FIFO with ONE head cursor, and it
   preserves today's latency exactly: a message polled this frame is still applied this frame. */
void InQueueDrain();
/* MAIN THREAD, the first statement of each frame (CommandChannelTick): closes the last frame's copy-budget tally into the burst
   book (one [SPAWNPACE] line per burst) and starts the next; the frame's two drains share one budget (spawnpace.h). */
void SpawnPaceFrameStart();
/* the copy budget's counts, for the spawn REPORT line */
std::string SpawnPaceToken();
/* BACK-PRESSURE, MAIN THREAD: has this origin passed the high-water mark? The pump stops polling that origin's
   socket while it has (ENet then buffers, and reliable channels retransmit - which is precisely the pre-P7h
   behaviour that lost nothing), and both load gates stop waiting on a saturated queue, because the thing they
   are waiting for cannot be enqueued. */
int InQueueSaturated(int origin);
/* THE NOTEBOOK LINK'S GENERATION, ANY THREAD, bumped on the notebook link's UP and DOWN edges. Separate from
   StoreLinkGen(), which moves on the UP edge only and is what the WELCOME/AREAMAP slot comparisons key on: this
   one has to move on BOTH edges or an entry queued before a drop would survive it. */
long StoreNotebookLinkGen();
/* P7f (review-p6z C-1), RENAMED BY P7p (review-p7f M-1). MAIN THREAD: a record-class message ARRIVED with no
   world. It is booked ABOVE the refusals of the function it sits in, so it counts arrivals - a malformed
   UNIQUE_STATE at the title screen books it without touching anything. This number is a WINDOW REPORT, and a
   non-zero here is not by itself a defect. Exported so worldstate.cpp books the same one. */
void StoreNoteRecordArrivedNoWorld(const char* what);
/* P7p (review-p7f M-1) - THE ACCEPTANCE TOKEN, AND THE ONE THAT MUST READ 0. MAIN THREAD: booked immediately
   before the FIRST engine-touching call of an apply that is running with no world, which is review-p6z C-1's own
   shape. Exported for the same reason as the one above. */
void StoreNoteRecordEngineTouchNoWorld(const char* what);
/* P7v: StoreQueueSessionRecord / StoreQueueSessionGone are RETIRED. The classification table in net/session.cpp
   routes MSG_RECORD and MSG_RECORD_GONE into InQueueEnqueue before any handler runs, so net::OnRecord and
   net::OnRecordGone have ceased to be inbound entry points - their decode-and-apply bodies survive as the
   drain's replay, reached only through net::SessionDispatchQueued. M2: both are now deleted - the session link
   drops RECORD and RECORD_GONE at the poll, because the notebook alone carries them. */
/* ============ B12 (decision 52): THE OUTAGE QUEUE ============
   A write decision 44 refused because the notebook process is unreachable is WRITTEN DOWN instead of being
   lost, and re-issued at the next link-up. `family` is one of coopqueue::kQueueFamily* (box, door, zone);
   `recordKey` is the NOTEBOOK RECORD the entry's conflict is decided against - the box family's sector zone
   record, the zone family's own id, and for a door a key that resolves to no record at all, which is
   correct: a door has no notebook record, so nothing can have changed it and its entries always replay.
   `payload` is the family's own saved write. Returns 1 when the entry is in the journal and on disk.
   MAIN THREAD ONLY (it reads the record index and rewrites the journal file); an off-thread call is
   refused, counted and said, never served. */
int StoreQueueWrite(int family, const std::string& recordKey, const std::vector<char>& payload);
/* The two per-family replays, defined in items.cpp and doors.cpp beside the live writes they re-issue
   (the journal's drain in store.cpp calls them; it does not carry a second copy of either write - lesson
   11). Each returns 1 when the entry is finished with and 0 when the send could not go out, which leaves
   the entry in the journal to re-arm at the next link-up. */
int ItQueueReplayBox(const std::vector<char>& payload);
int DoorsQueueReplay(const std::vector<char>& payload);

bool StoreRelayLinked();                                 // the relay link is up   // decision 30: a faction's bitmap arrived (OR-merged)                           // decision 28: the host's "world listed" marker arrived   // P4e: a gone mark arrived
int StoreRecreateSleepingFromRecord(const std::string& worldId);   // F492: after a peer drop, the last sleep record becomes the sleeping copy again                              // section 13: engine-unload this save's own awake copy (no record)
// A record from the peer (net): stored, and applied to a local sleeping squad of that id.
void ApplyRemoteRecord(const std::string& worldId, const std::string& squadSid, const std::string& factionName,
                       float x, float y, float z, long long writtenAt, int owner, const std::vector<char>& bytes, unsigned recFlags = 0);   /* recFlags: the RECORD's flags byte (factionkey.h) */
/* tickwait (coop.cpp): the AI-worker wait's counters, printed on the [SAVE] hb[ line */
std::string TickWaitToken();
/* PROBE P113 (T-341): the p113squad lever (store.cpp, end of file) and its 1 Hz tick */
std::string P113SquadLever(const std::string& args);
void P113LeverTick();
}
