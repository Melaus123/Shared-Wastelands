// joblever.h - the argument shapes of three TEST-ONLY levers, pure and offline-tested (the test program, test_main.cpp):
//   jobtest work <characterUid> <buildingKey> [plain] [task=<n>] - P17: this game's own character is given the engine's own
//                                                                  "operate this machine" order on the building (items.cpp)
//   jobtest input <buildingKey> <itemSid> <n> [section=<name>]  - P17: n of an item into the machine's input section (items.cpp)
//   jobtest input <buildingKey> need<i> <n> [section=<name>]    - T-609: the same, the item being the one the machine's i-th
//                                                                  input asks for (i = 0..; read as `jobtest show` reads it)
//   jobtest show <buildingKey>                                   - P17: read-only - the machine's name, product, inputs, sections
//   cagetest <copyUid> nearest                                   - P43: this game's engine cages the other game's character's copy
//                                                                  in the nearest free cage (spawn.cpp)
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
const int kInputMax = 1000;             // jobtest input: at most this many items in one put
const int kNeedMax = 15;                // jobtest input need<i>: i at most this (a machine's input list holds at most 16)

struct JobWork { unsigned int uid; std::string key; int task; int plain; };
struct JobInput { std::string key, sid, section; int count; int need; };   // need = i of need<i>, -1 = sid is an item sid

/* `work <uid> <key> [plain] [task=<n>]`: uid decimal or 0x hex, non-zero; no `plain` = the order as a player's (giveOrder's
   player-order argument set), `plain` = without it (the way the mod's NPC orders give one); task= overrides OPERATE_MACHINERY. Each option at most once, in either order. 1 parsed, 0 not this form or malformed. */
inline int JobWorkParse(const std::string& arg, JobWork* out)
{
    out->uid = 0; out->key.clear(); out->task = kTaskOperateMachinery; out->plain = 0;
    std::string tok[6];
    const int n = coopsay::TalkSplit(arg, tok, 6);
    if (n < 3 || tok[0] != "work") return 0;
    unsigned long long v = 0;
    if (!coopsay::TalkParseNumber(tok[1], &v) || v == 0 || v > 0xFFFFFFFFULL) return 0;
    int plain = 0, task = -1;
    for (int k = 3; k < n; ++k)
    {
        if (tok[k] == "plain" && plain == 0) { plain = 1; continue; }
        if (tok[k].compare(0, 5, "task=") == 0 && task < 0)
        {
            unsigned long long t = 0;
            if (!coopsay::TalkParseNumber(tok[k].substr(5), &t) || t == 0 || t > (unsigned long long)kTaskMax) return 0;
            task = (int)t;
            continue;
        }
        return 0;
    }
    out->uid = (unsigned int)v; out->key = tok[2]; out->plain = plain;
    if (task > 0) out->task = task;
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

/* `show <key>`. 1 parsed, 0 not. */
inline int JobShowParse(const std::string& arg, std::string* key)
{
    key->clear();
    std::string tok[3];
    if (coopsay::TalkSplit(arg, tok, 3) != 2 || tok[0] != "show") return 0;
    *key = tok[1];
    return 1;
}

/* `cagetest <copyUid> nearest`: uid decimal or 0x hex, non-zero. 1 parsed, 0 not. */
inline int CageTestParse(const std::string& arg, unsigned int* uid)
{
    *uid = 0;
    std::string tok[3];
    unsigned long long v = 0;
    if (coopsay::TalkSplit(arg, tok, 3) != 2 || tok[1] != "nearest" || !coopsay::TalkParseNumber(tok[0], &v) || v == 0
        || v > 0xFFFFFFFFULL) return 0;
    *uid = (unsigned int)v;
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
