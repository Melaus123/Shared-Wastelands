/* src/common/ownerwire.h - inv6 phase 1 (investigations/inv6-stolen-mark-design.md Q1, Q2): THE STOLEN MARK, IN WORDS BOTH GAMES UNDERSTAND.
 *
 * When a player steals, the engine writes the item's "belongs to" hand at Item+0x140 (`properOwner`). A hand is a per-process ticket
 * (index/serial), so it can never travel raw. It travels as an OWNER IDENTITY instead:
 *
 *   kind  0 none (the block is not sent at all), 1 a character, 2 a squad (platoon), 3 another object type
 *   uid   kind 1: the character's replicated uid; kind 2: the squad LEADER's uid; 0 = not tracked / not found on the sender
 *   sid   the owner's faction wire name (relations' SidOf: an FCS stringID, or "@slot:<n>[:<name>]" for a player faction)
 *
 * WIRE (game protocol 71): a tagged block, appended only when kind != 0 -
 *   marker u32 'OWN1' | u8 kind (1..3) | u32 uid | str sid (u32 len <= kOwnMaxSid + bytes)
 * MSG_ITEM_MOVE op 0: after the box key/id. MSG_ITEM_REQUEST (a GIVE) and MSG_ITEM_CONFIRM (a TAKE, ok 1): after the optional BAG1
 * block. A block that is there but not whole and valid REFUSES THE MESSAGE, as BAG1 does.
 * JOURNAL (the offline box-move journal, items.cpp ItBoxMoveToBytes): a "b2" line is a "b1" line plus kind, uid and sid as three more
 * tab fields; a "b1" line reads as no owner. No store protocol change: the journal's bytes are opaque to the store.
 *
 * Also here, for phase 2/3 and the offline suite: the deterministic "unknown owner" (tombstone) index/serial (GATED - not used by
 * the plugin until the resolver read of design Q1 step 3 is done) and the box digest's owner term (phase 2 parity).
 * PHASE 2 (game protocol 73): an OWNER TAIL after a LIST - the PARITY_BOX items, the BAG1 rows, the CLOTHING 'QLT1' entries - naming
 * only the entries that are marked, by their index in the list:
 *   marker u32 'OWNL' | u32 n (1..kOwnTailMax, <= the list's count) | n x (u32 index, strictly rising, < count | u8 kind 1..3 |
 *   u32 uid | u32 len <= kOwnMaxSid + sid)
 * Written only when at least one entry is marked, so a list with no marked entry is byte-identical to protocol 72's.
 * Pure: no engine memory, no Windows. C++03 (VS2010 v100).
 */
#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "slotwire.h"   /* a player faction's wire name, "@slot:<n>[:<name>]" */

