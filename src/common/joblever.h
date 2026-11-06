// joblever.h - the argument shapes of three TEST-ONLY levers, pure and offline-tested (the test program, test_main.cpp):
//   jobtest work <characterUid> <buildingKey> [plain] [task=<n>] - P17: this game's own character is given the engine's own
//                                                                  "operate this machine" order on the building (items.cpp)
//                                                                  [door=<i>]: the order on door i of the building instead
//   jobtest click <characterUid> <buildingKey> [near=<x>,<z>]   - T-1191: the direct order a player's right click on the building
//                                                                  gives (items.cpp JobClickRun)
//   jobtest input <buildingKey> <itemSid> <n> [section=<name>]  - P17: n of an item into the machine's input section (items.cpp)
//   jobtest input <buildingKey> need<i> <n> [section=<name>]    - T-609: the same, the item being the one the machine's i-th
//                                                                  input asks for (i = 0..; read as `jobtest show` reads it)
//   jobtest show <needle...> [near=<x>,<z>]                      - P17: read-only - the machine's name, product, inputs, sections
//   The <needle> of work / show / list is one or more words, matched in any case against a loaded building's, furniture row's or
//   interior piece's instance id, position key, record id or name; near=<x>,<z> takes the match nearest that world point.
//   jobtest list <needle...> [radius]                           - read-only - every match, with its position and distance
//   jobtest craft <buildingKey> <productSid> [count]             - T-1192: that product added `count` times to a crafting bench's
//                                                                  craft list, as the crafting window's recipe click does (items.cpp JobCraftRun)
//   cagetest <copyUid> nearest [faction=<name>] [hours=<n>]      - P43: this game's engine cages the other game's character's copy
//                                                                  in the nearest free cage (spawn.cpp); T-478: of that faction,
//                                                                  with a started sentence of n game hours (default 24)
//   boxdigest [<window>] players                                 - the digest with player-owned boxes included (items.cpp)
//   cursortest open|pick|drop|square placed|copyof:<sid> ...     - T-609 (T1367): a box named without its key (CursorBoxArg)
// No engine type and no logging here.
#pragma once
#include <cstdlib>
#include <string>
#include "saywire.h"   /* TalkSplit / TalkParseNumber - the levers' shared token and number readers */
#include "poskey.h"    /* T-609: ParseKeySpot / SidFieldsSame - a position key's record field (CursorKeyIsRecord) */

namespace joblever {

const int kTaskOperateMachinery = 87;   // TaskType OPERATE_MACHINERY, "Operating machine" (.modding/03-systems/taskdata-table.md)
const int kTaskMax = 1000;              // a task= override above this is a typing error, not a task type
const int kWorkDoorMax = 31;            // jobtest work door=<i>: i at most this (the door list read holds at most 32)
const int kInputMax = 1000;             // jobtest input: at most this many items in one put
const int kNeedMax = 15;                // jobtest input need<i>: i at most this (a machine's input list holds at most 16)

struct JobWork { unsigned int uid; std::string key; int task; int plain; int door; int hasNear; float nearX, nearZ; };   // door -1 = the building itself
struct JobPick { std::string key; int hasNear; float nearX, nearZ; };   // `show <needle...> [near=<x>,<z>]`
struct JobInput { std::string key, sid, section; int count; int need; };   // need = i of need<i>, -1 = sid is an item sid

const int kNeedleTokMax = 12;            // jobtest: at most this many words in one command line
const double kJobCoordMax = 10000000.0;  // near=: a world coordinate beyond this is a typing error
const int kJobListRadiusMax = 1000000;   // jobtest list: the largest radius (world units)

/* One ASCII letter lower-cased (the needle match ignores case). */
inline char JobLow(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* Does `hay` contain `needle`, ignoring ASCII case? An empty needle matches nothing. 1 yes, 0 no. */
inline int JobNeedleMatches(const std::string& hay, const std::string& needle)
{
    if (needle.empty() || needle.size() > hay.size()) return 0;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i)
    {
        size_t k = 0;
        while (k < needle.size() && JobLow(hay[i + k]) == JobLow(needle[k])) ++k;
        if (k == needle.size()) return 1;
    }
    return 0;
}

/* A world coordinate - an optional sign, digits, an optional '.' and digits (at least one digit). 1 parsed, 0 not. */
inline int JobCoordParse(const std::string& s, float* out)
{
    size_t i = 0;
    int neg = 0, digits = 0, frac = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-') ? 1 : 0; ++i; }
    double v = 0.0, scale = 1.0;
    for (; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '.' && frac == 0) { frac = 1; continue; }
        if (c < '0' || c > '9') return 0;
        ++digits;
        if (frac != 0) { scale /= 10.0; v += (double)(c - '0') * scale; }
        else v = v * 10.0 + (double)(c - '0');
        if (v > kJobCoordMax) return 0;
    }
    if (digits == 0) return 0;
    *out = (float)(neg != 0 ? -v : v);
    return 1;
}

