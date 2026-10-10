#pragma once
/* SHARED NAMES FOR GROUND ITEMS.
   A ground key (groundkey.h) is worked out from where the item lies NOW, and each game's physics settles a dropped item on its own,
   so two games can key one item differently (seen: 10.5 units flat). An item's NAME is its first PUBLISHED ground key - the key
   its first GROUND ADD or first PUT carried - frozen from then on; same text grammar as a key, so routing by the key's sector, the
   holder test and "build at the key's tenths" are unchanged. Every game keeps, per item it knows by name, the object's address,
   the name and the item's current key (refreshed while it is seen). A TAKE, a GONE, the kept row, the undo's ADD, the ledger and
   the ground catch-up carry the name; an item nobody named (world loot, a game started again, a row dropped from a full table) is
   carried by its current key, as before.
   THE LOOKUP on a receiving game (NameLookup): the items carrying the name exactly (several = a small bag, GroundBagPick); else,
   for items carrying NO name only, the exact current key, then the nearest of the same sid within kGroundNearTenths flat - a near
   match never lands on an item that carries a different name. A TAKE and a GONE say whether their id is a published NAME or a
   BARE current key (the sender knows no name for the item - it started again, or never saw it named): a BARE id may land on a
   named item too, by exact key, then near, items carrying no name first; the matched item's name then names it from there on.
   THE REMOVED-NAME MEMORY (GoneMemory): the names (and unnamed keys) this game removed or announced gone in the last 10 minutes.
   A holder whose TAKE lookup misses answers reason 2 "not found" only when the name is remembered there (the item really left);
   otherwise reason 5 "name not known here" (coopground::kGroundReasonUnknownName), which the picker asks again and then keeps -
   never removed.
   Header-only, no engine and no Windows types, no globals; the table functions allocate nothing (the pickup detour reads the table
   off the main thread under the ground lock). NameRebindPlan alone allocates and runs on the main thread outside that lock. */

#include <vector>
#include <string>
#include <utility>
#include <algorithm>
#include "groundkey.h"

