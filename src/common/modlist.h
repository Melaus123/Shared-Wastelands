/* src/common/modlist.h - settings5 S5 (docs/design-settings1.md section 4; user decision 2026-09-26: every player's
 * mods must match EXACTLY - there is no allow-list): THE MOD LIST ON THE STORE LINK, ITS COMPARISON AND ITS WORDS.
 *
 * The list is the S1 fingerprint's own rows (settings.cpp ComputeFingerprint): the active mods in load order as
 * "name|version|crc", and the plugin DLLs named in the available mods' RE_Kenshi.json, sorted, as "dll|crc" - the
 * co-op mod itself left out (its compatibility is the protocol number). It travels at the end of the store HELLO
 * (store protocol 46). The notebook keeps the world's copy in the world's folder (mods.txt), and a HELLO whose list
 * differs is refused with kRefuseMods (storelink.h), the world's list riding in the REFUSE - so the refused game can
 * name what differs, in words a player can act on, on its own title screen.
 * T-246 (owner decision 199 b, 2026-09-29): a differing game is WARNED AND LET IN under DIFFERENT MODS Accepted: the world's
 * list rides in the WELCOME (kModsDiffers, store protocol 57) and the game tells its player once in its world (ModWarnText,
 * owner 200, titled MODS DON'T MATCH). T-247 (2026-11-15): the refusal is the world rule DIFFERENT MODS Rejected, and the
 * world's list is set only by an admin (ModsHelloDecideWorld below).
 *
 * Pure: no global, no operating-system call, no engine header. Compiled into the plugin, the notebook and the offline
 * test exe, so the two ends cannot hold two ideas of what "the same mods" means. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_MODLIST_H
#define COOP_COMMON_MODLIST_H
#include "names.h"   /* the on-disk names (mod folder, files, window texts) */

#include <string>
#include <vector>
#include <algorithm>   /* T-1238: PluginRowsCut sorts */
#include <cstring>
#include <cstdio>

