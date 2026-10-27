#pragma once
/* src/common/shoprec.h - ONE SHOP'S RESTOCK STATE AS ONE WORLD-SERVER RECORD, and the pure rules a shop's restock follows in a shared
 * world: the game holding the shop's area decides the restock, the keepers' money and the empty-shop repair, once, through this record.
 *
 * One record per shop BUILDING (every keeper squad whose home is that building shares it), id `shop.<sx>.<sy>.#<hash8>.<x10>.<z10>`:
 * the home building's position key turned into an id the way a box id is made (boxrec.h MakeBoxRecId: no height, the record name
 * always hashed) with the prefix `shop.`. No session id is in it, so it names the same building in every session of the world. The
 * record's bytes (the `.platoon` file on the world server, message 28 RECORD on the wire), little-endian:
 *
 *   u32 marker 'SHP1' | u32 format (kSrFormat) | u32 generation | i32 writer slot | u32 length + the building's full position key |
 *   u32 deadline low | u32 deadline high (an f64: the next restock time in in-game hours on the shared clock, the engine's own
 *   Platoon +0x1F8 unit) | u32 refills (the building's refills made) | u32 repair mark (the refill count when the last empty-shop repair
 *   was asked; kSrNoRepair = never) | u32 n (0..kSrMaxSquads) | n x { u32 length + the keeper squad's name (its template's string id,
 *   1..kSrNameMax bytes, no control byte, each name once) | i32 money (0..kSrMoneyMax) | u32 money changes | u32 refills applied (the
 *   building's refill count this squad's last refill answered) | u32 m (0..kSrMaxReqs) |
 *   m x { u32 asker (the player slot key the holder answers a trade request by) | u32 epoch (the asking process's) | u32 k
 *   (0..kSrReqKeep) | k x { u32 id (a request of that epoch whose trade the holder applied to this squad's money, each id once) |
 *   i32 n (the units it moved) | i32 move (what it moved on the keeper's pot: + a purchase's price, - a sale's) | u32 revoked (0 applied;
 *   1 the asker cancelled it and its goods and money were taken back - with n 0 and move 0 a MARKER: no holder applied it, none may
 *   later; 2 the asker cancelled it and the undo is owed; 3 the undo claimed by one game (the exact holder that read 2 stored) - it
 *   alone applies the undo once its claim is stored, then writes 1; 4 the cancel refused - the trade stands (a counter of the shop
 *   can never hold the undo in a record), nothing follows. With 3: u32 the claimer's slot. With 2 or 3, what the undo needs: u32 keeper uid | u32 dir (0 bought, 1 sold) | the item's base, company, material and colour ids (u32
 *   length + bytes each, at most kSrNameMax) | f32 quality | f32 charges | i32 function kind | i32 level | i32 unique) } } }.
 *   Nothing follows. At most kSrMaxBytes.
 *
 * A record that is cut short, carries an unknown marker or format, a key that makes no shop id, a deadline that is not a finite
 * number in [0, kSrHoursMax], a bad name, a repeated name, money out of range or bytes after the last squad DECODES TO REFUSED.
 *
 * THE WORLD SERVER'S RULE (ShopJudgeWrite): only the game the area map names as the holder of the shop's area writes, and the record
 * names that game as its writer; a write carries a generation above the stored one (the very same bytes at the stored generation are a
 * harmless repeat, nothing is written); a shop record is never removed. A refused write is answered with boxrec.h's BOX_REFUSE message
 * and reasons (the game routes a shop id to the shop table).
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */

#include <cstring>
#include <string>
#include <vector>
#include "boxrec.h"   /* the box id rule, the byte helpers and the refusal reasons the shop record shares */

namespace shoprec {

const unsigned int kProtocol = 121;            /* the world-server protocol that carries shop records */


const unsigned int kSrMarker = 0x31504853u;    /* 'S','H','P','1' little-endian */
const unsigned int kSrFormat = 2u;
const unsigned int kSrMaxSquads = 16u;
const unsigned int kSrMaxReqs = 8u;             /* askers remembered per squad (a full list drops the oldest asker) */
const unsigned int kSrReqKeep = 8u;             /* applied requests remembered per asker (a full list drops the oldest) */
const unsigned int kSrNameMax = 64u;
const unsigned int kSrMaxBytes = 32768u;
const int kSrMoneyMax = 100000000;
const unsigned int kSrNoRepair = 0xFFFFFFFFu;
const double kSrHoursMax = 1e9;
const double kSrRetryHours = 0.25;             /* a held or refused restock asks again after this many in-game hours */

/* decode results: anything but kSrOk is a refused record */
const int kSrOk = 0, kSrShort = 1, kSrBadMarker = 2, kSrBadFormat = 3, kSrBadRecord = 4, kSrTrailing = 5, kSrTooBig = 6;

/* WHAT AN OWED UNDO NEEDS (carried by an entry whose revoked is 2, so the exact holder reading it stored runs the undo with no cancel
   arriving): the keeper's uid, the direction (0 the player bought, 1 sold) and the item's description. has 0: not written (base empty). */
struct ShopUndoDesc
{
    int has;
    unsigned int keeperUid;
    int dir;
    std::string base, company, material, color;
    float quality, charges;
    int functionKind, level, unique;
    ShopUndoDesc() : has(0), keeperUid(0), dir(0), quality(0.0f), charges(0.0f), functionKind(0), level(0), unique(0) {}
};
struct ShopReqDone
{
    unsigned int id;
    int n, move;
    unsigned int revoked;
    ShopUndoDesc undo;   /* encoded only with revoked 2 or 3 */
    unsigned int claimer;   /* revoked 3: the slot of the game whose claim on the undo is stored (encoded only with revoked 3) */
    ShopReqDone() : id(0), n(0), move(0), revoked(0), claimer(0) {}
};
struct ShopReq
{
    unsigned int asker, epoch;
    std::vector<ShopReqDone> done;   /* the asker's last applied requests of this epoch, oldest first */
    ShopReq() : asker(0), epoch(0) {}
};
struct ShopSquad
{
    std::string name;
    int money;
    unsigned int moneyChanges, refillsApplied;
    std::vector<ShopReq> reqs;       /* per asker: the last trade requests applied to this squad's money */
    ShopSquad() : money(0), moneyChanges(0), refillsApplied(0) {}
};
struct ShopRec
{
    unsigned int generation;
    int writerSlot;
    std::string key;                 /* the shop building's full position key */
    double deadline;                 /* in-game hours */
    unsigned int refills, repairMark;
    std::vector<ShopSquad> squads;
    ShopRec() : generation(0), writerSlot(0), deadline(0.0), refills(0), repairMark(kSrNoRepair) {}
};

/* ---- the id ---- */
/* any spelling of the prefix counts (file names ignore case); only the spelling MakeShopRecId makes parses */
inline bool IsShopRecId(const std::string& id)
{
    static const char kPrefix[] = "shop.";
    if (id.size() < 5) return false;
    for (size_t i = 0; i < 5; ++i) { char c = id[i]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c != kPrefix[i]) return false; }
    return true;
}
/* 1 = exactly a spelling MakeShopRecId makes (its area square into sx / sy when not 0); 0 = not a shop id */
inline int ShopRecIdParse(const std::string& id, int* sx, int* sy)
{
    if (id.size() < 5 || id.compare(0, 5, "shop.") != 0) return 0;
    return coopbrec::BoxRecIdParse("box." + id.substr(5), sx, sy, 0, 0, 0);
}
/* the shop id of the building a full position key names; "" when the key is not a position key */
inline std::string MakeShopRecId(const std::string& fullKey)
{
    const std::string b = coopbrec::MakeBoxRecId(fullKey);
    return b.empty() ? std::string() : "shop." + b.substr(4);
}

