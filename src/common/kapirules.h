/* src/common/kapirules.h - THE Kenshi API's RULE ENGINE (skeleton, 2026-10-19).

   Mods restrict and tune things Kenshi's game data cannot express (a race's "races exclude" does not stop a weapon
   being equipped; race skill bonuses are capped at +-20% and do not stack). The rules are read from kenshi_api.cfg
   beside the plugin DLL (src/coop-plugin/kapi.cpp loads it; a sample is src/coop-plugin/kenshi_api.sample.cfg).

   PURE: no Windows.h, no engine, no globals, no I/O. The offline suite (src/coop-test) checks exactly what the plugin
   compiles. CO-OP: the rules are local data every game evaluates the same way; nothing here is ever sent.

   THE FILE, one rule per line, # starts a comment (outside quotes), blank lines are ignored:
       race <race> block_slot <slot>[|<slot>...] [<slot> ...]
       race <race> no_weapon_in_hand
       race <race> skill_xp <skill> <percent>        +40 = 1.4x the XP, -99 = 0.01x, -100 = none (range -100..+1000)
       race <race> stat_cap <skill> <max>            a ceiling for that stat (0..1000)
       race <race> anim_speed <multiplier>           0.05..10
   <race> is a Kenshi string ID (7100001-OCS-100.mod) or a race NAME; a name with spaces is "double quoted". Matching is
   ASCII case-insensitive. A query that names both (Find(sid, name)) uses the string ID's rules when there are any, else
   the name's. A later line for the same race and the same slot/skill/verb overrides the earlier one.
   Slots:  weapon armour pants shirt boots helmet backpack belt  (aliases: weapons armor legs shoes hat head bag)
   Skills: the Kenshi skill names with _ for spaces (heavy_weapons, melee_defence ...); a few aliases (thieving, medic,
           crossbows, armor_smith, defense ...).
   A line that does not parse is SKIPPED and counted (Report().skipped, the first kMaxWhy with their reason); it never
   stops the file being read.

   ADDING A VERB: write one VerbFn below and add one row to Verbs(). Nothing else changes.

   C++03 (VS2010 v100). Header-only. */
#pragma once

#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>

