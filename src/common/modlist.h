/* src/common/modlist.h - settings5 S5 (docs/design-settings1.md section 4; user decision 2026-09-26: every player's
 * mods must match EXACTLY - there is no allow-list): THE MOD LIST ON THE STORE LINK, ITS COMPARISON AND ITS WORDS.
 *
 * The list is the S1 fingerprint's own rows (settings.cpp ComputeFingerprint): the active mods in load order as
 * "name|version|crc", and the plugin DLLs named in the available mods' RE_Kenshi.json, sorted, as "dll|crc" - the
 * co-op mod itself left out (its compatibility is the protocol number). It travels at the end of the store HELLO
 * (store protocol 46). The notebook keeps the world's copy in the world's folder (mods.txt), and a HELLO whose list
 * differs is refused with kRefuseMods (storelink.h), the world's list riding in the REFUSE - so the refused game can
 * name what differs, in words a player can act on, on its own title screen.
 * T-246 (owner decision 199 b, 2026-09-29): FOR NOW a differing game is WARNED AND LET IN instead (kModsMismatchPolicyNow
 * below): the world's list rides in the WELCOME (kModsDiffers, store protocol 57) and the game tells its player once in its
 * world (ModWarnText, owner 200, titled MODS DON'T MATCH). The refusal above is kept whole behind that one constant (T-247).
 *
 * Pure: no global, no operating-system call, no engine header. Compiled into the plugin, the notebook and the offline
 * test exe, so the two ends cannot hold two ideas of what "the same mods" means. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_MODLIST_H
#define COOP_COMMON_MODLIST_H
#include "names.h"   /* the on-disk names (mod folder, files, window texts) */

#include <string>
#include <vector>
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
    ModDiff() : orderDiffers(0) {}
};

