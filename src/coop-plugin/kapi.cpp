/* kapi.cpp - per-race rules, plugin side. See kapi.h and src/common/kapirules.h. Ported from the third public pull request
   (author: MassDivide), reworked: the rules travel as the world's race.* options, the hooks sit on address-table rows. T-1209 (owner
   1414 d): the world's rows come from the world's own kenshi_api.cfg (the world server reads it); no game sends race rows.

   THE HOOKS (installed at start, whatever the rules say; each acts only for a race the active rules name):
     InventorySection::isLimitedSlotCompatible (row InventorySection_isLimitedSlotCompatible) - block_slot and
       no_weapon_in_hand. The engine asks it on every road an item takes into a character's section: the drag, buy / sell /
       trade, loot and give, the section add, fitsAt and the save load (where a refused item is put in the bag). Win64
       bool(section, item). It judges only a character this game owns: another game's character (a copy, ItCcIsCopy) wears
       what its owner's game allowed, and this mod places that equipment through fitsAt (T-1157 review fold).
     the skill XP sink (row Kapi_SkillXpGain) addSkillXp(float* field, float xp, float cap), where every skill trainer ends,
       and the task XP adder (row Kapi_SkillXpTask) taskXp(CharStats*, other, float rate, StatsEnumerated) - jobs and first
       aid, which add inline. Both are linear in xp / rate, so the race's multiplier scales that argument (cap unchanged); a
       scale that leaves nothing skips the call. The sink takes a gain only up to 20, the task adder has no limit and does
       not call the sink (1.0.65 disassembly): kapi::ScaleXp and kapi::ScaleRate.
   The active rules are an immutable set swapped in whole on the main thread and read by the hooks on any thread; a replaced
   set is never freed, because a hook on another thread may still be reading it (a set is made only when the rules change). */
#include "kapi.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "coop_log.h"
#include "addresses.h"          /* coop::AddrReg, AddrAbs */
#include "command_channel.h"    /* PathNextToDll */
#include "u8file.h"             /* U8fopen: the DLL's folder may hold non-ANSI letters */
#include "hooks.h"              /* AddHook */
#include "store.h"              /* StoreLinkGen, StoreWelcomedThisLink, StoreLinkIsUp */
#include "config.h"             /* RoleIsSingle */

class Character;
namespace coop
{
    int KapiSectionOwner(void* sec, void** character, char* name, size_t cap);   /* items.cpp */
    int KapiRaceSidOf(::Character* c, char* out, int cap);                        /* appearance.cpp */
}

namespace
{
    const char* const kRulesFile = "kenshi_api.cfg";
    const long kMaxFileBytes = 1024 * 1024;   /* a rules file is small; a larger one is refused, not half read */

    unsigned long long g_slotRva = 0; coop::AddrReg g_slotReg("InventorySection_isLimitedSlotCompatible", &g_slotRva);
    unsigned long long g_xpSinkRva = 0; coop::AddrReg g_xpSinkReg("Kapi_SkillXpGain", &g_xpSinkRva);
    unsigned long long g_xpTaskRva = 0; coop::AddrReg g_xpTaskReg("Kapi_SkillXpTask", &g_xpTaskRva);
    unsigned long long g_csVftRva = 0; coop::AddrReg g_csVftReg("CharStats_vftable", &g_csVftRva);
    unsigned long long g_csAnimalVftRva = 0; coop::AddrReg g_csAnimalVftReg("CharStatsAnimal_vftable", &g_csAnimalVftRva);

    /* THE ACTIVE RULES. Written on the main thread only (ApplyRows), read by the hooks with an interlocked read. */
    kapi::Rules* volatile g_active = 0;
    enum { kSrcNone = 0, kSrcOwn = 1, kSrcWorld = 2 };
    int         g_activeSrc = kSrcNone;     /* MAIN THREAD */
    std::string g_activeText;               /* MAIN THREAD: kapi::RowsText of the active set */
    long long   g_applies = 0;

    kapi::Rules g_own;                      /* this game's file, read once at start */
    kapi::Rows  g_ownRows;                  /* its option rows */
    int         g_ownRead = 0;              /* 1 = a file was read */

    kapi::Rows  g_mapRows;                  /* MAIN THREAD: the race.* rows of the map being read */
    long long   g_mapBadRows = 0;
    /* T-1157 review fold: the world's rules belong to the link they came on */
    int         g_activeGen = -1;           /* MAIN THREAD: the link generation a world set (kSrcWorld) was taken on */
    long long   g_worldDropped = 0;

