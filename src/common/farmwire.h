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
const unsigned int  kFarmMaxKey = 63;
const unsigned int  kFarmMaxPlants = 512;   /* a farm's plant count; a bigger message is refused, never truncated */
const float         kFarmOpMax = 60.0f;     /* one FARM_OP's work (operate amount x g_dt; the non-writer sends every 0.5 s) */
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
    float work;         /* FARM_OP: operate amount x the sender's g_dt */
    FarmMsg() : kind(0), grown(0.0f), died(0.0f), cleared(0.0f), growStart(0.0f), harvested(0), prodState(0), hasProgress(0),
                progress(0.0f), uid(0), skill(kFarmSkillUnknown), work(0.0f) { marker[0] = marker[1] = marker[2] = 0.0f; }
};

inline bool FarmFloatOk(float v, float lim) { return v == v && v > -lim && v < lim; }

inline bool FarmEncodable(const FarmMsg& m)
{
    if (m.key.empty() || m.key.size() > kFarmMaxKey) return false;
    if (m.kind == kFarmOp)
        return m.work == m.work && m.work > 0.0f && m.work <= kFarmOpMax && FarmFloatOk(m.skill, 1.0e4f) && m.skill >= kFarmSkillUnknown;
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

/* 1 when the first byte names a farm kind - the session's peek before DecodeBuild. */
inline int IsFarmKind(const char* p, size_t size) { return (p != 0 && size >= 1 && ((unsigned char)p[0] == kFarmState || (unsigned char)p[0] == kFarmOp)) ? 1 : 0; }

inline int DecodeFarm(const char* p, size_t size, FarmMsg* out)
{
    if (p == 0 || size < 2) return kFarmDecodeTooShort;
    FarmMsg m;
    m.kind = (unsigned char)p[0];
    if (m.kind != kFarmState && m.kind != kFarmOp) return kFarmDecodeBadKind;
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
   A lone game, a link that is down and an undecided farm run the engine as before (nothing is held without a writer named).
   par16 fold #8: verdict 3 = the farm table is FULL and this farm has no row - it is HELD (never run ungated: a non-writer
   that ran it would be a second writer; the cost is that nobody grows a farm past the table, and the table logs full). */
const int kFarmRun = 0, kFarmHold = 1;
const int kFarmVerdictFull = 3;
inline int FarmGate(int single, int linked, int verdict)
{
    if (single != 0 || linked == 0) return kFarmRun;
    return (verdict == 2 || verdict == kFarmVerdictFull) ? kFarmHold : kFarmRun;
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

/* par16 fold #3. A plant whose age is below kFarmHiddenAge is drawn by nobody (updatePlantInstance 0xDF320 returns at once),
   so its parts' scale must be zeroed the way destroyAPlant 0xE47C0 does or the ripe plant stays on screen. */
inline int FarmPlantHidden(float age) { return age < kFarmHiddenAge ? 1 : 0; }

}   /* namespace coopfarm */
