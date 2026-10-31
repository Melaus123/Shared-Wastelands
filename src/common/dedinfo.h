#pragma once
/* DEDICATED (--dedicated notebook): DEDINFO (96, down only) - what the PLAYERS tab shows about the server's rules. Sent to games in
   the world when it changes. {u32 mode, u32 startmode, u32 count, count x {u32 slot, f32 vote, u32 flags}}. Pure; swept offline by coop-test.
   startmode (src/common/startmode.h): 0 freeplay, 1 kenshi - the world's start mode, for the record (a game learns it from the WELCOME's
   OPTIONS map, which arrives before the start is chosen). Every game on a dedicated notebook runs this plugin, so the shape changed in place. */
#include <cstring>
#include <string>
#include <vector>
#include "startmode.h"

namespace dedinfo {

const unsigned kMsgDedInfo = 96;
enum { kModeNone = 0, kModeHost = 1, kModeVote = 2, kModeAdmin = 3 };   /* locked shows nothing: kModeNone */
enum { kFlagHost = 1, kFlagAdminLoud = 2 };
const unsigned kRowsMax = 256;

/* HOSTED PLAY IS UNCHANGED: the dedicated-only rules a game applies only while linked to a dedicated world (its DEDINFO came on this link).
   StewardKeepsWorldValues (settings.cpp): a game made the world's authority MID-WORLD (the steward move) keeps the world's gameplay values
   instead of restoring its own over them - on a dedicated world only; on a hosted world it restores its own, exactly as 0.3.4 does. */
inline bool StewardKeepsWorldValues(bool relay, bool run, bool haveRecord, bool dedicatedWorld) { return dedicatedWorld && relay && run && haveRecord; }

struct Row { unsigned slot; float vote; unsigned flags; Row() : slot(0), vote(1.0f), flags(0) {} };

inline void Put32(std::vector<char>* b, unsigned v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline bool Get32(const char* p, size_t n, size_t* at, unsigned* v) { if (*at + 4 > n) return false; std::memcpy(v, p + *at, 4); *at += 4; return true; }

inline void Encode(unsigned mode, unsigned startMode, const std::vector<Row>& rows, std::vector<char>* b)
{
    Put32(b, mode); Put32(b, startMode); Put32(b, (unsigned)rows.size());
    for (size_t i = 0; i < rows.size(); ++i) { unsigned f = 0; std::memcpy(&f, &rows[i].vote, 4); Put32(b, rows[i].slot); Put32(b, f); Put32(b, rows[i].flags); }
}
inline bool Decode(const char* p, size_t n, unsigned* mode, unsigned* startMode, std::vector<Row>* rows)
{
    size_t at = 0; unsigned m = 0, sm = 0, cnt = 0;
    if (p == 0 || !Get32(p, n, &at, &m) || !Get32(p, n, &at, &sm) || !Get32(p, n, &at, &cnt) || m > (unsigned)kModeAdmin
        || sm > (unsigned)swstart::kKenshi || cnt > kRowsMax) return false;
    std::vector<Row> out;
    for (unsigned i = 0; i < cnt; ++i)
    {
        Row r; unsigned f = 0;
        if (!Get32(p, n, &at, &r.slot) || !Get32(p, n, &at, &f) || !Get32(p, n, &at, &r.flags)) return false;
        std::memcpy(&r.vote, &f, 4);
        out.push_back(r);
    }
    if (at != n) return false;
    *mode = m; *startMode = sm; *rows = out;
    return true;
}

/* The words for one player: `prefix` goes before the name (an announced admin's star), `status` after the STATUS column's word. */
inline std::string VoteWord(float v)
{
    if (v <= 0.0f) return "paused";
    if (v < 1.5f) return "x1";
    if (v < 2.5f) return "x2";
    return "x3";
}
inline void Badge(unsigned mode, const Row& r, std::string* prefix, std::string* status)
{
    *prefix = (r.flags & kFlagAdminLoud) ? "* " : "";
    status->clear();
    if (mode == (unsigned)kModeHost && (r.flags & kFlagHost)) *status = "Host";
    else if (mode == (unsigned)kModeVote) *status = "votes " + VoteWord(r.vote);
    else if (mode == (unsigned)kModeAdmin && (r.flags & kFlagAdminLoud)) *status = "Admin";
}

}   /* namespace dedinfo */
