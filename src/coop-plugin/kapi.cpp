/* kapi.cpp - THE Kenshi API, plugin side (skeleton, 2026-10-19). See kapi.h and src/common/kapirules.h.

   WHAT IS HERE: the rules file is read and logged, the answers are exposed, and the five named hook points are walked.
   WHAT IS NOT: no engine function is hooked yet. A hook point is a table key name; the rows are NOT in any address table,
   and none is guessed. When RE finds a function, its row goes into every table (tools/check_tables.py demands that), its
   detour is written here and `detourReady` set - until then the rule is parsed, logged and NOT enforced.
   The keys are read with coop::Rva (0 = not in the table) and deliberately NOT bound with coop::AddrReg: a registered name
   the table lacks is a refusal that turns the whole mod off (addrBindMissing). */
#include "kapi.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "coop_log.h"
#include "addresses.h"          /* coop::Rva */
#include "command_channel.h"    /* PathNextToDll */
#include "u8file.h"             /* U8fopen: the DLL's folder may hold non-ANSI letters */
#include "hooks.h"              /* AddHook */

class Character;
namespace coop
{
    int  KapiSectionOwner(void* sec, void** character, char* name, size_t cap);   /* items.cpp */
    bool KapiRaceOf(::Character* c, std::string* sid, std::string* name);          /* appearance.cpp */
}

namespace
{
    const char* const kRulesFile = "kenshi_api.cfg";
    const long kMaxFileBytes = 1024 * 1024;   /* a rules file is small; a larger one is refused, not half read */

    kapi::Rules g_rules;
    int         g_loaded = 0;

    /* THE NAMED HOOK POINTS. key = the address-table row name the detour will be installed at. */
    struct HookPoint
    {
        const char* name;         /* for the log */
        const char* key;          /* address-table row name */
        const char* what;         /* the engine call it is meant to sit on */
        int         detourReady;  /* 1 once a detour with the function's real signature exists in this file */
    };
    const HookPoint kHookPoints[] = {
        { "inventory_can_equip", "InventorySection_fitsAt", "the equip check every add path asks, found via fitsAt (block_slot)",          1 },
        { "equip_weapon_hand",   "InventorySection_fitsAt", "the same check on the weapon sections: no weapon slot, no weapon to draw",  1 },
        { "skill_xp_gain",       "Kapi_SkillXpGain",       "skill XP gain (skill_xp)",                                   1 },
        { "stat_set_clamp",      "Kapi_StatSetClamp",      "stat set / clamp (stat_cap)",                                0 },
        { "anim_speed",          "Kapi_AnimSpeed",         "animation speed (anim_speed)",                               0 } };

    /* how many races carry a rule this hook point would enforce */
    int RacesUsing(int point)
    {
        int n = 0;
        for (int i = 0; i < g_rules.RaceCount(); ++i)
        {
            const kapi::RaceRules& r = g_rules.RaceAt(i);
            bool uses = false;
            switch (point)
            {
            case 0: uses = r.blockedSlots != 0; break;
            case 1: uses = r.noWeaponInHand; break;
            case 2: for (int k = 0; k < kapi::kSkillCount; ++k) if (r.hasXp[k]) uses = true; break;
            case 3: for (int k = 0; k < kapi::kSkillCount; ++k) if (r.hasCap[k]) uses = true; break;
            case 4: uses = r.hasAnim; break;
            default: break;
            }
            if (uses) ++n;
        }
        return n;
    }

