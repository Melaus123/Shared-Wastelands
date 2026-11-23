// build.h - build1-a/b (build1-b: an own-faction placement is sent as MSG_BUILD PLACE and copied as coop-peer on the other game)
// build1-a (docs/design-build1.md sections 2-3): OBSERVE construction on this game, plus a test lever.
// Nothing crosses between games in this effort. The four observe hooks (probe P082, .modding/04-probes.md) count and
// log; the registry `buildreg` (position key -> record sid, owner, progress) is filled by the placement hook and by
// the lever's own placement, never by walking zones.
#pragma once
#include <string>
#include <vector>
namespace coopbuild { struct BuildMsg; }   /* build1-b: src/common/buildwire.h */
namespace powerwire { struct PowerMsg; }   /* P47: src/common/powerwire.h */
namespace coopown { struct BuildRow; struct HelpRecRow; }     /* mmo8a: src/common/ownrec.h (pp.build); help1 fold: pp.help */
class hand;   /* T-1130 re-check: game/hand.h - BdHandLookupPod below */
namespace coop {
int BdHandLookupPod(const hand* h, int which, void** out);   /* T-903 (F1026), T-1130 re-check: a hand's engine lookup, guarded - which 0 = hand::asBuilding, 1 = hand::asObjectBase; 1 = it returned (*out its answer, null included), 0 = it faulted (build.cpp; items.cpp's shop home walk sorts its handles with it) */
unsigned int BuildSafePointNo();   /* T-1130 re-check, MAIN THREAD: the K2 safe points seen (BuildCopyDrain) - the clock a dead-handle set ages on (coopbuild::BuildDeadSetStep) */
void InstallBuild();                                  // preload: the observe hooks (address table names below)
void BuildTick();                                     // MAIN THREAD, every frame: drains the detours' POD queue into the log and the registry
std::string BuildTestArm(const std::string& arg);     // MAIN THREAD: `buildtest place <sid> [dx dz] | progress <key-substring> <amount> | dismantle <key-substring> | into <near|host-key-substring> <sid> [indoors]`
void* BuildTestPlacedLive(std::string* why);         /* TEST-ONLY, MAIN THREAD: the piece the last `buildtest place` made while it is loaded, else 0 and why */
void BuildCopyDrain();                                // build1-b, K2 safe point next to BuildTestDrain: pending PLACEs -> coop-peer copies
void BuildNoteRecv(const coopbuild::BuildMsg& m, unsigned int fromPeer);   // build1-b/c/d, MAIN THREAD (session dispatch): a decoded PLACE, STATE or REMOVE (build1-d), queued
void BuildForgetWorld();                              // build1-c (review-build1b M2): world teardown - pending PLACEs / STATEs and the copy rows
void BuildNoteBad();                                  // build1-b: a MSG_BUILD that did not decode
void PowerNoteRecv(const powerwire::PowerMsg& m, unsigned int fromPeer);   // P47, MAIN THREAD (session dispatch): a POWER_STATE / POWER_ASK, kept for the K2 safe point
int  CraftAskOf(void* b, int slot, int op, const std::string& sid, const std::string& extra, int mode, unsigned int index);   // T-1192 (c), MAIN THREAD: the crafting window's add / entry removal / repeat asked of the bench's writer at slot (CRAFT_ASK, op powerwire::kCraftOp*); 1 asked, 2 a like ask already waits (not sent again), 0 not sent (said)
void BuildTestDrain();                                // K2 safe point (combat.cpp detour_tsRagdollUpdates): runs the armed request
std::string BuildListCommand();
#ifdef SW_DEV_BUILD
std::string BuildPartsCommand(const std::string& arg);   // T-1372 decision 1690, TEST-ONLY, DEV build, MAIN THREAD, read-only: `bldparts <key-substring|key>` - is a building intact (one [BUILD] bldparts line)
std::string BdEditorKeyArm();   // T-1414, TEST-ONLY, DEV build, MAIN THREAD: `editorkey` - the Level Editor key's own call runs on the next game frame
void BdEditorKeyTick();         // T-1414, TEST-ONLY, DEV build: each frame after the engine loop - runs an armed editorkey press (one [EDITOR] line)
std::string BdNavKeyLeverArm();  // T-1462, TEST-ONLY, DEV build, MAIN THREAD: `editorkey rebuildnav` - the rebuild_navmesh key action's two calls, next frame
void BdNavKeyLeverTick();         // T-1462, TEST-ONLY, DEV build: each frame after the engine loop - runs an armed rebuildnav (one [EDITOR] line)
#endif
void BdRegenSectorPress();        // T-1462 (owner 1838), MAIN THREAD: the Level Editor window's 'Regenerate sector' - the area in view rebuilt on this game
void BdRegenSectorTick();         // T-1462: each in-world frame - a press that waits for the engine's own job for its area
std::string BuildStateCommand(const std::string& arg);   // KEPT, TEST-ONLY, MAIN THREAD, read-only: `bldstate <key-substring|key>` - a piece's own state as the kept lay writes it
std::string BuildSetArm(const std::string& arg);         // KEPT, TEST-ONLY, MAIN THREAD: `bldset <key-substring|key> <field> <value>` - armed, written at the K2 safe point
std::string OutpostTestCommand(const std::string& arg);   // OUTPOSTS, TEST-ONLY, MAIN THREAD: `outposttest found <name> [dx dz] | refound <name> <dx> <dz> | link <key-substring> | list` - found / refound / link armed for the K2 safe point, list read now; `found relink <name> [dx dz]` (T-1332) also links the own pieces the new town is now nearest to, as the engine's founding does
void BuildTownsMapEvent();                               // T-1158 step 6 review MED-2, MAIN THREAD (store.cpp KeptRecordSweepTick, each applied area map): released pieces held for another writer owe their area's own-piece check
void BuildTownsNote(const char* p, size_t n);            // OUTPOSTS, MAIN THREAD (store.cpp): a TOWNS LIST from the world server - the kept outposts and their pieces' keys
void BuildShowNote(const char* p, size_t n);             // T-811, MAIN THREAD (store.cpp): a SHOW from the world server - the teammates' outposts, shown at the next drain
void BuildShowBeforeSave(int onMainThread);              // T-811 (store.cpp saveGame pre-hook): counts the bases founded here still standing at the save (no engine write)
std::string BuildDiscoveredText(long long* guardN);      // T-1047 step 5 fold 1, MAIN THREAD (store.cpp, a save request): the outposts this player discovered in this save, as the file beside it holds them (baseshow kDiscoveredFile); *guardN: the save take-outs noted so far
int BuildDiscoveredGuard(long long guardN0, std::string* text);   // T-1047 step 5 fold 1, MAIN THREAD (store.cpp, the save landed): 1 = a save take-out was noted since guardN0 - *text the set as it noted it
void BuildDiscoveredLoaded(const std::string& text, int have);   // T-1047 step 5 fold 1, MAIN THREAD (store.cpp, a load's prepare): the loading save's file (have 0: none of that save) - the new world's discovered outposts
void BuildDiscoveredLoadEnded(long genAtPrepare);   // T-1047 step 5 fold 2, MAIN THREAD (store.cpp, the end of the engine's load call): a load that made no new world is disarmed
std::string BuildKeptListCommand();                  // KEPT, TEST-ONLY, MAIN THREAD, read-only: `keptlist` - the kept rows this game holds, their state, claims and the key search here
std::string StandInCommand(const std::string& arg);     // TEST-ONLY, MAIN THREAD: `standin <slot> [read]` - make (or take into the table) / read back that player's stand-in and the loaded buildings it owns
std::string BuildGiveCommand(const std::string& arg);   // TEST-ONLY, MAIN THREAD: `buildgive <key-substring|nearest> <slot>` - give a loaded building to that player's faction; T-608 T1389: `buildgive counter=<full box key> <slot>` - one shop counter only
std::string BuyHouseCommand(const std::string& arg);   /* P18 fold 1: TEST LEVER `buyhouse <buildingKey|nearest> [cats=<n>]` / `buyhouse show <buildingKey|last>` */
int BuildRecordedOwnerSlot(const char* key);          // build1h, MAIN THREAD: the owner slot the build registry recorded for the piece at this P7n key (own rows: my slot); -1 none                       // MAIN THREAD, read-only: `buildlist` - the registry with live values
int  BuildMadeHere(const char* key, double* placedSince);   // MAIN THREAD: 1 = this game placed the piece at this key itself in the world now loaded and it is still its own; *placedSince = the area's "loaded since" stamp at the placement
int  BuildRowPendingFor(void* building);              // MAIN THREAD: another game's PLACE or STATE (a copy, or an owner change such as a purchase) for this piece still waits here - 1 yes, 0 no, -1 key unreadable
int  BuildHomeDropPod(void* platoon, void* building);   // MAIN THREAD, review MED 2: the group's home dropped when it names the building (Ownerships::removeOwnedObject, the engine purchase's first step) - 1 called, 0 no row / nothing, -1 faulted
int  BuildKeyPlayerPiece(const char* key);            // MAIN THREAD: the build records name a player as the owner of the piece at this P7n key (a live row of this world, an own row waiting to be restored, a copy-list entry) - 1 yes, 0 no, -1 off the main thread or no key
void ReportBuild();                                   // one [BUILD] REPORT line (the `report` fan-out)
std::string PowerDiagToken();   /* T-1455 diagnostics: `powerDiag[frozenNow,frozenQuietNow,verdictChanges,frozenQuiet]=..` - on the [POWER] REPORT and the [SAVE] hb line; the count walked on the main thread (off it: the last count) */
/* T-1372 (H-T1372-removal), ANY THREAD - worldgen.cpp's GameWorld::destroy detour, before the engine's destroy: the engine's own reason
   (and the Level Editor's delete around it) decides whether this game's own building ends for good; such a piece's row is marked
   removed at the next BuildTick as a finished dismantle's is (coopbuild::BuildEngineDestroyDecide).
   T-1372 review fold 3: two halves. BuildEngineDestroyNote reads the piece (key, state, its doors' keys) while it is whole and keeps
   them in a ticket; BuildEngineDestroyDone, after the engine's destroy, queues the event only when the destroy answered true. A
   building's destroy the engine POSTPONED (false: it queued the object while the world lock was busy, decomp_798f50 :72-76 - the
   object alone is kept, not its reason) keeps its for-good capture; the same object's later destroy that completes, whatever its
   reason, queues it. ticket: 0 = nothing captured. */
int  BuildEngineDestroyNote(void* obj, int justUnloaded, const char* reason);
void BuildEngineDestroyDone(void* obj, int ticket, int destroyed);
/* mmo8a (mmo8-buildings-read.md 5): the buildings this player owns, kept in the own-records store as pp.build (store.cpp writes it) */
void BuildOwnRows(std::vector<coopown::BuildRow>* rows, std::vector<std::string>* retired, int* skipped);   // MAIN THREAD: own rows (own, not a copy, not removed) as PLACE + STATE bytes; retired = own keys taken down here
int  BuildLedgerHandNow();                            // P14 fold 2 (D5), MAIN THREAD: 1 = pp.build handed to the writer (a parked ground-pickup hold ends)
int  BuildOwnDirty();                                 // MAIN THREAD: 0 nothing new, 1 progress (written after the 2 s gap), 2 placed / handed here / removed (written now)
void BuildOwnDirtyClear();                            // MAIN THREAD: store.cpp took the mark
void BuildOwnRetiredTaken(const std::vector<std::string>& taken);   // mmo8a2 (review-mmo8a M2), MAIN THREAD: the writer's record now drops these retired keys
void BuildOwnDirtyMark(int level);                    // MAIN THREAD: raise the mark (store.cpp, a held write asks again)
int  BuildRestoreQueue(const std::vector<coopown::BuildRow>& rows);   // MAIN THREAD, the own-records restore edge: each row -> the copy queue with the RESTORE flag; returns rows queued
void BuildRestoreEdgeDone();   // MAIN THREAD, after the restore edge (or a disarmed load): areas applied before it take their held rows, waiting makes are asked again
unsigned long BuildSaveAccepted();                    // P87 rf1, MAIN THREAD (store.cpp): the engine accepted a save request - its number (1, 2, ...)
void BuildSaveLanded(unsigned long reqNo, const char* name);   // P87 rf1, MAIN THREAD (store.cpp): that save landed - removed marks set before its request retire
void BuildCopyListFromBase(const std::vector<coopown::BuildRow>& rows, const char* why);   // P87 rf1, MAIN THREAD (store.cpp): this world's copy list from the pp.build base
std::string BuildRestoreText();                       // the own[buildRestore ...] token of the [OWNSAVE] REPORT
/* help1 fold (review MED 4): store.cpp OwnBuildWrite handed pp.build to the writer as record `seq` (a skipped same record: the seq of the
   last one handed, 0 = none this store) - a confirmation captured into it goes to the helper once that seq has landed */
void BuildOwnWriteHanded(unsigned long long seq);     // MAIN THREAD
/* help1 fold (review MED 5/6): pp.help - the helper's given / next-seq counts, in the own-records store (store.cpp reads and writes it) */
void BuildHelpRecLoaded(const std::vector<coopown::HelpRecRow>& rows);   // MAIN THREAD: the record read for this store + world (none = empty)
void BuildHelpRecUnread();                            // MAIN THREAD: the record exists and cannot be read - refunds wait (never twice)
void BuildHelpRecRows(std::vector<coopown::HelpRecRow>* rows);   // MAIN THREAD: the rows to write
int  BuildHelpRecDirty();                             // MAIN THREAD: 0 nothing new, 1 a seq moved (batched, 5 s), 2 items handed back (now)
void BuildHelpRecDirtyClear();                        // MAIN THREAD: store.cpp took the mark
void BuildRefundLanded(void* who, const char* gkey, int qty);             // P14 fold 1, MAIN THREAD (items.cpp GrOnDropFound): an own piece's refund item landed at gkey - the escape ledger's refund list
int  BuildRefundTaken(const char* gkey, int qty, void* picker, int peer);   // P14 fold 1, MAIN THREAD (items.cpp): qty taken from the ground at gkey - by our picker (peer 0) or the other player (peer 1)
int  BuildRefundHanded(const char* gkey, int qty);   // P14 fold 5, MAIN THREAD (items.cpp GroundNoteConfirm): qty of our PUT at gkey applied by the area's holder - on its ground, never re-owed at reload
int  BuildGroundSource(void* b);                      // P3, MAIN THREAD (ground5 fold 1: LocalPlayerFaction walks the faction list): coopground::kGroundSrc* of a building's spill / refund
int  BuildPieceRecordOwner(const std::string& id, int* removed);   // T-1443 fold 3, MAIN THREAD: the owner slot a piece record held here names; -2 none; *removed = a removal mark
long BuildPieceRecordsGen();   // T-1443 fold 3, MAIN THREAD: moves at every change of the piece record table
void PieceRecordArrive(const std::string& id, const std::vector<char>& bytes, unsigned long long seq, long long writtenAt);   // MAIN THREAD (store.cpp): a piece record from the world server - kept per id with the seq it arrived with (T-1429 machine age; 0 not known) and its writer's stamp (machine age fold 1), never applied
void PieceRecordGone(const std::string& id, const char* from);   // MAIN THREAD (store.cpp): a piece record's removal - its kept copy is forgotten
void PieceRecordEcho(const std::string& id, int hasStamp, long long stamp, unsigned long long seq);   // MAIN THREAD (store.cpp RECORD_SEQ): this game's piece write sent with that stamp is on the world server's disk, with that seq (T-1429)
int  PieceRecLeaveFlush();   // T-1429 machine age (B), MAIN THREAD (store.cpp OwnLeaveSaveBegin): every changed piece of a loaded area marked as an unload marks it; the count
int  PieceRecLeavePending(int* confirmed, int* held);   // T-1429 (B), MAIN THREAD (the leave save's drain): flushed pieces whose writes are not stored yet; *confirmed stored, *held not this game's to write
void PieceRecSaveFlush();    // T-1429 machine age, MAIN THREAD (store.cpp detour_saveGame): pending LIVE and door values marked before a plain save
void PieceRefuseArrive(const std::vector<char>& payload);   // MAIN THREAD (store.cpp): PIECE_REFUSE (79) - counted, the piece marked again or its mark dropped
/* T-1333 (src/common/bldgrec.h): ONE TOWN BUILDING AS ONE WORLD-SERVER RECORD - a town wall's ruin flag and health, written by the
   holder of its area from the engine's own hit / ruin / repair, laid on every game through the engine's own setDestroyed and the
   construction state. MAIN THREAD, each. */
void TownBldgRecordArrive(const std::string& id, const std::vector<char>& bytes);   /* store.cpp: a building record from the world server - kept, laid when its area is laid */
void TownBldgRecordGone(const std::string& id);   /* store.cpp: a building record removed on the world server - forgotten */
void TownBldgRecordEcho(const std::string& id, int hasStamp, long long stamp);   /* store.cpp: RECORD_SEQ - this game's write was stored */
void TownBldgRefuseArrive(const std::string& id, int reason, unsigned int storedGen, unsigned int sentGen);   /* items.cpp: BOX_REFUSE for a building record */
void TownBldgMapEvent();   /* store.cpp: an area map arrived - queued removals tried against the holders it names */
void TownBldgAreaRebuilt(int c);   /* townver.cpp: area c's town was rebuilt for a new version - its records are never laid; the holder removes them */
void TbAreaLay(int c);   /* PieceRecApplyTick: area c's record list is complete - its kept building records are laid */
void TbAreaLeft(int c);   /* PieceRecApplyTick: area c left - laid again at its next load */
void TbLinkReset();   /* PieceRecApplyTick: a new link - every area laid again */
void TbTick();   /* BuildTick: lays that waited for the engine's writes */
int TbAreaLaidAnyThread(int c);   /* T-1333 fold 1, ANY THREAD (the wall hit hook): 1 = area c's building records are laid here (a byte read) */
void TbRepairAnswer(const std::string& key, int door, int repair, int verdict);   /* T-1333 fold 1 D: the holder's answer to this game's town ask (BdHitAnswer) */
void TownBldgReport();   /* the REPORT's [TOWNBLDG] line */
void BuildHelpQuitFlush();                            // MAIN THREAD, process exit: the batched owed-file save now (review MED 8)
}