namespace coopmods {

const unsigned int kModRowsMax      = 1024;   /* per list, on encode and on decode */
const unsigned int kModRowLenMax    = 512;    /* per row, and for the hash */
const size_t       kNamesInHeadline = 3;      /* names per kind in the sentence the player reads */
const size_t       kNamesInDetail   = 12;     /* names per kind in the detail line */
const char* const  kSaltRow         = "coop-test-salt|0|00000000";   /* the made-up mod modfingerprint_salt=1 adds (TEST ONLY) */

struct ModList
{
    int known;                          /* 1 = the game read its own list; 0 = it could not (never compared, never recorded) */
    std::string hash;                   /* the S1 fingerprint, 16 hex - logged, not compared (the rows are) */
    std::vector<std::string> active;    /* "name|version|crc", load order */
    std::vector<std::string> plugins;   /* "dll|crc", sorted */
    ModList() : known(0) {}
};

inline void ModPutU32(std::vector<char>* o, unsigned int v) { char b[4]; std::memcpy(b, &v, 4); o->insert(o->end(), b, b + 4); }
inline void ModPutStr(std::vector<char>* o, const std::string& s)
{
    const size_t n = s.size() > kModRowLenMax ? (size_t)kModRowLenMax : s.size();
    ModPutU32(o, (unsigned int)n);
    o->insert(o->end(), s.begin(), s.begin() + n);
}
inline bool ModGetU32(const std::vector<char>& p, size_t* at, unsigned int* v)
{
    if (*at > p.size() || p.size() - *at < 4) return false;
    std::memcpy(v, &p[*at], 4); *at += 4;
    return true;
}
inline bool ModGetStr(const std::vector<char>& p, size_t* at, std::string* s)
{
    unsigned int n = 0;
    if (!ModGetU32(p, at, &n)) return false;
    if (n > kModRowLenMax || p.size() - *at < (size_t)n) return false;
    s->assign(p.begin() + *at, p.begin() + *at + n); *at += n;
    return true;
}

/* APPENDS {u32 known, str hash, u32 nActive, nActive x str, u32 nPlugins, nPlugins x str} (str = u32 length + bytes). */
inline void ModListEncode(const ModList& m, std::vector<char>* out)
{
    ModPutU32(out, m.known ? 1u : 0u);
    ModPutStr(out, m.hash);
    const size_t na = m.active.size() > kModRowsMax ? (size_t)kModRowsMax : m.active.size();
    ModPutU32(out, (unsigned int)na);
    for (size_t i = 0; i < na; ++i) ModPutStr(out, m.active[i]);
    const size_t np = m.plugins.size() > kModRowsMax ? (size_t)kModRowsMax : m.plugins.size();
    ModPutU32(out, (unsigned int)np);
    for (size_t i = 0; i < np; ++i) ModPutStr(out, m.plugins[i]);
}

/* Reads one list at *at and moves *at past it. False - and *m empty with known 0, *at unmoved - for an absent or
   malformed list, which the notebook treats as "not checked" and never as a match. */
inline bool ModListDecode(const std::vector<char>& p, size_t* at, ModList* m)
{
    *m = ModList();
    ModList r;
    size_t k = *at;
    unsigned int known = 0, na = 0, np = 0;
    if (!ModGetU32(p, &k, &known) || !ModGetStr(p, &k, &r.hash) || !ModGetU32(p, &k, &na) || na > kModRowsMax) return false;
    for (unsigned int i = 0; i < na; ++i) { std::string s; if (!ModGetStr(p, &k, &s)) return false; r.active.push_back(s); }
    if (!ModGetU32(p, &k, &np) || np > kModRowsMax) return false;
    for (unsigned int i = 0; i < np; ++i) { std::string s; if (!ModGetStr(p, &k, &s)) return false; r.plugins.push_back(s); }
    r.known = known ? 1 : 0;
    *m = r; *at = k;
    return true;
}

/* settings5 fold (review-settings5 4): A ROW WHOSE FILE COULD NOT BE READ NEVER MATCHES ANYTHING - not even another
   unreadable row. Its checksum field is "-1" (the file did not open) or "?" (the mod itself was unreadable, a placeholder
   row "(unreadable mod N)|?|?"). Two games that both failed to read a file have not shown they hold the same file. */
inline bool ModRowComparable(const std::string& row)
{
    const size_t k = row.rfind('|');
    if (k == std::string::npos) return false;
    const std::string c = row.substr(k + 1);
    return !(c.empty() || c == "-1" || c == "?");
}
/* EXACT: the same mods, the same versions and files, the same load order, the same plugin DLLs - and every row readable. */
inline bool ModListSame(const ModList& a, const ModList& b)
{
    if (a.active != b.active || a.plugins != b.plugins) return false;
    for (size_t i = 0; i < a.active.size(); ++i) if (!ModRowComparable(a.active[i])) return false;
    for (size_t i = 0; i < a.plugins.size(); ++i) if (!ModRowComparable(a.plugins[i])) return false;
    return true;
}

inline std::string ModRowName(const std::string& row) { const size_t k = row.find('|'); return k == std::string::npos ? row : row.substr(0, k); }

struct ModDiff
{
    std::vector<std::string> missing;   /* in the world's list, not in this game's */
    std::vector<std::string> extra;     /* in this game's list, not in the world's */
    std::vector<std::string> changed;   /* in both, with another version or another file */
    int orderDiffers;                   /* the same active mods in another load order */
    std::vector<std::string> unreadable;   /* T-247 fold (1680 notice): a slot or a file one side could not read - a placeholder row's own
                                              words "(unreadable mod N)", or the mod whose file could not be read; never missing / extra */
    ModDiff() : orderDiffers(0) {}
};
inline bool ModRowPlaceholder(const std::string& row);                          /* below (T-247 review fold FIX 3) */
inline std::string ModRowHead(const std::string& row);                         /* below */
inline int ModListDiffersOnlyUnreadable(const ModList& world, const ModList& mine);   /* below (1680) */
inline void ModAddOnce(std::vector<std::string>* v, const std::string& s) { for (size_t i = 0; i < v->size(); ++i) if ((*v)[i] == s) return; v->push_back(s); }

inline int ModIndexOfName(const std::vector<std::string>& rows, const std::string& name)
{
    for (size_t i = 0; i < rows.size(); ++i) if (ModRowName(rows[i]) == name) return (int)i;
    return -1;
}
/* T-247 re-check fold (item 2): THE ROW A PLACEHOLDER STANDS IN FOR - the placeholder at rows[i] takes the place of one row of other
   that rows lacks by name and that lies between the readable rows both lists hold just before and just after the placeholder (the
   list's ends when there is none). Returns that row's index in other (marked in *taken, so two placeholders never take one row), or
   -1: the placeholder took the place of no row there. */
inline int ModPlaceholderStandsFor(const std::vector<std::string>& rows, size_t i, const std::vector<std::string>& other, std::vector<char>* taken)
{
    int lo = -1, hi = (int)other.size();
    for (size_t k = i; k-- > 0; )
    {
        if (ModRowPlaceholder(rows[k])) continue;
        const int at = ModIndexOfName(other, ModRowName(rows[k]));
        if (at >= 0) { lo = at; break; }
    }
    for (size_t k = i + 1; k < rows.size(); ++k)
    {
        if (ModRowPlaceholder(rows[k])) continue;
        const int at = ModIndexOfName(other, ModRowName(rows[k]));
        if (at >= 0) { hi = at; break; }
    }
    for (int j = lo + 1; j < hi; ++j)
        if (!(*taken)[(size_t)j] && !ModRowPlaceholder(other[(size_t)j]) && ModIndexOfName(rows, ModRowName(other[(size_t)j])) < 0) { (*taken)[(size_t)j] = 1; return j; }
    return -1;
}
inline void ModDiffRows(const std::vector<std::string>& world, const std::vector<std::string>& mine, ModDiff* d)
{
    /* T-247 re-check fold (item 2): a row a placeholder on the other side stands in for is neither missing nor extra - the slot is
       named once, as could not be read */
    std::vector<char> worldTaken(world.size(), (char)0), mineTaken(mine.size(), (char)0);
    for (size_t i = 0; i < mine.size(); ++i) if (ModRowPlaceholder(mine[i])) ModPlaceholderStandsFor(mine, i, world, &worldTaken);
    for (size_t i = 0; i < world.size(); ++i) if (ModRowPlaceholder(world[i])) ModPlaceholderStandsFor(world, i, mine, &mineTaken);
    for (size_t i = 0; i < world.size(); ++i)
    {
        if (ModRowPlaceholder(world[i])) { ModAddOnce(&d->unreadable, ModRowName(world[i])); continue; }   /* T-247 fold: a slot that could not be read */
        if (worldTaken[i]) continue;   /* this game's placeholder stands in for it: not "missing" */
        const int j = ModIndexOfName(mine, ModRowName(world[i]));
        if (j < 0) d->missing.push_back(ModRowName(world[i]));
        else if (mine[(size_t)j] != world[i] || !ModRowComparable(world[i]) || !ModRowComparable(mine[(size_t)j]))
        {   /* settings5 fold: an unreadable row is never the same. T-247 re-check fold (item 2): the same mod and version whose file one
               side could not read is "could not be read", never "different version or file" */
            if (ModRowHead(world[i]) == ModRowHead(mine[(size_t)j]) && !(ModRowComparable(world[i]) && ModRowComparable(mine[(size_t)j])))
                ModAddOnce(&d->unreadable, ModRowName(world[i]));
            else d->changed.push_back(ModRowName(world[i]));
        }
    }
    for (size_t i = 0; i < mine.size(); ++i)
    {
        if (ModRowPlaceholder(mine[i])) { ModAddOnce(&d->unreadable, ModRowName(mine[i])); continue; }   /* T-247 fold: never an "extra" mod */
        if (mineTaken[i]) continue;   /* the world's placeholder stands in for it: not "extra" */
        if (ModIndexOfName(world, ModRowName(mine[i])) < 0) d->extra.push_back(ModRowName(mine[i]));
    }
}
/* What differs: the WORLD's list against THIS game's. A plugin is named by its DLL. */
inline ModDiff ModListDiff(const ModList& world, const ModList& mine)
{
    ModDiff d;
    ModDiffRows(world.active, mine.active, &d);
    ModDiffRows(world.plugins, mine.plugins, &d);
    /* Load order decides which mod wins where two change the same thing, so the same mods in another order differ too. */
    if (world.active.size() == mine.active.size())
        for (size_t i = 0; i < world.active.size(); ++i)
            if (!ModRowPlaceholder(world.active[i]) && !ModRowPlaceholder(mine.active[i])   /* T-247 fold: a placeholder's slot is not an order */
                && ModRowName(world.active[i]) != ModRowName(mine.active[i])) { d.orderDiffers = 1; break; }
    if (ModListDiffersOnlyUnreadable(world, mine))
    {   /* T-247 fold (1680): every difference is a slot or a file one side could not read - the mod a placeholder stands in for is not
           "missing", and a file that could not be read is not "a different version"; each is named as could not be read */
        for (size_t i = 0; i < d.changed.size(); ++i) ModAddOnce(&d.unreadable, d.changed[i]);
        d.missing.clear(); d.extra.clear(); d.changed.clear(); d.orderDiffers = 0;
    }
    return d;
}

inline std::string ModNames(const std::vector<std::string>& v, size_t cap)
{
    std::string s;
    const size_t n = v.size() < cap ? v.size() : cap;
    for (size_t i = 0; i < n; ++i) { if (i) s += ", "; s += v[i]; }
    if (v.size() > n) { char b[40]; std::sprintf(b, " and %u more", (unsigned int)(v.size() - n)); s += b; }
    return s;
}

/* T-247 (the approved page, version 3 section 4): THE SENTENCE A GAME TURNED AWAY UNDER "Rejected" READS, on the title screen in the
   MODS DON'T MATCH box. Missing and extra mods are named as the let-in notice names them; a different version is never named (only
   "don't match"); the same mods in another order is its own sentence. */
const char* const kModsRefuseHead = "This world only accepts players whose mods match the world's";
inline std::string ModRefusalText(const ModDiff& d)
{
    std::vector<std::string> parts;
    if (d.changed.empty())
    {
        if (!d.missing.empty()) parts.push_back("missing " + ModNames(d.missing, kNamesInHeadline));
        if (!d.extra.empty())   parts.push_back("extra " + ModNames(d.extra, kNamesInHeadline));
        if (parts.empty() && d.orderDiffers) return std::string(kModsRefuseHead) + ". Yours are the same mods in a different order.";
    }
    if (parts.empty()) return std::string(kModsRefuseHead) + ", and yours don't.";
    std::string s = std::string(kModsRefuseHead) + ". Yours don't:";
    for (size_t i = 0; i < parts.size(); ++i) s += (i ? "; " : " ") + parts[i];
    return s + ".";
}

/* THE DETAIL LINE: every kind, its count and up to kNamesInDetail names. Both logs (words1: the notice's second
   paragraph is ModDetailPlayerText below). */
inline std::string ModDetailText(const ModDiff& d)
{
    char b[80];
    std::string s;
    std::sprintf(b, "Details: missing (%u): ", (unsigned int)d.missing.size()); s += b;
    s += d.missing.empty() ? std::string("none") : ModNames(d.missing, kNamesInDetail);
    std::sprintf(b, " | extra (%u): ", (unsigned int)d.extra.size()); s += b;
    s += d.extra.empty() ? std::string("none") : ModNames(d.extra, kNamesInDetail);
    std::sprintf(b, " | different version or file (%u): ", (unsigned int)d.changed.size()); s += b;
    s += d.changed.empty() ? std::string("none") : ModNames(d.changed, kNamesInDetail);
    s += d.orderDiffers ? " | load order: different" : " | load order: same";
    if (!d.unreadable.empty())
    {   /* T-247 fold (1680): only when something could not be read, so every other line reads as before */
        std::sprintf(b, " | could not be read (%u): ", (unsigned int)d.unreadable.size()); s += b;
        s += ModNames(d.unreadable, kNamesInDetail);
    }
    return s;
}

/* words1 (owner wording audit 2026-09-27, section 10): the notice's second paragraph in the player's words - one line per
   kind that differs, empty kinds left out, no counts and no "load order: same". */
inline std::string ModDetailPlayerText(const ModDiff& d)
{
    std::string s;
    if (!d.missing.empty()) s += "Missing: " + ModNames(d.missing, kNamesInDetail);
    if (!d.extra.empty())   s += std::string(s.empty() ? "" : "\n") + "Extra: " + ModNames(d.extra, kNamesInDetail);
    if (!d.changed.empty()) s += std::string(s.empty() ? "" : "\n") + "Different version: " + ModNames(d.changed, kNamesInDetail);
    if (d.orderDiffers)     s += std::string(s.empty() ? "" : "\n") + "Load order is different.";
    return s;
}

/* settings5 fold (review-settings5 4): the sentence for a game that could not read its OWN list, when the world has one. */
inline std::string ModUnreadableText()
{
    return "Your game could not read its own mod list, so it can't be checked against the world's.";
}

/* ---- T-247 (owner decisions 1679, 1680, 1681, 1686, 1692 and "Admins decide", 2026-11-15): THE WORLD'S MOD LIST AND THE DOOR ----
   The world's list is mods.txt in the world's folder, and nothing sets it by itself: not the first game in, not the owner's game, not
   a HOST press. It is set only by an admin's USE MY MODS FOR THIS WORLD (the world server writes the asker's greeting list) or by a
   hand edit; deleting the file gives the world no list again. "No list" (no mods.txt: nobody's mods are checked) is NOT "a list of
   no mods" (known=1 and no rows: set by an admin running none, checked like any list) - the file format (ModListFromText: absent
   or torn = no list), the decision below and the offline suite keep them apart.
   DIFFERENT MODS (the world rule "modcheck", GAME OPTIONS' MODS tab): Accepted (the default) lets a differing game in with the
   notice; Rejected turns it away - except (1680) a game whose only differences are files one side could not read, and (1686) a
   proven admin or the owner (the owner password, or the admin password with the person on the admin list, shown at the door), who
   is let in and told why. Nobody already connected is ever disconnected when the list or the setting changes (1681). */
enum ModsHelloVerdict
{
    kModsHelloAdmitUnchecked   = 0,   /* mods.txt is held by another program: let in unchecked, nothing written (T-1280) */
    kModsHelloMatch            = 1,
    kModsHelloNoList           = 2,   /* the world has no list: nobody's mods are checked (admins are told) */
    kModsHelloRefuseDiffers    = 4,   /* Rejected: readable, differs, not only unreadable files, not a proven admin */
    kModsHelloRefuseUnreadable = 5,   /* Rejected: this game's OWN list unreadable, not a proven admin */
    kModsHelloAdmitDiffers     = 6,   /* Accepted (or 1680: only unreadable files differ): let in and warned */
    kModsHelloAdmitUnreadable  = 7,   /* Accepted: this game's OWN list unreadable - let in, shown the plain notice */
    kModsHelloAdmitAdmin       = 9    /* Rejected: differs (or its own list unreadable), a proven admin or the owner - let in, told why (1686) */
};
enum ModsMismatchPolicy { kModsMismatchWarn = 0, kModsMismatchRefuse = 1 };   /* DIFFERENT MODS: Accepted / Rejected */
const char* const kModsRuleKey = "modcheck";          /* the world rule (rules.txt, OPTIONS): "accepted" / "rejected"; absent = accepted */
const char* const kModsRuleAccepted = "accepted";
const char* const kModsRuleRejected = "rejected";
inline bool ModsRuleValueOk(const std::string& v) { return v == kModsRuleAccepted || v == kModsRuleRejected; }
inline int ModsPolicyOfRule(const std::string& v) { return v == kModsRuleRejected ? kModsMismatchRefuse : kModsMismatchWarn; }
inline const char* ModsPolicyWord(int policy) { return policy == kModsMismatchRefuse ? "Rejected" : "Accepted"; }

/* 1680: the two lists differ ONLY where a file could not be read - the same mods and plugin DLLs in the same order, every row's
   name and version the same, and each row whose checksum differs (or is unreadable) unreadable on at least one side. Two identical
   readable lists are not "only unreadable" (they match). */
inline std::string ModRowHead(const std::string& row) { const size_t k = row.rfind('|'); return k == std::string::npos ? row : row.substr(0, k); }
inline int ModRowsOnlyUnreadable(const std::vector<std::string>& a, const std::vector<std::string>& b, int* anyUnreadable)
{
    if (a.size() != b.size()) return 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (ModRowHead(a[i]) != ModRowHead(b[i])) return 0;
        const bool ca = ModRowComparable(a[i]), cb = ModRowComparable(b[i]);
        if (ca && cb) { if (a[i] != b[i]) return 0; continue; }
        *anyUnreadable = 1;
    }
    return 1;
}
/* T-247 review fold (FIX 3): a PLACEHOLDER row - an active slot whose mod could not be read at all (settings.cpp writes
   "(unreadable mod N)|?|?"). Its name is not a mod's name: it may stand for any mod, or for a base or the mod's own entry the other
   side leaves out, so it is never a real missing or extra mod. */