namespace coopmark {

const unsigned int kOwnMarker = 0x314E574Fu;   /* 'O','W','N','1' little-endian */
const unsigned int kOwnMaxSid = 95u;           /* SidOf names: a stringID or "@slot:<n>:<name>" (names are <= 24 chars) */
const unsigned char kOwnNone = 0, kOwnCharacter = 1, kOwnSquad = 2, kOwnOther = 3;

/* The engine's itemType values a hand's `type` carries. */
const unsigned int kHandTypeCharacter = 1u;
const unsigned int kHandTypeNull = 11u;        /* RECORD_NONE: "no owner" */
const unsigned int kHandTypePlatoon = 34u;

const int kOwnOk = 0;
const int kOwnAbsent = 1;   /* no bytes left, or the next bytes are not an OWN1 block (the caller decides what that means) */
const int kOwnBad = 2;      /* an OWN1 marker followed by a block that is cut or out of range */

struct OwnerId
{
    unsigned char kind;
    unsigned int uid;
    std::string factionSid;
    OwnerId() : kind(0), uid(0) {}
};

inline bool OwnerEqual(const OwnerId& a, const OwnerId& b)
{
    return a.kind == b.kind && a.uid == b.uid && a.factionSid == b.factionSid;
}

/* The raw 5-word hand as the plugin reads it: {type, container, containerStamp, index, serial} (hand +0x8 .. +0x1C). An owner is
   there when the type is not RECORD_NONE and the ticket is not all zero (a default-constructed hand). */
inline bool RawHandIsOwner(const unsigned int* w5)
{
    if (w5 == 0) return false;
    if (w5[0] == kHandTypeNull) return false;
    return !(w5[3] == 0u && w5[4] == 0u);
}
inline unsigned char OwnerKindOfHandType(unsigned int type)
{
    if (type == kHandTypeNull) return kOwnNone;
    if (type == kHandTypeCharacter) return kOwnCharacter;
    if (type == kHandTypePlatoon) return kOwnSquad;
    return kOwnOther;
}
inline const char* OwnerKindName(unsigned char kind)
{
    return kind == kOwnNone ? "none" : kind == kOwnCharacter ? "char" : kind == kOwnSquad ? "squad" : kind == kOwnOther ? "other" : "?";
}

/* A sid is printable ASCII with no tab / newline / control byte (it also rides a tab-separated journal line). */
inline bool OwnerSidOk(const std::string& s)
{
    if (s.size() > kOwnMaxSid) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}
/* The one aliveStamp rule both ends apply. kind 0 carries nothing. */
inline bool OwnerValid(const OwnerId& o)
{
    if (o.kind > kOwnOther) return false;
    if (o.kind == kOwnNone) return o.uid == 0 && o.factionSid.empty();
    return OwnerSidOk(o.factionSid);
}

inline void OwnPutU32(std::vector<char>* b, unsigned int v)
{
    const size_t at = b->size();
    b->resize(at + 4);
    std::memcpy(&(*b)[at], &v, 4);
}

/* Appends the block when kind != 0; kind 0 appends NOTHING and succeeds (a clean item's message is byte-identical to protocol 70's).
   An invalid identity appends nothing and fails - the caller sends the item clean and counts it. */
inline bool EncodeOwner(std::vector<char>* b, const OwnerId& o)
{
    if (b == 0 || !OwnerValid(o)) return false;
    if (o.kind == kOwnNone) return true;
    OwnPutU32(b, kOwnMarker);
    b->push_back((char)o.kind);
    OwnPutU32(b, o.uid);
    OwnPutU32(b, (unsigned int)o.factionSid.size());
    b->insert(b->end(), o.factionSid.begin(), o.factionSid.end());
    return true;
}

/* 1 when the four bytes at `at` are the OWN1 marker. */
inline bool OwnerBlockAt(const char* p, size_t size, size_t at)
{
    if (p == 0 || at > size || size - at < 4) return false;
    unsigned int m = 0;
    std::memcpy(&m, p + at, 4);
    return m == kOwnMarker;
}

/* Reads the block at `at`. kOwnAbsent when there are no bytes or they are not an OWN1 block (*out untouched); kOwnBad when the
   marker is there but the rest is cut or invalid (*out untouched); kOwnOk: *out is the owner and *end the offset after the block. */
inline int DecodeOwner(const char* p, size_t size, size_t at, OwnerId* out, size_t* end)
{
    if (!OwnerBlockAt(p, size, at)) return kOwnAbsent;
    size_t off = at + 4;
    if (size - off < 1 + 4 + 4) return kOwnBad;
    OwnerId o;
    o.kind = (unsigned char)p[off]; off += 1;
    std::memcpy(&o.uid, p + off, 4); off += 4;
    unsigned int len = 0;
    std::memcpy(&len, p + off, 4); off += 4;
    if (len > kOwnMaxSid || size - off < (size_t)len) return kOwnBad;
    o.factionSid.assign(p + off, len); off += len;
    if (o.kind == kOwnNone || !OwnerValid(o)) return kOwnBad;
    if (out) *out = o;
    if (end) *end = off;
    return kOwnOk;
}
inline const char* OwnerDecodeWhy(int why)
{
    return why == kOwnOk ? "ok" : why == kOwnAbsent ? "absent" : "malformed";
}

/* ---- the offline box-move journal's three extra fields ("b2") ---- */
inline std::string OwnerJournalTail(const OwnerId& o)
{
    char b[48];
    std::sprintf(b, "%u\t%u\t", (unsigned int)o.kind, o.uid);
    return std::string(b) + o.factionSid;
}
inline bool OwnerNumField(const std::string& s, unsigned int maxV, unsigned int* v)
{
    if (s.empty() || s.size() > 10) return false;
    unsigned long long a = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9') return false;
        a = a * 10ull + (unsigned long long)(s[i] - '0');
    }
    if (a > (unsigned long long)maxV) return false;
    *v = (unsigned int)a;
    return true;
}
/* A "b2" line's three fields back into an owner; a kind-0 owner is refused here (b2 is written only with an owner). */
inline bool OwnerFromJournal(const std::string& kindF, const std::string& uidF, const std::string& sidF, OwnerId* out)
{
    unsigned int k = 0, u = 0;
    if (!OwnerNumField(kindF, 3u, &k) || !OwnerNumField(uidF, 0xFFFFFFFFu, &u)) return false;
    OwnerId o;
    o.kind = (unsigned char)k; o.uid = u; o.factionSid = sidF;
    if (o.kind == kOwnNone || !OwnerValid(o)) return false;
    if (out) *out = o;
    return true;
}

