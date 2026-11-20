#pragma once
/* src/common/towntime.h - ONE TOWN'S 5-DAY REBUILD CLOCK AS ONE WORLD-SERVER RECORD, and the pure rules the clock follows in a shared
 * world (T-4, owner rule 1560): one clock per town, kept on the world server, read and advanced by whichever game holds the town's
 * area - as decision 1085's shop deadline (shoprec.h).
 *
 * One record per town, id `towntime.<sx>.<sy>.<sid>`: sx / sy the town's area square (0..63, the plugin's SectorOf of the town's
 * position), sid the town's string id (Town stringID, the bar table's key). No session id is in it, so it names the same town in
 * every session of the world. The record's bytes (the `.platoon` file on the world server, message 28 RECORD on the wire),
 * little-endian:
 *
 *   u32 marker 'TTM1' | u32 format (kTtFormat) | u32 generation | i32 writer slot | i32 sx | i32 sy | u32 length + sid (1..kTtSidMax
 *   bytes, no control byte) | f64 started (the in-game hour this town's clock was first written) | f64 due (the in-game hour the next
 *   period walk is due) | f64 lastRebuilt (the in-game hour of the last walk that rebuilt or marked at least one group; kTtNever =
 *   none) | u32 rebuilds (groups rebuilt or marked under this clock, all holders together) | u32 n (0..kTtOwedMax) |
 *   n x { u32 length + a group's world id (1..kTtSidMax bytes, no control byte, each once) } - the OWED groups: picked by a period
 *   walk but held back because a player saw them; the holder rebuilds each at a later check-up once nobody sees it |
 *   u32 m (0..kTtOwedMax) | m x { the same } - the MARKED groups: asleep picks given the engine's rebuild mark (Platoon+0x118),
 *   rebuilt at their own wake. The mark lives only in the marking game's memory (the save does not keep it - Platoon::
 *   _NV_serialiseEverything 0x372DB0 writes no +0x118), so the list carries it: a holder re-applies the mark to a listed group still
 *   asleep and still qualifying; a group leaves the list when it wakes or fails a final test.
 *   Every hour is on the engine's in-game clock, which every game keeps in step with the world server's CLOCK. Nothing follows.
 *
 * A record that is cut short, carries an unknown marker or format, an area square out of 0..63, a bad sid or owed id, a repeated
 * owed id, an hour that is not a finite number in [0, kTtHoursMax] (lastRebuilt may also be kTtNever), or bytes after the last
 * field DECODES TO REFUSED.
 *
 * THE WORLD SERVER'S RULE (TownTimeJudgeWrite, shoprec.h ShopJudgeWrite's rule): only the game the area map names as the holder of
 * the town's area writes, and the record names that game as its writer; a write carries a generation above the stored one (the very
 * same bytes at the stored generation are a harmless repeat, nothing is written); the record's own area square and sid make its id;
 * a town clock record is never removed. A refused write is answered with boxrec.h's BOX_REFUSE message and reasons, and a write
 * refused as older or at the stored generation is also answered with the stored record.
 *
 * THE GAME'S RULE (TtClockStep, TtWalk*): the holder judges the record only once the world server's records of the town's area are in.
 *   - no record known: the first one is written, due = now + kTtPeriodHours;
 *   - due more than one period ahead (the world's clock went back): written again, due = now + kTtPeriodHours;
 *   - now at or past due: the PERIOD WALK. It always FINISHES: after its last queued group is committed it writes due = now +
 *     kTtPeriodHours. A group a player sees (at the walk or at its commitment) is put on the OWED list instead of rebuilt; a group
 *     rebuilt or skipped for good leaves it. A past due on a new holder is one walk, not one per missed period;
 *   - an asleep pick: the engine's mark, its id kept on the MARKED list until it wakes (re-applied by any later holder);
 *   - before due with groups owed or marked: the OWED WALK at each check-up - only the owed groups, each rebuilt once nobody sees it (sight read
 *     live at its commitment) and then dropped from the list; due is not moved. A group is rebuilt at most once a period: one
 *     rebuilt is never owed, and a new period's owed groups are merged with the list, never doubled.
 *   - this game's write is kept apart from the last record the world server accepted (TtKeep); a refusal goes back to the accepted
 *     one (and the stored record the server sends with an older / same-generation refusal is taken when it arrives).
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */

#include <cstdio>
#include <cstdlib>   /* strtod: the TEST-ONLY period lever's number */
#include <cstring>
#include <string>
#include <vector>
#include "boxrec.h"   /* the byte helpers and the refusal reasons the town clock record shares */

namespace towntime {

const unsigned int kProtocol = 170;            /* the world-server protocol that carries town clock records */


const unsigned int kTtMarker = 0x314D5454u;    /* 'T','T','M','1' little-endian */
const unsigned int kTtFormat = 3u;             /* 2: the owed list; 3: the marked list (neither 1 nor 2 left an unmerged branch) */
const unsigned int kTtSidMax = 96u;
const unsigned int kTtOwedMax = 64u;
const unsigned int kTtMaxBytes = 8192u;
const double kTtHoursMax = 1e9;
const double kTtNever = -1.0;                  /* lastRebuilt: no group rebuilt or marked under this clock */
const double kTtPeriodHours = 120.0;           /* 5 in-game days (coopt2::kRebuildPeriodHours) */
const double kTtClockBackSlack = 1.0;          /* a due more than one period + this ahead of now = the world's clock went back */
const double kTtPeriodMinHours = 1.0;          /* T-4 TEST-ONLY `towntime period <hours>`: the period's range is [1, kTtPeriodHours] */

/* decode results: anything but kTtOk is a refused record */
const int kTtOk = 0, kTtShort = 1, kTtBadMarker = 2, kTtBadFormat = 3, kTtBadRecord = 4, kTtTrailing = 5, kTtTooBig = 6;

struct TownTimeRec
{
    unsigned int generation;
    int writerSlot;
    int sx, sy;
    std::string sid;
    double started, due, lastRebuilt;
    unsigned int rebuilds;
    std::vector<std::string> owed;   /* group world ids owed a rebuild, in the order they were owed */
    std::vector<std::string> marked; /* asleep group world ids given the engine's rebuild mark, not yet woken */
    TownTimeRec() : generation(0), writerSlot(0), sx(0), sy(0), started(0.0), due(0.0), lastRebuilt(kTtNever), rebuilds(0) {}
};

/* ---- the id ---- */
inline bool TtSidOk(const std::string& s)
{
    if (s.empty() || s.size() > kTtSidMax) return false;
    for (size_t i = 0; i < s.size(); ++i) if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] == 0x7F) return false;
    return true;
}
inline bool TtSquareOk(int v) { return v >= 0 && v <= 63; }
/* "" when the square or the sid is not valid */
inline std::string MakeTownTimeId(int sx, int sy, const std::string& sid)
{
    if (!TtSquareOk(sx) || !TtSquareOk(sy) || !TtSidOk(sid)) return std::string();
    char b[32];
    std::sprintf(b, "towntime.%d.%d.", sx, sy);
    return std::string(b) + sid;
}
/* any spelling of the prefix counts (file names ignore case); only the spelling MakeTownTimeId makes parses */
inline bool IsTownTimeId(const std::string& id)
{
    static const char kPrefix[] = "towntime.";
    if (id.size() < 9) return false;
    for (size_t i = 0; i < 9; ++i) { char c = id[i]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c != kPrefix[i]) return false; }
    return true;
}
/* 1 = exactly a spelling MakeTownTimeId makes (its square and sid into the pointers that are not 0); 0 = not a town clock id */
inline int TownTimeIdParse(const std::string& id, int* sx, int* sy, std::string* sid)
{
    if (id.size() < 9 || id.compare(0, 9, "towntime.") != 0) return 0;
    size_t at = 9;
    int v[2] = { 0, 0 };
    for (int k = 0; k < 2; ++k)
    {
        const size_t s = at;
        int n = 0;
        while (at < id.size() && id[at] >= '0' && id[at] <= '9' && at - s < 2) { n = n * 10 + (id[at] - '0'); ++at; }
        if (at == s || at >= id.size() || id[at] != '.') return 0;
        v[k] = n;
        ++at;
    }
    const std::string rest = id.substr(at);
    if (MakeTownTimeId(v[0], v[1], rest) != id) return 0;   /* no leading zero, square in range, sid valid */
    if (sx) *sx = v[0];
    if (sy) *sy = v[1];
    if (sid) *sid = rest;
    return 1;
}