inline bool ModRowPlaceholder(const std::string& row)
{
    static const char kHead[] = "(unreadable mod ";
    const size_t h = sizeof(kHead) - 1;
    return row.size() >= h + 4 && row.compare(0, h, kHead) == 0 && row.compare(row.size() - 4, 4, "|?|?") == 0;
}
inline int ModRowsHavePlaceholder(const std::vector<std::string>& v)
{
    for (size_t i = 0; i < v.size(); ++i) if (ModRowPlaceholder(v[i])) return 1;
    return 0;
}
/* 1680 with placeholder rows on either side: the two lists line up when every readable row pairs with the other side's row of the same
   name and version (equal, or unreadable on one side) in the same order, and each placeholder takes the place of one row of the other
   side (any row, or another placeholder) or of none. Rows are at most kModRowsMax a side. */
inline int ModRowsAlignPlaceholders(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    const size_t na = a.size(), nb = b.size(), w = nb + 1;
    std::vector<char> r((na + 1) * w, (char)0);
    r[0] = 1;
    for (size_t i = 0; i <= na; ++i)
        for (size_t j = 0; j <= nb; ++j)
        {
            if (!r[i * w + j]) continue;
            const bool pa = i < na && ModRowPlaceholder(a[i]), pb = j < nb && ModRowPlaceholder(b[j]);
            if (pa) r[(i + 1) * w + j] = 1;
            if (pb) r[i * w + j + 1] = 1;
            if (i < na && j < nb)
            {
                bool ok = pa || pb;
                if (!ok && ModRowHead(a[i]) == ModRowHead(b[j]))
                    ok = !(ModRowComparable(a[i]) && ModRowComparable(b[j])) || a[i] == b[j];
                if (ok) r[(i + 1) * w + j + 1] = 1;
            }
        }
    return r[na * w + nb] ? 1 : 0;
}
inline int ModListDiffersOnlyUnreadable(const ModList& world, const ModList& mine)
{
    if (!world.known || !mine.known) return 0;
    int any = 0;
    if (ModRowsHavePlaceholder(world.active) || ModRowsHavePlaceholder(mine.active))
    {   /* T-247 review fold (FIX 3): a placeholder on either side is "unreadable", never a real missing / extra mod */
        if (!ModRowsAlignPlaceholders(world.active, mine.active) || !ModRowsOnlyUnreadable(world.plugins, mine.plugins, &any)) return 0;
        return 1;
    }
    if (!ModRowsOnlyUnreadable(world.active, mine.active, &any) || !ModRowsOnlyUnreadable(world.plugins, mine.plugins, &any)) return 0;
    return any;
}
/* T-247 review fold (FIX 3): a list USE MY MODS may make the world's - read whole, and no active slot unreadable (a placeholder row). A
   list with one would become a world's list no game can match (decision 1698's sentence answers it, 1699's log line names the slot). */
