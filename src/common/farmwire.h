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
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes and the same decisions. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
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

/* par16 fold #3. A plant whose age is below kFarmHiddenAge is drawn by nobody (updatePlantInstance 0xDF320 returns at once),
   so its parts' scale must be zeroed the way destroyAPlant 0xE47C0 does or the ripe plant stays on screen. */
inline int FarmPlantHidden(float age) { return age < kFarmHiddenAge ? 1 : 0; }

}   /* namespace coopfarm */
