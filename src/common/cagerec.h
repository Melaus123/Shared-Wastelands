#pragma once
/* src/common/cagerec.h - ONE CAGE'S LOCK AS ONE WORLD-SERVER RECORD (T-860).
 *
 * A cage's lock (locked, broken, level) is a fact of the cage, not of the prisoner sitting in it. It has one source: this record,
 * written only by the game the area map names as the holder of the cage's area, on a real change of the lock (the engine's lock,
 * unlock, pick or break of the cage, the caging's close, a teammate's request the holder took). Every game lays the record's lock onto
 * its own cage once the cage is loaded and caged there (after the engine's load has written the save's lock, so the record beats the
 * save). A game that does not hold the area never writes: its change is a request to the holder (BOX_LOCK kind kBlmCage, boxrec.h),
 * kept applied here until the holder's record answers it. The prisoner road's cage lock word is retired (prisonwire.h); shackles
 * stay on it.
 *
 * One record per cage, id `cage.<sx>.<sy>.#<hash>.<x10>.<z10>` - the box record id of the cage's position key (boxrec.h
 * MakeBoxRecId: area square, hashed record name, x and z; no height, as games match a cage by sector, record, x and z) with the
 * prefix `cage.` in place of `box.`. The full key travels in the record, and a record is taken only under the id its key makes. The
 * record's bytes (the `.platoon` file on the world server, message 28 RECORD on the wire), little-endian:
 *
 *   u32 marker 'CAG1' | u32 format (kCrFormat) | u32 flags (kCrFlagSeed: the holder's first record of the cage, from its own live
 *   lock) | u32 generation | i32 writer slot | u32 length + the cage's position key (1..kCrKeyMax bytes, no control byte) |
 *   u8 locked | u8 broken | i32 lock level (0..kCrLevelMax). Nothing follows.
 *
 * A record cut short, with an unknown marker, format or flag, a key that makes no cage id, a byte flag other than 0 or 1, a level out
 * of range or bytes after its last field DECODES TO REFUSED.
 *
 * THE WORLD SERVER'S RULE (CageJudgeWrite, as doorrec.h DoorJudgeWrite): only the holder of the id's area writes, and the record names
 * that game as its writer; a seed is taken only while no record of that id exists (the refused seeder is sent the stored record); any
 * other write carries a generation above the stored one (the very same bytes at the stored generation are a harmless repeat). A
 * removal is taken only from the area's holder. A refusal is boxrec.h's BOX_REFUSE; a taken write is answered with RECORD_SEQ.
 * World-server protocol 147 carries it (a world server of 146 would keep a `cage.` record as a squad's).
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "boxrec.h"   /* the cage id is the box id of its key; the refusal reasons and BOX_REFUSE are shared */

