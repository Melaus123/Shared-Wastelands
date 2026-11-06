#pragma once
// settings1 S1 (docs/design-settings1.md s5 "S1"): look-only logging of the world-settings blocks and the mod fingerprint.
#include <string>
#include <vector>    /* mp5 */
#include <utility>
#include "../common/modlist.h"   /* settings5 S5 */
namespace coop {
void SettingsTick();                 // MAIN THREAD, every frame: [SETTINGS]/[MODS] lines at the link-up edge and each world-live edge
std::string SettingsDumpCommand();   // the `settingsdump` verb: the S1 lines plus the S2 world-record line, on demand (read-only); returns the status text
// settings2 S2 (docs/design-settings1.md sections 2, 3.1, 3.2; user 2026-09-26: the host may change them later).
void InstallSettings();                                  // preload: the GameplayOptions::load 0x3EEDE0 post-hook
void SettingsWorldOptionsBegin();                        // MAIN THREAD, store.cpp's OPTIONS handler: a map starts
void SettingsWorldOption(const std::string& key, const std::string& value);   // ... one gp.* row of it
void SettingsWorldValue(const std::string& key, const std::string& value);    // ... every row of it: the world's values the HOST screen's waiting rows are checked against
void SettingsWorldOptionsEnd(bool complete);             // ... the map ended: a non-authority applies it; the authority only records absent keys (never overwrites its live values); an @refused reply keeps the old record
std::string SettingsAdvSetCommand(const std::string& key, const std::string& value);   // the TEST-ONLY `advset` verb
std::string SettingsGtSetCommand(const std::string& key, const std::string& value);    // settings4 S4: the TEST-ONLY `gtset` verb (gt.* twin of advset)
// settings5 S5 (design section 4): this game's mod list, the S1 fingerprint's rows, for the store HELLO. MAIN THREAD. Returns out->known.
int SettingsModList(coopmods::ModList* out);
// mp5 (docs/design-mpmenu1.md section 5): the Game options screen. MAIN THREAD. The rows the host CHANGED there, as
// (notebook key, value); replaces any earlier hand-over (empty = withdraw). Sent through the authority record path the
// moment this game is the world's authority with the record in hand, and written over the host's live values at its
// load (after the S2 apply) and world-live edge. Returns 1 when sent now, 0 when kept for later.
int SettingsHostChoices(const std::vector<std::pair<std::string, std::string> >& kv);
// GAME OPTIONS' DONE in a running world: an admin's changed rows to the world server NOW as one OPTIONS message, or dropped (logged) -
// never held for a later send, never written over this game's live values, the HOST screen's waiting rows untouched. 1 = sent.
int SettingsWorldChange(const std::vector<std::pair<std::string, std::string> >& kv);
// Moves each time an OPTIONS map from the world server starts (SettingsWorldOptionsBegin): GAME OPTIONS in a world reads the rules again.
long SettingsWorldValuesGen();
// The HOST screen's rows the world (worldFolder) has STORED since the last call - an OPTIONS map from it held them at those values -
// handed out once (stewardrules.h TakeStored). Only these count as rows that already went. MAIN THREAD. Returns how many.
int SettingsHostRowsStored(const std::string& worldFolder, std::vector<std::pair<std::string, std::string> >* out);
// mp5: this game's own live value of one gp.* / gt.* key as the notebook would store it ("" = unreadable). MAIN THREAD.
std::string SettingsLiveValue(const std::string& key);
// The world's value of one key as this game last had it from that world's OPTIONS map ("" = not known: another world, or no such row). MAIN THREAD.
std::string SettingsWorldValueOf(const std::string& worldFolder, const std::string& key);
// ui.cpp's HOST / JOIN press, once armed: worldFolder = the folder of the world this HOST press joined while it ran (no world server
// started), "" = any other press. As the rule-setter in that world, this game keeps the world's record (stewardrules.h). MAIN THREAD.
void SettingsHostJoinedRunning(const std::string& worldFolder);
// The HOST press is this world's first hosting: worldFolder = the folder its world server starts for, holding neither
// rules.txt nor options.txt; "" for any other press. Only then do this game's own values become the world's rules. MAIN THREAD.
void SettingsHostFirstWorld(const std::string& worldFolder);
// This game sets the world's rules and keeps the world's record (every hosting but a world's first). MAIN THREAD.
bool SettingsKeepsRecord();
// This game's last HOST press is its world's first hosting (SettingsHostFirstWorld). MAIN THREAD.
bool SettingsFirstHosting();
}
