/* src/common/loadraidstandin.h - T-1477 design A1 (F2019 / F2020): a stand-in a saved raid names is made at the raid's load.
 *
 * The T-1122 roller's raid on an offline teammate's base is saved with its target faction = that teammate's stand-in
 * ("coop-p<n>"). After a restart the stand-in does not exist while the save loads (this mod makes it later), the war
 * manager's load 0x9C8EF0 (1.0.65) looks the saved "enemy c<i>" name up by id (0x2E79E0: 0 for an unknown id), stores the
 * null unchecked, and the raid's retarget dereferences it ~33 s later (run T3028). A1: on that load, BEFORE the engine's
 * lookup, every missing coop-p<n> a running raid names gets a placeholder stand-in ("Player <n>", the one placeholder
 * road), so the engine's own lookup finds it; the player's later arrival reuses it (CreatePeer's reuse branch).
 *
 * Only a coop-p<n> id with n in 0..kSlotMax is made; every other missing name is left exactly as today. This game's own
 * number (when known) is never made a stand-in.
 *
 * Pure: no engine memory, no Windows. The offline suite (src/coop-test/test_main.cpp, t1477_loadraid) runs it.
 */
#pragma once
#include <string>
#include <cstddef>
#include "slotwire.h"

namespace loadraid {

const char kEnemyKeyPrefix[] = "enemy c";   /* 0x9C8EF0's per-raid key: "enemy c" + the active's index (decomp_9c8ef0:151) */
const int kMaxNames = 32;                   /* names read per war manager's load (a save holds a handful of running raids) */

/* the index an "enemy c<i>" key names - decimal digits as Ogre::StringConverter::toString writes them (no sign, no leading
   zero except "0", at most 6 digits) - or -1 for any other key. p need not be terminated. */
inline int EnemyKeyIndex(const char* p, std::size_t n)
{
    const std::size_t pl = sizeof(kEnemyKeyPrefix) - 1;
    if (p == 0 || n <= pl || n > pl + 6) return -1;
    for (std::size_t i = 0; i < pl; ++i) if (p[i] != kEnemyKeyPrefix[i]) return -1;
    if (n - pl > 1 && p[pl] == '0') return -1;
    int v = 0;
    for (std::size_t i = pl; i < n; ++i) { if (p[i] < '0' || p[i] > '9') return -1; v = v * 10 + (p[i] - '0'); }
    return v;
}

enum Verdict
{
    kMake = 1,         /* a missing coop-p<n>: make its placeholder stand-in before the engine's lookup */
    kExists = 0,       /* the faction exists: the engine's lookup finds it, nothing to do */
    kNotStandIn = -1,  /* not a coop-p<n> id (a game faction, coop-peer, a malformed number): left exactly as today */
    kOwnSlot = -2      /* this game's own number: never its own stand-in - left as today */
};

/* the decision for one saved target id. exists = the faction directory finds it now; mySlot = this game's number, -1 unknown.
   After a restart this game's number is ALWAYS unknown at the load (the world server has not answered yet, and the last known
   number, store.cpp g_lastKnownSlot, is memory-only), so the 'never my own number' guard (kOwnSlot) cannot fire then - it acts
   only on a reload inside one run of the game. *slotOut = the id's number, or -1. */
inline int Judge(const std::string& sid, bool exists, int mySlot, int* slotOut)
{
    const int slot = coopslot::StandInSlotOfId(sid);
    if (slotOut != 0) *slotOut = slot;
    if (slot < 0) return kNotStandIn;
    if (exists) return kExists;
    if (mySlot >= 0 && slot == mySlot) return kOwnSlot;
    return kMake;
}

/* one placeholder per number per load: 1 added, 0 already there or no room */
inline int AddUniqueSlot(int* slots, int* n, int cap, int slot)
{
    if (slots == 0 || n == 0 || slot < 0) return 0;
    for (int i = 0; i < *n; ++i) if (slots[i] == slot) return 0;
    if (*n >= cap) return 0;
    slots[(*n)++] = slot;
    return 1;
}

/* THE NAME COLLECTION: a save record's string fields (GameData+0x138), the engine's hash map as 0x9C8EF0's own 'updatetime' find
   walks the float one (decomp_9c8ef0:531-581): m+0x18 bucket count (a power of two), m+0x20 size, m+0x38 buckets, each the link
   (a node, or the list's start) BEFORE its first node; node +0 next, +8 hash, +0x10 the key, a VS2010 std::string (16-byte inline
   buffer or a pointer at +0, size +0x10, capacity +0x18). Every "enemy c<i>" key is copied (at most cap, each at most 15 chars).
   Reads raw memory: the caller guards faults (playerfaction.cpp LrEnemyKeysPod's __try). Returns the count copied. */
inline int EnemyKeysOfMap(const char* m, char (*keys)[16], int cap)
{
    typedef unsigned long long U;
    if (m == 0 || keys == 0 || cap <= 0) return 0;
    const U nb = *(const U*)(m + 0x18), size = *(const U*)(m + 0x20);
    if (size == 0 || nb == 0 || (nb & (nb - 1)) != 0 || nb > (1ull << 24)) return 0;
    const char* const* buckets = *(const char* const* const*)(m + 0x38);
    if (buckets == 0) return 0;
    int n = 0;
    U seen = 0;
    for (U b = 0; b < nb && seen <= size; ++b)
    {
        const char* link = buckets[b];
        if (link == 0) continue;
        for (const char* node = *(const char* const*)link; node != 0 && seen <= size; node = *(const char* const*)node)
        {
            if ((*(const U*)(node + 8) & (nb - 1)) != b) break;   /* the next bucket's first node */
            ++seen;
            const char* k = node + 0x10;
            const std::size_t ks = (std::size_t)*(const U*)(k + 0x10), kr = (std::size_t)*(const U*)(k + 0x18);
            if (ks < 8 || ks > 15) continue;
            const char* t = kr >= 16 ? *(const char* const*)k : k;
            if (t == 0 || n >= cap || EnemyKeyIndex(t, ks) < 0) continue;
            for (std::size_t c = 0; c < ks; ++c) keys[n][c] = t[c];
            keys[n][ks] = 0; ++n;
        }
    }
    return n;
}

} // namespace loadraid