    int g_slotHookOn = 0, g_xpHookOn = 0, g_taskHookOn = 0;
    volatile LONG64 g_slotRefused[kapi::kSlotCount];
    volatile LONG64 g_slotRefusedNoWeapon = 0;
    volatile LONG64 g_xpScaled = 0, g_taskScaled = 0, g_xpSkipped = 0;
    /* with a rule set active: equip checks left to a copy's owner, and the checks / XP owners whose owner or race could not be read */
    volatile LONG64 g_slotCopy = 0, g_slotOwnerUnread = 0, g_slotRaceUnread = 0, g_xpOwnerUnread = 0, g_xpRaceUnread = 0;

    const char* SrcName(int s) { return s == kSrcOwn ? "this game's kenshi_api.cfg" : s == kSrcWorld ? "the world's options" : "none"; }

    const kapi::Rules* ActiveRules()
    {
        return (const kapi::Rules*)::InterlockedCompareExchangePointer((PVOID volatile*)&g_active, 0, 0);
    }

    /* MAIN THREAD. Makes the rows the active rules, unless they already are. */
    void ApplyRows(const kapi::Rows& rows, int src, const char* why)
    {
        const std::string text = kapi::RowsText(rows);
        if (src == g_activeSrc && text == g_activeText) return;
        kapi::Rules* next = new kapi::Rules();
        const int bad = next->FromRows(rows);
        ::InterlockedExchangePointer((PVOID volatile*)&g_active, (PVOID)next);
        g_activeSrc = src;
        g_activeText = text;
        ++g_applies;
        char l[320];
        _snprintf_s(l, sizeof(l), _TRUNCATE, "[KAPI] race rules now from %s (%s): %d race(s)%s", SrcName(src), why, next->RaceCount(),
                    bad > 0 ? " - some rows did not decode and are left out" : "");
        if (bad > 0) ErrorLog(l); else DebugLog(l);
        for (int i = 0; i < next->RaceCount(); ++i)
            DebugLog("[KAPI] rule applied: race " + kapi::Rules::Describe(next->RaceAt(i)));
    }

