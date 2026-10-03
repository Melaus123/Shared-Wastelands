/* src/common/capturewire.h - P11 (protocol 101; P11 f5 CHAIN: 105): a slaver on game A processes game B's character (a COPY on A).
 *
 * The victim's game (its owner) makes every change to the victim; the captor's game makes every change to its own slaver.
 * The captor's engine writes to the copy inside the slaver's processing (task bodies 0x35A9C0 / 0x34E750) are caught and
 * not made; they become ONE request to the owner, which makes them with the engine's own functions and answers.
 *
 * MSG_CAPTURE (60, captor -> the victim's owner, RELIABLE):
 *   u32 reqId | u32 victimUid | u32 slaverUid | u16 taskType | u8 flags (1 strip, 2 dress, 4 state, 8 owner, 16 shave,
 *   32 chain - P11 f5: the engine's setChainedMode 0x32E550 on the victim with the slaver as owner, task 166)
 *   | u8 wantState (0..3, 0xFF none) | u8 nDress + nDress x (u8 len + sid) | u8 nStrip + nStrip x (u8 len + section,
 *   i16 x, i16 y, u8 len + sid, i32 qty)                                     nDress <= 4, nStrip <= 32, sid <= 48, section <= 32
 * MSG_CAPTURE_DONE (61, owner -> captor, RELIABLE):
 *   u32 reqId | u32 victimUid | u8 result (0 applied, 1 not mine, 2 dead / absent, 3 refused: not down, 4 malformed,
 *   5 refused: the slaver is not the asker's, 6 refused: already a slave with an owner - P11 f3) | u8 nRows + per row u8 outcome (0 taken, 1 gone, 2 fault, 3 refused: not taken - no slaver / no sid / no room to hold - P11 f3) + for a taken row
 *   the item record: 4 x (u8 len + sid: base, company, material, color) | f32 quality | f32 charges | i32 functionKind
 *   | i32 level | i32 unique | i32 quantity | u8 dressAdded | u8 stateNow (0..3, 0xFF unreadable) | u32 ownerUidNow
 *   | u8 shavedNow (P11 f3: 0 not shaved, 1 shaved, 0xFF unreadable - the captor undoes its copy's shave on a refusal only at 0)
 *   | u8 chainedNow (P11 f5: the chained byte after, 0 / 1, 0xFF unreadable) | u8 len + shackles sid (<= 48, empty none)
 *   | u8 len + shackles section (<= 32) | u8 shacklesLocked (0 / 1, 0xFF unreadable or none) - the first worn shackles after
 * MSG_CAPTURE_PLACED (62, captor -> owner, RELIABLE, P11 f3):
 *   u32 reqId | u32 victimUid | u8 nRows (the DONE's row count, <= 32) | u32 placedMask (bit i: the DONE's row i landed in the
 *   captor's slaver - the owner destroys its held object; every other taken row goes back on the owner's character; no bit at
 *   or above nRows)
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100): no auto, no nullptr,
 * no range-for, no lambdas.
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace coopcapture {

const size_t kCapSidMax   = 48;
const size_t kCapSecMax   = 32;
const size_t kCapStripMax = 32;
const size_t kCapDressMax = 4;
const int    kCapFlagsAll = 63;   /* P11 f5: + 32 chain */

enum { kCapFlagStrip = 1, kCapFlagDress = 2, kCapFlagState = 4, kCapFlagOwner = 8, kCapFlagShave = 16, kCapFlagChain = 32 };
enum { kCapApplied = 0, kCapNotMine = 1, kCapAbsent = 2, kCapNotDown = 3, kCapMalformed = 4, kCapWrongSlaver = 5,
       kCapAlreadySlave = 6, kCapResultMax = 6 };
enum { kCapRowTaken = 0, kCapRowGone = 1, kCapRowFault = 2, kCapRowRefused = 3, kCapRowMax = 3 };

