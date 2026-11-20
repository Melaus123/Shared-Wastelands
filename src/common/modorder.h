/* src/common/modorder.h - T-1375 (owner decisions 1654 and 1692; the approved page build/pages/different-mods-mockup-v3.html,
 * sections 1, 2, 4 and 8): PUT MY MODS IN THE WORLD'S ORDER - this game's own mod list files rewritten in the world's order, the
 * same mods, then a restart. Pure: no global, no operating-system call. C++03 (VS2010 v100). The plugin (settings.cpp) does the
 * file reads and writes; the offline suite tests every rule here.
 *
 * WHERE KENSHI KEEPS THE ORDER (F1039, 1.0.68): data\mods.cfg = the TICKED mods in load order, "<name>.mod" a line (CRLF; written
 * by the launcher at Launch; the game only reads it). data\__mods.list = EVERY installed mod, ticked or not, in the launcher's
 * order, "<name>" a line (read and written by the launcher's mods panel, and rewritten when its dialog opens - T2373). A mod's
 * name in the world's list is its file name without ".mod" (settings.cpp ComputeFingerprint).
 *
 * DESIGN COMPARE (each judged by whether it decides from the real fact):
 *  A. Write mods.cfg only. The game loads mods.cfg, so a start without the launcher gets the world's order. But the launcher
 *     writes mods.cfg at every Launch from its own panel, whose order is __mods.list's (F1039: Read; that the panel follows the
 *     file is Inferred) - a player who starts through the launcher would get the old order back. Decides from half the fact.
 *  B. Write BOTH files in the world's order (CHOSEN). mods.cfg gets the order the game loads; __mods.list's rows of those mods
 *     get the same order, so the launcher's next Launch writes the same order again. Established practice (Inferred from how
 *     the tools are documented, not read here): Mod Organizer 2 keeps its own load-order record and writes the game's own
 *     plugin list from it so both stay in step; Vortex writes the game's load-order file and keeps its own record in the same
 *     order; neither adds or removes a mod when it sorts, and each keeps a backup of the list it replaces.
 *  C. Have the launcher do it (drive its panel). The launcher is not running while the game is, and it offers no other way in.
 *     Not possible.
 *
 * THE RULE (B), per file: only the lines naming a mod of the world's list move - they take the slots those mods held, in the
 * world's order. Every other line (our own mod, a base-game mod, a mod __mods.list records unticked, a blank or odd line) keeps
 * its slot and its bytes, and each slot keeps its own line ending - so nothing is added, removed, ticked or unticked.
 * THE SET CHECK, decided from each file as it is at the press (not from the list the game loaded at its start): every world mod
 * is named by exactly one line of the file; in mods.cfg every other mod line must name a mod this game loaded without counting it
 * (our own, the base game's) - a mod ticked since this game started, or one it did not load, refuses. Names compare with ASCII
 * case folded (Windows file names), spaces and tabs around a line ignored. A file that would not change is not written. A missing
 * __mods.list (the launcher never recorded one) leaves mods.cfg alone to write (the plugin's choice, settings.cpp).
 * THE TEXT ROUND-TRIPS: a UTF-8 byte-order mark, each line's own ending (CRLF / LF / CR, or none on a last line) - parse then
 * join gives the same bytes.
 */
#ifndef COOP_COMMON_MODORDER_H
#define COOP_COMMON_MODORDER_H

#include <string>
#include <vector>
#include <cstdio>
#include "modlist.h"   /* ModList, ModListDiff, ModsOrderOnly, ModsYourLine - the MODS tab's state below */

