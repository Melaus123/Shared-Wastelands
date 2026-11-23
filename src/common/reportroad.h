/* src/common/reportroad.h - T-1457 (owner decision 1824, F1707): REPORT A BUG ON A GAME THE ADDRESS GATE SWITCHED OFF.
 *
 * When the mod is switched on in Kenshi's mod list but its address gate refuses the game (addresses.cpp AddrInit - one row that
 * does not fit switches the whole mod off), the player must still be able to send a bug report with the same REPORT A BUG buttons
 * (the title screen's and the pause menu's). That road needs a few engine addresses of its own; each is found by its own bytes in
 * addresses/signatures.sig (sigtable.h SigSubset + SigResolveAll: each EXACTLY ONCE and passing its own check), checked against
 * the running game, and only then is the report started - with the title hook (already there), a report-only pause-menu hook and
 * a report-only frame hook, and nothing else. A row that fails switches off only the report (the game stays plain Kenshi).
 *
 * This header is the road's pure part, shared by the plugin and the offline suite: which rows, the verdict over them, which steps
 * of a report run on a switched-off game, and which lines the TEST-ONLY reader of a switched-off test build takes. C++03. No OS
 * call, no global.
 */
#ifndef COOP_COMMON_REPORTROAD_H
#define COOP_COMMON_REPORTROAD_H

#include <string>

#include "addrtable.h"