/* P11 f6: the owner's gate for a victim that is ALREADY a slave whose owner hand names a character. ownerIsSlaver: that character
   is the asking slaver. chained: the victim's chained byte (0 / 1, -1 unreadable); shackles: worn shackles (0 none, 1 some, -1
   unreadable). A processing request (182) from its own slaver is processed (P11 f5); a CHAIN (166) from its own slaver is applied
   unless the victim is known chained AND shackled - the engine's setChainedMode sets the byte whatever it was and creates shackles
   whenever none are worn (single-player: cuffing your own unshackled slave shackles it). Anything from another slaver: refused. */
enum { kCapGateRefuse = 0, kCapGateProcess = 1, kCapGateRechain = 2 };
inline int CapSlaveGate(bool isChain, bool ownerIsSlaver, int chained, int shackles)
{
    if (!ownerIsSlaver) return kCapGateRefuse;
    if (!isChain) return kCapGateProcess;
    if (chained == 0 || shackles == 0) return kCapGateRechain;
    return kCapGateRefuse;
}

/* P11 f6: the captor's send order - a before b in the order the records became ready (wrap-safe). */
inline bool CapSeqBefore(long a, long b) { return (long)((unsigned long)a - (unsigned long)b) < 0; }
/* Sorts idx[0..n) (with seq alongside) by seq, stable; 1 when the order changed. */
inline int CapOrderBySeq(int* idx, long* seq, int n)
{
    int moved = 0;
    for (int i = 1; i < n; ++i)
    {
        const int ki = idx[i]; const long ks = seq[i];
        int j = i - 1;
        while (j >= 0 && CapSeqBefore(ks, seq[j])) { idx[j + 1] = idx[j]; seq[j + 1] = seq[j]; --j; moved = 1; }
        idx[j + 1] = ki; seq[j + 1] = ks;
    }
    return moved;
}

/* M7b (T750): the captor's per-victim block after an owner's answer. A processing answer (applied, or already a slave) sets the
   block to the state it answered. A CHAIN answer never creates one (P11 f5); when one exists and the CHAIN was applied with a known
   state, it moves the block's expected state to that answer - the cuff changed the owner's state, and a block left expecting the
   earlier state held back the processing request that follows the cuff for the whole window. */
enum { kCapBlockNone = 0, kCapBlockSet = 1, kCapBlockRetarget = 2 };
inline int CapBlockOnAnswer(bool isChain, int result, int stateNow, bool blockExists)
{
    if (!isChain) return (result == kCapApplied || result == kCapAlreadySlave) ? kCapBlockSet : kCapBlockNone;
    return (blockExists && result == kCapApplied && stateNow >= 0) ? kCapBlockRetarget : kCapBlockNone;
}
/* The block lets a processing request through (and is cleared): our copy shows the state it expects, or its window has passed. */
inline bool CapBlockClears(int expected, int stNow, unsigned long elapsedMs, unsigned long windowMs)
{
    return (expected >= 0 && stNow == expected) || elapsedMs >= windowMs;
}
/* M7b: a request for a victim the block holds. Send (the block clears); wait (its record stays ready and is asked again next
   frame - a deferral never consumes its trigger); or abandon (the victim's copy is gone or the link is down - nothing can carry
   it, and the copy's shave is undone exactly as for a request not sent). */
enum { kCapHeldSend = 0, kCapHeldWait = 1, kCapHeldAbandon = 2 };
inline int CapHeldDecide(int expected, int stNow, unsigned long elapsedMs, unsigned long windowMs, bool victimPresent, bool linked)
{
    if (!victimPresent || !linked) return kCapHeldAbandon;
    return CapBlockClears(expected, stNow, elapsedMs, windowMs) ? kCapHeldSend : kCapHeldWait;
}
inline bool CapAbandonUndoesShave(int flags) { return (flags & kCapFlagShave) != 0; }

