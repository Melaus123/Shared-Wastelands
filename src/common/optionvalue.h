/* optionvalue.h - THE WORLD SERVER'S CHECK OF ONE RULE: a key, and a value as the world server stores it. Pure: no I/O, no globals.
   One copy, compiled into the world server (store_main.cpp OptionValueOk: every OPTIONS row a game sends, every rules.txt and
   options.txt line it reads), the host's game (ui.cpp PanelStartNotebook: the rows a world's first hosting writes into rules.txt
   before the world server starts - T-1202, rulesfile.h FirstStartRows) and the offline suite, so the three cannot hold different
   ideas of what a world accepts. The body is store_main.cpp OptionValueOk's, moved here unchanged (T-1202 review fold). */
#pragma once

#include <string>
#include "clockmath.h"      /* coopclock::TimeModeOfWire - clockmath.cpp is compiled into all three */
#include "gpoptions.h"      /* coopgp::GpValueOk / GtValueOk */
#include "recruitmult.h"    /* coopr::RecruitMultValueOk */
#include "profiles.h"       /* coopprof::CapValueOk */
#include "resurrectfee.h"   /* swfee::IsOptionKey / OptionValueOk */
#include "kapirules.h"      /* kapi::IsOptionKey */
#include "spimport.h"       /* spimport::IsOptionKey / OptionValueOk */

namespace worldopt {

// The legal values for each key, and the value a world that has never been told gets. A game must never have to
// guess: the store ALWAYS has a basepolicy row after LoadOptions, so "no answer" on the game side means "no
// notebook process", not "the notebook is quiet".
inline bool ValueOk(const std::string& key, const std::string& value)
{
    if (key == "basepolicy") return value == "shared" || value == "owner" || value == "locked";
    /* E40 / decision 45 - THE PACE IS A HOSTING OPTION, and it lives here with the base policy for the same
       reason: it is a rule about the WORLD, so it has to outlive whichever game happens to be running. */
    if (key == "timemode") return coopclock::TimeModeOfWire(value.c_str()) >= 0;   /* fixed, consensus, vote, locked */
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
    /* T-556 (owner 485, 491, 497): HOST options - resurrection on/off (absent = off), the fee amount (absent = 1000) and its growth
       steady / steep (absent = steep); resurrectfee.h holds the legal values. */
    if (swfee::IsOptionKey(key)) return swfee::OptionValueOk(key, value);
    /* per-race rules (T-1209, owner 1414 d): never stored here - they come from the world's own kenshi_api.cfg (worldrace.h). A race
       line in rules.txt or options.txt is recognized only so it is taken out (RulesParseInto, RaceInRulesDrop); OnOptions refuses a
       game's race row before this is asked. */
    if (kapi::IsOptionKey(key)) return true;
    /* T-1096 (owner 1224): HOST options - who may start a profile from a single-player save (all / owner / off, absent = all; the old "host" is kept as owner) and
       whether its money and research come along (yes / no, absent = yes); spimport.h holds the legal values. */
    if (spimport::IsOptionKey(key)) return spimport::OptionValueOk(key, value);
    if (key.compare(0, 3, "gp.") == 0) return coopgp::GpValueOk(key, value, 0);
    if (key.compare(0, 3, "gt.") == 0) return coopgp::GtValueOk(key, value);
    return false;
}

} // namespace worldopt