/* ---- the owed list ---- */
inline int TtOwedAt(const std::vector<std::string>& l, const std::string& id)
{
    for (size_t i = 0; i < l.size(); ++i) if (l[i] == id) return (int)i;
    return -1;
}
inline bool TtOwedOk(const std::vector<std::string>& l)
{
    if (l.size() > kTtOwedMax) return false;
    for (size_t i = 0; i < l.size(); ++i)
    {
        if (!TtSidOk(l[i])) return false;
        for (size_t j = 0; j < i; ++j) if (l[j] == l[i]) return false;
    }
    return true;
}
/* (before + add) - drop: before's order first, then add's; each id once; ids that are not valid are left out, and ids past kTtOwedMax
   are left out and counted in *capped (when not 0) - the caller says so (fold 3: never silent) */
inline std::vector<std::string> TtOwedMerge(const std::vector<std::string>& before, const std::vector<std::string>& add, const std::vector<std::string>& drop, int* capped = 0)
{
    std::vector<std::string> out;
    if (capped != 0) *capped = 0;
    for (int k = 0; k < 2; ++k)
    {
        const std::vector<std::string>& src = (k == 0) ? before : add;
        for (size_t i = 0; i < src.size(); ++i)
        {
            if (!TtSidOk(src[i]) || TtOwedAt(out, src[i]) >= 0 || TtOwedAt(drop, src[i]) >= 0) continue;
            if (out.size() >= kTtOwedMax) { if (capped != 0) ++*capped; continue; }
            out.push_back(src[i]);
        }
    }
    return out;
}