namespace kapi {

enum Slot
{
    kSlotWeapon = 0, kSlotArmour, kSlotPants, kSlotShirt, kSlotBoots, kSlotHelmet, kSlotBackpack, kSlotBelt,
    kSlotCount
};

/* A character inventory section's name (the engine's own: shirt, legs, armour, head, boots, backpack_attach, belt, and the weapon
   sections) -> the Slot a block_slot rule names; -1 = no slot (main, flat, anything else). Compared without case. A weapon section is
   any name with "weapon" or "hand" in it, or exactly "hip" or "back" (the sheath points). */
/* An equipment section's own limitedSlot (InventorySection +0xB8, Kenshi's AttachSlot) -> Slot; -1 = none or not certain.
   0 weapon, 1 back (a weapon carried on the back), 3 hat, 5 body, 6 legs, 8 shirt, 9 boots, 12 backpack, 14 belt; 7 = general storage. */
/* The Kenshi API's skill_xp hook (1.0.68): a CharStats field's offset -> the skill (getStatRef 0x884190), and the engine's
   StatsEnumerated -> the skill (the name switch 0x888440); -1 = none. Precision shooting and crossbows both map to precision_shooting. */
struct XpField { unsigned short off; signed char skill; };
inline const XpField* XpFields(int* count)
{
    static const XpField k[] = {
        {0x80,0},{0x90,1},{0x88,2},{0x8C,3},{0x120,4},{0x124,5},{0xF0,6},{0x10C,7},{0xF8,8},{0xFC,9},{0x100,10},{0x108,11},
        {0x104,12},{0x118,13},{0xF4,14},{0x110,14},{0x114,15},{0xA4,16},{0xB0,17},{0xAC,18},{0xB8,19},{0x94,20},{0xA8,21},
        {0xE0,22},{0xCC,23},{0xDC,24},{0x98,25},{0xD0,26},{0xD4,27},{0xD8,28},{0xE4,29},{0xE8,30},{0xEC,31} };
    *count = (int)(sizeof(k) / sizeof(k[0]));
    return k;
}
inline int SkillOfStatsField(unsigned off)
{
    int n = 0;
    const XpField* f = XpFields(&n);
    for (int i = 0; i < n; ++i) if (f[i].off == off) return f[i].skill;
    return -1;
}
inline int SkillOfEngineStat(int e)
{
    static const signed char k[39] = {
        -1, 0, 4,29,22,23,24,26,27,25, 18,15,30,31,25,25,16,20, 2, 5, -1, 1,19,21, 3, 8, 9,10,11,12,  7,-1, 6,-1,13,14,14,17,28 };
    return (e >= 0 && e < 39) ? k[e] : -1;
}

/* skill_xp: the gain after a race's multiplier. The engine applies a single gain only when 0 < xp <= 20 and throws a larger one away,
   so a boost never lifts a gain the engine would have taken past 20; a multiplier of 1 (or NaN / negative) leaves xp as it was. */
inline float ScaleXp(float xp, float m)
{
    if (!(m >= 0.0f) || m == 1.0f) return xp;
    const float out = xp * m;
    return (xp <= 20.0f && out > 20.0f) ? 20.0f : out;
}

inline int SlotOfAttach(int a)
{
    switch (a)
    {
    case 0: case 1: return kSlotWeapon;
    case 3:  return kSlotHelmet;
    case 5:  return kSlotArmour;
    case 6:  return kSlotPants;
    case 8:  return kSlotShirt;
    case 9:  return kSlotBoots;
    case 12: return kSlotBackpack;
    case 14: return kSlotBelt;
    default: return -1;
    }
}

inline int SlotOfSection(const char* name)
{
    if (name == 0 || name[0] == 0) return -1;
    char lo[40];
    size_t n = 0;
    for (; name[n] != 0 && n + 1 < sizeof(lo); ++n) lo[n] = (char)((name[n] >= 'A' && name[n] <= 'Z') ? name[n] - 'A' + 'a' : name[n]);
    lo[n] = 0;
    const std::string s(lo);
    if (s == "armour" || s == "armor" || s == "body") return kSlotArmour;
    if (s == "legs" || s == "pants") return kSlotPants;
    if (s == "shirt") return kSlotShirt;
    if (s == "boots") return kSlotBoots;
    if (s == "head" || s == "hat" || s == "helmet") return kSlotHelmet;
    if (s == "backpack_attach" || s == "backpack") return kSlotBackpack;
    if (s == "belt") return kSlotBelt;
    if (s == "hip" || s == "back" || s.find("weapon") != std::string::npos || s.find("hand") != std::string::npos) return kSlotWeapon;
    return -1;
}

enum { kMaxWhy = 16 };   /* skipped lines kept with their reason */

inline const char* SlotName(int slot)
{
    static const char* const k[kSlotCount] = { "weapon", "armour", "pants", "shirt", "boots", "helmet", "backpack", "belt" };
    return (slot >= 0 && slot < kSlotCount) ? k[slot] : "?";
}

/* THE GAME'S SKILLS, in a fixed order of our own (NOT the engine's StatsEnumerated numbers - mapping them is the RE work
   that wires a hook point). */
enum
{
    kSkillCount = 32
};
inline const char* SkillName(int skill)
{
    static const char* const k[kSkillCount] = {
        "strength", "toughness", "dexterity", "perception",
        "melee_attack", "melee_defence", "dodge", "martial_arts",
        "katanas", "sabres", "hackers", "heavy_weapons", "blunt", "polearms",
        "precision_shooting", "turrets",
        "stealth", "lockpicking", "thievery", "assassination",
        "athletics", "swimming",
        "science", "engineering", "robotics", "field_medic",
        "weapon_smith", "armour_smith", "bow_smith",
        "labouring", "farming", "cooking" };
    return (skill >= 0 && skill < kSkillCount) ? k[skill] : "?";
}

inline char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

inline bool IEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (Lower(a[i]) != Lower(b[i])) return false;
    return true;
}

/* lower case; ' ' and '-' become '_' */
inline std::string Norm(const std::string& s)
{
    std::string r(s);
    for (size_t i = 0; i < r.size(); ++i)
    {
        r[i] = Lower(r[i]);
        if (r[i] == ' ' || r[i] == '-') r[i] = '_';
    }
    return r;
}

/* -1 = not a slot */
inline int SlotFromName(const std::string& name)
{
    const std::string n = Norm(name);
    for (int i = 0; i < kSlotCount; ++i)
        if (n == SlotName(i)) return i;
    if (n == "weapons") return kSlotWeapon;
    if (n == "armor") return kSlotArmour;
    if (n == "legs" || n == "trousers") return kSlotPants;
    if (n == "shoes") return kSlotBoots;
    if (n == "hat" || n == "head") return kSlotHelmet;
    if (n == "bag") return kSlotBackpack;
    return -1;
}

