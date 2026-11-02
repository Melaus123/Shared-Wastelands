/* src/common/kapirules.h - per-race rules a race mod sets through a rules file (kenshi_api.cfg), and their form in the world's
   option map. Ported from the third public pull request (author: MassDivide), reworked to travel as world options.

   Kenshi's game data cannot say some things a race mod needs: a race's "races exclude" does not stop a weapon being equipped,
   and race skill bonuses are capped at +-20% and do not stack. These rules can.

   PURE: no Windows.h, no engine, no globals, no I/O. Compiled into the plugin (src/coop-plugin/kapi.cpp), the world server
   (src/coop-store/store_main.cpp OptionValueOk / OnOptions) and the offline suite (src/coop-test), so the three cannot hold
   different ideas of the file, the keys or their values.

   THE FILE, one rule per line, # starts a comment (outside quotes), blank lines are ignored:
       race <race> block_slot <slot>[|<slot>...] [<slot> ...]
       race <race> no_weapon_in_hand
       race <race> skill_xp <skill> <percent>        +40 = 1.4x the XP, -99 = 0.01x, -100 = none (range -100..+1000)
       race <race> stat_cap <skill> <max>            read and logged only (0..1000)
       race <race> anim_speed <multiplier>           read and logged only (0.05..10)
   <race> is the race's string ID (for example 7100001-OCS-100.mod), "double quoted" when it holds spaces. It is compared
   with the race record's string ID only, without ASCII case - never with the race's shown name, which a player or a
   translation can change. A later line for the same race and the same slot/skill/verb overrides the earlier one.
   Numbers are kept to fixed steps: skill_xp percent to 0.01, stat_cap to 0.1, anim_speed to 0.01.
   Slots:  weapon armour pants shirt boots helmet backpack belt  (aliases: weapons armor legs trousers shoes hat head bag)
   Skills: the Kenshi skill names with _ for spaces (heavy_weapons, melee_defence ...); a few aliases (thieving, medic,
           crossbows, armor_smith, defense ...).
   A line that does not parse is SKIPPED and counted (Report().skipped, the first kMaxWhy with their reason); it never
   stops the file being read.

   THE WORLD'S COPY. The session authority's game turns its file into option rows, one per race:
       key    race.<string id, ASCII lower case>          (1..kMaxSidLen bytes of id; no control character, no tab)
       value  parts in this order, joined by ';', each present only when the race has that rule:
                s<bits>                 blocked slots, bit (1 << Slot), 1..255
                w                       no_weapon_in_hand
                x<skill>:<pct100>,...   skill_xp, skills ascending, percent x100 (-10000..100000)
                c<skill>:<max10>,...    stat_cap, skills ascending, max x10 (0..10000)
                a<mult100>              anim_speed x100 (5..1000)
              integers in decimal with no '+' and no leading zero; at most kMaxValueLen bytes. Example: s63;w;x11:4000,16:-9900
   Every game builds its rules from the world's rows alone; solo play builds them from its own file's rows - one road.
   An empty value sent for a race key asks the world server to remove that race's row. At most kMaxRaceKeys race rows.

   C++03 (VS2010 v100). Header-only. */
#pragma once

#include <string>
#include <vector>
#include <map>
#include <utility>
#include <cstdlib>
#include <cstdio>
#include <cmath>

