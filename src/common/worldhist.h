#pragma once
/* T-1383 (owner request 2026-11-16, decision 1704; the approved page build/pages/world-history-mockup.html, version 3) - A WORLD'S
   HISTORY: the named characters who died and how the NPC factions feel about each other.
   A. A NEW WORLD - THE CREATOR'S IMPORT (design A, F1047). The first profile a brand-new world ever makes, when it is made with import
      from single player, holds the world's CREATOR CLAIM (world_creator.txt, kept beside profiles.txt - a new field on a profiles.txt
      row would make the file read-only to every older build, T-222). Its game may import with Kenshi's own 0x10 (dead named
      characters) and 0x20 (relations) and then gives the world its starting values: the dead rows of the engine's named-character
      table (WORLD_START, message 95) and its relations through the existing seed walk, which the world server takes first while other
      games' seeds are held (the operator's hold, S2-62). Only the claimed creator, only what it ticked, only once.
   B. RESET WORLD (design C): the world server copies its own uniques.txt DEAD rows and the whole world_relations.txt into the reset
      world when the host ticks them (--keep-dead / --keep-relations).
   Pure: the world server, the game and the offline suite read it. C++03 (VS2010). */
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "fallenwire.h"    /* swfallen::UniqueRow, UniqueParseFileLine, UniqueFileLine - uniques.txt's line */
#include "worldrelwire.h"  /* coopwrel::ParseLine - world_relations.txt's line */
#include "townverwire.h"   /* cooptv::TvParseLine - town_versions.txt's line (owner 1707 a) */
#include "townrefill.h"    /* townrefill::ParseLine - town_losses.txt's line (T-1383 fold 2) */