/* ---- validity ---- */
inline bool SrHoursOk(double h) { return h == h && h >= 0.0 && h <= kSrHoursMax; }
inline bool ShopRecValid(const ShopRec& r)
{
    if (!coopbrec::BrSlotOk(r.writerSlot) || !coopbrec::BrTextOk(r.key, coopbrec::kBrKeyMax) || MakeShopRecId(r.key).empty()) return false;
    if (!SrHoursOk(r.deadline) || r.squads.size() > kSrMaxSquads) return false;
    for (size_t i = 0; i < r.squads.size(); ++i)
    {
        const ShopSquad& s = r.squads[i];
        if (!coopbrec::BrTextOk(s.name, kSrNameMax) || s.money < 0 || s.money > kSrMoneyMax || s.reqs.size() > kSrMaxReqs) return false;
        for (size_t j = 0; j < i; ++j) if (r.squads[j].name == s.name) return false;
        for (size_t a = 0; a < s.reqs.size(); ++a)
        {
            for (size_t b = 0; b < a; ++b) if (s.reqs[b].asker == s.reqs[a].asker) return false;
            const std::vector<ShopReqDone>& d = s.reqs[a].done;
            if (d.size() > kSrReqKeep) return false;
            for (size_t x = 0; x < d.size(); ++x)
            {
                if (d[x].n < 0 || d[x].move < -kSrMoneyMax || d[x].move > kSrMoneyMax || d[x].revoked > 4u) return false;
                for (size_t y = 0; y < x; ++y) if (d[y].id == d[x].id) return false;
            }
        }
    }
    return true;
}

/* ---- bytes ---- */
inline bool EncodeShopRec(std::vector<char>* out, const ShopRec& r)
{
    out->clear();
    if (!ShopRecValid(r)) return false;
    coopbrec::BrPutU32(out, kSrMarker);
    coopbrec::BrPutU32(out, kSrFormat);
    coopbrec::BrPutU32(out, r.generation);
    coopbrec::BrPutU32(out, (unsigned int)r.writerSlot);
    coopbrec::BrPutStr(out, r.key);
    unsigned int d[2] = { 0u, 0u };
    std::memcpy(d, &r.deadline, 8);
    coopbrec::BrPutU32(out, d[0]);
    coopbrec::BrPutU32(out, d[1]);
    coopbrec::BrPutU32(out, r.refills);
    coopbrec::BrPutU32(out, r.repairMark);
    coopbrec::BrPutU32(out, (unsigned int)r.squads.size());
    for (size_t i = 0; i < r.squads.size(); ++i)
    {
        coopbrec::BrPutStr(out, r.squads[i].name);
        coopbrec::BrPutU32(out, (unsigned int)r.squads[i].money);
        coopbrec::BrPutU32(out, r.squads[i].moneyChanges);
        coopbrec::BrPutU32(out, r.squads[i].refillsApplied);
        coopbrec::BrPutU32(out, (unsigned int)r.squads[i].reqs.size());
        for (size_t q = 0; q < r.squads[i].reqs.size(); ++q)
        {
            const ShopReq& rq = r.squads[i].reqs[q];
            coopbrec::BrPutU32(out, rq.asker);
            coopbrec::BrPutU32(out, rq.epoch);
            coopbrec::BrPutU32(out, (unsigned int)rq.done.size());
            for (size_t x = 0; x < rq.done.size(); ++x)
            {
                coopbrec::BrPutU32(out, rq.done[x].id);
                coopbrec::BrPutU32(out, (unsigned int)rq.done[x].n);
                coopbrec::BrPutU32(out, (unsigned int)rq.done[x].move);
                coopbrec::BrPutU32(out, rq.done[x].revoked);
                if (rq.done[x].revoked == 2u || rq.done[x].revoked == 3u)
                {   /* an owed or claimed entry: its claimer (claimed), then what its undo needs */
                    if (rq.done[x].revoked == 3u) coopbrec::BrPutU32(out, rq.done[x].claimer);
                    const ShopUndoDesc& u = rq.done[x].undo;
                    coopbrec::BrPutU32(out, u.keeperUid);
                    coopbrec::BrPutU32(out, (unsigned int)u.dir);
                    coopbrec::BrPutStr(out, u.base); coopbrec::BrPutStr(out, u.company); coopbrec::BrPutStr(out, u.material); coopbrec::BrPutStr(out, u.color);
                    unsigned int fq = 0, fc = 0;
                    std::memcpy(&fq, &u.quality, 4); std::memcpy(&fc, &u.charges, 4);
                    coopbrec::BrPutU32(out, fq); coopbrec::BrPutU32(out, fc);
                    coopbrec::BrPutU32(out, (unsigned int)u.functionKind); coopbrec::BrPutU32(out, (unsigned int)u.level); coopbrec::BrPutU32(out, (unsigned int)u.unique);
                }
            }
        }
    }
    if (out->size() > kSrMaxBytes) { out->clear(); return false; }
    return true;
}
inline int DecodeShopRec(const char* p, size_t size, ShopRec* out)
{
    if (p == 0 || size < 4) return kSrShort;
    if (size > kSrMaxBytes) return kSrTooBig;
    size_t at = 0;
    unsigned int marker = 0, format = 0, writer = 0, d0 = 0, d1 = 0, n = 0;
    ShopRec r;
    if (!coopbrec::BrGet4(p, size, &at, &marker)) return kSrShort;
    if (marker != kSrMarker) return kSrBadMarker;
    if (!coopbrec::BrGet4(p, size, &at, &format)) return kSrShort;
    if (format != kSrFormat) return kSrBadFormat;
    if (!coopbrec::BrGet4(p, size, &at, &r.generation) || !coopbrec::BrGet4(p, size, &at, &writer)
        || !coopbrec::BrGetStr(p, size, &at, coopbrec::kBrKeyMax, &r.key) || !coopbrec::BrGet4(p, size, &at, &d0)
        || !coopbrec::BrGet4(p, size, &at, &d1) || !coopbrec::BrGet4(p, size, &at, &r.refills)
        || !coopbrec::BrGet4(p, size, &at, &r.repairMark) || !coopbrec::BrGet4(p, size, &at, &n)) return kSrShort;
    if (n > kSrMaxSquads) return kSrBadRecord;
    r.writerSlot = (int)writer;
    const unsigned int d[2] = { d0, d1 };
    std::memcpy(&r.deadline, d, 8);
    for (unsigned int i = 0; i < n; ++i)
    {
        ShopSquad s;
        unsigned int money = 0;
        if (!coopbrec::BrGetStr(p, size, &at, kSrNameMax, &s.name) || !coopbrec::BrGet4(p, size, &at, &money)
            || !coopbrec::BrGet4(p, size, &at, &s.moneyChanges) || !coopbrec::BrGet4(p, size, &at, &s.refillsApplied)) return kSrShort;
        s.money = (int)money;
        unsigned int m = 0;
        if (!coopbrec::BrGet4(p, size, &at, &m)) return kSrShort;
        if (m > kSrMaxReqs) return kSrBadRecord;
        for (unsigned int q = 0; q < m; ++q)
        {
            ShopReq rq;
            unsigned int k = 0;
            if (!coopbrec::BrGet4(p, size, &at, &rq.asker) || !coopbrec::BrGet4(p, size, &at, &rq.epoch) || !coopbrec::BrGet4(p, size, &at, &k)) return kSrShort;
            if (k > kSrReqKeep) return kSrBadRecord;
            for (unsigned int x = 0; x < k; ++x)
            {
                ShopReqDone d;
                unsigned int dn = 0, dm = 0;
                if (!coopbrec::BrGet4(p, size, &at, &d.id) || !coopbrec::BrGet4(p, size, &at, &dn) || !coopbrec::BrGet4(p, size, &at, &dm)
                    || !coopbrec::BrGet4(p, size, &at, &d.revoked)) return kSrShort;
                d.n = (int)dn; d.move = (int)dm;
                if (d.revoked == 2u || d.revoked == 3u)
                {
                    if (d.revoked == 3u && !coopbrec::BrGet4(p, size, &at, &d.claimer)) return kSrShort;
                    ShopUndoDesc& u = d.undo;
                    unsigned int dir = 0, fq = 0, fc = 0, fk = 0, lv = 0, un = 0;
                    if (!coopbrec::BrGet4(p, size, &at, &u.keeperUid) || !coopbrec::BrGet4(p, size, &at, &dir)
                        || !coopbrec::BrGetStr(p, size, &at, kSrNameMax, &u.base) || !coopbrec::BrGetStr(p, size, &at, kSrNameMax, &u.company)
                        || !coopbrec::BrGetStr(p, size, &at, kSrNameMax, &u.material) || !coopbrec::BrGetStr(p, size, &at, kSrNameMax, &u.color)
                        || !coopbrec::BrGet4(p, size, &at, &fq) || !coopbrec::BrGet4(p, size, &at, &fc) || !coopbrec::BrGet4(p, size, &at, &fk)
                        || !coopbrec::BrGet4(p, size, &at, &lv) || !coopbrec::BrGet4(p, size, &at, &un)) return kSrShort;
                    std::memcpy(&u.quality, &fq, 4); std::memcpy(&u.charges, &fc, 4);
                    if (dir > 1u || !(u.quality == u.quality) || !(u.charges == u.charges) || u.quality > 1.0e9f || u.quality < -1.0e9f
                        || u.charges > 1.0e9f || u.charges < -1.0e9f) return kSrBadRecord;
                    u.dir = (int)dir; u.functionKind = (int)fk; u.level = (int)lv; u.unique = (int)un;
                    u.has = u.base.empty() ? 0 : 1;
                }
                rq.done.push_back(d);
            }
            s.reqs.push_back(rq);
        }
        r.squads.push_back(s);
    }
    if (at != size) return kSrTrailing;
    if (!ShopRecValid(r)) return kSrBadRecord;
    if (out != 0) *out = r;
    return kSrOk;
}
inline const char* ShopDecodeWhy(int why)
{
    switch (why)
    {
    case kSrOk: return "read whole";
    case kSrShort: return "cut short";
    case kSrBadMarker: return "not a shop record (marker)";
    case kSrBadFormat: return "an unknown format";
    case kSrBadRecord: return "a field out of range (slot, key, deadline, name, money or request list)";
    case kSrTrailing: return "bytes after the last squad";
    case kSrTooBig: return "over the size cap";
    }
    return "?";
}