inline int ModListReadableWhole(const ModList& m) { return (m.known && !ModRowsHavePlaceholder(m.active)) ? 1 : 0; }
/* THE DECISION AT A GREETING. listKnown = this game read its own list; haveList = the world has a list (mods.txt read, known=1 -
   a list of no mods included); provenAdmin = the owner or a listed admin proven at its door; sameAsList = ModListSame;
   onlyUnreadable = ModListDiffersOnlyUnreadable; policy = DIFFERENT MODS. */
inline int ModsHelloDecideWorld(int listKnown, int haveList, int provenAdmin, int sameAsList, int onlyUnreadable, int policy)
{
    if (!haveList) return kModsHelloNoList;
    if (!listKnown) return policy == kModsMismatchWarn ? kModsHelloAdmitUnreadable : (provenAdmin ? kModsHelloAdmitAdmin : kModsHelloRefuseUnreadable);
    if (sameAsList) return kModsHelloMatch;
    if (policy == kModsMismatchWarn || onlyUnreadable) return kModsHelloAdmitDiffers;
    return provenAdmin ? kModsHelloAdmitAdmin : kModsHelloRefuseDiffers;
}
/* T-246 fold (review item 7): the "differs, let in" line and modsAdmittedDiffers count only a game actually admitted (after
   the lobby, the profile pick and the lifetime limit). */
