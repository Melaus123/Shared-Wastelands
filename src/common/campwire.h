/* src/common/campwire.h - ONE LIST OF THE WORLD'S BANDIT CAMPS AND ANIMAL DENS, KEPT BY THE WORLD SERVER (message 93 CAMP, T-929
 * design B). Pure: the world server (src/coop-store/store_main.cpp) and the offline suite (src/coop-test) compile this same header.
 *
 * WHY. Every 2 s the engine's camp spawner (1.0.65 0x8F8370 -> 0x8F6440) picks, for a region under its camp limit, a camp kind
 * (0x57F880, the region's "nests" list) and a random spot (0x8F5480 -> 0x8F4200) and builds the camp (0x582970). Each game draws
 * alone, so each game had its own camps (05-findings "Each game invents its own bandit camps", run T1852). The world's camps are
 * one list here instead: a game PROPOSES the camp its spawner drew, the world server takes it once, and every game builds the same
 * camp at the same spot (the engine's furniture step reseeds from the camp's position, so the same kind + spot gives the same fires,
 * bedrolls and loot).
 *
 * WHAT IS STORED. One record per camp: key = "<kind sid>@<x>,<z>" (CampKey - the spot rounded to whole units, the town-version key
 * form), the kind's sid, the exact spot (the three floats' bits), the region key (the sid of the region's world data, whose camp
 * limit counts the camp) and that limit as the proposer read it, the camp faction's sid and the slot of the game that proposed it.
 *
 * THE LIST EXISTS OR NOT. camps.txt absent = the world has no camp list yet: the first game to say so publishes all its camps as
 * the list (SEED, owner 1376); once taken, the list exists (an empty list is still a list) and every game's camps are made to
 * match it (CampListDiff). camps.txt on disk but unreadable = every write is refused this session (nothing is written over it).
 *
 * WIRE: u8 op, three zero bytes, then by op (a string is u32 length + bytes, <= kCampStrMax, no tab / CR / LF / NUL; a record is
 * str key, str kind, str region, str fac, u32 xbits, u32 ybits, u32 zbits, u32 regionCap, i32 slot). Nothing may follow.
 *   up   1 PROPOSE   record (slot ignored) - the camp this game's spawner drew; nothing is built until the answer
 *   down 2 STORED    record - a stored camp: inside a list (BEGIN..END), an accepted proposal (to every game whose feed is on and
 *                    ALWAYS to its proposer - the proposer's "accepted"), or the stored camp a refused duplicate collided with
 *   down 3 REFUSED   u32 reason, record (the proposal) - to the proposer only (kCampDuplicate is followed by the stored record);
 *                    also the answer to a CAMP message that could not be read (kCampBad, key "?" - the game ends every waiting
 *                    proposal), to a GONE while camps.txt is unreadable (kCampUnreadable, the GONE's key) and to a PROPOSE from a
 *                    connection with no player slot (kCampNotAdmitted, the proposal)
 *   down 4 BEGIN     u32 count, u32 state - the whole list follows (count STORED); state kListNone = no list yet (a SEED is due),
 *                    kListHere = the list, kListUnreadable = camps.txt could not be read this session (a game keeps its own camps)
 *   down 5 END       u32 count - the whole list is here (count = the BEGIN's)
 *   up   6 SEED      record - one camp of this game's save, while the world has no list
 *   up   7 SEED_END  u32 count - this game's seed is whole (count = the SEED records sent since its last SEED_END)
 *   down 8 SEEDED    u32 taken, u32 count - the answer to a SEED_END; then the whole list follows (to every game if taken)
 *   up   9 GONE      str key - the camp is gone from the world (reported by the game running its area)
 *   down 10 REMOVED  str key - to every game whose feed is on and always to the reporter (an unknown key: to the reporter alone)
 *   up   11 NOT_BUILT u32 reason, str key - a game could not build a stored camp; counted and logged, nothing changes
 * FILE (world folder camps.txt): first line "camps\tv1", then "c1\t<key>\t<kind>\t<region>\t<fac>\t<xbits>\t<ybits>\t<zbits>\t<cap>\t<slot>"
 * one line per camp (bits in hex). */
