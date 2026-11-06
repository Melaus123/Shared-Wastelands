#pragma once
/* src/common/doorrec.h - ONE WORLD DOOR AS ONE WORLD-SERVER RECORD.
 *
 * One record per door of a building no player owns (a town's house doors and its gates), id `door.<sx>.<sy>.#<hash8>`: the door's
 * area square (the "@x,y@" field of its position key, 0..63 each, plain decimal) and the FNV-1a hash of its whole key in eight
 * lower-case hex digits. The key is the door road's own (doors.cpp DoorKeyBuild: a house door's position key, a gate's gateway
 * building key with "gate" appended) and travels in the record, so a reader checks that the record it holds names that very door
 * (two keys hashed to one id are told apart: the record's key must make the id it is stored under, and a lay matches by key). The
 * record's bytes (the `.platoon` file on the world server, message 28 RECORD on the wire), little-endian:
 *
 *   u32 marker 'DOR1' | u32 format (kDrFormat) | u32 flags (kDrFlagSeed: written at the holder's first load of the area, not for a
 *   change) | u32 generation | i32 writer slot | u32 length + the door's key (1..kDrKeyMax bytes, no control byte) | u8 gate |
 *   u8 state (0 closed, 1 open - the world's fact for a gate only; an ordinary door's swing is each game's own) | u8 lock word
 *   (bit 0 locked, bit 1 wants to lock - doorsync.h DoorLockWord) | u8 broken | u8 has a lock | i32 lock level (0..kDrLevelMax) |
 *   u8 lock broken | f32 lock hardness | f32 door health. Nothing follows.
 *
 * A record that is cut short, carries an unknown marker, format or flag, a key that makes no door id, a byte flag other than 0 or
 * 1, a state or lock word out of range, a level out of range, a number that is not finite, or bytes after its last field DECODES TO
 * REFUSED.
 *
 * THE WORLD SERVER'S RULE (DoorJudgeWrite): only the game the area map names as the holder of the door's area writes, and the record
 * names that game as its writer; a seed is taken only while no record of that id exists (the first writer won, and the refused seeder is sent the stored record); any other write
 * carries a generation above the stored one (the very same bytes at the stored generation are a harmless repeat, nothing is
 * written). A record is removed (RECORD_GONE) only by the area's holder: a ruined building's doors leave with it, and so do a rebuilt town's records that name no door (DoorJudgeRemove).
 * A refused write is answered with boxrec.h's BOX_REFUSE message and reasons (the game routes a door id to the door table).
 * A taken write is answered to its writer alone with RECORD_SEQ, naming the stamp the write was sent with (the record itself is
 * forwarded to every other game, never back to its writer): it confirms the writer's pending write (DoorRecEchoConfirms).
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "boxrec.h"   /* the refusal reasons and the BOX_REFUSE message the door record shares */