    /* this game's own file: solo play's rules, and the file HOST hands the world server (--host-rules, ui.cpp) for a NEW world */
    void LoadOwnFile()
    {
        const std::string path = coop::PathNextToDll(kRulesFile);
        FILE* f = U8fopen(path.c_str(), "rb");
        if (f == 0)
        {
            DebugLog(std::string("[KAPI] no ") + kRulesFile + " beside the DLL - this game has no race rules of its own");
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
            ErrorLog(std::string("[KAPI] ") + kRulesFile + " is larger than 1 MB - NOT read, no race rules of this game's own");
            return;
        }
        g_own.ParseText(text);
        g_ownRead = 1;
        std::vector<std::string> tooLong, tooMany;
        g_own.ToRows(&g_ownRows, &tooLong, &tooMany);
        const kapi::Report& rep = g_own.GetReport();
        char line[256];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "[KAPI] %s: %d lines, %d rules for %d races, %d skipped; %d race row(s) for the world",
                    kRulesFile, rep.lines, rep.rules, g_own.RaceCount(), rep.skipped, (int)g_ownRows.size());
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
        for (size_t i = 0; i < tooLong.size(); ++i)
            ErrorLog("[KAPI] race " + tooLong[i] + ": its rules are longer than one world option holds - this race's rules are NOT used");
        for (size_t i = 0; i < tooMany.size(); ++i)
            ErrorLog("[KAPI] race " + tooMany[i] + ": past the world's limit of 32 races - this race's rules are NOT used");
    }

    /* ---- HOOK 1: block_slot and no_weapon_in_hand ---- */
    typedef bool (*SlotCompatFn)(void* sec, void* item);
    SlotCompatFn g_origSlotCompat = 0;
    const size_t kSecLimitedSlot = 0xB8;  /* InventorySection::limitedSlot: 7 = general storage, else the AttachSlot it takes */

    int SecLimitedSlotPod(void* sec)
    {
        __try { return *(const int*)((const char*)sec + kSecLimitedSlot); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 7; }
    }

    bool detour_kapiSlotCompat(void* sec, void* item)
    {
        const bool r = g_origSlotCompat(sec, item);
        if (!r || sec == 0) return r;
        const kapi::Rules* rules = ActiveRules();
        if (rules == 0 || rules->RaceCount() == 0) return r;
        const int limited = SecLimitedSlotPod(sec);
        if (limited == 7) return r;                       /* general storage: the busy road, untouched */
        char name[40];
        void* ch = 0;
        const int so = coop::KapiSectionOwner(sec, &ch, name, sizeof(name));
        if (so == 2) { ::InterlockedIncrement64(&g_slotCopy); return r; }   /* another game's character: its owner's game is the judge */
        if (so == -1) { ::InterlockedIncrement64(&g_slotOwnerUnread); return r; }
        if (so != 1) return r;                            /* a character's own section only */
        int slot = kapi::SlotOfAttach(limited);           /* the section's own AttachSlot first ("back" may be a pack or a sheath) */
        if (slot < 0) slot = kapi::SlotOfSection(name);
        if (slot < 0) return r;
        char sid[kapi::kMaxSidLen + 1];
        const int rs = coop::KapiRaceSidOf((::Character*)ch, sid, (int)sizeof(sid));
        if (rs != 1) { if (rs == 0) ::InterlockedIncrement64(&g_slotRaceUnread); return r; }   /* 2 = an id longer than any rule names */
        const kapi::RaceRules* rr = rules->Find(std::string(sid));
        if (rr == 0) return r;
        const bool slotOk = kapi::Rules::SlotAllowedR(rr, slot);
        const bool weaponOk = slot != kapi::kSlotWeapon || kapi::Rules::WeaponInHandAllowedR(rr);
        if (slotOk && weaponOk) return r;
        const LONG64 n = slotOk ? ::InterlockedIncrement64(&g_slotRefusedNoWeapon) : ::InterlockedIncrement64(&g_slotRefused[slot]);
        if (n <= 5)
        {
            char l[256];
            _snprintf_s(l, sizeof(l), _TRUNCATE, "[KAPI] refused: race '%s' may not use section '%s' (slot %s, %s)", sid, name,
                        kapi::SlotName(slot), slotOk ? "no_weapon_in_hand" : "block_slot");
            DebugLog(l);
        }
        return false;
    }

    /* ---- HOOKS 2 and 3: skill_xp ---- */
    typedef void (*SkillXpAddFn)(float* field, float xp, float cap);
    typedef void (*TaskXpFn)(void* stats, void* other, float rate, int stat);
    SkillXpAddFn g_origSkillXpAdd = 0;
    TaskXpFn     g_origTaskXp = 0;
    unsigned long long g_csVft = 0, g_csAnimalVft = 0;    /* absolute CharStats vftables */
    const size_t kCsOwner = 0x10;      /* CharStats -> its Character */
    const size_t kCsMedical = 0x8;     /* CharStats -> the Character's embedded MedicalSystem (Character + 0x458) */
    const size_t kChMedical = 0x458;   /* Character::medical (game/gamelayout.h Character_medical) */
    const size_t kChStats = 0x450;     /* Character::stats (game/gamelayout.h Character_stats) */

    /* the CharStats a float field belongs to, and its skill: the one field offset whose base holds a CharStats vftable */
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
    /* the owning Character, accepted only when its stats pointer points back at these stats; two ways to find it are tried */
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

    /* per-CharStats race-rules cache: XP ticks are frequent. An entry holds for 2 s (a freed CharStats reused for another
       character) and only for the rule set it was read under. */
    struct XpCacheEntry { void* stats; const kapi::Rules* rules; const kapi::RaceRules* rr; DWORD tick; };
    XpCacheEntry     g_xpCache[64];
    CRITICAL_SECTION g_xpCs;
    const kapi::RaceRules* XpRulesOf(void* stats, const kapi::Rules* rules)
    {
        const DWORD now = ::GetTickCount();
        const size_t k = ((size_t)stats >> 4) & 63;
        ::EnterCriticalSection(&g_xpCs);
        XpCacheEntry e = g_xpCache[k];
        ::LeaveCriticalSection(&g_xpCs);
        if (e.stats == stats && e.rules == rules && now - e.tick < 2000) return e.rr;
        const kapi::RaceRules* rr = 0;
        ::Character* c = XpOwnerPod(stats);
        char sid[kapi::kMaxSidLen + 1];
        const int rs = c != 0 ? coop::KapiRaceSidOf(c, sid, (int)sizeof(sid)) : -1;
        if (rs == 1) rr = rules->Find(std::string(sid));
        else if (rs == -1) ::InterlockedIncrement64(&g_xpOwnerUnread);   /* once per cache entry (2 s), not per tick */
        else if (rs == 0) ::InterlockedIncrement64(&g_xpRaceUnread);
        e.stats = stats; e.rules = rules; e.rr = rr; e.tick = now;
        ::EnterCriticalSection(&g_xpCs);
        g_xpCache[k] = e;
        ::LeaveCriticalSection(&g_xpCs);
        return rr;
    }
    /* xp after the race's multiplier for that skill; xp itself when no rule names this race and skill */
    float XpScale(void* stats, int skill, float xp, volatile LONG64* counter, bool task)
    {
        if (skill < 0 || skill >= kapi::kSkillCount) return xp;
        const kapi::Rules* rules = ActiveRules();
        if (rules == 0 || rules->RaceCount() == 0) return xp;
        const kapi::RaceRules* rr = XpRulesOf(stats, rules);
        if (rr == 0 || !rr->hasXp[skill]) return xp;
        const float m = rr->xpMult[skill];
        if (!(m >= 0.0f)) return xp;                         /* NaN / negative: the engine's own */
        const float out = task ? kapi::ScaleRate(xp, m) : kapi::ScaleXp(xp, m);
        if (::InterlockedIncrement64(counter) <= 5)
        {
            char l[200];
            _snprintf_s(l, sizeof(l), _TRUNCATE, "[KAPI] xp race '%s' %s x%.4f (%.4f -> %.4f)", rr->key.c_str(), kapi::SkillName(skill),
                        (double)m, (double)xp, (double)out);
            DebugLog(l);
        }
        return out;
    }

    void detour_kapiSkillXpAdd(float* field, float xp, float cap)
    {
        if (field != 0 && xp > 0.0f)
        {
            int skill = -1;
            void* stats = XpStatsOfFieldPod(field, &skill);
            if (stats != 0) xp = XpScale(stats, skill, xp, &g_xpScaled, false);   /* ScaleXp: a gain over the engine's 20 is left, a boost stops at 20 */
            else
            {
                const kapi::Rules* rules = ActiveRules();
                if (rules != 0 && rules->RaceCount() > 0) ::InterlockedIncrement64(&g_xpOwnerUnread);   /* no CharStats found for the field */
            }
            if (!(xp > 0.0f)) { ::InterlockedIncrement64(&g_xpSkipped); return; }   /* scaled to nothing: no call */
        }
        g_origSkillXpAdd(field, xp, cap);
    }
    void detour_kapiTaskXp(void* stats, void* other, float rate, int stat)
    {
        const int skill = kapi::SkillOfEngineStat(stat);
        if (stats != 0 && other != 0 && rate > 0.0f && skill >= 0 && XpIsStatsPod(stats))
        {
            rate = XpScale(stats, skill, rate, &g_taskScaled, true);   /* ScaleRate: no 20 limit on this road, scaled once */
            if (!(rate > 0.0f)) { ::InterlockedIncrement64(&g_xpSkipped); return; }
        }
        g_origTaskXp(stats, other, rate, stat);
    }

    /* One hook, one line: ON, or OFF and why (never silent). */
    int InstallOne(const char* what, const char* row, unsigned long long rva, void* detour, void** orig, const char* rule)
    {
        char l[320];
        if (rva == 0)
        {
            _snprintf_s(l, sizeof(l), _TRUNCATE, "[KAPI] hook %s: OFF - row %s is not bound for this Kenshi build - %s NOT applied", what, row, rule);
            ErrorLog(l);
            return 0;
        }
        const coop::HookStatus st = coop::AddHook((void*)coop::AddrAbs(rva), detour, orig);
        if (st != coop::SUCCESS)
        {
            _snprintf_s(l, sizeof(l), _TRUNCATE, "[KAPI] hook %s (row %s, rva 0x%llX): OFF - the install failed - %s NOT applied", what, row, rva, rule);
            ErrorLog(l);
            return 0;
        }
        _snprintf_s(l, sizeof(l), _TRUNCATE, "[KAPI] hook %s (row %s, rva 0x%llX): ON - %s", what, row, rva, rule);
        DebugLog(l);
        return 1;
    }

}