/* ---- the world server ---- */
/* One write of shop record `id` from slot `senderSlot`; holderSlot: the slot the area map names as the holder of the record's area
   (-1 nobody); exists / storedGen: a stored record and its generation; sameBytes: the bytes equal the stored file. coopbrec::kBrAccept,
   kBrRepeat (nothing to write) or a coopbrec refusal reason. *out (when not 0) gets the decoded record when it read. */
inline int ShopJudgeWrite(const std::string& id, const char* p, size_t size, int senderSlot, int holderSlot, int exists,
                          unsigned int storedGen, int sameBytes, ShopRec* out)
{
    if (ShopRecIdParse(id, 0, 0) == 0) return coopbrec::kBrRefuseBadId;
    ShopRec r;
    if (DecodeShopRec(p, size, &r) != kSrOk) return coopbrec::kBrRefuseUnreadable;
    if (out != 0) *out = r;
    if (MakeShopRecId(r.key) != id) return coopbrec::kBrRefuseWrongBox;
    if (senderSlot < 0 || r.writerSlot != senderSlot) return coopbrec::kBrRefuseWriter;
    if (holderSlot < 0 || senderSlot != holderSlot) return coopbrec::kBrRefuseNotWriter;
    if (exists != 0)
    {
        if (r.generation < storedGen) return coopbrec::kBrRefuseOlder;
        if (r.generation == storedGen) return (sameBytes != 0) ? coopbrec::kBrRepeat : coopbrec::kBrRefuseSameGen;
    }
    return coopbrec::kBrAccept;
}

/* ---- the game: keeping and writing ---- */
/* a received record is kept when nothing is kept for its id or its generation is above the kept one */
inline int ShopIntakeKeep(int haveKept, unsigned int keptGen, unsigned int gen) { return (haveKept == 0 || gen > keptGen) ? 1 : 0; }
/* WHICH COPY A GAME WORKS FROM. Every record the world server sends is kept as "the server's last record" whatever the working copy's
   generation. It also becomes the working copy when: there is none; the working copy is stale (this game's write was refused and no
   base was known) and the arrival is at or above the generation the refusal named as stored; it is newer; or it is at the working
   copy's generation from another writer (the world server forwards only what it stored, so this game's own write at that
   generation was or will be refused). 1 = the arrival becomes the working copy. */
inline int ShopArrivalTakes(int haveLocal, unsigned int localGen, int localWriter, int stale, unsigned int floorGen, unsigned int arrGen, int arrWriter)
{
    if (haveLocal == 0) return 1;
    if (stale != 0) return (arrGen >= floorGen) ? 1 : 0;
    if (arrGen > localGen) return 1;
    return (arrGen == localGen && arrWriter != localWriter) ? 1 : 0;
}
/* AFTER A REFUSAL the working copy goes back to the record the world server holds (generation storedGen): the server's last record
   when it is that generation, else this game's own sent write of that generation (the world server does not send a writer its own
   write back). 1 = *out is that record; 0 = none known here (the working copy stays stale until a record arrives). */
