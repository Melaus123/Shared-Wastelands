// teamally.h - T-546 (owner 512): this player's characters and its teammates' characters are one side, "as if one person owned
// both sets - whatever the base game would do". For two characters of the SAME faction the engine answers Character::isAllyOf
// 0x791830 "ally" and Character::isEnemyOf 0x79BDB0 "not an enemy" before anything else (build/decomp_791830.txt,
// decomp_79bdb0.txt): no fight mark, standing or memory is consulted, so a hit between them starts no fight and
// rememberCharacter 0x673A10 stores no temporary-enemy mark (it asks isAllyOf first). The player's own relations take no standing
// change by event (PlayerFactionRelations' slot +0x28 is empty, H071). The plugin gives a pair of DIFFERENT factions that are
// both in this player's team the same answers (peace.cpp) and passes no standing change by event between them to the engine
// (relations.cpp).
// The team set: this game's player faction and its teammates' factions, held in plain cells (pointer compares only, never
// followed), empty while this game's player is in no team. PURE: no engine, no plugin types.
#pragma once
namespace swteamally {
const int kCells = 17;   // this player's faction and up to 16 teammates' factions
// Writes the set into cells[0..cap): `own` first, then every non-zero mate that is not `own`; every other cell 0. `own` is
// held only beside at least one mate (a player in no team has no team set). Each cell is written only when it changes.
// Returns how many cells hold a faction.
inline int CellsWrite(void* volatile* cells, int cap, void* own, void* const* mates, int n)
{
    if (cells == 0 || cap <= 0) return 0;
    int mateCount = 0;
    for (int i = 0; mates != 0 && i < n; ++i) if (mates[i] != 0 && mates[i] != own) ++mateCount;
    /* the new set is laid out first, then written from the highest cell down and the leftover cells cleared after: a faction
       that moves up a cell is written at its new cell before its old one is overwritten, so a reader on another thread never
       misses a faction that is in both the old and the new set */
    void* next[64] = { 0 };
    const int room = cap < 64 ? cap : 64;
    int k = 0;
    if (mateCount > 0)
    {
        if (own != 0 && k < room) next[k++] = own;
        for (int i = 0; i < n && k < room; ++i)
        {
            void* m = mates[i];
            if (m == 0 || m == own) continue;
            next[k++] = m;
        }
    }
    for (int r = k - 1; r >= 0; --r) if (cells[r] != next[r]) cells[r] = next[r];
    for (int r = k; r < cap; ++r) if (cells[r] != 0) cells[r] = 0;
    return k;
}
// A faction is in the team set when one of the first n cells holds it.
inline bool InSet(const void* f, void* const volatile* cells, int n)
{
    if (f == 0 || cells == 0) return false;
    for (int k = 0; k < n; ++k) if (cells[k] == f) return true;
    return false;
}
// The team pair: two DIFFERENT factions, both in the set. The same faction is the engine's own exit (never answered here).
inline bool TeamPair(const void* a, const void* b, void* const volatile* cells, int n)
{
    return a != 0 && b != 0 && a != b && InSet(a, cells, n) && InSet(b, cells, n);
}
}