/* `near=<x>,<z>` (world units, the x and z of a world position). 1 parsed, 0 not this option or malformed. */
inline int JobNearParse(const std::string& tok, float* x, float* z)
{
    if (tok.compare(0, 5, "near=") != 0) return 0;
    const std::string v = tok.substr(5);
    const size_t c = v.find(',');
    if (c == std::string::npos || v.find(',', c + 1) != std::string::npos) return 0;
    float a = 0.0f, b = 0.0f;
    if (!JobCoordParse(v.substr(0, c), &a) || !JobCoordParse(v.substr(c + 1), &b)) return 0;
    *x = a; *z = b;
    return 1;
}

/* The needle = the words from `from` up to the first option word (near=..., and with workOptions also `plain`, task=... and
   door=...), joined by one space, so `Training Dummy` is one needle. *next = the first word after it. */
inline std::string JobNeedleJoin(const std::string* tok, int n, int from, int workOptions, int* next)
{
    std::string needle;
    int k = from;
    for (; k < n; ++k)
    {
        const std::string& t = tok[k];
        if (t.compare(0, 5, "near=") == 0) break;
        if (workOptions != 0 && (t == "plain" || t.compare(0, 5, "task=") == 0 || t.compare(0, 5, "door=") == 0)) break;
        if (!needle.empty()) needle += " ";
        needle += t;
    }
    *next = k;
    return needle;
}

/* WHICH MATCH `jobtest show` / `work` take. Without near: the first (index 0 - the walk order). With near: the one whose x,z is
   nearest (nx, nz) among those with a position (has[i] != 0), the earlier one on a tie. -1 = none (no match, or near asked and
   no match has a position). */
inline int JobPickNearest(const float* xs, const float* zs, const int* has, int n, int hasNear, float nx, float nz)
{
    if (n <= 0) return -1;
    if (hasNear == 0) return 0;
    int best = -1;
    double bestD = 0.0;
    for (int i = 0; i < n; ++i)
    {
        if (has[i] == 0) continue;
        const double dx = (double)xs[i] - (double)nx, dz = (double)zs[i] - (double)nz, d = dx * dx + dz * dz;
        if (best < 0 || d < bestD) { best = i; bestD = d; }
    }
    return best;
}

/* `work <uid> <needle...> [plain] [task=<n>] [door=<i>] [near=<x>,<z>]`: uid decimal or 0x hex, non-zero; the needle is every word
   up to the first option (so it never holds the word `plain` or a word starting task= / door= / near=); no `plain` = the order as
   a player's (giveOrder's player-order argument set), `plain` = without it (the way the mod's NPC orders give one); task= overrides
   OPERATE_MACHINERY; door=<i> (decimal 0..kWorkDoorMax) gives the order on door i of the building (out->door, -1 = the building
   itself); near= takes the match nearest that world point. Each option at most once, in any order, all after the needle.
   1 parsed, 0 not this form or malformed. */
