// itemlook.h - T-1449 (owner 1826 a, 2026-11-20; F1544 / F1545 / F1553): the [ITEMLOOK] line - a light, permanent watch for an
// item that looks black, so the next sighting (in a test or on a player's game) is caught in the log. The pure parts only: the
// verdicts on what was read, the line caps and the lines' text. The reads, the hooks and the follow-up live in
// coop-plugin/items.cpp; the offline suite tests this header (t_t1449_itemlook_*).
//
// Three roads write the same line kind:
//  1. an item's look build (the engine's setupItemMaterial): the material it ended with has no technique, or one of its first
//     texture units names a texture that is not loaded or is not the object the texture manager hands out for that name;
//  2. about 2 s after a copy's outfit is redressed (clothing.cpp ApplyGarmentsBody): an item of that copy whose material object is
//     no longer the material manager's object for its name (stale), or whose material is empty or not loaded;
//  3. the engine making a material again under a name it already made once this session (counted; the first names logged).
// Every line is capped: kLineCap black-looking lines a session, kPerTypeCap per item type (base sid), one cap line; the counts go
// on in the REPORT token itemLook[checked,blackLooking,remade,staleAfterRedress,queueDropped].
//
// T-1449 (owner 1842, F1782): THE STUCK-LOOK SWEEP - a permanent, log-only checker for the black states of F1782:
//  A  the look's colour unit (texture unit 0) holds the engine's stand-in ('black.dds' / 'nothing.dds') or any texture other than
//     the colour texture named in the look's own name ('<base>_<n>:<colour>.dds<normal>.dds[...]', decomp_840b80 :420-430);
//  B  unit 0 names a texture that is not there or not loaded (Resource state != 2);
//  C  unit 0 is blank (Ogre's TextureUnitState::isBlank: no name, or its load failed).
//  (D, the tint, has no reader in the mod; not checked.)
// Every kSweepMs (5 s) every loaded character's attached items' looks are read (each look once a sweep). A look first seen black is
// a SUSPECT (an item's colour texture normally arrives within ~0.4 s, F1782 'normal case', but a joiner's heavy load can hold it
// longer); a look still black at the first sweep kConfirmMs (15 s) or more after its first black reading is logged STUCK once (the
// real stuck looks lasted 92-105 s, T2659 / T2668); a shorter black is only counted (loadBlack[seen,clearedUnder15s]). A STUCK look
// later seen fine is CLEARED; a STUCK look no longer on any held item at a sweep is GONE (its last wearer went, F1784). A look the
// TEST-ONLY lever forced is confirmed at its first black reading (marked forced). `itemlook now` logs every black look at once
// (SNAPSHOT); the TEST-ONLY `itemlook force` puts one look into a black state to prove the checker sees it.
#ifndef COOP_COMMON_ITEMLOOK_H
#define COOP_COMMON_ITEMLOOK_H

#include <cstdio>
#include <map>
#include <string>