namespace doorrec {

const unsigned int kProtocol = 145;            /* the world-server protocol that carries door records */


const unsigned int kDrMarker = 0x31524F44u;    /* 'D','O','R','1' little-endian */
const unsigned int kDrFormat = 1u;
const unsigned int kDrFlagSeed = 1u;
const unsigned int kDrKeyMax = 63u;            /* doors.cpp kDoorKeyCap - 1 */
const unsigned int kDrMaxBytes = 256u;
const int kDrMaxSector = 63;
const int kDrLevelMax = 100000;
const float kDrMaxNumber = 1.0e7f;

/* decode results: anything but kDrOk is a refused record */
const int kDrOk = 0, kDrShort = 1, kDrBadMarker = 2, kDrBadFormat = 3, kDrBadRecord = 4, kDrTrailing = 5, kDrTooBig = 6;

struct DoorRec
{
    unsigned int flags, generation;
    int writerSlot;
    std::string key;
    int gate, state, lockWord, broken, hasLock, lockLevel, lockBroken;
    float hardness, health;
    DoorRec() : flags(0), generation(0), writerSlot(0), gate(0), state(0), lockWord(0), broken(0), hasLock(0), lockLevel(0), lockBroken(0),
                hardness(0.0f), health(0.0f) {}
};

/* ---- the id ---- */
/* the area square a door key names: its "@x,y@" field, as doorsync.h DoorKeySector reads it (at most four digits each). 1 = read. */
inline int DrKeySector(const char* key, int* sx, int* sy)
{
    if (key == 0 || sx == 0 || sy == 0) return 0;
    const char* p = key;
    int x = 0, y = 0, nx = 0, ny = 0;
    while (*p != 0 && *p != '@') ++p;
    if (*p != '@') return 0;
    ++p;
    while (*p >= '0' && *p <= '9') { if (++nx > 4) return 0; x = x * 10 + (*p - '0'); ++p; }
    if (nx == 0 || *p != ',') return 0;
    ++p;
    while (*p >= '0' && *p <= '9') { if (++ny > 4) return 0; y = y * 10 + (*p - '0'); ++p; }
    if (ny == 0 || *p != '@') return 0;
    *sx = x; *sy = y;
    return 1;
}
inline unsigned int DrHash(const std::string& s)
{
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < s.size(); ++i) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}
inline bool DrKeyOk(const std::string& k)
{
    if (k.empty() || k.size() > (size_t)kDrKeyMax) return false;
    for (size_t i = 0; i < k.size(); ++i) { const unsigned char c = (unsigned char)k[i]; if (c < 0x20 || c == 0x7F) return false; }
    return true;
}
/* the door id of a door key; "" when the key is not a door key whose area square is on the grid */
inline std::string MakeDoorRecId(const std::string& key)
{
    int sx = -1, sy = -1;
    if (!DrKeyOk(key) || DrKeySector(key.c_str(), &sx, &sy) == 0 || sx < 0 || sx > kDrMaxSector || sy < 0 || sy > kDrMaxSector) return std::string();
    char b[48];
    std::sprintf(b, "door.%d.%d.#%08x", sx, sy, DrHash(key));
    return std::string(b);
}
/* any spelling of the prefix counts (file names ignore case); only the spelling MakeDoorRecId makes parses */
inline bool IsDoorRecId(const std::string& id)
{
    static const char kPrefix[] = "door.";
    if (id.size() < 5) return false;
    for (size_t i = 0; i < 5; ++i) { char c = id[i]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c != kPrefix[i]) return false; }
    return true;
}
inline int DrParseSector(const std::string& s, size_t* at, char stop, int* out)
{
    size_t i = *at;
    int v = 0, digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9')
    {
        if (digits > 0 && v == 0) return 0;   /* a leading zero: not the one spelling MakeDoorRecId makes */
        v = v * 10 + (s[i] - '0');
        ++digits; ++i;
        if (v > kDrMaxSector) return 0;
    }
    if (digits == 0 || i >= s.size() || s[i] != stop) return 0;
    *at = i + 1;
    *out = v;
    return 1;
}
/* 1 = exactly a spelling MakeDoorRecId makes (its area square into sx / sy when not 0); 0 = not a door id */
inline int DoorRecIdParse(const std::string& id, int* sx, int* sy)
{
    if (id.compare(0, 5, "door.") != 0) return 0;
    size_t at = 5;
    int a = 0, b = 0;
    if (DrParseSector(id, &at, '.', &a) == 0 || DrParseSector(id, &at, '.', &b) == 0) return 0;
    if (at >= id.size() || id[at] != '#') return 0;
    ++at;
    if (id.size() - at != 8) return 0;
    for (size_t i = at; i < id.size(); ++i) { const char c = id[i]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0; }
    if (sx) *sx = a;
    if (sy) *sy = b;
    return 1;
}
/* the plugin's area index of an area square (build.cpp's sx * 64 + sy); -1 off the grid */
inline int DrAreaIndex(int sx, int sy) { return (sx < 0 || sx > kDrMaxSector || sy < 0 || sy > kDrMaxSector) ? -1 : sx * 64 + sy; }

/* ---- validity ---- */
inline bool DrNumberOk(float f) { return f == f && f > -kDrMaxNumber && f < kDrMaxNumber; }
inline bool DrBit(int v) { return v == 0 || v == 1; }
inline bool DoorRecValid(const DoorRec& r)
{
    return (r.flags & ~kDrFlagSeed) == 0 && !MakeDoorRecId(r.key).empty() && DrBit(r.gate) && DrBit(r.state) && r.lockWord >= 0 && r.lockWord <= 3
        && DrBit(r.broken) && DrBit(r.hasLock) && r.lockLevel >= 0 && r.lockLevel <= kDrLevelMax && DrBit(r.lockBroken)
        && DrNumberOk(r.hardness) && DrNumberOk(r.health);
}