inline int JobWorkParse(const std::string& arg, JobWork* out)
{
    out->uid = 0; out->key.clear(); out->task = kTaskOperateMachinery; out->plain = 0; out->door = -1;
    out->hasNear = 0; out->nearX = 0.0f; out->nearZ = 0.0f;
    std::string tok[kNeedleTokMax];
    const int n = coopsay::TalkSplit(arg, tok, kNeedleTokMax);
    if (n < 3 || tok[0] != "work") return 0;
    unsigned long long v = 0;
    if (!coopsay::TalkParseNumber(tok[1], &v) || v == 0 || v > 0xFFFFFFFFULL) return 0;
    int k = 2;
    const std::string needle = JobNeedleJoin(tok, n, 2, 1, &k);
    if (needle.empty()) return 0;
    int plain = 0, task = -1, door = -1, hasNear = 0;
    float nx = 0.0f, nz = 0.0f;
    for (; k < n; ++k)
    {
        if (tok[k].compare(0, 5, "near=") == 0 && hasNear == 0)
        {
            if (!JobNearParse(tok[k], &nx, &nz)) return 0;
            hasNear = 1;
            continue;
        }
        if (tok[k] == "plain" && plain == 0) { plain = 1; continue; }
        if (tok[k].compare(0, 5, "door=") == 0 && door < 0)
        {
            const std::string d = tok[k].substr(5);
            if (d.empty() || d.size() > 2) return 0;
            for (size_t q = 0; q < d.size(); ++q) if (d[q] < '0' || d[q] > '9') return 0;
            door = std::atoi(d.c_str());
            if (door > kWorkDoorMax) return 0;
            continue;
        }
        if (tok[k].compare(0, 5, "task=") == 0 && task < 0)
        {
            unsigned long long t = 0;
            if (!coopsay::TalkParseNumber(tok[k].substr(5), &t) || t == 0 || t > (unsigned long long)kTaskMax) return 0;
            task = (int)t;
            continue;
        }
        return 0;
    }
    out->uid = (unsigned int)v; out->key = needle; out->plain = plain; out->door = door;
    if (task > 0) out->task = task;
    out->hasNear = hasNear; out->nearX = nx; out->nearZ = nz;
    return 1;
}

/* T-1191 `click <uid> <needle...> [near=<x>,<z>]`: this game's own character gets the order a player's right-click on the building
   gives (items.cpp JobClickRun). uid decimal or 0x hex, non-zero; the needle as `work` reads it (no word `plain`, task= or door=:
   the click picks its own task, on the building itself); near= at most once. out->task / plain / door are left at work's defaults
   and unused. 1 parsed, 0 not this form or malformed. */
inline int JobClickParse(const std::string& arg, JobWork* out)
{
    out->uid = 0; out->key.clear(); out->task = kTaskOperateMachinery; out->plain = 0; out->door = -1;
    out->hasNear = 0; out->nearX = 0.0f; out->nearZ = 0.0f;
    std::string tok[kNeedleTokMax];
    const int n = coopsay::TalkSplit(arg, tok, kNeedleTokMax);
    if (n < 3 || tok[0] != "click") return 0;
    unsigned long long v = 0;
    if (!coopsay::TalkParseNumber(tok[1], &v) || v == 0 || v > 0xFFFFFFFFULL) return 0;
    int k = 2;
    const std::string needle = JobNeedleJoin(tok, n, 2, 1, &k);
    if (needle.empty()) return 0;
    int hasNear = 0;
    float nx = 0.0f, nz = 0.0f;
    for (; k < n; ++k)
    {
        if (tok[k].compare(0, 5, "near=") == 0 && hasNear == 0)
        {
            if (!JobNearParse(tok[k], &nx, &nz)) return 0;
            hasNear = 1;
            continue;
        }
        return 0;
    }
    out->uid = (unsigned int)v; out->key = needle;
    out->hasNear = hasNear; out->nearX = nx; out->nearZ = nz;
    return 1;
}

/* `input <key> <sid> <n> [section=<name>]`: n decimal 1..kInputMax; no section = the section whose limit list names the item.
   T-609: <sid> may be `need<i>` (i decimal 0..kNeedMax) = the item the machine's i-th input asks for; out->need = i, out->sid
   stays "need<i>". `need` with nothing, or with anything but decimal digits after it, is malformed (0). */
inline int JobInputParse(const std::string& arg, JobInput* out)
{
    out->key.clear(); out->sid.clear(); out->section.clear(); out->count = 0; out->need = -1;
    std::string tok[6];
    const int n = coopsay::TalkSplit(arg, tok, 6);
    if ((n != 4 && n != 5) || tok[0] != "input") return 0;
    unsigned long long v = 0;
    if (tok[3].size() > 2 && (tok[3][1] == 'x' || tok[3][1] == 'X')) return 0;   // a count is decimal
    if (!coopsay::TalkParseNumber(tok[3], &v) || v == 0 || v > (unsigned long long)kInputMax) return 0;
    std::string sec;
    if (n == 5)
    {
        if (tok[4].compare(0, 8, "section=") != 0 || tok[4].size() == 8) return 0;
        sec = tok[4].substr(8);
    }
    int need = -1;
    if (tok[2].compare(0, 4, "need") == 0)
    {
        const std::string d = tok[2].substr(4);
        if (d.empty() || d.size() > 2) return 0;
        for (size_t k = 0; k < d.size(); ++k) if (d[k] < '0' || d[k] > '9') return 0;
        need = std::atoi(d.c_str());
        if (need > kNeedMax) return 0;
    }
    out->key = tok[1]; out->sid = tok[2]; out->count = (int)v; out->section = sec; out->need = need;
    return 1;
}