namespace coopitemlook {

const int kLineCap = 20;         /* black-looking lines a session (all roads together) */
const int kPerTypeCap = 3;       /* of them per item type (base sid; the item's name when the sid is unread) */
const int kRemadeNameCap = 60;   /* re-makes logged (each with its holder; owner 1842 - was 10 names); every re-make is counted */
const int kTexUnits = 3;         /* the material's technique 0 / pass 0 texture units looked at */
const unsigned long kFollowMs = 2000;   /* the follow-up after a copy's redress */
const int kFollowMax = 256;      /* copies waiting for their follow-up (one entry per copy; a newer redress moves its time) */
const int kFollowPerTick = 8;    /* follow-ups run in one frame */
const int kAttMax = 24;          /* a copy's attached items looked at */

/* why a follow-up item looks black; kWhyNone at a look build (its line carries no why) or when the item is fine */
enum { kWhyNone = 0, kWhyStale = 1, kWhyEmpty = 2, kWhyUnloaded = 3 };

inline const char* WhyWord(int why)
{
    switch (why) { case kWhyStale: return "stale"; case kWhyEmpty: return "empty"; case kWhyUnloaded: return "unloaded"; default: return ""; }
}

/* the look at its build: 1 black-looking. techs = the material's technique count (-1 unread); loaded[k] / sameObj[k] for texture
   unit k: 1 yes, 0 no, -1 unread or a unit that names no texture (never judged). *unit = the unit to name on the line. */
inline int BuildBlack(int techs, const int* loaded, const int* sameObj, int units, int* unit)
{
    *unit = 0;
    if (techs == 0) return 1;
    if (techs < 0) return 0;
    for (int k = 0; k < units && k < kTexUnits; ++k)
        if (loaded[k] == 0 || sameObj[k] == 0) { *unit = k; return 1; }
    return 0;
}

/* a follow-up item: mgr = what the material manager hands out for the material's name now (1 the same object, 0 another, 2 none,
   -1 unread); techs / passes / tus counts (-1 unread); matState the material's loading state (2 loaded, -1 unread); loaded[k] as
   above. Stale first, then empty, then unloaded; kWhyNone = fine. Unread values never make a finding. */
inline int FollowWhy(int mgr, int techs, int passes, int tus, int matState, const int* loaded, int units, int* unit)
{
    *unit = 0;
    if (mgr == 0 || mgr == 2) return kWhyStale;
    if (techs == 0 || passes == 0 || tus == 0) return kWhyEmpty;
    if (matState >= 0 && matState != 2) return kWhyUnloaded;
    for (int k = 0; k < units && k < kTexUnits; ++k)
        if (loaded[k] == 0) { *unit = k; return kWhyUnloaded; }
    return kWhyNone;
}

/* the black-looking line cap, MAIN THREAD */
struct LineCap
{
    long written;
    std::map<std::string, int> perType;
    LineCap() : written(0) {}
    /* 0 = not written (the session's or this type's cap is reached), 1 = write the line, 2 = write it, then the cap line */
    int Step(const std::string& typeKey)
    {
        if (written >= kLineCap) return 0;
        int& k = perType[typeKey];
        if (k >= kPerTypeCap) return 0;
        ++k;
        ++written;
        return written == kLineCap ? 2 : 1;
    }
};

/* a name as printed: cut to cap characters, a quote as ` and a line break as a space; empty as ? */
inline std::string Clean(const std::string& s, size_t cap)
{
    if (s.empty()) return "?";
    std::string r = s.size() > cap ? s.substr(0, cap) : s;
    for (size_t k = 0; k < r.size(); ++k)
    {
        if (r[k] == '\'') r[k] = '`';
        else if (r[k] == '\n' || r[k] == '\r') r[k] = ' ';
    }
    return r;
}

inline std::string Num(long long v) { char b[32]; std::sprintf(b, "%lld", v); return b; }
inline std::string Flag(int v) { return v == 1 ? "1" : v == 0 ? "0" : "?"; }

/* [ITEMLOOK] black-looking item: holder='<name>' (<kind> uid=<n>) item='<item>' type=<n> material='<name>' techs=<n>
   tex<k>='<name>' loaded=<0|1> sameObj=<0|1>[ why=stale|empty|unloaded]   (? = unread) */
inline std::string BlackLine(const std::string& holder, const std::string& kind, unsigned int uid, const std::string& item, int type,
                             const std::string& material, int techs, int unit, const std::string& tex, int loaded, int sameObj, int why)
{
    std::string r = "[ITEMLOOK] black-looking item: holder='" + Clean(holder, 64) + "' (" + kind + " uid=" + Num((long long)uid) + ") item='"
                  + Clean(item, 64) + "' type=" + Num((long long)type) + " material='" + Clean(material, 160) + "' techs=" + Num((long long)techs)
                  + " tex" + Num((long long)unit) + "='" + Clean(tex, 96) + "' loaded=" + Flag(loaded) + " sameObj=" + Flag(sameObj);
    if (why != kWhyNone) r += std::string(" why=") + WhyWord(why);
    return r;
}

inline std::string CapLine()
{
    return "[ITEMLOOK] black-looking item lines: cap reached (" + Num((long long)kLineCap)
         + ") - further ones are counted on the REPORT itemLook token only";
}

/* the k-th re-make (1-based, k <= kRemadeNameCap): the name, the item and the holder it was made for */
inline std::string RemadeLine(int k, const std::string& name, const std::string& item, const std::string& holder, const std::string& kind,
                              unsigned int uid)
{
    return "[ITEMLOOK] material made again under a name already made this session: name='" + Clean(name, 160) + "' item='" + Clean(item, 64)
         + "' holder='" + Clean(holder, 64) + "' (" + kind + " uid=" + Num((long long)uid) + ") (#" + Num((long long)k) + "; the first "
         + Num((long long)kRemadeNameCap) + " are logged, every one is counted as remade)";
}

/* once at plugin start: the two hooks and the Ogre lookups found */
/* T-1449 after T-1453: the row AppearanceBase_setupItemMaterial is optional:itemlook - not found (setupHook "NO ROW") or its
   hook not taken ("FAILED"), the whole watch is off (both hooks, the tick, the redress follow-ups) and the install line says so */
inline std::string OffNote(const char* setupHook)
{
    const std::string h = setupHook ? setupHook : "";
    if (h == "on") return std::string();
    if (h == "NO ROW") return " - itemlook is off (row AppearanceBase_setupItemMaterial not found)";
    return " - itemlook is off (the look-maker hook was not taken)";
}

inline std::string InstallLine(const char* setupHook, const char* cloneHook, int texMgr, int matMgr, int texByName, int matByName)
{
    return std::string("[ITEMLOOK] watch: setupItemMaterial=") + setupHook + " materialClone=" + cloneHook + " ogre[texMgr,matMgr,texByName,matByName]="
         + Flag(texMgr) + "," + Flag(matMgr) + "," + Flag(texByName) + "," + Flag(matByName) + " caps[lines,perType,remadeNames]="
         + Num((long long)kLineCap) + "," + Num((long long)kPerTypeCap) + "," + Num((long long)kRemadeNameCap)
         + OffNote(setupHook);
}

/* queueDropped: black-looking findings off the main thread lost because the 8-slot queue was full (counted, never logged) */
inline std::string ReportToken(long long checked, long long black, long long remade, long long stale, long long queueDropped)
{
    return " itemLook[checked,blackLooking,remade,staleAfterRedress,queueDropped]=" + Num(checked) + "," + Num(black) + "," + Num(remade) + ","
         + Num(stale) + "," + Num(queueDropped);
}

/* ---- T-1449 (owner 1842, F1782): the stuck-look sweep ---- */
const unsigned long kSweepMs = 5000;    /* the sweep's period (a stuck look lasted 92-95 s in T2659 / T2668; a normal load ~0.4 s) */
const unsigned long kConfirmMs = 15000; /* black this long (decided at the first sweep past it) = STUCK; shorter = a load, counted only */
const int kSweepLineCap = 60;           /* STUCK / CLEARED / GONE / forced-look lines a life (their own cap; every one counted) */
const int kCountEvery = 12;             /* the count line every 12th sweep (~1 min) */
const int kCountLineCap = 120;          /* count lines a life */
const int kSnapLineCap = 200;           /* SNAPSHOT look lines per `itemlook now` */
const int kSweepCharMax = 600;          /* characters read in one sweep */
const int kSubMax = 16;                 /* an item's sub-entities read */
const int kTrackMax = 4096;             /* looks remembered between sweeps (more: counted, not tracked) */

/* state A of unit 0 */
enum { kANo = 0, kAStandIn = 1, kAOther = 2 };
/* a tracked look's state */
enum { kTrFine = 0, kTrSuspect = 1, kTrStuck = 2 };
/* a sweep sighting's event */
enum { kEvNone = 0, kEvSuspect = 1, kEvStuck = 2, kEvCleared = 3, kEvPassed = 4, kEvGone = 5 };
/* the TEST-ONLY force kinds */
enum { kForceBad = 0, kForceBlack = 1, kForceNothing = 2, kForceBlank = 3, kForceUnload = 4 };

inline char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
inline bool SameNoCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t k = 0; k < a.size(); ++k) if (Lower(a[k]) != Lower(b[k])) return false;
    return true;
}
/* the engine's two stand-ins (F1782): black.dds (StaticObject_*), nothing.dds (Skinned_*) */
inline bool IsStandIn(const std::string& tex) { return SameNoCase(tex, "black.dds") || SameNoCase(tex, "nothing.dds"); }