/* ---- validity and bytes ---- */
inline bool TtHoursOk(double h) { return h == h && h >= 0.0 && h <= kTtHoursMax; }
inline bool TownTimeRecValid(const TownTimeRec& r)
{
    if (!coopbrec::BrSlotOk(r.writerSlot) || MakeTownTimeId(r.sx, r.sy, r.sid).empty()) return false;
    if (!TtHoursOk(r.started) || !TtHoursOk(r.due)) return false;
    if (!(r.lastRebuilt == kTtNever || TtHoursOk(r.lastRebuilt))) return false;
    return TtOwedOk(r.owed) && TtOwedOk(r.marked);
}
inline void TtPutF64(std::vector<char>* b, double d) { unsigned long long u = 0; std::memcpy(&u, &d, 8); coopbrec::BrPutU64(b, u); }
inline bool TtGetF64(const char* p, size_t size, size_t* off, double* d)
{
    unsigned long long u = 0;
    if (!coopbrec::BrGet8(p, size, off, &u)) return false;
    std::memcpy(d, &u, 8);
    return true;
}
inline bool TtGetStr(const char* p, size_t size, size_t* off, std::string* s, int* bad)
{
    unsigned int len = 0;
    if (!coopbrec::BrGet4(p, size, off, &len)) return false;
    if (len == 0 || len > kTtSidMax) { *bad = 1; return false; }
    if (size - *off < (size_t)len) return false;
    s->assign(p + *off, len);
    *off += len;
    return true;
}
/* false = nothing written (*out emptied): the record breaks TownTimeRecValid */
inline bool EncodeTownTimeRec(std::vector<char>* out, const TownTimeRec& r)
{
    if (out == 0) return false;
    out->clear();
    if (!TownTimeRecValid(r)) return false;
    coopbrec::BrPutU32(out, kTtMarker);
    coopbrec::BrPutU32(out, kTtFormat);
    coopbrec::BrPutU32(out, r.generation);
    coopbrec::BrPutU32(out, (unsigned int)r.writerSlot);
    coopbrec::BrPutU32(out, (unsigned int)r.sx);
    coopbrec::BrPutU32(out, (unsigned int)r.sy);
    coopbrec::BrPutStr(out, r.sid);
    TtPutF64(out, r.started);
    TtPutF64(out, r.due);
    TtPutF64(out, r.lastRebuilt);
    coopbrec::BrPutU32(out, r.rebuilds);
    coopbrec::BrPutU32(out, (unsigned int)r.owed.size());
    for (size_t i = 0; i < r.owed.size(); ++i) coopbrec::BrPutStr(out, r.owed[i]);
    coopbrec::BrPutU32(out, (unsigned int)r.marked.size());
    for (size_t i = 0; i < r.marked.size(); ++i) coopbrec::BrPutStr(out, r.marked[i]);
    return true;
}
/* kTtOk: *out holds the record; anything else: refused, *out untouched */
inline int DecodeTownTimeRec(const char* p, size_t size, TownTimeRec* out)
{
    if (p == 0 || size < 8u) return kTtShort;
    if (size > (size_t)kTtMaxBytes) return kTtTooBig;
    size_t off = 0;
    unsigned int marker = 0, format = 0;
    TownTimeRec r;
    coopbrec::BrGet4(p, size, &off, &marker);
    if (marker != kTtMarker) return kTtBadMarker;
    coopbrec::BrGet4(p, size, &off, &format);
    if (format != kTtFormat) return kTtBadFormat;
    if (!coopbrec::BrGet4(p, size, &off, &r.generation) || !coopbrec::BrGet4(p, size, &off, &r.writerSlot)
        || !coopbrec::BrGet4(p, size, &off, &r.sx) || !coopbrec::BrGet4(p, size, &off, &r.sy))
        return kTtShort;
    int bad = 0;
    if (!TtGetStr(p, size, &off, &r.sid, &bad)) return bad ? kTtBadRecord : kTtShort;
    if (!TtGetF64(p, size, &off, &r.started) || !TtGetF64(p, size, &off, &r.due) || !TtGetF64(p, size, &off, &r.lastRebuilt)
        || !coopbrec::BrGet4(p, size, &off, &r.rebuilds))
        return kTtShort;
    unsigned int n = 0;
    if (!coopbrec::BrGet4(p, size, &off, &n)) return kTtShort;
    if (n > kTtOwedMax) return kTtBadRecord;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::string s;
        if (!TtGetStr(p, size, &off, &s, &bad)) return bad ? kTtBadRecord : kTtShort;
        r.owed.push_back(s);
    }
    if (!coopbrec::BrGet4(p, size, &off, &n)) return kTtShort;
    if (n > kTtOwedMax) return kTtBadRecord;
    for (unsigned int i = 0; i < n; ++i)
    {
        std::string s;
        if (!TtGetStr(p, size, &off, &s, &bad)) return bad ? kTtBadRecord : kTtShort;
        r.marked.push_back(s);
    }
    if (off != size) return kTtTrailing;
    if (!TownTimeRecValid(r)) return kTtBadRecord;
    if (out) *out = r;
    return kTtOk;
}
inline const char* TownTimeDecodeWhy(int why)
{
    return why == kTtOk ? "ok" : why == kTtShort ? "cut short" : why == kTtBadMarker ? "not a town clock record (wrong marker)"
         : why == kTtBadFormat ? "an unknown format" : why == kTtBadRecord ? "a field out of range" : why == kTtTrailing ? "bytes after the last field"
         : why == kTtTooBig ? "larger than the byte cap" : "malformed";
}

/* ---- the world server ---- */
/* One write of town clock record `id` from slot senderSlot; holderSlot: the slot the area map names as the holder of the id's area
   (-1 nobody); exists / storedGen: a stored record and its generation; sameBytes: the bytes equal the stored file. coopbrec::kBrAccept,
   kBrRepeat (nothing to write) or a refusal reason. *out (when not 0) gets the decoded record when it read. */
