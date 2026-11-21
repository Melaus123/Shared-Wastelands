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
#ifndef COOP_COMMON_ITEMLOOK_H
#define COOP_COMMON_ITEMLOOK_H

#include <cstdio>
#include <map>
#include <string>

namespace coopitemlook {

const int kLineCap = 20;         /* black-looking lines a session (all roads together) */
const int kPerTypeCap = 3;       /* of them per item type (base sid; the item's name when the sid is unread) */
const int kRemadeNameCap = 10;   /* material names made again that are logged (each name once); every re-make is counted */
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

/* the k-th material name made again (1-based, k <= kRemadeNameCap) */
inline std::string RemadeLine(int k, const std::string& name, const std::string& item)
{
    return "[ITEMLOOK] material made again under a name already made this session: name='" + Clean(name, 160) + "' item='" + Clean(item, 64)
         + "' (#" + Num((long long)k) + "; the first " + Num((long long)kRemadeNameCap) + " names are logged, every one is counted as remade)";
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

}   /* namespace coopitemlook */

#endif