/* the colour texture a look's own name names, FOR THE LINES ONLY (the verdict is StateA's prefix test): the look maker names it
   '<base>_<n>:' + colour + normal [+ ':' ...] (decomp_840b80 :420-430; T2659 'StaticObject_0:01_HI.ddsflat.dds',
   'Skinned_0:RagArmored_D_HI.ddsRagArmored_N_HI.dds'): the text after the first ':' up to and including the first '.dds' (any
   case); "" when the name has no such part */
inline std::string ExpectedColour(const std::string& look)
{
    const size_t c = look.find(':');
    if (c == std::string::npos) return std::string();
    for (size_t k = c + 1; k + 4 <= look.size(); ++k)
        if (look[k] == '.' && Lower(look[k + 1]) == 'd' && Lower(look[k + 2]) == 'd' && Lower(look[k + 3]) == 's')
            return k == c + 1 ? std::string() : look.substr(c + 1, k + 4 - (c + 1));
    return std::string();
}

/* the text after the look name's first ':' starts with tex (any case) */
inline bool NameStartsWith(const std::string& look, const std::string& tex)
{
    const size_t c = look.find(':');
    if (c == std::string::npos || tex.empty() || look.size() - (c + 1) < tex.size()) return false;
    return SameNoCase(look.substr(c + 1, tex.size()), tex);
}
/* state A from unit 0's texture name and the look's own name (review fold: by prefix, so a colour texture that is not a .dds is
   never a false 'other'): an empty (unread) unit 0 never makes a finding; a stand-in is A (standin) unless the look's name itself
   starts with that stand-in; when the name has a ':' part, a unit 0 name it does not start with is A (other); no ':' part - only
   the stand-in test */
