/* src/common/hovground.h - T-1039 (ported from pull request 4, outside contributor, task 3; review folds 2026-11-03): WHICH GROUND
 * ITEM A HAND-OVER LINE MAY TAKE FOR ITS OWN, after a restart.
 *
 * A hand-over line from the ground (`g:<name>`, T-889) stores two things about the item at the take: its shared NAME (often made on
 * another game - the dropper's key, whose height differs on each game, items.cpp GrFind's note) and its SPOT on this game (the line's
 * section, the item's position read here at the take). After a restart this game keeps no shared names, so the reloaded item carries
 * none. It used to be taken as any same stack within 0.5 units, so another player's identical stack close by could be removed.
 *
 * THE LOOKUP ORDER over this game's loaded ground items of the line's record (GroundLineFind):
 *   1. an item carrying the stored name (fields matching - HovItemIs);
 *   2. an unnamed item whose key IS the stored name (compared parsed, never as text);
 *   3. an unnamed item lying exactly at the line's spot (tenths, height included);
 *   4. an unnamed item within kGroundSpotTenths FLAT (x, z; height left out - it differs between games and a load may settle it) of
 *      the name's or the spot's place - taken only when it is the ONLY such stack; two or more: WAIT.
 *   An item carrying ANOTHER name is never the line's item (the ground's naming rule, groundname.h NameLookup).
 *   Nothing found: ABSENT only when every loaded ground item was walked (the list not cut short) - the proof the item is not here;
 *   a cut-short list is WAIT. Only ABSENT lets the holder rebuild.
 * THE WAIT is bounded (GroundLookAnswer): after kGroundWaitMax waits of one line in one load it is "here, but not identifiable" - a
 * look answers 1 (it is here: never a rebuild over it), a removal removes nothing (the line closes; at worst a duplicate stays,
 * never a wrong deletion). Pure: compiled by the game and by the offline suite. */
#pragma once
#include "groundkey.h"