inline int TownTimeJudgeWrite(const std::string& id, const char* p, size_t size, int senderSlot, int holderSlot, int exists,
                              unsigned int storedGen, int sameBytes, TownTimeRec* out)
{
    if (TownTimeIdParse(id, 0, 0, 0) == 0) return coopbrec::kBrRefuseBadId;
    TownTimeRec r;
    if (DecodeTownTimeRec(p, size, &r) != kTtOk) return coopbrec::kBrRefuseUnreadable;
    if (out != 0) *out = r;
    if (MakeTownTimeId(r.sx, r.sy, r.sid) != id) return coopbrec::kBrRefuseWrongBox;
    if (senderSlot < 0 || r.writerSlot != senderSlot) return coopbrec::kBrRefuseWriter;
    if (holderSlot < 0 || senderSlot != holderSlot) return coopbrec::kBrRefuseNotWriter;
    if (exists != 0)
    {
        if (r.generation < storedGen) return coopbrec::kBrRefuseOlder;
        if (r.generation == storedGen) return (sameBytes != 0) ? coopbrec::kBrRepeat : coopbrec::kBrRefuseSameGen;
    }
    return coopbrec::kBrAccept;
}
/* 1 = a refusal the world server answers with its stored record too (the writer's view is behind the stored one) */
inline int TtRefusalSendsStored(int why) { return (why == coopbrec::kBrRefuseOlder || why == coopbrec::kBrRefuseSameGen) ? 1 : 0; }

/* ---- the game: what it knows (B - the accepted record apart from the one in flight) ---- */
struct TtKeep
{
    int haveAcc; TownTimeRec acc;    /* the last record the world server accepted or sent */
    int have; TownTimeRec r;         /* the working record: acc, or this game's write in flight */
    int inflight; long long stamp;   /* a write in flight and its stamp (its echo names it) */
    unsigned int floorGen;           /* the stored generation a refusal named */
    TtKeep() : haveAcc(0), have(0), inflight(0), stamp(0), floorGen(0) {}
};
/* the generation of this game's next write: one above the working one or the floor a refusal named, whichever is higher */
inline unsigned int TtNextGen(int haveKept, unsigned int keptGen, unsigned int floorGen)
{
    const unsigned int base = (haveKept != 0 && keptGen > floorGen) ? keptGen : floorGen;
    return base + 1u;
}
inline void TtOnWrite(TtKeep* k, const TownTimeRec& sent, long long stamp) { k->have = 1; k->r = sent; k->inflight = 1; k->stamp = stamp; }
/* the world server stored this game's write sent with `stamp` */
inline void TtOnEcho(TtKeep* k, long long stamp)
{
    if (k->inflight == 0 || stamp != k->stamp) return;
    k->haveAcc = 1; k->acc = k->r; k->inflight = 0;
}
/* the world server refused this game's write: back to the accepted record (none -> nothing known), the next write above storedGen */
inline void TtOnRefuse(TtKeep* k, unsigned int storedGen)
{
    if (storedGen > k->floorGen) k->floorGen = storedGen;
    if (k->inflight == 0) return;
    k->inflight = 0;
    k->have = k->haveAcc;
    if (k->haveAcc != 0) k->r = k->acc;
}
/* fold 3: a new link (its WELCOME): a write sent before the link dropped may never be answered - back to the accepted record; the
   clock is judged again once the new link's records of the area are in. 1 = a write in flight was given up. */
inline int TtOnLinkNew(TtKeep* k)
{
    if (k->inflight == 0) return 0;
    k->inflight = 0;
    k->have = k->haveAcc;
    if (k->haveAcc != 0) k->r = k->acc;
    return 1;
}
/* a record from the world server (stored there): it becomes the accepted one when newer (or at the accepted generation from another
   writer), and the working one unless this game's own newer write is in flight. 1 = taken as the accepted record. */