/* ---- D5-2: which mark a TAKE's rebuilt item gets on the taker ----
   The holder's confirm carries an owner only when its item was ALREADY stolen before it sat in the box - that one wins. Otherwise
   the taker's own engine marked its local object a moment before the drain undid the move, and that local hand (valid on THIS
   game) is written back. 0 nothing, 1 the confirm's owner, 2 the local hand. */
const int kTakerOwnNone = 0, kTakerOwnWire = 1, kTakerOwnLocal = 2;
/* review-inv6 LOW (2026-09-26): a confirm owner that cannot be resolved HERE (kind 3, uid 0, or a uid this game does not hold -
   `confirmResolvesHere` false) would leave the item clean, so a valid local mark wins over it. With no local mark it still goes to
   the apply, which counts it unresolved. */
inline int OwnerTakerChoice(const OwnerId& fromConfirm, bool confirmResolvesHere, bool haveLocal)
{
    if (fromConfirm.kind != kOwnNone && (confirmResolvesHere || !haveLocal)) return kTakerOwnWire;
    return haveLocal ? kTakerOwnLocal : kTakerOwnNone;
}
/* review-inv6p2 MEDIUM (2026-09-26): WHEN a created pack takes its own mark. ContainerItem's setter 0x76B340 marks every CLEAN row
   already inside the pack. A mark from the WIRE (OWN1 on the request / the confirm) goes on BEFORE the fill, onto the empty pack, so
   rows that arrived clean or with their own exact marks keep them (as GrBuild's ground path does). Only the local D5-2 mark (the take
   itself is the theft) goes on AFTER the fill, where the engine spreads it to the clean contents. 1 = after the fill. */
inline int OwnerPackMarkAfterFill(bool hasRows, const OwnerId& wire, bool haveLocal)
{
    return (hasRows && wire.kind == kOwnNone && haveLocal) ? 1 : 0;
}

/* ---- owner decision 396: a player's mark on GROUND items ----
   The engine gives a dropped item its dropper as owner. On another game that character belongs to the dropper's stand-in faction,
   so picking the copy up there would be theft. Does this owner name a PLAYER - a character, squad or object whose faction travels as
   a player's wire name ("@slot:<n>[:<name>]": a player's own faction or a stand-in, coopslot::IsSlotWire)? An NPC faction's
   stringID, an owner with no faction (not found on the sender), or no owner: false. */
inline bool OwnerNamesPlayer(const OwnerId& o) { return o.kind != kOwnNone && coopslot::IsSlotWire(o.factionSid); }

/* ---- phase 2 / GATED ---- */
inline unsigned int OwnFnv(unsigned int h, const char* p, size_t n)
{
    for (size_t i = 0; i < n; ++i) { h ^= (unsigned char)p[i]; h *= 16777619u; }
    return h;
}
/* Design Q1 step 3: the deterministic ticket of an "unknown owner" (tombstone) hand - container 0xFFFFFFFF, index and serial from
   (kind, uid, sid). Equal identities give equal tickets, so two stacks from the same unknown owner still merge. The index has its
   top bit set and the serial is never 0. GATED: the plugin does not write tombstones until resolver 0x2844D0 is read on such a hand. */
inline void OwnerTombstone(const OwnerId& o, unsigned int* index, unsigned int* serial)
{
    unsigned int h = 2166136261u;
    const unsigned char k = o.kind;
    h = OwnFnv(h, (const char*)&k, 1);
    h = OwnFnv(h, (const char*)&o.uid, 4);
    h = OwnFnv(h, o.factionSid.data(), o.factionSid.size());
    unsigned int s = OwnFnv(h ^ 0x9E3779B9u, "tomb", 4);
    if (s == 0u) s = 1u;
    if (index) *index = h | 0x80000000u;
    if (serial) *serial = s;
}
/* Phase 2 (R4): the box digest's owner term - empty for a clean item, so clean boxes keep today's digest. */
inline std::string OwnerDigestTerm(const OwnerId& o)
{
    if (o.kind == kOwnNone) return std::string();
    char b[16];
    std::sprintf(b, "|o%u:", (unsigned int)o.kind);
    return std::string(b) + o.factionSid;
}

/* ---- phase 2 (protocol 73): the OWNER TAIL after a list ---- */
const unsigned int kOwnTailMarker = 0x4C4E574Fu;   /* 'O','W','N','L' little-endian */
const unsigned int kOwnTailMax = 1024u;            /* the largest list it follows (PARITY_BOX: cooppar::kParityMaxItems) */

