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
// mp5: this game's own live value of one gp.* / gt.* key as the notebook would store it ("" = unreadable). MAIN THREAD.
std::string SettingsLiveValue(const std::string& key);
}
