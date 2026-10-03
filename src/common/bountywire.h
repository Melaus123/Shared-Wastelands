/* src/common/bountywire.h - crime5 (docs/design-crime.md section 3 C): A CHARACTER'S BOUNTIES BETWEEN ITS OWNER AND ITS COPIES.
 *
 * Kenshi keeps a character's bounties in a per-character map, one entry per law faction (BountyManager at Character+0xF0;
 * cloud/ANSWERS.md crime4 1, Read): amount, a mask of the crimes, a claimed flag and the assignment time. A guard adds to the
 * map of the character it SEES - on another game's character that is this game's COPY - so without this a bounty stays on
 * the guard's game only. The owner's map is the truth:
 *   kind 0 (owner -> copies): the owner's whole list; the copy's map is made equal to it.
 *   kind 1 (copy -> owner):   what a guard here ADDED to the copy since the owner's last list (amount = the increase,
 *                             crimes = the bits that were not set); the owner adds it to its own map and sends its list.
 *   kind 2 (copy -> owner):   par20 (parity P20) - what this game TOOK OFF the copy since the owner's last list (a bounty
 *                             claimed, paid off, pardoned, or the character turned in here): amount = the amount taken
 *                             off, crimes = the crime bits cleared, claimed = flags (1 the entry became claimed, 2 it is
 *                             gone); the owner takes the same off its own map, so its next list - and every 30-s re-send -
 *                             carries the cleared state instead of putting the bounty back to be collected again.
 *
 *   uid u32 | kind u8 | n u8 (0..kBountyMaxEntries) | n x { len u8 (1..kBountyMaxSid) + faction sid | amount i32 |
 *                                                          crimes u32 | claimed u8 | time f64 }
 *
 * The faction is the RESOLVED bounty faction as a relations sid (a stringID). A player faction ("@...") never travels
 * (design-crime.md section 4 decision 4 is the user's). Pure: no engine memory, no Windows; the offline suite hits the same
 * bytes and the same decisions the plugin uses (crime.cpp). C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace coopbounty {

const unsigned int kBountyMaxSid = 96;
const unsigned int kBountyMaxEntries = 16;
const int kBountyMaxAmount = 100000000;      /* a larger or negative amount is refused on decode */
const unsigned char kBountyKindList = 0;
const unsigned char kBountyKindAdd  = 1;
const unsigned char kBountyKindClear = 2;       /* par20: copy -> owner, what the copy's game took off */
const unsigned char kBountyClearClaimed = 1;    /* kind 2's claimed byte: the copy's entry became claimed */
const unsigned char kBountyClearErased  = 2;    /* kind 2's claimed byte: the copy's entry is gone */
const unsigned char kBountyClearFlagMask = 3;

const int kBountyDecodeOk        = 0;
const int kBountyDecodeTooShort  = 1;   /* fewer than the 6 fixed bytes, or an entry cut short */
const int kBountyDecodeBadKind   = 2;
const int kBountyDecodeTooMany   = 3;   /* n > kBountyMaxEntries */
const int kBountyDecodeBadSid    = 4;   /* sid length 0 or > kBountyMaxSid */
const int kBountyDecodeBadAmount = 5;
const int kBountyDecodeBadTime   = 6;   /* NaN or infinite (by its bits) */

struct BountyEntry
{
    std::string sid;
    int amount;
    unsigned int crimes;
    unsigned char claimed;
    double time;
    BountyEntry() : amount(0), crimes(0), claimed(0), time(0.0) {}
};
typedef std::vector<BountyEntry> BountyList;