namespace coopmods {

/* The words (the approved page, version 3 - word for word). */
const char* const kModsOrderButton = "PUT MY MODS IN THE WORLD'S ORDER";   /* sections 1 and 4: the button */
const char* const kModsOrderTitle  = "MOD ORDER";                          /* section 2: the box that asks first */
const char* const kModsOrderAsk    = "Put your mods in the world's order? Kenshi must be restarted before the new order takes effect.";
const char* const kModsOrderDone   = "Your mod order now matches the world's. Restart Kenshi for it to take effect.";   /* section 2: after YES */
const char* const kModsOrderFailed = "Your mod order could not be changed. You can put your mods in the world's order yourself in Kenshi's launcher.";   /* owner 1710 a: after YES when the order could not be written */
/* section 1: the MODS tab's hover on the button (two lines of at most 60, as kModsUseHover) and the tab's line after YES */
const char* const kModsOrderHover  = "Changes the order of your own mods to the world's.\nKenshi must be restarted for the new order to take effect.";
const char* const kModsOrderWaitsLine = "Your mods: same mods, new order waits for a restart";
const char* const kModsOrderYes    = "YES";
const char* const kModsOrderNo     = "NO";
/* section 8: the log line every game writes when it did it (log only) */
inline std::string ModsOrderDoneLogLine(unsigned mods)
{
    char b[160];
    std::sprintf(b, "[MODS] this game's mod order was put in the world's order (%u mods); takes effect after a restart", mods);
    return b;
}
const char* const kModsOrderBackupSuffix = ".before-world-order";   /* the backup beside each file written (not player-facing) */
const unsigned    kModsListMaxBytes = 1048576;                      /* __mods.list read cap (every installed mod); mods.cfg keeps kModsCfgMaxBytes */

/* A list file as lines: each line's text and its own ending ("\r\n", "\n", "\r", or "" for a last line with none). */
struct ModsLines
{
    int bom;
    std::vector<std::string> text, end;
    ModsLines() : bom(0) {}
};
inline void ModsLinesParse(const std::string& t, ModsLines* o)
{
    *o = ModsLines();
    size_t i = 0;
    const size_t n = t.size();
    if (n >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) { o->bom = 1; i = 3; }
    while (i < n)
    {
        size_t j = i;
        while (j < n && t[j] != '\r' && t[j] != '\n') ++j;
        o->text.push_back(t.substr(i, j - i));
        if (j >= n) { o->end.push_back(std::string()); break; }
        if (t[j] == '\r' && j + 1 < n && t[j + 1] == '\n') { o->end.push_back("\r\n"); i = j + 2; }
        else { o->end.push_back(std::string(1, t[j])); i = j + 1; }
    }
}
inline std::string ModsLinesJoin(const ModsLines& l)
{
    std::string s = l.bom ? std::string("\xEF\xBB\xBF") : std::string();
    for (size_t i = 0; i < l.text.size(); ++i) s += l.text[i] + (i < l.end.size() ? l.end[i] : std::string());
    return s;
}
inline std::string ModsLowerAscii(const std::string& s)
{
    std::string o(s);
    for (size_t i = 0; i < o.size(); ++i) if (o[i] >= 'A' && o[i] <= 'Z') o[i] = (char)(o[i] - 'A' + 'a');
    return o;
}
/* The mod a line names, or "" when it names none. cfg = 1 (mods.cfg): "<name>.mod" (".mod" in any case), else not a mod line;
   cfg = 0 (__mods.list): the trimmed line. */
inline std::string ModsLineName(const std::string& line, int cfg)
{
    size_t a = 0, b = line.size();
    while (a < b && (line[a] == ' ' || line[a] == '\t')) ++a;
    while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t')) --b;
    const std::string s = line.substr(a, b - a);
    if (!cfg) return s;
    if (s.size() <= 4 || ModsLowerAscii(s.substr(s.size() - 4)) != ".mod") return std::string();
    return s.substr(0, s.size() - 4);
}
inline int ModsIndexOfLower(const std::vector<std::string>& v, const std::string& lower)
{
    for (size_t i = 0; i < v.size(); ++i) if (v[i] == lower) return (int)i;
    return -1;
}

enum ModsOrderVerdict
{
    kMoChanged = 0,     /* the file's world mods were in another order: *out holds the world's order */
    kMoSame    = 1,     /* already in the world's order: nothing to write */
    kMoMissing = 2,     /* a world mod is named by no line */
    kMoTwice   = 3,     /* a world mod is named by two lines (or the world's list names one twice) */
    kMoOther   = 4,     /* mods.cfg names a mod this game did not load uncounted (ticked since the start, or not loaded) */
    kMoNoWorld = 5      /* the world's list has no mods: there is no order to take */
};
struct ModsOrderFacts
{
    int verdict;
    std::string name;      /* the mod a refusal names */
    unsigned modLines;     /* lines naming a mod - the same before and after (only their order changes) */
    unsigned modLinesAfter;
    unsigned moved;        /* slots whose line changed */
    ModsOrderFacts() : verdict(kMoNoWorld), modLines(0), modLinesAfter(0), moved(0) {}
};
inline unsigned ModsCountModLines(const ModsLines& l, int cfg)
{
    unsigned n = 0;
    for (size_t i = 0; i < l.text.size(); ++i) if (!ModsLineName(l.text[i], cfg).empty()) ++n;
    return n;
}
/* THE REORDER. world = the world's mod names in its load order; cfg = 1 for mods.cfg, 0 for __mods.list; uncountedLower = the
   names (lower case) this game loaded without counting them (our own, the base game's) - used for mods.cfg's set check, 0 = every
   other line is kept as it is (__mods.list: it records unticked mods too). *out = in, with the world mods' lines in the world's
   order (kMoChanged) or unchanged (every other verdict). */
inline int ModsOrderApply(const std::vector<std::string>& world, const ModsLines& in, int cfg, const std::vector<std::string>* uncountedLower,
                          ModsLines* out, ModsOrderFacts* f)
{
    *out = in;
    *f = ModsOrderFacts();
    f->modLines = f->modLinesAfter = ModsCountModLines(in, cfg);
    if (world.empty()) { f->verdict = kMoNoWorld; return f->verdict; }
    std::vector<std::string> wl;
    for (size_t k = 0; k < world.size(); ++k)
    {
        const std::string lw = ModsLowerAscii(world[k]);
        if (ModsIndexOfLower(wl, lw) >= 0) { f->verdict = kMoTwice; f->name = world[k]; return f->verdict; }
        wl.push_back(lw);
    }
    std::vector<int> at(world.size(), -1);
    std::vector<size_t> slots;
    for (size_t i = 0; i < in.text.size(); ++i)
    {
        const std::string nm = ModsLineName(in.text[i], cfg);
        if (nm.empty()) continue;
        const std::string lnm = ModsLowerAscii(nm);
        const int k = ModsIndexOfLower(wl, lnm);
        if (k >= 0)
        {
            if (at[(size_t)k] >= 0) { f->verdict = kMoTwice; f->name = nm; return f->verdict; }
            at[(size_t)k] = (int)i;
            slots.push_back(i);
        }
        else if (uncountedLower != 0 && ModsIndexOfLower(*uncountedLower, lnm) < 0) { f->verdict = kMoOther; f->name = nm; return f->verdict; }
    }
    for (size_t k = 0; k < world.size(); ++k) if (at[k] < 0) { f->verdict = kMoMissing; f->name = world[k]; return f->verdict; }
    for (size_t s = 0; s < slots.size(); ++s)
    {
        const std::string& want = in.text[(size_t)at[s]];   /* the k-th world mod's own line text goes to the k-th slot */
        if (out->text[slots[s]] != want) { out->text[slots[s]] = want; ++f->moved; }
    }
    f->modLinesAfter = ModsCountModLines(*out, cfg);
    f->verdict = f->moved ? kMoChanged : kMoSame;
    return f->verdict;
}
/* For the log: why a file was not reordered (name cut to 80 bytes, one line). */
inline std::string ModsOrderWhy(const ModsOrderFacts& f)
{
    std::string nm;
    for (size_t i = 0; i < f.name.size() && i < 80; ++i) nm += (f.name[i] == '\r' || f.name[i] == '\n') ? ' ' : f.name[i];
    switch (f.verdict)
    {
    case kMoChanged: return "reordered";
    case kMoSame:    return "already in the world's order";
    case kMoMissing: return "it does not name the world's mod '" + nm + "'";
    case kMoTwice:   return "it names the mod '" + nm + "' twice";
    case kMoOther:   return "it names the mod '" + nm + "', which this game did not load at its start (ticked since, or not loaded)";
    default:         return "the world's list has no mods";
    }
}

/* THE MODS TAB (section 1): what it shows for this game's list against the world's. writtenFor = the world list's active rows this
   game last put its files in the order of this session ("" rows = never) - the new order waits for a restart while the game still
   runs the old one and the world's list is the same. kMoTabButton: the button (never greyed - it changes only this game, section 1);
   kMoTabWaits: the waits-for-a-restart line and no button; kMoTabNone: neither. */
enum ModsOrderTab { kMoTabNone = 0, kMoTabButton = 1, kMoTabWaits = 2 };
inline int ModsOrderTabState(int haveList, const ModList& w, const ModList& mine, const std::vector<std::string>& writtenFor)
{
    if (!haveList || !w.known || !mine.known || ModListSame(w, mine)) return kMoTabNone;
    if (!ModsOrderOnly(ModListDiff(w, mine))) return kMoTabNone;
    return (!writtenFor.empty() && writtenFor == w.active) ? kMoTabWaits : kMoTabButton;
}
/* the tab's "Your mods" line: the waits-for-a-restart line after YES, else T-247's line */
inline std::string ModsOrderYourLine(int haveList, const ModList& w, const ModList& mine, const std::vector<std::string>& writtenFor)
{
    return ModsOrderTabState(haveList, w, mine, writtenFor) == kMoTabWaits ? std::string(kModsOrderWaitsLine) : ModsYourLine(haveList, w, mine);
}

}   /* namespace coopmods */

#endif