const int kCapDecodeOk       = 0;
const int kCapDecodeTooShort = 1;
const int kCapDecodeBadValue = 2;   /* a count above its cap, a string too long, a flag / state / result / outcome out of range */
const int kCapDecodeTrailing = 3;   /* bytes left over after the last field */

struct CapStripRow
{
    std::string section;
    int x, y;
    std::string sid;
    int qty;
    CapStripRow() : x(0), y(0), qty(0) {}
};

struct CaptureReq
{
    unsigned int reqId, victimUid, slaverUid;
    int taskType;      /* 0..65535 */
    int flags;         /* kCapFlag* */
    int wantState;     /* 0..3, -1 none */
    std::vector<std::string> dress;
    std::vector<CapStripRow> strip;
    CaptureReq() : reqId(0), victimUid(0), slaverUid(0), taskType(0), flags(0), wantState(-1) {}
};

struct CapItem
{
    std::string baseSid, companySid, materialSid, colorSid;
    float quality, charges;
    int functionKind, level, unique, quantity;
    CapItem() : quality(0.0f), charges(0.0f), functionKind(0), level(0), unique(0), quantity(0) {}
};

struct CapDoneRow
{
    int outcome;       /* kCapRow* */
    CapItem item;      /* only for kCapRowTaken */
    CapDoneRow() : outcome(kCapRowGone) {}
};

struct CaptureDone
{
    unsigned int reqId, victimUid;
    int result;        /* kCap* result */
    std::vector<CapDoneRow> rows;
    int dressAdded;    /* 0..255 */
    int stateNow;      /* 0..3, -1 unreadable */
    unsigned int ownerUidNow;
    CaptureDone() : reqId(0), victimUid(0), result(kCapApplied), dressAdded(0), stateNow(-1), ownerUidNow(0), shavedNow(-1),
                    chainedNow(-1), shacklesLocked(-1) {}
    int shavedNow;     /* P11 f3: 0 not shaved, 1 shaved, -1 unreadable */
    int chainedNow;    /* P11 f5: the victim's chained byte after (Character +0x320): 0 / 1, -1 unreadable */
    std::string shacklesSid, shacklesSection;   /* P11 f5: the first shackles worn after (boots / armour); empty = none */
    int shacklesLocked;   /* P11 f5: its DoorLock's locked byte: 0 / 1, -1 unreadable or none */
};

/* ---- byte helpers (little-endian, as every other wire header) ---- */
inline void CapPutU8(std::vector<char>* b, unsigned int v) { b->push_back((char)(unsigned char)(v & 0xFFu)); }
inline void CapPutU16(std::vector<char>* b, unsigned int v) { CapPutU8(b, v); CapPutU8(b, v >> 8); }
inline void CapPutU32(std::vector<char>* b, unsigned int v) { CapPutU16(b, v & 0xFFFFu); CapPutU16(b, v >> 16); }
inline void CapPutF32(std::vector<char>* b, float f) { unsigned int u = 0; std::memcpy(&u, &f, 4); CapPutU32(b, u); }
inline bool CapPutStr(std::vector<char>* b, const std::string& s, size_t cap)
{
    if (s.size() > cap) return false;
    CapPutU8(b, (unsigned int)s.size());
    b->insert(b->end(), s.begin(), s.end());
    return true;
}

struct CapReader
{
    const unsigned char* p;
    size_t n, at;
    bool ok;
    CapReader(const char* q, size_t size) : p((const unsigned char*)q), n(size), at(0), ok(q != 0) {}
    bool Need(size_t k) { if (!ok || n - at < k || at > n) { ok = false; return false; } return true; }
    unsigned int U8() { if (!Need(1)) return 0; return p[at++]; }
    unsigned int U16() { unsigned int a = U8(); unsigned int b = U8(); return a | (b << 8); }
    unsigned int U32() { unsigned int a = U16(); unsigned int b = U16(); return a | (b << 16); }
    float F32() { unsigned int u = U32(); float f = 0.0f; std::memcpy(&f, &u, 4); return f; }
    /* false with ok still true = too long (a bad value); ok false = too short */
    bool Str(std::string* out, size_t cap)
    {
        const unsigned int len = U8();
        if (!ok) return false;
        if (len > cap) return false;
        if (!Need(len)) return false;
        out->assign((const char*)p + at, (size_t)len);
        at += len;
        return true;
    }
};