/* How many of `owners` a tail would carry: kind != 0 and a valid identity. */
inline unsigned int OwnerTailMarked(const std::vector<OwnerId>& owners)
{
    unsigned int n = 0;
    for (size_t i = 0; i < owners.size(); ++i) if (owners[i].kind != kOwnNone && OwnerValid(owners[i])) ++n;
    return n;
}
/* review-inv6p2 LOW (2026-09-26): an "owner unknown" mark - the sender did not find its own owner, so it travels as kind + uid 0 +
   no faction (ItOwnerTranslate's ownerSenderUnknown). A list's rows book it once, when the list is sent. */
inline bool OwnerUnknown(const OwnerId& o) { return o.kind != kOwnNone && o.uid == 0 && o.factionSid.empty(); }
inline unsigned int OwnerTailUnknown(const std::vector<OwnerId>& owners)
{
    unsigned int n = 0;
    for (size_t i = 0; i < owners.size(); ++i) if (OwnerUnknown(owners[i]) && OwnerValid(owners[i])) ++n;
    return n;
}
/* Appends the tail for the marked, valid entries of `owners` (index = position in the list). NOTHING when none is marked, or when the
   list is over kOwnTailMax (every entry then travels clean). Returns the number of entries written; `*invalid` (may be 0) counts
   marked entries whose identity is not valid - they travel clean. */
inline unsigned int EncodeOwnerTail(std::vector<char>* b, const std::vector<OwnerId>& owners, unsigned int* invalid)
{
    if (invalid) *invalid = 0;
    if (b == 0 || owners.size() > (size_t)kOwnTailMax) return 0;
    unsigned int n = 0;
    for (size_t i = 0; i < owners.size(); ++i)
    {
        if (owners[i].kind == kOwnNone) continue;
        if (!OwnerValid(owners[i])) { if (invalid) ++*invalid; continue; }
        ++n;
    }
    if (n == 0) return 0;
    OwnPutU32(b, kOwnTailMarker);
    OwnPutU32(b, n);
    for (size_t i = 0; i < owners.size(); ++i)
    {
        const OwnerId& o = owners[i];
        if (o.kind == kOwnNone || !OwnerValid(o)) continue;
        OwnPutU32(b, (unsigned int)i);
        b->push_back((char)o.kind);
        OwnPutU32(b, o.uid);
        OwnPutU32(b, (unsigned int)o.factionSid.size());
        b->insert(b->end(), o.factionSid.begin(), o.factionSid.end());
    }
    return n;
}
/* 1 when the four bytes at `at` are the OWNL marker. */
inline bool OwnerTailAt(const char* p, size_t size, size_t at)
{
    if (p == 0 || at > size || size - at < 4) return false;
    unsigned int m = 0;
    std::memcpy(&m, p + at, 4);
    return m == kOwnTailMarker;
}
/* Reads the tail at `at` for a list of `count` entries. kOwnAbsent: no bytes, or not an OWNL marker (*out untouched). kOwnBad: the
   marker is there but the tail is cut, n is 0 or over the cap or the count, an index is not strictly rising or not below `count`, or an
   identity is invalid (*out untouched). kOwnOk: *out holds `count` entries (clean where the tail names none), *end the offset after. */
inline int DecodeOwnerTail(const char* p, size_t size, size_t at, size_t count, std::vector<OwnerId>* out, size_t* end)
{
    if (!OwnerTailAt(p, size, at)) return kOwnAbsent;
    size_t off = at + 4;
    unsigned int n = 0;
    if (size - off < 4) return kOwnBad;
    std::memcpy(&n, p + off, 4); off += 4;
    if (n == 0 || n > kOwnTailMax || (size_t)n > count) return kOwnBad;
    std::vector<OwnerId> v(count);
    long long last = -1;
    for (unsigned int k = 0; k < n; ++k)
    {
        if (size - off < 4 + 1 + 4 + 4) return kOwnBad;
        unsigned int idx = 0, len = 0;
        OwnerId o;
        std::memcpy(&idx, p + off, 4); off += 4;
        o.kind = (unsigned char)p[off]; off += 1;
        std::memcpy(&o.uid, p + off, 4); off += 4;
        std::memcpy(&len, p + off, 4); off += 4;
        if (len > kOwnMaxSid || size - off < (size_t)len) return kOwnBad;
        o.factionSid.assign(p + off, len); off += len;
        if ((long long)idx <= last || (size_t)idx >= count) return kOwnBad;
        if (o.kind == kOwnNone || !OwnerValid(o)) return kOwnBad;
        last = (long long)idx;
        v[idx] = o;
    }
    if (out) out->swap(v);
    if (end) *end = off;
    return kOwnOk;
}

}   /* namespace coopmark */
