/* src/common/worldrace.h - T-1209 (owner 1414 d, 1416 a, 1417 a, 1418; build/mockups-t1209.md): THE WORLD'S RACE RULES COME
   FROM THE WORLD ITSELF. A race mod's kenshi_api.cfg in the world's own folder, beside rules.txt, is read by the world server at
   start and whenever the file is saved; its rows (kapirules.h's race.* form) are added to the option map only when the map is
   sent, so they are never written into rules.txt; every game follows them. A game never sets them: a race row a game sends is
   refused, and a login changes nothing. No file = no race rules. A file that cannot be read keeps the rows from before and is
   read again after a wait that grows while it stays unreadable (RetryWaitMs). Races past kapi::kMaxRaceKeys, and a race whose
   rules do not fit one world rule, are left out, each said on its own line. Races are named by their id as the option
   key holds it (the world server never loads the game's data, so it cannot know a race's shown name).

   The window lines are the approved mock-up's (decisions 1418, 1420, 1430), the folder shown is the world's real folder.

   PURE: no Windows.h, no I/O, no globals. Compiled into the world server (src/coop-store/store_main.cpp) and the offline suite.
   C++03 (VS2010 v100). Header-only. */
#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdio>
#include "kapirules.h"
#include "rulesfile.h"   /* rules.txt's lines (SplitLines, RuleLine) and the note a 0.4.0 world wrote above a race row (Note) */