inline int TtOnArrive(TtKeep* k, const TownTimeRec& r)
{
    if (k->haveAcc != 0 && (r.generation < k->acc.generation || (r.generation == k->acc.generation && r.writerSlot == k->acc.writerSlot))) return 0;
    k->haveAcc = 1; k->acc = r;
    if (k->inflight == 0 || r.generation >= k->r.generation) { k->inflight = 0; k->have = 1; k->r = r; }
    return 1;
}

/* What a holder's check-up does with the clock it knows (have: a record is known for the town). */
enum ClockStep { kTtNoClock = 0, kTtSeed, kTtClockBack, kTtWait, kTtDue, kTtOwed };
inline const char* ClockStepName(int s)
{
    return s == kTtSeed ? "seed" : s == kTtClockBack ? "clockBack" : s == kTtWait ? "wait" : s == kTtDue ? "due" : s == kTtOwed ? "owed" : "noClock";
}
/* now < 0 = no clock; forced (a TEST `townres now`) = due whatever the record says. periodHours: the period every NEW due is
   written with (kTtPeriodHours for players; the TEST-ONLY `towntime period` lever shortens it) - a due more than one period
   ahead of now is the clock-back case, so a record written under a longer period is rewritten once with the shorter one. */
inline int TtClockStep(int have, const TownTimeRec& r, double now, int forced, double periodHours = kTtPeriodHours)
{
    if (!(now == now) || now < 0.0) return kTtNoClock;
    if (forced != 0) return kTtDue;
    if (have == 0) return kTtSeed;
    if (r.due > now + periodHours + kTtClockBackSlack) return kTtClockBack;
    if (now >= r.due) return kTtDue;
    return (r.owed.empty() && r.marked.empty()) ? kTtWait : kTtOwed;
}

/* ---- the game: one walk (a PERIOD walk at due, or an OWED walk of the owed groups only) ---- */
enum GroupEnd { kTtEndRebuilt = 0, kTtEndOwe, kTtEndFinal, kTtEndMarked, kTtEndRemarked, kTtEndWoke };   /* marked: an asleep pick given the mark (counted); remarked: a listed group still asleep (its mark kept or re-applied, not counted again); woke: a listed group awake - it leaves the marked list */
struct TtWalk
{
    int active, period, pending, asked;
    double at;                                  /* the in-game hour the walk began */
    std::vector<std::string> add, drop;         /* owed ids the walk adds / takes off */
    std::vector<std::string> madd, mdrop;       /* marked ids the walk adds / takes off */
    TtWalk() : active(0), period(0), pending(0), asked(0), at(0.0) {}
};
inline void TtWalkBegin(TtWalk* w, int period, double now) { w->active = 1; w->period = period; w->pending = 0; w->asked = 0; w->at = now; w->add.clear(); w->drop.clear(); w->madd.clear(); w->mdrop.clear(); }
inline void TtWalkQueued(TtWalk* w) { ++w->pending; }
inline void TtListOut(std::vector<std::string>* l, const std::string& id) { const int at = TtOwedAt(*l, id); if (at >= 0) l->erase(l->begin() + at); }
inline void TtListIn(std::vector<std::string>* l, const std::string& id) { if (TtOwedAt(*l, id) < 0) l->push_back(id); }
/* one group's end in the walk: rebuilt (or marked for the engine's own rebuild), owed (seen, or not committed), or a final skip;
   fromQueue 1 = a queued group's commitment (Towns2Act) */
