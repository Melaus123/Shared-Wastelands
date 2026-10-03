#pragma once
// recruit3 (user decision 2026-09-26, .modding/02-project-rules.md "Recruits"; option A of .modding/investigations/recruits.md):
// a town's bar HIRE lists are rolled x the number of players, capped at x4, as a HOST option. The notebook keeps it in
// options.txt beside basepolicy / timemode and accepts it only from the session authority (store_main.cpp OnOptions).
// ONE header compiled into the plugin (towngen.cpp, command_channel.cpp), the notebook (store_main.cpp OptionValueOk)
// and the offline suite, so the three cannot hold different ideas of the key's legal values.
//   recruitmult = auto | 1 | 2 | 3 | 4      (absent = auto; auto = player slots seen in this world, capped at 4)
#include <string>

namespace coopr {

const int kRecruitMultCap = 4;

// value text -> 0 (auto) or 1..4; -1 = not a legal value
inline int RecruitMultCode(const std::string& v)
{
    if (v == "auto") return 0;
    if (v.size() == 1 && v[0] >= '1' && v[0] <= '0' + kRecruitMultCap) return v[0] - '0';
    return -1;
}
inline bool RecruitMultValueOk(const std::string& v) { return RecruitMultCode(v) >= 0; }
inline const char* RecruitMultName(int code)
{
    static const char* const n[] = { "auto", "1", "2", "3", "4" };
    return (code >= 0 && code <= kRecruitMultCap) ? n[code] : "?";
}
// The multiplier applied: a fixed 1..4 as given; auto = the player slots seen, at least 1 and at most 4.
inline int RecruitMultEffective(int code, int slotsSeen)
{
    if (code >= 1 && code <= kRecruitMultCap) return code;
    if (slotsSeen < 1) return 1;
    return slotsSeen > kRecruitMultCap ? kRecruitMultCap : slotsSeen;
}
inline int RecruitSlotCount(unsigned long mask)
{
    int n = 0;
    for (int i = 0; i < 16; ++i) if ((mask >> i) & 1UL) ++n;
    return n;
}
// A bar-squad SQUAD_TEMPLATE is a HIRE list when its FCS name contains "recruit" (any case). The vanilla data (Read,
// the names in gamedata.base / Newwworld.mod / rebirth.mod) has no recruitable flag; its hire lists are named
// "Recruits list <town|faction>","shady bar recruits list travellers", "mongrel recruits" and "mongrel permanent
// recruits", and the other bar squads are not ("Drifter squad bar", "Shek Warrior (bar squad)", "Cannibal Hunter Bar
// Squad", thugs, bounty hunters, slave traders). A name this cannot read is NOT a hire list (never multiplied).
inline bool IsHireListName(const char* name)
{
    if (name == 0) return false;
    static const char k[] = "recruit";
    for (const char* p = name; *p != 0; ++p)
    {
        int i = 0;
        for (; k[i] != 0; ++i)
        {
            char c = p[i];
            if (c == 0) return false;
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (c != k[i]) break;
        }
        if (k[i] == 0) return true;
    }
    return false;
}

} // namespace coopr