inline int ModsCountAdmittedDiffers(int verdict, int admitted)
{
    return (admitted && (verdict == kModsHelloAdmitDiffers || verdict == kModsHelloAdmitUnreadable || verdict == kModsHelloAdmitAdmin)) ? 1 : 0;
}
/* A CONNECTED game against a list just set (1681: never a disconnect, under either value) - what it and every game are told. */
enum ModsRecheckVerdict { kMrNotChecked = 0, kMrMatch = 1, kMrDiffers = 2, kMrUnreadable = 3 };
inline int ModsRecheck(int haveList, const ModList& world, const ModList& peer)
{
    if (!haveList) return kMrNotChecked;
    if (!peer.known) return kMrUnreadable;
    return ModListSame(world, peer) ? kMrMatch : kMrDiffers;
}
/* USE MY MODS FOR THIS WORLD (an admin's request; the world server's answer): 0 = written, else why not. */
enum ModsSetVerdict { kMsSet = 0, kMsNotAdmin = 1, kMsOwnUnreadable = 2, kMsFileHeld = 3, kMsNoList = 4 };
inline int ModsSetDecide(int askerAdmin, int haveGreetingList, int greetingKnown, int fileHeld)
{
    if (!askerAdmin) return kMsNotAdmin;
    if (!haveGreetingList) return kMsNoList;
    if (!greetingKnown) return kMsOwnUnreadable;
    if (fileHeld) return kMsFileHeld;
    return kMsSet;
}

/* T-246 (owner decision 200, exact words): THE NOTICE A JOINER LET IN WITH OTHER MODS READS, once, in its world. The kinds that
   apply, in this order, joined by "; " - up to kNamesInHeadline names each, then "and N more" (ModNames). The same mods in
   another order and nothing else is its own sentence. */
const char* const kModsWarnTitle = "MODS DON'T MATCH";   /* owner 200 - panelstatus.h NoticeTitle keys it on the two starts below */
/* T-246 fold (owner 201, 2026-09-29: "you can just keep it simple and say mods don't match"): the plain notice - for a version
   difference, anything that cannot be named, and a game whose own list could not be read. */
inline std::string ModWarnPlainText()
{
    return "Your mods don't match the world's. You can still play, but some things may look or work differently for you than for the host.";
}
/* "Your mods don't match the world's: missing A; extra B." - the named part a notice opens with ("" when nothing can be named: a
   version difference, an unreadable list, nothing named). */
inline std::string ModWarnNamed(const ModDiff& d)
{
    if (!d.changed.empty()) return std::string();   /* T-246 fold: a version difference is never named to the player; the logs' ModDetailText names it */
    std::vector<std::string> parts;
    if (!d.missing.empty()) parts.push_back("missing " + ModNames(d.missing, kNamesInHeadline));
    if (!d.extra.empty())   parts.push_back("extra " + ModNames(d.extra, kNamesInHeadline));
    /* T-247 fold (1680); owner 1708 (2026-11-16, "just tell them how many mods can't be read... naming it is counterproductive"): the
       player is told only HOW MANY could not be read, never which slot or file - the log lines (ModDetailText) keep the names */
    if (!d.unreadable.empty())
    {
        char nb[48];
        std::sprintf(nb, "%u mod%s could not be read", (unsigned int)d.unreadable.size(), d.unreadable.size() == 1 ? "" : "s");
        parts.push_back(nb);
    }
    if (parts.empty()) return std::string();
    std::string s = "Your mods don't match the world's:";
    for (size_t i = 0; i < parts.size(); ++i) s += (i ? "; " : " ") + parts[i];
    return s + ".";
}
inline std::string ModWarnText(const ModDiff& d)
{
    const std::string tail(" You can still play, but some things may look or work differently for you than for the host.");
    const std::string named = ModWarnNamed(d);
    if (!named.empty()) return named + tail;
    if (d.changed.empty() && d.missing.empty() && d.extra.empty() && d.unreadable.empty() && d.orderDiffers) return "Your mods are the same as the world's but in a different order." + tail;
    return ModWarnPlainText();   /* nothing could be named */
}
/* T-246 fold (owner 201): the notice for a WELCOME that says "let in, differs" - plain when this game's own list could not be
   read or the notebook sent no list to compare with. */
inline std::string ModWarnNoticeText(int ownKnown, int haveWorld, const ModDiff& d)
{
    return (!ownKnown || !haveWorld) ? ModWarnPlainText() : ModWarnText(d);
}

/* ---- T-246 fold (review items 3, 5, 6): THE NOTICE'S LIFE IN THE GAME, as pure steps (store.cpp ModsWarnTick drives the UI) ----
   pending = waiting for the world; said = told this stay ("once"); showing = on screen. Never on screen with the host-left
   window: it is not raised while that window is up, and one already up is taken down and HELD (pending again) until that
   window goes. A notice that cannot be built is retried for kModsNoticeRetryMs from its first failure, then dropped.
   T-246 fold 2: "said" is marked when a notice is SHOWN (item 3), and the retry budget does not run while the notice is held
   behind the host-left window (item 5: holding / holdFrom, ModsNoticeHold). */
struct ModsNoticeState
{
    std::string pending, said, showing;
    int up, failing, holding;
    unsigned int failFirstAt, holdFrom;
    ModsNoticeState() : up(0), failing(0), holding(0), failFirstAt(0), holdFrom(0) {}
};
const unsigned int kModsNoticeRetryMs = 10000;
enum ModsNoticeAct { kMnNothing = 0, kMnShow = 1, kMnClose = 2, kMnYield = 3 };
/* A new stay (back at the title): forget what was said and any failures. A window still up is closed by the tick. */
inline void ModsNoticeNewStay(ModsNoticeState* s) { s->pending.clear(); s->said.clear(); s->failing = 0; s->failFirstAt = 0; s->holding = 0; s->holdFrom = 0; }
/* A WELCOME's verdict. differs=1: queue the text unless it was shown this stay, is already waiting or is on screen (1 queued,
   0 not). differs=0 (match, recorded, not checked): a pending notice is dropped (2 if one was, else 0); one already on screen
   stays until its OK. T-246 fold 2 (item 3): queuing does not mark it said - a notice dropped before it was shown (a later
   match, or given up after 10 s) is queued again by a later identical WELCOME. */
inline int ModsNoticeOnWelcome(ModsNoticeState* s, int differs, const std::string& text)
{
    if (!differs) { const int had = s->pending.empty() ? 0 : 2; s->pending.clear(); s->failing = 0; s->holding = 0; return had; }
    if (text == s->said || text == s->pending || (s->up && text == s->showing)) return 0;
    s->pending = text; s->failing = 0; s->holding = 0;
    return 1;
}
/* T-246 fold 2 (item 5): called every tick BEFORE ModsNoticeStep. While a pending notice waits behind the host-left window the
   retry budget is paused: the time held is added to failFirstAt when the hold ends. now = GetTickCount (wraps). */