#ifndef COOP_COMMON_CAMPWIRE_H
#define COOP_COMMON_CAMPWIRE_H

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace campl {

const unsigned int kMsgCamp = 93u;      /* 28-92, 97, 100, 101, 103 are taken on the world-server link; 96-103 are admincmd's reserve */
const unsigned int kProtocol = 145u;    /* the world-server protocol that carries CAMP */

const char* const kCampFile = "camps.txt";

enum { kOpPropose = 1, kOpStored = 2, kOpRefused = 3, kOpBegin = 4, kOpEnd = 5, kOpSeed = 6, kOpSeedEnd = 7, kOpSeeded = 8,
       kOpGone = 9, kOpRemoved = 10, kOpNotBuilt = 11 };
/* the world server's answer to a PROPOSE */
enum { kCampAccept = 0, kCampDuplicate = 1, kCampRegionFull = 2, kCampBad = 3, kCampUnreadable = 4, kCampNoList = 5, kCampTooSoon = 6, kCampNotAdmitted = 7, kCampVerdicts = 8 };
/* a BEGIN's list state */
enum { kListNone = 0, kListHere = 1, kListUnreadable = 2 };
/* a game's NOT_BUILT reason */
enum { kNbNoKind = 1, kNbNoFaction = 2, kNbEngineRefused = 3, kNbNotHooked = 4 };
const size_t kCampStrMax = 512;
const unsigned int kCampSeedMax = 4096;
/* THE ENGINE'S OWN RATE. The spawner is WORLD-WIDE (0x8F6440 walks every region of its table, loaded or not - decomp_8f6440) and
   builds at most ONE camp per pass (it returns after a build), one pass every 2 s (0x8F8370's timer, decomp_8f8370). So a single
   player's world gains at most one camp per 2 s. With several games each proposing on its own pass, the world server takes at most
   one camp per kCampPeriodUs whatever the number of games - the single-player rate - and refuses the rest (kCampTooSoon); a refused
   game's spawner draws again on a later pass. */
const long long kCampPeriodUs = 2000000;   /* the most SEED records one game may send before its SEED_END */

struct Camp
{
    std::string key, kind, region, fac;
    float x, y, z;
    unsigned int regionCap;
    int slot;
    Camp() : x(0), y(0), z(0), regionCap(0), slot(-1) {}
};
struct CampMsg
{
    unsigned int op, reason, count, flag;
    Camp rec;
    std::string key;
    CampMsg() : op(0), reason(0), count(0), flag(0) {}
};

inline unsigned int FloatBits(float f) { unsigned int u = 0; std::memcpy(&u, &f, 4); return u; }
inline float BitsFloat(unsigned int u) { float f = 0; std::memcpy(&f, &u, 4); return f; }

/* THE CAMP KEY: the kind + the spot rounded to whole units - the same text on every game for the same camp, because every game builds
   the camp at the record's exact spot. It replaces "a camp of that kind" (the kind's name), which matched any camp of the kind. */
inline std::string CampKey(const std::string& kind, float x, float z)
{
    char b[48];
    _snprintf(b, sizeof(b) - 1, "@%d,%d", (int)std::floor(x + 0.5f), (int)std::floor(z + 0.5f)); b[sizeof(b) - 1] = 0;
    return kind + b;
}

inline bool CampCleanStr(const std::string& s)
{
    if (s.size() > kCampStrMax) return false;
    for (size_t i = 0; i < s.size(); ++i) if (s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == 0) return false;
    return true;
}
/* A record any game may hold: a kind, a region, a finite spot that is not the engine's "no spot" (0,0,0), a limit, and the key that
   kind and spot make. */
inline bool CampValid(const Camp& c)
{
    if (c.kind.empty() || c.region.empty() || !CampCleanStr(c.kind) || !CampCleanStr(c.region) || !CampCleanStr(c.fac)) return false;
    const float v[3] = { c.x, c.y, c.z };
    for (int i = 0; i < 3; ++i) if (!(v[i] > -1.0e7f && v[i] < 1.0e7f)) return false;
    if (c.x == 0.0f && c.y == 0.0f && c.z == 0.0f) return false;
    if (c.regionCap == 0) return false;
    return c.key == CampKey(c.kind, c.x, c.z);
}

inline void CampPut32(std::vector<char>* b, unsigned int v) { for (int i = 0; i < 4; ++i) b->push_back((char)((v >> (8 * i)) & 0xFFu)); }
inline unsigned int CampGet32(const char* p)
{
    return (unsigned int)(unsigned char)p[0] | ((unsigned int)(unsigned char)p[1] << 8) | ((unsigned int)(unsigned char)p[2] << 16) | ((unsigned int)(unsigned char)p[3] << 24);
}
inline void CampPutStr(std::vector<char>* b, const std::string& s) { CampPut32(b, (unsigned int)s.size()); b->insert(b->end(), s.begin(), s.end()); }
inline bool CampGetStr(const char* p, size_t n, size_t* at, std::string* out)
{
    if (n < *at + 4) return false;
    const unsigned int len = CampGet32(p + *at);
    if (len > kCampStrMax || n < *at + 4 + len) return false;
    out->assign(p + *at + 4, len);
    *at += 4 + len;
    return CampCleanStr(*out);
}
inline void CampPutRec(std::vector<char>* b, const Camp& c)
{
    CampPutStr(b, c.key); CampPutStr(b, c.kind); CampPutStr(b, c.region); CampPutStr(b, c.fac);
    CampPut32(b, FloatBits(c.x)); CampPut32(b, FloatBits(c.y)); CampPut32(b, FloatBits(c.z)); CampPut32(b, c.regionCap); CampPut32(b, (unsigned int)c.slot);
}
inline bool CampGetRec(const char* p, size_t n, size_t* at, Camp* c)
{
    Camp t;
    if (!CampGetStr(p, n, at, &t.key) || !CampGetStr(p, n, at, &t.kind) || !CampGetStr(p, n, at, &t.region) || !CampGetStr(p, n, at, &t.fac)) return false;
    if (n < *at + 20) return false;
    t.x = BitsFloat(CampGet32(p + *at)); t.y = BitsFloat(CampGet32(p + *at + 4)); t.z = BitsFloat(CampGet32(p + *at + 8));
    t.regionCap = CampGet32(p + *at + 12); t.slot = (int)CampGet32(p + *at + 16);
    *at += 20;
    *c = t;
    return true;
}

/* An encode that refuses appends nothing. Which fields an op carries is the table in the header comment. */
inline bool CampEncode(std::vector<char>* b, const CampMsg& m)
{
    std::vector<char> o;
    o.push_back((char)m.op); o.push_back(0); o.push_back(0); o.push_back(0);
    switch (m.op)
    {
    case kOpPropose: case kOpStored: case kOpSeed:
        if (!CampValid(m.rec)) return false;
        CampPutRec(&o, m.rec); break;
    case kOpRefused:
        if (m.rec.key.empty() || !CampCleanStr(m.rec.key) || !CampCleanStr(m.rec.kind) || !CampCleanStr(m.rec.region) || !CampCleanStr(m.rec.fac)) return false;
        CampPut32(&o, m.reason); CampPutRec(&o, m.rec); break;
    case kOpBegin: CampPut32(&o, m.count); CampPut32(&o, m.flag); break;
    case kOpEnd: case kOpSeedEnd: CampPut32(&o, m.count); break;
    case kOpSeeded: CampPut32(&o, m.flag); CampPut32(&o, m.count); break;
    case kOpGone: case kOpRemoved:
        if (m.key.empty() || !CampCleanStr(m.key)) return false;
        CampPutStr(&o, m.key); break;
    case kOpNotBuilt:
        if (m.key.empty() || !CampCleanStr(m.key)) return false;
        CampPut32(&o, m.reason); CampPutStr(&o, m.key); break;
    default: return false;
    }
    b->insert(b->end(), o.begin(), o.end());
    return true;
}
inline bool CampDecode(const char* p, size_t n, CampMsg* m)
{
    if (p == 0 || n < 4 || p[1] != 0 || p[2] != 0 || p[3] != 0) return false;
    CampMsg t; t.op = (unsigned int)(unsigned char)p[0];
    size_t at = 4;
    switch (t.op)
    {
    case kOpPropose: case kOpStored: case kOpSeed:
        if (!CampGetRec(p, n, &at, &t.rec) || !CampValid(t.rec)) return false; break;
    case kOpRefused:
        if (n < at + 4) return false;
        t.reason = CampGet32(p + at); at += 4;
        if (!CampGetRec(p, n, &at, &t.rec) || t.rec.key.empty()) return false; break;
    case kOpBegin:
        if (n < at + 8) return false;
        t.count = CampGet32(p + at); t.flag = CampGet32(p + at + 4); at += 8;
        if (t.flag > (unsigned int)kListUnreadable) return false; break;
    case kOpEnd: case kOpSeedEnd:
        if (n < at + 4) return false;
        t.count = CampGet32(p + at); at += 4; break;
    case kOpSeeded:
        if (n < at + 8) return false;
        t.flag = CampGet32(p + at); t.count = CampGet32(p + at + 4); at += 8;
        if (t.flag > 1) return false; break;
    case kOpGone: case kOpRemoved:
        if (!CampGetStr(p, n, &at, &t.key) || t.key.empty()) return false; break;
    case kOpNotBuilt:
        if (n < at + 4) return false;
        t.reason = CampGet32(p + at); at += 4;
        if (!CampGetStr(p, n, &at, &t.key) || t.key.empty()) return false; break;
    default: return false;
    }
    if (at != n) return false;
    *m = t;
    return true;
}

/* How many stored camps a region already holds. */
inline unsigned int CampRegionCount(const std::map<std::string, Camp>& list, const std::string& region)
{
    unsigned int n = 0;
    for (std::map<std::string, Camp>::const_iterator it = list.begin(); it != list.end(); ++it) if (it->second.region == region) ++n;
    return n;
}
/* The region's limit a PROPOSE or a SEED record is judged by: the larger of its own reading and every stored camp's of that region. */
inline unsigned int CampRegionCapOf(const std::map<std::string, Camp>& list, const std::string& region, unsigned int proposed)
{
    unsigned int cap = proposed;
    for (std::map<std::string, Camp>::const_iterator it = list.begin(); it != list.end(); ++it) if (it->second.region == region && it->second.regionCap > cap) cap = it->second.regionCap;
    return cap;
}
/* THE WORLD SERVER'S ANSWER TO A PROPOSE (live reads at the moment it is taken): an unreadable camps.txt refuses everything; a record
   that is not valid is refused; a key already stored is a duplicate (the stored camp is sent back); a region whose stored camps
   already reach its limit is full (the limit is the larger of the stored camps' and the proposal's own reading of it - the region's
   world data is the same on every game, so both are the same number); a world with no list yet takes no proposal (its SEED comes
   first - owner 1376 - else one proposal would become the whole list and every joiner's camps would be removed); else it is taken. */
inline int CampDecide(int listUnreadable, int hasList, int valid, int haveKey, unsigned int regionCount, unsigned int regionCap)
{
    if (listUnreadable) return kCampUnreadable;
    if (!hasList) return kCampNoList;
    if (!valid) return kCampBad;
    if (haveKey) return kCampDuplicate;
    if (regionCount >= regionCap) return kCampRegionFull;
    return kCampAccept;
}
/* The rate rule, judged after CampDecide accepted: the last camp taken (by a PROPOSE - a SEED is the save's camps, not new ones) was
   at least kCampPeriodUs ago on the world server's monotonic clock, or none was taken since it started. */
inline int CampRateOk(int haveLast, long long lastUs, long long nowUs) { return (!haveLast || nowUs - lastUs >= kCampPeriodUs) ? 1 : 0; }
inline int CampDecideIn(const std::map<std::string, Camp>& list, int listUnreadable, int hasList, const Camp& c)
{
    const int valid = CampValid(c) ? 1 : 0;
    return CampDecide(listUnreadable, hasList, valid, list.find(c.key) != list.end() ? 1 : 0, valid ? CampRegionCount(list, c.region) : 0u, CampRegionCapOf(list, c.region, c.regionCap));
}
/* Who gets a stored camp or a removal: its proposer / reporter always (that is its answer), every other connected game whose record feed
   is on (a game whose feed is off gets the whole list with its feed's first page or before its first index page). */
inline int CampForwardTo(int connected, int isSender, int feedOn) { return (connected && (isSender || feedOn)) ? 1 : 0; }

/* OWNER 1376 - THE SEED. Taken only while the world has no list (camps.txt absent and readable), from a game whose SEED records since
   its last SEED_END number exactly the count it closes with, and not past kCampSeedMax. A refused seed changes nothing: the seeder is
   sent the list as it stands, like any joiner. The first whole seed wins; a second game's seed is refused because a list now exists. */
inline int CampSeedTake(int hasList, int listUnreadable, unsigned int received, unsigned int declared, int overflowed)
{
    return (!hasList && !listUnreadable && !overflowed && received == declared && declared <= kCampSeedMax) ? 1 : 0;
}
/* The seed's records as the list: invalid ones are dropped, a repeated key keeps its first record, and a region keeps no more than its
   own limit (the save already obeyed it - this only guards the list against a malformed seed). Each dropped record goes to *dropped
   with the reason CampDecideIn gave (the world server logs them). */
struct CampDropped { Camp rec; int why; };
inline std::map<std::string, Camp> CampSeedList(const std::vector<Camp>& seed, int slot, std::vector<CampDropped>* dropped = 0)
{
    std::map<std::string, Camp> out;
    if (dropped != 0) dropped->clear();
    for (size_t i = 0; i < seed.size(); ++i)
    {
        Camp c = seed[i]; c.slot = slot;
        const int v = CampDecideIn(out, 0, 1, c);
        if (v == kCampAccept) out[c.key] = c;
        else if (dropped != 0) { CampDropped d; d.rec = c; d.why = v; dropped->push_back(d); }
    }
    return out;
}

/* OWNER 1376 - A GAME'S CAMPS MADE TO MATCH THE LIST, once the whole list is here: a camp of the list this game does not have is built,
   a camp this game has that the list does not hold is removed. Keys only - the camp key names the same camp on every game. */
inline void CampListDiff(const std::set<std::string>& listKeys, const std::set<std::string>& liveKeys, std::vector<std::string>* toBuild, std::vector<std::string>* toRemove)
{
    toBuild->clear(); toRemove->clear();
    for (std::set<std::string>::const_iterator it = listKeys.begin(); it != listKeys.end(); ++it) if (liveKeys.find(*it) == liveKeys.end()) toBuild->push_back(*it);
    for (std::set<std::string>::const_iterator it = liveKeys.begin(); it != liveKeys.end(); ++it) if (listKeys.find(*it) == listKeys.end()) toRemove->push_back(*it);
}
/* A GONE (the camp left the world): taken only from the game the area map names as the holder of the camp's area (holderSlot = that
   slot, -1 = nobody holds it - then nobody may remove it, and it waits for its area's next holder); an unknown key is already gone. A
   refused GONE is answered with the stored record (the camp stands), an unknown one with REMOVED to the reporter alone. */
enum { kGoneRemove = 0, kGoneUnknown = 1, kGoneNotHolder = 2, kGoneUnreadable = 3 };
inline int CampGoneDecide(int listUnreadable, int have, int holderSlot, int reporterSlot)
{
    if (listUnreadable) return kGoneUnreadable;
    if (!have) return kGoneUnknown;
    return (holderSlot >= 0 && holderSlot == reporterSlot) ? kGoneRemove : kGoneNotHolder;
}
/* A GAME'S PROPOSAL GATE: a game proposes only once the whole list is here, while NONE of its proposals waits for an answer (the engine
   builds one camp per pass, so a game has one proposal out at a time - its spawner's walk stops at the first one), and while the world
   server has not answered "region full" for the region since the region's last removal. The answer (STORED of the key, REFUSED) or the link
   going down ends the wait; the engine's spawner asks again on its own 2 s pass. */
inline int CampMayPropose(int listComplete, int waiting, int regionFullMark) { return (listComplete && !waiting && !regionFullMark) ? 1 : 0; }

/* camps.txt */
inline const char* CampFileHeader() { return "camps\tv1"; }
inline std::string CampFileLine(const Camp& c)
{
    char n[96];
    _snprintf(n, sizeof(n) - 1, "\t%08X\t%08X\t%08X\t%u\t%d", FloatBits(c.x), FloatBits(c.y), FloatBits(c.z), c.regionCap, c.slot); n[sizeof(n) - 1] = 0;
    return "c1\t" + c.key + "\t" + c.kind + "\t" + c.region + "\t" + c.fac + n;
}
inline bool CampHexBits(const std::string& s, unsigned int* out)
{
    if (s.size() != 8) return false;
    for (size_t i = 0; i < 8; ++i) { const char ch = s[i]; if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'))) return false; }
    *out = (unsigned int)strtoul(s.c_str(), 0, 16);
    return true;
}
inline bool CampParseLine(const std::string& lineIn, Camp* out)
{
    std::string line = lineIn;
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    std::vector<std::string> f; size_t at = 0;
    for (;;) { const size_t t = line.find('\t', at); if (t == std::string::npos) { f.push_back(line.substr(at)); break; } f.push_back(line.substr(at, t - at)); at = t + 1; }
    if (f.size() != 10 || f[0] != "c1" || f[8].empty() || f[9].empty()) return false;
    for (size_t i = 0; i < f[8].size(); ++i) if (f[8][i] < '0' || f[8][i] > '9') return false;
    Camp c; c.key = f[1]; c.kind = f[2]; c.region = f[3]; c.fac = f[4];
    unsigned int xb = 0, yb = 0, zb = 0;
    if (!CampHexBits(f[5], &xb) || !CampHexBits(f[6], &yb) || !CampHexBits(f[7], &zb)) return false;
    c.x = BitsFloat(xb); c.y = BitsFloat(yb); c.z = BitsFloat(zb);
    c.regionCap = (unsigned int)strtoul(f[8].c_str(), 0, 10); c.slot = atoi(f[9].c_str());
    if (!CampValid(c)) return false;
    *out = c;
    return true;
}
/* The file's text as a list: false when the first line is not the header (then the file is treated as unreadable - never written over). */
inline bool CampParseFile(const std::string& text, std::map<std::string, Camp>* out, long long* bad)
{
    out->clear(); *bad = 0;
    size_t at = 0; bool first = true;
    while (at <= text.size())
    {
        size_t e = text.find('\n', at); if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        at = e + 1;
        if (first) { if (line != CampFileHeader()) return false; first = false; continue; }
        if (line.empty()) { if (e == text.size()) break; continue; }
        Camp c; if (CampParseLine(line, &c)) (*out)[c.key] = c; else ++*bad;
        if (e == text.size()) break;
    }
    return !first;
}
inline std::string CampFileText(const std::map<std::string, Camp>& list)
{
    std::string body = std::string(CampFileHeader()) + "\n";
    for (std::map<std::string, Camp>::const_iterator it = list.begin(); it != list.end(); ++it) body += CampFileLine(it->second) + "\n";
    return body;
}

}   // namespace campl

#endif   // COOP_COMMON_CAMPWIRE_H