inline int StateA(const std::string& tex0, const std::string& look)
{
    if (tex0.empty()) return kANo;
    const bool named = NameStartsWith(look, tex0);
    if (IsStandIn(tex0)) return named ? kANo : kAStandIn;
    if (look.find(':') != std::string::npos && look.size() > look.find(':') + 1 && !named) return kAOther;
    return kANo;
}
inline const char* AWord(int a) { return a == kAStandIn ? "standin" : a == kAOther ? "other" : "no"; }
/* black now: A, or B (loaded 0), or C (blank 1); unread (-1) B / C never count */
inline int BlackNow(int a, int loaded, int blank) { return (a != kANo || loaded == 0 || blank == 1) ? 1 : 0; }

/* one look's memory between sweeps */
struct Track
{
    int state;              /* kTr* */
    unsigned long since;    /* when it was first seen black in this spell (ms clock) */
    int forced;             /* kForce* when the TEST-ONLY lever made it black, else 0 */
    int forcedSeen;         /* a forced look's first sighting after the force was reported */
    Track() : state(kTrFine), since(0), forced(0), forcedSeen(0) {}
};
/* one sighting of a tracked look: black or not, at now. Black once = suspect; still black at a sighting confirmMs or more after its
   first black reading = STUCK (once); a forced look (t->forced) black = STUCK at once; a STUCK look seen fine = CLEARED; a suspect
   seen fine = PASSED (a load, counted only) */