/* T-610 `stop <uid>`: uid decimal or 0x hex, non-zero, nothing after it. 1 parsed, 0 not. */
inline int JobStopParse(const std::string& arg, unsigned int* uid)
{
    *uid = 0;
    std::string tok[3];
    if (coopsay::TalkSplit(arg, tok, 3) != 2 || tok[0] != "stop") return 0;
    unsigned long long v = 0;
    if (!coopsay::TalkParseNumber(tok[1], &v) || v == 0 || v > 0xFFFFFFFFULL) return 0;
    *uid = (unsigned int)v;
    return 1;
}

/* `dojobs <uid> 0|1`. 1 parsed, 0 not. */
inline int JobDoJobsParse(const std::string& arg, unsigned int* uid, int* on)
{
    *uid = 0; *on = 0;
    std::string tok[4];
    if (coopsay::TalkSplit(arg, tok, 4) != 3 || tok[0] != "dojobs") return 0;
    unsigned long long v = 0;
    if (!coopsay::TalkParseNumber(tok[1], &v) || v == 0 || v > 0xFFFFFFFFULL) return 0;
    if (tok[2] != "0" && tok[2] != "1") return 0;
    *uid = (unsigned int)v;
    *on = (tok[2] == "1") ? 1 : 0;
    return 1;
}

/* `show <needle...> [near=<x>,<z>]`: a needle of one or more words; near= takes the match nearest that world point, and must be
   the last word. 1 parsed, 0 not. */
inline int JobShowParse(const std::string& arg, JobPick* out)
{
    out->key.clear(); out->hasNear = 0; out->nearX = 0.0f; out->nearZ = 0.0f;
    std::string tok[kNeedleTokMax];
    const int n = coopsay::TalkSplit(arg, tok, kNeedleTokMax);
    if (n < 2 || tok[0] != "show") return 0;
    int k = 1;
    const std::string needle = JobNeedleJoin(tok, n, 1, 0, &k);
    if (needle.empty()) return 0;
    if (k < n)
    {
        float x = 0.0f, z = 0.0f;
        if (k != n - 1 || !JobNearParse(tok[k], &x, &z)) return 0;
        out->hasNear = 1; out->nearX = x; out->nearZ = z;
    }
    out->key = needle;
    return 1;
}

/* `list <needle...> [radius]` (read only): a last word of digits only, after at least one needle word, is the radius in world
   units (1..kJobListRadiusMax); without it *radius = 0 (no limit). The needle is every other word. 1 parsed, 0 not. */
inline int JobListParse(const std::string& arg, std::string* needle, int* radius)
{
    needle->clear(); *radius = 0;
    std::string tok[kNeedleTokMax];
    int n = coopsay::TalkSplit(arg, tok, kNeedleTokMax);
    if (n < 2 || tok[0] != "list") return 0;
    int r = 0;
    if (n >= 3)
    {
        const std::string& last = tok[n - 1];
        size_t d = 0;
        while (d < last.size() && last[d] >= '0' && last[d] <= '9') ++d;
        if (d == last.size())
        {
            unsigned long long v = 0;
            if (!coopsay::TalkParseNumber(last, &v) || v == 0 || v > (unsigned long long)kJobListRadiusMax) return 0;
            r = (int)v;
            --n;
        }
    }
    std::string w;
    for (int k = 1; k < n; ++k) { if (!w.empty()) w += " "; w += tok[k]; }
    *needle = w; *radius = r;
    return 1;
}

/* T-1192 `craft <buildingKey> <productSid> [count]`: the product added to the bench's craft list as the crafting window's recipe
   click does (items.cpp JobCraftRun, the engine's CraftQueueAdd 0x2B5C60 on 1.0.65), once per count. count decimal
   1..kCraftCountMax, default 1. 1 parsed, 0 not. */