/* false (nothing appended) when a field is out of range. */
inline bool EncodeCapture(std::vector<char>* b, const CaptureReq& r)
{
    if (b == 0 || r.taskType < 0 || r.taskType > 0xFFFF || r.flags < 0 || r.flags > kCapFlagsAll) return false;
    if (r.wantState < -1 || r.wantState > 3 || r.dress.size() > kCapDressMax || r.strip.size() > kCapStripMax) return false;
    std::vector<char> o;
    CapPutU32(&o, r.reqId); CapPutU32(&o, r.victimUid); CapPutU32(&o, r.slaverUid);
    CapPutU16(&o, (unsigned int)r.taskType); CapPutU8(&o, (unsigned int)r.flags);
    CapPutU8(&o, r.wantState < 0 ? 0xFFu : (unsigned int)r.wantState);
    CapPutU8(&o, (unsigned int)r.dress.size());
    for (size_t i = 0; i < r.dress.size(); ++i) if (!CapPutStr(&o, r.dress[i], kCapSidMax)) return false;
    CapPutU8(&o, (unsigned int)r.strip.size());
    for (size_t i = 0; i < r.strip.size(); ++i)
    {
        const CapStripRow& w = r.strip[i];
        if (w.x < -32768 || w.x > 32767 || w.y < -32768 || w.y > 32767) return false;
        if (!CapPutStr(&o, w.section, kCapSecMax)) return false;
        CapPutU16(&o, (unsigned int)(w.x & 0xFFFF)); CapPutU16(&o, (unsigned int)(w.y & 0xFFFF));
        if (!CapPutStr(&o, w.sid, kCapSidMax)) return false;
        CapPutU32(&o, (unsigned int)w.qty);
    }
    b->insert(b->end(), o.begin(), o.end());
    return true;
}

/* reqId and victimUid are written as soon as they are read (the owner answers a malformed request by them); the rest
   only on success. */
inline int DecodeCapture(const char* p, size_t size, CaptureReq* r)
{
    if (r == 0) return kCapDecodeTooShort;
    CapReader q(p, size);
    CaptureReq t;
    t.reqId = q.U32(); t.victimUid = q.U32();
    if (!q.ok) return kCapDecodeTooShort;
    r->reqId = t.reqId; r->victimUid = t.victimUid;
    t.slaverUid = q.U32();
    t.taskType = (int)q.U16();
    t.flags = (int)q.U8();
    const unsigned int ws = q.U8();
    const unsigned int nd = q.U8();
    if (!q.ok) return kCapDecodeTooShort;
    if (t.flags > kCapFlagsAll || (ws > 3 && ws != 0xFFu) || nd > kCapDressMax) return kCapDecodeBadValue;
    t.wantState = (ws == 0xFFu) ? -1 : (int)ws;
    for (unsigned int i = 0; i < nd; ++i)
    {
        std::string s;
        if (!q.Str(&s, kCapSidMax)) return q.ok ? kCapDecodeBadValue : kCapDecodeTooShort;
        t.dress.push_back(s);
    }
    const unsigned int ns = q.U8();
    if (!q.ok) return kCapDecodeTooShort;
    if (ns > kCapStripMax) return kCapDecodeBadValue;
    for (unsigned int i = 0; i < ns; ++i)
    {
        CapStripRow w;
        if (!q.Str(&w.section, kCapSecMax)) return q.ok ? kCapDecodeBadValue : kCapDecodeTooShort;
        w.x = (int)(short)(unsigned short)q.U16(); w.y = (int)(short)(unsigned short)q.U16();
        if (!q.Str(&w.sid, kCapSidMax)) return q.ok ? kCapDecodeBadValue : kCapDecodeTooShort;
        w.qty = (int)q.U32();
        if (!q.ok) return kCapDecodeTooShort;
        t.strip.push_back(w);
    }
    if (q.at != size) return kCapDecodeTrailing;
    *r = t;
    return kCapDecodeOk;
}