inline int TrackStep(Track* t, int black, unsigned long now, unsigned long confirmMs)
{
    if (black)
    {
        if (t->state == kTrStuck) return kEvNone;
        if (t->state == kTrFine) t->since = now;
        if (t->forced != 0 || (t->state == kTrSuspect && now - t->since >= confirmMs)) { t->state = kTrStuck; return kEvStuck; }
        if (t->state == kTrFine) { t->state = kTrSuspect; return kEvSuspect; }
        return kEvNone;
    }
    if (t->state == kTrStuck) { t->state = kTrFine; return kEvCleared; }
    if (t->state == kTrSuspect) { t->state = kTrFine; return kEvPassed; }
    return kEvNone;
}
/* a tracked look NOT seen on any held item at a sweep: a STUCK one is GONE (its last wearer went); a suspect is dropped (kEvNone) */
inline int UnseenStep(Track* t)
{
    const int was = t->state;
    t->state = kTrFine;
    return was == kTrStuck ? kEvGone : kEvNone;
}

/* a plain line cap: 1 write, 0 over the cap (counted in dropped) */
struct Cap
{
    long written; long dropped; int cap;
    explicit Cap(int c) : written(0), dropped(0), cap(c) {}
    int Step() { if (written >= cap) { ++dropped; return 0; } ++written; return 1; }
};

inline int ForceKindOf(const std::string& w)
{
    if (w == "black") return kForceBlack;
    if (w == "nothing") return kForceNothing;
    if (w == "blank") return kForceBlank;
    if (w == "unload") return kForceUnload;
    return kForceBad;
}
inline const char* ForceWord(int k)
{
    switch (k) { case kForceBlack: return "black"; case kForceNothing: return "nothing"; case kForceBlank: return "blank"; case kForceUnload: return "unload"; default: return ""; }
}
/* the texture name the lever sets on unit 0 (kForceUnload sets none): the two stand-ins, and a name no game file carries (state C) */
inline const char* ForceTexName(int k)
{
    return k == kForceBlack ? "black.dds" : k == kForceNothing ? "nothing.dds" : k == kForceBlank ? "sw_itemlook_absent_t1449.dds" : "";
}

inline std::string MsOrQ(long long ms) { return ms < 0 ? std::string("?") : Num(ms); }
inline std::string ForcedTail(int forced) { return forced ? std::string(" forced=") + ForceWord(forced) : std::string(); }
/* B as printed: 1 = not loaded (loaded 0), 0 = loaded, ? unread */
inline std::string BWord(int loaded) { return Flag(loaded == 0 ? 1 : loaded == 1 ? 0 : -1); }

/* T-1449 attempt 2 diagnostic (F1851, log only): WHO MADE A LOOK, and whether a body rebuild destroyed one of its item models while
   it was young. The clone hook keeps, with a look's make time, the character the maker ran for (its uid, 0 = none found: an own
   player character or a character the mod never replicated) and the item; a re-make (the same name made again) starts a new record.
   A body rebuild (createBody with a body, decomp_539440.txt:233-261 destroys every attached item model) marks each look of the
   copy's attached items made less than kYoungMs ago (its texture may still be loading; F1782: ~0.4 s normally). The STUCK and
   SNAPSHOT lines carry the record. */