/* -1 = not a skill */
inline int SkillFromName(const std::string& name)
{
    std::string n = Norm(name);
    const std::string::size_type d = n.find("defense");
    if (d != std::string::npos) n.replace(d, 7, "defence");
    const std::string::size_type a = n.find("armor");
    if (a != std::string::npos && n.find("armour") == std::string::npos) n.replace(a, 5, "armour");
    for (int i = 0; i < kSkillCount; ++i)
        if (n == SkillName(i)) return i;
    static const char* const kAlias[][2] = {
        { "thieving", "thievery" }, { "medic", "field_medic" }, { "first_aid", "field_medic" },
        { "crossbows", "precision_shooting" }, { "crossbow", "precision_shooting" }, { "crossbow_smith", "bow_smith" },
        { "poles", "polearms" }, { "labour", "labouring" }, { "labor", "labouring" }, { "laboring", "labouring" },
        { "unarmed", "martial_arts" }, { "heavy_weapon", "heavy_weapons" }, { "katana", "katanas" }, { "sabre", "sabres" },
        { "hacker", "hackers" } };
    for (size_t j = 0; j < sizeof(kAlias) / sizeof(kAlias[0]); ++j)
        if (n == kAlias[j][0]) return SkillFromName(kAlias[j][1]);
    return -1;
}

/* A whole-token number: optional sign, digits, optional fraction, optional trailing % when allowPct. */
inline bool ParseNumber(const std::string& s, bool allowPct, double* out)
{
    std::string t(s);
    if (allowPct && !t.empty() && t[t.size() - 1] == '%') t.erase(t.size() - 1);
    if (t.empty()) return false;
    size_t i = 0;
    if (t[i] == '+' || t[i] == '-') ++i;
    int digits = 0, dots = 0;
    for (; i < t.size(); ++i)
    {
        if (t[i] >= '0' && t[i] <= '9') ++digits;
        else if (t[i] == '.' && dots == 0) ++dots;
        else return false;
    }
    if (digits == 0) return false;
    *out = std::strtod(t.c_str(), 0);
    return true;
}

/* One line into tokens: whitespace separated, "double quoted" tokens may hold spaces, # outside quotes ends the line.
   false = an unclosed quote. */
inline bool Tokenize(const std::string& line, std::vector<std::string>* out)
{
    out->clear();
    size_t i = 0;
    const size_t n = line.size();
    while (i < n)
    {
        const char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
        if (c == '#') break;
        std::string tok;
        if (c == '"')
        {
            ++i;
            while (i < n && line[i] != '"') tok += line[i++];
            if (i >= n) return false;
            ++i;
        }
        else
        {
            while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\n' && line[i] != '#')
                tok += line[i++];
        }
        out->push_back(tok);
    }
    return true;
}

struct RaceRules
{
    std::string key;                    /* as written in the file */
    unsigned    blockedSlots;           /* bit (1 << Slot) */
    bool        noWeaponInHand;
    bool        hasXp[kSkillCount];
    float       xpMult[kSkillCount];    /* 1.0 when !hasXp */
    bool        hasCap[kSkillCount];
    float       cap[kSkillCount];
    bool        hasAnim;
    float       animSpeed;              /* 1.0 when !hasAnim */
    RaceRules() : blockedSlots(0), noWeaponInHand(false), hasAnim(false), animSpeed(1.0f)
    {
        for (int i = 0; i < kSkillCount; ++i) { hasXp[i] = false; xpMult[i] = 1.0f; hasCap[i] = false; cap[i] = -1.0f; }
    }
};

struct SkipWhy
{
    int         line;   /* 1-based */
    std::string why;
};

struct Report
{
    int lines;      /* every line read */
    int rules;      /* lines that parsed into a rule */
    int skipped;    /* lines that did not parse */
    std::vector<SkipWhy> why;   /* the first kMaxWhy skipped lines */
    Report() : lines(0), rules(0), skipped(0) {}
};

/* A verb's handler: args = the tokens after the verb. true = the rule is stored; false = *why says what is wrong. */
typedef bool (*VerbFn)(RaceRules* r, const std::vector<std::string>& args, std::string* why);

inline bool VerbBlockSlot(RaceRules* r, const std::vector<std::string>& args, std::string* why)
{
    if (args.empty()) { *why = "block_slot needs at least one slot"; return false; }
    unsigned bits = 0;
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string& a = args[i];
        size_t start = 0;
        for (;;)
        {
            const size_t bar = a.find('|', start);
            const std::string part = a.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
            if (!part.empty())
            {
                const int s = SlotFromName(part);
                if (s < 0) { *why = "unknown slot '" + part + "'"; return false; }
                bits |= (1u << s);
            }
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
    }
    if (bits == 0) { *why = "block_slot needs at least one slot"; return false; }
    r->blockedSlots |= bits;
    return true;
}