const int kCraftCountMax = 1000;
struct JobCraft { std::string key, sid; int count; };
inline int JobCraftParseVerb(const std::string& arg, const char* verb, int countOk, JobCraft* out)
{
    out->key.clear(); out->sid.clear(); out->count = 0;
    std::string tok[5];
    int n = 0;
    for (size_t i = 0; i < arg.size(); )   // words split on spaces and tabs; more than 4 words is malformed
    {
        while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t')) ++i;
        if (i >= arg.size()) break;
        if (n == 5) return 0;
        const size_t s = i;
        while (i < arg.size() && arg[i] != ' ' && arg[i] != '\t') ++i;
        tok[n++] = arg.substr(s, i - s);
    }
    if ((n != 3 && !(n == 4 && countOk != 0)) || tok[0] != verb) return 0;
    int count = 1;
    if (n == 4)
    {
        if (tok[3].empty() || tok[3].size() > 4) return 0;   // decimal digits only, at most kCraftCountMax
        int v = 0;
        for (size_t k = 0; k < tok[3].size(); ++k)
        {
            if (tok[3][k] < '0' || tok[3][k] > '9') return 0;
            v = v * 10 + (tok[3][k] - '0');
        }
        if (v == 0 || v > kCraftCountMax) return 0;
        count = v;
    }
    out->key = tok[1]; out->sid = tok[2]; out->count = count;
    return 1;
}
inline int JobCraftParse(const std::string& arg, JobCraft* out) { return JobCraftParseVerb(arg, "craft", 1, out); }
/* T-1192 TEST-ONLY `craftwin <buildingKey> <productSid>`: the bench's crafting window opened as the game opens it for a building
   (0x2B5050 on 1.0.65), the recipe row naming the product found among the window's own row widgets (user string "id"), the
   window's own add 0x2B9730 called with that row through its hooked entry (so the T-1192 window route runs), then the window
   closed. One click per line (a second click on a game that asks is held as a duplicate until the answer). 1 parsed, 0 not. */
inline int JobCraftWinParse(const std::string& arg, JobCraft* out) { return JobCraftParseVerb(arg, "craftwin", 0, out); }
/* Why `jobtest craftwin` did not click (0 = it clicked) - the refusal words of its [JOB] line */
enum { kCwOk = 0, kCwNoAddress = 1, kCwNoHook = 2, kCwOpenFaulted = 3, kCwNoWindow = 4, kCwOtherBench = 5, kCwNoRow = 6, kCwAddFaulted = 7 };
inline const char* JobCraftWinWhy(int code)
{
    switch (code)
    {
    case kCwOk:          return "clicked";
    case kCwNoAddress:   return "an address the lever needs (CraftOpenFor, OverviewShow, OverviewWindowGlobal, CraftWindowAdd, the window's bench lookup) is not on this game's executable";
    case kCwNoHook:      return "the crafting window's add is not hooked here, so the click would not take the window route";
    case kCwOpenFaulted: return "the game's own open of the crafting window FAULTED";
    case kCwNoWindow:    return "no crafting window after the open (the overview window or its crafting part is not made)";
    case kCwOtherBench:  return "the opened window does not show this bench";
    case kCwNoRow:       return "no recipe row in the window names the product (the bench does not offer it here)";
    case kCwAddFaulted:  return "the window's add FAULTED";
    }
    return "?";
}
/* The click's outcome from what changed on this game: the bench's craft list grew by one (a lone game or the bench's writer adds
   here) or one recipe was asked of the writer (CRAFT_ASK) and nothing added here. Anything else (both, neither, a held duplicate,
   a refused add) is not ok. 1 ok, 0 not. */
