#pragma once
// THE TEST-ONLY `raidnow` LEVER AND THE RAID CREATION LINES - the pure half, ONE header compiled into the plugin (towngen.cpp
// RaidNowLever and the squad-creation detour) and the offline suite.
//
// THE RAID ROLL (Read, our decompile of Steam 1.0.65): the faction update 0x6B9470 calls the war manager's update 0x9CA440 with
// the pointer at Faction+0x88 (its only caller). 0x9CA440 asks 0x791080(warManager+0xB0, 0.0) whether the double at +0xB0 is due:
// due when it holds the engine's "unset" value or when the clock's `now` (the double at +0xA0 of the clock object behind
// ClockOwnerPtr, read by 0x66C180) is greater than it. When due it writes now + the interval at +0xB0 and, only when the war
// manager has no running campaign (+0x70 count 0) and none waiting to start (+0xC0 count 0), rolls each campaign kind it holds
// (+0xD8 count) by chance and starts the chosen one through 0x9C8960 - the road whose squads come through createRandomUnloadedSquad.
// The lever skips the timer and the dice: it asks the kinds' target finders as the update does and starts the kind PickKind
// names through 0x9C8960 itself.
//
// THE LINES: every creation call from the raid caller gets a [TOWN] line, at most one per town per kLineGapMs; the calls in between
// are counted and the next line carries the count.
#include <cstring>
#include <string>