inline int ModIndexOfName(const std::vector<std::string>& rows, const std::string& name)
{
    for (size_t i = 0; i < rows.size(); ++i) if (ModRowName(rows[i]) == name) return (int)i;
    return -1;
}
inline void ModDiffRows(const std::vector<std::string>& world, const std::vector<std::string>& mine, ModDiff* d)
{
    for (size_t i = 0; i < world.size(); ++i)
    {
        const int j = ModIndexOfName(mine, ModRowName(world[i]));
        if (j < 0) d->missing.push_back(ModRowName(world[i]));
        else if (mine[(size_t)j] != world[i] || !ModRowComparable(world[i]) || !ModRowComparable(mine[(size_t)j]))
            d->changed.push_back(ModRowName(world[i]));   /* settings5 fold: an unreadable row is never the same */
    }
    for (size_t i = 0; i < mine.size(); ++i)
        if (ModIndexOfName(world, ModRowName(mine[i])) < 0) d->extra.push_back(ModRowName(mine[i]));
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
            if (ModRowName(world.active[i]) != ModRowName(mine.active[i])) { d.orderDiffers = 1; break; }
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

/* THE SENTENCE THE PLAYER READS, on the title screen and in the Multiplayer panel. It is written for someone who has
   never seen a log: what is wrong, which mods, and what to do. */
inline std::string ModRefusalText(const ModDiff& d)
{
    std::vector<std::string> parts;
    if (!d.missing.empty()) parts.push_back("missing " + ModNames(d.missing, kNamesInHeadline));
    if (!d.extra.empty())   parts.push_back("extra " + ModNames(d.extra, kNamesInHeadline));
    if (!d.changed.empty()) parts.push_back("different version of " + ModNames(d.changed, kNamesInHeadline));
    if (d.orderDiffers && d.missing.empty() && d.extra.empty()) parts.push_back("the same mods in a different load order");
    if (parts.empty()) parts.push_back("the two mod lists are not the same");
    std::string s = "Mod mismatch:";   /* words1: NoticeTitle keys MOD MISMATCH on this start */
    for (size_t i = 0; i < parts.size(); ++i) s += (i ? "; " : " ") + parts[i];
    s += ". Use the same mods as the host, in the same order, then restart Kenshi and join again.";
    return s;
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
    return "Your game could not read its own mod list, so it can't be checked against the host's. Restart Kenshi and try again.";
}

/* ---- settings5 fold (review-settings5 1a/1b/4): THE NOTEBOOK'S DECISION AT A GREETING ----
   Only the OPERATOR's game writes mods.txt. A readable list with no record yet is admitted PROVISIONALLY and re-checked
   the moment the operator's list is written (ModsRecheckRefuses). An unreadable list is refused when a record exists
   (it cannot be shown to match) and admitted unchecked when none does. */
enum ModsHelloVerdict
{
    kModsHelloAdmitUnchecked   = 0,   /* unreadable list, no record: nothing to compare with */
    kModsHelloMatch            = 1,
    kModsHelloRecord           = 2,   /* the operator: its list is written as the world's */
    kModsHelloProvisional      = 3,   /* readable, no record, not the operator: in, and re-checked at the next write */
    kModsHelloRefuseDiffers    = 4,
    kModsHelloRefuseUnreadable = 5,
    kModsHelloAdmitDiffers     = 6,   /* T-246 (owner 199 b): readable, differs from the record - let in and warned (ModsHelloDecidePolicy) */
    kModsHelloAdmitUnreadable  = 7,   /* T-246 fold (owner 201): this game's OWN list unreadable, the world has one - let in, shown the plain notice (warn only) */
    kModsHelloOperatorUnreadable = 8  /* T-246 fold 2 (manager decision: owner 201 is about joiners): the OPERATOR's own list unreadable, the world has one - let in silently (logged on both sides), never shown the notice; the world keeps its recorded list (warn only) */
};
inline int ModsHelloDecide(int listKnown, int haveRecord, int isOperator, int sameAsRecord)
{
    if (!listKnown) return haveRecord ? kModsHelloRefuseUnreadable : kModsHelloAdmitUnchecked;
    if (haveRecord && sameAsRecord) return kModsHelloMatch;
    if (isOperator) return kModsHelloRecord;
    if (!haveRecord) return kModsHelloProvisional;
    return kModsHelloRefuseDiffers;
}
/* After EVERY write of mods.txt: 1 = this connected game must go (its readable list is not the new record). A game whose
   list was unreadable is judged at its next greeting. */
inline int ModsRecheckRefuses(const ModList& record, const ModList& peer)
{
    return (peer.known && !ModListSame(record, peer)) ? 1 : 0;
}

/* ---- T-246 (owner decision 199 b, 2026-09-29): A GAME WHOSE MODS DIFFER IS WARNED AND LET IN ----
   This replaces the 2026-09-26 "mods must match exactly" refusal FOR NOW. The refusal path (kModsHelloRefuseDiffers, the
   REFUSE carrying the world's list, ModRefusalText's words on the title screen) is kept whole behind this ONE switch; turning
   it back on is T-247 and needs the owner's confirmation. A constant - not a player option, not a config key. A game that
   cannot read its OWN list is let in too (T-246 fold, owner 201) and shown the plain notice; it is refused only behind the
   refuse constant. */
enum ModsMismatchPolicy { kModsMismatchWarn = 0, kModsMismatchRefuse = 1 };
const int kModsMismatchPolicyNow = kModsMismatchWarn;   /* owner 199 b / T-247: warn and admit; kModsMismatchRefuse restores the refusal */
/* ModsHelloDecide under a policy: a readable list that differs from the record is admitted and warned under kModsMismatchWarn. */
inline int ModsHelloDecidePolicy(int listKnown, int haveRecord, int isOperator, int sameAsRecord, int policy)
{
    const int v = ModsHelloDecide(listKnown, haveRecord, isOperator, sameAsRecord);
    if (policy != kModsMismatchWarn) return v;
    if (v == kModsHelloRefuseDiffers) return kModsHelloAdmitDiffers;
    if (v == kModsHelloRefuseUnreadable) return isOperator ? kModsHelloOperatorUnreadable : kModsHelloAdmitUnreadable;   /* T-246 fold (owner 201); fold 2: the host is never shown the notice */
    return v;
}
/* T-246 fold (review item 8): the notebook's verdict when it may not know its operator yet. With no operator named (no --owner,
   no owner.txt) the first game to talk after its WELCOME becomes the operator and its list replaces mods.txt
   (AppointFirstOperator) - so under the warn policy a game joining then is not warned against the old list: a readable list
   is PROVISIONAL (re-checked when the operator's list is written) and an unreadable one is let in unchecked. The refuse
   policy is untouched (T-247). */
inline int ModsHelloDecideWorld(int listKnown, int haveRecord, int isOperator, int sameAsRecord, int policy, int operatorKnown)
{
    const int v = ModsHelloDecidePolicy(listKnown, haveRecord, isOperator, sameAsRecord, policy);
    if (operatorKnown || policy != kModsMismatchWarn) return v;
    if (v == kModsHelloAdmitDiffers) return kModsHelloProvisional;
    if (v == kModsHelloAdmitUnreadable) return kModsHelloAdmitUnchecked;
    return v;
}
/* T-246 fold (review item 7): the "differs, let in" line and modsAdmittedDiffers count only a game actually admitted (after
   the lobby, the profile pick and the lifetime limit). */
inline int ModsCountAdmittedDiffers(int verdict, int admitted)
{
    return (admitted && (verdict == kModsHelloAdmitDiffers || verdict == kModsHelloAdmitUnreadable)) ? 1 : 0;
}
/* ModsRecheckRefuses under a policy: under kModsMismatchWarn a connected game is never sent away by the re-check. */
inline int ModsRecheckRefusesPolicy(const ModList& record, const ModList& peer, int policy)
{
    return policy == kModsMismatchWarn ? 0 : ModsRecheckRefuses(record, peer);
}
/* T-246 fold 2 (item 2): a game let in PROVISIONALLY is re-checked once the world's list is decided - when a list is written, and
   at the first operator's appointment WHETHER OR NOT its list was written (it is not when it equals the saved list or cannot be
   read; the world then keeps the saved one). kMprNone = no world list, or this game's list unreadable; kMprMatch; kMprAdmitDiffers
   = differs under the warn policy: it stays, logged and counted as let in with differing mods; kMprRefused = differs under the
   refuse policy (T-247): refused. */
enum ModsProvisionalAct { kMprNone = 0, kMprMatch = 1, kMprAdmitDiffers = 2, kMprRefused = 3 };
inline int ModsProvisionalRecheck(int haveWorld, const ModList& world, const ModList& peer, int policy)
{
    if (!haveWorld || !peer.known) return kMprNone;
    if (ModListSame(world, peer)) return kMprMatch;
    return policy == kModsMismatchWarn ? kMprAdmitDiffers : kMprRefused;
}

/* T-246 (owner decision 200, exact words): THE NOTICE A JOINER LET IN WITH OTHER MODS READS, once, in its world. The kinds that
   apply, in this order, joined by "; " - up to kNamesInHeadline names each, then "and N more" (ModNames). The same mods in
   another order and nothing else is its own sentence. */
const char* const kModsWarnTitle = "MODS DON'T MATCH";   /* owner 200 - panelstatus.h NoticeTitle keys it on the two starts below */
/* T-246 fold (owner 201, 2026-09-29: "you can just keep it simple and say mods don't match"): the plain notice - for a version
   difference, anything that cannot be named, and a game whose own list could not be read. */
inline std::string ModWarnPlainText()
{
    return "Your mods don't match the host's. You can still play, but some things may look or work differently for you than for the host.";
}
inline std::string ModWarnText(const ModDiff& d)
{
    const std::string tail(" You can still play, but some things may look or work differently for you than for the host.");
    if (!d.changed.empty()) return ModWarnPlainText();   /* T-246 fold: a version difference is never named to the player; the logs' ModDetailText names it */
    std::vector<std::string> parts;
    if (!d.missing.empty()) parts.push_back("missing " + ModNames(d.missing, kNamesInHeadline));
    if (!d.extra.empty())   parts.push_back("extra " + ModNames(d.extra, kNamesInHeadline));
    if (parts.empty() && d.orderDiffers) return "Your mods are the same as the host's but in a different order." + tail;
    if (parts.empty()) return ModWarnPlainText();   /* nothing could be named */
    std::string s = "Your mods don't match the host's:";
    for (size_t i = 0; i < parts.size(); ++i) s += (i ? "; " : " ") + parts[i];
    return s + "." + tail;
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

/* The notebook's mods.txt: "known=1", "hash=..", then one "a=<row>" per active mod and one "p=<row>" per plugin DLL.
   A row holding a line break is not written (a mod name never holds one). */
inline std::string ModListToText(const ModList& m)
{
    std::string s = "# the world's mod list (settings5 S5): recorded from the operator's game; every joining game must match it exactly\n";
    s += std::string("known=") + (m.known ? "1" : "0") + "\n";
    s += "hash=" + m.hash + "\n";
    for (size_t i = 0; i < m.active.size(); ++i)
        if (m.active[i].find_first_of("\r\n") == std::string::npos) s += "a=" + m.active[i] + "\n";
    for (size_t i = 0; i < m.plugins.size(); ++i)
        if (m.plugins[i].find_first_of("\r\n") == std::string::npos) s += "p=" + m.plugins[i] + "\n";
    return s;
}
/* False (and *m empty) unless the text says known=1 - a file that is absent, torn or unreadable is "no record". */
inline bool ModListFromText(const std::string& t, ModList* m)
{
    *m = ModList();
    ModList r;
    int sawKnown = 0;
    size_t at = 0;
    while (at < t.size())
    {
        size_t e = t.find('\n', at);
        if (e == std::string::npos) e = t.size();
        std::string line = t.substr(at, e - at);
        at = e + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty() || line[0] == '#') continue;
        if (line.compare(0, 6, "known=") == 0) { r.known = (line.substr(6) == "1") ? 1 : 0; sawKnown = 1; }
        else if (line.compare(0, 5, "hash=") == 0) r.hash = line.substr(5);
        else if (line.compare(0, 2, "a=") == 0) { if (r.active.size() < kModRowsMax) r.active.push_back(line.substr(2)); }
        else if (line.compare(0, 2, "p=") == 0) { if (r.plugins.size() < kModRowsMax) r.plugins.push_back(line.substr(2)); }
    }
    if (!sawKnown || !r.known) return false;
    *m = r;
    return true;
}

/* ---- T-318 (2026-09-30): WHICH RE_Kenshi.json ROWS ARE PLUGIN DLLs OF THIS GAME ----
   The plugin half of the list is the DLLs named in the available mods' RE_Kenshi.json files. Only RE_Kenshi reads those files
   and loads those DLLs, so a row is a plugin of this game ONLY while RE_Kenshi is loading plugins in this process. Without it
   (the mod started by SharedWastelandsLoader.dll; RE_Kenshi.dll not loaded, or the game started with --norekenshi) a RE_Kenshi.json
   names nothing that runs - a leftover from a player who once had RE_Kenshi is not a mod difference - and its rows are left out.
   Our OWN mod's rows are left out always, whoever loads them: SharedWastelands.dll and test DLLs such as KenshiCoopHello.dll are the
   co-op build, whose sameness is the protocol number and the same-build check (T-64), never a player's mod difference. Every
   other mod's rows are counted exactly as before while RE_Kenshi loads plugins. */
const char* const kOwnModName = swnames::kModNameLower;   /* our mod's name in Kenshi's mod list, lower case (it is compared lower-cased) */
const char* const kOwnModDll  = swnames::kPluginDllLower;   /* our DLL, whichever mod's RE_Kenshi.json names it (a second copy of the mod) */
enum PluginRowVerdict { kPluginRowCounted = 0, kPluginRowOwnMod = 1, kPluginRowNoReKenshi = 2 };

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
/* one RE_Kenshi.json row: modName = the mod whose folder holds the file, dll = the name as the file writes it */
inline int PluginRowDecide(int reKenshiLoadsPlugins, const std::string& modName, const std::string& dll)
{
    if (ModLowerAscii(modName) == kOwnModName) return kPluginRowOwnMod;
    if (ModLowerAscii(ModBaseNameOf(dll)) == kOwnModDll) return kPluginRowOwnMod;
    if (!reKenshiLoadsPlugins) return kPluginRowNoReKenshi;
    return kPluginRowCounted;
}
inline const char* PluginRowWhy(int verdict)
{
    if (verdict == kPluginRowOwnMod) return "our own mod's file (the same-build check covers it, not the mod list)";
    if (verdict == kPluginRowNoReKenshi) return "RE_Kenshi is not loading plugins in this game, so its RE_Kenshi.json loads nothing";
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