inline int ShopRefusalBase(int haveSrv, const ShopRec& srv, const std::vector<ShopRec>& ownSent, unsigned int storedGen, unsigned int sentGen, ShopRec* out)
{
    if (storedGen == 0u) return 0;
    if (haveSrv != 0 && srv.generation == storedGen) { *out = srv; return 1; }
    if (storedGen == sentGen) return 0;
    for (size_t i = ownSent.size(); i > 0; --i)
        if (ownSent[i - 1].generation == storedGen) { *out = ownSent[i - 1]; return 1; }
    return 0;
}
/* the generation of this game's next write: above the kept copy and above any generation a refusal named as stored */
inline unsigned int ShopNextGen(int haveKept, unsigned int keptGen, unsigned int floorGen)
{
    const unsigned int base = (haveKept != 0 && keptGen > floorGen) ? keptGen : floorGen;
    return base + 1u;
}
inline int ShopSquadAt(const ShopRec& r, const std::string& name)
{
    for (size_t i = 0; i < r.squads.size(); ++i) if (r.squads[i].name == name) return (int)i;
    return -1;
}
/* 1 = the record does not carry the squad and holds kSrMaxSquads already: no entry can be made for it */
inline int ShopRecFull(const ShopRec& r, int squadAt) { return (squadAt < 0 && r.squads.size() >= kSrMaxSquads) ? 1 : 0; }

/* ---- the restock gate ---- */
/* The road a restock call came by: the engine's timed check (the squad update), the engine's forced refill (the squad builder made
   the keepers), the holder's own start of a due refill, the empty-shop repair's start, the mod's own test command. */
const int kRoadTimed = 0, kRoadForced = 1, kRoadHolderStarted = 2, kRoadRepair = 3, kRoadLever = 4, kRoadMax = 4;
const int kGateRun = 0, kGateRefuseNotHolder = 1, kGateRefuseNotKnown = 2, kGateRefuseNotDue = 3, kGateHeldPlayerInside = 4,
          kGateRefuseHasRecord = 5, kGateRefuseFull = 6, kGateMax = 6;
inline const char* ShopRoadName(int road)
{
    return road == kRoadTimed ? "timed" : road == kRoadForced ? "forced" : road == kRoadHolderStarted ? "holder-started"
         : road == kRoadRepair ? "repair" : road == kRoadLever ? "lever" : "?";
}
inline const char* ShopGateName(int v)
{
    return v == kGateRun ? "run" : v == kGateRefuseNotHolder ? "refuse-not-holder" : v == kGateRefuseNotKnown ? "refuse-not-known"
         : v == kGateRefuseNotDue ? "refuse-not-due" : v == kGateHeldPlayerInside ? "held-player-inside"
         : v == kGateRefuseHasRecord ? "refuse-has-record" : v == kGateRefuseFull ? "refuse-record-full" : "?";
}
/* 1 = the squad's refill is due: the building's deadline has passed, or the squad has no entry yet, or the building refilled since
   this squad's last refill (each keeper squad of the building refills once per building refill) */
inline int ShopDue(const ShopRec& r, int squadAt, double now)
{
    if (r.deadline <= now) return 1;
    if (squadAt < 0 || squadAt >= (int)r.squads.size()) return 1;
    return (r.squads[squadAt].refillsApplied < r.refills) ? 1 : 0;
}
/* THE VERDICT. holder: the current area map names this game as the holder of the shop's area (not 'runs the keeper', not the load
   window's presumption); known: this game has the shop's record, or the world server's records of that area have all arrived and
   none is there (and the area's stored counters are in); haveRecord / haveSquad: a record exists / it carries this squad; due:
   ShopDue; anyInside: any player's character is inside the shop building. The lever runs on the holder whatever the record says. A
   forced refill runs only for a squad the record does not carry yet. A due refill waits while a player is inside. full: the record
   does not carry the squad and has no room for another (ShopRecFull) - nothing could be written, so nothing runs. */
inline int ShopGateDecide(int road, int holder, int known, int haveRecord, int haveSquad, int due, int anyInside, int full = 0)
{
    if (holder == 0) return kGateRefuseNotHolder;
    if (road == kRoadLever) return kGateRun;
    if (known == 0) return kGateRefuseNotKnown;
    if (full != 0) return kGateRefuseFull;
    if (road == kRoadForced) return (haveRecord != 0 && haveSquad != 0) ? kGateRefuseHasRecord : kGateRun;
    if (haveRecord != 0 && haveSquad != 0 && due == 0) return kGateRefuseNotDue;
    if (anyInside != 0) return kGateHeldPlayerInside;
    return kGateRun;
}
/* The engine deadline a refused or held restock leaves on the squad (Platoon +0x1F8): the record's deadline while it is ahead,
   else a short retry step, so the engine's own check neither fires every pass nor waits past the shop's time. */
inline double ShopEngineTimer(int haveRecord, double recDeadline, double now)
{
    return (haveRecord != 0 && recDeadline > now) ? recDeadline : now + kSrRetryHours;
}
/* AFTER A RESTOCK RAN on the holder. timerChanged: the engine moved the squad's deadline (it returns early, refilling nothing, while
   the shop's inside is shown or with no home - then nothing is written); newDeadline: the deadline the engine wrote (now + the
   squad's period); money / moneyRead: the keeper squad's money after it. The building's refill count moves and its deadline becomes
   the engine's when the building was due (or had no record); a squad answering a building refill already made only records it. The
   squad's money is the restock's, as a new change. 1 = *r changed (generation raised: write it); 0 = nothing to write. */
inline int ShopAfterRun(ShopRec* r, int haveRecord, const std::string& squad, int timerChanged, double newDeadline, double now,
                        int moneyRead, int money)
{
    if (timerChanged == 0 || !SrHoursOk(newDeadline) || squad.empty()) return 0;
    if (haveRecord == 0) { r->deadline = newDeadline; r->refills = 1; r->repairMark = kSrNoRepair; r->squads.clear(); }
    else if (r->deadline <= now) { r->refills += 1; r->deadline = newDeadline; }
    int at = ShopSquadAt(*r, squad);
    if (at < 0)
    {
        if (r->squads.size() >= kSrMaxSquads) return 0;
        ShopSquad s; s.name = squad;
        r->squads.push_back(s);
        at = (int)r->squads.size() - 1;
    }
    ShopSquad& s = r->squads[at];
    s.refillsApplied = r->refills;
    if (moneyRead != 0)
    {
        s.money = (money < 0) ? 0 : (money > kSrMoneyMax ? kSrMoneyMax : money);
        s.moneyChanges += 1;
    }
    r->generation += 1;
    return 1;
}

/* ---- the keeper's money ---- */
/* The game running a keeper squad takes the record's money when the record's change count is above the one it last took. Its first
   look in a session takes the count without the money (the squad's own save and record carry the trades since the last refill);
   a resend of the same count changes nothing. */
