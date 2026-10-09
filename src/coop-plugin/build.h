// build.h - build1-a/b (build1-b: an own-faction placement is sent as MSG_BUILD PLACE and copied as coop-peer on the other game)
// build1-a (docs/design-build1.md sections 2-3): OBSERVE construction on this game, plus a test lever.
// Nothing crosses between games in this effort. The four observe hooks (probe P082, .modding/04-probes.md) count and
// log; the registry `buildreg` (position key -> record sid, owner, progress) is filled by the placement hook and by
// the lever's own placement, never by walking zones.
#pragma once
#include <string>
#include <vector>
namespace coopbuild { struct BuildMsg; }   /* build1-b: src/common/buildwire.h */
namespace coopown { struct BuildRow; struct HelpRecRow; }     /* mmo8a: src/common/ownrec.h (pp.build); help1 fold: pp.help */
namespace coop {
void InstallBuild();                                  // preload: the observe hooks (address table names below)
void BuildTick();                                     // MAIN THREAD, every frame: drains the detours' POD queue into the log and the registry
std::string BuildTestArm(const std::string& arg);     // MAIN THREAD: `buildtest place <sid> [dx dz] | progress <key-substring> <amount> | dismantle <key-substring> | into <near|host-key-substring> <sid> [indoors]`
void BuildCopyDrain();                                // build1-b, K2 safe point next to BuildTestDrain: pending PLACEs -> coop-peer copies
void BuildNoteRecv(const coopbuild::BuildMsg& m, unsigned int fromPeer);   // build1-b/c/d, MAIN THREAD (session dispatch): a decoded PLACE, STATE or REMOVE (build1-d), queued
void BuildForgetWorld();                              // build1-c (review-build1b M2): world teardown - pending PLACEs / STATEs and the copy rows
void BuildNoteBad();                                  // build1-b: a MSG_BUILD that did not decode
void BuildTestDrain();                                // K2 safe point (combat.cpp detour_tsRagdollUpdates): runs the armed request
std::string BuildListCommand();
std::string StandInCommand(const std::string& arg);     // TEST-ONLY, MAIN THREAD: `standin <slot> [read]` - make (or take into the table) / read back that player's stand-in and the loaded buildings it owns
std::string BuildGiveCommand(const std::string& arg);   // TEST-ONLY, MAIN THREAD: `buildgive <key-substring|nearest> <slot>` - give a loaded building to that player's faction
std::string BuyHouseCommand(const std::string& arg);   /* P18 fold 1: TEST LEVER `buyhouse <buildingKey|nearest> [cats=<n>]` / `buyhouse show <buildingKey|last>` */
int BuildRecordedOwnerSlot(const char* key);          // build1h, MAIN THREAD: the owner slot the build registry recorded for the piece at this P7n key (own rows: my slot); -1 none                       // MAIN THREAD, read-only: `buildlist` - the registry with live values
int  BuildRowPendingFor(void* building);              // MAIN THREAD: another game's PLACE or STATE (a copy, or an owner change such as a purchase) for this piece still waits here - 1 yes, 0 no, -1 key unreadable
void ReportBuild();                                   // one [BUILD] REPORT line (the `report` fan-out)
/* mmo8a (mmo8-buildings-read.md 5): the buildings this player owns, kept in the own-records store as pp.build (store.cpp writes it) */
void BuildOwnRows(std::vector<coopown::BuildRow>* rows, std::vector<std::string>* retired, int* skipped);   // MAIN THREAD: own rows (own, not a copy, not removed) as PLACE + STATE bytes; retired = own keys taken down here
int  BuildLedgerHandNow();                            // P14 fold 2 (D5), MAIN THREAD: 1 = pp.build handed to the writer (a parked ground-pickup hold ends)
int  BuildOwnDirty();                                 // MAIN THREAD: 0 nothing new, 1 progress (written after the 2 s gap), 2 placed / handed here / removed (written now)
void BuildOwnDirtyClear();                            // MAIN THREAD: store.cpp took the mark
void BuildOwnRetiredTaken(const std::vector<std::string>& taken);   // mmo8a2 (review-mmo8a M2), MAIN THREAD: the writer's record now drops these retired keys
void BuildOwnDirtyMark(int level);                    // MAIN THREAD: raise the mark (store.cpp, a held write asks again)
int  BuildRestoreQueue(const std::vector<coopown::BuildRow>& rows);   // MAIN THREAD, the own-records restore edge: each row -> the copy queue with the RESTORE flag; returns rows queued
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
void BuildHelpQuitFlush();                            // MAIN THREAD, process exit: the batched owed-file save now (review MED 8)
}