inline int JobCraftWinOk(int clicked, long long listDelta, long long askedDelta)
{
    if (clicked != 1) return 0;
    return ((listDelta == 1 && askedDelta == 0) || (listDelta == 0 && askedDelta == 1)) ? 1 : 0;
}
inline const char* JobCraftWinRoute(long long listDelta, long long askedDelta)
{
    if (listDelta == 1 && askedDelta == 0) return "added here";
    if (listDelta == 0 && askedDelta == 1) return "asked of the writer";
    return "neither";
}
/* The class a crafting bench's vtable names through RTTI (keptwire.h's production class list) - exactly, not a part of it */
inline int JobCraftClassOk(const std::string& cls) { return cls == ".?AVCraftingBuilding@@" ? 1 : 0; }
/* Why `jobtest craft` did not call the engine (0 = it did) - the refusal words of its [JOB] line */
enum { kCraftOk = 0, kCraftNoBuilding = 1, kCraftNotBench = 2, kCraftNoRecord = 3, kCraftNoFunction = 4, kCraftNoSlot = 5, kCraftFaulted = 6 };
inline const char* JobCraftWhy(int code)
{
    switch (code)
    {
    case kCraftOk:         return "called";
    case kCraftNoBuilding: return "no loaded building with an inventory matched the key";
    case kCraftNotBench:   return "not a crafting bench (its class is not CraftingBuilding)";
    case kCraftNoRecord:   return "the product sid is not a record on this game";
    case kCraftNoFunction: return "the bench's functionality record (+0x400) is unreadable";
    case kCraftNoSlot:     return "the craft list add (CraftQueueAdd) has no address on this game's executable";
    case kCraftFaulted:    return "the engine call FAULTED";
    }
    return "?";
}

/* `cagetest <copyUid> nearest [faction=<name>] [hours=<n>]`: uid decimal or 0x hex, non-zero. T-478 (owner 1446 b): the jailing
   faction is not a field the engine stores - a prisoner is held by the faction of the cage it is in (GetBountyFaction 0x851140
   resolves the cage through the prisoner's own hand) - so faction=<name> picks the nearest free cage OF that faction (its name or
   stringID, as FindFactionByKey reads it; a name may hold spaces and runs to the next hours= or the end); no faction = the nearest
   cage, held by its own faction. hours=<n>: the sentence, 1..kCageTestMaxHours whole game hours, default kCageTestDefaultHours.
   Each key once. 1 parsed, 0 not. */
const int kCageTestDefaultHours = 24;
const int kCageTestMaxHours = 9999;          // the engine's own cap is 999999 (853a80:17); a typing error is refused well below it
const size_t kCageTestFactionMax = 64;
inline int CageTestParse(const std::string& arg, unsigned int* uid, std::string* faction, int* hours)
{
    *uid = 0; faction->clear(); *hours = kCageTestDefaultHours;
    const int kTokMax = 16;
    std::string tok[kTokMax];
    const int n = coopsay::TalkSplit(arg, tok, kTokMax);
    unsigned long long v = 0;
    if (n < 2 || tok[1] != "nearest" || !coopsay::TalkParseNumber(tok[0], &v) || v == 0 || v > 0xFFFFFFFFULL) return 0;
    bool inFaction = false, sawFaction = false, sawHours = false;
    std::string fac;
    int h = kCageTestDefaultHours;
    for (int k = 2; k < n; ++k)
    {
        const std::string& t = tok[k];
        if (t.size() >= 6 && t.compare(0, 6, "hours=") == 0)
        {
            unsigned long long hv = 0;
            if (sawHours || !coopsay::TalkParseNumber(t.substr(6), &hv) || hv == 0 || hv > (unsigned long long)kCageTestMaxHours) return 0;
            h = (int)hv; sawHours = true; inFaction = false;
        }
        else if (t.size() >= 8 && t.compare(0, 8, "faction=") == 0)
        {
            if (sawFaction) return 0;
            fac = t.substr(8); sawFaction = true; inFaction = true;
        }
        else if (inFaction) { if (!fac.empty()) fac += " "; fac += t; }
        else return 0;
    }
    if (sawFaction && (fac.empty() || fac.size() > kCageTestFactionMax)) return 0;
    *uid = (unsigned int)v; *faction = fac; *hours = h;
    return 1;
}

/* T-980 `capturetest cage <carrierUid> [near | <cageKeySubstring>] [type=<n>]` (arg = the text after "cage"): the carrier decimal or
   0x hex, non-zero; no cage word or `near` = the nearest cage with a free slot (*sub left empty); one cage word at most; type 1..1000,
   default 108 PUT_IN_CAGE - the task the engine's own right-click on a cage gives a character carrying someone (decomp_7f9280.txt:
   185-193: the building's function 8 -> 0x6C). 1 parsed, 0 not. */