inline int ShopMoneyApplyDue(int haveSeen, unsigned int seenChanges, unsigned int recChanges)
{
    return (haveSeen != 0 && recChanges > seenChanges) ? 1 : 0;
}
/* The money lever and any other set: the squad's money becomes `money` as a new change. 1 = *r changed (generation raised). */
inline int ShopMoneySet(ShopRec* r, const std::string& squad, int money)
{
    const int at = ShopSquadAt(*r, squad);
    if (at < 0 || money < 0 || money > kSrMoneyMax) return 0;
    r->squads[at].money = money;
    r->squads[at].moneyChanges += 1;
    r->generation += 1;
    return 1;
}

/* ---- the empty-shop repair ---- */
/* The holder asks a repair (the refill made due now) once per refill: not again until a refill has happened since the refill the
   last repair asked for (a shop that stays empty after its repair waits for its next timed refill, never loops). */
inline int ShopRepairAllowed(const ShopRec& r)
{
    return (r.repairMark == kSrNoRepair || r.refills > r.repairMark + 1u) ? 1 : 0;
}
/* the repair: the building's deadline becomes now (the timed road refills once), the mark the refill count. 1 = *r changed. */
inline int ShopRepairApply(ShopRec* r, double now)
{
    if (ShopRepairAllowed(*r) == 0 || !SrHoursOk(now)) return 0;
    r->deadline = now;
    r->repairMark = r->refills;
    r->generation += 1;
    return 1;
}

/* ---- trades through the record ---- */
/* WHOSE RULE MOVES A KEEPER SQUAD'S MONEY. No shop record carrying the squad: today's roads (the runner's announcement, the squad
   record's money, the restock message). With one: the holder of the shop's area takes every money change on its pot into the
   record; every other game sets its pot from the record. */
const int kMoneyToday = 0, kMoneyHolderCapture = 1, kMoneyFromRecord = 2;
inline int ShopMoneyRoute(int recordCarriesSquad, int holder)
{
    if (recordCarriesSquad == 0) return kMoneyToday;
    return (holder != 0) ? kMoneyHolderCapture : kMoneyFromRecord;
}
/* the applied entry of exactly this request (asker, epoch, id); 0 = not applied by any holder (a lower or higher id is another request) */
inline const ShopReqDone* ShopReqFind(const ShopSquad& s, unsigned int asker, unsigned int epoch, unsigned int id)
{
    for (size_t i = 0; i < s.reqs.size(); ++i)
    {
        if (s.reqs[i].asker != asker) continue;
        if (s.reqs[i].epoch != epoch) return 0;
        for (size_t x = 0; x < s.reqs[i].done.size(); ++x) if (s.reqs[i].done[x].id == id) return &s.reqs[i].done[x];
        return 0;
    }
    return 0;
}
/* 1 = a holder already applied this request's trade to the squad's money: a resend, from the same holder or after a holder change,
   is answered from the entry and changes nothing */
inline int ShopReqSeen(const ShopSquad& s, unsigned int asker, unsigned int epoch, unsigned int id) { return ShopReqFind(s, asker, epoch, id) != 0 ? 1 : 0; }
/* the holder applied this request (n units, `move` on the keeper's pot): it joins the asker's applied list (a new epoch replaces the
   old list; a full list drops its oldest entry; a full asker list drops its first asker). 1 = the list changed. */
inline int ShopReqNote(ShopSquad* s, unsigned int asker, unsigned int epoch, unsigned int id, int n = 0, int move = 0)
{
    size_t at = s->reqs.size();
    for (size_t i = 0; i < s->reqs.size(); ++i) if (s->reqs[i].asker == asker) { at = i; break; }
    if (at == s->reqs.size())
    {
        if (s->reqs.size() >= kSrMaxReqs) s->reqs.erase(s->reqs.begin());
        ShopReq q; q.asker = asker; q.epoch = epoch;
        s->reqs.push_back(q);
        at = s->reqs.size() - 1;
    }
    ShopReq& q = s->reqs[at];
    if (q.epoch != epoch) { q.epoch = epoch; q.done.clear(); }
    for (size_t x = 0; x < q.done.size(); ++x) if (q.done[x].id == id) return 0;
    if (q.done.size() >= kSrReqKeep)
    {   /* a full list drops its LOWEST id, so every dropped id lies below the lowest id kept (the asker's floor, ShopReqBelowFloor) */
        size_t lo = 0;
        for (size_t x = 1; x < q.done.size(); ++x) if (q.done[x].id < q.done[lo].id) lo = x;
        if (id < q.done[lo].id) return 0;   /* at or below the floor already: not kept */
        q.done.erase(q.done.begin() + (std::ptrdiff_t)lo);
    }
    ShopReqDone d; d.id = id; d.n = (n < 0) ? 0 : n;
    d.move = (move < -kSrMoneyMax) ? -kSrMoneyMax : (move > kSrMoneyMax ? kSrMoneyMax : move);
    q.done.push_back(d);
    return 1;
}
/* AN ENTRY'S CANCEL STATE (ShopReqDone::revoked): kRevApplied 0; kRevOwed 2 - cancelled, its goods and money undo owed; kRevClaimed 3 -
   the undo claimed by the game in `claimer` (the exact holder that read it owed in a stored record): only that game, once its claim is
   stored, applies the undo, then writes kRevUndone; kRevUndone 1 - cancelled and undone (with n 0 and move 0 a marker: no holder
   applied it); kRevRefused 4 - the cancel refused by the claimer, the trade stands (a counter of the shop could never hold the undo
   in a record): its asker is answered refused. ShopRevRank orders them: applied 0, owed 1, claimed 2, undone or refused 3 (both
   final). At most kSrOwedCap entries of a record are owed or claimed (each carries its undo's description). */
const unsigned int kRevApplied = 0u, kRevUndone = 1u, kRevOwed = 2u, kRevClaimed = 3u, kRevRefused = 4u;
const unsigned int kSrOwedCap = 32u;
inline int ShopRevRank(unsigned int revoked) { return (revoked == kRevApplied) ? 0 : ((revoked == kRevOwed) ? 1 : ((revoked == kRevClaimed) ? 2 : 3)); }
inline ShopReqDone* ShopReqFindMut(ShopSquad* s, unsigned int asker, unsigned int epoch, unsigned int id)
{
    for (size_t i = 0; i < s->reqs.size(); ++i)
    {
        if (s->reqs[i].asker != asker || s->reqs[i].epoch != epoch) continue;
        for (size_t x = 0; x < s->reqs[i].done.size(); ++x) if (s->reqs[i].done[x].id == id) return &s->reqs[i].done[x];
    }
    return 0;
}
/* the entry marked undone (from applied or owed): a further resend is not answered as applied, and *move gets what it had moved on
   the keeper's pot, which the holder takes back. 1 = marked now; 0 = no such entry, or undone already. */