const unsigned long kYoungMs = 2000;   /* a look made less than this ago is young */
const int kRebuildLineCap = 40;        /* REBUILD lines a life (every rebuild counted) */
const int kRebuildNames = 3;           /* young looks named on one REBUILD line */
struct Maker
{
    int known;                 /* 1 = this name was made this session (the record below is read) */
    unsigned int uid;          /* the character the last make ran for (0 = none found) */
    long makes;                /* makes of this name this session (1 = never re-made) */
    std::string item;          /* the item the last make was for */
    long youngRebuilds;        /* body rebuilds that destroyed an item model of this look while it was young (since its last make) */
    unsigned int rebuildUid;   /* the first such rebuild's character */
    long long rebuildAfterMs;  /* ... and how long after the make it came */
    Maker() : known(0), uid(0), makes(0), youngRebuilds(0), rebuildUid(0), rebuildAfterMs(-1) {}
};
/* 1 = made less than kYoungMs ago (madeAgoMs -1 = unknown: never young) */
inline int IsYoung(long long madeAgoMs) { return (madeAgoMs >= 0 && madeAgoMs < (long long)kYoungMs) ? 1 : 0; }
/* ' madeBy=<uid> madeItem='<item>' makes=<n> youngRebuilds=<n>[ firstRebuildBy=<uid> afterMs=<n>]'; unknown: ' madeBy=? madeItem=?' */
inline std::string MakerTail(const Maker& m)
{
    if (!m.known) return " madeBy=? madeItem=?";
    std::string r = " madeBy=" + Num((long long)m.uid) + " madeItem='" + Clean(m.item, 64) + "' makes=" + Num((long long)m.makes);
    r += " youngRebuilds=" + Num((long long)m.youngRebuilds);
    if (m.youngRebuilds > 0) r += " firstRebuildBy=" + Num((long long)m.rebuildUid) + " afterMs=" + MsOrQ(m.rebuildAfterMs);
    return r;
}
/* one young look a rebuild destroys (for its line) */
struct YoungLook { std::string look, item; long long agoMs; Maker m; };
/* [ITEMLOOK] REBUILD #k uid=<n><cls> destroys young looks: msSinceFirstBuild=<n|?> sexRaceFirst=<0|1|?> models=<n> young=<n> look='..'
   (item='..' madeAgoMs=<n> madeBy=..)...; 40 lines at most. sexRace -1 = not a first-build-held copy (?) */
inline std::string RebuildLine(long k, unsigned int uid, const std::string& cls, long long msSinceFirstBuild, int sexRace, int models, int young,
                               const YoungLook* looks, int nLooks)
{
    std::string r = "[ITEMLOOK] REBUILD #" + Num((long long)k) + " uid=" + Num((long long)uid) + cls + " destroys young looks (made < "
                  + Num((long long)kYoungMs) + " ms ago): msSinceFirstBuild=" + MsOrQ(msSinceFirstBuild) + " sexRaceFirst=" + Flag(sexRace);
    r += " models=" + Num((long long)models) + " young=" + Num((long long)young);
    for (int j = 0; j < nLooks && j < kRebuildNames; ++j)
        r += " look='" + Clean(looks[j].look, 160) + "' (item='" + Clean(looks[j].item, 64) + "' madeAgoMs=" + MsOrQ(looks[j].agoMs) + MakerTail(looks[j].m) + ")";
    r += "; " + Num((long long)kRebuildLineCap) + " lines at most";
    return r;
}
/* the rebuilds' counts: withBody = createBody calls with a body the guard let through (rebuilds), withYoung = those that destroyed a
   young look's model, models = attached item models they destroyed, youngModels = of them with a young look, lines = REBUILD lines */
struct RebuildCounts
{
    long long withBody, withYoung, models, youngModels, lines;
    RebuildCounts() : withBody(0), withYoung(0), models(0), youngModels(0), lines(0) {}
};
inline std::string RebuildToken(const RebuildCounts& c)
{
    return " itemLookRebuild[withBody,withYoungLook,models,youngModels,lines]=" + Num(c.withBody) + "," + Num(c.withYoung) + "," + Num(c.models) + ","
         + Num(c.youngModels) + "," + Num(c.lines);
}

/* [ITEMLOOK] STUCK #k look='..' item='..' holder='..' (<kind> uid=n) tex0='..' expected='..' a=<standin|other|no> b=<0|1|?>
   c=<0|1|?> madeAgoMs=<n|?> <MakerTail> blackForMs=<n>[ forced=<kind>] */