namespace worldhist {

/* ---- what the creator ticked (the PROFILES kind 7 request's trailing word, profiles.h) ---- */
const unsigned int kAskDead = 1u, kAskRelations = 2u, kAskMask = 3u;
inline unsigned int AskOf(bool dead, bool relations) { return (dead ? kAskDead : 0u) | (relations ? kAskRelations : 0u); }

/* ---- THE CREATOR DECISION (the world server, at a NEW with import): the world has never had a profile (profiles.txt keeps every
   deleted row, so no row = never), its named-character list, its relations table and its records are empty, nothing holds the
   claim already, and none of those files is on disk unread (a deferred load is not an empty world). Stricter than
   worldrace::WorldIsNew. ---- */
inline bool CreatorOpen(bool claimHeld, bool filesUnread, size_t profileRows, size_t uniques, size_t relations, size_t records)
{
    return !claimHeld && !filesUnread && profileRows == 0 && uniques == 0 && relations == 0 && records == 0;
}

/* ---- THE CLAIM: world_creator.txt, one line "creator1\t<person>\t<num>\t<ask>\t<done>\t<made unix>". done: the asked parts the
   world has taken (a part is taken once). The old world keeps the file at a reset (worldreset.h MOVE): a reset world has profiles. ---- */
const char* const kCreatorFile = "world_creator.txt";
struct Claim
{
    bool held; std::string person; unsigned int num, ask, done; long long made;
    Claim() : held(false), num(0), ask(0), done(0), made(0) {}
};
inline std::string ClaimText(const Claim& c)
{
    if (!c.held) return std::string();
    char b[96];
    std::sprintf(b, "\t%u\t%u\t%u\t%lld\n", c.num, c.ask & kAskMask, c.done & kAskMask, c.made);
    return "creator1\t" + c.person + b;
}
inline bool ClaimParse(const std::string& text, Claim* out)
{
    std::string line = text.substr(0, text.find('\n'));
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    std::vector<std::string> f; size_t at = 0;
    for (;;) { const size_t t = line.find('\t', at); if (t == std::string::npos) { f.push_back(line.substr(at)); break; } f.push_back(line.substr(at, t - at)); at = t + 1; }
    if (f.size() < 6 || f[0] != "creator1" || f[1].empty()) return false;
    for (size_t i = 2; i < 6; ++i) if (f[i].empty() || f[i].find_first_not_of("0123456789") != std::string::npos) return false;
    Claim c; c.held = true; c.person = f[1];
    c.num = (unsigned int)std::strtoul(f[2].c_str(), 0, 10);
    c.ask = (unsigned int)std::strtoul(f[3].c_str(), 0, 10) & kAskMask;
    c.done = (unsigned int)std::strtoul(f[4].c_str(), 0, 10) & kAskMask;
    c.made = 0;   /* a 64-bit parse (atol is 32 bits on Windows); the field holds digits only (checked above) */
    for (size_t i = 0; i < f[5].size() && c.made < 100000000000000000LL; ++i) c.made = c.made * 10 + (long long)(f[5][i] - '0');
    if (c.num == 0) return false;
    *out = c;
    return true;
}
/* the claim asked for this part and the world has not taken it yet */
inline bool Pending(const Claim& c, unsigned int bit) { return c.held && (c.ask & bit) != 0 && (c.done & bit) == 0; }

/* ---- A CREATOR-ONLY UPLOAD (the dead rows; the relations seed): taken only from the connection admitted under the claimed
   profile, for a part it asked for, once. ---- */
enum { kUpTake = 0, kUpNoClaim = 1, kUpNotCreator = 2, kUpNotAsked = 3, kUpDone = 4 };
inline int UploadVerdict(const Claim& c, bool fromIsCreator, unsigned int bit)
{
    if (!c.held) return kUpNoClaim;
    if (!fromIsCreator) return kUpNotCreator;
    if ((c.ask & bit) == 0) return kUpNotAsked;
    if ((c.done & bit) != 0) return kUpDone;
    return kUpTake;
}
inline const char* UploadVerdictName(int v)
{
    return v == kUpTake ? "taken" : v == kUpNoClaim ? "this world has no creator claim" : v == kUpNotCreator ? "not from the world's creator"
         : v == kUpNotAsked ? "the creator did not ask for it" : v == kUpDone ? "already taken once" : "?";
}

/* ---- THE RELATIONS SEED WHILE THE CREATOR'S RELATIONS ARE OWED (T-1383 fold, review 2026-11-16 item 1): the creator's seed is taken
   as the starting table (its walk's end marks the part taken and releases what was held); another game's seed - the world operator's
   own included - is held ONLY while the creator's game is connected (the lobby connection that made the claim, or a connection
   admitted under the claimed profile). THE RULE: the hold ends on a real event, never on a timer - the creator's walk ends (its
   upload) or the creator's game leaves (a creator that did not tick relations made no relations claim, so nothing is held). A creator
   whose game is not connected is not coming as far as this world can tell (it may have quit before importing): holding the
   operator's own walk behind it would leave the world without a starting table for as long as it stays away, so the operator's
   rule (S2-62) applies; if that game comes back later its walk fills only the pairs the table lacks. ---- */
enum { kRelNormal = 0, kRelTakeCreator = 1, kRelHoldForCreator = 2 };
inline int RelSeedStep(bool pending, bool fromCreator, bool creatorConnected)
{
    if (!pending) return kRelNormal;
    if (fromCreator) return kRelTakeCreator;
    if (creatorConnected) return kRelHoldForCreator;
    return kRelNormal;
}

/* ---- WORLD_START (world-server message 95, up only; 94 is T-1167's MSG_TRADE on its branches - checked unused on main and every
   effort branch, 2026-11-16): kind 1 the creator's dead named characters - {u8 kind 1, u32 n, n * (u32 len, sid bytes,
   u32 playerInvolved)}. Kind 2 is retired, never reused (T-1383 fold 2: its "no relations" road could not be reached - a creator
   that ticked relations always imports with 0x20). ---- */
const unsigned int kMsgWorldStart = 95u;
const unsigned char kWsDead = 1;
inline int KindOf(const std::vector<char>& b) { return b.empty() ? 0 : (int)(unsigned char)b[0]; }
const unsigned int kDeadMax = 8192u, kSidMax = 200u;
struct DeadRow { std::string sid; unsigned int involved; DeadRow() : involved(0) {} };
inline void PutU32(std::vector<char>* b, unsigned int v) { char c[4]; std::memcpy(c, &v, 4); b->insert(b->end(), c, c + 4); }
inline bool GetU32(const std::vector<char>& b, size_t* at, unsigned int* v)
{
    if (b.size() < *at + 4) return false;
    std::memcpy(v, &b[*at], 4); *at += 4; return true;
}
inline bool EncodeDead(std::vector<char>* b, const std::vector<DeadRow>& rows)
{
    if (rows.size() > kDeadMax) return false;
    b->clear(); b->push_back((char)kWsDead); PutU32(b, (unsigned int)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].sid.empty() || rows[i].sid.size() > kSidMax) return false;
        PutU32(b, (unsigned int)rows[i].sid.size()); b->insert(b->end(), rows[i].sid.begin(), rows[i].sid.end());
        PutU32(b, rows[i].involved != 0 ? 1u : 0u);
    }
    return true;
}
inline bool DecodeDead(const std::vector<char>& b, std::vector<DeadRow>* out)
{
    out->clear();
    if (b.empty() || (unsigned char)b[0] != kWsDead) return false;
    size_t at = 1; unsigned int n = 0;
    if (!GetU32(b, &at, &n) || n > kDeadMax) return false;
    for (unsigned int i = 0; i < n; ++i)
    {
        unsigned int len = 0, inv = 0;
        if (!GetU32(b, &at, &len) || len == 0 || len > kSidMax || b.size() < at + len) return false;
        DeadRow r; r.sid.assign(b.begin() + (long)at, b.begin() + (long)(at + len)); at += len;
        if (!GetU32(b, &at, &inv) || inv > 1u) return false;
        r.involved = inv;
        out->push_back(r);
    }
    return at == b.size();
}