namespace raidnow {

const size_t kNameMax = 127;                 // the longest faction name the lever accepts
const int kKindsMax = 256;                   // the most campaign kinds a war manager is read with (more = a layout this build does not know)
const unsigned long long kLineGapMs = 1000;  // at most one raid creation line per town per second
const size_t kTownCap = 64;                  // towns with their own line gate; the rest share one overflow gate

/* The lever's argument: the rest of the command line, trimmed of spaces and tabs at both ends; inner spaces are kept (faction
   names have them). 1 = *name written; 0 = empty, longer than kNameMax, or holding a control character. */
inline int ParseName(const std::string& rest, std::string* name)
{
    const size_t s = rest.find_first_not_of(" \t\r\n");
    if (s == std::string::npos) return 0;
    const size_t e = rest.find_last_not_of(" \t\r\n");
    const std::string t = rest.substr(s, e - s + 1);
    if (t.size() > kNameMax) return 0;
    for (size_t i = 0; i < t.size(); ++i)
        if ((unsigned char)t[i] < 0x20 || (unsigned char)t[i] == 0x7F) return 0;
    *name = t;
    return 1;
}

/* The campaign kind the lever starts: of the kinds whose target finder found a target (hasTarget[i] != 0), the one with the
   highest chance - the first on a tie; a chance that is not a number counts lowest. -1 = no kind found a target. The update
   itself picks among the kinds whose chance beat a random draw, by weight; the lever leaves both draws out. */
inline int PickKind(const int* hasTarget, const float* chance, int n)
{
    int best = -1;
    for (int i = 0; i < n; ++i)
    {
        if (hasTarget[i] == 0) continue;
        if (best < 0) { best = i; continue; }
        const float c = chance[i], b = chance[best];
        if (c == c && (b != b || c > b)) best = i;
    }
    return best;
}

/* THE KIND CHOICE (T-1122 kind choice, owner 1500 a; TEST-ONLY): `raidnow <faction name> kind <name piece | #index>` starts the kind the
   choice names instead of the highest chance. The faction name is everything before the LAST word `kind` (lower case, with a space
   or a tab on both sides); the choice is the rest: `#` and the kind's place in the war manager's list (0 .. kKindsMax-1, the number
   the lever's lines print), or a piece of the kind's name, matched without regard to upper / lower case (1 to kPieceMax characters,
   inner spaces kept). No word `kind` = no choice: the highest chance, as before. */
const size_t kPieceMax = 63;   // the longest name piece a choice accepts
const int kKindNameMax = 64;   // bytes kept of each kind's name (its end zero included) for the choice and the lever's list
struct KindSel { int given; int index; char piece[kPieceMax + 1]; };   // given 0 = no choice; index -1 = a name piece

/* 1 = *name and *sel written; 0 = no name or a bad one (ParseName's rules), or a bad choice (empty, `#` without 1 to 3 digits or past
   kKindsMax-1, a piece longer than kPieceMax or holding a control character). Nothing is written on 0. */
inline int ParseArgs(const std::string& rest, std::string* name, KindSel* sel)
{
    const size_t s = rest.find_first_not_of(" \t\r\n");
    if (s == std::string::npos) return 0;
    const size_t e = rest.find_last_not_of(" \t\r\n");
    const std::string t = rest.substr(s, e - s + 1);
    size_t at = std::string::npos;
    for (size_t p = t.find("kind"); p != std::string::npos; p = t.find("kind", p + 1))
        if (p > 0 && (t[p - 1] == ' ' || t[p - 1] == '\t') && (p + 4 == t.size() || t[p + 4] == ' ' || t[p + 4] == '\t')) at = p;
    KindSel k; k.given = 0; k.index = -1; k.piece[0] = 0;
    std::string n;
    if (at == std::string::npos)
    {
        if (ParseName(t, &n) == 0) return 0;
        *name = n; *sel = k;
        return 1;
    }
    if (ParseName(t.substr(0, at), &n) == 0) return 0;
    const size_t cs = t.find_first_not_of(" \t", at + 4);
    if (cs == std::string::npos) return 0;
    const std::string v = t.substr(cs);   /* its end is trimmed already */
    if (v[0] == '#')
    {
        if (v.size() < 2 || v.size() > 4) return 0;
        int idx = 0;
        for (size_t i = 1; i < v.size(); ++i) { if (v[i] < '0' || v[i] > '9') return 0; idx = idx * 10 + (v[i] - '0'); }
        if (idx >= kKindsMax) return 0;
        k.index = idx;
    }
    else
    {
        if (v.size() > kPieceMax) return 0;
        for (size_t i = 0; i < v.size(); ++i)
            if ((unsigned char)v[i] < 0x20 || (unsigned char)v[i] == 0x7F) return 0;
        std::memcpy(k.piece, v.c_str(), v.size() + 1);
    }
    k.given = 1;
    *name = n; *sel = k;
    return 1;
}

inline char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* 1 = kind i (named kindName; 0 or "" = its name was not read) is one the choice names. No choice (sel 0 or given 0) names every kind;
   `#index` names only that place; a piece names every kind whose name holds it, upper / lower case alike. */
inline int KindMatches(const KindSel* sel, int i, const char* kindName)
{
    if (sel == 0 || sel->given == 0) return 1;
    if (sel->index >= 0) return i == sel->index ? 1 : 0;
    if (kindName == 0) return 0;
    const size_t m = std::strlen(sel->piece), n = std::strlen(kindName);
    if (m == 0 || m > n) return 0;
    for (size_t s = 0; s + m <= n; ++s)
    {
        size_t j = 0;
        while (j < m && LowerAscii(kindName[s + j]) == LowerAscii(sel->piece[j])) ++j;
        if (j == m) return 1;
    }
    return 0;
}

/* The kind the lever starts WITH a choice: of the kinds the choice names that found a target, the one with the highest chance
   (PickKind's rules: the first on a tie, a chance that is not a number counts lowest; a chance of 0 still starts - the dice are
   skipped). -1 = none of the named kinds found a target. names[i] = kind i's name, "" when unread. No choice = PickKind. */
inline int PickKindSel(const int* hasTarget, const float* chance, const char (*names)[kKindNameMax], int n, const KindSel* sel)
{
    int best = -1;
    for (int i = 0; i < n; ++i)
    {
        if (hasTarget[i] == 0 || KindMatches(sel, i, names[i]) == 0) continue;
        if (best < 0) { best = i; continue; }
        const float c = chance[i], b = chance[best];
        if (c == c && (b != b || c > b)) best = i;
    }
    return best;
}

/* One town's line gate. Zero-initialise before first use. */
struct LineGate
{
    unsigned long long lastMs;   // when the last line for this town was printed
    long long suppressed;        // creation calls since that line that printed nothing
    int used;                    // 1 once a line has been printed
};

/* 1 = print a line now (*skipped = the calls not printed since the previous line; the gate is re-armed), 0 = count this call and
   print nothing. The first call always prints; a clock that went backwards prints too. */
inline int LineDue(LineGate* g, unsigned long long nowMs, long long* skipped)
{
    if (!g->used || nowMs < g->lastMs || nowMs - g->lastMs >= kLineGapMs)
    {
        *skipped = g->suppressed;
        g->suppressed = 0;
        g->lastMs = nowMs;
        g->used = 1;
        return 1;
    }
    ++g->suppressed;
    *skipped = 0;
    return 0;
}

/* The raid caller: the return address of the creation call in the campaign's squad maker (0x9C3280's call to
   createRandomUnloadedSquad), the address-table row RaidSquadCreateRet. `bound` is that row's value in the running game, 0 when
   unbound - then nothing is the raid caller. */
inline int IsRaidCaller(unsigned long long rva, unsigned long long bound) { return (bound != 0 && rva == bound) ? 1 : 0; }

}   /* namespace raidnow */
