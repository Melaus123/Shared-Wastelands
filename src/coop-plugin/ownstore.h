// ownstore.h - mmo1 (e47-mmo-design.md sections 2 and 7 item 1): YOUR OWN SQUADS, WRITTEN CONTINUOUSLY AS SMALL RECORDS.
//
// Two halves, one interface:
//   * THE DISK HALF (ownstore.cpp): the records folder, ONE FILE PER SQUAD, a writer thread fed by a queue guarded by a
//     CRITICAL_SECTION, the atomic write (temp file -> flush -> rename over the old one, the old one kept as .rec.prev;
//     never truncated in place), the read-back CRC check, and the index line that is the commit point. It makes NO
//     engine call and reads no engine memory, so it may run off the main thread.
//   * THE ENGINE HALF (store.cpp, beside the sleep path): the walk of this game's own squads, the side-effect-free
//     serialise into a container this plugin owns (never the squad's own, never 0x372DB0), the pacing, the dirty marks,
//     the TEST verb and the [OWNSAVE] report. MAIN THREAD ONLY.
//
// T-251: the writer ships ON; each multiplayer world opens the player's own store by itself; `ownsave <name>` picks a test store (<data folder>\players\<name>\, or <save root>\Shared Wastelands\players\<name>\ without a data folder), `ownsave on|off`
// switches it, `ownsave poke` (= `flush`) writes every owned squad now, ignoring the change check. A fault in any engine
// call switches it off for the process (counted, logged once). Loading from records is mmo2 - not here.
#pragma once

#include <string>
#include <vector>
#include "../common/restoreguard.h"   /* restore1c: the repair rows */

namespace coopown { struct LedgerEntry; }   /* mmo6: ownrec.h */