inline int ShopReqRevoke(ShopSquad* s, unsigned int asker, unsigned int epoch, unsigned int id, int* move)
{
    *move = 0;
    ShopReqDone* d = ShopReqFindMut(s, asker, epoch, id);
    if (d == 0 || d->revoked == kRevUndone) return 0;
    d->revoked = kRevUndone; *move = d->move;
    return 1;
}
/* an applied entry marked cancelled with its undo owed, carrying what the undo needs (desc, when given). 1 = marked now; 0 = no such
   entry, or not applied */
inline int ShopReqOwe(ShopSquad* s, unsigned int asker, unsigned int epoch, unsigned int id, const ShopUndoDesc* desc = 0)
{
    ShopReqDone* d = ShopReqFindMut(s, asker, epoch, id);
    if (d == 0 || d->revoked != kRevApplied) return 0;
    d->revoked = kRevOwed;
    if (desc != 0) d->undo = *desc;
    return 1;
}
/* the entries of record r that are owed or claimed (each carries its undo's description; at most kSrOwedCap) */
inline unsigned int ShopRecOwedCount(const ShopRec& r)
{
    unsigned int n = 0;
    for (size_t s = 0; s < r.squads.size(); ++s)
        for (size_t q = 0; q < r.squads[s].reqs.size(); ++q)
            for (size_t x = 0; x < r.squads[s].reqs[q].done.size(); ++x)
            { const unsigned int v = r.squads[s].reqs[q].done[x].revoked; if (v == kRevOwed || v == kRevClaimed) ++n; }
    return n;
}
/* THE UNDONE MARK WITH THE UNDO'S MONEY: the entry marked undone and what it had moved on the keeper's pot (*move) taken back out of
   the squad's money in the same record - an undone entry holds no money (ShopRebase's rule) - so a stored record never has the mark
   without the money or the money without the mark. 1 = marked now; 0 = no such entry, or undone already. */
inline int ShopReqUndo(ShopSquad* s, unsigned int asker, unsigned int epoch, unsigned int id, int* move)
{
    if (ShopReqRevoke(s, asker, epoch, id, move) == 0) return 0;
    if (*move != 0)
    {
        const long long v = (long long)s->money - (long long)*move;
        s->money = (v < 0) ? 0 : (v > (long long)kSrMoneyMax ? kSrMoneyMax : (int)v);
        s->moneyChanges += 1;
    }
    return 1;
}
/* A RE-CLAIM: a claim stored for `from` whose claimer has been away (shopcancel.h ShopOwedDue kOwReclaim) taken over by slot. 1 =
   re-claimed now; 0 = not claimed by `from` */
inline int ShopReqReclaim(ShopSquad* s, unsigned int asker, unsigned int epoch, unsigned int id, unsigned int from, unsigned int slot)
{
    ShopReqDone* d = ShopReqFindMut(s, asker, epoch, id);
    if (d == 0 || d->revoked != kRevClaimed || d->claimer != from || from == slot) return 0;
    d->claimer = slot;
    return 1;
}
/* THE CANCEL REFUSED: an owed or claimed entry (its claimer kept) marked kRevRefused - the trade stands, its money stays in the squad's,
   nothing is undone, and the owed place is free. 1 = marked now; 0 = not owed or claimed */
inline int ShopReqRefuse(ShopSquad* s, unsigned int asker, unsigned int epoch, unsigned int id)
{
    ShopReqDone* d = ShopReqFindMut(s, asker, epoch, id);
    if (d == 0 || (d->revoked != kRevOwed && d->revoked != kRevClaimed)) return 0;
    d->revoked = kRevRefused;
    d->undo = ShopUndoDesc();
    return 1;
}
/* an owed entry claimed by slot (its undo is applied by that game alone, once this claim is stored). 1 = claimed now; 0 = not owed */
inline int ShopReqClaim(ShopSquad* s, unsigned int asker, unsigned int epoch, unsigned int id, unsigned int slot)
{
    ShopReqDone* d = ShopReqFindMut(s, asker, epoch, id);
    if (d == 0 || d->revoked != kRevOwed) return 0;
    d->revoked = kRevClaimed; d->claimer = slot;
    return 1;
}
/* 1 = one of this game's own requests in flight (its process epoch, exactly its id) is already in the record's money: it is not added
   again to the pot this game shows. */
inline int ShopReqCovered(const ShopSquad& s, unsigned int myEpoch, unsigned int rowId)
{
    for (size_t i = 0; i < s.reqs.size(); ++i)
    {
        if (s.reqs[i].epoch != myEpoch) continue;
        for (size_t x = 0; x < s.reqs[i].done.size(); ++x) if (s.reqs[i].done[x].id == rowId) return 1;
    }
    return 0;
}
/* one of this game's in-flight trades on the pot a game that is not the holder shows: its move counts when the engine step moved
   that very pot here (rowPot, whoever runs the keeper) and the record does not carry its request yet */
inline long long ShopInFlightAdd(const ShopSquad& s, unsigned int myEpoch, unsigned int rowId, const void* rowPot, const void* pot, long long move)
{
    return (rowPot != 0 && rowPot == pot && ShopReqCovered(s, myEpoch, rowId) == 0) ? move : 0;
}
/* WHETHER A TRADE MAY MOVE A KEEPER'S MONEY HERE. carried: a kept shop record names the keeper's squad; exactHolder: the current area
   map names this game as the holder of the shop's area; stale: this game's copy is under a refusal. 0 = no record carries it (today's
   roads); 1 = this game records the trade; 2 = refused before anything moves (nobody would record it, or a resend elsewhere would
   apply it again) */
inline int ShopTradeRole(int carried, int exactHolder, int stale)
{
    if (carried == 0) return 0;
    return (exactHolder != 0 && stale == 0) ? 1 : 2;
}
/* the pot a game that is not the holder shows: the record's money plus its own in-flight trade moves the record does not carry yet,
   kept inside [0, kSrMoneyMax] */
inline int ShopRunnerPot(int recordMoney, long long inFlight)
{
    const long long v = (long long)recordMoney + inFlight;
    return (v < 0) ? 0 : (v > (long long)kSrMoneyMax ? kSrMoneyMax : (int)v);
}
/* THE HOLDER'S CAPTURE: its pot (moved by its own engine's trades, by the trade requests it applied, by its refunds) is the squad's
   money; a pot that differs from the record is written as a new change. 1 = *r changed (generation raised). */
inline int ShopMoneyFromPot(ShopRec* r, const std::string& squad, int pot)
{
    const int at = ShopSquadAt(*r, squad);
    if (at < 0 || pot < 0 || pot > kSrMoneyMax || r->squads[at].money == pot) return 0;
    r->squads[at].money = pot;
    r->squads[at].moneyChanges += 1;
    r->generation += 1;
    return 1;
}
/* THE HOLDER'S POT BY CHANGE: `saved` is the pot that last matched the record's money; the trades since moved the pot by
   (cur - saved), and the record's money may have moved too (another writer's record arrived). The squad's money is now the
   record's money plus this game's own change, kept inside [0, kSrMoneyMax]; the pot is set to it and it becomes the new `saved`. */