inline bool VerbNoWeaponInHand(RaceRules* r, const std::vector<std::string>& args, std::string* why)
{
    if (!args.empty()) { *why = "no_weapon_in_hand takes no arguments"; return false; }
    r->noWeaponInHand = true;
    return true;
}

inline bool VerbSkillXp(RaceRules* r, const std::vector<std::string>& args, std::string* why)
{
    if (args.size() != 2) { *why = "skill_xp needs <skill> <percent>"; return false; }
    const int sk = SkillFromName(args[0]);
    if (sk < 0) { *why = "unknown skill '" + args[0] + "'"; return false; }
    double pct = 0;
    if (!ParseNumber(args[1], true, &pct)) { *why = "skill_xp percent '" + args[1] + "' is not a number"; return false; }
    if (pct < -100.0 || pct > 1000.0) { *why = "skill_xp percent out of range -100..+1000"; return false; }
    r->hasXp[sk] = true;
    r->xpMult[sk] = (float)(1.0 + pct / 100.0);
    return true;
}

inline bool VerbStatCap(RaceRules* r, const std::vector<std::string>& args, std::string* why)
{
    if (args.size() != 2) { *why = "stat_cap needs <skill> <max>"; return false; }
    const int sk = SkillFromName(args[0]);
    if (sk < 0) { *why = "unknown skill '" + args[0] + "'"; return false; }
    double v = 0;
    if (!ParseNumber(args[1], false, &v)) { *why = "stat_cap max '" + args[1] + "' is not a number"; return false; }
    if (v < 0.0 || v > 1000.0) { *why = "stat_cap max out of range 0..1000"; return false; }
    r->hasCap[sk] = true;
    r->cap[sk] = (float)v;
    return true;
}

inline bool VerbAnimSpeed(RaceRules* r, const std::vector<std::string>& args, std::string* why)
{
    if (args.size() != 1) { *why = "anim_speed needs <multiplier>"; return false; }
    double v = 0;
    if (!ParseNumber(args[0], false, &v)) { *why = "anim_speed '" + args[0] + "' is not a number"; return false; }
    if (v < 0.05 || v > 10.0) { *why = "anim_speed out of range 0.05..10"; return false; }
    r->hasAnim = true;
    r->animSpeed = (float)v;
    return true;
}

struct VerbRow
{
    const char* name;
    VerbFn      fn;
};

/* THE VERBS. A new verb is one row here and its VerbFn above. */
inline const VerbRow* Verbs(int* count)
{
    static const VerbRow k[] = {
        { "block_slot",        VerbBlockSlot },
        { "no_weapon_in_hand", VerbNoWeaponInHand },
        { "skill_xp",          VerbSkillXp },
        { "stat_cap",          VerbStatCap },
        { "anim_speed",        VerbAnimSpeed } };
    *count = (int)(sizeof(k) / sizeof(k[0]));
    return k;
}

class Rules
{
public:
    void Clear() { m_races.clear(); m_report = Report(); }

    /* One line (no line break). true = a rule was stored or the line is blank/a comment; false = skipped (*why set). */
    bool ParseLine(const std::string& line, std::string* why)
    {
        std::vector<std::string> t;
        if (!Tokenize(line, &t)) { *why = "unclosed quote"; return false; }
        if (t.empty()) return true;
        if (!IEquals(t[0], "race")) { *why = "a rule starts with 'race'"; return false; }
        if (t.size() < 3) { *why = "needs race <race> <verb> ..."; return false; }
        if (t[1].empty()) { *why = "empty race"; return false; }
        int n = 0;
        const VerbRow* v = Verbs(&n);
        for (int i = 0; i < n; ++i)
        {
            if (Norm(t[2]) != v[i].name) continue;
            const std::vector<std::string> args(t.begin() + 3, t.end());
            RaceRules scratch = *Ensure(t[1], false);   /* a failed verb leaves the stored race untouched */
            if (!v[i].fn(&scratch, args, why)) return false;
            *Ensure(t[1], true) = scratch;
            return true;
        }
        *why = "unknown verb '" + t[2] + "'";
        return false;
    }