inline void ModsNoticeHold(ModsNoticeState* s, int hostLeftUp, unsigned int now)
{
    const int held = (hostLeftUp && !s->up && !s->pending.empty()) ? 1 : 0;
    if (held && !s->holding) { s->holding = 1; s->holdFrom = now; }
    else if (!held && s->holding) { s->holding = 0; if (s->failing) s->failFirstAt += (unsigned int)(now - s->holdFrom); }
}
inline int ModsNoticeStep(const ModsNoticeState& s, int okClicked, int inWorld, int hostLeftUp, int worldReady)
{
    if (s.up)
    {
        if (okClicked || !inWorld) return kMnClose;
        return hostLeftUp ? kMnYield : kMnNothing;
    }
    return (!s.pending.empty() && !hostLeftUp && inWorld && worldReady) ? kMnShow : kMnNothing;
}
/* After the window was closed: a yield puts the text back to wait until the host-left window is gone. */
inline void ModsNoticeClosed(ModsNoticeState* s, int yielded)
{
    if (yielded && s->pending.empty()) s->pending = s->showing;
    s->up = 0; s->showing.clear();
}
/* After a show attempt (built=1: on screen). 0 shown (the first time: now it is said), 3 shown AGAIN after a hold behind the
   host-left window (T-246 fold 2 item 6: not a second "once"), 1 try again later, 2 given up (the pending text dropped). now =
   a millisecond tick that wraps (GetTickCount). */
inline int ModsNoticeShown(ModsNoticeState* s, int built, unsigned int now)
{
    if (built)
    {
        const int again = (s->pending == s->said) ? 1 : 0;
        s->up = 1; s->showing = s->pending; s->said = s->pending; s->pending.clear(); s->failing = 0; s->holding = 0;
        return again ? 3 : 0;
    }
    if (!s->failing) { s->failing = 1; s->failFirstAt = now; }
    if ((unsigned int)(now - s->failFirstAt) >= kModsNoticeRetryMs) { s->pending.clear(); s->failing = 0; return 2; }
    return 1;
}

/* THE WORLD'S mods.txt: "known=1", "hash=..", "by=<who set it>", "at=<YYYY-MM-DD>", then one "a=<row>" per active mod and one
   "p=<row>" per plugin DLL. A row holding a line break is not written (a mod name never holds one). known=1 with no rows is a list of
   no mods - never the same as no file. */
inline std::string ModsOneLine(const std::string& s) { std::string o; for (size_t i = 0; i < s.size(); ++i) o += (s[i] == '\r' || s[i] == '\n') ? ' ' : s[i]; return o; }
inline std::string ModListToText(const ModList& m, const std::string& by, const std::string& at)
{
    std::string s = "# the world's mod list (T-247): set by an admin's USE MY MODS FOR THIS WORLD, or edited here by hand. Delete this file and the world has no list (nobody's mods are checked).\n";
    s += std::string("known=") + (m.known ? "1" : "0") + "\n";
    s += "hash=" + m.hash + "\n";
    if (!by.empty()) s += "by=" + ModsOneLine(by) + "\n";
    if (!at.empty()) s += "at=" + ModsOneLine(at) + "\n";
    for (size_t i = 0; i < m.active.size(); ++i)
        if (m.active[i].find_first_of("\r\n") == std::string::npos) s += "a=" + m.active[i] + "\n";
    for (size_t i = 0; i < m.plugins.size(); ++i)
        if (m.plugins[i].find_first_of("\r\n") == std::string::npos) s += "p=" + m.plugins[i] + "\n";
    return s;
}
/* False (and *m empty) unless the text says known=1 - a file that is absent, torn or unreadable is "no list". by / at: who set it and
   when ("" when the file names nobody - a hand-made file). */