namespace cagerec {

const unsigned int kProtocol = 177;            /* the world-server protocol that carries cage records */


const unsigned int kCrMarker = 0x31474143u;    /* 'C','A','G','1' little-endian */
const unsigned int kCrFormat = 1u;
const unsigned int kCrFlagSeed = 1u;
const unsigned int kCrKeyMax = 255u;           /* fold 1 (L5): the box key's cap (coopbrec::kBrKeyMax) */
const unsigned int kCrMaxBytes = 320u;
const int kCrLevelMax = 100000;
const int kCrMaxSlot = 65535;

/* decode results: anything but kCrOk is a refused record */
const int kCrOk = 0, kCrShort = 1, kCrBadMarker = 2, kCrBadFormat = 3, kCrBadRecord = 4, kCrTrailing = 5, kCrTooBig = 6;

struct CageLock
{
    int locked, broken, level;
    CageLock() : locked(0), broken(0), level(0) {}
    CageLock(int l, int b, int v) : locked(l), broken(b), level(v) {}
};
inline bool CageLockSame(const CageLock& a, const CageLock& b) { return a.locked == b.locked && a.broken == b.broken && a.level == b.level; }
inline bool CageLockValid(const CageLock& l)
{
    return (l.locked == 0 || l.locked == 1) && (l.broken == 0 || l.broken == 1) && l.level >= 0 && l.level <= kCrLevelMax;
}

struct CageRec
{
    unsigned int flags, generation;
    int writerSlot;
    std::string key;
    CageLock lock;
    CageRec() : flags(0), generation(0), writerSlot(0) {}
};

/* ---- the id ---- */
inline bool CrKeyOk(const std::string& k)
{
    if (k.empty() || k.size() > (size_t)kCrKeyMax) return false;
    for (size_t i = 0; i < k.size(); ++i) { const unsigned char c = (unsigned char)k[i]; if (c < 0x20 || c == 0x7F) return false; }
    return true;
}
/* the cage id of a cage's position key; "" when the key is not a position key on the grid */
inline std::string MakeCageRecId(const std::string& key)
{
    if (!CrKeyOk(key)) return std::string();
    const std::string b = coopbrec::MakeBoxRecId(key);
    if (b.size() < 5 || b.compare(0, 4, "box.") != 0) return std::string();
    return "cage." + b.substr(4);
}
/* any spelling of the prefix counts (file names ignore case); only the spelling MakeCageRecId makes parses */
inline bool IsCageRecId(const std::string& id)
{
    static const char kPrefix[] = "cage.";
    if (id.size() < 5) return false;
    for (size_t i = 0; i < 5; ++i) { char c = id[i]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c != kPrefix[i]) return false; }
    return true;
}
/* 1 = a spelling MakeCageRecId makes (its area square into sx / sy when not 0); 0 = not a cage id */
inline int CageRecIdParse(const std::string& id, int* sx, int* sy)
{
    if (id.size() < 6 || id.compare(0, 5, "cage.") != 0) return 0;
    int a = -1, b = -1;
    int haveY = 0;
    if (coopbrec::BoxRecIdParse("box." + id.substr(5), &a, &b, 0, 0, 0, 0, &haveY) == 0 || haveY != 0) return 0;
    if (sx) *sx = a;
    if (sy) *sy = b;
    return 1;
}
/* the plugin's area index of a cage id's area square (sx * 64 + sy); -1 not a cage id */
inline int CageRecIdArea(const std::string& id)
{
    int sx = -1, sy = -1;
    if (CageRecIdParse(id, &sx, &sy) == 0 || sx < 0 || sx > 63 || sy < 0 || sy > 63) return -1;
    return sx * 64 + sy;
}