/* ---- bytes ---- */
inline void DrPutU32(std::vector<char>* b, unsigned int v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline void DrPutF32(std::vector<char>* b, float v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline bool DrGet4(const char* p, size_t size, size_t* off, void* v)
{
    if (*off > size || size - *off < 4) return false;
    std::memcpy(v, p + *off, 4);
    *off += 4;
    return true;
}
inline bool DrGet1(const char* p, size_t size, size_t* off, int* v)
{
    if (*off >= size) return false;
    *v = (int)(unsigned char)p[*off];
    *off += 1;
    return true;
}
/* false = nothing written (*out emptied): the record breaks DoorRecValid */
inline bool EncodeDoorRec(std::vector<char>* out, const DoorRec& r)
{
    if (out == 0) return false;
    out->clear();
    if (!DoorRecValid(r)) return false;
    DrPutU32(out, kDrMarker);
    DrPutU32(out, kDrFormat);
    DrPutU32(out, r.flags);
    DrPutU32(out, r.generation);
    DrPutU32(out, (unsigned int)r.writerSlot);
    DrPutU32(out, (unsigned int)r.key.size());
    out->insert(out->end(), r.key.begin(), r.key.end());
    out->push_back((char)r.gate); out->push_back((char)r.state); out->push_back((char)r.lockWord); out->push_back((char)r.broken);
    out->push_back((char)r.hasLock);
    DrPutU32(out, (unsigned int)r.lockLevel);
    out->push_back((char)r.lockBroken);
    DrPutF32(out, r.hardness);
    DrPutF32(out, r.health);
    return true;
}
/* kDrOk: *out holds the record; anything else: refused, *out untouched */
inline int DecodeDoorRec(const char* p, size_t size, DoorRec* out)
{
    if (p == 0 || size < 24u) return kDrShort;
    if (size > (size_t)kDrMaxBytes) return kDrTooBig;
    size_t off = 0;
    unsigned int marker = 0, format = 0, len = 0, lv = 0;
    DoorRec r;
    DrGet4(p, size, &off, &marker);
    if (marker != kDrMarker) return kDrBadMarker;
    DrGet4(p, size, &off, &format);
    if (format != kDrFormat) return kDrBadFormat;
    if (!DrGet4(p, size, &off, &r.flags) || !DrGet4(p, size, &off, &r.generation) || !DrGet4(p, size, &off, &r.writerSlot) || !DrGet4(p, size, &off, &len))
        return kDrShort;
    if (len == 0 || len > kDrKeyMax) return kDrBadRecord;
    if (size - off < (size_t)len) return kDrShort;
    r.key.assign(p + off, len);
    off += len;
    if (!DrGet1(p, size, &off, &r.gate) || !DrGet1(p, size, &off, &r.state) || !DrGet1(p, size, &off, &r.lockWord) || !DrGet1(p, size, &off, &r.broken)
        || !DrGet1(p, size, &off, &r.hasLock) || !DrGet4(p, size, &off, &lv) || !DrGet1(p, size, &off, &r.lockBroken)
        || !DrGet4(p, size, &off, &r.hardness) || !DrGet4(p, size, &off, &r.health)) return kDrShort;
    r.lockLevel = (int)lv;
    if (off != size) return kDrTrailing;
    if (!DoorRecValid(r)) return kDrBadRecord;
    if (out) *out = r;
    return kDrOk;
}
inline const char* DoorDecodeWhy(int why)
{
    return why == kDrOk ? "ok" : why == kDrShort ? "cut short" : why == kDrBadMarker ? "not a door record (wrong marker)"
         : why == kDrBadFormat ? "an unknown format" : why == kDrBadRecord ? "a field out of range" : why == kDrTrailing ? "bytes after the last field"
         : why == kDrTooBig ? "larger than the byte cap" : "malformed";
}

/* ---- the world server ---- */
/* One write of door record `id` from slot `senderSlot`; holderSlot: the slot the area map names as the holder of the record's area
   (-1 nobody); exists / storedGen: a stored record and its generation; sameBytes: the bytes equal the stored file. coopbrec::kBrAccept,
   kBrRepeat (nothing to write) or a coopbrec refusal reason. *out (when not 0) gets the decoded record when it read. */
inline int DoorJudgeWrite(const std::string& id, const char* p, size_t size, int senderSlot, int holderSlot, int exists, unsigned int storedGen,
                          int sameBytes, DoorRec* out)
{
    if (DoorRecIdParse(id, 0, 0) == 0) return coopbrec::kBrRefuseBadId;
    DoorRec r;
    if (DecodeDoorRec(p, size, &r) != kDrOk) return coopbrec::kBrRefuseUnreadable;
    if (out != 0) *out = r;
    if (MakeDoorRecId(r.key) != id) return coopbrec::kBrRefuseWrongBox;
    if (senderSlot < 0 || r.writerSlot != senderSlot) return coopbrec::kBrRefuseWriter;
    if (holderSlot < 0 || senderSlot != holderSlot) return coopbrec::kBrRefuseNotWriter;
    if (exists != 0)
    {
        if ((r.flags & kDrFlagSeed) != 0) return coopbrec::kBrRefuseSeedExists;
        if (r.generation < storedGen) return coopbrec::kBrRefuseOlder;
        if (r.generation == storedGen) return (sameBytes != 0) ? coopbrec::kBrRepeat : coopbrec::kBrRefuseSameGen;
    }
    return coopbrec::kBrAccept;
}
/* A removal of door record `id`: only the area's holder removes one (a ruined building's doors). */
inline int DoorJudgeRemove(const std::string& id, int senderSlot, int holderSlot)
{
    if (DoorRecIdParse(id, 0, 0) == 0) return coopbrec::kBrRefuseBadId;
    if (senderSlot < 0 || holderSlot < 0 || senderSlot != holderSlot) return coopbrec::kBrRefuseNotWriter;
    return coopbrec::kBrAccept;
}

/* ---- the game ---- */
/* the area index of a door key: its "@x,y@" field - the area the world server judges the door's record by (its holder is the holder of
   the id's area, which is the key's, not the area the door's position falls in); -1 when the key names no area on the grid */
inline int DrKeyArea(const char* key)
{
    int sx = -1, sy = -1;
    if (DrKeySector(key, &sx, &sy) == 0) return -1;
    return DrAreaIndex(sx, sy);
}
/* which of the record's facts a door's live values change: the open / closed state (a gate's only), the lock word, broken, the lock's
   level or hardness, the lock's broken flag. Health rides with any write and is no trigger of its own (every hit moves it). */
const int kDrChState = 1, kDrChLock = 2, kDrChBroken = 4, kDrChLevel = 8, kDrChLockBroken = 16;
inline int DoorRecChangeMask(int haveRec, const DoorRec& rec, const DoorRec& live)
{
    if (haveRec == 0) return (live.gate != 0 ? kDrChState : 0) | kDrChLock | kDrChBroken | kDrChLevel | kDrChLockBroken;
    int m = 0;
    if (live.gate != 0 && live.state != rec.state) m |= kDrChState;
    if (live.lockWord != rec.lockWord) m |= kDrChLock;
    if (live.broken != rec.broken) m |= kDrChBroken;
    if (live.hasLock != 0 && (live.lockLevel != rec.lockLevel || live.hardness != rec.hardness)) m |= kDrChLevel;
    if (live.hasLock != 0 && live.lockBroken != rec.lockBroken) m |= kDrChLockBroken;
    return m;
}
/* the words of a change mask, for the write's log line */
inline std::string DoorRecChangeWords(int m)
{
    std::string s;
    if (m & kDrChState) s += " open/closed";
    if (m & kDrChLock) s += " lock";
    if (m & kDrChBroken) s += " broken";
    if (m & kDrChLevel) s += " lock-level";
    if (m & kDrChLockBroken) s += " lock-broken";
    return s.empty() ? std::string(" nothing") : s;
}
/* Whether this game writes the record for a door change. weHold: the holder of the key's area is this game (1), another game or nobody
   (0), not known yet (-1); laid: the row's record is laid; mask: DoorRecChangeMask; awaitStored: this door's seed was refused because
   the world has a record, and that record has not arrived yet; keyClash: the record kept under this id names another door (two keys
   hashed to one id). A change made before the lay, while the holder is not known, or while the stored record is awaited is HELD: the
   row is marked with the facts it moved and its lay writes only those over the record it has then (kDrLayWriteHeld,
   DoorRecHeldMerge) - nothing is ever written above a generation this game has not received. A change that moves none of the stored
   facts (an ordinary door's swing) is neither written nor held; mask is measured from the record for a laid door and from the door's
   facts before any held change for one not laid. awaitStored also covers this game's own seed not yet confirmed (DoorRecAwaitWrite). */
const int kDrWrite = 0, kDrSkipNotHolder = 1, kDrHoldUntilLaid = 2, kDrSkipNoChange = 3, kDrSkipKeyClash = 4;
inline int DoorRecWriteDecide(int weHold, int laid, int mask, int awaitStored, int keyClash)
{
    if (keyClash != 0) return kDrSkipKeyClash;
    if (weHold == 0) return kDrSkipNotHolder;
    if (mask == 0) return kDrSkipNoChange;
    if (weHold < 0 || laid == 0 || awaitStored != 0) return kDrHoldUntilLaid;
    return kDrWrite;
}
/* A door at the lay (its area's record list is complete here). haveRec: a record of this door is kept; weHold as above; heldChange: a
   change of this door was held (DoorRecWriteDecide); awaitStored as above. A record with no held change is laid. The holder with a
   held change writes the facts it moved over the record (DoorRecHeldMerge, the newer facts); another game with one lays the record.
   No record: the holder seeds one from its own door, another game shows its own door until the holder's seed arrives. It waits (the
   row is tried again by the event that ends the wait) while a stored record or this game's seed's confirmation is awaited and the
   door has no record or a held change, or while the holder is not known and the answer decides. */
const int kDrLayWait = 0, kDrLayRecord = 1, kDrLaySeed = 2, kDrLayOwn = 3, kDrLayWriteHeld = 4;
inline int DoorRecLayDecide(int haveRec, int weHold, int heldChange, int awaitStored)
{
    if (awaitStored != 0 && (haveRec == 0 || heldChange != 0)) return kDrLayWait;
    if (haveRec != 0 && heldChange == 0) return kDrLayRecord;
    if (weHold < 0) return kDrLayWait;
    if (haveRec != 0) return (weHold == 1) ? kDrLayWriteHeld : kDrLayRecord;
    return (weHold == 1) ? kDrLaySeed : kDrLayOwn;
}
/* the health a lay writes: the record's only when the record's broken flag differs from the door's (a break or a mend carries its
   health); otherwise the door keeps its own - a hit's health alone is never written, so the record's health is no newer than it */
inline float DoorRecLayHealth(int recBroken, int liveBroken, float recHealth, float liveHealth)
{
    return (recBroken != liveBroken) ? recHealth : liveHealth;
}
/* a door laid showing its own (no record; another game or nobody held the key's area): once this game holds that area and still has
   no record, the door is laid again, which seeds it. 1 = lay it again. */
inline int DoorRecOwnRecheck(int ownShown, int haveRec, int weHold) { return (ownShown != 0 && haveRec == 0 && weHold == 1) ? 1 : 0; }
/* the record kept under a door's id, against the door: none kept, kept for this door, or kept for another key (a hash collision -
   that door is neither laid from it nor written, and counted) */
const int kDrKeptNone = 0, kDrKeptSame = 1, kDrKeptClash = 2;
inline int DoorRecKeptFor(int haveKept, int sameKey) { return (haveKept == 0) ? kDrKeptNone : ((sameKey != 0) ? kDrKeptSame : kDrKeptClash); }
/* the state a lay gives the door road's held word: a gate's comes from its record; an ordinary door keeps a word already held for it,
   else its own resting state (opening counts as open, closing as closed) - its swing is each game's own */
inline int DoorRecLayState(int gate, int recState, int heldState, int liveState)
{
    if (gate != 0) return recState;
    if (heldState == 0 || heldState == 1) return heldState;
    return (liveState == 1 || liveState == 2) ? 1 : 0;
}
/* A queued removal of a door record (a ruined building's door, or a rebuilt town's record that names no door): kept while the key's
   area has no complete record list here or its holder is not known (a record not yet arrived is not a missing one); then sent by the
   holder when a record is kept for it, else dropped (none exists, or another game holds the area). A send that fails stays queued. */
const int kDrGoneWait = 0, kDrGoneSend = 1, kDrGoneDrop = 2;
inline int DoorRecGoneDecide(int listDone, int weHold, int haveRec)
{
    if (listDone == 0 || weHold < 0) return kDrGoneWait;
    return (weHold == 1 && haveRec != 0) ? kDrGoneSend : kDrGoneDrop;
}
/* An area whose town was rebuilt here (its saved buildings replaced): once the rebuild has run and the area's record list is complete
   and laid, and its holder is known, the holder removes the area's door records that name no live door here (DoorRecRebuiltRemoveList); another game drops the mark. */
const int kDrRebuiltWait = 0, kDrRebuiltSweep = 1, kDrRebuiltDrop = 2;
inline int DoorRecRebuiltDecide(int rebuiltAndLaid, int weHold)
{
    if (rebuiltAndLaid == 0 || weHold < 0) return kDrRebuiltWait;
    return (weHold == 1) ? kDrRebuiltSweep : kDrRebuiltDrop;
}
/* the next generation this game writes: above the record it knows and above any generation a refusal named */
inline unsigned int DoorRecNextGen(int haveRec, unsigned int gen, unsigned int floorGen)
{
    unsigned int g = (haveRec != 0) ? gen : 0u;
    if (floorGen > g) g = floorGen;
    return g + 1u;
}
/* a received record is kept when nothing is kept for its id or its generation is not below the kept one */
inline int DoorRecArrivalTakes(int haveKept, unsigned int keptGen, unsigned int gen) { return (haveKept == 0 || gen >= keptGen) ? 1 : 0; }
/* What a refusal of this game's write asks: older or the same generation with other bytes - another write is stored past it, so the
   change is written again above it (at most kDrRewriteCap times per record); a seed where a record exists - the stored record wins:
   the world server sends it back, it is laid, and nothing of this door is written until it has arrived; anything else - dropped. */
const int kDrRefuseRewrite = 0, kDrRefuseTakeStored = 1, kDrRefuseDrop = 2;
const int kDrRewriteCap = 3;
inline int DoorRecRefuseNext(int reason, int rewrites)
{
    if (reason == coopbrec::kBrRefuseOlder || reason == coopbrec::kBrRefuseSameGen) return rewrites < kDrRewriteCap ? kDrRefuseRewrite : kDrRefuseDrop;
    if (reason == coopbrec::kBrRefuseSeedExists) return kDrRefuseTakeStored;
    return kDrRefuseDrop;
}
/* A write refused because this game is not the door's writer while no record of the door is kept here (a seed sent on the area-map
   presumption before the world server named this game the area's holder - T1819): the door shows its own again, so the area map that
   names this game the holder seeds it (DoorsRecMapEvent); a refused change of a door that has a record keeps the record. 1 = shows its
   own. */
inline int DoorRecRefuseShowsOwn(int reason, int haveAfter) { return (reason == coopbrec::kBrRefuseNotWriter && haveAfter == 0) ? 1 : 0; }
/* The kept copy of a door record is this game's newest write as soon as it is sent (the next change is measured and numbered from it),
   and the last record that arrived from the world server is kept as the confirmed copy. A refusal of a write above the confirmed copy
   while this game's writes are pending (not taken back by an arrival since) puts the confirmed copy back, so a write the world server
   never took is not kept as the world's, whichever pending write was refused. 1 = put it back. */
inline int DoorRecRefuseRestores(int pending, int confHave, unsigned int confGen, unsigned int sentGen)
{
    return (pending != 0 && (confHave == 0 || sentGen > confGen)) ? 1 : 0;
}
/* The facts the holder writes at the lay for a change held before it: the stored facts the held changes moved (heldMask, the
   DoorRecChangeMask bits) from the door's live values, every other fact from the record - the save's lock, level or health never ride
   above the world's record. Health comes with the broken flag (a break or a mend carries it), else the record's. An ordinary door's
   swing is no bit, so it never rides. */
inline DoorRec DoorRecHeldMerge(const DoorRec& rec, const DoorRec& live, int heldMask)
{
    DoorRec m = rec;
    if ((heldMask & kDrChState) != 0 && live.gate != 0) m.state = live.state;
    if ((heldMask & kDrChLock) != 0) m.lockWord = live.lockWord;
    if ((heldMask & kDrChBroken) != 0) { m.broken = live.broken; m.health = live.health; }
    if ((heldMask & kDrChLevel) != 0 && live.hasLock != 0) { m.hasLock = live.hasLock; m.lockLevel = live.lockLevel; m.hardness = live.hardness; }
    if ((heldMask & kDrChLockBroken) != 0 && live.hasLock != 0) { m.hasLock = live.hasLock; m.lockBroken = live.lockBroken; }
    return m;
}
/* this game's own seed, sent and not yet confirmed (the world server's answer, DoorRecEchoConfirms): nothing is written above it, as while a refused seed's stored record
   is awaited - if the world server holds another game's seed, a write above this game's would be taken over theirs and the stored
   record that follows the refusal dropped as older. 1 = unconfirmed. */
inline int DoorRecSeedUnconfirmed(int pending, unsigned int flags) { return (pending != 0 && (flags & kDrFlagSeed) != 0) ? 1 : 0; }
/* a change of this door waits (is held) rather than written: a refused seed's stored record, or this game's unconfirmed seed */
inline int DoorRecAwaitWrite(int awaitStored, int pending, unsigned int flags)
{
    return (awaitStored != 0 || DoorRecSeedUnconfirmed(pending, flags) != 0) ? 1 : 0;
}
/* the world server's answer to this game's taken write (RECORD_SEQ) names the stamp the write was sent with: it confirms the pending
   write sent with that stamp (the kept copy is what was sent, and becomes the confirmed copy). Another stamp, no stamp, no stamp kept
   or no pending write confirms nothing. 1 = confirmed. */
inline int DoorRecEchoConfirms(int pending, long long sentStamp, int hasStamp, long long echoStamp)
{
    return (pending != 0 && hasStamp != 0 && sentStamp != 0 && echoStamp == sentStamp) ? 1 : 0;
}
/* A rebuilt town's area: the kept records whose key names no live door here are removed - no row, and a complete walk of the loaded
   buildings finds no door for it (kDrResolveNoDoor); a live door, or a walk that could not tell, keeps the record. ids[i] is judged by
   resolve[i]; returns the ids to remove. */
const int kDrResolveFound = 0, kDrResolveNoDoor = 1, kDrResolveUnsure = 2;
inline std::vector<std::string> DoorRecRebuiltRemoveList(const std::vector<std::string>& ids, const std::vector<int>& resolve)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < ids.size() && i < resolve.size(); ++i)
        if (resolve[i] == kDrResolveNoDoor) out.push_back(ids[i]);
    return out;
}
/* ONE RECORD PER DOOR PER TICK. A door can change more than once inside one engine update (a lever lock of an open door: the door
   shuts, then the engine's lock lands on it), and a stored fact can be written straight into a door (its lock level). Every such change
   of the tick is gathered as an entry and merged here: each door gets ONE write, in the order of its first entry, with its final state
   and lock word (the last entry that carries each; -1 = read off the door at the write) and the facts moved between the tick's own
   entries (moved: kDrChState, kDrChLock). The facts moved since the door's record are measured at the write itself. */