namespace coop
{
    void InstallKapi()
    {
        for (int i = 0; i < kapi::kSlotCount; ++i) g_slotRefused[i] = 0;
        ::InitializeCriticalSection(&g_xpCs);                 /* before any hook can call XpRulesOf */
        std::memset(g_xpCache, 0, sizeof(g_xpCache));
        LoadOwnFile();
        g_slotHookOn = InstallOne("equip check", "InventorySection_isLimitedSlotCompatible", g_slotRva, (void*)&detour_kapiSlotCompat,
                                  (void**)&g_origSlotCompat, "block_slot and no_weapon_in_hand");
        g_csVft = g_csVftRva != 0 ? coop::AddrAbs(g_csVftRva) : 0;
        g_csAnimalVft = g_csAnimalVftRva != 0 ? coop::AddrAbs(g_csAnimalVftRva) : 0;
        if (g_csVft == 0)
            ErrorLog("[KAPI] hook skill xp / task xp: OFF - row CharStats_vftable is not bound for this Kenshi build - skill_xp NOT applied");
        else
        {
            g_xpHookOn = InstallOne("skill xp", "Kapi_SkillXpGain", g_xpSinkRva, (void*)&detour_kapiSkillXpAdd, (void**)&g_origSkillXpAdd,
                                    "skill_xp for skill training");
            g_taskHookOn = InstallOne("task xp", "Kapi_SkillXpTask", g_xpTaskRva, (void*)&detour_kapiTaskXp, (void**)&g_origTaskXp,
                                      "skill_xp for jobs and first aid");
        }
        DebugLog("[KAPI] stat_cap and anim_speed rules are read and logged only - not applied");
        KapiWorldLoadBegan();
    }