/* ---- validity and bytes ---- */
inline bool CageRecValid(const CageRec& r)
{
    return (r.flags & ~kCrFlagSeed) == 0 && r.writerSlot >= 0 && r.writerSlot <= kCrMaxSlot && !MakeCageRecId(r.key).empty() && CageLockValid(r.lock);
}
inline void CrPutU32(std::vector<char>* b, unsigned int v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline bool CrGet4(const char* p, size_t size, size_t* off, void* v)
{
    if (*off > size || size - *off < 4) return false;
    std::memcpy(v, p + *off, 4);
    *off += 4;
    return true;
}
inline bool CrGet1(const char* p, size_t size, size_t* off, int* v)
{
    if (*off >= size) return false;
    *v = (int)(unsigned char)p[*off];
    *off += 1;
    return true;
}
/* false = nothing written (*out emptied): the record breaks CageRecValid */
inline bool EncodeCageRec(std::vector<char>* out, const CageRec& r)
{
    if (out == 0) return false;
    out->clear();
    if (!CageRecValid(r)) return false;
    CrPutU32(out, kCrMarker);
    CrPutU32(out, kCrFormat);
    CrPutU32(out, r.flags);
    CrPutU32(out, r.generation);
    CrPutU32(out, (unsigned int)r.writerSlot);
    CrPutU32(out, (unsigned int)r.key.size());
    out->insert(out->end(), r.key.begin(), r.key.end());
    out->push_back((char)r.lock.locked);
    out->push_back((char)r.lock.broken);
    CrPutU32(out, (unsigned int)r.lock.level);
    return true;
}
/* kCrOk: *out holds the record; anything else: refused, *out untouched */
inline int DecodeCageRec(const char* p, size_t size, CageRec* out)
{
    if (p == 0 || size < 24u) return kCrShort;
    if (size > (size_t)kCrMaxBytes) return kCrTooBig;
    size_t off = 0;
    unsigned int marker = 0, format = 0, len = 0, lv = 0;
    CageRec r;
    CrGet4(p, size, &off, &marker);
    if (marker != kCrMarker) return kCrBadMarker;
    CrGet4(p, size, &off, &format);
    if (format != kCrFormat) return kCrBadFormat;
    if (!CrGet4(p, size, &off, &r.flags) || !CrGet4(p, size, &off, &r.generation) || !CrGet4(p, size, &off, &r.writerSlot) || !CrGet4(p, size, &off, &len))
        return kCrShort;
    if (len == 0 || len > kCrKeyMax) return kCrBadRecord;
    if (size - off < (size_t)len) return kCrShort;
    r.key.assign(p + off, len);
    off += len;
    if (!CrGet1(p, size, &off, &r.lock.locked) || !CrGet1(p, size, &off, &r.lock.broken) || !CrGet4(p, size, &off, &lv)) return kCrShort;
    r.lock.level = (int)lv;
    if (off != size) return kCrTrailing;
    if (!CageRecValid(r)) return kCrBadRecord;
    if (out) *out = r;
    return kCrOk;
}
inline const char* CageDecodeWhy(int why)
{
    return why == kCrOk ? "ok" : why == kCrShort ? "cut short" : why == kCrBadMarker ? "not a cage record (wrong marker)"
         : why == kCrBadFormat ? "an unknown format" : why == kCrBadRecord ? "a field out of range" : why == kCrTrailing ? "bytes after the last field"
         : why == kCrTooBig ? "larger than the byte cap" : "malformed";
}

/* ---- the world server ---- */
/* One write of cage record `id` from slot senderSlot; holderSlot: the slot the area map names as the holder of the id's area (-1
   nobody); exists / storedGen: a stored record and its generation; sameBytes: the bytes equal the stored file. coopbrec::kBrAccept,
   kBrRepeat (nothing to write) or a refusal reason. *out (when not 0) gets the decoded record when it read. */
inline int CageJudgeWrite(const std::string& id, const char* p, size_t size, int senderSlot, int holderSlot, int exists, unsigned int storedGen,
                          int sameBytes, CageRec* out)
{
    if (CageRecIdParse(id, 0, 0) == 0) return coopbrec::kBrRefuseBadId;
    CageRec r;
    if (DecodeCageRec(p, size, &r) != kCrOk) return coopbrec::kBrRefuseUnreadable;
    if (out != 0) *out = r;
    if (MakeCageRecId(r.key) != id) return coopbrec::kBrRefuseWrongBox;
    if (senderSlot < 0 || r.writerSlot != senderSlot) return coopbrec::kBrRefuseWriter;
    if (holderSlot < 0 || senderSlot != holderSlot) return coopbrec::kBrRefuseNotWriter;
    if (exists != 0)
    {
        if ((r.flags & kCrFlagSeed) != 0) return coopbrec::kBrRefuseSeedExists;
        if (r.generation < storedGen) return coopbrec::kBrRefuseOlder;
        if (r.generation == storedGen) return (sameBytes != 0) ? coopbrec::kBrRepeat : coopbrec::kBrRefuseSameGen;
    }
    return coopbrec::kBrAccept;
}
/* a removal of cage record `id`: only the area's holder removes one */
inline int CageJudgeRemove(const std::string& id, int senderSlot, int holderSlot)
{
    if (CageRecIdParse(id, 0, 0) == 0) return coopbrec::kBrRefuseBadId;
    if (senderSlot < 0 || holderSlot < 0 || senderSlot != holderSlot) return coopbrec::kBrRefuseNotWriter;
    return coopbrec::kBrAccept;
}

/* ---- the game ---- */
/* a received record is kept when nothing is kept for its id or its generation is not below the kept one */
inline int CageArrivalTakes(int haveKept, unsigned int keptGen, unsigned int gen) { return (haveKept == 0 || gen >= keptGen) ? 1 : 0; }
/* the next generation this game writes: above the record it knows and above any generation a refusal named */
inline unsigned int CageNextGen(int haveRec, unsigned int gen, unsigned int floorGen)
{
    unsigned int g = (haveRec != 0) ? gen : 0u;
    if (floorGen > g) g = floorGen;
    return g + 1u;
}
/* What a refusal of this game's write asks. Older / the same generation with other bytes: another write is stored past it - the
   kept copy goes back to the confirmed one and the cage is read again (its live lock, if it still differs, is written above the
   stored generation; at most kCrRewriteCap times). A seed where a record exists: the stored record wins - the world server sends it,
   and it is laid. Not the writer: the confirmed copy is put back and this game's change is held as a request, routed again at the
   next area map (kCrRefuseHold). Anything else: dropped. */
const int kCrRefuseRewrite = 0, kCrRefuseTakeStored = 1, kCrRefuseHold = 2, kCrRefuseDrop = 3;
const int kCrRewriteCap = 3;
inline int CageRefuseNext(int reason, int rewrites)
{
    if (reason == coopbrec::kBrRefuseOlder || reason == coopbrec::kBrRefuseSameGen) return rewrites < kCrRewriteCap ? kCrRefuseRewrite : kCrRefuseDrop;
    if (reason == coopbrec::kBrRefuseSeedExists) return kCrRefuseTakeStored;
    if (reason == coopbrec::kBrRefuseNotWriter) return kCrRefuseHold;
    return kCrRefuseDrop;
}
/* the world server's RECORD_SEQ names the stamp this game's write was sent with: it confirms that pending write */
inline int CageEchoConfirms(int pending, long long sentStamp, int hasStamp, long long echoStamp)
{
    return (pending != 0 && hasStamp != 0 && sentStamp != 0 && echoStamp == sentStamp) ? 1 : 0;
}
/* ---- the game: one cage on this game, and what each event makes it do (spawn.cpp runs the actions and sends) ----
   THE ORDER RULE (fold 1): a record decides a cage only when it is newer than the cage's lock here. A lock this game set itself - its own
   change (a pick, the lever, its caging's close, a release, an engine caging of its own character during play) - is kept as PEND until
   a record HOLDING THAT LOCK arrives (the holder took it) or this game writes it as the holder: no record without it is ever laid over
   it, whatever order records arrive in (a third game's question answered with the older record cannot undo it). A lock this game did
   not set (the save's, laid by its load) is older than any record, so the record is laid over it. A change on a game that does not hold
   the area is sent to the holder at once (BOX_LOCK kBlmCage); with nobody holding it is held and sent on the area map's arrival that
   names a holder (an event). Every request is answered: kBlmCageTaken (with the generation written), kBlmCageStale (the holder's record moved
   past the one the change was made over), kBlmCageNotHolder or kBlmCageNone (fold 2). */
const int kCaNothing = 0, kCaLay = 1, kCaMarkLaid = 2, kCaSeed = 3, kCaWrite = 4, kCaAsk = 5, kCaQuery = 6, kCaHeld = 7, kCaTaken = 8;
const int kCwSeed = 0, kCwLive = 1, kCwRequest = 2;   /* what a write of this game carried: a seed, its own change, a teammate's request */
const unsigned long kCageLayRetryMs = 5000;           /* a lay queued for the safe point and not seen on the cage by then is queued again */
struct CageState
{
    int have; CageRec r;               /* kept: this game's newest write as soon as sent, else the newest record taken */
    int confHave; CageRec conf;        /* confirmed: the last record taken from the world server, or this game's write it took */
    int laid; unsigned int laidGen;    /* the kept record's lock is on this game's cage */
    int layQueued; unsigned long layAt;
    int pending; long long sentStamp; int sentKind; unsigned int floorGen; int rewrites;   /* this game's write awaiting the world server */
    int pend, pendSent, heldSaid; CageLock pendLock;   /* this game's own change, in no record this game has yet */
    unsigned int pendBaseGen; CageLock pendBaseLock;   /* fold 2: the record the change was made over (0 = none) - sent with it */
    unsigned int takenGen;             /* fold 2: the holder took the change as this generation: any record of it or newer ends it */
    unsigned int waitGen;              /* fold 2: the holder said its record of this generation is newer: no older record is laid meanwhile */
    int pendRevert;                    /* fold 2 (LOW-1): the change returns the cage to the record's lock after a request went out - sent all the same */
    int fwd; CageLock fwdLock;         /* a teammate's request this game took as the holder whose write was refused: sent on */
    int queried, noneAnswer, blocked;
    CageState() : have(0), confHave(0), laid(0), laidGen(0), layQueued(0), layAt(0), pending(0), sentStamp(0), sentKind(kCwSeed), floorGen(0),
                  rewrites(0), pend(0), pendSent(0), heldSaid(0), pendBaseGen(0), takenGen(0), waitGen(0), pendRevert(0), fwd(0), queried(0), noneAnswer(0), blocked(0) {}
};
inline void CagePendSet(CageState& s, const CageLock& l)
{
    s.pend = 1; s.pendSent = 0; s.heldSaid = 0; s.pendLock = l; s.takenGen = 0; s.pendRevert = 0;
    s.pendBaseGen = (s.have != 0) ? s.r.generation : 0u;
    s.pendBaseLock = (s.have != 0) ? s.r.lock : l;
}
/* THE READ OF A CAGED CHARACTER'S CAGE (the lock watch, after the load's own writes and the re-caging). live: the cage's lock here;
   hold: the holder of the cage's area - 1 this game, 0 another game, -1 nobody or not known yet; changedHere: this read's lock is this
   game's own change (CageCagedLive: an engine caging of its own character during play); now: ms. Returns the action:
   kCaLay (queue the record's lock for the safe point), kCaMarkLaid (the cage already holds it), kCaSeed (the holder's first record, from
   its live lock), kCaWrite (the holder writes pendLock), kCaAsk (send pendLock to the holder), kCaQuery (no record: ask the holder for
   its lock - this game's read is the save's and is never sent), kCaHeld (nobody holds the area: the change stays here; said once),
   kCaTaken (the record holds this game's change), kCaNothing. */
inline int CageOnRead(CageState& s, const CageLock& live, int hold, int changedHere, unsigned long now)
{
    if (s.blocked != 0 && hold == 1) hold = -1;   /* the world server said this game is not the writer: held until the next area map */
    if (s.layQueued != 0 && now - s.layAt >= kCageLayRetryMs) s.layQueued = 0;
    const int same = (s.have != 0 && CageLockSame(s.r.lock, live)) ? 1 : 0;
    if (changedHere != 0 && !(s.pend != 0 && CageLockSame(s.pendLock, live))) CagePendSet(s, live);
    if (s.pend != 0 && !CageLockSame(s.pendLock, live))
    {   /* nothing lays a record while a change is pending, so the cage moved again here: its lock now is the change (or none) */
        if (same != 0 && s.pendSent == 0 && s.takenGen == 0) { s.pend = 0; }   /* undone before it went out: nothing to send */
        else
        {   /* made over this game's own change in flight: that change is its base, so the holder takes it after the first */
            const int revert = same; const CageLock prevLock = s.pendLock; const unsigned int prevGen = (s.takenGen != 0) ? s.takenGen : s.pendBaseGen;
            const int inFlight = (s.pendSent != 0 || s.takenGen != 0) ? 1 : 0;
            CagePendSet(s, live); s.pendRevert = revert;
            if (inFlight != 0) { s.pendBaseLock = prevLock; s.pendBaseGen = prevGen; }
        }   /* fold 2 (LOW-1): a request in flight is followed by this newer lock, never dropped */
    }
    if (s.pend == 0)
    {
        if (s.have == 0)
        {
            if (hold == 1) return (s.pending != 0) ? kCaNothing : kCaSeed;
            if (hold == 0) return (s.queried != 0 || s.noneAnswer != 0) ? kCaNothing : kCaQuery;
            return kCaNothing;
        }
        if (s.laid == 0 || s.laidGen != s.r.generation)
        {
            if (s.waitGen != 0 && s.r.generation < s.waitGen) return kCaNothing;   /* the holder's newer record is on its way */
            if (same != 0) { s.laid = 1; s.laidGen = s.r.generation; s.layQueued = 0; return kCaMarkLaid; }
            if (s.layQueued != 0) return kCaNothing;
            s.layQueued = 1; s.layAt = now;
            return kCaLay;
        }
        if (same != 0) return kCaNothing;
        CagePendSet(s, live);   /* a change after the lay: the engine moved this cage's lock here */
    }
    if (same != 0 && s.pendRevert == 0) { s.pend = 0; s.pendSent = 0; s.laid = 1; s.laidGen = s.r.generation; s.layQueued = 0; return kCaTaken; }
    if (hold == 1) return (s.pending != 0 && s.sentKind == kCwSeed) ? kCaNothing : kCaWrite;
    if (hold == 0) return (s.pendSent != 0 || s.takenGen != 0) ? kCaNothing : kCaAsk;
    if (s.heldSaid != 0) return kCaNothing;
    s.heldSaid = 1;
    return kCaHeld;
}
/* fold 2 (MED-1): an engine caging of this game's own character during play, decided from events, not from time. freeAfterLoad: the
   watch read the character out of any cage after this game's load ended (the own-record restore edge has run - StoreOwnRestorePending
   false; a character the load re-cages is caged from its first read and never read free); firstCagedRead: this read is that caging's
   first; recordAfterFree: a record of the cage arrived after that free read (the record may then be newer than the caging, so it is
   laid). 1 = the cage's lock is this caging's, newer than the record kept before it: a change made here. The mod's own cagings are
   marked where they are made (PrisonOwnCagedTail, cagetest - CageLockChangedHere). */
inline int CageCagedLive(int mine, int freeAfterLoad, int firstCagedRead, int recordAfterFree)
{
    return (mine != 0 && freeAfterLoad != 0 && firstCagedRead != 0 && recordAfterFree == 0) ? 1 : 0;
}
/* this game wrote rec (sent): kind kCwSeed / kCwLive (its live lock - the cage holds it) / kCwRequest (a teammate's - laid at the next read) */
inline void CageOnWrote(CageState& s, const CageRec& rec, long long stamp, int kind)
{
    s.pending = 1; s.sentStamp = stamp; s.sentKind = kind; s.r = rec; s.have = 1;
    if (kind != kCwRequest) { s.laid = 1; s.laidGen = rec.generation; s.layQueued = 0; }
    if (kind == kCwLive) { s.pend = 0; s.pendSent = 0; }
}
inline void CageOnAsked(CageState& s, int query) { if (query != 0) s.queried = 1; else s.pendSent = 1; }
/* A record of the cage from the world server. liveKnown / live: this game's cage read now (a change made since the last read, on a laid
   cage, becomes PEND before the record is taken - it is newer than a record that does not hold it). 1 = kept, 0 = older (dropped). */
inline int CageOnArrive(CageState& s, const CageRec& rec, int liveKnown, const CageLock& live)
{
    if (CageArrivalTakes(s.have, s.r.generation, rec.generation) == 0) return 0;
    if (liveKnown != 0 && s.pend == 0 && s.have != 0 && s.laid != 0 && s.laidGen == s.r.generation && !CageLockSame(s.r.lock, live)) CagePendSet(s, live);
    s.r = rec; s.have = 1; s.conf = rec; s.confHave = 1; s.pending = 0; s.queried = 0; s.noneAnswer = 0; s.rewrites = 0;
    if (s.pend != 0 && (CageLockSame(rec.lock, s.pendLock) || (s.takenGen != 0 && rec.generation >= s.takenGen)))
    { s.pend = 0; s.pendSent = 0; s.takenGen = 0; }   /* the record holds this game's change, or is at or past the generation that took it */
    if (s.waitGen != 0 && rec.generation >= s.waitGen) s.waitGen = 0;
    if (s.laidGen != rec.generation) s.laid = 0;
    return 1;
}
/* the world server took this game's write sent with that stamp */
inline int CageOnEcho(CageState& s, int hasStamp, long long stamp)
{
    if (CageEchoConfirms(s.pending, s.sentStamp, hasStamp, stamp) == 0) return 0;
    s.pending = 0; s.conf = s.r; s.confHave = 1; s.rewrites = 0;
    return 1;
}
/* The world server refused this game's write (CageRefuseNext). The kept copy goes back to the confirmed one. A refused CHANGE (this
   game's own, or a teammate's it took) is never lost to an older stored record: its own change is PEND again (written above the stored
   generation at the next read when this game holds the area, else sent to the holder), a teammate's is FWD (sent on). A refused seed
   leaves the stored record to win (it is sent back on SeedExists). Not the writer: held until the next area map. */
inline int CageOnRefuse(CageState& s, int reason, unsigned int storedGen)
{
    const int next = CageRefuseNext(reason, s.rewrites);
    const CageLock mine = s.r.lock;
    const int kind = s.sentKind;
    if (s.pending != 0) { s.r = s.conf; s.have = s.confHave; s.pending = 0; }
    if (storedGen > s.floorGen) s.floorGen = storedGen;
    if (kind == kCwSeed || next == kCrRefuseDrop) { s.laid = 0; return next; }
    if (next == kCrRefuseHold) s.blocked = 1;
    if (next == kCrRefuseRewrite || next == kCrRefuseTakeStored) ++s.rewrites;
    if (kind == kCwLive) CagePendSet(s, mine); else { s.fwd = 1; s.fwdLock = mine; }
    return next;
}
/* where this game's pending change, or a teammate's request it holds to send on (fwd), goes now: kCaWrite (this game holds the area),
   kCaAsk (another game does), kCaNothing (nobody - kept). Asked at THE AREA MAP'S ARRIVAL (an event - a held change is sent then, with no
   read needed: the prisoner may have left the cage), and for fwd also at a read. */
inline int CageHoldNow(const CageState& s, int hold) { return (s.blocked != 0 && hold == 1) ? -1 : hold; }   /* not the writer until the next area map */
inline int CagePendRoute(const CageState& s, int hold) { hold = CageHoldNow(s, hold); return (s.pend == 0 || s.takenGen != 0) ? kCaNothing : (hold == 1) ? kCaWrite : (hold == 0) ? kCaAsk : kCaNothing; }
inline int CageFwdRoute(const CageState& s, int hold) { hold = CageHoldNow(s, hold); return (s.fwd == 0) ? kCaNothing : (hold == 1) ? kCaWrite : (hold == 0) ? kCaAsk : kCaNothing; }
inline void CageOnMapEvent(CageState& s)
{
    s.blocked = 0; s.heldSaid = 0; s.pendSent = 0;
    if (s.have == 0) { s.queried = 0; s.noneAnswer = 0; }
}
/* the holder's answer to this game's request or question (BOX_LOCK kind, gen): kBlmCageTaken - written as gen: the change ends at the
   first record of gen or newer (already here: now); kBlmCageStale - the holder's record moved past the one the change was made over: the
   change ends and the holder's record (gen) is laid, no older one meanwhile; kBlmCageNotHolder - the change stays here and is sent again
   at the next area map; kBlmCageNone - nobody has a record or the cage loaded: this cage keeps the save's lock (an older world - not
   repaired) until the area map changes. */
inline void CageOnAnswer(CageState& s, int kind, unsigned int gen)
{
    if (kind == coopbrec::kBlmCageTaken)
    {
        if (s.pend == 0) return;
        s.takenGen = (gen != 0) ? gen : 1u;
        if (s.have != 0 && s.r.generation >= s.takenGen) { s.pend = 0; s.pendSent = 0; s.takenGen = 0; s.laid = (s.laid != 0 && s.laidGen == s.r.generation) ? 1 : 0; }
    }
    else if (kind == coopbrec::kBlmCageStale)
    {
        s.pend = 0; s.pendSent = 0; s.takenGen = 0; s.laid = 0;
        if (!(s.have != 0 && s.r.generation >= gen)) s.waitGen = gen;
    }
    else if (kind == coopbrec::kBlmCageNotHolder) s.heldSaid = 0;   /* pendSent stays: not asked again before the area map changes */
    else if (kind == coopbrec::kBlmCageNone) s.noneAnswer = 1;
}
/* THE HOLDER'S ANSWER TO A TEAMMATE (BOX_LOCK kBlmCage / kBlmCageQuery). A change is written whether or not this game has the cage
   loaded, and answered kBlmCageTaken with the generation written (kCqWrite) - unless it is stale (below). A question is answered with the kept record written again one generation up (kCqRewrite) or a seed from this game's live
   lock (kCqSeedLive); else BOX_LOCK kBlmCageNone (kCqNone). A game that does not hold the area answers kBlmCageNotHolder (kCqNotHolder). */
/* fold 2 (MED-2): a change is STALE (not written; answered kBlmCageStale with the holder's generation) when the holder's record moved past
   the record the change was made over - a newer generation holding ANOTHER lock (baseGen: that record's generation, 0 = the asker had
   none; baseLock: its lock). A newer generation with the same lock (a question answered by writing the record again) moved nothing, so
   the change is still taken. A resend of a change already taken is stale too (the record holds it): nothing is written twice. */
const int kCqWrite = 0, kCqStale = 1, kCqRewrite = 2, kCqSeedLive = 3, kCqNone = 4, kCqNotHolder = 5;
inline int CageRequestDecide(int hold, int query, int haveRec, int liveRead, unsigned int recGen, const CageLock& recLock, unsigned int baseGen,
                             const CageLock& baseLock)
{
    if (hold != 1) return kCqNotHolder;
    if (query == 0)
    {
        if (haveRec != 0 && baseGen != 0 && recGen > baseGen && !CageLockSame(recLock, baseLock)) return kCqStale;
        return kCqWrite;
    }
    if (haveRec != 0) return kCqRewrite;
    return liveRead != 0 ? kCqSeedLive : kCqNone;
}
/* T-980 FREED at the prisoner's owner (fold 1, H1): the freer's game read the cage open as it freed the copy (cageBit), and that open is
   a change on the freer's game, sent to the cage's holder. When every other check would raise the event (raiseOtherwise) and the
   owner's cage still reads locked, the owner opens its own cage first - the same change, routed to the holder too - and raises the
   event in the same pass; nothing waits for the record. 1 = open the cage, then raise. */
inline int FreedCageOpenFirst(int raiseOtherwise, int cageBit, int cageLockedHere) { return (raiseOtherwise != 0 && cageBit != 0 && cageLockedHere != 0) ? 1 : 0; }

/* the prisoner road's lock word: a cage's lock is no longer sent on it (its record is its one source); shackles still are */
inline int PrisonRoadSendsLock(int isCage) { return isCage != 0 ? 0 : 1; }

}   /* namespace cagerec */