inline void TtWalkGroupEnd(TtWalk* w, int end, const std::string& wid, int fromQueue)
{
    if (fromQueue != 0 && w->pending > 0) --w->pending;
    if (end == kTtEndRebuilt) { ++w->asked; TtListOut(&w->add, wid); TtListIn(&w->drop, wid); TtListOut(&w->madd, wid); TtListIn(&w->mdrop, wid); }
    else if (end == kTtEndOwe) { TtListOut(&w->drop, wid); TtListIn(&w->add, wid); }
    else if (end == kTtEndMarked) { ++w->asked; TtListOut(&w->add, wid); TtListIn(&w->drop, wid); TtListOut(&w->mdrop, wid); TtListIn(&w->madd, wid); }
    else if (end == kTtEndRemarked) { TtListOut(&w->mdrop, wid); }
    else if (end == kTtEndWoke) { TtListOut(&w->madd, wid); TtListIn(&w->mdrop, wid); }
    else { TtListOut(&w->add, wid); TtListIn(&w->drop, wid); TtListOut(&w->madd, wid); TtListIn(&w->mdrop, wid); }
}
/* the town's area left this game's hold before a queued group's commitment: the walk is dropped, nothing is written */
inline void TtWalkLostHold(TtWalk* w) { w->active = 0; w->pending = 0; w->asked = 0; w->add.clear(); w->drop.clear(); w->madd.clear(); w->mdrop.clear(); }
/* 1 = the walk has ended (every queued group committed or skipped) */
inline int TtWalkReady(const TtWalk& w) { return (w.active != 0 && w.pending == 0) ? 1 : 0; }
inline std::vector<std::string> TtWalkOwedAfter(const std::vector<std::string>& before, const TtWalk& w, int* capped = 0) { return TtOwedMerge(before, w.add, w.drop, capped); }
inline std::vector<std::string> TtWalkMarkedAfter(const std::vector<std::string>& before, const TtWalk& w, int* capped = 0) { return TtOwedMerge(before, w.madd, w.mdrop, capped); }
/* 1 = the ended walk writes the clock: a period walk always; an owed walk when it rebuilt or marked a group or changed a list */
inline int TtWalkWrites(const TtWalk& w, const std::vector<std::string>& owedBefore, const std::vector<std::string>& markedBefore)
{
    if (TtWalkReady(w) == 0) return 0;
    if (w.period != 0) return 1;
    return (w.asked > 0 || TtWalkOwedAfter(owedBefore, w) != owedBefore || TtWalkMarkedAfter(markedBefore, w) != markedBefore) ? 1 : 0;
}
/* The record a holder writes. period 1 (a period walk, the first record, the clock going back): due = now + kTtPeriodHours; period 0
   (an owed walk): due kept. started is the first record's hour; asked groups move lastRebuilt / rebuilds; owed is the list after. */
inline TownTimeRec TtNextRecord(int have, const TownTimeRec& kept, unsigned int floorGen, int writerSlot, int sx, int sy,
                                const std::string& sid, double now, int asked, int period, const std::vector<std::string>& owed,
                                const std::vector<std::string>& marked, double periodHours = kTtPeriodHours)
{
    TownTimeRec n;
    if (have != 0) n = kept;
    n.generation = TtNextGen(have, kept.generation, floorGen);
    n.writerSlot = writerSlot;
    n.sx = sx; n.sy = sy; n.sid = sid;
    if (have == 0) { n.started = now; n.lastRebuilt = kTtNever; n.rebuilds = 0; n.due = now + periodHours; }
    else if (period != 0) n.due = now + periodHours;
    if (asked > 0) { n.lastRebuilt = now; n.rebuilds += (unsigned int)asked; }
    n.owed = owed;
    n.marked = marked;
    return n;
}

/* T-4 TEST-ONLY lever `towntime period <hours> | show` (command file only): 1 = set *hours (a number, 1 to 120 inclusive, nothing
   after it), 2 = show, 0 = refused (*hours untouched). */
inline int TtPeriodLeverParse(const std::string& args, double* hours)
{
    std::vector<std::string> t;
    {
        std::string cur;
        for (size_t i = 0; i <= args.size(); ++i)
        {
            const char ch = (i < args.size()) ? args[i] : ' ';
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { if (!cur.empty()) { t.push_back(cur); cur.clear(); } }
            else cur += ch;
        }
    }
    if (t.size() != 2 || t[0] != "period") return 0;
    if (t[1] == "show") return 2;
    char* end = 0;
    const double h = strtod(t[1].c_str(), &end);
    if (end == t[1].c_str() || *end != 0 || !(h == h) || h < kTtPeriodMinHours || h > kTtPeriodHours) return 0;
    *hours = h;
    return 1;
}

}  // namespace towntime