inline bool EncodeCaptureDone(std::vector<char>* b, const CaptureDone& d)
{
    if (b == 0 || d.result < 0 || d.result > kCapResultMax || d.rows.size() > kCapStripMax) return false;
    if (d.dressAdded < 0 || d.dressAdded > 255 || d.stateNow < -1 || d.stateNow > 3 || d.shavedNow < -1 || d.shavedNow > 1) return false;
    if (d.chainedNow < -1 || d.chainedNow > 1 || d.shacklesLocked < -1 || d.shacklesLocked > 1) return false;   /* P11 f5 */
    std::vector<char> o;
    CapPutU32(&o, d.reqId); CapPutU32(&o, d.victimUid); CapPutU8(&o, (unsigned int)d.result);
    CapPutU8(&o, (unsigned int)d.rows.size());
    for (size_t i = 0; i < d.rows.size(); ++i)
    {
        const CapDoneRow& w = d.rows[i];
        if (w.outcome < 0 || w.outcome > kCapRowMax) return false;
        CapPutU8(&o, (unsigned int)w.outcome);
        if (w.outcome != kCapRowTaken) continue;
        const CapItem& it = w.item;
        if (!CapPutStr(&o, it.baseSid, kCapSidMax) || !CapPutStr(&o, it.companySid, kCapSidMax)
            || !CapPutStr(&o, it.materialSid, kCapSidMax) || !CapPutStr(&o, it.colorSid, kCapSidMax)) return false;
        CapPutF32(&o, it.quality); CapPutF32(&o, it.charges);
        CapPutU32(&o, (unsigned int)it.functionKind); CapPutU32(&o, (unsigned int)it.level);
        CapPutU32(&o, (unsigned int)it.unique); CapPutU32(&o, (unsigned int)it.quantity);
    }
    CapPutU8(&o, (unsigned int)d.dressAdded);
    CapPutU8(&o, d.stateNow < 0 ? 0xFFu : (unsigned int)d.stateNow);
    CapPutU32(&o, d.ownerUidNow);
    CapPutU8(&o, d.shavedNow < 0 ? 0xFFu : (unsigned int)d.shavedNow);
    CapPutU8(&o, d.chainedNow < 0 ? 0xFFu : (unsigned int)d.chainedNow);   /* P11 f5: the chain tail */
    if (!CapPutStr(&o, d.shacklesSid, kCapSidMax) || !CapPutStr(&o, d.shacklesSection, kCapSecMax)) return false;
    CapPutU8(&o, d.shacklesLocked < 0 ? 0xFFu : (unsigned int)d.shacklesLocked);
    b->insert(b->end(), o.begin(), o.end());
    return true;
}