namespace coop {

/* ---- the disk half (ownstore.cpp) ---- */
struct OwnStoreStat
{
    long long committed, writeFail, verifyFail, indexFail, replacedQueued, bytesCommitted, diskUsMax, diskUsSum, queued;
    long long rotated, rotateSkipped, tmpDeleted, tmpKept;   /* mmo1b: .prev rotations made / refused (live not the indexed version); leftover *.tmp at open */
    int       busy;
};
/* MAIN THREAD. Makes the folder (and its parents), reads the index (the highest seq is returned in *maxSeq, unparsable
   lines in *badLines) and starts the writer thread once. Refuses (false) while a previous folder still has queued work. */
bool OwnStoreOpen(const std::string& dir, unsigned long long* maxSeq, int* badLines);
/* owner 429: the store folder's format number - read only (swformat::FormatDecide's verdict; *found = its number, 0 = no file); and
   the conversion of an open store to this build's number, format.txt written last. MAIN THREAD. */
int  OwnStoreFormatCheck(const std::string& dir, unsigned int* found, std::string* why = 0);   /* why: an unreadable file's reason */
bool ConvertRecordsFolder(const std::string& dir, unsigned int from);
/* MAIN THREAD. Hands one squad's bytes to the writer. A payload already queued for the same key is REPLACED (a burst is
   one write). crc/len are what the read-back must reproduce. */
void OwnStoreEnqueue(const std::string& key, const std::vector<char>& bytes, unsigned int crc, unsigned long long seq,
                     long long writtenAt, int chars);
void OwnStoreStats(OwnStoreStat* out);
std::string OwnStoreDir();
/* mmo1b (review-mmo1 item 4). MAIN THREAD. What a fresh record is compared against: the job still queued for this key if
   there is one (it is what lands next), else the last COMMITTED record (the index at open, then each successful commit,
   set by the writer thread under the queue lock). false = nothing to compare against. */
bool OwnStoreLastFor(const std::string& key, long long* len, unsigned int* crc);
/* mmo1b (review-mmo1 item 1). MAIN THREAD. The writer thread never logs (the logger is not thread-safe); it records the
   first failure of each kind as plain data and this writes the line. */
void OwnStoreDrainFailLog();
/* mmo2 fold (review-mmo2 item 5). MAIN THREAD. The world key and profile id the writer puts in own.index's first line
   ("oh1", ownrec.h) at every index rewrite. An empty world writes no header (the overlay then refuses the store). */
void OwnStoreSetIdentity(const std::string& world, const std::string& profile);

/* ---- the engine half (store.cpp) ---- */
void OwnTick();                                   /* MAIN THREAD, every frame, from StoreTick */
int  OwnSaveCommand(const std::string& arg, std::string* status);   /* MAIN THREAD, the `ownsave` TEST verb */
void OwnSaveReport();                             /* MAIN THREAD, the [OWNSAVE] line on `report` */
void OwnNoteCharChange(void* character);          /* ANY THREAD: an item moved in/out of this character's inventory */
int  OwnSquadKeyOfCharNow(void* character, std::string* key);   /* P14 fold 1, MAIN THREAD: the squad record key holding the character; that squad marked write-now */
unsigned long long OwnSeqNow();                    /* P14 fold 1, MAIN THREAD: the last seq handed out (any record) */
int  OwnBuildWriteNow(const char* why);           /* P14 fold 1, MAIN THREAD: pp.build's dirty rows handed to the writer NOW (1 = queued) */
unsigned long long OwnOverlayAppliedSeq(const std::string& key);   /* P14 fold 1, MAIN THREAD: the seq of the record this load's overlay APPLIED for key (0 none) */
void OwnMarkCharDirtyNow(void* character);       /* MAIN THREAD: a cross-player hand-over completed - ONLY that character's squad (mmo1b) */
bool OwnWriterOn();                               /* ANY THREAD: the writer is on and not faulted - callers skip their lookups otherwise (mmo1b) */

/* ---- mmo2: the save's own mark and the load overlay (store.cpp) ---- */
void OwnMarkNoteSaveRequest(void* saveMgr, const void* nameStr, int before, int after);   /* MAIN THREAD: SaveManager::save, after orig */
void OwnMarkSaveTick();                           /* MAIN THREAD, every frame: multiplayer.mark once the engine has finished the save */
void BuildSaveTrackNote(void* saveMgr, const void* nameStr, int before, int after);   /* P87 rf1, MAIN THREAD: SaveManager::save, after orig */
void BuildSaveTrackTick();                        /* P87 rf1, MAIN THREAD, every frame (from OwnMarkSaveTick): a tracked save landed -> BuildSaveLanded */
void OwnBuildCopyListFeed();                      /* P87 rf1, MAIN THREAD: the copy list from the pp.build base when the load edge does not hand it */
void OwnOverlayPrepare(const void* folderStr, const void* nameStr);   /* MAIN THREAD: loadGame, before orig - the overlay set + roster check */
/* ANY THREAD (detour_loadFromDisk): 0..n = this container's file was swapped to its own record (put it back with
   OwnOverlayDone after the engine's load); -1 = not an overlaid squad. */
int  OwnOverlayClaim(void* container, unsigned char force, std::string** name, std::string* was);
void OwnOverlayDone(int slot, std::string* name, const std::string& was, unsigned char loaded);
void OwnOverlayDrainLog();                        /* MAIN THREAD: the LOAD-OVERRIDE own lines, and the window's close */

/* ---- mmo4: the one engine save of the profile's own slot at leave / Exit, and the timed autosave (store.cpp) ---- */
int   OwnEdgeSaveRunning();   /* inv7c fold 2: MAIN THREAD - a leave / quit / host-left save is running (g_ownLs not idle) */
int   OwnLeaveSaveForLeave();                     /* MAIN THREAD, the `leave` verb: 1 = a leave save runs and the tick leaves the session at its completion edge; 0 = leave now */
bool  OwnLeaveSaveHoldQuit(void* sender);                     /* MAIN THREAD, EXIT GAME's accept path before orig: true = the quit is held until the leave save completes or gives up */
void  OwnLeaveSaveTick();                         /* MAIN THREAD, every frame from OwnTick */
int   OwnAutosaveSkip(const void* nameStr, char isAutosave);   /* SaveManager::save before orig: 1 = the engine's timed autosave is skipped */
void* OwnSaveManagerPtr();                        /* command_channel.cpp: SaveManager::getSingleton() */
void  OwnLeaveSaveCancelQuit(int result, void* sender);         /* MAIN THREAD, the confirm's accept path with result != 2: a held quit is not resumed (fold item 4) */
/* ---- mmo5: when the host leaves (store.cpp) ---- */
void  OwnHostLeftTick();                          /* MAIN THREAD, every frame from OwnTick: a joiner's host-left edge, pause, leave save, window */
extern long long g_cmdVerbsRun;                   /* command_channel.cpp: verbs run in this process (> 0 = harness-driven) */
unsigned long long OwnStoreOldestPendingSeq();    /* ANY THREAD: the lowest seq queued or being written, 0 = none (fold item 7) */

/* ---- mmo3: money, research, map knowledge and the player faction row (store.cpp) ---- */
void OwnInstallMmo3(unsigned long long base);    /* InstallStore: the Research::setResearched 0x833680 detour */
void OwnNoteFactionChange();                      /* ANY THREAD: a detoured relation writer touched a pair with this game's player faction */
bool OwnBuildTick(unsigned long now);             /* mmo8a, MAIN THREAD, from OwnTick: pp.build when the build registry marked it (true = it ran) */
void OwnBuildTeardownFlush();                     /* mmo8a3 (D1), MAIN THREAD, world teardown: pp.build's dirty rows to the writer before the build registry's generation bump */
bool OwnHelpTick(unsigned long now);              /* help1 fold (review MED 5/6), MAIN THREAD, from OwnTick: pp.help read for this store + world, written when build.cpp marked it */
void OwnHelpTeardownFlush();                      /* help1 fold, MAIN THREAD, world teardown: pp.help's marked rows to the writer before build.cpp forgets them */
void OwnHelpQuitFlush();                          /* help1 fold, MAIN THREAD, process exit: the same, before the exit waits for the writer */
bool OwnCatTick(unsigned long now);               /* MAIN THREAD, from OwnTick: at most one category check a frame (true = it ran) */
void OwnCatForget();                              /* MAIN THREAD: a world reset - every category is checked again at once */
void OwnRestoreTick();                            /* MAIN THREAD, InQueueDrain: the restore at the inv7a edge (once per load) */
int  OwnPokeMoney(int money, std::string* status);   /* MAIN THREAD, `ownsave poke money <n>` (TEST, decision M7) */
std::string OwnMmo3ReportText();                  /* MAIN THREAD, the report's mmo3 group */

/* ---- mmo6: a cross-player hand-over is two record writes, with a ledger (e47-mmo-design.md 1.3, 7 item 6) ---- */
bool OwnStoreCommittedSeq(const std::string& key, unsigned long long* seq);   /* MAIN THREAD: the seq of key's last COMMITTED record */
/* MAIN THREAD: pp.ledger in the store folder - read, `line` appended, the last `cap` kept, written to a temp file, flushed and
   renamed over the old one (never in place). *linesNow = the lines the file now holds. false = no store or the write failed. */
bool OwnLedgerAppend(const std::string& line, unsigned int cap, int* linesNow);
/* inv7c's READ HELPER. MAIN THREAD: every line of the current store's pp.ledger that parses, oldest first; *badLines the rest.
   false = no store folder or no ledger file. */
bool OwnLedgerRead(std::vector<coopown::LedgerEntry>* out, int* badLines);
/* MAIN THREAD (items.cpp, a completed hand-over, BEFORE OwnMarkCharDirtyNow): one ledger line - only while the writer is on. */
/* mmo6 fold: dirIn / role take these (equal to coopown::kLedgerOut/In/Rev and kLedgerRoleRequester/Owner, ownrec.h) */
const int kHandOut = 0, kHandIn = 1, kHandRev = 2, kHandRequester = 1, kHandOwner = 2;
void OwnHandOverLedger(unsigned int uid, const char* sid, int qty, int dirIn, unsigned int msgId, int peerSlot, int role);

/* ---- restore1b1: the records high-water and the checkpoints (restoreguard.h) ---- */
unsigned long long OwnStoreCommittedHigh();   /* ANY THREAD: the highest COMMITTED own seq of the open store (the index's at open); 0 = none */
/* restore1b1 fold (review item 4): A CHECKPOINT IS A JOB FOR THE RECORDS WRITER THREAD, run between two records (the thread takes
   no record while it copies). It copies the records folder's files (own.index first, no *.tmp, no sub-folder) into
   <store>\checkpoint\<world>\<tag>\ through <tag>.part and a rename, then prunes that world's folder (restoreguard::PruneSelect)
   to the Recycle Bin - only when the drive's bin can take every selected folder (review item 6). MAIN THREAD: queue one (at most
   8 waiting; false = no store open or the queue is full). */
bool OwnStoreCheckpointRequest(const std::string& tag, const std::string& world, unsigned int cap);
/* One job's result, for the main thread to log and count. rc: 1 made, 0 already there, -1 no store, -3 a copy / folder / rename
   failed (the .part goes to the Recycle Bin when it can). pruneNoBin = folders the prune selected and left, the bin could not take them. */
struct OwnCpResult
{
    std::string tag, world; int rc; long long bytes; int pruned, pruneFailed, pruneNoBin;
    OwnCpResult() : rc(0), bytes(0), pruned(0), pruneFailed(0), pruneNoBin(0) {}
};
bool OwnStoreCheckpointSettled();                 /* fold 2: ANY THREAD - no checkpoint job waiting and the writer idle */
long long OwnStoreCheckpointNotesDropped();       /* fold 2: results lost because 32 were already waiting */
void OwnStoreCheckpointResults(std::vector<OwnCpResult>* out);   /* MAIN THREAD: the finished jobs' results since the last call (appended) */

/* ---- restore1c (design s4): THE REPAIR on this game ---- */
bool OwnStoreIdleNow();                           /* ANY THREAD: no record or checkpoint job waiting and the writer idle */
void OwnStoreSetStamp(unsigned int epoch, unsigned long long nbSeq, bool known);   /* MAIN THREAD: stamped on each record queued from now (o2) */
/* rc: 1 applied, 0 nothing to apply, -1 no records store, -2 the writer is busy, -3 the Recycle Bin cannot take the files, -4 a file
   step failed (said by the caller). recycled = records whose post-R files went to the Recycle Bin (fromCheckpoint + dropped). */
struct OwnRepairResult
{
    int rc, applied, recycled, fromCheckpoint, dropped, copyFailed, ledgerFromCheckpoint, ledgerRecycled; unsigned int epoch; unsigned long long maxSeq;
    OwnRepairResult() : rc(0), applied(0), recycled(0), fromCheckpoint(0), dropped(0), copyFailed(0), ledgerFromCheckpoint(0), ledgerRecycled(0), epoch(0), maxSeq(0ULL) {}
};
void OwnStoreRepairApply(const std::vector<restoreguard::RepairRow>& rows, const std::string& world, OwnRepairResult* out);   /* MAIN THREAD, writer idle */
/* store.cpp */
int  RepairWorldCommand(const std::string& name, std::string* status);   /* MAIN THREAD, the `repairworld` TEST verb: 0 again, 1 done, 2 load now */
void RepairOnNotebook(const std::vector<char>& payload);                 /* MAIN THREAD: REPAIR down */
void RepairOnWelcome(const std::vector<char>& payload);                  /* MAIN THREAD: the WELCOME's repair rows */
int  OwnRepairGate(const std::string& name);                             /* MAIN THREAD, the load gate: 1 = refuse the load */
std::string OwnAutoStoreWant();                                          /* T-490: MAIN THREAD - the player's records store name for this link's world */
void OwnAutoStoreBeforeLoad();                                           /* T-251 fold 2: MAIN THREAD, the load gate and loadGame - the player's store opened before the load */
std::string RepairReportToken();

}   // namespace coop