inline bool BountyTimeOk(double t)
{
    unsigned long long b = 0;
    std::memcpy(&b, &t, 8);
    return (b & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
}

/* false (nothing appended) for a bad kind, too many entries, an empty / too long sid or an amount out of range. */
inline bool EncodeBounty(std::vector<char>* b, unsigned int uid, unsigned char kind, const BountyList& list)
{
    if (b == 0 || (kind != kBountyKindList && kind != kBountyKindAdd && kind != kBountyKindClear) || list.size() > (size_t)kBountyMaxEntries) return false;
    size_t need = 6;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const BountyEntry& e = list[i];
        if (e.sid.empty() || e.sid.size() > (size_t)kBountyMaxSid || e.amount < 0 || e.amount > kBountyMaxAmount || !BountyTimeOk(e.time))
            return false;
        need += 1 + e.sid.size() + 4 + 4 + 1 + 8;
    }
    size_t at = b->size();
    b->resize(at + need);
    char* p = &(*b)[at];
    std::memcpy(p, &uid, 4); p[4] = (char)kind; p[5] = (char)list.size(); p += 6;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const BountyEntry& e = list[i];
        *p++ = (char)e.sid.size();
        std::memcpy(p, e.sid.data(), e.sid.size()); p += e.sid.size();
        std::memcpy(p, &e.amount, 4); p += 4;
        std::memcpy(p, &e.crimes, 4); p += 4;
        *p++ = (char)(kind == kBountyKindClear ? (e.claimed & kBountyClearFlagMask) : (e.claimed ? 1 : 0));   /* par20: kind 2 flags */
        std::memcpy(p, &e.time, 8); p += 8;
    }
    return true;
}

/* Every length test is `size - off < n` with off already <= size, so a hostile length cannot wrap. */
inline int DecodeBounty(const char* p, size_t size, unsigned int* uid, unsigned char* kind, BountyList* out)
{
    if (p == 0 || size < 6) return kBountyDecodeTooShort;
    unsigned int u = 0;
    std::memcpy(&u, p, 4);
    const unsigned char k = (unsigned char)p[4];
    const unsigned int n = (unsigned char)p[5];
    if (k != kBountyKindList && k != kBountyKindAdd && k != kBountyKindClear) return kBountyDecodeBadKind;
    if (n > kBountyMaxEntries) return kBountyDecodeTooMany;
    BountyList list;
    size_t off = 6;
    for (unsigned int i = 0; i < n; ++i)
    {
        if (size - off < 1) return kBountyDecodeTooShort;
        const unsigned int len = (unsigned char)p[off]; off += 1;
        if (len == 0 || len > kBountyMaxSid) return kBountyDecodeBadSid;
        if (size - off < (size_t)len + 17) return kBountyDecodeTooShort;
        BountyEntry e;
        e.sid.assign(p + off, len); off += len;
        std::memcpy(&e.amount, p + off, 4); off += 4;
        std::memcpy(&e.crimes, p + off, 4); off += 4;
        e.claimed = (unsigned char)(k == kBountyKindClear ? ((unsigned char)p[off] & kBountyClearFlagMask) : (p[off] ? 1 : 0)); off += 1;
        std::memcpy(&e.time, p + off, 8); off += 8;
        if (e.amount < 0 || e.amount > kBountyMaxAmount) return kBountyDecodeBadAmount;
        if (!BountyTimeOk(e.time)) return kBountyDecodeBadTime;
        list.push_back(e);
    }
    if (uid) *uid = u;
    if (kind) *kind = k;
    if (out) out->swap(list);
    return kBountyDecodeOk;
}

inline const BountyEntry* BountyFind(const BountyList& l, const std::string& sid)
{
    for (size_t i = 0; i < l.size(); ++i) if (l[i].sid == sid) return &l[i];
    return 0;
}

/* The owner's change test: the same factions with the same amount, crimes and claimed flag (the time rides along). */
inline bool BountyListsEqual(const BountyList& a, const BountyList& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const BountyEntry* o = BountyFind(b, a[i].sid);
        if (o == 0 || o->amount != a[i].amount || o->crimes != a[i].crimes || o->claimed != a[i].claimed) return false;
    }
    return true;
}

/* The copy's additions since the owner's last list `known`: per faction, the amount increase and the crime bits that were not
   set (time = the copy's). A lower amount, a cleared crime bit or a missing faction is never an addition - par20: it is a
   REMOVAL (BountyRemovals below), sent to the owner as kind 2. */
