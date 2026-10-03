/* src/common/uidtable.h - THE UID TABLE'S DECISIONS, AS PURE FUNCTIONS (mirror1, crash T487).
 *
 * ONE RULE, the same one clockmath.h and retirewithdraw.h keep: nothing in here reads engine memory, reads a
 * global, calls the operating system or includes any header at all.  The plugin's uid mirror (spawn.cpp) and its
 * lock-free hash index keep their own storage and their own interlocked publication order; what lives here is
 * every DECISION they take, so the offline suite can drive them instead of believing a comment.
 *
 * WHY IT EXISTS.  T487, game B, after `leave` + `join direct`: the 512-row mirror filled (rows were never freed -
 * a despawned or destroyed character kept its row until the world was torn down), the FULL message said
 * "Nothing further will be adopted", and 69 characters were adopted and driven anyway.  A puppet the table did
 * not hold was invisible to removal: the DESPAWN found nothing, the retired guard found nothing, and the drive
 * teleported a character whose memory had become a GameData.  So:
 *   A. a character the table cannot hold is never adopted or driven        (AdoptDecision / DriveDecision);
 *   B. removal does not depend on the table alone                          (DespawnRoute);
 *   C. rows are released, and the index can release a slot without breaking a concurrent reader's probe
 *      (IxFind / IxInsertAt / IxTrimmable - linear probing with tombstones that are reused and trimmed).
 *
 * THE INDEX, AND WHY A RELEASE IS SAFE FOR A READER ON ANOTHER THREAD.  Linear probing keeps every entry in an
 * unbroken run of non-empty slots from its home slot to where it sits.  Releasing writes a TOMBSTONE (never an
 * empty slot), which is non-empty, so no run is broken and a reader probing past it keeps going.  A tombstone is
 * turned back into EMPTY only when the slot after it is already empty (IxTrimmable): such a slot cannot lie inside
 * any entry's run, because the empty slot after it would already have ended that run.  The plugin has ONE writer
 * (the main thread); readers only compare addresses.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_UIDTABLE_H
#define COOP_COMMON_UIDTABLE_H

namespace coopuid {

/* THE KEY OF A RELEASED INDEX SLOT.  Never a heap address: PlausiblePtr refuses anything under 0x10000, and a
   reader compares it against a real object address, which it can never equal. */
inline const void* Tomb() { return (const void*)1; }

/* FIND.  `keys(i)` returns slot i's key.  Returns the slot holding `obj`, or -1.  An empty slot ends the run;
   a tombstone does not. */
template <class K>
inline int IxFind(const K& keys, int mask, int home, const void* obj)
{
    for (int p = 0; p <= mask; ++p)
    {
        const int s = (home + p) & mask;
        const void* k = keys(s);
        if (k == 0) return -1;
        if (k == obj) return s;
    }
    return -1;
}

/* WHERE TO INSERT.  Returns the slot to write (the first tombstone of the run if there is one, else the empty slot
   that ends it), or -1 when the table has neither.  When `obj` is ALREADY in the run, *existing is set to its slot
   and that slot is returned - the caller rebinds it rather than writing a second row for one object.  The whole run
   is walked before a tombstone is chosen, so an object is never indexed twice. */
template <class K>
inline int IxInsertAt(const K& keys, int mask, int home, const void* obj, int* existing)
{
    *existing = -1;
    int firstTomb = -1;
    for (int p = 0; p <= mask; ++p)
    {
        const int s = (home + p) & mask;
        const void* k = keys(s);
        if (k == obj) { *existing = s; return s; }
        if (k == Tomb()) { if (firstTomb < 0) firstTomb = s; continue; }
        if (k == 0) return firstTomb >= 0 ? firstTomb : s;
    }
    return firstTomb;
}

/* MAY THIS TOMBSTONE BECOME EMPTY?  Only when the next slot is already empty (see the header note).  The caller
   walks backwards from a fresh tombstone while this answers 1. */
template <class K>
inline int IxTrimmable(const K& keys, int mask, int s)
{
    return (keys(s & mask) == Tomb() && keys((s + 1) & mask) == 0) ? 1 : 0;
}

/* A. MAY THIS CHARACTER BE ADOPTED AS A PUPPET?  `registeredUid` is what the uid table answers for the character's
   object (0 = not held, retired, or released).  Adopted only when the table holds it live under THIS uid. */
enum { kAdoptYes = 0, kAdoptRefusedUnregistered = 1, kAdoptRefusedOtherUid = 2 };
inline int AdoptDecision(unsigned int registeredUid, unsigned int uid)
{
    if (registeredUid == 0) return kAdoptRefusedUnregistered;
    if (registeredUid != uid) return kAdoptRefusedOtherUid;
    return kAdoptYes;
}

/* A. MAY THIS PUPPET BE DRIVEN THIS FRAME?  The same question, asked every frame, because a row can be retired,
   released or rebound after adoption.  1 = drive, 0 = skip without touching the object. */