inline int ShopPotByChange(int recMoney, int cur, int saved)
{
    const long long v = (long long)recMoney + (long long)cur - (long long)saved;
    return (v < 0) ? 0 : (v > (long long)kSrMoneyMax ? kSrMoneyMax : (int)v);
}
/* 1 = the request (asker, epoch, id) is not in the record but lies at or below the asker's floor: its list for that epoch is full and
   the id is below every id kept (ShopReqNote drops the lowest id). It may have been applied and dropped - it is refused, never
   applied (applying it again could move the keeper's money twice). */
inline int ShopReqBelowFloor(const ShopSquad& s, unsigned int asker, unsigned int epoch, unsigned int id)
{
    for (size_t i = 0; i < s.reqs.size(); ++i)
    {
        if (s.reqs[i].asker != asker) continue;
        const std::vector<ShopReqDone>& d = s.reqs[i].done;
        if (s.reqs[i].epoch != epoch || d.size() < kSrReqKeep) return 0;
        for (size_t x = 0; x < d.size(); ++x) if (d[x].id <= id) return 0;
        return 1;
    }
    return 0;
}
/* THE RECORD THIS GAME'S WORKING COPY IS BUILT ON that the record now taking its place carries (up to generation maxGen): the newest
   of `base` (the stored record the working copy was last built on) and this game's own sent writes not refused that came after it
   (generation above base's - written on top of it). 0 = none known. */
inline const ShopRec* ShopRebaseParent(int haveBase, const ShopRec& base, const std::vector<ShopRec>& ownSent, unsigned int maxGen)
{
    const ShopRec* p = (haveBase != 0 && base.generation <= maxGen) ? &base : 0;
    for (size_t i = 0; i < ownSent.size(); ++i)
        if (ownSent[i].generation <= maxGen && (haveBase == 0 || ownSent[i].generation > base.generation)
            && (p == 0 || ownSent[i].generation >= p->generation)) p = &ownSent[i];
    return p;
}
/* 1 = a refusal names as stored the very record this game's working copy is already built on (its arrival came first and the
   working copy was rebased onto it): nothing to fall back to - the working copy stands */
inline int ShopRefusalSuperseded(int haveBase, unsigned int baseGen, int baseWriter, unsigned int storedGen, int storedWriter)
{
    return (haveBase != 0 && baseGen == storedGen && baseWriter == storedWriter) ? 1 : 0;
}
/* REBASE: the record `onto` (another writer's arrival, or the record a refusal names as stored) replaces this game's working copy
   `work`, which was built on `parent`. For each squad named in `held` (the squads this game holds the pot of) that `onto`, `work`
   and `parent` all carry: the money this game added since parent (work - parent) goes on top of onto's money as a change, and the
   requests this game applied since parent (in work, not in parent) that onto lacks are noted in it (an asker onto carries under
   another epoch keeps onto's list). The result keeps at most kSrOwedCap entries owed or claimed: an entry of this game's that would
   go past it stays (or is noted) applied - its cancel is asked again - and is counted in *capHeld. *out = onto with those on top;
   the return is how many changes went on (0 = *out is onto). */
inline int ShopRebase(const ShopRec& onto, const ShopRec& work, const ShopRec& parent, const std::vector<std::string>& held, ShopRec* out,
                      int* capHeld = 0)
{
    *out = onto;
    int changed = 0, overCap = 0;
    for (size_t h = 0; h < held.size(); ++h)
    {
        const int o = ShopSquadAt(*out, held[h]), w = ShopSquadAt(work, held[h]), p = ShopSquadAt(parent, held[h]);
        if (o < 0 || w < 0 || p < 0) continue;
        ShopSquad& os = out->squads[(size_t)o];
        const ShopSquad& ws = work.squads[(size_t)w];
        const ShopSquad& ps = parent.squads[(size_t)p];
        const int mine = ws.money - ps.money;
        if (mine != 0)
        {
            const int m = ShopPotByChange(os.money, ws.money, ps.money);
            if (m != os.money) { os.money = m; os.moneyChanges += 1; ++changed; }
        }
        for (size_t q = 0; q < ws.reqs.size(); ++q)
        {
            const ShopReq& rq = ws.reqs[q];
            int otherEpoch = 0;
            for (size_t i = 0; i < os.reqs.size(); ++i) if (os.reqs[i].asker == rq.asker && os.reqs[i].epoch != rq.epoch) otherEpoch = 1;
            if (otherEpoch != 0) continue;
            for (size_t x = 0; x < rq.done.size(); ++x)
            {
                const ShopReqDone& d = rq.done[x];
                const ShopReqDone* pd = ShopReqFind(ps, rq.asker, rq.epoch, d.id);
                const ShopReqDone* od = ShopReqFind(os, rq.asker, rq.epoch, d.id);
                if (od != 0)
                {   /* the same entry in onto and in this game's copy. A marker (no units, no money: written for a request no holder had
                       applied) and an applied entry meeting: the trade happened - the applied entry stands, whichever side carries it
                       (an applied entry of this game's keeps its money in this game's change above). Otherwise the further state goes on
                       (applied, owed, undone - ShopRevRank) when this game made it since parent; an undone entry holds no money: what
                       onto's money still holds of it and what this game's change above holds of it is taken back out */
                    ShopReqDone* om = ShopReqFindMut(&os, rq.asker, rq.epoch, d.id);
                    const int dMarker = (d.revoked == kRevUndone && d.n == 0 && d.move == 0) ? 1 : 0;
                    const int oMarker = (om->revoked == kRevUndone && om->n == 0 && om->move == 0) ? 1 : 0;
                    if (dMarker != 0 && om->revoked == kRevApplied) continue;
                    if (oMarker != 0 && d.revoked == kRevApplied && (d.n != 0 || d.move != 0))
                    {
                        om->n = d.n; om->move = d.move; om->revoked = kRevApplied; ++changed;
                        continue;
                    }
                    const unsigned int before = om->revoked;
                    const int claimOther = (om->revoked == kRevClaimed && d.claimer != om->claimer) ? 1 : 0;   /* another game's stored claim: only it goes on */
                    int markNow = (claimOther == 0 && ShopRevRank(d.revoked) > ShopRevRank(om->revoked) && (pd == 0 || ShopRevRank(pd->revoked) < ShopRevRank(d.revoked))) ? 1 : 0;
                    if (markNow != 0 && (d.revoked == kRevOwed || d.revoked == kRevClaimed) && om->revoked != kRevOwed && om->revoked != kRevClaimed
                        && ShopRecOwedCount(*out) >= kSrOwedCap) { markNow = 0; ++overCap; }   /* the record holds kSrOwedCap owed or claimed already */
                    if (markNow != 0) { om->revoked = d.revoked; om->claimer = d.claimer; if (d.revoked == kRevOwed || d.revoked == kRevClaimed) om->undo = d.undo; ++changed; }
                    if (om->revoked == kRevUndone)
                    {
                        const long long inOnto = (before != kRevUndone) ? (long long)om->move : 0;
                        const long long inMine = (long long)((d.revoked != kRevUndone) ? d.move : 0) - (long long)((pd != 0 && pd->revoked != kRevUndone) ? pd->move : 0);
                        const long long fix = -(inOnto + inMine);
                        if (fix != 0)
                        {
                            const long long v = (long long)os.money + fix;
                            os.money = (v < 0) ? 0 : (v > (long long)kSrMoneyMax ? kSrMoneyMax : (int)v);
                            os.moneyChanges += 1; ++changed;
                        }
                    }
                    else if (d.revoked == kRevUndone && pd != 0 && pd->revoked != kRevUndone && pd->move != 0)
                    {   /* this game's undone mark did not go on (another game's stored claim stands): the money the mark took out of this
                           game's change goes back - the mark and its money go on together or not at all */
                        const long long v = (long long)os.money + (long long)pd->move;
                        os.money = (v < 0) ? 0 : (v > (long long)kSrMoneyMax ? kSrMoneyMax : (int)v);
                        os.moneyChanges += 1; ++changed;
                    }
                    continue;
                }
                if (pd != 0) continue;
                if (ShopReqBelowFloor(os, rq.asker, rq.epoch, d.id) != 0) continue;
                if (ShopReqNote(&os, rq.asker, rq.epoch, d.id, d.n, d.move) == 0) continue;
                if (d.revoked != kRevApplied)
                {
                    ShopReqDone* nd = ShopReqFindMut(&os, rq.asker, rq.epoch, d.id);
                    if (nd != 0 && (d.revoked == kRevOwed || d.revoked == kRevClaimed) && ShopRecOwedCount(*out) >= kSrOwedCap) { ++overCap; nd = 0; }   /* noted applied */
                    if (nd != 0) { nd->revoked = d.revoked; nd->undo = d.undo; nd->claimer = d.claimer; }
                }
                ++changed;
            }
        }
    }
    if (capHeld != 0) *capHeld = overCap;
    return changed;
}
/* ---- a cancel at the holder of the shop's area ---- */
/* THE REQUEST'S ENTRY in a squad's applied lists: kRsNone (not applied by any holder), kRsApplied, kRsRevoked (cancelled; *marker = 1
   when it moved nothing - a marker written for a trade no holder applied), kRsBelowFloor (not kept, at or below the asker's floor) */
