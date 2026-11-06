/* src/common/farmwire.h - par16 (parity backlog P16; parity-audit-buildings-world.md H5 / M5). A FARM's growth and harvest
 * belong to ONE game - the same writer the production machines have (items.cpp FarmWriterHere: the owner's game while it
 * is online and holds the area, else the area holder). Both kinds ride MSG_BUILD (the building channel) as kinds 6 and 7;
 * buildwire.h's DecodeBuild still refuses them, the session peeks the kind byte first (session.cpp OnBuild).
 *
 *   kind u8 (6 FARM, writer -> other) | u8 len + P7n key | f32 grown | f32 died | f32 cleared | f32 growStart
 *   | i32 harvested | i32 productionState | u8 hasProgress | f32 progress | 3 x f32 marker | u16 n | n x f32 plant age
 *   | u8 nIn | nIn x (f32 stock, f32 rate)
 *     = FarmBuilding +0x518 / +0x51C / +0x520 / +0x524 / +0x528 / +0x468, the harvest bar *(+0x448), the work spot +0x26C,
 *     every Plant::age (plants lektor +0x4E8: count +0x4F0, data +0x4F8, stride 0x28, age +0) and (par16 fold #7) every
 *     input record's water stock and use rate (count +0x480, data +0x488, stride 0x20, stock +0, rate +4 - _updateInputs
 *     0xDF480, timeSkip 0xDF6A0) - the farm's WHOLE growth state. The input records' ITEMS are not carried: they are the
 *     farm's inventory, which the box road carries.
 *   kind u8 (7 FARM_OP, non-writer -> writer) | u8 len + P7n key | u32 worker uid | f32 worker skill | f32 work
 *     = the WORK a worker on the non-writer's game put into the farm: FarmBuilding::operate 0xE59A0 multiplies its amount
 *     (param 3) by the frame's g_dt 0x2132734, so the non-writer sends amount x ITS g_dt (0 < work <= kFarmOpMax) and the
 *     writer calls operate with work / ITS OWN g_dt (par16 fold #2). The worker: operate reads the worker's farming skill
 *     (CharStats::getStat 0x883BF0, stat 0xC) x 0.01 into getYieldChancePerCrop 0xDFDD0 (0xE5A9F-0xE5AC8); uid 0 = no
 *     replicated worker, skill -1 = unknown (par16 fold #1). The writer runs the engine's own operate, so the crop is made
 *     ONCE, into the farm's own output box, and reaches the other game as an ordinary box move.
 *   kind u8 (10 MINE_OP, non-writer -> writer) | u8 len + P7n key | u32 worker uid | f32 amount | f32 work
 *     = the WORK a worker on a game that does not write a production building (a mining node, a machine) put into it:
 *     ProductionBuilding::operate 0x2986C0 (who, amount) scales its step by amount and by the frame's g_dt, and its output
 *     step updateOutput 0x298470 makes at most ONE item per call (the bar goes back to 0). So the non-writer sends the
 *     amount it was called with and amount x ITS g_dt summed (0 < work <= kFarmOpMax); the writer calls the engine's own
 *     operate with that amount, at most once per its own frame's worth of the work (FarmWorkSlice). The ore is made ONCE,
 *     into the building's own box, and reaches every game as an ordinary box move. Kinds 8 / 9 are the building road's.
 *   kind u8 (11 MINE_STATE, writer -> every game) | u8 len + P7n key | i32 productionState | f32 progress
 *     = a production building's state +0x468 (2 working, 1 blocked) and its bar *(+0x448) (the output step updateOutput
 *     0x298470 adds to it and sets it back at an item). A game that does not write the building never runs its step, so the
 *     bar moves there only through this: the writer sends them while a worker works the building on its game, and every
 *     game another game writes it for writes them onto its copy (farm.cpp).
 *   T-674: a machine's WORKER SPOTS (Building +0x3AC the limit, the worker set +0x3D0, its count +0x3E0) are counted by the
 *   game that runs the machine. A game that does not run it asks for a spot before its worker's step takes one locally:
 *   kind u8 (13 SPOT_ASK, asker -> every game) | u8 len + P7n key | u32 worker uid
 *   kind u8 (14 SPOT_ANSWER, runner -> every game) | u8 len + P7n key | u32 worker uid | u8 result (0 full, 1 yes, 2 no copy - no longer
 *     sent: T-853 counts such a worker, see below; 3 revoked - the runner freed a spot it had granted, the asker asks again)
 *   kind u8 (15 SPOT_RELEASE, asker -> every game) | u8 len + P7n key | u32 worker uid
 *     The runner takes the spot through the engine's own take (vt+0x4F8, 0xF7FF0) with its copy of that worker, so its own
 *     workers see the machine full, and frees it through the set's own erase (0x28D400) on a release or when the copy goes.
 *     T-853: a worker the runner has NO copy of is COUNTED (no hand in the set; the runner's own take detour refuses its own
 *     unlisted workers while the set's count plus the counted spots reaches the limit); the asker re-asks a granted spot every
 *     kSpotKeepMs and a counted spot not asked for kSpotCountedForgetMs is freed; any spot the runner frees but for a release
 *     is answered kSpotRevoked.
 *     T-1192 (d): crafting benches and furnaces take the same road (SpotTakeOf) - the same messages, rows and endings.
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes and the same decisions. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include "poskey.h"   /* FarmTsSameFarm: the key's spot and the match every key search uses */
#include <string>
#include <vector>

