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
    long long amount, price, money;
    std::vector<std::string> alive;
    std::vector<swtab::Row> rows;
    FallenTabRead() : on(0), growth(0), moneyRead(0), amount(0), price(0), money(0) {}
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
