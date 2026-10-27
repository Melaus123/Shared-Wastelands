// resurrect.h - T-556: the fallen list (a snapshot of each of this player's own characters at its death) and the TEST-ONLY
// bring-back lever. The list's shape and its decisions: src/common/fallenwire.h.
#pragma once

#include "../common/fallentab.h"   /* swtab::Row - the FALLEN tab's rows */
#include <string>
#include <vector>

namespace coop {

// ANY THREAD (declareDead runs on the main thread and off it). Called by the declareDead detour after the engine's own call
// for a death that was let through. Notes the uid of a character this game drives and how the call was reached; the
// snapshot is taken by ResurrectTick on the main thread. No allocation, no lock, no log.
void ResurrectNoteDeath(void* character, unsigned long long callerRet, int unique);

// MAIN THREAD, every command-channel pump. Takes the snapshots noted since the last call and finishes a bring-back whose
// look waits for its body.
void ResurrectTick();

// ANY THREAD (T-1062): 1 while a death noted by ResurrectNoteDeath waits for its snapshot (the snapshot writes the person's
// left note, OwnLeftNoteDeath), else 0. Plain reads of the ring.
int ResurrectDeathsWaiting();

// MAIN THREAD (T-1062, the records report line): the deaths whose left note was never asked for, by why - the death queue full
// (queueFull), dropped while a world loaded or closed (blocked), the character gone before its snapshot (gone), the snapshot's
// read faulted (fault, the note may or may not be written); then leftRepeat, a death of a uid already snapshotted (its note was
// written then, so none is missed). These count deaths of any faction this game drives: the faction is read only by the snapshot.
// ",leftMissed[...],leftRepeat=n".
std::string ResurrectLeftMissWords();

// MAIN THREAD (the command channel). TEST-ONLY lever: `resurrect list` | `resurrect <n> [squad <i> | beside <uid>]` |
// `resurrect killlast` | `resurrect killother <uid>` | `resurrect host on|off` | `resurrect host fee <amount>` |
// `resurrect host growth steady|steep` | `resurrect price` | `resurrect money <n>` | `resurrect tab ...` (fallentab.h).
std::string ResurrectLever(const std::string& args);

// THE FALLEN TAB's reads (fallentab.cpp), MAIN THREAD. on/amount/growth: the host options as this game holds them; alive: the
// names of this player's brought-back characters alive now (their count is A); price: what a bring-back costs now; money: this
// player's purse (moneyRead 0 = it does not read); rows: this game's copy of the list, newest first.
struct FallenTabRead
{
    int on, growth, moneyRead;
    int atLimit;                    /* swtab::AtCharacterLimit: 1 at the game's character limit, 0 below, -1 not judged */
    long long amount, price, money;
    std::vector<std::string> alive;
    std::vector<swtab::Row> rows;
    FallenTabRead() : on(0), growth(0), moneyRead(0), atLimit(-1), amount(0), price(0), money(0) {}
};
void ResurrectTabRead(FallenTabRead* out);
// The characters a fallen one may come back beside: this game's own living characters of this player's faction that the
// placement rules call free (fallenwire.h BesideVerdict: not knocked out, carried, caged, chained or enslaved), grouped by squad
// in the order each squad's first one is driven here. squad: the squad's name ("" unread).
struct FallenMate { unsigned int uid; std::string name, squad; int selected; FallenMate() : uid(0), selected(0) {} };   /* selected: 1 when the player has it selected in the game */
void ResurrectFreeMates(std::vector<FallenMate>* out);
// THE BRING-BACK, the road the TEST lever takes too: the row of dead character deadUid comes back beside the character
// besideUid. expectPrice >= 0: refused (kRefPriceChanged) when the price read now differs from it. ok = the character was made;
// else refusal = why (swtab::kRef*). name / mate: the row's and the squadmate's names; price, money: as read at the press;
// paid: what was taken.
struct BringBackOut
{
    int ok, refusal;
    std::string name, mate;
    long long price, money, paid;
    BringBackOut() : ok(0), refusal(0), price(0), money(0), paid(0) {}
};
BringBackOut ResurrectBringBackFor(unsigned int deadUid, unsigned int besideUid, long long expectPrice);
// THE CAMERA ROAD (src/common/fallentab.h CameraRoadOffered): 1 while this player has no living character (the people and
// animals of the player's faction this game drives) - the tab then brings a character back where the camera is looking; 0 with
// one alive (the squadmate road). MAIN THREAD.
int ResurrectCameraRoadOffered();
// The bring-back on the camera road: the row of deadUid comes back at the first safe spot of the ground point the camera is
// looking at and the rings round it, in a new squad of the player's faction; the camera is never moved. The same checks and
// result as ResurrectBringBackFor (mate stays empty). MAIN THREAD.
BringBackOut ResurrectBringBackHere(unsigned int deadUid, long long expectPrice);

// MAIN THREAD (store.cpp's OPTIONS drain): the host's resurrection options (src/common/resurrectfee.h). Begin before a map,
// ResurrectOption for each row (true = the key is one of resurrect / resurrectfee / resurrectgrowth, taken here), End after a
// map: complete = a whole map of the world's options was read, so a key it did not name goes back to its default.
void ResurrectOptionsMapBegin();
bool ResurrectOption(const std::string& key, const std::string& value);
void ResurrectOptionsMapEnd(bool complete);

// MAIN THREAD, at world teardown: the list, the noted deaths, the uids already snapshotted and the bring-backs in flight are
// all of the world being destroyed - all forgotten; the list is asked for again once a world is loaded.
void ResurrectWorldTeardown();

// MAIN THREAD: a FALLEN message from the world server (TABLE: this player's whole list) - this game's copy is replaced by it.
void ResurrectArrive(const std::vector<char>& payload);

// MAIN THREAD: this game's own save of its profile finished (store.cpp ProfileSavedTell) - the bring-backs made in this world
// since its last save are in it, so they are SAVED (final) on the world server.
void ResurrectSaveFinished(const std::string& saveName);

// MAIN THREAD: the world-server link went down - the list is the server's, so this game's copy goes until the next TABLE.
void ResurrectLinkLost();

// "[FALLEN] REPORT ..." - the counters.
std::string ResurrectReportLine();

} // namespace coop
