#pragma once
/* DEDICATED (store_main.cpp): the notebook's steward rule and the recorder's CONTEXT reader - pure, swept offline by coop-test. */
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace spawnrec {

/* When the steward's connection goes: KEEP when the same person is still (or again) connected - a re-dial evicts the old socket
   while the new one is still at its HELLO, and "the person keeps the flag" is the upstream rule the eviction path relies on;
   MOVE to a game that is in the world; else VACANT (the next game to talk after its WELCOME is appointed, as upstream). A game at
   the title is never made steward: it has no world to keep. */
enum { kStewardKeep = 0, kStewardMove = 1, kStewardVacant = 2 };
inline int StewardMoveDecide(bool personStillHere, bool inWorldCandidate)
{
    if (personStillHere) return kStewardKeep;
    return inWorldCandidate ? kStewardMove : kStewardVacant;
}

/* The CONTEXT payload as net/session.cpp SendContext writes it: uid, squadType, memberType (u32 each), 32 hand bytes, 32 char bytes,
   bx/by/bz (f32), then squad sid, town sid, platoon id (u32 length + bytes each), then loadAgeMs. 1 = read. */
struct ContextRec { unsigned int uid, squadType, memberType; std::string squad, town; };
inline int ContextParse(const char* p, size_t n, ContextRec* out)
{
    const size_t kFixed = 12 + 64 + 12;
    if (p == 0 || out == 0 || n < kFixed + 4) return 0;
    std::memcpy(&out->uid, p, 4); std::memcpy(&out->squadType, p + 4, 4); std::memcpy(&out->memberType, p + 8, 4);
    size_t at = kFixed; unsigned int len = 0;
    std::memcpy(&len, p + at, 4); at += 4;
    if (len > 256 || at + len > n) return 0;
    out->squad.assign(p + at, len); at += len;
    out->town.clear();
    if (at + 4 <= n) { std::memcpy(&len, p + at, 4); at += 4; if (len <= 256 && at + len <= n) out->town.assign(p + at, len); }
    return 1;
}

/* timemode admin: does this admin's speed vote change the world's admin speed? A first vote at 1x is a standing vote (ignored); otherwise
   any vote that differs from the current admin speed is a change, whichever admin set the current one. */
inline bool AdminSpeedTakes(bool firstVote, float vote, float current)
{
    if (firstVote && vote == 1.0f) return false;
    return vote != current;
}

/* The recorder's uid sets are bounded: past this many entries both are cleared (a uid seen again reads "spawn", once). */
const size_t kRecSetMax = 200000;

/* SPAWNLOG RETENTION: the recorder writes one file per UTC day (spawnlog-YYYYMMDD.tsv) and keeps the newest `keep` of them; older day files
   are deleted when a new day's file starts and when dedicated.txt is read. dedicated.txt: spawnlog-keep=<files> (0 = keep every file;
   default kSpawnlogKeepDefault; at most kSpawnlogKeepMax). Only names of exactly that shape are ever touched. */
const char* const kSpawnlogKeepKey = "spawnlog-keep";
const int kSpawnlogKeepDefault = 30, kSpawnlogKeepMax = 3650;
inline bool SpawnlogKeepParse(const std::string& v, int* keep)
{
    if (v.empty() || v.size() > 5) return false;
    int n = 0;
    for (size_t i = 0; i < v.size(); ++i) { if (v[i] < '0' || v[i] > '9') return false; n = n * 10 + (v[i] - '0'); }
    if (n > kSpawnlogKeepMax) return false;
    *keep = n;
    return true;
}
/* "spawnlog-" + 8 digits + ".tsv", nothing else */
inline bool IsSpawnlogName(const std::string& n)
{
    if (n.size() != 9 + 8 + 4 || n.compare(0, 9, "spawnlog-") != 0 || n.compare(17, 4, ".tsv") != 0) return false;
    for (size_t i = 9; i < 17; ++i) if (n[i] < '0' || n[i] > '9') return false;
    return true;
}
/* the day files to delete: every spawnlog file but the newest `keep` (by the date in the name); keep <= 0 = none. Other names are never listed. */
inline std::vector<std::string> SpawnlogPrune(const std::vector<std::string>& names, int keep)
{
    std::vector<std::string> logs, out;
    if (keep <= 0) return out;
    for (size_t i = 0; i < names.size(); ++i) if (IsSpawnlogName(names[i])) logs.push_back(names[i]);
    std::sort(logs.begin(), logs.end());
    logs.erase(std::unique(logs.begin(), logs.end()), logs.end());
    if (logs.size() <= (size_t)keep) return out;
    out.assign(logs.begin(), logs.end() - keep);
    return out;
}

}   /* namespace spawnrec */