const int kCageOrderDefaultType = 108;
inline int CageOrderParse(const std::string& arg, unsigned int* carrier, std::string* sub, int* type)
{
    *carrier = 0; sub->clear(); *type = kCageOrderDefaultType;
    std::string tok[4];
    const int n = coopsay::TalkSplit(arg, tok, 4);
    unsigned long long v = 0;
    if (n < 1 || n > 3 || !coopsay::TalkParseNumber(tok[0], &v) || v == 0 || v > 0xFFFFFFFFULL) return 0;
    int typeSeen = 0, cageSeen = 0;
    for (int k = 1; k < n; ++k)
    {
        if (tok[k].compare(0, 5, "type=") == 0)
        {
            unsigned long long t = 0;
            if (typeSeen != 0 || !coopsay::TalkParseNumber(tok[k].substr(5), &t) || t == 0 || t > 1000) return 0;
            *type = (int)t;
            typeSeen = 1;
        }
        else
        {
            if (cageSeen != 0) return 0;
            cageSeen = 1;
            if (tok[k] != "near") *sub = tok[k];
        }
    }
    *carrier = (unsigned int)v;
    return 1;
}

/* A cage by its record's name: the jail and kidnap cages are named "... Cage" (Prisoner Cage, Spiked Cage -
   .modding/investigations/captivity.md 1). Case-insensitive "cage" anywhere in the name. */
inline int CageNameMatches(const std::string& name)
{
    for (size_t i = 0; i + 4 <= name.size(); ++i)
    {
        const char a = (char)(name[i] | 0x20), b = (char)(name[i + 1] | 0x20), c = (char)(name[i + 2] | 0x20), d = (char)(name[i + 3] | 0x20);
        if (a == 'c' && b == 'a' && c == 'g' && d == 'e') return 1;
    }
    return 0;
}

/* `boxdigest ... players`: a last token `players` asks for player-owned boxes too; *window = the argument without it. 1 = asked. */
inline int BoxDigestPlayersSplit(const std::string& arg, std::string* window)
{
    *window = arg;
    const std::string w = " players";
    if (arg == "players") { window->clear(); return 1; }
    if (arg.size() > w.size() && arg.compare(arg.size() - w.size(), w.size(), w) == 0)
    {
        size_t e = arg.size() - w.size();
        while (e > 0 && (arg[e - 1] == ' ' || arg[e - 1] == '\t')) --e;
        *window = arg.substr(0, e);
        return 1;
    }
    return 0;
}

/* T-609 (T1367) TEST-ONLY: the box argument of `cursortest open | pick | drop | square` (items.cpp P105BoxArg). A box key holds the
   position to 0.1 unit, so a run cannot name a piece placed during the run by its key; these two words name it instead:
     placed            - the exact piece the last `buildtest place` made on this game (as `bagtest ... placed`)
     copyof:<sid>      - the nearest loaded COPY (a stand-in faction owns it) of record <sid> to this game's own character - on game B,
                         the piece game A placed
   kBoxArgKey = anything else (a key, me, peer, a uid, name:...), passed on unchanged; kBoxArgBad = `copyof:` with no sid, or a sid
   holding '@' or a space (a key, not a record). *sid = the record sid of copyof:. */
enum { kBoxArgBad = -1, kBoxArgKey = 0, kBoxArgPlaced = 1, kBoxArgCopyOf = 2 };
inline int CursorBoxArg(const std::string& w, std::string* sid)
{
    sid->clear();
    if (w == "placed") return kBoxArgPlaced;
    if (w.compare(0, 7, "copyof:") != 0) return kBoxArgKey;
    const std::string s = w.substr(7);
    if (s.empty() || s.find('@') != std::string::npos || s.find(' ') != std::string::npos || s.size() >= (size_t)poskey::kSidRoom) return kBoxArgBad;
    *sid = s;
    return kBoxArgCopyOf;
}
/* 1 = the position key's record field names record `sid` (verbatim, or the "#"+hash form a long key carries); 0 = another record or
   not a key */
inline int CursorKeyIsRecord(const std::string& key, const std::string& sid)
{
    char f[poskey::kSidRoom];
    int sx = 0, sy = 0;
    long x10 = 0, z10 = 0;
    if (sid.empty() || poskey::ParseKeySpot(key.c_str(), f, poskey::kSidRoom, &sx, &sy, &x10, &z10) == 0) return 0;
    return poskey::SidFieldsSame(f, sid.c_str());
}

}   // namespace joblever