struct DoorRecTickEntry { std::string key; int state; int lockWord; };
struct DoorRecTickWrite { std::string key; int state; int lockWord; int entries; int moved; };
inline DoorRecTickEntry DoorRecTickMake(const std::string& key, int state, int lockWord)
{
    DoorRecTickEntry e;
    e.key = key; e.state = state; e.lockWord = lockWord;
    return e;
}
inline std::vector<DoorRecTickWrite> DoorRecTickMerge(const std::vector<DoorRecTickEntry>& in)
{
    std::vector<DoorRecTickWrite> out;
    for (size_t i = 0; i < in.size(); ++i)
    {
        const DoorRecTickEntry& e = in[i];
        if (e.key.empty()) continue;
        size_t j = 0;
        while (j < out.size() && out[j].key != e.key) ++j;
        if (j == out.size())
        {
            DoorRecTickWrite n;
            n.key = e.key; n.state = -1; n.lockWord = -1; n.entries = 0; n.moved = 0;
            out.push_back(n);
        }
        DoorRecTickWrite& w = out[j];
        if (e.state >= 0)
        {
            if (w.state >= 0 && w.state != e.state) w.moved |= kDrChState;
            w.state = e.state;
        }
        if (e.lockWord >= 0)
        {
            if (w.lockWord >= 0 && w.lockWord != e.lockWord) w.moved |= kDrChLock;
            w.lockWord = e.lockWord;
        }
        ++w.entries;
    }
    return out;
}
/* the facts a merged write names: those it moved since the door's record (mask) and those moved between the tick's entries (tickMoved;
   the open / closed state only for a gate, the one door whose state is a stored fact). Nothing when the write moved nothing since the
   record - a change and its undo inside one tick is no change, and no write. */
inline int DoorRecTickWords(int mask, int tickMoved, int gate)
{
    if (mask == 0) return 0;
    return mask | (tickMoved & ((gate != 0) ? (kDrChState | kDrChLock) : kDrChLock));
}

}   /* namespace doorrec */