inline BountyList BountyAdditions(const BountyList& known, const BountyList& cur)
{
    BountyList add;
    for (size_t i = 0; i < cur.size() && add.size() < (size_t)kBountyMaxEntries; ++i)
    {
        const BountyEntry* k = BountyFind(known, cur[i].sid);
        const int was = k ? k->amount : 0;
        const unsigned int wasCrimes = k ? k->crimes : 0u;
        const int delta = cur[i].amount - was;
        const unsigned int newBits = cur[i].crimes & ~wasCrimes;
        if (delta <= 0 && newBits == 0) continue;
        BountyEntry e = cur[i];
        e.amount = delta > 0 ? delta : 0;
        e.crimes = newBits;
        e.claimed = 0;
        add.push_back(e);
    }
    return add;
}

/* par20 (parity P20): the copy's REMOVALS since the owner's last list `known` - per faction, the amount taken off, the crime
   bits cleared and the flags (kBountyClearClaimed: the entry became claimed; kBountyClearErased: it is gone). A bounty
   claimed, paid off, pardoned or the character turned in on the NON-owner's game changes only the copy; without this the
   owner's next list (re-sent every 30 s) put it back, to be collected again. A faction missing from a FULL list
   (kBountyMaxEntries: the wire cap may have cut it) is not called gone. time = the known entry's. */
inline BountyList BountyRemovals(const BountyList& known, const BountyList& cur)
{
    BountyList out;
    for (size_t i = 0; i < known.size() && out.size() < (size_t)kBountyMaxEntries; ++i)
    {
        const BountyEntry& k = known[i];
        if (k.amount == 0 && k.crimes == 0) continue;
        const BountyEntry* c = BountyFind(cur, k.sid);
        BountyEntry e = k;
        if (c == 0)
        {
            if (cur.size() >= (size_t)kBountyMaxEntries) continue;
            e.claimed = kBountyClearErased;   /* amount = all of it, crimes = all of them */
            out.push_back(e);
            continue;
        }
        const int taken = k.amount - c->amount;
        const unsigned int gone = k.crimes & ~c->crimes;
        const bool nowClaimed = c->claimed != 0 && k.claimed == 0;
        if (taken <= 0 && gone == 0 && !nowClaimed) continue;
        e.amount = taken > 0 ? taken : 0;
        e.crimes = gone;
        e.claimed = nowClaimed ? kBountyClearClaimed : 0;
        out.push_back(e);
    }
    return out;
}

const int kBountyClearNone  = 0;
const int kBountyClearWrite = 1;
const int kBountyClearErase = 2;
struct BountyClearOutcome { int action; BountyEntry entry; BountyClearOutcome() : action(kBountyClearNone) {} };

/* par20: the owner's decision for one kind-2 entry against its own entry `own` (0 = it has none: already cleared, nothing
   to do). The amount taken off on the other game comes off here (never below 0) - so a bounty the owner added after the
   copy's last list survives as the remainder - the cleared crime bits go and a claim sticks. The entry is erased when no
   amount is left and either the copy's entry is gone or no crime bit is left. */
inline BountyClearOutcome BountyClearApply(const BountyEntry* own, const BountyEntry& clr)
{
    BountyClearOutcome o;
    if (own == 0) return o;
    BountyEntry n = *own;
    const long long left = (long long)own->amount - (long long)(clr.amount > 0 ? clr.amount : 0);
    n.amount = left > 0 ? (int)left : 0;
    n.crimes = own->crimes & ~clr.crimes;
    if (clr.claimed & kBountyClearClaimed) n.claimed = 1;
    o.entry = n;
    if (n.amount == 0 && ((clr.claimed & kBountyClearErased) != 0 || n.crimes == 0)) { o.action = kBountyClearErase; return o; }
    if (n.amount != own->amount || n.crimes != own->crimes || n.claimed != own->claimed) o.action = kBountyClearWrite;
    return o;
}

} // namespace coopbounty