inline bool ModListFromText(const std::string& t, ModList* m, std::string* by = 0, std::string* at = 0)
{
    *m = ModList();
    if (by) by->clear();
    if (at) at->clear();
    ModList r;
    std::string rb, ra;
    int sawKnown = 0;
    size_t pos = 0;
    while (pos < t.size())
    {
        size_t e = t.find('\n', pos);
        if (e == std::string::npos) e = t.size();
        std::string line = t.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty() || line[0] == '#') continue;
        if (line.compare(0, 6, "known=") == 0) { r.known = (line.substr(6) == "1") ? 1 : 0; sawKnown = 1; }
        else if (line.compare(0, 5, "hash=") == 0) r.hash = line.substr(5);
        else if (line.compare(0, 3, "by=") == 0) rb = line.substr(3);
        else if (line.compare(0, 3, "at=") == 0) ra = line.substr(3);
        else if (line.compare(0, 2, "a=") == 0) { if (r.active.size() < kModRowsMax) r.active.push_back(line.substr(2)); }
        else if (line.compare(0, 2, "p=") == 0) { if (r.plugins.size() < kModRowsMax) r.plugins.push_back(line.substr(2)); }
    }
    if (!sawKnown || !r.known) return false;
    *m = r;
    if (by) *by = rb;
    if (at) *at = ra;
    return true;
}
/* "2026-11-15" -> "15 Nov" (the MODS tab's date); anything else is shown as written. */
inline std::string ModsDateShown(const std::string& at)
{
    static const char* const mon[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    if (at.size() != 10 || at[4] != '-' || at[7] != '-') return at;
    for (size_t i = 0; i < 10; ++i) if (i != 4 && i != 7 && (at[i] < '0' || at[i] > '9')) return at;
    const int mm = (at[5] - '0') * 10 + (at[6] - '0'), dd = (at[8] - '0') * 10 + (at[9] - '0');
    if (mm < 1 || mm > 12 || dd < 1 || dd > 31) return at;
    char b[16]; std::sprintf(b, "%d %s", dd, mon[mm - 1]);
    return b;
}

/* ---- T-247 (the approved page, version 3): THE WORDS A PLAYER READS ABOUT THE WORLD'S LIST ---- */
/* section 5: an admin or the owner let in under Rejected with mods that differ */
inline std::string ModAdminLetInText(const ModDiff& d, int ownKnown)
{
    const std::string named = ownKnown ? ModWarnNamed(d) : std::string();
    return (named.empty() ? std::string("Your mods don't match the world's.") : named)
         + " You were let in because you are an admin of this world. You can change the world's list in GAME OPTIONS > MODS.";
}
/* section 3: an admin joining a world with no list, once */
inline std::string ModNoListAdminText() { return "This world has no mod list yet, so nobody's mods are checked. You can set it in GAME OPTIONS > MODS."; }
/* section 7 (version 2 section 5): the world's list changed while this game plays - told once */
inline std::string ModChangedMatchText(const std::string& by) { return "The world's mod list was changed by " + (by.empty() ? std::string("an admin") : by) + ". Your mods match."; }
inline std::string ModChangedDiffersText(const ModDiff& d, int ownKnown, int policy)
{
    const std::string base = ownKnown ? ModWarnText(d) : ModWarnPlainText();
    return policy == kModsMismatchRefuse ? base + " This world rejects players whose mods differ, so you will be turned away the next time you join." : base;
}
/* T-247 (owner 1699): WHICH PART OF THIS GAME'S OWN MOD LIST COULD NOT BE READ - settings.cpp ComputeFingerprint's failure points, kept
   for the log line a refused USE MY MODS FOR THIS WORLD writes (log only, no player-facing words). The list is "unreadable" (known=0, the
   refusal's case) when the active list cannot be read at all; one unreadable slot or plugin list leaves it readable ("?" rows). */
struct ModsReadFacts
{
    int noWorld;              /* GameWorldGlobal not in the address table: nothing was read */
    int activeWhole;          /* GameWorld+0x528, the whole active list, unreadable */
    int activeEmpty;          /* the active list read with no rows (it always holds the base mods) */
    std::vector<int> slots;   /* active slots that could not be read (i=N) */
    int pluginsUnreadable;    /* GameWorld+0x568, the installed list the plugin DLLs come from, unreadable (plugins=-1) */
    ModsReadFacts() : noWorld(0), activeWhole(0), activeEmpty(0), pluginsUnreadable(0) {}
};
inline std::string ModsUnreadablePartsText(const ModsReadFacts& f)
{
    std::vector<std::string> p;
    if (f.noWorld) p.push_back("GameWorldGlobal not in the address table");
    if (f.activeWhole) p.push_back("active list GameWorld+0x528 unreadable");
    if (f.activeEmpty) p.push_back("active list read with no rows");
    for (size_t i = 0; i < f.slots.size(); ++i) { char b[48]; std::sprintf(b, "active i=%d unreadable", f.slots[i]); p.push_back(b); }
    if (f.pluginsUnreadable) p.push_back("plugins=-1, GameWorld+0x568 unreadable");
    if (p.empty()) return "the failed part was not recorded";
    std::string s;
    for (size_t i = 0; i < p.size(); ++i) s += (i ? "; " : "") + p[i];
    return s;
}
inline std::string ModsUseRefusedLine(const ModsReadFacts& f) { return "[MODS] USE MY MODS refused: own mod list unreadable (" + ModsUnreadablePartsText(f) + ")"; }
/* the same mods in another order and nothing else - the refusal's and the MODS tab's own sentence, and where T-1375's PUT MY MODS IN THE
   WORLD'S ORDER button goes (modorder.h; on the MODS DON'T MATCH box - ui.cpp) */
inline bool ModsOrderOnly(const ModDiff& d) { return d.changed.empty() && d.missing.empty() && d.extra.empty() && d.unreadable.empty() && d.orderDiffers; }
/* the MODS tab's lines: " - " drawn as an em dash (U+2014 is inside the fonts' 8127-8217 range, panelstatus.h's note on kenshi_fonts.xml);
   the page's '>' in "GAME OPTIONS > MODS" stays ASCII (U+203A is outside that range) */
const char* const kModsDash = " \xE2\x80\x94 ";
/* the MODS tab (section 1): the world's list line and this game's line */
inline std::string ModsWorldLine(int haveList, const ModList& w, const std::string& by, const std::string& at, int byIsMe)
{
    if (!haveList) return "World's mods: none set yet \xE2\x80\x94 nobody's mods are checked. An admin can set them with the button below.";
    const std::string who = byIsMe ? std::string("you") : by;
    std::string tail;
    if (!who.empty())
    {
        tail = std::string(kModsDash) + "set by " + who;
        if (!at.empty()) tail += ", " + ModsDateShown(at);
    }
    if (w.active.empty() && w.plugins.empty()) return "World's mods: no mods" + tail;
    char b[64]; std::sprintf(b, "World's mods: %u mods, %u plugins", (unsigned)w.active.size(), (unsigned)w.plugins.size());
    return b + tail;
}
inline std::string ModsYourLine(int haveList, const ModList& w, const ModList& mine)
{
    if (!haveList) return std::string();
    if (!mine.known) return "Your mods: differ";
    if (ModListSame(w, mine)) return "Your mods: match";
    const ModDiff d = ModListDiff(w, mine);
    if (ModsOrderOnly(d)) return "Your mods: same mods, different order";   /* T-1375 (built separately) puts its PUT MY MODS IN THE WORLD'S ORDER button under this line */
    std::vector<std::string> parts;
    if (d.changed.empty())
    {
        if (!d.missing.empty()) parts.push_back("missing " + ModNames(d.missing, kNamesInHeadline));
        if (!d.extra.empty())   parts.push_back("extra " + ModNames(d.extra, kNamesInHeadline));
    }
    std::string s = "Your mods: differ";
    for (size_t i = 0; i < parts.size(); ++i) s += (i ? std::string("; ") : std::string(kModsDash)) + parts[i];
    return s;
}
/* T-247 (the approved page, version 2 sections 1-2 and version 3 section 4): the words of the MODS tab's button and of the two boxes */
const char* const kModsUseHover = "Makes your mods the world's mod list.\nPlayers already connected stay.";   /* the button's hover note: two lines of at most 60 */
const char* const kModsAskTitle = "WORLD'S MODS";
const char* const kModsAskText = "Replace the world's mod list with yours? Players already connected stay.";
const char* const kModsAdminJoinTitle = "JOIN AS ADMIN";   /* the MODS DON'T MATCH box's button and the box it opens */
const char* const kModsAdminJoinLine = "Enter this world's admin password.";
const char* const kModsAdminWrongLine = "That isn't this world's admin password.";

/* ---- T-318 (2026-09-30): WHICH RE_Kenshi.json ROWS ARE PLUGIN DLLs OF THIS GAME ----
   The plugin half of the list is the DLLs named in the available mods' RE_Kenshi.json files. Only RE_Kenshi reads those files
   and loads those DLLs, so a row is a plugin of this game ONLY while RE_Kenshi is loading plugins in this process. Without it
   (the mod started by SharedWastelandsLoader.dll; RE_Kenshi.dll not loaded, or the game started with --norekenshi) a RE_Kenshi.json
   names nothing that runs - a leftover from a player who once had RE_Kenshi is not a mod difference - and its rows are left out.
   Our OWN mod's rows are left out always, whoever loads them: SharedWastelands.dll and test DLLs such as KenshiCoopHello.dll are the
   co-op build, whose sameness is the protocol number and the same-build check (T-64), never a player's mod difference. Every
   other mod's rows are counted exactly as before while RE_Kenshi loads plugins.
   T-1238 (2026-11-14): a "Plugins" row counts only while its mod is TICKED (in the active list): RE_Kenshi loads a mod's
   Plugins DLLs only when it is ticked, and its PreloadPlugins DLLs while it is merely available (run T2383, Confirmed), so a
   "PreloadPlugins" row keeps counting for every available mod. */
const char* const kOwnModName = swnames::kModNameLower;   /* our mod's name in Kenshi's mod list, lower case (it is compared lower-cased) */
const char* const kOwnModDll  = swnames::kPluginDllLower;   /* our DLL, whichever mod's RE_Kenshi.json names it (a second copy of the mod) */
enum PluginRowVerdict { kPluginRowCounted = 0, kPluginRowOwnMod = 1, kPluginRowNoReKenshi = 2, kPluginRowNotTicked = 3 };

inline std::string ModLowerAscii(const std::string& s)
{
    std::string o(s);
    for (size_t i = 0; i < o.size(); ++i) if (o[i] >= 'A' && o[i] <= 'Z') o[i] = (char)(o[i] - 'A' + 'a');
    return o;
}
inline std::string ModBaseNameOf(const std::string& p)
{
    const size_t k = p.find_last_of("\\/");
    return k == std::string::npos ? p : p.substr(k + 1);
}
/* one RE_Kenshi.json row: modName = the mod whose folder holds the file, dll = the name as the file writes it,
   isPreloadKey = 1 for a "PreloadPlugins" row and 0 for a "Plugins" row, modTicked = 1 when the mod is in the active list */
inline int PluginRowDecide(int reKenshiLoadsPlugins, const std::string& modName, const std::string& dll, int isPreloadKey, int modTicked)
{
    if (ModLowerAscii(modName) == kOwnModName) return kPluginRowOwnMod;
    if (ModLowerAscii(ModBaseNameOf(dll)) == kOwnModDll) return kPluginRowOwnMod;
    if (!reKenshiLoadsPlugins) return kPluginRowNoReKenshi;
    if (!isPreloadKey && !modTicked) return kPluginRowNotTicked;
    return kPluginRowCounted;
}
/* T-1238 (way 5, 2026-11-15): THE DLL FILE NAME OF ONE RE_Kenshi.json ROW. A name whose base name has no '.' gets ".dll"
   added (KEP's own json names "kep-core-preload-x64" with no ending, so the row read checksum -1 on every game; the same
   files under another mod's json, named with ".dll", read a real checksum - report 2026-11-05, Seen). The row always reads
   "<name>.dll|<crc>"; a name that already has an ending ("x.dll", "x.asi") is kept as written. Folders before the base
   name are kept: "sub/x" -> "sub/x.dll". */
inline std::string PluginDllFileName(const std::string& jsonName)
{
    const std::string base = ModBaseNameOf(jsonName);
    if (base.empty() || base.find('.') != std::string::npos) return jsonName;
    return jsonName + ".dll";
}
/* T-1238 (way 6, 2026-11-15): THE PLUGIN ROWS THE LIST COMPARES. fullRows are "dll|crc|key|mod"; each is cut to "dll|crc"
   (which mod carries the file is detail), then sorted and de-duplicated, so one DLL named by two mods' jsons is ONE row -
   a game holding both mods and a game holding one of them list the same plugin. */
inline void PluginRowsCut(const std::vector<std::string>& fullRows, std::vector<std::string>* out)
{
    out->clear();
    for (size_t r = 0; r < fullRows.size(); ++r)
    {
        const std::string& row = fullRows[r];
        const size_t p1 = row.find('|');
        const size_t p2 = (p1 == std::string::npos) ? std::string::npos : row.find('|', p1 + 1);
        out->push_back(p2 == std::string::npos ? row : row.substr(0, p2));
    }
    std::sort(out->begin(), out->end());
    out->erase(std::unique(out->begin(), out->end()), out->end());
}
inline const char* PluginRowWhy(int verdict)
{
    if (verdict == kPluginRowOwnMod) return "our own mod's file (the same-build check covers it, not the mod list)";
    if (verdict == kPluginRowNoReKenshi) return "RE_Kenshi is not loading plugins in this game, so its RE_Kenshi.json loads nothing";
    if (verdict == kPluginRowNotTicked) return "its mod is not ticked, and RE_Kenshi loads a mod's Plugins DLLs only when it is ticked (T2383)";
    return "counted";
}
/* Is `arg` one of the process's arguments? The command line split as Windows splits it (spaces and tabs outside double quotes,
   the quotes dropped), argument 0 (the exe) skipped, an exact case-sensitive match - RE_Kenshi's dllmain.cpp compares
   "--norekenshi" the same way (wcscmp over CommandLineToArgvW, from argument 1). Backslash-quote escapes are not decoded; the
   one flag asked for holds neither (Read). */
inline bool CmdLineHasArg(const std::string& cmd, const char* arg)
{
    std::string cur;
    bool inQuote = false, any = false;
    int index = 0;
    for (size_t i = 0; i <= cmd.size(); ++i)
    {
        const bool end = (i == cmd.size());
        const char c = end ? ' ' : cmd[i];
        if (!end && c == '"') { inQuote = !inQuote; any = true; continue; }
        if (end || (!inQuote && (c == ' ' || c == '\t')))
        {
            if (any)
            {
                if (index > 0 && cur == arg) return true;
                ++index; cur.clear(); any = false;
            }
            continue;
        }
        cur += c; any = true;
    }
    return false;
}
/* RE_Kenshi loads plugins in this game: its DLL is loaded in THIS process (moduleLoaded - asked of the running process,
   never a file on disk: the benches keep RE_Kenshi.dll.off and a player may keep a leftover RE_Kenshi.dll outside
   Plugins_x64.cfg) and the game was not started with --norekenshi (RE_Kenshi then returns before loading anything). */
inline int ReKenshiLoadsPlugins(int moduleLoaded, int noReKenshiFlag) { return (moduleLoaded && !noReKenshiFlag) ? 1 : 0; }

}   /* namespace coopmods */

#endif