const int kRsNone = 0, kRsApplied = 1, kRsRevoked = 2, kRsBelowFloor = 3, kRsOwed = 4, kRsClaimed = 5, kRsCancelRefused = 6;   /* kRsCancelRefused: the trade stands */   /* kRsRevoked: undone; kRsOwed: cancelled, undo owed; kRsClaimed: its undo claimed */
inline int ShopReqState(const ShopSquad& s, unsigned int asker, unsigned int epoch, unsigned int id, int* marker)
{
    *marker = 0;
    const ShopReqDone* d = ShopReqFind(s, asker, epoch, id);
    if (d == 0) return (ShopReqBelowFloor(s, asker, epoch, id) != 0) ? kRsBelowFloor : kRsNone;
    if (d->revoked == kRevApplied) return kRsApplied;
    if (d->revoked == kRevOwed) return kRsOwed;
    if (d->revoked == kRevClaimed) return kRsClaimed;
    if (d->revoked == kRevRefused) return kRsCancelRefused;
    *marker = (d->n == 0 && d->move == 0) ? 1 : 0;
    return kRsRevoked;
}
/* A CANCELLED MARKER for a request no holder applied: an entry of no units and no money, marked cancelled - a later resend or a
   settle-only ask finds it cancelled and never applies the trade fresh. 1 = written into *s; 0 = an entry exists already, or the id
   is at or below the asker's floor (nothing can be kept for it). */
inline int ShopReqMarker(ShopSquad* s, unsigned int asker, unsigned int epoch, unsigned int id)
{
    if (ShopReqFind(*s, asker, epoch, id) != 0 || ShopReqBelowFloor(*s, asker, epoch, id) != 0) return 0;
    if (ShopReqNote(s, asker, epoch, id, 0, 0) == 0) return 0;
    int mv = 0;
    return ShopReqRevoke(s, asker, epoch, id, &mv);
}
/* the request's entry for squad in record r as ShopRevRank (0 applied, 1 owed, 2 claimed, 3 undone); -1 = no entry */
inline int ShopRecRank(const ShopRec& r, const std::string& squad, unsigned int asker, unsigned int epoch, unsigned int id)
{
    const int at = ShopSquadAt(r, squad);
    if (at < 0) return -1;
    const ShopReqDone* d = ShopReqFind(r.squads[(size_t)at], asker, epoch, id);
    return (d != 0) ? ShopRevRank(d->revoked) : -1;
}
/* 1 = record r carries the request's entry for squad, marked cancelled (owed, claimed or undone) */
inline int ShopRecMarked(const ShopRec& r, const std::string& squad, unsigned int asker, unsigned int epoch, unsigned int id)
{
    const int at = ShopSquadAt(r, squad);
    if (at < 0) return 0;
    const ShopReqDone* d = ShopReqFind(r.squads[(size_t)at], asker, epoch, id);
    return (d != 0 && d->revoked != kRevApplied && d->revoked != kRevRefused) ? 1 : 0;
}
/* THIS GAME'S WRITES OF ONE SHOP RECORD IN FLIGHT: each sent write's generation and stamp (the world server's RECORD_SEQ echo carries
   the stamp back once the write is on its disk; a refused write gets BOX_REFUSE naming its generation and no echo). Writes of one id
   land in the order sent, so an echo answers the OLDEST write in flight with its stamp; the writes before it can no longer land
   (a generation below the stored one is refused). The index of the write an echo answers; -1 = none. */
struct ShopFlight { unsigned int gen; long long stamp; ShopRec rec; ShopFlight() : gen(0), stamp(0) {} };
inline int ShopFlightFor(const std::vector<ShopFlight>& f, long long stamp)
{
    for (size_t i = 0; i < f.size(); ++i) if (f[i].stamp == stamp) return (int)i;
    return -1;
}
/* A RESTOCK WHOSE SHOP DOES NOT READ: 0 = the old road (a leader read, and it has no home building - not a shop keeper); 2 =
   refused and asked again soon (the leader could not be read now, or the home could not be read). */
inline int ShopUnreadVerdict(int leaderRead, int haveHome)
{
    return (leaderRead != 0 && haveHome == 0) ? 0 : 2;
}
/* AN ASKER'S CANCEL of an applied request by the old road (tail kind 0, a caravan's): 0 = the record does not carry the keeper
   (today's roads); 1 = marked here (this game is the exact holder: the entry is marked cancelled and the money undo is captured into
   the record); 2 = not markable here. A home shop's cancel goes to the area's holder by tail kind 2 (coopshop::CancelHolderDecide). */
inline int ShopRevokeRoad(int carried, int exactHolder, int stale)
{
    if (carried == 0) return 0;
    return (exactHolder != 0 && stale == 0) ? 1 : 2;
}

}   /* namespace shoprec */