inline int DriveDecision(unsigned int registeredUid, unsigned int puppetUid)
{
    return (registeredUid != 0 && registeredUid == puppetUid) ? 1 : 0;
}

/* B. WHAT AN INBOUND DESPAWN DOES.
     liveCopy   - the table holds the uid's object live (FindSpawned answered);
     rawHeld    - the uid -> object map still names an object for it (retired, released, or never registered);
     puppetHeld - the puppet list holds the uid.
   kRouteRemove    : the ordinary removal - drop the puppet, retire, destroy the copy, release the row.
   kRouteDropStale : forget every trace WITHOUT touching the object (it may be freed or reused memory).
   kRouteUnknown   : nothing here for that uid - normal, counted. */
enum { kRouteRemove = 0, kRouteDropStale = 1, kRouteUnknown = 2 };
inline int DespawnRoute(int liveCopy, int rawHeld, int puppetHeld)
{
    if (liveCopy) return kRouteRemove;
    if (rawHeld || puppetHeld) return kRouteDropStale;
    return kRouteUnknown;
}
/* The pre-mirror1 route, kept so the suite can show the hole: without a live copy it was always "unknown", and the
   puppet stayed in the list. */
inline int DespawnRouteLegacy(int liveCopy, int /*rawHeld*/, int /*puppetHeld*/)
{
    return liveCopy ? kRouteRemove : kRouteUnknown;
}

/* C. MAY THIS MIRROR ROW BE RELEASED BY THE PERIODIC RECLAIM?  Only a row whose identity is FINISHED: `destroyed`
   (the engine destroyed the object, or P034 proved the address holds a different one).  A row that is merely
   `dead` (streamed out) is kept: it comes back through MirrorRestore with its uid.  A despawn releases its own row
   directly, after the copy is destroyed. */
inline int ReclaimRow(int occupied, int dead, int destroyed)
{
    return (occupied && dead && destroyed) ? 1 : 0;
}

/* D. MirrorAdd MEETS ITS OWN ADDRESS ALREADY IN A ROW (mirror1 fold, review-mirror1 #6).  askUid 0 = "un-retire in place
   and keep the row's uid" (AdoptExisting's came-back path).  A row of ANOTHER uid is refused: answering "registered" for it
   told CreateAt / AdoptExistingTwin the character was theirs while every lookup answered the other uid.  A destroyed row
   is rebound to a new uid, never kept under its old one (F328). */
enum { kRowAlready = 0, kRowUnretire = 1, kRowRebind = 2, kRowRefuseRebind = 3, kRowRefuseOtherUid = 4 };
inline int ExistingRowAction(unsigned int askUid, unsigned int rowUid, int dead, int destroyed)
{
    if (destroyed) return askUid == 0 ? kRowRefuseRebind : kRowRebind;
    if (askUid != 0 && askUid != rowUid) return kRowRefuseOtherUid;
    return dead ? kRowUnretire : kRowAlready;
}

/* E. RATE LIMIT OF A REPEATING REFUSAL LINE (review-mirror1 #12): the first 5, then every 100th.  `count` is the refusal's
   own counter, already incremented. */
inline int LogRefusal(long long count)
{
    return (count >= 1 && (count <= 5 || (count % 100) == 0)) ? 1 : 0;
}

/* F. WHAT THE WORLD SWEEP DOES AFTER ONE REFUSED REGISTRATION (review-mirror1 #3).  Rows are released since mirror1, so a
   refusal is not permanent and the sweep never switches itself off.  No free row -> end THIS tick (nothing else can
   register before a row frees; the next tick retries).  A free row (refused for another reason) -> skip only this
   character for the rest of the tick. */
enum { kSweepSkipCharacter = 0, kSweepEndTick = 1 };
inline int SweepRefusalAction(int tableHasRoom)
{
    return tableHasRoom ? kSweepSkipCharacter : kSweepEndTick;
}

/* G. T-354 - THE TABLE'S BOUND, FOR 256 PLAYERS.  Fixed, never grown at run time: the index above is read lock-free from
   game threads (the combat, hit and medical detours) while the main thread writes, and that is safe only because no slot ever
   MOVES - a resize would move every entry under a reader mid-probe.  So the size is chosen from the peak with margin.
   PEAK (Inferred, M12 audit 2026-09-30): rows are per game, one per character this game owns and replicates (its own squad +
   the world characters it adopted in its loaded sectors) plus one per copy of another game's character (SPAWNs reach a game
   only for the sectors it holds - route AREA).  So the count follows the CROWD IN THIS GAME'S AREAS, not the player count:
   measured 2-player peaks since rows are released (mirror1) are 71-288 (runs, mirrorHigh).  Worst realistic case at 256 players:
   a gathering of ~100 players with ~10 characters each in this game's areas (1,000) + the area's world characters (~300, the
   measured peak) + streamed-out rows kept for MirrorRestore (~500, a Guess) = ~1,800; 2,048 has no margin over that, 4,096 has
   2.2x, and covers ~370 players at 10 characters each.  The index keeps 4 x the rows (load under 0.25). */