    void KapiOptionsMapBegin() { g_mapRows.clear(); }

    void KapiWorldOption(const std::string& key, const std::string& value)
    {
        if (!kapi::OptionValueOk(key, value))
        {
            if (++g_mapBadRows <= 5) ErrorLog("[KAPI] the world's options hold a race row this build cannot read ('" + key + "') - left out");
            return;
        }
        g_mapRows.push_back(std::make_pair(key, value));
    }

    void KapiOptionsMapEnd(bool complete)
    {
        if (!complete) return;
        ApplyRows(g_mapRows, kSrcWorld, "the world's options map");
        g_activeGen = StoreLinkGen();   /* this set is this link's: a world load not on this welcomed link drops it (KapiWorldLoadBegan) */
    }

    /* T-1157 review fold: a world set is kept only for a load on the welcomed link it came on. Any other load (another
       world, or no link) starts with no rules; the E38 load gate, which runs after this, pumps the link and the new world's
       options map (sent right behind its WELCOME, before its CLOCK) applies that world's rules before the save is read. A
       link that drops during play keeps the rules: the world being played has not changed. */
    void KapiWorldLoadBegan()
    {
        if (RoleIsSingle()) { ApplyRows(g_ownRows, kSrcOwn, "solo play"); return; }
        if (g_activeSrc == kSrcOwn) { ApplyRows(kapi::Rows(), kSrcNone, "a multiplayer role: this game's own file is not used, the world's options bring the rules"); return; }
        if (g_activeSrc == kSrcWorld && (g_activeGen != StoreLinkGen() || StoreWelcomedThisLink() == 0 || !StoreLinkIsUp()))
        {
            ++g_worldDropped;
            ApplyRows(kapi::Rows(), kSrcNone, "a world load not on the welcomed link these rules came on: the world loaded brings its own");
        }
    }

    void ReportKapi()
    {
        const kapi::Rules* rules = ActiveRules();
        char l[768];
        _snprintf_s(l, sizeof(l), _TRUNCATE,
                    "[KAPI] REPORT source=%s races=%d hooks[slot,xp,task]=%d,%d,%d refused[weapon,armour,pants,shirt,boots,helmet,backpack,belt]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                    " refusedNoWeapon=%lld xpScaled=%lld taskScaled=%lld xpSkipped=%lld applies=%lld fileRead=%d fileRows=%d mapBadRows=%lld"
                    " copyLeft=%lld ownerUnread[slot,xp]=%lld,%lld raceUnread[slot,xp]=%lld,%lld worldDropped=%lld",
                    SrcName(g_activeSrc), rules != 0 ? rules->RaceCount() : 0, g_slotHookOn, g_xpHookOn, g_taskHookOn,
                    (long long)g_slotRefused[0], (long long)g_slotRefused[1], (long long)g_slotRefused[2], (long long)g_slotRefused[3],
                    (long long)g_slotRefused[4], (long long)g_slotRefused[5], (long long)g_slotRefused[6], (long long)g_slotRefused[7],
                    (long long)g_slotRefusedNoWeapon, (long long)g_xpScaled, (long long)g_taskScaled, (long long)g_xpSkipped, g_applies,
                    g_ownRead, (int)g_ownRows.size(), g_mapBadRows,
                    (long long)g_slotCopy, (long long)g_slotOwnerUnread, (long long)g_xpOwnerUnread, (long long)g_slotRaceUnread,
                    (long long)g_xpRaceUnread, g_worldDropped);
        DebugLog(l);
    }
}