/* ---- THE ENGINE'S NAMED-CHARACTER TABLE WALK (worldstate.cpp WorldStateDeadWalk; F1049): its rows by state ---- */
struct WalkCounts
{
    long long entries, dead, alive, prisoner, other, noId;
    WalkCounts() : entries(0), dead(0), alive(0), prisoner(0), other(0), noId(0) {}
};
inline void WalkNote(WalkCounts* c, int state, bool hasId)
{
    ++c->entries;
    if (!hasId) { ++c->noId; return; }
    if (state == 0) ++c->dead; else if (state == 1) ++c->alive; else if (state == 2) ++c->prisoner; else ++c->other;
}

/* ---- RESET WORLD: the reset world's uniques.txt - only the DEAD rows of the old world's file, each as it was (its bring-back
   count too); *kept = rows written, *skipped = living, imprisoned and unreadable lines left out ---- */
inline std::string DeadRowsOf(const std::string& uniquesText, long long* kept, long long* skipped)
{
    std::string out; long long k = 0, s = 0;
    size_t a = 0;
    while (a < uniquesText.size())
    {
        size_t e = uniquesText.find('\n', a);
        if (e == std::string::npos) e = uniquesText.size();
        const std::string line = uniquesText.substr(a, e - a);
        a = e + 1;
        if (line.empty() || line == "\r") continue;
        std::string sid; swfallen::UniqueRow r;
        if (swfallen::UniqueParseFileLine(line, &sid, &r) && r.state == 0) { out += swfallen::UniqueFileLine(sid, r); ++k; }
        else ++s;
    }
    if (kept != 0) *kept = k;
    if (skipped != 0) *skipped = s;
    return out;
}
/* ...and how many faction pairs the world_relations.txt it copies whole holds (a line this build reads as one world row) */
inline long long RelationPairsOf(const std::string& text)
{
    coopwrel::Table t;
    size_t a = 0;
    while (a < text.size())
    {
        size_t e = text.find('\n', a);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(a, e - a);
        a = e + 1;
        if (line.empty() || line == "\r") continue;
        coopwrel::ParseLine(line, &t, 0);
    }
    return (long long)t.size();
}
/* ...and how many town versions the town_versions.txt it copies whole holds (owner 1707 a: kept with the dead named characters - a
   town whose named characters stay dead keeps the version it was rebuilt to; a line this build reads as one town version) */