namespace coopreport {

/* THE ROWS. feature 0 = REQUIRED: the report is off unless it is found exactly once and passes both checks. A non-0 feature makes
   the row optional for this road (only that feature is off). testOnly = asked only in the test build (SW_DEV_BUILD): the TEST-ONLY
   `load` of the switched-off game's reader (command_channel.cpp CommandChannelRefusedTick). */
struct ReportRow
{
    const char* name;
    const char* feature;
    int         testOnly;
};
inline const ReportRow* ReportRows(int* count)
{
    static const ReportRow t[] = {
        { "TitleScreen_update",        0,           0 },   /* the title hook: the title screen's REPORT A BUG (coop.cpp) */
        { "MainMenuPopup_show",        0,           0 },   /* the pause menu's show, hooked: its REPORT A BUG row (ui.cpp) */
        { "MainMenuPopup_get",         0,           0 },   /* the pause menu's getter: opened again after ESC over the window */
        { "GameWorld_gpuFrame",        0,           0 },   /* the in-world frame, hooked: the window over the pause menu */
        { "KenshiSaveRootText",        0,           0 },   /* optional by its own marker (crash-watch): Kenshi's crash zip folder */
        { "SaveManager_instancePtr",   "test-load", 1 },   /* TEST-ONLY: the switched-off test build's `load` */
        { "SaveManager_loadSave",      "test-load", 1 },
        { "SaveManager_anySavesExist", "test-load", 1 },
    };
    if (count != 0) *count = (int)(sizeof(t) / sizeof(t[0]));
    return t;
}

/* THE VERDICT over SigResolveAll's answer for the road's rows (sigtable.h SigSubset): `resolved` = its return (0 = refused: a
   required row matched 0 or 2+ times, failed its own check or a cross-check), `absentRequired` = required rows the file does not
   carry, e[0..n) its entries and live[0..n) each one's check against the running game (addresses.cpp VerifyEntry: 1 matched,
   -1 nothing to compare - a writable global; anything else did not match). 1 = the report may start; 0 = it stays off, *why
   (may be 0) names the first reason. An optional row (e[i].optional non-empty) never decides: a miss there is that feature's. */
inline int ReportRowsVerdict(int resolved, int absentRequired, const coopaddr::AddrEntry* e, const int* live, int n, std::string* why)
{
    if (absentRequired > 0) { if (why) *why = "a row the report needs is not in the signatures file"; return 0; }
    if (resolved <= 0 || e == 0 || n <= 0) { if (why) *why = "the signature search did not find every row the report needs exactly once"; return 0; }
    int i;
    for (i = 0; i < n; ++i)
    {
        if (e[i].optional[0] != 0) continue;
        if (e[i].missed != 0) { if (why) *why = std::string(e[i].name) + " was not found"; return 0; }
        if (live == 0 || (live[i] != 1 && live[i] != -1))
        { if (why) *why = std::string(e[i].name) + "'s bytes in the running game are not the ones its signature found"; return 0; }
    }
    return 1;
}

/* THE STEPS OF A REPORT (bugreport.cpp FillJob / StartJob / BugReportTick). On a switched-off game (addrOk 0) only the logs and
   files are collected and the refusal's reason goes on the info line; every step that asks another feature of the mod is skipped
   (none of them ran in this game). On a running mod everything runs as before and there is no refusal to name. */
enum ReportStep
{
    kStepLogs = 0,        /* the mod's logs, their kept parts, the crash folders - always */
    kStepRefusalInfo,     /* the info line says the address gate switched the mod off (InfoLineText) - only on a switched-off game */
    kStepWhereLink,       /* "a multiplayer world" read from the world link (store.cpp StoreLiveReady) and the soak's running flag */
    kStepKeeper,          /* the empty-shop repairs and [KEEPER] lines (items.cpp, the log's keeper lines) */
    kStepServerLog,       /* the world server's log (ui.cpp UiServerLogPath) */
    kStepArea,            /* the reporter's map square's stored files (store.cpp StoreAreaReportFiles) */
    kStepAskNearby,       /* the nearby players' logs (the world link) */
    kStepCounters,        /* every REPORT counter set and the farm totals (ReportEverythingForReport, ReportFarm) */
    kStepLiveTicks,       /* the nearby-log asks and answers and the report's thread (BugReportTick's link steps) */
    kStepCount
};
inline int ReportStepRuns(int step, int addrOk)
{
    if (step == kStepLogs) return 1;
    if (step == kStepRefusalInfo) return addrOk == 0 ? 1 : 0;
    if (step > kStepRefusalInfo && step < kStepCount) return addrOk != 0 ? 1 : 0;
    return 0;
}

/* THE INFO LINE'S LIMIT (T2716, F-1457): both report services refuse an info line over 300 characters ("the info line is too long" -
   tools/relay/bug-report-worker.js and bug-report-test-worker.js MAX_INFO_CHARS, counted in UTF-16 units). The address gate's own
   refusal text is far longer than that, so a switched-off game's info line names only THAT it was switched off; the packed log's
   `[ADDR] kenshi-coop is DISABLED ..` line says why. With the longest parts a line can have (a 9-character version, the pattern road's
   table name, a 10-digit protocol, the longest `where`) the line and this note make 296 characters - kept whole (t1457 (i)). Any line still over the limit is cut to it at a whole UTF-8 character (a UTF-8
   text never has fewer bytes than UTF-16 units, so <= 300 bytes is <= 300 units). */
const size_t kInfoMaxChars = 300;
inline const char* SwitchedOffInfoNote() { return "; the mod was switched off by its address check (its [ADDR] line in the log says why)"; }
inline std::string InfoLineText(const std::string& head, int switchedOff)
{
    std::string s = head + (switchedOff ? SwitchedOffInfoNote() : "");
    if (s.size() > kInfoMaxChars)
    {
        size_t n = kInfoMaxChars;
        while (n > 0 && (((unsigned char)s[n]) & 0xC0) == 0x80) --n;   /* s[n] is the first byte cut: back up off a character's tail */
        s.resize(n);
    }
    return s;
}

/* THE TEST-ONLY READER OF A SWITCHED-OFF TEST BUILD (command_channel.cpp CommandChannelRefusedTick, SW_DEV_BUILD only): the lines
   a run needs to reach both REPORT A BUG buttons and nothing else. `verb` and `first` = the line's first two words. "" = it runs;
   otherwise why not (the status file says `error <verb> refused: <why>`). */
inline std::string RefusedReaderRefusal(const std::string& verb, const std::string& first)
{
    if (verb == "load") return first.empty() ? std::string("a save name is needed") : std::string();
    if (verb == "uimenu") return first == "ingame" ? std::string() : std::string("only 'uimenu ingame'");
    if (verb == "uiclick")
        return (first == "bugreport" || first == "bugsend" || first == "bugcancel" || first == "bugok" || first == "bugback" || first == "bugretry")
               ? std::string() : std::string("only the REPORT A BUG buttons (bugreport, bugsend, bugcancel, bugok, bugback, bugretry)");
    if (verb == "uitype") return first == "bugtext" ? std::string() : std::string("only the report's text box (bugtext)");
    return "this game's mod is switched off; its TEST-ONLY reader takes only load, uimenu ingame, uiclick on a REPORT A BUG button and uitype bugtext";
}

}   /* namespace coopreport */

#endif