namespace worldrace {

const char* const kFile = "kenshi_api.cfg";

inline bool EndsWithDots(const std::string& s) { return s.size() >= 3 && s.compare(s.size() - 3, 3, "...") == 0; }
inline std::string Num(int v) { char b[16]; std::sprintf(b, "%d", v); return b; }
inline std::string Races(int n) { return Num(n) + (n == 1 ? " race" : " races"); }

/* ---- the window lines (build/mockups-t1209.md section 1) ---- */
inline std::string SetLine(int races) { return "Race rules: " + Races(races) + " set by kenshi_api.cfg."; }
inline std::string NoRacesLine() { return "Race rules: kenshi_api.cfg sets no races, so there are no race rules."; }
inline std::string NoneSetLine() { return "Race rules: none of the races in kenshi_api.cfg could be set, so there are no race rules."; }
inline std::string NoFileLine(const std::string& dir)
{
    return "Race rules: no kenshi_api.cfg in this world's folder (" + dir + "), so there are no race rules.";
}
inline std::string UnreadableLine() { return "Race rules: kenshi_api.cfg couldn't be read, so the race rules from before are kept. Trying again in a moment."; }
inline std::string CopiedLine(const std::string& dir, int races)
{
    return "Race rules: kenshi_api.cfg was copied from the hosting game into this world's folder (" + dir + "). " + Races(races) + " set by it.";
}
inline std::string CopiedNoneLine(const std::string& dir)
{
    return "Race rules: kenshi_api.cfg was copied from the hosting game into this world's folder (" + dir + "). It sets no races, so there are no race rules.";
}
/* skipped: how many lines did not parse (a race left out is not a line: it has its own line); reasons: one sentence each, already ending in '.' */
inline std::string SkippedLine(int skipped, const std::vector<std::string>& reasons)
{
    std::string s = "Race rules: " + Num(skipped) + (skipped == 1 ? " line in kenshi_api.cfg was skipped." : " lines in kenshi_api.cfg were skipped.");
    for (size_t i = 0; i < reasons.size(); ++i) s += " " + reasons[i];
    return s;
}
/* a race's id as an option key holds it ("race.7100001-ocs-100.mod" -> "7100001-ocs-100.mod") */
inline std::string IdOf(const std::string& key) { return kapi::IsOptionKey(key) ? key.substr(5) : key; }
/* the reasons a race is left out (each a sentence, after the line reasons) */
inline std::string TooLongReason(const std::string& id) { return "Race \"" + id + "\" has more rules than one world rule can hold, so it was left out."; }
inline std::string TooManyReason(const std::string& id) { return "Race \"" + id + "\" was left out: a world holds the rules of " + Num(kapi::kMaxRaceKeys) + " races at most."; }
/* decision 1416 a: the race rows a 0.4.0 world holds (pushed earlier by a player's game) are dropped - said once, naming them by id */
inline std::string DroppedLine(const std::vector<std::string>& keys, const std::string& dir)
{
    std::string names;
    for (size_t i = 0; i < keys.size(); ++i) names += (i > 0 ? ", " : "") + IdOf(keys[i]);
    return "Race rules: this world had rules for " + Races((int)keys.size()) + " that came from a player's game (" + names
           + "). They are no longer used. To set race rules, put a race mod's kenshi_api.cfg in " + dir + ".";
}

/* ---- the file's text -> its rows, and what was skipped ---- */
struct FromFile
{
    kapi::Rows rows;
    int skipped;                        /* lines that did not parse */
    std::vector<std::string> reasons;   /* one sentence each, for the skipped lines */
    std::vector<std::string> leftOut;   /* one window line per race left out */
    int races;                          /* the races the file sets a rule for, those left out included */
    FromFile() : skipped(0), races(0) {}
};
inline FromFile ReadText(const std::string& text)
{
    FromFile out;
    kapi::Rules r;
    r.ParseText(text);
    std::vector<std::string> tooLong, tooMany;
    r.ToRows(&out.rows, &tooLong, &tooMany);
    const kapi::Report& rep = r.GetReport();
    out.skipped = rep.skipped;
    for (size_t i = 0; i < rep.why.size(); ++i) out.reasons.push_back("Line " + Num(rep.why[i].line) + ": " + rep.why[i].why + (EndsWithDots(rep.why[i].why) ? "" : "."));   /* a reason that ends in "..." takes no extra full stop */
    if (rep.skipped > (int)rep.why.size()) out.reasons.push_back("And " + Num(rep.skipped - (int)rep.why.size()) + " more.");
    for (size_t i = 0; i < tooLong.size(); ++i) out.leftOut.push_back(TooLongReason(tooLong[i]));
    for (size_t i = 0; i < tooMany.size(); ++i) out.leftOut.push_back(TooManyReason(tooMany[i]));
    out.races = (int)(out.rows.size() + tooLong.size() + tooMany.size());
    return out;
}

/* ---- THE FILE'S STATE on the world server, one step per read ---- */
enum { kAbsent = 0, kUnreadable = 1, kReadable = 2 };
struct FileState
{
    kapi::Rows  rows;       /* the race rows every game follows */
    std::string text;       /* the bytes last read */
    int         kind;       /* the last read's kind; -1 = not read yet */
    FileState() : kind(-1) {}
};
/* One read of the file (kind, and its bytes when readable). Lines to say go to *say. copied: this read follows HOST's copy of the
   hosting game's file into a new world's folder (decision 1417 a). Returns true when the rows changed. A missing file clears the
   rows; an unreadable one keeps them (the caller reads again a moment later); the same bytes again change nothing. */
inline bool Step(FileState* st, int kind, const std::string& text, const std::string& dir, bool copied, std::vector<std::string>* say)
{
    const kapi::Rows was = st->rows;
    if (kind == kUnreadable)
    {
        if (st->kind != kUnreadable) say->push_back(UnreadableLine());
        st->kind = kUnreadable;
        return false;
    }
    if (kind == kAbsent)
    {
        if (st->kind != kAbsent) say->push_back(NoFileLine(dir));
        st->kind = kAbsent; st->text.clear(); st->rows.clear();
        return kapi::RowsText(was) != kapi::RowsText(st->rows);
    }
    if (st->kind == kReadable && text == st->text) return false;
    const FromFile f = ReadText(text);
    st->kind = kReadable; st->text = text; st->rows = f.rows;
    /* the count line (a file naming no race, or one whose every race was left out, says so), the skipped lines, then each race left out */
    const int n = (int)f.rows.size();
    if (copied) say->push_back(n == 0 ? CopiedNoneLine(dir) : CopiedLine(dir, n));
    else say->push_back(f.races == 0 ? NoRacesLine() : n == 0 ? NoneSetLine() : SetLine(n));
    if (f.skipped > 0) say->push_back(SkippedLine(f.skipped, f.reasons));
    for (size_t i = 0; i < f.leftOut.size(); ++i) say->push_back(f.leftOut[i]);
    return kapi::RowsText(was) != kapi::RowsText(st->rows);
}

/* THE MAP EVERY GAME IS SENT: the world's other rules with every race row replaced by the file's rows (at most kMaxRaceKeys). */
inline std::map<std::string, std::string> WithFileRows(const std::map<std::string, std::string>& options, const kapi::Rows& fileRows)
{
    std::map<std::string, std::string> out;
    for (std::map<std::string, std::string>::const_iterator it = options.begin(); it != options.end(); ++it)
        if (!kapi::IsOptionKey(it->first)) out.insert(*it);
    int n = 0;
    for (size_t i = 0; i < fileRows.size() && n < kapi::kMaxRaceKeys; ++i)
        if (kapi::IsOptionKey(fileRows[i].first)) { out[fileRows[i].first] = fileRows[i].second; ++n; }
    return out;
}

/* a row a game sends: a race row is refused (race rules come from the world's file, never from a game) */
inline bool GameRowRefused(const std::string& key) { return kapi::IsOptionKey(key); }

/* decision 1417 a: HOST copies the hosting game's own kenshi_api.cfg into a NEW world's folder only when that folder has none.
   hostFile: the hosting game named its file (--host-rules); worldNew: no world server has run in this folder yet. */
inline bool HostCopyWanted(bool hostFile, bool worldNew, bool worldHasFile) { return hostFile && worldNew && !worldHasFile; }
/* A NEW world: none of the files a world server leaves behind is in its folder - the rules-made marker, options.txt (a world from
   before rules.txt), profiles.txt (a game has joined) or world.gen (a world save was numbered). rules.txt alone does not count:
   HOST writes the world password into it before the world server starts. So a 0.4.0 world whose marker could not be written is
   still not new once a game has joined it. */
inline bool WorldIsNew(bool madeMarker, bool optionsTxt, bool profilesTxt, bool worldGen) { return !madeMarker && !optionsTxt && !profilesTxt && !worldGen; }

/* THE WAIT BEFORE THE NEXT READ of a file that could not be read: 1 s after the first failure, doubling to 30 s at most.
   streak: the failed reads in a row (1 = the first). A readable or missing file ends the streak. */
const unsigned kSettleMs = 1000u, kRetryMaxMs = 30000u;
inline unsigned RetryWaitMs(int streak)
{
    unsigned w = kSettleMs;
    for (int i = 1; i < streak && w < kRetryMaxMs; ++i) w *= 2u;
    return w < kRetryMaxMs ? w : kRetryMaxMs;
}

/* THE RACE ROWS rules.txt HOLDS (never used; the world server takes them out). all: every race key a line names, a line with no
   value included; fromGame: those whose line has the note the world server wrote above a row a game sent (a 0.4.0 world's
   "# race.<id>: the race rules for <race>, set in the game."). A line typed by hand has no such note, so it is not called a
   player's game's. */
inline void RaceRowsInRules(const std::string& text, std::set<std::string>* all, std::set<std::string>* fromGame)
{
    const std::vector<rulesfile::Line> ls = rulesfile::SplitLines(text);
    for (size_t i = 0; i < ls.size(); ++i)
    {
        std::string key, value;
        bool unknown = false;
        if (!rulesfile::RuleLine(rulesfile::BodyOf(ls, i), &key, &value, &unknown) || !kapi::IsOptionKey(key)) continue;
        all->insert(key);
        if (i > 0 && rulesfile::Trim(rulesfile::BodyOf(ls, i - 1)) == "# " + rulesfile::Note(key)) fromGame->insert(key);
    }
}

}   /* namespace worldrace */