    void LoadRules()
    {
        const std::string path = coop::PathNextToDll(kRulesFile);
        FILE* f = U8fopen(path.c_str(), "rb");
        if (f == 0)
        {
            DebugLog(std::string("[KAPI] no ") + kRulesFile + " beside the DLL - no rules, Kenshi's own behaviour");
            return;
        }
        std::string text;
        char buf[4096];
        size_t got;
        long total = 0;
        bool tooBig = false;
        while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0)
        {
            total += (long)got;
            if (total > kMaxFileBytes) { tooBig = true; break; }
            text.append(buf, got);
        }
        std::fclose(f);
        if (tooBig)
        {
            ErrorLog(std::string("[KAPI] ") + kRulesFile + " is larger than 1 MB - NOT read, no rules");
            return;
        }
        g_rules.ParseText(text);
        g_loaded = 1;
        const kapi::Report& rep = g_rules.GetReport();
        char line[256];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KAPI] %s: %d lines, %d rules for %d races, %d skipped",
                    kRulesFile, rep.lines, rep.rules, g_rules.RaceCount(), rep.skipped);
        DebugLog(line);
        for (size_t i = 0; i < rep.why.size(); ++i)
        {
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[KAPI] line %d skipped: %s", rep.why[i].line, rep.why[i].why.c_str());
            ErrorLog(line);
        }
        if (rep.skipped > (int)rep.why.size())
        {
            _snprintf_s(line, sizeof(line), _TRUNCATE, "[KAPI] ... and %d more skipped lines", rep.skipped - (int)rep.why.size());
            ErrorLog(line);
        }
        for (int i = 0; i < g_rules.RaceCount(); ++i)
            DebugLog("[KAPI] race " + kapi::Rules::Describe(g_rules.RaceAt(i)));
    }

    const kapi::RaceRules* FindRace(const char* sid, const char* name)
    {
        if (g_loaded == 0) return 0;
        return g_rules.Find(std::string(sid ? sid : ""), std::string(name ? name : ""));
    }

    /* HOOK 1 + 2: InventorySection::isLimitedSlotCompatible (1.0.68 0x74BC30) - THE predicate every add path asks: the GUI drag
       (fitsAt), the inventory add loop (loot, buy, spawn, auto-equip) and InventorySection::addItem. Win64 bool(section, item).
       The engine's answer stands unless it said yes for an equipment section (limitedSlot +0xB8 != 7) of a character whose race
       blocks that slot (block_slot) or, for a weapon section, carries no_weapon_in_hand: then no, and the add loop moves on to the
       next section (the bag). Any thread; reads only.
       FOUND, NOT TABLED: fitsAt (row InventorySection_fitsAt) calls it first thing - `call <thunk>; test al, al` - and the thunk
       jumps to it. Following that call needs no new table row and works on any build whose fitsAt has the same shape; a shape that
       differs leaves the hook out and says so. */
    typedef bool (*SlotCompatFn)(void* sec, void* item);
    SlotCompatFn g_origSlotCompat = 0;
    bool g_slotRules = false;            /* some race has block_slot or no_weapon_in_hand: written once at the start */
    volatile LONG64 g_slotRefused = 0;
    volatile LONG64 g_slotSeen = 0;      /* the first equipment sections asked about, logged to prove the names in game */
    const size_t kSecLimitedSlot = 0xB8;  /* InventorySection::limitedSlot: 7 = general storage, else the AttachSlot it takes */

    int SecLimitedSlotPod(void* sec)
    {
        __try { return *(const int*)((const char*)sec + kSecLimitedSlot); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 7; }
    }

    bool detour_kapiSlotCompat(void* sec, void* item)
    {
        const bool r = g_origSlotCompat(sec, item);
        if (!r || !g_slotRules || sec == 0) return r;
        const int limited = SecLimitedSlotPod(sec);
        if (limited == 7) return r;                       /* general storage: the hot path, untouched */
        char name[40];
        void* ch = 0;
        if (coop::KapiSectionOwner(sec, &ch, name, sizeof(name)) != 1) return r;
        int slot = kapi::SlotOfAttach(limited);                 /* the section's own AttachSlot first ("back" may be a pack or a sheath) */
        if (slot < 0) slot = kapi::SlotOfSection(name);
        if (::InterlockedIncrement64(&g_slotSeen) <= 12)
        {
            char l[160];
            _snprintf_s(l, sizeof(l), _TRUNCATE, "[KAPI] equip check: section '%s' limitedSlot %d -> slot %d", name, limited, slot);
            DebugLog(l);
        }
        if (slot < 0) return r;
        std::string sid, rname;
        if (!coop::KapiRaceOf((::Character*)ch, &sid, &rname)) return r;
        const kapi::RaceRules* rr = FindRace(sid.c_str(), rname.c_str());
        if (rr == 0) return r;
        bool ok = kapi::Rules::SlotAllowedR(rr, slot);
        if (ok && slot == kapi::kSlotWeapon && !kapi::Rules::WeaponInHandAllowedR(rr)) ok = false;
        if (ok) return r;
        if (::InterlockedIncrement64(&g_slotRefused) <= 20)
            DebugLog("[KAPI] refused: race '" + rname + "' (" + sid + ") may not use section '" + name + "'");
        return false;
    }

    /* fitsAt's first call - E8 rel32 followed by `test al, al` (84 C0) within its first 0x60 bytes - and, when that lands on a
       jmp thunk (E9 rel32), the thunk's target. 0 = not that shape. */
    unsigned char* SlotCompatFromFitsAt(unsigned char* fits)
    {
        __try
        {
            for (int i = 0; i < 0x60; ++i)
                if (fits[i] == 0xE8 && fits[i + 5] == 0x84 && fits[i + 6] == 0xC0)
                {
                    unsigned char* t = fits + i + 5 + *(const int*)(fits + i + 1);
                    if (t[0] == 0xE9) t = t + 5 + *(const int*)(t + 1);
                    return t;
                }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }

    void InstallSlotHook()
    {
        for (int i = 0; i < g_rules.RaceCount(); ++i)
            if (g_rules.RaceAt(i).blockedSlots != 0 || g_rules.RaceAt(i).noWeaponInHand) g_slotRules = true;
        if (!g_slotRules) { DebugLog("[KAPI] hook equip check: no race blocks a slot - not installed"); return; }
        const unsigned long long fitsRva = coop::Rva("InventorySection_fitsAt");
        if (fitsRva == 0) { DebugLog("[KAPI] hook equip check: no fitsAt row for this Kenshi build - block_slot / no_weapon_in_hand NOT enforced"); return; }
        unsigned char* const target = SlotCompatFromFitsAt((unsigned char*)coop::AddrAbs(fitsRva));
        if (target == 0) { DebugLog("[KAPI] hook equip check: fitsAt does not have the expected shape - NOT enforced"); return; }
        {   /* G1: the target is inside this image's code and starts with the 1.0.68 prologue, or nothing is hooked */
            static const unsigned char kPro[11] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57 };
            const unsigned long long t = (unsigned long long)target, lo = coop::AddrAbs(fitsRva) - fitsRva, hi = coop::AddrTextEnd();
            if (t <= lo || hi == 0 || t + sizeof(kPro) > hi || std::memcmp(target, kPro, sizeof(kPro)) != 0)
            {
                DebugLog("[KAPI] hook equip check: the call fitsAt makes does not land on the expected function - NOT enforced");
                return;
            }
        }
        const coop::HookStatus st = coop::AddHook((void*)target, (void*)&detour_kapiSlotCompat, (void**)&g_origSlotCompat);
        char l[200];
        _snprintf_s(l, sizeof(l), _TRUNCATE, "[KAPI] hook equip check (isLimitedSlotCompatible, rva 0x%llX via fitsAt): %s",
                    (unsigned long long)(target - (unsigned char*)coop::AddrAbs(fitsRva)) + fitsRva,
                    st == coop::SUCCESS ? "INSTALLED - block_slot and no_weapon_in_hand are enforced" : "install FAILED - not enforced");
        DebugLog(l);
    }

    /* HOOK 3: skill_xp. Kenshi 1.0.68 0x8C5DF0 addSkillXp(float* field, float xp, float cap) - the sink every skill trainer ends in -
       and 0x8C6130 taskXp(CharStats*, other, float rate, StatsEnumerated) - job / medic XP, which adds inline. Both gains are linear
       in xp / rate, so the race's multiplier scales the argument (cap unchanged); a scale that leaves nothing skips the call.
       The rows are OPTIONAL: a missing or mismatched one only turns skill_xp off. Nothing is installed unless a race has skill_xp. */
    typedef void (*SkillXpAddFn)(float* field, float xp, float cap);
    typedef void (*TaskXpFn)(void* stats, void* other, float rate, int stat);
    SkillXpAddFn g_origSkillXpAdd = 0;
    TaskXpFn     g_origTaskXp = 0;
    bool  g_xpRules = false;
    unsigned long long g_csVft = 0, g_csAnimalVft = 0;    /* absolute CharStats vftables */
    unsigned long long g_xpSinkRva = 0, g_xpTaskRva = 0;
    coop::AddrReg g_xpSinkReg("Kapi_SkillXpGain", &g_xpSinkRva, coop::kAddrUseOptional);
    coop::AddrReg g_xpTaskReg("Kapi_SkillXpTask", &g_xpTaskRva, coop::kAddrUseOptional);
    volatile LONG64 g_xpScaled = 0;
    const size_t kCsOwner = 0x10;      /* CharStats -> its Character (KAPI-3) */
    const size_t kCsMedical = 0x8;     /* CharStats -> the Character's embedded MedicalSystem (Character + 0x458) */
    const size_t kChMedical = 0x458;
    const size_t kChStats = 0x450;     /* Character::stats */

    void* XpStatsOfFieldPod(const float* field, int* skill)
    {
        int n = 0;
        const kapi::XpField* f = kapi::XpFields(&n);
        __try
        {
            for (int i = 0; i < n; ++i)
            {
                const char* base = (const char*)field - f[i].off;
                const unsigned long long v = *(const unsigned long long*)base;
                if (v == g_csVft || (g_csAnimalVft != 0 && v == g_csAnimalVft)) { *skill = f[i].skill; return (void*)base; }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }
    bool XpIsStatsPod(void* stats)
    {
        __try { const unsigned long long v = *(const unsigned long long*)stats; return v == g_csVft || (g_csAnimalVft != 0 && v == g_csAnimalVft); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    /* the owning Character, accepted only when its +0x450 points back at these stats; two ways to find it are tried */
    ::Character* XpOwnerPod(void* stats)
    {
        __try
        {
            char* c = *(char**)((char*)stats + kCsOwner);
            if (c != 0 && *(void**)(c + kChStats) == stats) return (::Character*)c;
            char* m = *(char**)((char*)stats + kCsMedical);
            if (m != 0) { c = m - kChMedical; if (*(void**)(c + kChStats) == stats) return (::Character*)c; }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }

    /* per-CharStats race-rules cache: XP ticks are frequent and KapiRaceOf copies two strings; 2 s expiry covers a freed CharStats
       reused for another character */
    struct XpCacheEntry { void* stats; const kapi::RaceRules* rr; DWORD tick; };
    XpCacheEntry     g_xpCache[64];
    CRITICAL_SECTION g_xpCs;
    const kapi::RaceRules* XpRulesOf(void* stats)
    {
        const DWORD now = ::GetTickCount();
        const size_t k = ((size_t)stats >> 4) & 63;
        ::EnterCriticalSection(&g_xpCs);
        XpCacheEntry e = g_xpCache[k];
        ::LeaveCriticalSection(&g_xpCs);
        if (e.stats == stats && now - e.tick < 2000) return e.rr;
        const kapi::RaceRules* rr = 0;
        ::Character* c = XpOwnerPod(stats);
        std::string sid, rname;
        if (c != 0 && coop::KapiRaceOf(c, &sid, &rname)) rr = FindRace(sid.c_str(), rname.c_str());
        e.stats = stats; e.rr = rr; e.tick = now;
        ::EnterCriticalSection(&g_xpCs);
        g_xpCache[k] = e;
        ::LeaveCriticalSection(&g_xpCs);
        return rr;
    }
    float XpScale(void* stats, int skill, float xp)
    {
        if (skill < 0 || skill >= kapi::kSkillCount) return xp;
        const kapi::RaceRules* rr = XpRulesOf(stats);
        if (rr == 0 || !rr->hasXp[skill]) return xp;
        const float m = rr->xpMult[skill];
        if (!(m >= 0.0f)) return xp;                         /* NaN / negative: the engine's own */
        if (::InterlockedIncrement64(&g_xpScaled) <= 20)
        {
            char l[160];
            _snprintf_s(l, sizeof(l), _TRUNCATE, "[KAPI] xp %s x%.2f (%.4f -> %.4f)", kapi::SkillName(skill), (double)m, (double)xp, (double)(xp * m));
            DebugLog(l);
        }
        return kapi::ScaleXp(xp, m);
    }

    void detour_kapiSkillXpAdd(float* field, float xp, float cap)
    {
        if (g_xpRules && field != 0 && xp > 0.0f)
        {
            int skill = -1;
            void* stats = XpStatsOfFieldPod(field, &skill);
            if (stats != 0) xp = XpScale(stats, skill, xp);   /* kapi::ScaleXp keeps a boosted gain at or under the engine's 20 */
            if (!(xp > 0.0f)) return;                        /* scaled to nothing: no call */
        }
        g_origSkillXpAdd(field, xp, cap);
    }
    void detour_kapiTaskXp(void* stats, void* other, float rate, int stat)
    {
        const int skill = kapi::SkillOfEngineStat(stat);
        if (g_xpRules && stats != 0 && other != 0 && rate > 0.0f && skill >= 0 && XpIsStatsPod(stats))
        {
            rate = XpScale(stats, skill, rate);
            if (!(rate > 0.0f)) return;
        }
        g_origTaskXp(stats, other, rate, stat);
    }

    void InstallXpHook()
    {
        bool any = false;
        for (int i = 0; i < g_rules.RaceCount() && !any; ++i)
            for (int k = 0; k < kapi::kSkillCount; ++k) if (g_rules.RaceAt(i).hasXp[k]) { any = true; break; }
        if (!any) { DebugLog("[KAPI] hook skill_xp: no race has a skill_xp rule - not installed"); return; }
        const unsigned long long vft = coop::Rva("CharStats_vftable"), vfa = coop::Rva("CharStatsAnimal_vftable");
        if (g_xpSinkRva == 0 || vft == 0) { DebugLog("[KAPI] hook skill_xp: Kapi_SkillXpGain or CharStats_vftable missing or unverified for this Kenshi build - skill_xp NOT enforced (unverified row)"); return; }
        g_csVft = coop::AddrAbs(vft);
        g_csAnimalVft = vfa != 0 ? coop::AddrAbs(vfa) : 0;
        ::InitializeCriticalSection(&g_xpCs);                 /* before any hook can call XpRulesOf */
        std::memset(g_xpCache, 0, sizeof(g_xpCache));
        g_xpRules = true;
        const coop::HookStatus a = coop::AddHook((void*)coop::AddrAbs(g_xpSinkRva), (void*)&detour_kapiSkillXpAdd, (void**)&g_origSkillXpAdd);
        coop::HookStatus b = coop::SUCCESS;
        if (g_xpTaskRva != 0) b = coop::AddHook((void*)coop::AddrAbs(g_xpTaskRva), (void*)&detour_kapiTaskXp, (void**)&g_origTaskXp);
        char l[220];
        _snprintf_s(l, sizeof(l), _TRUNCATE, "[KAPI] hook skill_xp (rva 0x%llX): %s; task xp (rva 0x%llX): %s", g_xpSinkRva,
                    a == coop::SUCCESS ? "INSTALLED" : "install FAILED", g_xpTaskRva,
                    g_xpTaskRva == 0 ? "no row - medic/job XP NOT scaled" : (b == coop::SUCCESS ? "INSTALLED" : "install FAILED"));
        DebugLog(l);
    }

    void WalkHookPoints()
    {
        const int n = (int)(sizeof(kHookPoints) / sizeof(kHookPoints[0]));
        char line[320];
        for (int i = 0; i < n; ++i)
        {
            const HookPoint& h = kHookPoints[i];
            const unsigned long long rva = coop::Rva(h.key);
            const int users = RacesUsing(i);
            if (rva == 0)
                _snprintf_s(line, sizeof(line), _TRUNCATE,
                            "[KAPI] hook %s: no address for this Kenshi build - rule parsed, not enforced (row %s, %d race(s) use it)",
                            h.name, h.key, users);
            else if (!h.detourReady)
                _snprintf_s(line, sizeof(line), _TRUNCATE,
                            "[KAPI] hook %s: row %s is in the table (rva 0x%llX) but its detour is not written yet - not installed, not enforced (%d race(s) use it)",
                            h.name, h.key, rva, users);
            else
                _snprintf_s(line, sizeof(line), _TRUNCATE, "[KAPI] hook %s: on row %s (rva 0x%llX), %d race(s) use it", h.name, h.key, rva, users);
            DebugLog(line);
        }
    }

}

namespace coop
{
    void InstallKapi()
    {
        LoadRules();
        WalkHookPoints();
        InstallSlotHook();
        InstallXpHook();
    }

    int KapiRuleCount() { return g_loaded ? g_rules.GetReport().rules : 0; }
    const kapi::Rules& KapiRules() { return g_rules; }

    bool KapiSlotAllowed(const char* raceSid, const char* raceName, int slot)
    {
        return kapi::Rules::SlotAllowedR(FindRace(raceSid, raceName), slot);
    }
    bool KapiWeaponInHandAllowed(const char* raceSid, const char* raceName)
    {
        return kapi::Rules::WeaponInHandAllowedR(FindRace(raceSid, raceName));
    }
    float KapiSkillXpMultiplier(const char* raceSid, const char* raceName, int skill)
    {
        return kapi::Rules::SkillXpMultiplierR(FindRace(raceSid, raceName), skill);
    }
    float KapiStatCap(const char* raceSid, const char* raceName, int skill)
    {
        return kapi::Rules::StatCapR(FindRace(raceSid, raceName), skill);
    }
    float KapiAnimSpeed(const char* raceSid, const char* raceName)
    {
        return kapi::Rules::AnimSpeedR(FindRace(raceSid, raceName));
    }
}