inline std::string StuckLine(long k, const std::string& look, const std::string& item, const std::string& holder, const std::string& kind,
                             unsigned int uid, const std::string& tex0, const std::string& expected, int a, int loaded, int blank,
                             long long madeAgoMs, long long blackForMs, int forced, const Maker& m)
{
    std::string r = "[ITEMLOOK] STUCK #" + Num((long long)k) + " look='" + Clean(look, 160) + "' item='" + Clean(item, 64) + "' holder='" + Clean(holder, 64);
    r += "' (" + kind + " uid=" + Num((long long)uid) + ") tex0='" + Clean(tex0, 96) + "' expected='" + Clean(expected, 96) + "' a=" + AWord(a);
    r += " b=" + BWord(loaded) + " c=" + Flag(blank) + " madeAgoMs=" + MsOrQ(madeAgoMs) + MakerTail(m) + " blackForMs=" + Num(blackForMs) + ForcedTail(forced);
    return r;
}
/* [ITEMLOOK] CLEARED look='..' item='..' holder='..' (<kind> uid=n) tex0='..' blackForMs=<n>[ forced=<kind>] */
inline std::string ClearedLine(const std::string& look, const std::string& item, const std::string& holder, const std::string& kind, unsigned int uid,
                               const std::string& tex0, long long blackForMs, int forced)
{
    std::string r = "[ITEMLOOK] CLEARED look='" + Clean(look, 160) + "' item='" + Clean(item, 64) + "' holder='" + Clean(holder, 64) + "' (" + kind;
    r += " uid=" + Num((long long)uid) + ") tex0='" + Clean(tex0, 96) + "' blackForMs=" + Num(blackForMs) + ForcedTail(forced);
    return r;
}
/* a forced look's first sweep sighting found it NOT black (the engine undid the force before the sweep) */
inline std::string ForcedFineLine(const std::string& look, const std::string& tex0, int loaded, int blank, int forced)
{
    return "[ITEMLOOK] forced look seen NOT black by the sweep: look='" + Clean(look, 160) + "' tex0='" + Clean(tex0, 96) + "' loaded=" + Flag(loaded)
         + " blank=" + Flag(blank) + ForcedTail(forced);
}
/* [ITEMLOOK] GONE stuck look ended: no wearer left (held for <s> s) look='..'[ forced=<kind>] */
inline std::string GoneLine(const std::string& look, long long heldMs, int forced)
{
    return "[ITEMLOOK] GONE stuck look ended: no wearer left (held for " + Num(heldMs / 1000) + " s) look='" + Clean(look, 160) + "'" + ForcedTail(forced);
}
inline std::string SweepCapLine()
{
    return "[ITEMLOOK] sweep lines: cap reached (" + Num((long long)kSweepLineCap) + ") - further ones are counted on the REPORT itemLookSweep token only";
}