namespace coopgname {

const int kNameCap = 4096;                 /* rows; the oldest is dropped (counted) when a new one needs room */
const long long kAddrWindowTenths = 50;    /* an address names its row while the item lies within 5 units flat of the row's key */
const int kGoneCap = 512;
const unsigned int kGoneKeepMs = 600000u;  /* 10 minutes */
const int kHowNone = 0, kHowName = 1, kHowKey = 2, kHowNear = 3;

struct NameRow
{
    const void* addr;   /* compared only, never dereferenced */
    int used;
    int frozen;         /* 1 = published: the name never changes again */
    int unloaded;       /* 1 = its item went missing while a zone was taken apart: it may come back at a new address (an orphan) */
    unsigned int orphanAt;   /* when it became an orphan (NameApply's now): forgotten kOrphanKeepMs later (NameExpireOrphans) */
    unsigned int seq;   /* insertion order */
    char name[coopground::kGroundKeyCap];
    coopground::GroundKeyParts cur;   /* the item's key as last seen on this game */
};
struct NameTable
{
    NameRow row[kNameCap];
    int n;                  /* rows in use */
    unsigned int nextSeq;
    long long dropped;      /* rows dropped to make room */
};

inline int NameStrEq(const char* a, const char* b)
{
    int i = 0;
    for (; a[i] != 0 && b[i] != 0; ++i) if (a[i] != b[i]) return 0;
    return a[i] == b[i] ? 1 : 0;
}
/* 1 copied whole, 0 empty or too long (out is then "") */
inline int NameCopy(char* out, int cap, const char* src)
{
    if (out == 0 || cap <= 0) return 0;
    out[0] = 0;
    if (src == 0 || src[0] == 0) return 0;
    int i = 0;
    for (; src[i] != 0; ++i) { if (i >= cap - 1) { out[0] = 0; return 0; } out[i] = src[i]; }
    out[i] = 0;
    return 1;
}

inline void NameClear(NameTable* t)
{
    for (int i = 0; i < kNameCap; ++i) { t->row[i].used = 0; t->row[i].addr = 0; t->row[i].frozen = 0; t->row[i].unloaded = 0; t->row[i].orphanAt = 0; t->row[i].name[0] = 0; }
    t->n = 0; t->nextSeq = 1;
}
/* the row at `addr` whatever it lies at, -1 none */
inline int NameAtAddr(const NameTable& t, const void* addr)
{
    if (addr == 0) return -1;
    for (int i = 0; i < kNameCap; ++i) if (t.row[i].used != 0 && t.row[i].addr == addr) return i;
    return -1;
}
/* 1 = the row stands for an item of key `cur`: the same sid within kAddrWindowTenths flat (an address reused by another object
   names something else) */
inline int NameRowFits(const NameRow& r, const coopground::GroundKeyParts& cur)
{
    const long long d2 = coopground::GroundKeyDist2XZ(r.cur, cur);
    return (d2 >= 0 && d2 <= kAddrWindowTenths * kAddrWindowTenths) ? 1 : 0;
}
/* the row naming the item at `addr` lying at `cur`, -1 = the item carries no name */
inline int NameAt(const NameTable& t, const void* addr, const coopground::GroundKeyParts& cur)
{
    const int i = NameAtAddr(t, addr);
    return (i >= 0 && NameRowFits(t.row[i], cur) != 0) ? i : -1;
}
/* THE DETOUR'S READ (any thread, the caller holds the ground lock): the item's name into out; 1 named, 0 not (out = ""). */
inline int NameOf(const NameTable& t, const void* addr, const coopground::GroundKeyParts& cur, char* out, int cap)
{
    if (out != 0 && cap > 0) out[0] = 0;
    const int i = NameAt(t, addr, cur);
    if (i < 0) return 0;
    return NameCopy(out, cap, t.row[i].name);
}
/* the same, and *frozenOut 1 = that name is published (frozen) - a TAKE then names the item by it, else by its bare key */
inline int NameOfEx(const NameTable& t, const void* addr, const coopground::GroundKeyParts& cur, char* out, int cap, int* frozenOut)
{
    if (frozenOut != 0) *frozenOut = 0;
    if (out != 0 && cap > 0) out[0] = 0;
    const int i = NameAt(t, addr, cur);
    if (i < 0) return 0;
    if (frozenOut != 0) *frozenOut = t.row[i].frozen;
    return NameCopy(out, cap, t.row[i].name);
}
inline void NameForgetRow(NameTable* t, int i)
{
    if (i < 0 || i >= kNameCap || t->row[i].used == 0) return;
    t->row[i].used = 0; t->row[i].addr = 0; t->row[i].name[0] = 0; t->row[i].frozen = 0; t->row[i].unloaded = 0;
    if (t->n > 0) --t->n;
}
inline int NameForgetAddr(NameTable* t, const void* addr)
{
    const int i = NameAtAddr(*t, addr);
    if (i < 0) return 0;
    NameForgetRow(t, i);
    return 1;
}
/* the rows carrying `name` exactly (at most cap indices written); returns how many there are */
inline int NameFindName(const NameTable& t, const char* name, int* idxOut, int cap)
{
    int n = 0;
    if (name == 0 || name[0] == 0) return 0;
    for (int i = 0; i < kNameCap; ++i)
    {
        if (t.row[i].used == 0 || NameStrEq(t.row[i].name, name) == 0) continue;
        if (idxOut != 0 && n < cap) idxOut[n] = i;
        ++n;
    }
    return n;
}
/* one row carrying `name` forgotten (the first found), 1 = one was */
inline int NameForgetName(NameTable* t, const char* name)
{
    int at = -1;
    if (NameFindName(*t, name, &at, 1) == 0) return 0;
    NameForgetRow(t, at);
    return 1;
}
/* BIND the item at `addr` (lying at `cur`) to `name`. A row at that address standing for another object (another sid, or past the
   address window) is replaced. An existing row keeps a FROZEN name whatever `name` says (an area handover, a re-send, a re-announce
   never renames); an unfrozen one follows `name` (an own drop before its first PUT). freeze 1 = published now. A new row takes a
   free slot, else the oldest row's (counted dropped). Returns the row, -1 = `name` empty or too long. */
inline int NameBind(NameTable* t, const void* addr, const char* name, const coopground::GroundKeyParts& cur, int freeze)
{
    if (addr == 0 || name == 0 || name[0] == 0) return -1;
    char tmp[coopground::kGroundKeyCap];
    if (NameCopy(tmp, (int)sizeof tmp, name) == 0) return -1;
    int i = NameAtAddr(*t, addr);
    if (i >= 0 && NameRowFits(t->row[i], cur) == 0) { NameForgetRow(t, i); i = -1; }
    if (i >= 0)
    {
        NameRow& r = t->row[i];
        if (r.frozen == 0) NameCopy(r.name, (int)sizeof r.name, tmp);
        if (freeze != 0) r.frozen = 1;
        r.cur = cur;
        r.unloaded = 0;
        return i;
    }
    int at = -1, oldest = -1;
    for (int k = 0; k < kNameCap; ++k)
    {
        if (t->row[k].used == 0) { at = k; break; }
        if (oldest < 0 || (int)(t->row[k].seq - t->row[oldest].seq) < 0) oldest = k;
    }
    if (at < 0) { at = oldest; NameForgetRow(t, at); ++t->dropped; }
    NameRow& r = t->row[at];
    r.used = 1; r.addr = addr; r.frozen = (freeze != 0) ? 1 : 0; r.unloaded = 0; r.seq = t->nextSeq++;
    NameCopy(r.name, (int)sizeof r.name, tmp);
    r.cur = cur;
    ++t->n;
    return at;
}
/* this game moved the item itself (same object): its current key is refreshed, its name kept */
inline void NameRefresh(NameTable* t, int i, const coopground::GroundKeyParts& cur)
{
    if (i < 0 || i >= kNameCap || t->row[i].used == 0) return;
    t->row[i].cur = cur;
}

/* ---- the lookup on a receiving game ---- */
struct LiveItem
{
    const void* addr;
    coopground::GroundKeyParts cur;
    int qty, q100;
};
/* The live ground item `name` stands for: items carrying that name (the bag match among several). A NAME id (bare 0): else, among
   items carrying NO name, the exact current key (the bag match), then - unless exactOnly - the nearest of the same sid within
   kGroundNearTenths flat (match rank, then height difference, then flat distance: coopground::GroundNearBetter); an item carrying
   a different name is never returned. A BARE id (bare 1, the sender knows no name): else the exact current key, then - unless
   exactOnly - the nearest within kGroundNearTenths flat, each among the items carrying no name first and then the named ones (the
   lookup before names). Only the first 16 of a bag are weighed. Returns the index into live[], -1 none; *howOut kHow*. */
inline int NameLookup(const NameTable& t, const LiveItem* live, int n, const char* name, int wantQty, int wantQ100, int exactOnly, int* howOut, int bare = 0)
{
    if (howOut != 0) *howOut = kHowNone;
    coopground::GroundKeyParts want;
    if (name == 0 || coopground::GroundKeyParse(name, &want) != 1) return -1;
    int qty[16], q100[16], idx[16];
    int nn = 0;
    int kqty[2][16], kq100[2][16], kidx[2][16];
    int nk[2] = { 0, 0 };   /* the exact current key: [0] items carrying no name, [1] named items (a BARE id only) */
    int best[2] = { -1, -1 }, bestRank[2] = { 0, 0 };
    long long bestD[2] = { 0, 0 }, bestDy[2] = { 0, 0 };
    const long long nearMax = (long long)coopground::kGroundNearTenths * coopground::kGroundNearTenths;
    for (int i = 0; i < n; ++i)
    {
        if (coopground::GroundSidEqual(live[i].cur.sid, want.sid) == 0) continue;   /* a name is always of the item's own sid */
        const int row = NameAt(t, live[i].addr, live[i].cur);
        int g = 0;
        if (row >= 0)
        {
            if (NameStrEq(t.row[row].name, name) != 0) { if (nn < 16) { qty[nn] = live[i].qty; q100[nn] = live[i].q100; idx[nn] = i; ++nn; } continue; }
            if (bare == 0) continue;   /* named otherwise: never a key or near match for a NAME */
            g = 1;                     /* a BARE id may land on it, after every item carrying no name */
        }
        if (coopground::GroundKeySame(live[i].cur, want) != 0)
        {
            if (nk[g] < 16) { kqty[g][nk[g]] = live[i].qty; kq100[g][nk[g]] = live[i].q100; kidx[g][nk[g]] = i; ++nk[g]; }
            continue;
        }
        if (exactOnly != 0) continue;
        const long long d = coopground::GroundKeyDist2XZ(live[i].cur, want);
        if (d < 0 || d > nearMax) continue;
        const int rk = coopground::GroundBagRank(live[i].qty, live[i].q100, wantQty, wantQ100);
        const long long dy = coopground::GroundKeyDy(live[i].cur, want);
        if (best[g] < 0 || coopground::GroundNearBetter(rk, dy, d, bestRank[g], bestDy[g], bestD[g]) != 0) { best[g] = i; bestRank[g] = rk; bestDy[g] = dy; bestD[g] = d; }
    }
    if (nn > 0)
    {
        const int k = coopground::GroundBagPick(qty, q100, 0, nn, wantQty, wantQ100);
        if (howOut != 0) *howOut = kHowName;
        return idx[k];
    }
    for (int g = 0; g < 2; ++g)
        if (nk[g] > 0)
        {
            const int k = coopground::GroundBagPick(kqty[g], kq100[g], 0, nk[g], wantQty, wantQ100);
            if (howOut != 0) *howOut = kHowKey;
            return kidx[g][k];
        }
    for (int g = 0; g < 2; ++g)
        if (best[g] >= 0) { if (howOut != 0) *howOut = kHowNear; return best[g]; }
    return -1;
}

/* ---- re-binding after a zone is taken apart and loads again (new addresses, the same places on THIS game) ----
   An ORPHAN is a row whose item went missing while a zone was taken apart (unloaded 1), or a row detached from an address another
   object now holds (addr 0). Only orphans are ever handed to another live item: a row whose item left this ground any other way
   (picked up, taken, destroyed) is never one - every such road forgets its row, and one it missed is left alone, never re-bound. */
const int kChRebind = 0, kChForget = 1, kChDetach = 2, kChMark = 3;
struct NameChange { int row; const void* addr; coopground::GroundKeyParts cur; int act; };   /* act kCh* */
inline int NameIsOrphan(const NameRow& r) { return (r.used != 0 && (r.addr == 0 || r.unloaded != 0)) ? 1 : 0; }
inline int NameOrphanCount(const NameTable& t)
{
    int c = 0;
    for (int i = 0; i < kNameCap; ++i) c += NameIsOrphan(t.row[i]);
    return c;
}
struct NameSidRow { std::string sid; int row; };
inline bool NameSidRowLess(const NameSidRow& a, const NameSidRow& b) { return (a.sid != b.sid) ? (a.sid < b.sid) : (a.row < b.row); }
struct NamePair { long long d; int live; int row; };
inline bool NamePairLess(const NamePair& a, const NamePair& b)
{
    if (a.d != b.d) return a.d < b.d;
    return (a.live != b.live) ? (a.live < b.live) : (a.row < b.row);
}
/* MAIN THREAD, outside the ground lock (it allocates): what to change so the table follows the live ground list. unloadedNow 1 = a
   zone was taken apart since the previous pass. skip[]: addresses never re-bound and never orphans - a pending drop's item, an
   overflow drop not yet PUT, a removal waiting for its claim, an item held for a granted TAKE or kept in escrow.
   - a row whose address is live and fits refreshes its key (fit: within kAddrWindowTenths flat; within kGroundNearTenths for an
     orphan whose own address went missing and is live again - the address may then be another object's); the same sid past the
     window, no zone taken apart: left;
   - a row whose address is live but holds another object (another sid, or past the fit while it was an orphan or a zone was taken
     apart) is detached (an orphan) and that item is offered an orphan like any item carrying no name;
   - a row whose address is not live becomes an orphan only when a zone was taken apart (marked); otherwise it is left as it is;
   - the orphans, grouped by sid, are handed to the live items carrying no name: every (item, orphan) pair of one sid within
     kGroundNearTenths flat, the nearest pair first, each item and each orphan once.
   Returns the number of re-binds planned. */
inline int NameRebindPlan(const NameTable& t, const LiveItem* live, int n, const void* const* skip, int nSkip, int unloadedNow, std::vector<NameChange>* out)
{
    out->clear();
    std::vector<const void*> la;
    la.reserve((size_t)(n > 0 ? n : 0));
    for (int i = 0; i < n; ++i) la.push_back(live[i].addr);
    std::sort(la.begin(), la.end());
    std::vector<const void*> sk;
    for (int i = 0; i < nSkip; ++i) sk.push_back(skip[i]);
    std::sort(sk.begin(), sk.end());
    std::vector<std::pair<const void*, int> > byAddr;
    std::vector<NameSidRow> orphan;
    for (int i = 0; i < kNameCap; ++i)
    {
        const NameRow& r = t.row[i];
        if (r.used == 0) continue;
        if (r.addr != 0 && std::binary_search(la.begin(), la.end(), r.addr)) { byAddr.push_back(std::make_pair(r.addr, i)); continue; }
        if (r.addr != 0 && std::binary_search(sk.begin(), sk.end(), r.addr)) continue;   /* held, in escrow or pending here */
        if (NameIsOrphan(r) == 0)
        {
            if (unloadedNow == 0) continue;   /* missing while no zone was taken apart: its item left this ground - never handed on */
            NameChange c; c.row = i; c.addr = r.addr; c.cur = r.cur; c.act = kChMark;
            out->push_back(c);
        }
        NameSidRow o; o.sid = std::string(r.cur.sid); o.row = i;
        orphan.push_back(o);
    }
    std::sort(byAddr.begin(), byAddr.end());
    const long long fitNear = (long long)coopground::kGroundNearTenths * coopground::kGroundNearTenths;
    const long long fitWin = kAddrWindowTenths * kAddrWindowTenths;
    std::vector<int> cand;   /* live items carrying no name: indices into live[] */
    for (int i = 0; i < n; ++i)
    {
        std::vector<std::pair<const void*, int> >::const_iterator it =
            std::lower_bound(byAddr.begin(), byAddr.end(), std::make_pair(live[i].addr, -1));
        if (it != byAddr.end() && it->first == live[i].addr)
        {
            const NameRow& r = t.row[it->second];
            const int strict = (r.unloaded != 0) ? 1 : 0;   /* its own address went missing: it may now hold another object - the 0.5 fit */
            const int reload = (strict != 0 || unloadedNow != 0) ? 1 : 0;
            const long long d2 = coopground::GroundKeyDist2XZ(r.cur, live[i].cur);   /* -1 = another sid */
            NameChange c; c.row = it->second; c.addr = live[i].addr; c.cur = live[i].cur; c.act = kChRebind;
            if (d2 >= 0 && d2 <= (strict != 0 ? fitNear : fitWin))
            {
                if (coopground::GroundKeySame(r.cur, live[i].cur) == 0 || r.unloaded != 0) out->push_back(c);
                continue;
            }
            if (d2 >= 0 && reload == 0) continue;   /* the same sid past the window, no zone taken apart: left as it is */
            c.act = kChDetach; c.addr = 0;
            out->push_back(c);
            NameSidRow o; o.sid = std::string(r.cur.sid); o.row = it->second;
            orphan.push_back(o);
        }
        if (std::binary_search(sk.begin(), sk.end(), live[i].addr)) continue;
        cand.push_back(i);
    }
    if (orphan.empty() || cand.empty()) return 0;
    std::sort(orphan.begin(), orphan.end(), NameSidRowLess);
    std::vector<NamePair> pairs;
    for (size_t c = 0; c < cand.size(); ++c)
    {
        const LiveItem& li = live[cand[c]];
        NameSidRow key; key.sid = std::string(li.cur.sid); key.row = -1;
        std::vector<NameSidRow>::const_iterator o = std::lower_bound(orphan.begin(), orphan.end(), key, NameSidRowLess);
        for (; o != orphan.end() && o->sid == key.sid; ++o)
        {
            const long long d = coopground::GroundKeyDist2XZ(t.row[o->row].cur, li.cur);
            if (d < 0 || d > fitNear) continue;
            NamePair p; p.d = d; p.live = cand[c]; p.row = o->row;
            pairs.push_back(p);
        }
    }
    std::sort(pairs.begin(), pairs.end(), NamePairLess);
    std::vector<char> liveTaken((size_t)n, 0), rowTaken((size_t)kNameCap, 0);
    int rebinds = 0;
    for (size_t p = 0; p < pairs.size(); ++p)
    {
        if (liveTaken[(size_t)pairs[p].live] != 0 || rowTaken[(size_t)pairs[p].row] != 0) continue;
        liveTaken[(size_t)pairs[p].live] = 1; rowTaken[(size_t)pairs[p].row] = 1;
        NameChange c; c.row = pairs[p].row; c.addr = live[pairs[p].live].addr; c.cur = live[pairs[p].live].cur; c.act = kChRebind;
        out->push_back(c);
        ++rebinds;
    }
    return rebinds;
}
/* under the ground lock: the planned changes, in order (allocates nothing); `now` stamps a row that becomes an orphan */
inline void NameApply(NameTable* t, const NameChange* c, int n, unsigned int now = 0)
{
    for (int i = 0; i < n; ++i)
    {
        if (c[i].row < 0 || c[i].row >= kNameCap || t->row[c[i].row].used == 0) continue;
        NameRow& r = t->row[c[i].row];
        if (c[i].act == kChForget) { NameForgetRow(t, c[i].row); continue; }
        if (c[i].act == kChDetach) { if (r.unloaded == 0) r.orphanAt = now; r.addr = 0; r.unloaded = 1; continue; }
        if (c[i].act == kChMark) { if (r.unloaded == 0) r.orphanAt = now; r.unloaded = 1; continue; }
        r.addr = c[i].addr;
        r.cur = c[i].cur;
        r.unloaded = 0;
    }
}

/* ---- orphans nobody takes back ---- */
const unsigned int kOrphanKeepMs = 600000u;   /* 10 minutes: an orphan whose zone never loads again is not kept for the session */
/* under the ground lock (allocates nothing): the rows that became orphans kOrphanKeepMs or more before `now` are forgotten; returns
   how many */
inline int NameExpireOrphans(NameTable* t, unsigned int now)
{
    int n = 0;
    for (int i = 0; i < kNameCap; ++i)
        if (NameIsOrphan(t->row[i]) != 0 && (unsigned int)(now - t->row[i].orphanAt) >= kOrphanKeepMs) { NameForgetRow(t, i); ++n; }
    return n;
}

/* ---- the catch-up's NAME FOR on the asker, and a repeated TAKE's match ---- */
const int kNfOk = 0, kNfSkipped = 1, kNfOtherName = 2, kNfNameTaken = 3, kNfUnreadable = 4;
inline int NameAddrIn(const void* const* list, int n, const void* addr)
{
    if (list == 0 || addr == 0) return 0;
    for (int i = 0; i < n; ++i) if (list[i] == addr) return 1;
    return 0;
}
/* May the item at `addr` (lying at `cur`), found for the listed id `idFor`, take `name`? Never: an address on the re-bind skip list
   (skip[]) or this game's own unconfirmed drop (ownDrop 1); an item carrying a name other than idFor and other than `name`; a name a
   row at another address already carries (one name, one row). Returns kNf*. */
inline int NameForCheck(const NameTable& t, const void* addr, const coopground::GroundKeyParts& cur, const char* name, const char* idFor,
                        const void* const* skip, int nSkip, int ownDrop)
{
    if (ownDrop != 0 || NameAddrIn(skip, nSkip, addr) != 0) return kNfSkipped;
    const int own = NameAt(t, addr, cur);
    if (own >= 0 && NameStrEq(t.row[own].name, name) == 0 && (idFor == 0 || NameStrEq(t.row[own].name, idFor) == 0)) return kNfOtherName;
    const int self = NameAtAddr(t, addr);
    int at[2]; at[0] = -1; at[1] = -1;
    const int nf = NameFindName(t, name, at, 2);
    if (nf > 1 || (nf == 1 && at[0] != self)) return kNfNameTaken;
    return kNfOk;
}
inline const char* NameForWhy(int nf)
{
    return nf == kNfSkipped ? "the item is this game's own drop not yet confirmed, a removal waiting, or an item held for a granted TAKE"
         : nf == kNfOtherName ? "the item carries another name"
         : nf == kNfNameTaken ? "another item here already carries that name"
         : nf == kNfUnreadable ? "the item cannot be read" : "ok";
}
/* a repeated TAKE's id names the kept row: its kept name, or the TAKE's own id (a bare key that landed on a named item; "" = none) */
inline int KeptIdMatch(const char* askId, const char* keptName, const char* keptAskId)
{
    if (askId == 0) return 0;
    if (keptName != 0 && NameStrEq(askId, keptName) != 0) return 1;
    return (keptAskId != 0 && keptAskId[0] != 0 && NameStrEq(askId, keptAskId) != 0) ? 1 : 0;
}

/* ---- the id's kind on the wire (game-to-game protocol 149) ----
   ITEM_MOVE with owner uid 0, right after the box id, and ITEM_REQUEST with owner uid 0, right after the owner box id: u8 kind -
   kIdBare 0 (the sender's current key: the item carries no published name there), kIdName 1 (a published name), kIdNameFor 2
   (ITEM_MOVE op 0 only: a published name, then u32 length + bytes - the id the RECEIVER listed for that item in its ground
   catch-up listing; that item takes the name, nothing is built). A storage box's move or request carries 0. */
const int kIdBare = 0, kIdName = 1, kIdNameFor = 2;
inline void IdKindPut(std::vector<char>* b, int kind, const std::string& listedAs)
{
    int k = (kind == kIdName || kind == kIdNameFor) ? kIdName : kIdBare;
    if (kind == kIdNameFor && !listedAs.empty() && listedAs.size() < (size_t)coopground::kGroundKeyCap) k = kIdNameFor;
    b->push_back((char)(unsigned char)k);
    if (k != kIdNameFor) return;
    const unsigned int len = (unsigned int)listedAs.size();
    for (int i = 0; i < 4; ++i) b->push_back((char)(unsigned char)((len >> (8 * i)) & 0xFFu));
    b->insert(b->end(), listedAs.begin(), listedAs.end());
}
/* 1 = read (*at moved past it); 0 = malformed: cut, an unknown kind, or a NAME FOR whose listed id is empty or too long */
inline int IdKindGet(const std::vector<char>& b, size_t* at, int* kind, std::string* listedAs)
{
    *kind = kIdBare;
    listedAs->clear();
    if (*at >= b.size()) return 0;
    const int k = (int)(unsigned char)b[*at];
    if (k != kIdBare && k != kIdName && k != kIdNameFor) return 0;
    size_t p = *at + 1;
    if (k == kIdNameFor)
    {
        if (b.size() < p + 4) return 0;
        unsigned int len = 0;
        for (int i = 0; i < 4; ++i) len |= (unsigned int)(unsigned char)b[p + (size_t)i] << (8 * i);
        p += 4;
        if (len == 0 || len >= (unsigned int)coopground::kGroundKeyCap || (size_t)len > b.size() - p) return 0;
        listedAs->assign(&b[p], (size_t)len);
        p += (size_t)len;
    }
    *kind = k;
    *at = p;
    return 1;
}

/* ---- the removed-name memory ---- */
struct GoneRow { int used; int named; unsigned int at; char name[coopground::kGroundKeyCap]; };
struct GoneMemory { GoneRow row[kGoneCap]; int head; long long overwritten; };
inline void GoneClear(GoneMemory* m)
{
    for (int i = 0; i < kGoneCap; ++i) { m->row[i].used = 0; m->row[i].name[0] = 0; }
    m->head = 0; m->overwritten = 0;
}
/* `name` left this game's ground at `now`. named 1 = a published name (matched exactly); 0 = an unnamed item's key (its position) */
inline void GoneNote(GoneMemory* m, const char* name, int named, unsigned int now)
{
    if (name == 0 || name[0] == 0) return;
    for (int i = 0; i < kGoneCap; ++i)
        if (m->row[i].used != 0 && NameStrEq(m->row[i].name, name) != 0) { m->row[i].at = now; if (named != 0) m->row[i].named = 1; return; }
    GoneRow& r = m->row[m->head];
    if (r.used != 0) ++m->overwritten;
    if (NameCopy(r.name, (int)sizeof r.name, name) == 0) return;
    r.used = 1; r.named = (named != 0) ? 1 : 0; r.at = now;
    m->head = (m->head + 1) % kGoneCap;
}
/* 1 = `name` itself left within kGoneKeepMs; 2 = an UNNAMED item of its sid within kGroundNearTenths flat left (an unnamed key is
   only a position, matched near as before names); 0 = neither */
inline int GoneHas(const GoneMemory& m, const char* name, unsigned int now)
{
    if (name == 0 || name[0] == 0) return 0;
    coopground::GroundKeyParts want;
    const int parsed = (coopground::GroundKeyParse(name, &want) == 1) ? 1 : 0;
    int nearHit = 0;
    const long long nearMax = (long long)coopground::kGroundNearTenths * coopground::kGroundNearTenths;
    for (int i = 0; i < kGoneCap; ++i)
    {
        const GoneRow& r = m.row[i];
        if (r.used == 0 || (unsigned int)(now - r.at) > kGoneKeepMs) continue;
        if (NameStrEq(r.name, name) != 0) return 1;
        if (r.named != 0 || parsed == 0 || nearHit != 0) continue;
        coopground::GroundKeyParts gp;
        if (coopground::GroundKeyParse(r.name, &gp) != 1) continue;
        const long long d = coopground::GroundKeyDist2XZ(gp, want);
        if (d >= 0 && d <= nearMax) nearHit = 2;
    }
    return nearHit;
}
/* THE HOLDER'S ANSWER when a TAKE's name matches nothing on its ground: "not found" (the requester's copy goes) only when the item
   is known to have left; otherwise "name not known here" (asked again, then kept - never removed). */
inline int TakeMissReason(int goneHas)
{
    return (goneHas != 0) ? coopground::kGroundReasonNotFound : coopground::kGroundReasonUnknownName;
}

}   /* namespace coopgname */
