/* lev343-keeperaim (owner 343 b, 2026-10-01) - WHERE THE `buytest keeper` TEST LEVER AIMS. No engine types; the plugin
 * (items.cpp BuyTestKeeper) and the offline suite (src/coop-test/test_main.cpp) compile this.
 *
 * WHY IT EXISTS (T787 readout): `ghost` took the first keeper in load order and aimed at that keeper's piece whose key sorts
 * first, with no check that the piece is a counter the trade can take stock from - it aimed at an empty piece with no general
 * grid while the same keeper's stocked counter sat beside it, so the run tested the lever, not the shop.
 *
 * THE RULE (test lever only - nothing a player's game does asks it):
 *   - a piece is aimable only when it is a VALID COUNTER (validCounter: the same stock-grid test BuyTest applies before buying,
 *     items.cpp ItCounterStockGrid) AND it has a position key (BuyTest is handed the key);
 *   - requireStock (rows and `own`) also needs localCount > 0; ghost and pinned do not (ghost shows what the non-holding game's
 *     player gets);
 *   - among a keeper's aimable pieces: localCount > 0 first, then the lowest key (strcmp) - deterministic;
 *   - pin (non-empty): only pieces whose key CONTAINS the pin text;
 *   - among eligible keepers that have an aimable piece: the smallest squad world id (an empty id sorts last), then the
 *     smallest leader uid - never load order. */
#ifndef COOP_KEEPERAIM_H
#define COOP_KEEPERAIM_H

#include <cstring>
#include <vector>
#include <algorithm>

namespace keeperaim {

struct Piece
{
    int validCounter;       /* 1 = the trade can take stock from it (BuyTest's own stock-grid test) */
    long long localCount;   /* items THIS game's copy holds; < 0 = unreadable */
    const char* key;        /* position key; 0 or "" = none (not aimable) */
};

struct Keeper
{
    int eligible;           /* matches the mode (row / ghost / own / pinned) and has a loaded leader */
    int loaded;             /* has a loaded leader (the trade window opens on it) - what a miss line may list */
    const char* worldId;    /* the squad's world id; 0 or "" = unknown (sorts last) */
    unsigned int leaderUid;
    const Piece* pieces;
    int nPieces;
};

enum { kWhyNone = 0, kWhyStockedHere = 1, kWhyNotStockedHere = 2, kWhyPinned = 3 };

inline const char* WhyWord(int why)
{
    return (why == kWhyStockedHere) ? "stocked-here" : (why == kWhyNotStockedHere) ? "not-stocked-here"
         : (why == kWhyPinned) ? "pinned" : "none";
}

inline int HasText(const char* s) { return (s != 0 && s[0] != 0) ? 1 : 0; }

/* 1 = piece p may be aimed at under (pin, requireStock) */
inline int Aimable(const Piece& p, const char* pin, int requireStock)
{
    if (p.validCounter == 0 || HasText(p.key) == 0) return 0;
    if (requireStock != 0 && p.localCount <= 0) return 0;
    if (HasText(pin) != 0 && std::strstr(p.key, pin) == 0) return 0;
    return 1;
}

/* a keeper's best aimable piece: stocked here first, then the lowest key. -1 = none. */
inline int BestPiece(const Keeper& k, const char* pin, int requireStock)
{
    int best = -1;
    for (int i = 0; i < k.nPieces; ++i)
    {
        const Piece& p = k.pieces[i];
        if (Aimable(p, pin, requireStock) == 0) continue;
        if (best < 0) { best = i; continue; }
        const Piece& b = k.pieces[best];
        const int ps = (p.localCount > 0) ? 1 : 0, bs = (b.localCount > 0) ? 1 : 0;
        if (ps != bs) { if (ps > bs) best = i; continue; }
        if (std::strcmp(p.key, b.key) < 0) best = i;
    }
    return best;
}

/* 1 = keeper a goes before keeper b: smallest world id (empty last), then smallest leader uid */
inline int KeeperBefore(const Keeper& a, const Keeper& b)
{
    const int ha = HasText(a.worldId), hb = HasText(b.worldId);
    if (ha != hb) return (ha > hb) ? 1 : 0;
    if (ha != 0)
    {
        const int c = std::strcmp(a.worldId, b.worldId);
        if (c != 0) return (c < 0) ? 1 : 0;
    }
    return (a.leaderUid < b.leaderUid) ? 1 : 0;
}

/* THE CHOICE. Returns the keeper index (-1 = no eligible keeper has an aimable piece); *pieceOut = that keeper's piece index,
   *whyOut = kWhyPinned when a pin was given, else kWhyStockedHere / kWhyNotStockedHere by the chosen piece's local count. */
inline int Choose(const Keeper* ks, int n, const char* pin, int requireStock, int* pieceOut, int* whyOut)
{
    *pieceOut = -1; *whyOut = kWhyNone;
    int bestK = -1, bestP = -1;
    for (int i = 0; i < n; ++i)
    {
        if (ks[i].eligible == 0) continue;
        const int p = BestPiece(ks[i], pin, requireStock);
        if (p < 0) continue;
        if (bestK < 0 || KeeperBefore(ks[i], ks[bestK]) != 0) { bestK = i; bestP = p; }
    }
    if (bestK < 0) return -1;
    *pieceOut = bestP;
    *whyOut = (HasText(pin) != 0) ? kWhyPinned : (ks[bestK].pieces[bestP].localCount > 0 ? kWhyStockedHere : kWhyNotStockedHere);
    return bestK;
}

struct KeeperOrder
{
    const Keeper* ks;
    explicit KeeperOrder(const Keeper* k) : ks(k) {}
    bool operator()(int a, int b) const
    {
        if (KeeperBefore(ks[a], ks[b]) != 0) return true;
        if (KeeperBefore(ks[b], ks[a]) != 0) return false;
        return a < b;
    }
};

/* FOR A MISS LINE: up to cap (keeper, piece) pairs a reader could aim at instead - valid keyed counters first (stocked before
   unstocked within a keeper is NOT re-sorted: chain order), keepers in KeeperBefore order; then, if room, the other pieces.
   only >= 0 limits the list to that keeper (a row miss); otherwise every keeper with loaded != 0 is listed (eligible or not).
   Returns the count written. */
inline int Candidates(const Keeper* ks, int n, int only, int* kOut, int* pOut, int cap)
{
    std::vector<int> idx;
    for (int i = 0; i < n; ++i) if ((only < 0 && ks[i].loaded != 0) || i == only) idx.push_back(i);
    std::sort(idx.begin(), idx.end(), KeeperOrder(ks));
    int got = 0;
    for (int pass = 0; pass < 2 && got < cap; ++pass)
        for (size_t j = 0; j < idx.size() && got < cap; ++j)
        {
            const Keeper& k = ks[idx[j]];
            for (int p = 0; p < k.nPieces && got < cap; ++p)
            {
                const int valid = Aimable(k.pieces[p], 0, 0);
                if ((pass == 0) != (valid != 0)) continue;
                kOut[got] = idx[j]; pOut[got] = p; ++got;
            }
        }
    return got;
}

}   // namespace keeperaim

#endif