/* the sweep's counts (the count line and the REPORT token carry the same) */
struct SweepCounts
{
    long long sweeps, chars, items, looks, tracked, untracked, stuckNow, suspectNow, stuckNatural, stuckForced, cleared, gone, loadBlackSeen,
              loadBlackCleared, seenB, seenC, linesDropped, snapshots, forces;
    SweepCounts() : sweeps(0), chars(0), items(0), looks(0), tracked(0), untracked(0), stuckNow(0), suspectNow(0), stuckNatural(0), stuckForced(0),
                    cleared(0), gone(0), loadBlackSeen(0), loadBlackCleared(0), seenB(0), seenC(0), linesDropped(0), snapshots(0), forces(0) {}
};
/* chars / items / looks = the last sweep's; seenB / seenC = sightings of a look in state B / C (any sweep) */
inline std::string CountLine(const SweepCounts& c)
{
    std::string r = "[ITEMLOOK] sweep count: sweeps=" + Num(c.sweeps) + " chars=" + Num(c.chars) + " items=" + Num(c.items) + " looks=" + Num(c.looks);
    r += " tracked=" + Num(c.tracked) + " stuckNow=" + Num(c.stuckNow) + " suspectNow=" + Num(c.suspectNow);
    r += " stuckEver[natural,forced]=" + Num(c.stuckNatural) + "," + Num(c.stuckForced) + " cleared=" + Num(c.cleared) + " gone=" + Num(c.gone);
    r += " loadBlack[seen,clearedUnder15s]=" + Num(c.loadBlackSeen) + "," + Num(c.loadBlackCleared);
    r += " seen[b,c]=" + Num(c.seenB) + "," + Num(c.seenC) + " linesDropped=" + Num(c.linesDropped);
    return r;
}
inline std::string SweepReportToken(const SweepCounts& c)
{
    std::string r = " itemLookSweep[sweeps,looks,stuckNow,stuckNatural,stuckForced,cleared,gone,seenB,seenC,untracked,linesDropped,snapshots,forces]=";
    r += Num(c.sweeps) + "," + Num(c.looks) + "," + Num(c.stuckNow) + "," + Num(c.stuckNatural) + "," + Num(c.stuckForced) + ",";
    r += Num(c.cleared) + "," + Num(c.gone) + "," + Num(c.seenB) + "," + Num(c.seenC) + "," + Num(c.untracked) + ",";
    r += Num(c.linesDropped) + "," + Num(c.snapshots) + "," + Num(c.forces);
    r += " loadBlack[seen,clearedUnder15s]=" + Num(c.loadBlackSeen) + "," + Num(c.loadBlackCleared);
    return r;
}
/* the second install line: what the sweep can read and the lever can call */
inline std::string SweepInstallLine(int isBlank, int setTex, int unload)
{
    std::string r = std::string("[ITEMLOOK] sweep: every ") + Num((long long)(kSweepMs / 1000)) + " s, STUCK when black " + Num((long long)(kConfirmMs / 1000));
    r += std::string(" s or more (a forced look at once); states A=unit0 name B=unit0 loaded C=") + (isBlank ? "isBlank" : "NOT READ (no isBlank export)") + " D=not read (no tint reader)";
    r += " lever[setTextureName,unload]=" + Flag(setTex) + "," + Flag(unload) + " caps[sweepLines,snapshotLines,remade]=";
    r += Num((long long)kSweepLineCap) + "," + Num((long long)kSnapLineCap) + "," + Num((long long)kRemadeNameCap);
    return r;
}
inline const char* TrackWord(int st) { return st == kTrFine ? "fine" : st == kTrSuspect ? "suspect" : st == kTrStuck ? "stuck" : "new"; }
/* [ITEMLOOK] SNAPSHOT #n gameHours=<h> look='..' item='..' holder='..' (<kind> uid=n) holders=<n> tex0='..' expected='..' a= b= c=
   tracked=<fine|suspect|stuck|new> madeAgoMs=<n|?> <MakerTail>[ forced=<kind>] */
inline std::string SnapLine(long n, const std::string& hours, const std::string& look, const std::string& item, const std::string& holder,
                            const std::string& kind, unsigned int uid, int holders, const std::string& tex0, const std::string& expected, int a,
                            int loaded, int blank, int tracked, long long madeAgoMs, int forced, const Maker& m)
{
    std::string r = "[ITEMLOOK] SNAPSHOT #" + Num((long long)n) + " gameHours=" + hours + " look='" + Clean(look, 160) + "' item='" + Clean(item, 64);
    r += "' holder='" + Clean(holder, 64) + "' (" + kind + " uid=" + Num((long long)uid) + ") holders=" + Num((long long)holders);
    r += " tex0='" + Clean(tex0, 96) + "' expected='" + Clean(expected, 96) + "' a=" + AWord(a) + " b=" + BWord(loaded);
    r += " c=" + Flag(blank) + " tracked=" + TrackWord(tracked) + " madeAgoMs=" + MsOrQ(madeAgoMs) + MakerTail(m) + ForcedTail(forced);
    return r;
}
inline std::string SnapDoneLine(long n, const std::string& hours, long long chars, long long items, long long looks, long long black, long long a,
                                long long b, long long c, long long lines)
{
    std::string r = "[ITEMLOOK] SNAPSHOT #" + Num((long long)n) + " done gameHours=" + hours + " chars=" + Num(chars) + " items=" + Num(items);
    r += " looks=" + Num(looks) + " black=" + Num(black) + " [a,b,c]=" + Num(a) + "," + Num(b) + "," + Num(c) + " lines=" + Num(lines);
    r += " (cap " + Num((long long)kSnapLineCap) + ")";
    return r;
}

}   /* namespace coopitemlook */

#endif