/* Nothing is written on a refusal. */
inline int DecodeCaptureDone(const char* p, size_t size, CaptureDone* d)
{
    if (d == 0) return kCapDecodeTooShort;
    CapReader q(p, size);
    CaptureDone t;
    t.reqId = q.U32(); t.victimUid = q.U32();
    t.result = (int)q.U8();
    const unsigned int nr = q.U8();
    if (!q.ok) return kCapDecodeTooShort;
    if (t.result > kCapResultMax || nr > kCapStripMax) return kCapDecodeBadValue;
    for (unsigned int i = 0; i < nr; ++i)
    {
        CapDoneRow w;
        w.outcome = (int)q.U8();
        if (!q.ok) return kCapDecodeTooShort;
        if (w.outcome > kCapRowMax) return kCapDecodeBadValue;
        if (w.outcome == kCapRowTaken)
        {
            CapItem& it = w.item;
            if (!q.Str(&it.baseSid, kCapSidMax) || !q.Str(&it.companySid, kCapSidMax)
                || !q.Str(&it.materialSid, kCapSidMax) || !q.Str(&it.colorSid, kCapSidMax))
                return q.ok ? kCapDecodeBadValue : kCapDecodeTooShort;
            it.quality = q.F32(); it.charges = q.F32();
            it.functionKind = (int)q.U32(); it.level = (int)q.U32(); it.unique = (int)q.U32(); it.quantity = (int)q.U32();
            if (!q.ok) return kCapDecodeTooShort;
        }
        t.rows.push_back(w);
    }
    t.dressAdded = (int)q.U8();
    const unsigned int st = q.U8();
    t.ownerUidNow = q.U32();
    const unsigned int sh = q.U8();
    const unsigned int ch = q.U8();   /* P11 f5: the chain tail */
    if (!q.ok) return kCapDecodeTooShort;
    if (!q.Str(&t.shacklesSid, kCapSidMax) || !q.Str(&t.shacklesSection, kCapSecMax)) return q.ok ? kCapDecodeBadValue : kCapDecodeTooShort;
    const unsigned int lk = q.U8();
    if (!q.ok) return kCapDecodeTooShort;
    if ((st > 3 && st != 0xFFu) || (sh > 1 && sh != 0xFFu) || (ch > 1 && ch != 0xFFu) || (lk > 1 && lk != 0xFFu)) return kCapDecodeBadValue;
    t.stateNow = (st == 0xFFu) ? -1 : (int)st;
    t.shavedNow = (sh == 0xFFu) ? -1 : (int)sh;
    t.chainedNow = (ch == 0xFFu) ? -1 : (int)ch;
    t.shacklesLocked = (lk == 0xFFu) ? -1 : (int)lk;
    if (q.at != size) return kCapDecodeTrailing;
    *d = t;
    return kCapDecodeOk;
}

/* P11 f3 - MSG_CAPTURE_PLACED: which of a DONE's taken rows landed in the captor's slaver. */
struct CapturePlaced
{
    unsigned int reqId, victimUid;
    int nRows;                 /* 0..kCapStripMax */
    unsigned int placedMask;   /* bit i = row i; no bit at or above nRows */
    CapturePlaced() : reqId(0), victimUid(0), nRows(0), placedMask(0) {}
};
inline bool CapPlacedMaskOk(int nRows, unsigned int mask)
{
    if (nRows < 0 || nRows > (int)kCapStripMax) return false;
    return nRows >= 32 || (mask >> nRows) == 0;
}
/* false (nothing appended) for a row count above the cap or a bit at or above it. */
inline bool EncodeCapturePlaced(std::vector<char>* b, const CapturePlaced& m)
{
    if (b == 0 || !CapPlacedMaskOk(m.nRows, m.placedMask)) return false;
    CapPutU32(b, m.reqId); CapPutU32(b, m.victimUid); CapPutU8(b, (unsigned int)m.nRows); CapPutU32(b, m.placedMask);
    return true;
}
/* Nothing is written on a refusal. */
inline int DecodeCapturePlaced(const char* p, size_t size, CapturePlaced* m)
{
    if (m == 0) return kCapDecodeTooShort;
    CapReader q(p, size);
    CapturePlaced t;
    t.reqId = q.U32(); t.victimUid = q.U32(); t.nRows = (int)q.U8(); t.placedMask = q.U32();
    if (!q.ok) return kCapDecodeTooShort;
    if (!CapPlacedMaskOk(t.nRows, t.placedMask)) return kCapDecodeBadValue;
    if (q.at != size) return kCapDecodeTrailing;
    *m = t;
    return kCapDecodeOk;
}

} // namespace coopcapture