const int kMirrorRows        = 4096;
const int kMirrorIndexSlots  = 4 * kMirrorRows;   /* 16,384 - a power of two (the index masks with slots - 1) */

/* The index's home slot for an object (was spawn.cpp's IndexHash, unchanged): heap addresses carry no information in their low 4
   bits, so the rest is mixed down with a Fibonacci multiply and the top half kept. */
inline int IxHome(const void* obj, int mask)
{
    unsigned long long v = ((unsigned long long)obj) >> 4;
    v *= 0x9E3779B97F4A7C15ull;
    return (int)((v >> 32) & (unsigned long long)mask);
}

/* The TEST-ONLY cap (`mirrorcap <n>`, 0 = off): the table answers "no free row" while `used` rows are occupied and used >= cap. */
inline int MirrorCapReached(int used, int testCap)
{
    return (testCap > 0 && used >= testCap) ? 1 : 0;
}

/* H. T-354 - NOT_SHOWN (game-to-game MSG_NOT_SHOWN 73, session protocol 130).  When this game's table refuses another game's
   character (its SPAWN made no copy here), this game tells the character's OWNER, on the road the SPAWN came by, so the refusal
   is counted on both games.  Nothing else changes on either side: the owner's character goes on as its own.
     uid u32 | reason u8 (kNs*: MirrorRefusalWhy's numbers) | refusals u32 (the sender's running count)      9 bytes, little-endian */
enum { kNsNoRow = 1, kNsIndexFull = 2, kNsDestroyedAddr = 3, kNsOtherUid = 4, kNsReasonMax = 4 };
const unsigned int kNotShownSize = 9;
enum { kNsDecodeOk = 0, kNsDecodeShort = 1, kNsDecodeBadUid = 2, kNsDecodeBadReason = 3 };
inline const char* NotShownReasonName(int r)
{
    switch (r)
    {
    case kNsNoRow:         return "no free row - its table is full";
    case kNsIndexFull:     return "its hash index is full";
    case kNsDestroyedAddr: return "the address was destroyed and may not keep its old uid";
    case kNsOtherUid:      return "the address is registered there under another uid";
    default:               return "?";
    }
}
/* Bytes written (kNotShownSize), or 0 (nothing written) for a buffer too small, uid 0 or a reason outside 1..4. */
inline unsigned int EncodeNotShown(unsigned char* p, unsigned int cap, unsigned int uid, int reason, unsigned int refusals)
{
    if (p == 0 || cap < kNotShownSize || uid == 0 || reason < kNsNoRow || reason > kNsReasonMax) return 0;
    for (int i = 0; i < 4; ++i) p[i] = (unsigned char)((uid >> (8 * i)) & 0xFFu);
    p[4] = (unsigned char)reason;
    for (int i = 0; i < 4; ++i) p[5 + i] = (unsigned char)((refusals >> (8 * i)) & 0xFFu);
    return kNotShownSize;
}
/* Nothing is written on a refusal. A longer payload is read up to kNotShownSize. */
inline int DecodeNotShown(const unsigned char* p, unsigned int size, unsigned int* uid, int* reason, unsigned int* refusals)
{
    if (p == 0 || uid == 0 || reason == 0 || refusals == 0 || size < kNotShownSize) return kNsDecodeShort;
    unsigned int u = 0, n = 0;
    for (int i = 0; i < 4; ++i) { u |= ((unsigned int)p[i]) << (8 * i); n |= ((unsigned int)p[5 + i]) << (8 * i); }
    const int r = (int)p[4];
    if (u == 0) return kNsDecodeBadUid;
    if (r < kNsNoRow || r > kNsReasonMax) return kNsDecodeBadReason;
    *uid = u; *reason = r; *refusals = n;
    return kNsDecodeOk;
}
/* The copy's game: did this SPAWN end in a table refusal?  `before` / `after` are the peer-copy refusal counters (spawnRefusedFull
   + twinRefusedFull) read around ApplyRemoteSpawn; any other failure (an unknown template, a faction that does not resolve) moves
   neither and tells nobody. */
inline int SpawnRefusedByTable(long long before, long long after)
{
    return after > before ? 1 : 0;
}
/* The owner: a NOT_SHOWN naming a character this game runs is counted as ours; one naming anything else (a stale uid, another
   game's character) is counted apart and changes nothing. */
enum { kNsOwnerCounted = 0, kNsOwnerNotMine = 1 };
inline int NotShownOwnerAction(int uidMine)
{
    return uidMine ? kNsOwnerCounted : kNsOwnerNotMine;
}

}   /* namespace coopuid */

#endif