namespace kapi {

enum Slot
{
    kSlotWeapon = 0, kSlotArmour, kSlotPants, kSlotShirt, kSlotBoots, kSlotHelmet, kSlotBackpack, kSlotBelt,
    kSlotCount
};

enum
{
    kSkillCount = 32,
    kMaxWhy = 16,          /* skipped lines kept with their reason */
    kMaxSidLen = 96,       /* a race string ID's longest form in an option key */
    kMaxValueLen = 256,    /* an option value's longest form (the plugin's StoreSendOptions limit) */
    kMaxRaceKeys = 32      /* race rows in one world: with the 26 other option keys the map stays under its 64 pairs */
};

/* skill_xp, the XP sink addSkillXp(float* field, ...): a CharStats field's offset -> the skill (getStatRef), and the engine's
   StatsEnumerated -> the skill (taskXp's stat argument); -1 = none. Precision shooting and crossbows both map to
   precision_shooting. */
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

/* skill_xp, the skill trainers' sink addSkillXp (1.0.65 0x8C4F20): it adds xp x (the room left to the cap)^2 only when
   0 < xp <= 20 and throws a larger gain away (read in its disassembly, T-1157 review fold). So a gain over 20 is left as it
   is - the engine drops it whatever the multiplier, and scaling it down would make the engine take a gain it never takes - and
   a boost never lifts a gain the engine would have taken past 20. A multiplier of 1 (or NaN / negative) leaves xp. */
inline float ScaleXp(float xp, float m)
{
    if (!(m >= 0.0f) || m == 1.0f || xp > 20.0f) return xp;
    const float out = xp * m;
    return out > 20.0f ? 20.0f : out;
}
/* skill_xp, the task adder taskXp (1.0.65 0x8C5260): it adds rate x its own factors straight into the stat, with no 20 limit,
   and does not call the sink (read in its disassembly: its calls are the stat lookup, a clamp and a map lookup), so its rate is
   scaled plainly and scaled once. */
inline float ScaleRate(float rate, float m)
{
    if (!(m >= 0.0f) || m == 1.0f) return rate;
    return rate * m;
}

/* An equipment section's own limitedSlot (InventorySection +0xB8, the engine's AttachSlot) -> Slot; -1 = none or not certain.
   0 weapon, 1 back (a weapon carried on the back), 3 hat, 5 body, 6 legs, 8 shirt, 9 boots, 12 backpack, 14 belt; 7 = general. */
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

/* A character inventory section's name (the engine's own: shirt, legs, armour, head, boots, backpack_attach, belt, and the weapon
   sections) -> Slot; -1 = no slot (main, flat, anything else). Compared without case. A weapon section is any name with "weapon"
   or "hand" in it, or exactly "hip" or "back" (the sheath points). Used only when the section's limitedSlot names no slot. */
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

inline const char* SlotName(int slot)
{
    static const char* const k[kSlotCount] = { "weapon", "armour", "pants", "shirt", "boots", "helmet", "backpack", "belt" };
    return (slot >= 0 && slot < kSlotCount) ? k[slot] : "?";
}

/* THE GAME'S SKILLS, in a fixed order of our own (not the engine's StatsEnumerated numbers; SkillOfEngineStat maps those). */
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

inline std::string LowerAscii(const std::string& s)
{
    std::string r(s);
    for (size_t i = 0; i < r.size(); ++i) r[i] = Lower(r[i]);
    return r;
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

/* v * scale to the nearest whole step (halves away from zero) */
inline int Steps(double v, double scale)
{
    const double x = v * scale;
    return (int)(x < 0.0 ? -std::floor(-x + 0.5) : std::floor(x + 0.5));
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

/* A race string ID that can be an option key: 1..kMaxSidLen bytes, no control character (below 0x20, 0x7F), no space at
   either end. Bytes from 0x80 up (a UTF-8 mod name) are allowed. */
inline bool SidOk(const std::string& s)
{
    if (s.empty() || s.size() > (size_t)kMaxSidLen || s[0] == ' ' || s[s.size() - 1] == ' ') return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

struct RaceRules
{
    std::string key;                    /* the race's string ID, ASCII lower case */
    unsigned    blockedSlots;           /* bit (1 << Slot) */
    bool        noWeaponInHand;
    bool        hasXp[kSkillCount];
    int         xpPct100[kSkillCount];  /* percent x100: +40% = 4000 */
    float       xpMult[kSkillCount];    /* 1.0 when !hasXp */
    bool        hasCap[kSkillCount];
    int         cap10[kSkillCount];     /* max x10 */
    float       cap[kSkillCount];       /* -1 when !hasCap */
    bool        hasAnim;
    int         anim100;                /* multiplier x100 */
    float       animSpeed;              /* 1.0 when !hasAnim */
    RaceRules() : blockedSlots(0), noWeaponInHand(false), hasAnim(false), anim100(100), animSpeed(1.0f)
    {
        for (int i = 0; i < kSkillCount; ++i) { hasXp[i] = false; xpPct100[i] = 0; xpMult[i] = 1.0f; hasCap[i] = false; cap10[i] = 0; cap[i] = -1.0f; }
    }
    bool Any() const
    {
        if (blockedSlots != 0 || noWeaponInHand || hasAnim) return true;
        for (int i = 0; i < kSkillCount; ++i) if (hasXp[i] || hasCap[i]) return true;
        return false;
    }
    void SetXp(int sk, int pct100) { hasXp[sk] = true; xpPct100[sk] = pct100; xpMult[sk] = (float)(1.0 + pct100 / 10000.0); }
    void SetCap(int sk, int c10) { hasCap[sk] = true; cap10[sk] = c10; cap[sk] = (float)(c10 / 10.0); }
    void SetAnim(int a100) { hasAnim = true; anim100 = a100; animSpeed = (float)(a100 / 100.0); }
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
    r->SetXp(sk, Steps(pct, 100.0));
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
    r->SetCap(sk, Steps(v, 10.0));
    return true;
}

inline bool VerbAnimSpeed(RaceRules* r, const std::vector<std::string>& args, std::string* why)
{
    if (args.size() != 1) { *why = "anim_speed needs <multiplier>"; return false; }
    double v = 0;
    if (!ParseNumber(args[0], false, &v)) { *why = "anim_speed '" + args[0] + "' is not a number"; return false; }
    if (v < 0.05 || v > 10.0) { *why = "anim_speed out of range 0.05..10"; return false; }
    r->SetAnim(Steps(v, 100.0));
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

/* ---- the option value: a whole-number reader and writer in the one canonical form ---- */
inline std::string IntText(int v) { char b[16]; std::sprintf(b, "%d", v); return b; }
/* s[*at..] -> *v: optional '-', 1..7 digits, no leading zero, no "-0"; *at moves past it */
inline bool ReadInt(const std::string& s, size_t* at, int* v)
{
    size_t i = *at;
    bool neg = false;
    if (i < s.size() && s[i] == '-') { neg = true; ++i; }
    const size_t d0 = i;
    long n = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { n = n * 10 + (s[i] - '0'); ++i; if (i - d0 > 7) return false; }
    if (i == d0) return false;
    if (s[d0] == '0' && (i - d0 > 1 || neg)) return false;
    *v = (int)(neg ? -n : n);
    *at = i;
    return true;
}
/* "<skill>:<n>,<skill>:<n>..." with skills strictly ascending and every n inside lo..hi */
inline bool ReadPairs(const std::string& s, size_t* at, int lo, int hi, std::vector<std::pair<int, int> >* out)
{
    out->clear();
    int last = -1;
    for (;;)
    {
        int k = 0, v = 0;
        if (!ReadInt(s, at, &k) || k <= last || k >= kSkillCount) return false;
        if (*at >= s.size() || s[*at] != ':') return false;
        ++*at;
        if (!ReadInt(s, at, &v) || v < lo || v > hi) return false;
        out->push_back(std::make_pair(k, v));
        last = k;
        if (*at < s.size() && s[*at] == ',') { ++*at; continue; }
        return true;
    }
}

/* the race's rules as an option value; "" = the race has no rule */
inline std::string EncodeValue(const RaceRules& r)
{
    std::string v;
    if ((r.blockedSlots & 0xFFu) != 0) v += "s" + IntText((int)(r.blockedSlots & 0xFFu));
    if (r.noWeaponInHand) { if (!v.empty()) v += ";"; v += "w"; }
    bool first = true;
    for (int i = 0; i < kSkillCount; ++i)
        if (r.hasXp[i]) { v += first ? (v.empty() ? "x" : ";x") : ","; v += IntText(i) + ":" + IntText(r.xpPct100[i]); first = false; }
    first = true;
    for (int i = 0; i < kSkillCount; ++i)
        if (r.hasCap[i]) { v += first ? (v.empty() ? "c" : ";c") : ","; v += IntText(i) + ":" + IntText(r.cap10[i]); first = false; }
    if (r.hasAnim) { if (!v.empty()) v += ";"; v += "a" + IntText(r.anim100); }
    return v;
}

/* an option value -> rules (key untouched); false = not exactly the canonical form above */
inline bool DecodeValue(const std::string& v, RaceRules* out)
{
    RaceRules r;
    r.key = out->key;
    if (v.empty() || v.size() > (size_t)kMaxValueLen) return false;
    const char order[] = "swxca";
    int next = 0;
    size_t at = 0;
    while (at < v.size())
    {
        const char tag = v[at++];
        int pos = -1;
        for (int i = next; i < 5; ++i) if (order[i] == tag) { pos = i; break; }
        if (pos < 0) return false;
        next = pos + 1;
        int n = 0;
        std::vector<std::pair<int, int> > pairs;
        switch (tag)
        {
        case 's': if (!ReadInt(v, &at, &n) || n < 1 || n > 255) return false; r.blockedSlots = (unsigned)n; break;
        case 'w': r.noWeaponInHand = true; break;
        case 'x': if (!ReadPairs(v, &at, -10000, 100000, &pairs)) return false; for (size_t i = 0; i < pairs.size(); ++i) r.SetXp(pairs[i].first, pairs[i].second); break;
        case 'c': if (!ReadPairs(v, &at, 0, 10000, &pairs)) return false; for (size_t i = 0; i < pairs.size(); ++i) r.SetCap(pairs[i].first, pairs[i].second); break;
        case 'a': if (!ReadInt(v, &at, &n) || n < 5 || n > 1000) return false; r.SetAnim(n); break;
        default: return false;
        }
        if (at < v.size())
        {
            if (v[at] != ';') return false;
            ++at;
            if (at >= v.size()) return false;
        }
    }
    *out = r;
    return true;
}

const char* const kKeyPrefix = "race.";
inline bool IsOptionKey(const std::string& k) { return k.size() >= 5 && k.compare(0, 5, kKeyPrefix) == 0; }
inline std::string OptionKey(const std::string& sid) { return std::string(kKeyPrefix) + LowerAscii(sid); }
/* the key's race string ID, or "" when the key is not a well-formed race key (no upper-case ASCII, SidOk) */
inline std::string SidOfKey(const std::string& k)
{
    if (!IsOptionKey(k)) return std::string();
    const std::string sid = k.substr(5);
    if (!SidOk(sid) || LowerAscii(sid) != sid) return std::string();
    return sid;
}
/* THE WORLD SERVER'S CHECK of a row to store: a well-formed race key and a value in exactly the canonical form. An empty value
   is not a row (it asks for the row's removal; OnOptions handles it before this). */
inline bool OptionValueOk(const std::string& k, const std::string& v)
{
    if (SidOfKey(k).empty()) return false;
    RaceRules r;
    return DecodeValue(v, &r) && EncodeValue(r) == v;
}
/* how many race rows a map holds, and whether a row for key k may be stored in it (not a race row, already there, or room for one more) */
inline int RaceKeyCount(const std::map<std::string, std::string>& m)
{
    int n = 0;
    for (std::map<std::string, std::string>::const_iterator it = m.begin(); it != m.end(); ++it) if (IsOptionKey(it->first)) ++n;
    return n;
}
inline bool RaceKeyRoom(const std::map<std::string, std::string>& m, const std::string& k)
{
    return !IsOptionKey(k) || m.find(k) != m.end() || RaceKeyCount(m) < kMaxRaceKeys;   /* only race rows count against the limit */
}
/* the race rows past the first kMaxRaceKeys (in key order) taken out of a map, so the whole map stays under its 64 pairs
   whatever an options file held; returns how many were taken out */
inline int TrimRaceKeys(std::map<std::string, std::string>* m)
{
    int seen = 0, cut = 0;
    for (std::map<std::string, std::string>::iterator it = m->begin(); it != m->end(); )
    {
        if (IsOptionKey(it->first) && ++seen > kMaxRaceKeys) { m->erase(it++); ++cut; }
        else ++it;
    }
    return cut;
}
/* THE WORLD SERVER'S STEP for one race row the authority sent, on its candidate map (store_main.cpp OnOptions): an empty
   value removes the row (the authority's kenshi_api.cfg no longer names that race); any other value must be canonical
   (OptionValueOk) and find room (RaceKeyRoom). T-1157 review fold: one pure step, so the offline suite tests what the server does. */
enum { kRowUnchanged = 0, kRowChanged = 1, kRowBadValue = -1, kRowNoRoom = -2 };
inline int RaceRowStep(std::map<std::string, std::string>* m, const std::string& k, const std::string& v)
{
    if (v.empty()) return m->erase(k) != 0 ? kRowChanged : kRowUnchanged;
    if (!OptionValueOk(k, v)) return kRowBadValue;
    if (!RaceKeyRoom(*m, k)) return kRowNoRoom;
    std::map<std::string, std::string>::const_iterator have = m->find(k);
    if (have != m->end() && have->second == v) return kRowUnchanged;
    (*m)[k] = v;
    return kRowChanged;
}
/* THE WORLD SERVER'S DEFERRED MERGE (store_main.cpp OptionsDeferredRetry): the changes held while options.txt could not be
   read are laid on the rows the file holds (an empty value for a race key takes that row out), then the race rows past the
   first kMaxRaceKeys are trimmed. A race row the file holds and the authority never removed comes back here; the authority
   sees it in the map that follows and asks for its removal. Returns how many race rows were trimmed. */
inline int MergeHeldChanges(std::map<std::string, std::string>* merged, const std::map<std::string, std::string>& held)
{
    for (std::map<std::string, std::string>::const_iterator it = held.begin(); it != held.end(); ++it)
        if (it->second.empty() && IsOptionKey(it->first)) merged->erase(it->first);
        else (*merged)[it->first] = it->second;
    return TrimRaceKeys(merged);
}

typedef std::vector<std::pair<std::string, std::string> > Rows;

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
        if (!SidOk(t[1])) { *why = "the race string ID is empty, too long or holds a control character"; return false; }
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

    /* THE OPTION ROWS of these rules, in race order: one per race with a rule. A race whose value is longer than kMaxValueLen
       goes to *tooLong, a race past the first kMaxRaceKeys to *tooMany (either may be 0); neither is in *out. */
    void ToRows(Rows* out, std::vector<std::string>* tooLong, std::vector<std::string>* tooMany) const
    {
        out->clear();
        for (size_t i = 0; i < m_races.size(); ++i)
        {
            if (!m_races[i].Any()) continue;
            const std::string v = EncodeValue(m_races[i]);
            if (v.size() > (size_t)kMaxValueLen) { if (tooLong != 0) tooLong->push_back(m_races[i].key); continue; }
            if ((int)out->size() >= kMaxRaceKeys) { if (tooMany != 0) tooMany->push_back(m_races[i].key); continue; }
            out->push_back(std::make_pair(OptionKey(m_races[i].key), v));
        }
    }

    /* Replaces any rules held with the races of these option rows (rows of other keys are not looked at). Returns how many
       race rows did not decode; they are left out. */
    int FromRows(const Rows& rows)
    {
        Clear();
        int bad = 0;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (!IsOptionKey(rows[i].first)) continue;
            RaceRules r;
            r.key = SidOfKey(rows[i].first);
            if (r.key.empty() || !OptionValueOk(rows[i].first, rows[i].second) || !DecodeValue(rows[i].second, &r)) { ++bad; continue; }
            *Ensure(r.key, true) = r;
        }
        return bad;
    }

    const Report& GetReport() const { return m_report; }
    int RaceCount() const { return (int)m_races.size(); }
    const RaceRules& RaceAt(int i) const { return m_races[(size_t)i]; }

    /* the rules of the race with this string ID; 0 = none */
    const RaceRules* Find(const std::string& sid) const
    {
        if (sid.empty()) return 0;
        for (size_t i = 0; i < m_races.size(); ++i)
            if (IEquals(m_races[i].key, sid)) return &m_races[i];
        return 0;
    }

    /* THE ANSWERS. A race with no rules gets Kenshi's own behaviour: allowed, 1.0, no cap (-1), 1.0. */
    bool SlotAllowed(const std::string& sid, int slot) const { return SlotAllowedR(Find(sid), slot); }
    bool WeaponInHandAllowed(const std::string& sid) const { return WeaponInHandAllowedR(Find(sid)); }
    float SkillXpMultiplier(const std::string& sid, int skill) const { return SkillXpMultiplierR(Find(sid), skill); }
    float StatCap(const std::string& sid, int skill) const { return StatCapR(Find(sid), skill); }
    float AnimSpeed(const std::string& sid) const { return AnimSpeedR(Find(sid)); }

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
                std::sprintf(buf, " xp %s x%.4f", SkillName(i), (double)r.xpMult[i]);
                s += any ? "," : ""; s += buf; any = true;
            }
        for (int i = 0; i < kSkillCount; ++i)
            if (r.hasCap[i])
            {
                std::sprintf(buf, " cap %s %.1f (not applied)", SkillName(i), (double)r.cap[i]);
                s += any ? "," : ""; s += buf; any = true;
            }
        if (r.hasAnim)
        {
            std::sprintf(buf, " anim x%.2f (not applied)", (double)r.animSpeed);
            s += any ? "," : ""; s += buf; any = true;
        }
        if (!any) s += " (nothing)";
        return s;
    }

private:
    RaceRules* Ensure(const std::string& sid, bool create)
    {
        for (size_t i = 0; i < m_races.size(); ++i)
            if (IEquals(m_races[i].key, sid)) return &m_races[i];
        if (!create) { m_blank = RaceRules(); m_blank.key = LowerAscii(sid); return &m_blank; }
        RaceRules r; r.key = LowerAscii(sid);
        m_races.push_back(r);
        return &m_races.back();
    }

    std::vector<RaceRules> m_races;
    RaceRules              m_blank;
    Report                 m_report;
};

/* WHAT THE AUTHORITY SENDS so the world's race rows become exactly `own`: first an empty value for every world race row `own`
   does not have (removals first, so the world server's kMaxRaceKeys room is free for the new rows), then every row of `own` the
   world lacks or holds with another value. Empty = the world already matches. At most 2 x kMaxRaceKeys pairs. */
inline Rows RowsToSend(const Rows& world, const Rows& own)
{
    Rows out;
    for (size_t i = 0; i < world.size(); ++i)
    {
        if (!IsOptionKey(world[i].first)) continue;
        bool kept = false;
        for (size_t j = 0; j < own.size() && !kept; ++j) if (own[j].first == world[i].first) kept = true;
        if (!kept) out.push_back(std::make_pair(world[i].first, std::string()));
    }
    for (size_t j = 0; j < own.size(); ++j)
    {
        bool same = false;
        for (size_t i = 0; i < world.size() && !same; ++i) if (world[i].first == own[j].first && world[i].second == own[j].second) same = true;
        if (!same) out.push_back(own[j]);
    }
    return out;
}

/* the race rows of a set in one text, sorted by key - two sets with the same text hold the same rules */
inline std::string RowsText(const Rows& rows)
{
    std::map<std::string, std::string> m;
    for (size_t i = 0; i < rows.size(); ++i) if (IsOptionKey(rows[i].first)) m[rows[i].first] = rows[i].second;
    std::string t;
    for (std::map<std::string, std::string>::const_iterator it = m.begin(); it != m.end(); ++it) t += it->first + "=" + it->second + "\n";
    return t;
}

/* THE AUTHORITY'S RESEND BOUND on one link. An ask is the world's race rows and the request made against them, in one text.
   The authority asks again only while the world's rows still differ from its file's: the same request against the same world
   rows at most kMaxSameAsk times (one retry covers a refused write and a map that crossed the send), and at most
   kMaxAsksPerLink requests in all, so a world that keeps answering with other rows cannot make it send in a loop. */
enum { kMaxSameAsk = 2, kMaxAsksPerLink = 8 };
struct AskBound
{
    std::string last;
    int same, asks;
    AskBound() : same(0), asks(0) {}
    void Reset() { last.clear(); same = 0; asks = 0; }
    bool Allowed(const std::string& ask) const { return asks < kMaxAsksPerLink && !(ask == last && same >= kMaxSameAsk); }
    void Sent(const std::string& ask) { if (ask == last) ++same; else { last = ask; same = 1; } ++asks; }
};

}   /* namespace kapi */