    /* The whole file's text (CRLF or LF; a UTF-8 BOM is ignored). Replaces any rules held. Returns the rule count. */
    int ParseText(const std::string& text)
    {
        Clear();
        size_t pos = 0;
        if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
            pos = 3;
        int lineNo = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string line = text.substr(pos, end - pos);
            pos = end + 1;
            ++lineNo;
            ++m_report.lines;
            std::vector<std::string> t;
            const bool blank = Tokenize(line, &t) && t.empty();
            std::string why;
            if (ParseLine(line, &why))
            {
                if (!blank) ++m_report.rules;
            }
            else
            {
                ++m_report.skipped;
                if ((int)m_report.why.size() < kMaxWhy)
                {
                    SkipWhy w; w.line = lineNo; w.why = why;
                    m_report.why.push_back(w);
                }
            }
        }
        return m_report.rules;
    }

    const Report& GetReport() const { return m_report; }
    int RaceCount() const { return (int)m_races.size(); }
    const RaceRules& RaceAt(int i) const { return m_races[(size_t)i]; }

    /* 0 = no rules for that race */
    const RaceRules* Find(const std::string& race) const
    {
        if (race.empty()) return 0;
        for (size_t i = 0; i < m_races.size(); ++i)
            if (IEquals(m_races[i].key, race)) return &m_races[i];
        return 0;
    }
    /* the string ID's rules when there are any, else the name's */
    const RaceRules* Find(const std::string& sid, const std::string& name) const
    {
        const RaceRules* r = Find(sid);
        return r ? r : Find(name);
    }

    /* THE ANSWERS. A race with no rules gets Kenshi's own behaviour: allowed, 1.0, no cap (-1), 1.0. */
    bool SlotAllowed(const std::string& race, int slot) const { return SlotAllowedR(Find(race), slot); }
    bool WeaponInHandAllowed(const std::string& race) const { return WeaponInHandAllowedR(Find(race)); }
    float SkillXpMultiplier(const std::string& race, int skill) const { return SkillXpMultiplierR(Find(race), skill); }
    float StatCap(const std::string& race, int skill) const { return StatCapR(Find(race), skill); }
    float AnimSpeed(const std::string& race) const { return AnimSpeedR(Find(race)); }

    static bool SlotAllowedR(const RaceRules* r, int slot)
    {
        if (r == 0 || slot < 0 || slot >= kSlotCount) return true;
        return (r->blockedSlots & (1u << slot)) == 0;
    }
    static bool WeaponInHandAllowedR(const RaceRules* r) { return r == 0 || !r->noWeaponInHand; }
    static float SkillXpMultiplierR(const RaceRules* r, int skill)
    {
        if (r == 0 || skill < 0 || skill >= kSkillCount) return 1.0f;
        return r->xpMult[skill];
    }
    static float StatCapR(const RaceRules* r, int skill)
    {
        if (r == 0 || skill < 0 || skill >= kSkillCount || !r->hasCap[skill]) return -1.0f;
        return r->cap[skill];
    }
    static float AnimSpeedR(const RaceRules* r) { return r ? r->animSpeed : 1.0f; }

    /* One line per race for the log: "<key>: block weapon|armour, no weapon in hand, xp heavy_weapons x1.40, cap ..., anim x0.80" */
    static std::string Describe(const RaceRules& r)
    {
        std::string s = r.key + ":";
        char buf[96];
        bool any = false;
        if (r.blockedSlots != 0)
        {
            s += " block ";
            bool first = true;
            for (int i = 0; i < kSlotCount; ++i)
                if (r.blockedSlots & (1u << i)) { if (!first) s += "|"; s += SlotName(i); first = false; }
            any = true;
        }
        if (r.noWeaponInHand) { s += any ? "," : ""; s += " no weapon in hand"; any = true; }
        for (int i = 0; i < kSkillCount; ++i)
            if (r.hasXp[i])
            {
                std::sprintf(buf, " xp %s x%.2f", SkillName(i), (double)r.xpMult[i]);
                s += any ? "," : ""; s += buf; any = true;
            }
        for (int i = 0; i < kSkillCount; ++i)
            if (r.hasCap[i])
            {
                std::sprintf(buf, " cap %s %.1f", SkillName(i), (double)r.cap[i]);
                s += any ? "," : ""; s += buf; any = true;
            }
        if (r.hasAnim)
        {
            std::sprintf(buf, " anim x%.2f", (double)r.animSpeed);
            s += any ? "," : ""; s += buf; any = true;
        }
        if (!any) s += " (nothing)";
        return s;
    }

private:
    RaceRules* Ensure(const std::string& key, bool create)
    {
        for (size_t i = 0; i < m_races.size(); ++i)
            if (IEquals(m_races[i].key, key)) return &m_races[i];
        if (!create) { m_blank = RaceRules(); m_blank.key = key; return &m_blank; }
        RaceRules r; r.key = key;
        m_races.push_back(r);
        return &m_races.back();
    }

    std::vector<RaceRules> m_races;
    RaceRules              m_blank;
    Report                 m_report;
};

}   /* namespace kapi */