namespace hovrec
{
/* one loaded ground item of the line's record: cur its key parts now; named 0 none, 1 the line's stored name, 2 another name;
   fields 1 = HovItemIs (record, stack size, quality, level) */
struct GroundCand { coopground::GroundKeyParts cur; int named; int fields; };
const int kGfFound = 0, kGfAbsent = 1, kGfWait = 2;
const int kGfByName = 1, kGfByKey = 2, kGfBySpot = 3, kGfNear = 4;
/* 1 tenth flat: the line's spot and the item's reloaded place are both this game's (a reload restores the saved position; only the
   rounding of %.3f to tenths can move it one tenth), and the name's x, z are the item's on every game (a receiver places at the key's
   own tenths and the item settles straight down - only its height differs). Two stacks 0.2 units apart are never confused. */
const long long kGroundSpotTenths = 1;
const int kGroundWaitMax = 10;   /* verdicts (asked every 60 s): about 10 minutes */

/* T-1039 fold 2: per axis (|dx| and |dz| each at most one tenth) - the keys are in tenths, so a one-tenth step on both axes (0.14 as a
   distance) is still the same spot's rounding; a round test of radius 0.1 rejected it. */
inline int GroundFlatNear(const coopground::GroundKeyParts& a, long x10, long z10)
{
    const long long dx = (long long)a.x10 - x10, dz = (long long)a.z10 - z10;
    return (dx <= kGroundSpotTenths && dx >= -kGroundSpotTenths && dz <= kGroundSpotTenths && dz >= -kGroundSpotTenths) ? 1 : 0;
}
/* T-1039 fold 1: the ground walk PROVES the line's item absent (GroundLineFind's listComplete) only when ALL hold: the line's spot's zone
   is loaded here (spotLoaded: the position test and the engine's zone-loaded byte - the area verdict answers "mine" during the load
   window with no loaded test), nothing cut the walk short (trunc), the zone list and every zone were read and no ground item was dropped
   for an unreadable state or position (unread), the walk stayed under its cap, and no item of the line's item lacked a readable key. */
inline int GroundWalkComplete(int spotLoaded, int trunc, int unread, int walked, int cap, int keyless)
{
    return (spotLoaded != 0 && trunc == 0 && unread == 0 && walked < cap && keyless == 0) ? 1 : 0;
}
inline const char* GroundWalkCutWhy(int spotLoaded, int trunc, int unread, int walked, int cap, int keyless)
{
    return spotLoaded == 0 ? "the line's spot is not loaded here" : trunc != 0 ? "the zone walk was cut short"
         : unread != 0 ? "the zone list, a zone or a ground item could not be read" : walked >= cap ? "the walk reached its cap"
         : keyless != 0 ? "a matching item had no readable key" : "complete";
}
/* name: the stored name parsed (nameOk 0: it would not parse); spot: the line's spot in tenths {x, z, y} (0: unreadable);
   listComplete: every loaded ground item walked. Returns kGf*; *idxOut the item (kGfFound), *howOut kGfBy* / kGfNear,
   *nearOut the matching unnamed stacks within the flat tolerance, *otherAtSpot items at the exact key / spot whose fields differ. */
inline int GroundLineFind(const GroundCand* c, int n, const coopground::GroundKeyParts& name, int nameOk, const long* spot,
                          int listComplete, int* idxOut, int* howOut, int* nearOut, int* otherAtSpot)
{
    *idxOut = -1; *howOut = 0; *nearOut = 0; *otherAtSpot = 0;
    for (int i = 0; i < n; ++i)
        if (c[i].named == 1 && c[i].fields != 0) { *idxOut = i; *howOut = kGfByName; return kGfFound; }
    int byKey = -1, bySpot = -1, nearIdx = -1;
    for (int i = 0; i < n; ++i)
    {
        if (c[i].named != 0) continue;   /* another name: another item; the stored name was handled above */
        const int atKey = (nameOk != 0 && coopground::GroundKeySame(c[i].cur, name) != 0) ? 1 : 0;
        const int atSpot = (spot != 0 && c[i].cur.x10 == spot[0] && c[i].cur.z10 == spot[1] && c[i].cur.y10 == spot[2]) ? 1 : 0;
        if (c[i].fields == 0) { if (atKey != 0 || atSpot != 0) ++*otherAtSpot; continue; }
        if (atKey != 0 && byKey < 0) byKey = i;
        if (atSpot != 0 && bySpot < 0) bySpot = i;
        const int near = ((nameOk != 0 && coopground::GroundSidEqual(c[i].cur.sid, name.sid) != 0 && GroundFlatNear(c[i].cur, name.x10, name.z10) != 0)
                          || (spot != 0 && GroundFlatNear(c[i].cur, spot[0], spot[1]) != 0)) ? 1 : 0;
        if (near != 0) { ++*nearOut; if (nearIdx < 0) nearIdx = i; }
    }
    if (byKey >= 0) { *idxOut = byKey; *howOut = kGfByKey; return kGfFound; }
    if (bySpot >= 0) { *idxOut = bySpot; *howOut = kGfBySpot; return kGfFound; }
    if (*nearOut == 1) { *idxOut = nearIdx; *howOut = kGfNear; return kGfFound; }
    if (*nearOut > 1) return kGfWait;
    return listComplete != 0 ? kGfAbsent : kGfWait;
}
/* HovLiveCopy's answer from a find and the line's waits so far in this load (waits counted before this one):
   kGaFound / kGaAbsent / kGaWait (the line kept, asked again) / kGaUnsure (bound reached: here, not identifiable) */
const int kGaFound = 1, kGaAbsent = 0, kGaWait = -1, kGaUnsure = 2;
inline int GroundLookAnswer(int find, int waits)
{
    if (find == kGfFound) return kGaFound;
    if (find == kGfAbsent) return kGaAbsent;
    return waits < kGroundWaitMax ? kGaWait : kGaUnsure;
}
/* HovLiveCopy's return for that answer: a look (doRemove 0) - found / unsure 1, absent 0, wait -1; a removal - unsure 0 (nothing
   removed: the line closes, never a guess deleted), wait -1, absent 0, found: the removal goes on (returned 1 here; the caller removes) */
inline int GroundLookReturn(int answer, int doRemove)
{
    if (answer == kGaWait) return -1;
    if (answer == kGaAbsent) return 0;
    if (answer == kGaUnsure) return doRemove != 0 ? 0 : 1;
    return 1;
}
inline const char* GroundHowWord(int how)
{
    return how == kGfByName ? "by its stored name" : how == kGfByKey ? "by its key (= its stored name)" : how == kGfBySpot ? "at the line's own spot"
         : how == kGfNear ? "the only same stack within 0.1 units flat" : "none";
}
}