namespace coopfarm {

const unsigned char kFarmState = 6;
const unsigned char kFarmOp = 7;
const unsigned char kMineOp = 10;
const unsigned char kMineState = 11;
const unsigned char kSpotAsk = 13;       /* 12 is buildwire.h's kBuildDismantleAsk - see IsFarmKind */
const unsigned char kSpotAnswer = 14;
const unsigned char kSpotRelease = 15;
const unsigned char kSpotFull = 0, kSpotYes = 1, kSpotNoCopy = 2, kSpotRevoked = 3;   /* SPOT_ANSWER's result */
const unsigned int  kFarmMaxKey = 63;
const unsigned int  kFarmMaxPlants = 512;   /* a farm's plant count; a bigger message is refused, never truncated */
const float         kFarmOpMax = 60.0f;     /* one FARM_OP's or MINE_OP's work (operate amount x g_dt; the non-writer sends every 0.5 s) */
const unsigned int  kFarmMaxInputs = 16;    /* a farm's input records (water); a bigger message is refused */
const float         kFarmHiddenAge = -1.0f; /* updatePlantInstance 0xDF320 draws nothing below this (DAT_141683fcc = -1.0) */
const float         kFarmSkillUnknown = -1.0f;
const float         kFarmAgeLimit = 1000.0f;   /* |age| and the state floats: a harvested plant sits at -10 (0xE47C0) */

const int kFarmDecodeOk = 0, kFarmDecodeTooShort = 1, kFarmDecodeBadKind = 2, kFarmDecodeBadKey = 3, kFarmDecodeBadValue = 4;

struct FarmMsg
{
    unsigned char kind;
    std::string key;
    float grown, died, cleared, growStart;
    int harvested, prodState;
    unsigned char hasProgress;
    float progress;
    float marker[3];
    std::vector<float> ages;
    std::vector<float> inStock, inRate;   /* FARM: the input records' water (same length) */
    unsigned int uid;   /* FARM_OP: the worker's uid, 0 = none */
    float skill;        /* FARM_OP: the worker's farming skill, kFarmSkillUnknown = unknown */
    float work;         /* FARM_OP / MINE_OP: operate amount x the sender's g_dt */
    float amount;       /* MINE_OP: the operate amount the worker's call carried (one engine call's worth) */
    unsigned char spot; /* SPOT_ANSWER: kSpotFull / kSpotYes / kSpotNoCopy */
    FarmMsg() : kind(0), grown(0.0f), died(0.0f), cleared(0.0f), growStart(0.0f), harvested(0), prodState(0), hasProgress(0),
                progress(0.0f), uid(0), skill(kFarmSkillUnknown), work(0.0f), amount(0.0f), spot(0) { marker[0] = marker[1] = marker[2] = 0.0f; }
};

const float         kMineAmountMax = 1000.0f;   /* one MINE_OP's per-call operate amount */

inline bool FarmFloatOk(float v, float lim) { return v == v && v > -lim && v < lim; }

inline bool FarmEncodable(const FarmMsg& m)
{
    if (m.key.empty() || m.key.size() > kFarmMaxKey) return false;
    if (m.kind == kSpotAsk || m.kind == kSpotRelease) return m.uid != 0;   /* a spot always names its worker */
    if (m.kind == kSpotAnswer) return m.uid != 0 && m.spot <= kSpotRevoked;
    if (m.kind == kFarmOp)
        return m.work == m.work && m.work > 0.0f && m.work <= kFarmOpMax && FarmFloatOk(m.skill, 1.0e4f) && m.skill >= kFarmSkillUnknown;
    if (m.kind == kMineState)   /* the state is one the game writes (0, 1 or 2); the bar as a FARM's is checked (finite, below kFarmAgeLimit), never below 0 */
        return m.prodState >= 0 && m.prodState <= 2 && FarmFloatOk(m.progress, kFarmAgeLimit) && m.progress >= 0.0f;
    if (m.kind == kMineOp)
        return m.work == m.work && m.work > 0.0f && m.work <= kFarmOpMax && m.amount == m.amount && m.amount > 0.0f && m.amount <= kMineAmountMax;
    if (m.kind != kFarmState) return false;
    if (m.ages.size() > kFarmMaxPlants || m.hasProgress > 1) return false;
    if (m.harvested < 0 || m.harvested > (int)m.ages.size() || m.prodState < 0 || m.prodState > 16) return false;
    if (!FarmFloatOk(m.grown, kFarmAgeLimit) || !FarmFloatOk(m.died, kFarmAgeLimit) || !FarmFloatOk(m.cleared, kFarmAgeLimit)
        || !FarmFloatOk(m.growStart, kFarmAgeLimit) || !FarmFloatOk(m.progress, kFarmAgeLimit)) return false;
    for (int i = 0; i < 3; ++i) if (!FarmFloatOk(m.marker[i], 1.0e9f)) return false;
    for (size_t i = 0; i < m.ages.size(); ++i) if (!FarmFloatOk(m.ages[i], kFarmAgeLimit)) return false;
    if (m.inStock.size() > kFarmMaxInputs || m.inRate.size() != m.inStock.size()) return false;
    for (size_t i = 0; i < m.inStock.size(); ++i) if (!FarmFloatOk(m.inStock[i], 1.0e7f) || !FarmFloatOk(m.inRate[i], 1.0e7f)) return false;
    return true;
}

inline void FarmPutF(std::vector<char>* b, float v) { char t[4]; std::memcpy(t, &v, 4); b->insert(b->end(), t, t + 4); }
inline void FarmPutI(std::vector<char>* b, int v) { char t[4]; std::memcpy(t, &v, 4); b->insert(b->end(), t, t + 4); }

/* Appends to *b. False (and *b unchanged) when m is not encodable. */
inline bool EncodeFarm(std::vector<char>* b, const FarmMsg& m)
{
    if (b == 0 || !FarmEncodable(m)) return false;
    b->push_back((char)m.kind);
    b->push_back((char)(unsigned char)m.key.size());
    b->insert(b->end(), m.key.begin(), m.key.end());
    if (m.kind == kFarmOp) { FarmPutI(b, (int)m.uid); FarmPutF(b, m.skill); FarmPutF(b, m.work); return true; }
    if (m.kind == kMineOp) { FarmPutI(b, (int)m.uid); FarmPutF(b, m.amount); FarmPutF(b, m.work); return true; }
    if (m.kind == kMineState) { FarmPutI(b, m.prodState); FarmPutF(b, m.progress); return true; }
    if (m.kind == kSpotAsk || m.kind == kSpotRelease) { FarmPutI(b, (int)m.uid); return true; }
    if (m.kind == kSpotAnswer) { FarmPutI(b, (int)m.uid); b->push_back((char)m.spot); return true; }
    FarmPutF(b, m.grown); FarmPutF(b, m.died); FarmPutF(b, m.cleared); FarmPutF(b, m.growStart);
    FarmPutI(b, m.harvested); FarmPutI(b, m.prodState);
    b->push_back((char)m.hasProgress);
    FarmPutF(b, m.progress);
    for (int i = 0; i < 3; ++i) FarmPutF(b, m.marker[i]);
    const unsigned int n = (unsigned int)m.ages.size();
    b->push_back((char)(n & 0xFF)); b->push_back((char)((n >> 8) & 0xFF));
    for (size_t i = 0; i < m.ages.size(); ++i) FarmPutF(b, m.ages[i]);
    b->push_back((char)(unsigned char)m.inStock.size());
    for (size_t i = 0; i < m.inStock.size(); ++i) { FarmPutF(b, m.inStock[i]); FarmPutF(b, m.inRate[i]); }
    return true;
}

/* 1 when the first byte names a kind this file decodes (FARM, FARM_OP, MINE_OP, MINE_STATE, the three SPOT kinds) - the session's
   peek before DecodeBuild. The ONE answer to "is this MSG_BUILD kind a farm kind": both tables share MSG_BUILD's kind byte, so a
   number here must never be one buildwire.h's DecodeBuild takes (T-609; the offline test t609_build_and_farm_kinds_never_share_a_number
   walks every kind byte through both). */
inline int IsFarmKind(const char* p, size_t size)
{
    if (p == 0 || size < 1) return 0;
    const unsigned char k = (unsigned char)p[0];
    return (k == kFarmState || k == kFarmOp || k == kMineOp || k == kMineState || k == kSpotAsk || k == kSpotAnswer || k == kSpotRelease) ? 1 : 0;
}

inline int DecodeFarm(const char* p, size_t size, FarmMsg* out)
{
    if (p == 0 || size < 2) return kFarmDecodeTooShort;
    FarmMsg m;
    m.kind = (unsigned char)p[0];
    if (IsFarmKind(p, size) == 0) return kFarmDecodeBadKind;
    const unsigned int kl = (unsigned char)p[1];
    size_t off = 2;
    if (kl == 0 || kl > kFarmMaxKey) return kFarmDecodeBadKey;
    if (size - off < kl) return kFarmDecodeTooShort;
    m.key.assign(p + off, kl); off += kl;
    if (m.kind == kFarmOp)
    {
        if (size - off != 12) return (size - off < 12) ? kFarmDecodeTooShort : kFarmDecodeBadValue;
        std::memcpy(&m.uid, p + off, 4); std::memcpy(&m.skill, p + off + 4, 4); std::memcpy(&m.work, p + off + 8, 4);
        if (!FarmEncodable(m)) return kFarmDecodeBadValue;
        if (out) *out = m;
        return kFarmDecodeOk;
    }
    if (m.kind == kMineOp)
    {
        if (size - off != 12) return (size - off < 12) ? kFarmDecodeTooShort : kFarmDecodeBadValue;
        std::memcpy(&m.uid, p + off, 4); std::memcpy(&m.amount, p + off + 4, 4); std::memcpy(&m.work, p + off + 8, 4);
        if (!FarmEncodable(m)) return kFarmDecodeBadValue;
        if (out) *out = m;
        return kFarmDecodeOk;
    }
    if (m.kind == kSpotAsk || m.kind == kSpotRelease || m.kind == kSpotAnswer)
    {
        const size_t want = (m.kind == kSpotAnswer) ? 5 : 4;
        if (size - off != want) return (size - off < want) ? kFarmDecodeTooShort : kFarmDecodeBadValue;
        std::memcpy(&m.uid, p + off, 4);
        if (m.kind == kSpotAnswer) m.spot = (unsigned char)p[off + 4];
        if (!FarmEncodable(m)) return kFarmDecodeBadValue;
        if (out) *out = m;
        return kFarmDecodeOk;
    }
    if (m.kind == kMineState)
    {
        if (size - off != 8) return (size - off < 8) ? kFarmDecodeTooShort : kFarmDecodeBadValue;
        std::memcpy(&m.prodState, p + off, 4); std::memcpy(&m.progress, p + off + 4, 4);
        if (!FarmEncodable(m)) return kFarmDecodeBadValue;
        if (out) *out = m;
        return kFarmDecodeOk;
    }
    if (size - off < (size_t)(4 * 4 + 4 + 4 + 1 + 4 + 12 + 2)) return kFarmDecodeTooShort;
    std::memcpy(&m.grown, p + off, 4); off += 4;
    std::memcpy(&m.died, p + off, 4); off += 4;
    std::memcpy(&m.cleared, p + off, 4); off += 4;
    std::memcpy(&m.growStart, p + off, 4); off += 4;
    std::memcpy(&m.harvested, p + off, 4); off += 4;
    std::memcpy(&m.prodState, p + off, 4); off += 4;
    m.hasProgress = (unsigned char)p[off]; off += 1;
    std::memcpy(&m.progress, p + off, 4); off += 4;
    for (int i = 0; i < 3; ++i) { std::memcpy(&m.marker[i], p + off, 4); off += 4; }
    const unsigned int n = (unsigned int)(unsigned char)p[off] | ((unsigned int)(unsigned char)p[off + 1] << 8); off += 2;
    if (n > kFarmMaxPlants) return kFarmDecodeBadValue;
    if (size - off < (size_t)(4 * n + 1)) return kFarmDecodeTooShort;
    m.ages.resize(n);
    for (unsigned int i = 0; i < n; ++i) { std::memcpy(&m.ages[i], p + off, 4); off += 4; }
    const unsigned int ni = (unsigned char)p[off]; off += 1;
    if (ni > kFarmMaxInputs) return kFarmDecodeBadValue;
    if (size - off != (size_t)(8 * ni)) return (size - off < (size_t)(8 * ni)) ? kFarmDecodeTooShort : kFarmDecodeBadValue;
    m.inStock.resize(ni); m.inRate.resize(ni);
    for (unsigned int i = 0; i < ni; ++i) { std::memcpy(&m.inStock[i], p + off, 4); std::memcpy(&m.inRate[i], p + off + 4, 4); off += 8; }
    if (!FarmEncodable(m)) return kFarmDecodeBadValue;
    if (out) *out = m;
    return kFarmDecodeOk;
}

/* ---- the decisions (pure; farm.cpp asks exactly these) ---- */

/* WHO GROWS AND WHO HARVESTS. `verdict`: 0 not decided yet, 1 this game writes, 2 another game writes.
   A lone game and a game with nobody else in the world run the engine as before. With another player in the world only the
   named writer's game runs it (verdict 1): an undecided farm is HELD on every game until FarmTick names its writer (T-853 #7: for at most
   kFarmUndecidedMs, then the area's holder writes it - FarmFallbackWriter) - each game
   running its own copy meanwhile grew and harvested it twice (H3). par16 fold #8: verdict 3 = the farm table is FULL and this
   farm has no row - it is HELD too (the cost is that nobody grows a farm past the table, and the table logs full). */
const int kFarmRun = 0, kFarmHold = 1;
const int kFarmVerdictFull = 3;
inline int FarmGate(int single, int linked, int verdict)
{
    if (single != 0 || linked == 0) return kFarmRun;
    return (verdict == 1) ? kFarmRun : kFarmHold;
}
/* THE ENGINE'S LOAD CATCH-UP (farm.cpp detour_timeskip / FmHoldTs: timeSkip, run inside the farm's load for the hours since the
   area save). In a multiplayer world it is kept in the farm's row whatever the writer verdict, until the farm's area has all its
   records here and a stored crop for it is laid (each lay re-times it: FarmCatchUpAtLay); then it runs once where this game writes the
   farm and is dropped where another game does (that game's record carries the growth). A single-player game runs it as the engine
   does; a farm with no row (haveRow 0) runs it now. kFarmTsHoldUndecided: held, no writer named (tsPend 1); kFarmTsHoldWriter: held,
   this game writes it (tsPend 2: due at the drain once nothing is owed); kFarmTsDrop: another game writes it */
const int kFarmTsRun = 0, kFarmTsHoldUndecided = 1, kFarmTsHoldWriter = 2, kFarmTsDrop = 3;
inline int FarmTsHold(int single, int haveRow, int verdict)
{
    if (single != 0 || haveRow == 0) return kFarmTsRun;
    if (verdict == 0) return kFarmTsHoldUndecided;
    return (verdict == 1) ? kFarmTsHoldWriter : kFarmTsDrop;
}
/* A CROP LAID ON A FARM WHOSE LOAD CATCH-UP IS HELD (farm.cpp FarmRecordLay, at every lay): the held catch-up always counts from the
   minute of the newest crop laid here - kFarmCuRetime: to the hours since that crop's minute (catchUp 1, piecerec::PieceFarmCatchUp);
   kFarmCuDrop: none is owed from it (its minute not known, not older than this game's clock, or within the engine's 1-hour floor);
   kFarmCuNone: nothing is held */
const int kFarmCuNone = 0, kFarmCuRetime = 1, kFarmCuDrop = 2;
inline int FarmCatchUpAtLay(int held, int catchUp)
{
    if (held == 0) return kFarmCuNone;
    return (catchUp != 0) ? kFarmCuRetime : kFarmCuDrop;
}
/* THE LOAD CATCH-UP'S FARM (farm.cpp FmLooseKeep / FmLooseFindLocked). Inside the farm's load, where the engine runs the catch-up, the
   farm reads a height of 0, so the key read there is not the key its row, its piece record and its lay carry (they end in its real
   height). The catch-up is kept with the farm's object and that key; a later read of the farm takes it when it is the SAME object
   AND its key names the same record in the same sector within poskey::kTwinDrift tenths on x and on z (poskey::SameSpotSameRecord,
   the match every key search uses). The height is not compared: it is the part the load misreads. The object tells two farms
   stacked at one x, z apart; the spot refuses an object whose memory another farm has taken since. 1 the same farm, 0 not. */
struct FarmTsSpot { char sid[64]; int sx, sy; long x10, z10; int ok; };   /* a key's record and spot, read once (ok 0 = not a key) */
inline int FarmTsSpotOf(const char* key, FarmTsSpot* sp)
{
    memset(sp, 0, sizeof(*sp));
    sp->ok = poskey::ParseKeySpot(key, sp->sid, (int)sizeof(sp->sid), &sp->sx, &sp->sy, &sp->x10, &sp->z10);
    return sp->ok;
}
inline int FarmTsSameFarmAt(const void* heldObj, const FarmTsSpot& held, const void* liveObj, const FarmTsSpot& live)
{
    if (heldObj == 0 || heldObj != liveObj || held.ok == 0 || live.ok == 0) return 0;
    return poskey::SameSpotSameRecord(live.sid, live.sx, live.sy, live.x10, live.z10, held.sid, held.sx, held.sy, held.x10, held.z10,
                                      poskey::kTwinDrift);
}
inline int FarmTsSameFarm(const void* heldObj, const char* heldKey, const void* liveObj, const char* liveKey)
{
    FarmTsSpot h, l;
    FarmTsSpotOf(heldKey, &h);
    FarmTsSpotOf(liveKey, &l);
    return FarmTsSameFarmAt(heldObj, h, liveObj, l);
}
/* KEEPING A LOAD CATCH-UP LOOSE (farm.cpp FmLooseKeep). sameObjWaiting: an entry for the same object still waits - that object's
   memory now holds a farm being loaded, so the farm that entry was for is gone (freed before any read took it) and the newer one
   replaces it (the replaced one counted dropped); freeSlot: the list has room. */
const int kFarmLooseReplace = 1, kFarmLooseNew = 2, kFarmLooseListFull = 3;
inline int FarmLooseOnKeep(int sameObjWaiting, int freeSlot)
{
    if (sameObjWaiting != 0) return kFarmLooseReplace;
    return (freeSlot != 0) ? kFarmLooseNew : kFarmLooseListFull;
}
/* A READ OF THE FARM WITH ITS REAL KEY (farm.cpp FmLooseAdopt, FarmRecordLay). matched: a waiting entry is this farm's
   (FarmTsSameFarmAt); haveRow: the farm has a row; tableFull: the read could make no row (the farm table is full - the farm is held,
   nobody grows it here). kFarmLooseAdopt: the row takes it; kFarmLooseHeldFull: dropped as a farm past a full table is;
   kFarmLooseWait: it waits for the read that has a row. */
const int kFarmLooseWait = 0, kFarmLooseAdopt = 1, kFarmLooseHeldFull = 2;
inline int FarmLooseOnRead(int matched, int haveRow, int tableFull)
{
    if (matched == 0) return kFarmLooseWait;
    if (haveRow != 0) return kFarmLooseAdopt;
    return (tableFull != 0) ? kFarmLooseHeldFull : kFarmLooseWait;
}
/* TEST-ONLY `buildtest farm water <key> <amount>`: the amount as the farm's water store holds it - input record 0's stock, 0 (dry)
   to 1 (full; the engine's rain fills it and caps it at 1). Plain digits with an optional '.' and fraction, no sign, no exponent.
   1 = *out holds it; 0 = not such a number in [0, 1] */
inline int FarmWaterArg(const char* s, float* out)
{
    if (s == 0 || out == 0) return 0;
    double v = 0.0, scale = 1.0;
    int digits = 0, frac = 0;
    const char* p = s;
    for (; *p >= '0' && *p <= '9'; ++p) { if (++digits > 6) return 0; v = v * 10.0 + (double)(*p - '0'); }
    if (*p == '.')
        for (++p; *p >= '0' && *p <= '9'; ++p) { if (++frac > 6) return 0; scale *= 10.0; v = v * 10.0 + (double)(*p - '0'); }
    if (*p != 0 || digits + frac == 0) return 0;
    v /= scale;
    if (!(v >= 0.0 && v <= 1.0)) return 0;
    *out = (float)v;
    return 1;
}

/* T-853 #7. A FARM THE LADDER LEAVES UNDECIDED is held on every game (FarmGate) for at most kFarmUndecidedMs; past that the game
   that holds the farm's area (the relay map's holder of its sector - one map, so one game on every game's reading) grows it and
   the others take its FARM. undecidedMs = how long the ladder has answered "no answer" (0 = it answers); byOwner = the owner rung
   decided it (a player's farm stays held: its owner's game may be writing it); areaSlot = the area holder's slot when it is another
   game (-1 not, or not known); areaHere = this game holds the area. kFarmFallNone: as the ladder says; kFarmFallHere: this game
   writes it; kFarmFallOther: the area holder (areaSlot) writes it. */
const unsigned int kFarmUndecidedMs = 15000;
const int kFarmFallNone = 0, kFarmFallHere = 1, kFarmFallOther = 2;
inline int FarmFallbackWriter(unsigned int undecidedMs, int byOwner, int areaSlot, int areaHere)
{
    if (undecidedMs <= kFarmUndecidedMs || byOwner != 0) return kFarmFallNone;
    if (areaHere != 0) return kFarmFallHere;
    return (areaSlot >= 0) ? kFarmFallOther : kFarmFallNone;
}

/* M3. WHOSE FARM STATE IS TAKEN: a FARM is written onto this game's copy only when it came from the game this game names the
   farm's writer. writerSlot = that game's slot, senderSlot = the sender's; either below 0 = not known now (wait - the message
   is kept, as an undecided farm's is). With three or more players a third game's FARM was taken as readily as the writer's. */
const int kFarmFromWait = -1, kFarmFromRefuse = 0, kFarmFromWriter = 1;
inline int FarmFromWriter(int writerSlot, int senderSlot)
{
    if (writerSlot < 0 || senderSlot < 0) return kFarmFromWait;
    return (writerSlot == senderSlot) ? kFarmFromWriter : kFarmFromRefuse;
}

/* THE PUBLISH RATE. A snapshot of the fields the FARM carries. 0 = nothing to send, 1 = changed, 2 = keep-alive.
   Never more often than kFarmMinGapMs per farm; a change is any discrete field, a plant count, or a float moved by its step;
   with nothing changed the whole state is re-sent every kFarmKeepMs (a copy that missed nothing converges anyway). */
const unsigned int kFarmMinGapMs = 2000, kFarmKeepMs = 30000;
const float kFarmAgeStep = 0.01f, kFarmBarStep = 0.05f, kFarmWaterStep = 0.02f;
inline float FarmAbs(float v) { return v < 0.0f ? -v : v; }
inline int FarmPublishDue(const FarmMsg& last, const FarmMsg& cur, int everSent, unsigned int msSince)
{
    if (everSent == 0) return 1;
    if (msSince < kFarmMinGapMs) return 0;
    int changed = 0;
    if (last.harvested != cur.harvested || last.prodState != cur.prodState || last.hasProgress != cur.hasProgress
        || last.ages.size() != cur.ages.size() || last.inStock.size() != cur.inStock.size()) changed = 1;
    if (FarmAbs(last.grown - cur.grown) >= kFarmAgeStep || FarmAbs(last.died - cur.died) >= kFarmAgeStep
        || FarmAbs(last.growStart - cur.growStart) >= kFarmAgeStep || FarmAbs(last.cleared - cur.cleared) >= kFarmBarStep
        || FarmAbs(last.progress - cur.progress) >= kFarmBarStep) changed = 1;
    for (size_t i = 0; changed == 0 && i < cur.ages.size() && i < last.ages.size(); ++i)
        if (FarmAbs(last.ages[i] - cur.ages[i]) >= kFarmAgeStep) changed = 1;
    for (size_t i = 0; changed == 0 && i < cur.inStock.size() && i < last.inStock.size() && i < cur.inRate.size() && i < last.inRate.size(); ++i)
        if (FarmAbs(last.inStock[i] - cur.inStock[i]) >= kFarmWaterStep || last.inRate[i] != cur.inRate[i]) changed = 1;
    if (changed) return 1;
    return (msSince >= kFarmKeepMs) ? 2 : 0;
}

/* A PLAYER'S FARM: ITS CROP TRAVELS ON ITS PIECE RECORD ALONE (piecerec.h kPrLiveFlagFarm, written at each 1% of growth and of death
   and at each harvest, replant and the crop starting to die). Its crop is growth (grown), death (died), plant start (growStart), plants
   harvested (harvested) and every plant's age (the farm builds the ages from those: keptwire.h KeptFarmAges). For a player's farm
   (playerFarm 1) a crop change is never a reason to send a FARM and there is no keep-alive: a FARM goes only when what is left moves -
   the cleared value, the work state, the harvest bar, the work spot or the water - at most every kFarmMinGapMs, as before. The FARM's
   layout is unchanged, so its crop fields still hold the writer's numbers, but a game never writes them onto a player's farm
   (FarmApplyCrop). playerFarm 0 (a farm no player owns): FarmPublishDue, crop and keep-alive as before. */
inline void FarmCropFrom(FarmMsg* to, const FarmMsg& from)
{
    to->grown = from.grown; to->died = from.died; to->growStart = from.growStart; to->harvested = from.harvested; to->ages = from.ages;
}
inline int FarmPublishDueOf(const FarmMsg& last, const FarmMsg& cur, int everSent, unsigned int msSince, int playerFarm)
{
    if (playerFarm == 0) return FarmPublishDue(last, cur, everSent, msSince);
    FarmMsg c = cur;
    if (everSent != 0) FarmCropFrom(&c, last);   /* the crop as last sent: its moves are not measured */
    const int due = FarmPublishDue(last, c, everSent, msSince);
    return (due == 2) ? 0 : due;
}
/* 1 = a FARM's crop (growth, death, plant start, plants harvested, every plant's age) is written onto this game's copy of the farm: a
   farm no player owns; 0 = a player's farm - its crop comes from its piece record alone, the rest of the FARM is written */
inline int FarmApplyCrop(int playerFarm) { return (playerFarm != 0) ? 0 : 1; }

/* HOW MANY PLANTS A FARM MESSAGE MAY WRITE on a copy that has `local` plants. -1 = wait (the copy's plants are not made yet);
   a count that differs is written up to the smaller count and booked as a mismatch by the caller. */
inline int FarmApplyCount(int local, int sent)
{
    if (local <= 0) return -1;
    return (sent < local) ? sent : local;
}

/* THE WORK a non-writer accumulates: clamped to [0, kFarmOpMax] per message, the rest kept for the next one. */
inline float FarmOpTake(float* pending)
{
    if (pending == 0 || !(*pending > 0.0f)) { if (pending) *pending = 0.0f; return 0.0f; }
    const float t = (*pending > kFarmOpMax) ? kFarmOpMax : *pending;
    *pending -= t;
    return t;
}

/* par16 fold #2. ONE OPERATE CALL on the writer: the work it may take from `left` so that the call's operate amount is at
   most `chunkAmount` (one call ends at most one crop), at the writer's own g_dt `dt`. 0 = none (dt 0: the game is paused -
   the work is HELD, never divided by a zero). *amount = the operate amount for that work (work / dt). */
inline float FarmWorkSlice(float left, float dt, float chunkAmount, float* amount)
{
    if (amount) *amount = 0.0f;
    if (!(left > 0.0f) || !(dt > 0.0f) || !(chunkAmount > 0.0f)) return 0.0f;
    const float cap = chunkAmount * dt;
    const float w = (left > cap) ? cap : left;
    if (amount) *amount = w / dt;
    return w;
}

/* THE PRODUCTION WRITER, three ways (items.cpp ProdStepWriterHere): 1 this game writes the building, 2 another game does,
   0 no answer yet (the area map or the owner cannot be read now). */
const int kProdWriterNone = 0, kProdWriterHere = 1, kProdWriterOther = 2;

/* A WORKER'S STEP ON A PRODUCTION BUILDING (ProductionBuilding::operate with a worker): where it runs. single = a lone game;
   linked = another player is in this world; writer = the three-way answer above. kMineRun: the engine's own step here.
   kMineRelay: not here - the work is kept and goes to the writer as MINE_OP, and while nobody is named writer it waits (a
   second game running it would put a second count of ore into its own copy of the box). */
const int kMineRun = 0, kMineRelay = 1;
inline int MineStepRoute(int single, int linked, int writer)
{
    if (single != 0 || linked == 0 || writer == kProdWriterHere) return kMineRun;
    return kMineRelay;
}

/* T-610: A WORKER THAT IS A COPY of another game's character (whoCopy): its step is neither run nor relayed on this game, on any
   linked game - the writer included. Its own game runs its job and runs or relays that work (MineStepRoute, MINE_OP); a second
   count from the copy was the work counted twice (T1321: B's character's copy on A ran one operate step on A while B relayed the
   same work). 1 = skip the step. */
inline int MineWorkerSkip(int single, int whoCopy)
{
    return (single == 0 && whoCopy != 0) ? 1 : 0;
}

/* T-610: THE COPY TEST of a worker, from any thread: a replicated character (uid != 0) that the owned mirror does not list
   (mine == 0), while that mirror is exact (mirrorExact != 0). A "not mine" from an inexact mirror is no proof, so the worker is
   then treated as this game's own - the fail-safe of items.cpp ItCcIsCopy (kit3 review D8). 1 = a copy. */
inline int WorkerIsCopy(unsigned int uid, int mine, int mirrorExact)
{
    return (uid != 0 && mine == 0 && mirrorExact != 0) ? 1 : 0;
}

/* T-610: THE TAKE STEP for an unlisted worker that is a copy of another game's character (whoCopy): refused on every linked game.
   Its own game asks the runner for its spot (SPOT_ASK); the runner takes it with its copy through the engine's own take, which
   does not pass the take detour (farm.cpp SpTakeRun), so that copy is then listed. 1 = refuse. */
inline int SpotCopyRefused(int single, int linked, int listed, int whoCopy)
{
    return (single == 0 && linked != 0 && listed == 0 && whoCopy != 0) ? 1 : 0;
}

/* MINE_OP work on this game (received, or kept by the worker's own game): run it (this game writes the building), wait (no
   writer named yet - held up to kFmPendMs, as FARM_OP), or refuse / send on (another game writes it). */
const int kMineAccRefuse = -1, kMineAccWait = 0, kMineAccRun = 1;
inline int MineOpAccept(int writer)
{
    if (writer == kProdWriterHere) return kMineAccRun;
    if (writer == kProdWriterOther) return kMineAccRefuse;
    return kMineAccWait;
}

/* T-1192: THE WORK STEPS THE GATE COVERS, by the function a building's work slot (vtable +0x4B0) leads to (build/vtable_*.txt,
   1.0.65; 1.0.68 +0x40): ProductionBuilding::operate 0x2986C0 (production machines, generators, rain collectors, torture
   buildings), the crafting bench's step 0x2B5A00 (CraftingBuilding) and the furnace's step 0x299F60 (FurnaceBuilding). The three
   take (building, worker, amount) and are gated alike: a worker's step runs on the building's writer and is sent there as MINE_OP
   from any other game; a step with no worker runs on the holder only. MINE_OP work is run through the step of the building's own
   class. 0 (kStepNone) = none of them - a farm's own step, an address not known, a zero slot. */
const int kStepNone = 0, kStepProduction = 1, kStepCrafting = 2, kStepFurnace = 3;
inline int WorkStepKind(unsigned long long slotFn, unsigned long long prodFn, unsigned long long craftFn, unsigned long long furnaceFn)
{
    if (slotFn == 0) return kStepNone;
    if (slotFn == prodFn) return kStepProduction;
    if (slotFn == craftFn) return kStepCrafting;
    if (slotFn == furnaceFn) return kStepFurnace;
    return kStepNone;
}

/* T-1192: THE TORTURE BUILDING'S OUTPUT. Its update 0x29AF50 calls the output step 0x298470 (its vtable +0x5C0) itself when its
   occupant is done, outside the gated work step, so every game that runs the update would make the product. The gate of an update
   (single = a lone game; writer = the three-way answer above): kTortRun on a lone game or the writer - the output is made there;
   kTortSkip on a game another game writes the building for - an output call made inside that update is skipped; kTortOwe when no
   writer is named at that moment - the output is not made, it is OWED on that building (items.cpp TortOwedNote). Re-check fold
   (finding 2): "no writer" is no answer to run on - off the main thread a player-owned building is "no writer" on EVERY game, the
   true writer's included, and on the main thread it is also the hand-back, a stale ownership map or a load hold; running there made
   the product on every game. The output is ONE call on one frame, so it is kept, not skipped: the main thread runs an owed output
   later only when this game is then the building's writer (TortureOwedDecide). */
const int kTortRun = 0, kTortSkip = 1, kTortOwe = 2;
inline int TortureUpdateHeld(int single, int writer)
{
    if (single != 0 || writer == kProdWriterHere) return kTortRun;
    return (writer == kProdWriterOther) ? kTortSkip : kTortOwe;
}
/* An owed output, asked on the main thread: stillThere = the building is still the one its key names (gone, or the world unloaded:
   dropped); single / writer = the gate's answer NOW. Run on a lone game or the writer; dropped when another game writes it (it runs
   its own owed one); kept while nobody is named, for kTortOwedKeepMs at most (bounded: kTortOwedCap rows). */
const int kTortOwedKeep = 0, kTortOwedRun = 1, kTortOwedDrop = 2;
const unsigned int kTortOwedCap = 8;
const unsigned long kTortOwedKeepMs = 60000;
inline int TortureOwedDecide(int stillThere, int single, int writer, unsigned long ageMs)
{
    if (stillThere == 0) return kTortOwedDrop;
    if (single != 0 || writer == kProdWriterHere) return kTortOwedRun;
    if (writer == kProdWriterOther) return kTortOwedDrop;
    return (ageMs < kTortOwedKeepMs) ? kTortOwedKeep : kTortOwedDrop;
}
inline int TortureOutputSkip(const void* heldBuilding, const void* building)
{
    return (heldBuilding != 0 && heldBuilding == building) ? 1 : 0;
}

/* T-1192 (e): THE TORTURE BUILDING'S CHANGES TO ITS OCCUPANT. The same update 0x29AF50 changes the character in it: each limb's
   health (LimbApplyHurt 0x643FE0, two calls), its medical state - unconscious, the fall, dead - (MedicalSystem_evaluate 0x648890),
   a limb cut off (MedicalSystem::amputate 0x64E330, already refused on a copy everywhere - clothing.cpp) and its worn armour taken
   off one piece at a time (CharacterHuman::dropItem 0x5C9CB0 - a copy's drop of its own item is already refused, items.cpp
   GrCharDrop - then GameWorld::destroy 0x798F50). Those calls run only on the game that owns the occupant; the building's own
   state (its timer, flags, bar and output) is the update's as before. The call is known by its return address inside the update
   (a TortureSite); occupantCopy = the occupant is a replicated character this game does not own (items.cpp ItCcIsCopy, the
   test the drop refusal uses); itemStillHeld = the item the destroy is given is still listed in the occupant's own inventory,
   which is exactly when that drop was refused - an item the drop let go is on the ground and the destroy runs as the engine's. */
const int kTortSiteNone = 0, kTortSiteHurt = 1, kTortSiteEvaluate = 2, kTortSiteDestroy = 3;
inline int TortureSite(unsigned long long retRva, unsigned long long hurt1, unsigned long long hurt2, unsigned long long evaluate,
                       unsigned long long destroy)
{
    if (retRva == 0) return kTortSiteNone;
    if (retRva == hurt1 || retRva == hurt2) return kTortSiteHurt;
    if (retRva == evaluate) return kTortSiteEvaluate;
    if (retRva == destroy) return kTortSiteDestroy;
    return kTortSiteNone;
}
inline int TortureOccupantSkip(int site, int occupantCopy, int itemStillHeld)
{
    if (site == kTortSiteNone || occupantCopy == 0) return 0;
    if (site == kTortSiteDestroy) return (itemStillHeld != 0) ? 1 : 0;
    return 1;
}

/* Kept work whose writer has stayed unnamed `undecidedMs` is dropped (0 = not waiting). */
const unsigned int kMinePendMs = 60000;
inline int MineHoldExpired(unsigned int undecidedMs) { return (undecidedMs > kMinePendMs) ? 1 : 0; }

/* MINE_STATE ON THE WRITER: whether a node's bar and state go out now. msSinceWorked = since a worker's step last ran on it
   here; everSent / msSinceSent / lastProgress / lastState = its last send; finalSent = the send that closed its last spell of
   work. Sent while it was worked in the last kMineStateWorkedMs, at most once per kMineStateGapMs, on a move of kFarmBarStep or
   a state change (the first one at once); once the work stops, one last send with what it holds then. */
const unsigned int kMineStateWorkedMs = 5000, kMineStateGapMs = 1000;
const int kMineStateNone = 0, kMineStateSend = 1, kMineStateFinal = 2;
inline int MineStatePublishDue(unsigned int msSinceWorked, int everSent, unsigned int msSinceSent, float lastProgress, int lastState,
                               float progress, int state, int finalSent)
{
    if (everSent != 0 && msSinceSent < kMineStateGapMs) return kMineStateNone;
    if (msSinceWorked > kMineStateWorkedMs) return (everSent != 0 && finalSent == 0) ? kMineStateFinal : kMineStateNone;
    if (everSent == 0) return kMineStateSend;
    if (state != lastState || FarmAbs(progress - lastProgress) >= kFarmBarStep) return kMineStateSend;
    return kMineStateNone;
}

/* MINE_STATE ON A RECEIVER: written onto this game's copy only while this game's own answer is "another game writes it"
   (writerHere = ProdStepWriterHere). This game writing it, or no writer named, keeps its own bar. 1 apply, 0 refuse. */
inline int MineStateAccept(int writerHere) { return (writerHere == kProdWriterOther) ? 1 : 0; }

/* A [MINE] line of a counted kind is written for the first 5 of that kind, then every 100th (n = the count with this one). */
inline int MineSayDue(long long n) { return (n >= 1 && (n <= 5 || n % 100 == 0)) ? 1 : 0; }

/* ---- T-674: a machine's worker spots, counted by the game that runs it ----
   A row per (building, worker) on the game whose worker it is: kSpotNone nothing asked, kSpotAsked an ask is out,
   kSpotGranted the runner took the spot, kSpotRefused the runner answered full (or has no copy of the worker). */
const int kSpotNone = 0, kSpotAsked = 1, kSpotGranted = 2, kSpotRefused = 3;
/* An ask with no answer this long is asked again: the runner may have left or the answer been lost; an ask is never a fixed
   wait for readiness - the worker's step is refused while it is out, and a later ask replaces it. */
const unsigned int kSpotAskGiveUpMs = 10000;
/* After "full" the worker's step is refused this long before it asks again (a spot may have been freed meanwhile). */
const unsigned int kSpotRetryMs = 5000;

/* THE TAKE STEP (the engine's take vt+0x4F8 on this game, once per work step). single / linked as FarmGate; listed = the worker
   is already in the building's worker set here; writer = the building's runner as last read (kProdWriter*); state / msSince = the
   worker's row and the time since its state last changed. kSpotPass: the engine's own take runs; kSpotWait: refused this step;
   kSpotAskNow: refused this step and an ask goes out. */
const int kSpotPass = 0, kSpotWait = 1, kSpotAskNow = 2;
inline int SpotTakeStep(int single, int linked, int listed, int writer, int state, unsigned int msSince)
{
    if (single != 0 || linked == 0 || listed != 0 || writer == kProdWriterHere || state == kSpotGranted) return kSpotPass;
    if (state == kSpotAsked) return (msSince > kSpotAskGiveUpMs) ? kSpotAskNow : kSpotWait;
    if (state == kSpotRefused) return (msSince > kSpotRetryMs) ? kSpotAskNow : kSpotWait;
    return kSpotAskNow;
}

/* T-1192 (run T1894): A TAKE THAT IS NOT ASKED AGAIN. A prisoner put into a torture building (the put-in order, task 108 -> the
   prison put-in 0x3305C0) calls the take ONCE; a new row's runner was filled only on the next frame (T-912), so that one take was
   always refused and the prisoner left lying with no building. The take now reads the runner in the same call: writerNow = that
   read (kProdWriter*; kProdWriterNone = not read or no answer). A row whose runner is not known takes "this game runs it" at once
   (its take then passes, SpotTakeStep); another game's runner or no answer leaves the row as it was - refused and asked. */
inline int SpotWriterForTake(int rowWriter, int writerNow)
{
    return (rowWriter == kProdWriterNone && writerNow == kProdWriterHere) ? kProdWriterHere : rowWriter;
}
/* Off the main thread the runner ladder has no answer for a player-owned building (its owner is read on the main thread only): the
   main thread's last keyed runner answer for the building is used while it is at most this old (older = no answer). */
const unsigned int kSpotRunnerFreshMs = 1000;
inline int SpotRunnerBooked(int found, int booked, unsigned int ageMs)
{
    return (found != 0 && ageMs <= kSpotRunnerFreshMs) ? booked : kProdWriterNone;
}
/* how the take's runner was read: on the main thread by the ladder; off it by the ladder's off-main road, by the main thread's
   booked answer, with no booked answer, or with one too old */
const int kRunReadMain = 0, kRunReadOffLadder = 1, kRunReadOffBooked = 2, kRunReadOffNone = 3, kRunReadOffStale = 4, kRunReadKinds = 5;

/* ---- T-1125: THE TORTURE BUILDING'S TAKE AND ITS OCCUPANT, SAID (release lines; they change nothing in the game) ----
   The take guard (farm.cpp SpTakeGate) decides a torture building's take on one of these roads: a single-player game, the hand
   unreadable or the null hand, the worker already in the building's worker list, no position key, no room (counted spots), a
   character no other game can name, a copy of another game's character, the row's verdict pass / ask / wait. */
const int kTortTakeLone = 0, kTortTakeHandless = 1, kTortTakeListed = 2, kTortTakeNoKey = 3, kTortTakeRoomFull = 4, kTortTakeNoUid = 5,
          kTortTakeCopy = 6, kTortTakePass = 7, kTortTakeAsk = 8, kTortTakeWait = 9,
          kTortTakePutInCopy = 10, kTortTakePutInOwed = 11, kTortTakePutInFull = 12, kTortTakeKinds = 13;   /* T-1211: the put-in's roads */
/* 1 = the engine's own take runs on this road, 0 = refused */
inline int TortTakeAllowed(int kind)
{
    return (kind == kTortTakeRoomFull || kind == kTortTakeCopy || kind == kTortTakeAsk || kind == kTortTakeWait
            || kind == kTortTakePutInOwed || kind == kTortTakePutInFull) ? 0 : 1;
}
/* T-1211: the line's final answer - allowed, refused, or owed (a put-in the safe-point round finishes or ends) */
inline const char* TortTakeWord(int kind)
{
    if (kind == kTortTakePutInOwed || kind == kTortTakePutInFull) return "owed";
    return (TortTakeAllowed(kind) != 0) ? "allowed" : "refused";
}
inline const char* TortTakeWhy(int kind)
{
    switch (kind)
    {
    case kTortTakeLone:     return "a single-player game - the engine's own take";
    case kTortTakeHandless: return "the hand is unreadable or the null hand - the engine's own take";
    case kTortTakeListed:   return "already in the building's worker list";
    case kTortTakeNoKey:    return "the building has no position key - the engine's own take";
    case kTortTakeRoomFull: return "no room - the list's count plus the spots counted for other games' workers reach the limit";
    case kTortTakeNoUid:    return "a character no other game can name - the engine's own take";
    case kTortTakeCopy:     return "a copy of another game's character - its own game asks the runner";
    case kTortTakePass:     return "this game runs the building, or the runner granted the spot";
    case kTortTakeAsk:      return "another game runs the building or no runner answer - refused, the runner asked";
    case kTortTakeWait:     return "an ask or a full answer is out - refused this step";
    case kTortTakePutInCopy: return "a copy of another game's prisoner at the put-in - passed, its own game is asked to cage it";
    case kTortTakePutInOwed: return "this game's own prisoner refused at the put-in - the put-in is owed (redone once the runner answers, else taken out)";
    case kTortTakePutInFull: return "no room at the put-in (counted spots) - owed as full, the prisoner is taken out";
    default: break;
    }
    return "unknown road";
}
/* A said line's cap: one per (building key hash, occupant uid, kind). 1 = say it (recorded), 0 = said before, -1 = the table is
   full (not said; the caller counts it) */
struct TortSeen { unsigned int keyHash; unsigned int uid; int kind; };
inline int TortSayFirst(TortSeen* rows, int* n, int cap, unsigned int keyHash, unsigned int uid, int kind)
{
    for (int i = 0; i < *n; ++i)
        if (rows[i].keyHash == keyHash && rows[i].uid == uid && rows[i].kind == kind) return 0;
    if (*n >= cap) return -1;
    rows[*n].keyHash = keyHash; rows[*n].uid = uid; rows[*n].kind = kind;
    ++*n;
    return 1;
}
/* a building key's hash for the cap and the occupant watch; never 0 (0 = no building) */
inline unsigned int TortKeyHash(const char* k)
{
    unsigned int h = 2166136261u;
    for (; k != 0 && *k != 0; ++k) { h ^= (unsigned char)*k; h *= 16777619u; }
    return (h == 0) ? 1u : h;
}
/* THE OCCUPANT WATCH (spawn.cpp CageOccupantWatch, main thread, every 500 ms): a character's inSomething (+0x2F8; 2 = a cage,
   which a torture building is) and the building its hand names (key hash, 0 = none resolves), against the last pass. In = it went
   in (or now names another building); Out = it left; Gone = still in but the building it was in no longer resolves (taken down or
   unloaded with it inside). A character first seen inside counts as going in. */
const int kCageEvNone = 0, kCageEvIn = 1, kCageEvOut = 2, kCageEvGone = 3;
inline int CageWatchEvent(int prevIns, unsigned int prevKey, int ins, unsigned int key)
{
    if (prevIns != 2) return (ins == 2) ? kCageEvIn : kCageEvNone;
    if (ins != 2) return kCageEvOut;
    if (prevKey != 0 && key == 0) return kCageEvGone;
    if (key != 0 && key != prevKey) return kCageEvIn;
    return kCageEvNone;
}

/* A granted spot the runner no longer holds: a revoke (T-853 #5), or a full answer to the granted row's keep-alive ask - the runner
   holds nothing for the worker and has no room (its revoke was not sent, or it took over the building without the grant). */
inline int SpotGrantLost(int state, unsigned char result)
{
    return (state == kSpotGranted && (result == kSpotRevoked || result == kSpotFull)) ? 1 : 0;
}

/* SPOT_ANSWER on the asking game: only a row with an ask out takes a yes / full / no copy; a lost grant (SpotGrantLost) sends a
   granted row back to asked (the caller asks again); a revoke changes no other row. The row's new state. */
inline int SpotAnswerApply(int state, unsigned char result)
{
    if (SpotGrantLost(state, result) != 0) return kSpotAsked;
    if (result == kSpotRevoked) return state;
    if (state != kSpotAsked) return state;
    return (result == kSpotYes) ? kSpotGranted : kSpotRefused;
}

/* SPOT_ASK on a game: runsHere = this game runs the building; took = the spot was taken for the worker - the engine's own take with
   this game's copy of it, or (T-853 #4) with no copy loaded a COUNTED spot (SpotRoomLeft). -1 = not this game's to answer (it stays
   silent). kSpotNoCopy is no longer answered: a worker whose copy the runner never shows could never work there. */
inline int SpotAnswerFor(int runsHere, int took)
{
    if (runsHere == 0) return -1;
    return (took != 0) ? kSpotYes : kSpotFull;
}

/* T-853 #4. ROOM ON THE RUNNER with the spots it counts for workers it has no copy of: count = the worker set's count +0x3E0,
   counted = those spots for the building, limit = +0x3AC. 1 = one more worker fits (the engine's own test is count < limit,
   0xF7FF0 / 0xF7BB0). */
inline int SpotRoomLeft(unsigned long long count, int counted, int limit)
{
    if (limit <= 0 || counted < 0) return 0;
    return (count + (unsigned long long)counted < (unsigned long long)limit) ? 1 : 0;
}

/* T-853 #2. A release owed for a row whose runner reads `writer` now: 1 send it (another game runs it), 0 keep it owed (no runner
   named now), -1 drop it (this game runs it now - the old runner frees the spot itself once it no longer runs the building). */
inline int SpotReleaseRoute(int writer)
{
    if (writer == kProdWriterOther) return 1;
    if (writer == kProdWriterHere) return -1;
    return 0;
}

/* T-853 #3. A granted spot the worker never took here (not in the building's set this long after the grant: it walked off before
   the yes came, or left the set by a road the erase detour does not see) is released. */
const unsigned int kSpotGrantUseMs = 10000;
inline int SpotGrantUnused(int state, int listed, unsigned int msSinceGrant)
{
    return (state == kSpotGranted && listed == 0 && msSinceGrant > kSpotGrantUseMs) ? 1 : 0;
}

/* T-1212 (runs T1891 / T1908 / T1910 / T1915): A CLICK ORDER THE TAKE REFUSED IS GIVEN AGAIN. A player's click on a rock, machine,
   bench, furnace, generator, rain collector or torture building starts its job through the job start (1.0.65 0x35B3D0; its take call
   returns to row JobStartTakeRet); a refused take there ENDS the click order (decomp_35b3d0.txt:64-67) - a standing job is restarted
   by the game, a click order is not. The take is refused when the row's runner is not known yet (a new row off the main thread) or
   another game runs the building and its yes has not come. The refusal at the job start leaves a re-order OWED on the worker's row;
   once the real answer is in (this game runs it, or the spot is granted) the main thread gives the same click order again.
   A refusal is owed only at the job start (callerJobStart; a prisoner's put-in is not a click order) and not within
   kSpotReorderLoopMs of this row's last re-give (everGiven / msSinceGiven): a re-given order refused again is never given a third
   time. */
const unsigned int kSpotReorderWindowMs = 3000;            /* a re-give is due only this long after the refusal */
const unsigned int kSpotReorderSettleMs = 250;             /* bug class 319: the ended click order may still sit in the order list */
const unsigned int kSpotReorderLoopMs = kSpotGrantUseMs;   /* a refusal this soon after a re-give is not owed again */
typedef char kSpotReorderBeforeGrantRelease[(kSpotReorderWindowMs < kSpotGrantUseMs) ? 1 : -1];   /* lands before an unused grant is released */
inline int SpotReorderOwe(int callerJobStart, int everGiven, unsigned int msSinceGiven)
{
    if (callerJobStart == 0) return 0;
    return (everGiven != 0 && msSinceGiven <= kSpotReorderLoopMs) ? 0 : 1;
}
/* T-1212 review MEDIUM 1: the NEWEST click wins - a refusal owed on one row clears the owed re-order on every other row of the same
   character (the player replaced that click; giving it first would make the newer one's re-give see it as "a newer order").
   rows[0..n) are the worker rows (fields used, uid, roOwed); keep = the row just owed. Returns the rows cleared. */
template <class Row> inline int SpotReorderKeepNewest(Row* rows, int n, int keep)
{
    int cleared = 0;
    if (keep < 0 || keep >= n) return 0;
    for (int j = 0; j < n; ++j)
        if (j != keep && rows[j].used != 0 && rows[j].roOwed != 0 && rows[j].uid == rows[keep].uid) { rows[j].roOwed = 0; ++cleared; }
    return cleared;
}
/* The answer an owed re-order waits for (the worker's row as the main thread reads it): none yet, this game runs it, granted, or
   (review LOW 2) the runner answered full - the spot will not come inside the window. */
const int kSpotReorderNoAnswer = 0, kSpotReorderRunsHere = 1, kSpotReorderGranted = 2, kSpotReorderFull = 3;
inline int SpotReorderAnswer(int writer, int state)
{
    if (writer == kProdWriterHere) return kSpotReorderRunsHere;
    if (state == kSpotGranted) return kSpotReorderGranted;
    return (state == kSpotRefused) ? kSpotReorderFull : kSpotReorderNoAnswer;
}
/* THE DUE CHECK (main thread, every frame while a re-order is owed). owed = the row holds one; answer = SpotReorderAnswer;
   msSinceRefusal = since the refusal; found = the character (by uid, this game's own) and the building (by key) were found again
   and the character's order lists read; queued / pending / standing = its order queue, pending orders and standing jobs.
   Bug class 495: a player's newer order or a standing job drops the re-order for good (it is never given after that order ends);
   a queued order in the first kSpotReorderSettleMs may be the ended click order itself, so it waits. A runner's full answer drops it.
   A run proving this fix must see the REPORT's givenHere / givenGrant move (an order actually re-given), not only droppedNewer -
   a drop alone shows the guard, not the re-give. */
const int kSpotReorderNone = 0, kSpotReorderWait = 1, kSpotReorderGive = 2, kSpotReorderDropNewer = 3, kSpotReorderDropTimeout = 4,
          kSpotReorderDropNotFound = 5, kSpotReorderDropFull = 6;
inline int SpotReorderDue(int owed, int answer, unsigned int msSinceRefusal, int found, long long queued, int pending, int standing)
{
    if (owed == 0) return kSpotReorderNone;
    if (msSinceRefusal > kSpotReorderWindowMs) return kSpotReorderDropTimeout;
    if (found == 0) return kSpotReorderDropNotFound;
    if (answer == kSpotReorderFull) return kSpotReorderDropFull;
    if (standing != 0) return kSpotReorderDropNewer;
    if (queued != 0 || pending != 0) return (msSinceRefusal < kSpotReorderSettleMs) ? kSpotReorderWait : kSpotReorderDropNewer;
    return (answer == kSpotReorderRunsHere || answer == kSpotReorderGranted) ? kSpotReorderGive : kSpotReorderWait;
}

/* T-1211: A PRISONER PUT INTO A TORTURE BUILDING ANOTHER GAME RUNS. The put-in (Task_PutInSomething 0x358610 -> setPrisonMode
   0x3305C0) sets "in something" (+0x2F8 = 2) and then calls the building's take ONCE (its return is row PrisonTakeRet); a refusal
   just returns, leaving the prisoner lying pinned with no building, and the engine never calls that take again
   (decomp_3305c0:83-92). So the game whose prisoner it is finishes its own put-in once the building's game answers (design D2,
   .modding/investigations/t1211-putin-design-2026-11-04.md): no new message.
   WHO MAY PASS: a copy of another game's prisoner (whoCopy, WorkerIsCopy) passes the take at the put-in (putIn = the take came from
   setPrisonMode) - this game's engine then holds the copy in the building, so its own game is sent the existing "please cage it"
   (MSG_PRISON IN, spawn.cpp PrisonTick). Anywhere else a copy is still refused (T-610). 1 = pass. */
inline int SpotPutInCopyPass(int putIn, int whoCopy)
{
    return (putIn != 0 && whoCopy != 0) ? 1 : 0;
}
/* OWED: this game's own prisoner whose put-in take was refused (act = the take step, kSpot*) is owed one redo; a refusal of the
   redo itself (redoing = the row's one redo is running now) is never owed again - it is taken out instead. 1 = owe. */
inline int SpotPutInOwe(int putIn, int act, int redoing)
{
    return (putIn != 0 && act != kSpotPass && redoing == 0) ? 1 : 0;
}
/* THE HOLD (spawn.cpp PrisonSafePointDrain): the owner caging its own character for another game's guard (ownOp) whose take there
   was refused and is now owed does not send the refusal at once; the owed put-in's outcome sends it (or the caging). 1 = hold. */
inline int SpotPutInHold(int ownOp, int owed)
{
    return (ownOp != 0 && owed != 0) ? 1 : 0;
}
/* THE REDO / GIVE-UP DECISION (the K2 safe point, every frame while a put-in is owed), from live reads: linked = the farm link is up
   (unlinked, the take guard passes, so the redo completes as a lone game); found = the prisoner found again; mine = it is still
   this game's own; pinned = it still lies "in something" with no building (+0x2F8 == 2, no hand); building = the building found
   again by its key; answer = SpotReorderAnswer of the worker row (this game runs it / granted / full / none); asks = the asks
   made for it so far, msSinceAsk = since the last one. *why = kPutInWhy*. Events over timers: the redo waits for the answer;
   the 3-ask limit is only the fallback when no answer ever comes (an unanswered ask is asked again every kSpotAskGiveUpMs). */
const int kSpotPutInWait = 0, kSpotPutInRedo = 1, kSpotPutInAskAgain = 2, kSpotPutInTakeOut = 3, kSpotPutInDrop = 4;
const int kPutInWhyNone = 0, kPutInWhyGranted = 1, kPutInWhyRunsHere = 2, kPutInWhyLinkDown = 3, kPutInWhyFull = 4,
          kPutInWhyBuildingGone = 5, kPutInWhyNoAnswer = 6, kPutInWhyPrisonerGone = 7, kPutInWhyNotMine = 8, kPutInWhyNotPinned = 9;
const int kSpotPutInAsksMax = 3;
inline int SpotPutInDue(int linked, int found, int mine, int pinned, int building, int answer, int asks, unsigned int msSinceAsk, int* why)
{
    *why = kPutInWhyNone;
    if (found == 0) { *why = kPutInWhyPrisonerGone; return kSpotPutInDrop; }
    if (mine == 0) { *why = kPutInWhyNotMine; return kSpotPutInDrop; }       /* another game's to write now */
    if (pinned == 0) { *why = kPutInWhyNotPinned; return kSpotPutInDrop; }   /* caged or out by another road: nothing owed */
    if (building == 0) { *why = kPutInWhyBuildingGone; return kSpotPutInTakeOut; }
    if (linked == 0) { *why = kPutInWhyLinkDown; return kSpotPutInRedo; }
    if (answer == kSpotReorderRunsHere) { *why = kPutInWhyRunsHere; return kSpotPutInRedo; }
    if (answer == kSpotReorderGranted) { *why = kPutInWhyGranted; return kSpotPutInRedo; }
    if (answer == kSpotReorderFull) { *why = kPutInWhyFull; return kSpotPutInTakeOut; }
    if (msSinceAsk <= kSpotAskGiveUpMs) return kSpotPutInWait;
    if (asks >= kSpotPutInAsksMax) { *why = kPutInWhyNoAnswer; return kSpotPutInTakeOut; }
    return kSpotPutInAskAgain;
}
/* review fold 1: the answer an owed put-in judges - a put-in this game refused because the building's spots are full (the counted
   spots it holds for other games' workers; roomFull) is "full" whoever runs it, so it is taken out at once instead of lying pinned. */
inline int SpotPutInAnswer(int roomFull, int writer, int state)
{
    return (roomFull != 0) ? kSpotReorderFull : SpotReorderAnswer(writer, state);
}
/* review fold 4: the worker row's runner just before the redo - the round's live "this game runs it" (liveHere) is written onto the
   row, so the take guard (which reads the row, refreshed only every 500 ms by SpSendRound) passes the redo instead of refusing it. */
inline int SpotPutInRedoWriter(int rowWriter, int liveHere)
{
    return (liveHere != 0) ? kProdWriterHere : rowWriter;
}
inline const char* SpotPutInWhyText(int why)
{
    switch (why)
    {
    case kPutInWhyGranted:      return "the building's game said yes";
    case kPutInWhyRunsHere:     return "this game runs the building now";
    case kPutInWhyLinkDown:     return "the link is down (a lone game)";
    case kPutInWhyFull:         return "the building's game answered full";
    case kPutInWhyBuildingGone: return "the building is gone";
    case kPutInWhyNoAnswer:     return "no answer after 3 asks (fallback)";
    case kPutInWhyPrisonerGone: return "the prisoner is gone";
    case kPutInWhyNotMine:      return "the prisoner is not (or no longer) this game's";
    case kPutInWhyNotPinned:    return "the prisoner no longer lies pinned (caged or out by another road)";
    default:                    return "-";
    }
}

/* T-853 #3. A yes that changed no row (SpotAnswerApply left it): the runner took a spot nobody here will use - released at once.
   Only for this game's own worker (ownWorker), since every game hears every answer and a row may exist here for another game's
   worker; then released unless this game's row already holds that grant (rowFound, state: a keep-alive's yes). */
inline int SpotStrayYesRelease(int rowFound, int ownWorker, int state, unsigned char result)
{
    if (result != kSpotYes || ownWorker == 0) return 0;
    return (rowFound != 0 && state == kSpotGranted) ? 0 : 1;
}

/* T-853 #5. A granted row whose grant came from a game that no longer runs the building (grantSlot = the slot that answered yes,
   runnerSlot = the slot the ladder names now; either below 0 = not known) is asked again of the new runner. */
inline int SpotRegrantDue(int state, int grantSlot, int runnerSlot)
{
    return (state == kSpotGranted && grantSlot >= 0 && runnerSlot >= 0 && grantSlot != runnerSlot) ? 1 : 0;
}

/* T-853 #4. The asker re-asks a granted spot every kSpotKeepMs (the runner answers yes again); a counted spot (no copy on the
   runner) not asked for kSpotCountedForgetMs is freed by the runner - its asker has gone. */
const unsigned int kSpotKeepMs = 10000, kSpotCountedForgetMs = 30000;
inline int SpotKeepAskDue(int state, unsigned int msSinceAsk) { return (state == kSpotGranted && msSinceAsk > kSpotKeepMs) ? 1 : 0; }

/* The worker left the building's worker set on the asking game (the set's erase removed `erased` entries): a release goes to
   the runner only for a spot the runner granted. */
inline int SpotReleaseDue(int state, long long erased) { return (state == kSpotGranted && erased > 0) ? 1 : 0; }

/* On the runner: a spot it took for another game's worker is freed when the release comes, when its copy of the worker is gone,
   or when it no longer runs the building (copyGone / runsHere: 1 yes). */
inline int SpotRunnerFree(int released, int copyGone, int runsHere) { return (released != 0 || copyGone != 0 || runsHere == 0) ? 1 : 0; }
/* T-853 #4: the same for any spot the runner holds - a counted one (counted 1: no copy, no hand) counts as gone once not asked for
   kSpotCountedForgetMs; its copy's presence says nothing. Any free but a release is told to the asker (kSpotRevoked, farm.cpp). */
inline int SpotRunnerFreeRow(int released, int counted, int copyGone, unsigned int msSinceAsk, int runsHere)
{
    const int gone = (counted != 0) ? ((msSinceAsk > kSpotCountedForgetMs) ? 1 : 0) : copyGone;
    return SpotRunnerFree(released, gone, runsHere);
}

/* T-1192 (d). THE BUILDINGS WHOSE WORKER SPOTS THEIR RUNNER COUNTS, by the work step their work slot leads to (WorkStepKind above):
   production machines and torture buildings (T-674), crafting benches and furnaces. All of them keep the worker set +0x3D0, its
   count +0x3E0 and the limit +0x3AC. The take (vtable +0x4F8) is Building's 0xF7FF0 for all but the crafting bench, whose own take
   0x2B5BB0 (build/vtable_CraftingBuilding.txt; 1.0.68 0x2B5BF0) answers 0 when its vt+0x4E8 test holds, then tests the same set
   against the same limit and adds the hand through the same 0xF7D50 (build/decomp_2b5bb0.txt). kSpotTakeNone: not counted (a farm,
   any other building) - its take passes as before. */
const int kSpotTakeNone = 0, kSpotTakeBuilding = 1, kSpotTakeBench = 2;
inline int SpotTakeOf(int stepKind)
{
    if (stepKind == kStepCrafting) return kSpotTakeBench;
    if (stepKind == kStepProduction || stepKind == kStepFurnace) return kSpotTakeBuilding;
    return kSpotTakeNone;
}
/* A take detour gates the call only when the building's own take is the one it sits on (take = SpotTakeOf of the building, hooked =
   the take the detour is on); any other building through it passes to the engine as before. */
inline int SpotTakeGated(int take, int hooked) { return (take != kSpotTakeNone && take == hooked) ? 1 : 0; }

/* par16 fold #3. A plant whose age is below kFarmHiddenAge is drawn by nobody (updatePlantInstance 0xDF320 returns at once),
   so its parts' scale must be zeroed the way destroyAPlant 0xE47C0 does or the ripe plant stays on screen. */
inline int FarmPlantHidden(float age) { return age < kFarmHiddenAge ? 1 : 0; }

}   /* namespace coopfarm */