inline long long TownVersionsOf(const std::string& text)
{
    long long n = 0;
    size_t a = 0;
    while (a < text.size())
    {
        size_t e = text.find('\n', a);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(a, e - a);
        a = e + 1;
        if (line.empty() || line == "\r") continue;
        cooptv::TownVer r;
        if (cooptv::TvParseLine(line, &r)) ++n;
    }
    return n;
}
/* ...and how many town rows the town_losses.txt it copies whole holds (T-1383 fold 2: a kept town version goes with its town's
   recorded losses - without them an emptied town at its kept version reads as one nothing accounts for and is refilled once,
   townrefill::RefillDecide, where the old world kept it wiped) */
inline long long TownLossRowsOf(const std::string& text)
{
    townrefill::Table t;
    size_t a = 0;
    while (a < text.size())
    {
        size_t e = text.find('\n', a);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(a, e - a);
        a = e + 1;
        if (line.empty() || line == "\r") continue;
        townrefill::ParseLine(line, &t);
    }
    return (long long)t.size();
}
/* THE RESET'S COPIES (KeepResetWorld): which file goes into the reset world and how - uniques.txt's DEAD rows, town_versions.txt
   (owner 1707 a) and town_losses.txt (T-1383 fold 2) whole with --keep-dead, world_relations.txt whole with --keep-relations. A file
   not kept that an earlier try left in the stage is moved aside. */
const int kResetCopyCount = 4;
const char* const kResetCopyFile[kResetCopyCount] = { "uniques.txt", "world_relations.txt", "town_versions.txt", "town_losses.txt" };
inline bool ResetCopyKept(int i, bool keepDead, bool keepRelations) { return i == 1 ? keepRelations : keepDead; }
/* the reset's two new options on the world server's command line, beside --keep-buildings */
const char* const kArgKeepDead = "--keep-dead";
const char* const kArgKeepRelations = "--keep-relations";

/* ---- the log lines (the approved page, section 3) ---- */
inline const char* YesNo(bool v) { return v ? "yes" : "no"; }
inline std::string ChoseLine(unsigned int ask)
{
    return std::string("[IMPORT] the world's first player chose: dead=") + YesNo((ask & kAskDead) != 0) + " relations=" + YesNo((ask & kAskRelations) != 0);
}
inline std::string BroughtLine(long long n)
{
    char b[32]; std::sprintf(b, "%lld", n);
    return std::string("[IMPORT] world creator import: dead named characters brought (") + b + ")";
}
inline std::string ResetKeptLine(bool buildings, bool dead, long long nDead, bool relations, long long nPairs)
{
    char d[32], r[32]; std::sprintf(d, "%lld", nDead); std::sprintf(r, "%lld", nPairs);
    return std::string("[RESET] kept from the old world: buildings=") + YesNo(buildings) + " dead named characters=" + YesNo(dead) + " (" + d
         + ") relations=" + YesNo(relations) + " (" + r + " pairs)";
}
/* owner 1707 a: its own [RESET] line - the approved page's kept line has no room for it */
inline std::string TownVersKeptLine(long long n)
{
    char b[32]; std::sprintf(b, "%lld", n);
    return std::string("[RESET] kept from the old world with the dead named characters: town versions kept (") + b + ")";
}
inline std::string WalkLine(const WalkCounts& c, long long tableCount, long long sent)
{
    char b[256];
    std::sprintf(b, "[IMPORT] named-character table walk: table count %lld, %lld entries read - dead %lld, alive %lld, imprisoned %lld, other %lld,"
                    " no text id %lld; %lld dead row(s) sent", tableCount, c.entries, c.dead, c.alive, c.prisoner, c.other, c.noId, sent);
    return b;
}

}   /* namespace worldhist */
