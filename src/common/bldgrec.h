#pragma once
/* src/common/bldgrec.h - T-1333 (design a): ONE TOWN BUILDING AS ONE WORLD-SERVER RECORD.
 *
 * One record per town (world) building the engine lets take damage - a WallBuilding piece (a town's wall segments and towers; plain
 * Building and GatewayBuilding take no damage, the vt+0x1F0 hit returns 0; doors keep their own door records, doorrec.h). Its id is
 * `bldg.<sx>.<sy>.#<hash8>`: the area square of the building's position key (its "@x,y@" field, 0..63 each, plain decimal) and the
 * FNV-1a hash of the whole key in eight lower-case hex digits - the door record's id rule (doorrec.h MakeDoorRecId) with its own
 * prefix. The key travels in the record, so a reader checks that the record names that very building. The record's bytes (the
 * `.platoon` file on the world server, message 28 RECORD on the wire), little-endian:
 *
 *   u32 marker 'BLD1' | u32 format (kBgFormat) | u32 flags (kBgFlagSeed: this game's first write of a building it had no record of) |
 *   u32 generation | i32 writer slot | u32 length + the building's key (1..kBgKeyMax bytes, no control byte) | u8 ruined (+0x1A1) |
 *   f32 wall health (the progress +0x164 of its construction state) | f32 its full health (+0x188, needed). Nothing follows.
 *
 * A record that is cut short, carries an unknown marker, format or flag, a key that makes no id, a ruined byte other than 0 or 1, a
 * health that is not finite or is below 0, or bytes after its last field DECODES TO REFUSED.
 *
 * THE WORLD SERVER'S RULE (BldgJudgeWrite, the door record's DoorJudgeWrite): only the game the area map names as the holder of the
 * building's area writes, and the record names that game as its writer; a seed is taken only while no record of that id exists (the
 * refused seeder is sent the stored record); any other write carries a generation above the stored one (the very same bytes at the
 * stored generation are a harmless repeat). A record is removed (RECORD_GONE) only by the area's holder: a town rebuilt for a new
 * version (TOWN_VER) loses the area's building records (BldgJudgeRemove). Refusals ride boxrec.h's BOX_REFUSE; a taken write is
 * answered to its writer with RECORD_SEQ and forwarded to every other game.
 *
 * A WALL GROUP: walls joined in a group share ONE health - the group's construction state lives at the lead's own +0x160 (the
 * CARRIER), and every member's vt+0x228 returns it. Each piece has its own record (its own ruin flag); the group's health is written
 * on the carrier's record and LAID FROM THE CARRIER'S RECORD ONLY (BgLayAction: isCarrier), so a group's health is applied once, never
 * once per member from records written at different moments.
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include "boxrec.h"    /* the refusal reasons and the BOX_REFUSE message the building record shares */
#include "doorrec.h"   /* the key's area square and hash (DrKeySector, DrHash), the id's area parse (DrParseSector) */

namespace bldgrec {

const unsigned int kProtocol = 177;            /* the world-server protocol that carries building records */


const unsigned int kBgMarker = 0x31444C42u;    /* 'B','L','D','1' little-endian */
const unsigned int kBgFormat = 1u;
const unsigned int kBgFlagSeed = 1u;
const unsigned int kBgKeyMax = 63u;            /* build.cpp kKeyCap - 1 (the position key ObjectPositionKey writes) */
const unsigned int kBgMaxBytes = 256u;
const int kBgMaxSector = 63;
const float kBgMaxNumber = 1.0e7f;
const int kBgBands = 20;                       /* THE STEP: a hit writes when the health crosses a twentieth of full (piecerec.h kPieceLiveDoorBands) */

/* decode results: anything but kBgOk is a refused record */
const int kBgOk = 0, kBgShort = 1, kBgBadMarker = 2, kBgBadFormat = 3, kBgBadRecord = 4, kBgTrailing = 5, kBgTooBig = 6;

struct BldgRec
{
    unsigned int flags, generation;
    int writerSlot;
    std::string key;
    int ruined;
    float progress, needed;
    BldgRec() : flags(0), generation(0), writerSlot(0), ruined(0), progress(0.0f), needed(0.0f) {}
};

/* ---- the id ---- */
inline bool BgKeyOk(const std::string& k)
{
    if (k.empty() || k.size() > (size_t)kBgKeyMax) return false;
    for (size_t i = 0; i < k.size(); ++i) { const unsigned char c = (unsigned char)k[i]; if (c < 0x20 || c == 0x7F) return false; }
    return true;
}
/* the record id of a building key; "" when the key is not one whose area square is on the grid */
inline std::string MakeBldgRecId(const std::string& key)
{
    int sx = -1, sy = -1;
    if (!BgKeyOk(key) || doorrec::DrKeySector(key.c_str(), &sx, &sy) == 0 || sx < 0 || sx > kBgMaxSector || sy < 0 || sy > kBgMaxSector) return std::string();
    char b[48];
    std::sprintf(b, "bldg.%d.%d.#%08x", sx, sy, doorrec::DrHash(key));
    return std::string(b);
}
/* any spelling of the prefix counts (file names ignore case); only the spelling MakeBldgRecId makes parses */
inline bool IsBldgRecId(const std::string& id)
{
    static const char kPrefix[] = "bldg.";
    if (id.size() < 5) return false;
    for (size_t i = 0; i < 5; ++i) { char c = id[i]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c != kPrefix[i]) return false; }
    return true;
}
/* 1 = exactly a spelling MakeBldgRecId makes (its area square into sx / sy when not 0); 0 = not a building record id */
inline int BldgRecIdParse(const std::string& id, int* sx, int* sy)
{
    if (id.compare(0, 5, "bldg.") != 0) return 0;
    size_t at = 5;
    int a = 0, b = 0;
    if (doorrec::DrParseSector(id, &at, '.', &a) == 0 || doorrec::DrParseSector(id, &at, '.', &b) == 0) return 0;
    if (at >= id.size() || id[at] != '#') return 0;
    ++at;
    if (id.size() - at != 8) return 0;
    for (size_t i = at; i < id.size(); ++i) { const char c = id[i]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0; }
    if (sx) *sx = a;
    if (sy) *sy = b;
    return 1;
}
/* the plugin's area index (sx * 64 + sy) of a building key; -1 when the key names no area on the grid */
inline int BgKeyArea(const std::string& key)
{
    int sx = -1, sy = -1;
    if (doorrec::DrKeySector(key.c_str(), &sx, &sy) == 0 || sx < 0 || sx > kBgMaxSector || sy < 0 || sy > kBgMaxSector) return -1;
    return sx * 64 + sy;
}

/* ---- validity ---- */
inline bool BgNumberOk(float f) { return f == f && f >= 0.0f && f < kBgMaxNumber; }
inline bool BldgRecValid(const BldgRec& r)
{
    return (r.flags & ~kBgFlagSeed) == 0 && !MakeBldgRecId(r.key).empty() && (r.ruined == 0 || r.ruined == 1) && BgNumberOk(r.progress) && BgNumberOk(r.needed);
}

/* ---- bytes ---- */
/* false = nothing written (*out emptied): the record breaks BldgRecValid */
inline bool EncodeBldgRec(std::vector<char>* out, const BldgRec& r)
{
    if (out == 0) return false;
    out->clear();
    if (!BldgRecValid(r)) return false;
    doorrec::DrPutU32(out, kBgMarker);
    doorrec::DrPutU32(out, kBgFormat);
    doorrec::DrPutU32(out, r.flags);
    doorrec::DrPutU32(out, r.generation);
    doorrec::DrPutU32(out, (unsigned int)r.writerSlot);
    doorrec::DrPutU32(out, (unsigned int)r.key.size());
    out->insert(out->end(), r.key.begin(), r.key.end());
    out->push_back((char)r.ruined);
    doorrec::DrPutF32(out, r.progress);
    doorrec::DrPutF32(out, r.needed);
    return true;
}
/* kBgOk: *out holds the record; anything else: refused, *out untouched */
inline int DecodeBldgRec(const char* p, size_t size, BldgRec* out)
{
    if (p == 0 || size < 24u) return kBgShort;
    if (size > (size_t)kBgMaxBytes) return kBgTooBig;
    size_t off = 0;
    unsigned int marker = 0, format = 0, len = 0;
    BldgRec r;
    doorrec::DrGet4(p, size, &off, &marker);
    if (marker != kBgMarker) return kBgBadMarker;
    doorrec::DrGet4(p, size, &off, &format);
    if (format != kBgFormat) return kBgBadFormat;
    if (!doorrec::DrGet4(p, size, &off, &r.flags) || !doorrec::DrGet4(p, size, &off, &r.generation) || !doorrec::DrGet4(p, size, &off, &r.writerSlot)
        || !doorrec::DrGet4(p, size, &off, &len)) return kBgShort;
    if (len == 0 || len > kBgKeyMax) return kBgBadRecord;
    if (size - off < (size_t)len) return kBgShort;
    r.key.assign(p + off, len);
    off += len;
    if (!doorrec::DrGet1(p, size, &off, &r.ruined) || !doorrec::DrGet4(p, size, &off, &r.progress) || !doorrec::DrGet4(p, size, &off, &r.needed)) return kBgShort;
    if (off != size) return kBgTrailing;
    if (!BldgRecValid(r)) return kBgBadRecord;
    if (out) *out = r;
    return kBgOk;
}
inline const char* BldgDecodeWhy(int why)
{
    return why == kBgOk ? "ok" : why == kBgShort ? "cut short" : why == kBgBadMarker ? "not a building record (wrong marker)"
         : why == kBgBadFormat ? "an unknown format" : why == kBgBadRecord ? "a field out of range" : why == kBgTrailing ? "bytes after the last field"
         : why == kBgTooBig ? "larger than the byte cap" : "malformed";
}

/* ---- the world server ---- */
/* One write of building record `id` from slot `senderSlot`; holderSlot: the slot the area map names as the holder of the record's
   area (-1 nobody); exists / storedGen: a stored record and its generation; sameBytes: the bytes equal the stored file.
   coopbrec::kBrAccept, kBrRepeat (nothing to write) or a coopbrec refusal reason - the door record's rule, unchanged. */
inline int BldgJudgeWrite(const std::string& id, const char* p, size_t size, int senderSlot, int holderSlot, int exists, unsigned int storedGen,
                          int sameBytes, BldgRec* out)
{
    if (BldgRecIdParse(id, 0, 0) == 0) return coopbrec::kBrRefuseBadId;
    BldgRec r;
    if (DecodeBldgRec(p, size, &r) != kBgOk) return coopbrec::kBrRefuseUnreadable;
    if (out != 0) *out = r;
    if (MakeBldgRecId(r.key) != id) return coopbrec::kBrRefuseWrongBox;
    if (senderSlot < 0 || r.writerSlot != senderSlot) return coopbrec::kBrRefuseWriter;
    if (holderSlot < 0 || senderSlot != holderSlot) return coopbrec::kBrRefuseNotWriter;
    if (exists != 0)
    {
        if ((r.flags & kBgFlagSeed) != 0) return coopbrec::kBrRefuseSeedExists;
        if (r.generation < storedGen) return coopbrec::kBrRefuseOlder;
        if (r.generation == storedGen) return (sameBytes != 0) ? coopbrec::kBrRepeat : coopbrec::kBrRefuseSameGen;
    }
    return coopbrec::kBrAccept;
}
/* A removal of building record `id`: only the area's holder removes one (a town rebuilt for a new version) */
inline int BldgJudgeRemove(const std::string& id, int senderSlot, int holderSlot)
{
    if (BldgRecIdParse(id, 0, 0) == 0) return coopbrec::kBrRefuseBadId;
    if (senderSlot < 0 || holderSlot < 0 || senderSlot != holderSlot) return coopbrec::kBrRefuseNotWriter;
    return coopbrec::kBrAccept;
}

/* ---- the game: which change is written ---- */
/* the health step of a wall: 0 at no health, 1..kBgBands below full, kBgBands + 1 at full (piecerec.h PieceLiveBandOf's rule) */
inline int BgBand(float progress, float needed)
{
    if (!(needed > 0.0f) || !(needed < kBgMaxNumber)) needed = 1.0f;
    if (!(progress > 0.0001f)) return 0;
    if ((double)progress >= (double)needed * (1.0 - 1e-6)) return kBgBands + 1;
    int b = 1 + (int)std::floor((double)progress / (double)needed * (double)kBgBands);
    if (b > kBgBands) b = kBgBands;
    if (b < 1) b = 1;
    return b;
}
/* what the engine just did to the building: its hit 0x29D520, its ruin flip setDestroyed 0x5567F0, its repair 0x558B10 */
const int kBgCauseHit = 1, kBgCauseRuin = 2, kBgCauseRepair = 3;
/* the facts a change moved against the building's record (no record: a whole, standing wall - the area file's state is never written
   as a change of its own): kBgChRuin the ruin flag, kBgChStep the health crossed a step. A repair counts only when it ADDS health (or
   makes a ruin whole); a hit only lowers it. */
const int kBgChRuin = 1, kBgChStep = 2;
inline int BgChangeMask(int haveRec, const BldgRec& rec, const BldgRec& live, int cause)
{
    const int baseRuined = (haveRec != 0) ? rec.ruined : 0;
    const float baseNeeded = (haveRec != 0 && rec.needed > 0.0f) ? rec.needed : live.needed;
    const float baseProgress = (haveRec != 0) ? rec.progress : baseNeeded;
    int m = 0;
    if (live.ruined != baseRuined) m |= kBgChRuin;
    const int stepMoved = (BgBand(live.progress, live.needed) != BgBand(baseProgress, baseNeeded)) ? 1 : 0;
    if (cause == kBgCauseRepair)
    {
        if ((m & kBgChRuin) != 0 && live.ruined != 0) m &= ~kBgChRuin;   /* a repair never ruins */
        if (stepMoved != 0 && live.progress > baseProgress) m |= kBgChStep;
        return m;
    }
    if (cause == kBgCauseHit)
    {
        if (stepMoved != 0 && live.progress < baseProgress) m |= kBgChStep;
        return m;
    }
    return m & kBgChRuin;   /* a ruin flip: the flag (its health rides with it) */
}
inline std::string BgChangeWords(int m)
{
    std::string s;
    if (m & kBgChRuin) s += " ruin-flag";
    if (m & kBgChStep) s += " health-step";
    return s.empty() ? std::string(" nothing") : s;
}
/* Whether this game writes the record for a change. weHold: the holder of the key's area is this game (1), another game or nobody (0),
   not known (-1); laid: the area's records are laid here (a change seen before the lay is the area file's or the load's, and the
   record wins it); mask: BgChangeMask; selfWrite: the change is the mod's own lay of a record (never written back). */
const int kBgWrite = 0, kBgSkipNotHolder = 1, kBgSkipUnknown = 2, kBgSkipNotLaid = 3, kBgSkipNoChange = 4, kBgSkipSelf = 5;
inline int BgWriteDecide(int weHold, int laid, int mask, int selfWrite)
{
    if (selfWrite != 0) return kBgSkipSelf;
    if (weHold == 0) return kBgSkipNotHolder;
    if (weHold < 0) return kBgSkipUnknown;
    if (laid == 0) return kBgSkipNotLaid;
    if (mask == 0) return kBgSkipNoChange;
    return kBgWrite;
}

/* ---- the game: laying a record ---- */
/* What laying record `rec` on a building whose live values are `live` does, through the engine's own functions (as a player piece's
   copy is made a ruin or whole again, build.cpp): kBgLayWhole - setDestroyed(0) (vt+0x340), kBgLayRuin - setDestroyed(1), kBgLayHealth
   - the construction state's progress set to *healthTo (the HIT result's write, BdApplyStatePod). The health is laid only from the
   CARRIER's record (isCarrier: the building holds its group's state at its own +0x160, or it is in no group) and only on a standing
   wall: a group's health is applied once. The ruin flag is every piece's own. */
const int kBgLayWhole = 1, kBgLayRuin = 2, kBgLayHealth = 4;
inline int BgLayAction(const BldgRec& rec, const BldgRec& live, int isCarrier, float* healthTo)
{
    int a = 0;
    if (rec.ruined != 0 && live.ruined == 0) a |= kBgLayRuin;
    if (rec.ruined == 0 && live.ruined != 0) a |= kBgLayWhole;
    if (rec.ruined == 0 && isCarrier != 0 && live.needed > 0.0f)
    {
        float to = rec.progress;
        if (rec.needed > 0.0f && rec.needed != live.needed) to = rec.progress / rec.needed * live.needed;   /* the record's share of full */
        if (to > live.needed) to = live.needed;
        if (to < 0.0f) to = 0.0f;
        if (std::fabs(to - live.progress) > 0.001f * (live.needed > 1.0f ? live.needed : 1.0f) || (a & kBgLayWhole) != 0)
        {
            a |= kBgLayHealth;
            if (healthTo != 0) *healthTo = to;
        }
    }
    return a;
}
/* A wall group's lay plan: pieces[i] is a piece of one area, carrier[i] the index of its group's carrier among them (itself when it
   is in no group or is the carrier), haveRec[i] whether a record of it is kept. Returns, per piece, whether ITS record's health is laid:
   only a carrier's own record lays health, so each group's health is laid at most once and only from the carrier's record. */
inline std::vector<int> BgGroupHealthPlan(const std::vector<int>& carrier, const std::vector<int>& haveRec)
{
    std::vector<int> out(carrier.size(), 0);
    for (size_t i = 0; i < carrier.size() && i < haveRec.size(); ++i)
        out[i] = (carrier[i] == (int)i && haveRec[i] != 0) ? 1 : 0;
    return out;
}

/* ---- the game: who runs a hit ---- */
/* A melee hit on a TOWN wall (no player owns it) or a town building's door, in a game that is not single-player (multi: RoleIsSingle
   is false - whatever the player count, fold 2: one road for one player or many) and not the mod's own replay. copyAttacker: the blow's attacker is a COPY on this game of a character another game runs (a uid this game does not own - a
   player's character or an NPC; combat.cpp's puppet test for a blow on a character, F349): the blow is DROPPED here (kBgHitDropCopy) -
   the attacker's own game swings it too and counts it there (runs it as the holder, or asks the holder), so a building hit counts once.
   Otherwise, with the damage read, the hit is HELD (kBgHitHold: the hook then decides, BgWallHitKept for a wall; a door is kept only on
   a game that does not hold its area). Any other case runs the engine's hit where it landed. index: a door's index in its building's
   door list (-1 for a wall); maxDoor: the HIT wire's cap. */
const int kBgHitEngine = 0, kBgHitHold = 1, kBgHitDropCopy = 2;
inline int BgTownHitRoute(int target, int index, int maxDoor, int playerOwned, int multi, int damageRead, int replay, int copyAttacker)
{
    if (target == 0 || playerOwned != 0 || multi == 0 || replay != 0) return kBgHitEngine;
    if (copyAttacker != 0) return kBgHitDropCopy;
    if (damageRead == 0) return kBgHitEngine;
    if (index > maxDoor) return kBgHitEngine;
    return kBgHitHold;
}
/* Decision 1609 a: a blow on a PLAYER-BUILT wall or a player piece's door whose attacker is a COPY here of a character another game runs
   (TbAttackerIsCopy - combat.cpp's F349 test) is DROPPED, as a town building's is: the attacker's own game swings it too and sends it to
   the piece's writer (or runs it as the writer), so it counts once. Only while another player is known (no copies otherwise) and never
   for the mod's own replay. target: the hit lands on a wall / a door of a piece. 1 = drop */
inline int BgPlayerHitDropCopy(int target, int playerOwned, int othersKnown, int replay, int copyAttacker)
{
    return (target != 0 && playerOwned != 0 && othersKnown != 0 && replay == 0 && copyAttacker != 0) ? 1 : 0;
}
/* A town WALL's hit the route holds: kept from the engine where it lands (queued for the main thread) unless this game holds the area
   AND the area's records are laid here - then the engine's hit runs at once and its record is written. A holder's hit before the lay
   is kept and run after the lay (the lay would lay the record over it), and a hit while the holder is not known is kept until it is
   (fold 1 A / C). weHold as BgWriteDecide's. 1 = kept */
inline int BgWallHitKept(int weHold, int laid) { return (weHold == 1 && laid != 0) ? 0 : 1; }
/* The main thread's decision for a held town hit (or a town wall repair's gain made here): weHold as BgWriteDecide's; holderSlot the
   holder's slot (-1 unknown); laid: the area's records are laid here; door: a door's hit. This game holds: the engine's hit runs here
   (a repair: its record is written) - a wall's only once the area's records are laid, before that it waits for the lay (kBgHoldToLay,
   run by TbAreaLay). Another game holds: asked of it (HIT ask naming the building's key). Not known: a wall's hit or repair waits for
   the holder to be known (kBgWaitHolder, an area map event); a door's runs here - its door record holds its change until the holder
   is known (doorrec.h kDrHoldUntilLaid). */
const int kBgRunHere = 0, kBgAskHolder = 1, kBgRunLocalNoHolder = 2, kBgHoldToLay = 3, kBgWaitHolder = 4;
inline int BgHeldHitDecide(int weHold, int holderSlot, int laid, int door)
{
    if (weHold == 1) return (laid != 0 || door != 0) ? kBgRunHere : kBgHoldToLay;
    if (weHold == 0 && holderSlot >= 0) return kBgAskHolder;
    return (door != 0) ? kBgRunLocalNoHolder : kBgWaitHolder;
}
/* A town wall REPAIR whose gain the engine already made here, decided by this game as the holder after a wait (for the lay or for
   the holder). laidSince: the area's records were laid here after the repair ran (the lay set the group's health to the record's, so
   the gain is no longer on the wall); haveRec: a record of the wall group's carrier is kept. kBgRepairAddGain: the gain is added again
   over the laid record (TbRepairHere) and written; kBgRepairWriteLive: the wall's health still holds the gain - written as it is
   (BgChangeMask decides). */
const int kBgRepairWriteLive = 0, kBgRepairAddGain = 1;
inline int BgRepairAfterWait(int laidSince, int haveRec) { return (laidSince != 0 && haveRec != 0) ? kBgRepairAddGain : kBgRepairWriteLive; }
/* Fold 2: the kept hits and repair gains of ONE town wall, one entry per wall key (so the kept list is bounded by walls, not by hits,
   and nothing is dropped for its length). hits / cut / blunt: how many hits and their summed damage numbers. They are run again as
   `hits` hits of the average, never as one hit of the sum: the engine's hit 0x29D520 clamps each hit's weighed damage to [10, 60]
   before taking it off the health (Read, build/decomp_29d520.txt and the 1.0.65 exe's constants), so one summed hit would lose
   damage. gainGone: repair gains made here that a lay since then laid a kept record over (no longer on the wall - added again);
   gainLive: gains made at the area's lay count `serial` (still on the wall unless a later lay laid a record over them). */
struct BgWaitSum
{
    int hits;
    double cut, blunt;
    int gainHave;
    double gainGone, gainLive;
    unsigned int serial;
    BgWaitSum() : hits(0), cut(0.0), blunt(0.0), gainHave(0), gainGone(0.0), gainLive(0.0), serial(0u) {}
};
inline void BgWaitAddHits(BgWaitSum* w, int hits, double cut, double blunt)
{
    if (w == 0 || hits <= 0) return;
    w->hits += hits; w->cut += cut; w->blunt += blunt;
}
/* a repair gain made at the area's lay count `serial`; haveRec: a record of the wall group's carrier is kept here (a lay sets the
   group's health from it). Gains made before a lay that laid a record are moved to gainGone, as BgRepairAfterWait decides one. */
inline void BgWaitAddGain(BgWaitSum* w, double gain, unsigned int serial, int haveRec)
{
    if (w == 0 || !(gain > 0.0)) return;
    if (w->gainHave != 0 && w->serial != serial && BgRepairAfterWait(1, haveRec) == kBgRepairAddGain) { w->gainGone += w->gainLive; w->gainLive = 0.0; }
    w->gainLive += gain;
    w->serial = serial;
    w->gainHave = 1;
}
/* a released entry kept again (still waiting) folded into the entry kept since for the same wall: the counts add, gains by their lay */
inline void BgWaitMerge(BgWaitSum* into, const BgWaitSum& from, int haveRec)
{
    if (into == 0) return;
    BgWaitAddHits(into, from.hits, from.cut, from.blunt);
    if (from.gainHave == 0) return;
    into->gainGone += from.gainGone;
    if (into->gainHave == 0) { into->gainLive = from.gainLive; into->serial = from.serial; into->gainHave = 1; return; }
    BgWaitAddGain(into, from.gainLive, from.serial, haveRec);
}
/* The gain the holder adds again over the wall now (the area's lay count serialNow): the gains a lay laid a record over */
inline double BgWaitGainToAdd(const BgWaitSum& w, unsigned int serialNow, int haveRec)
{
    if (w.gainHave == 0) return 0.0;
    return w.gainGone + ((BgRepairAfterWait(w.serial != serialNow ? 1 : 0, haveRec) == kBgRepairAddGain) ? w.gainLive : 0.0);
}
/* How many of a released wall's kept hits are asked of the holder now: as many as the ask list has room for (outUsed of outCap); the
   rest stay kept and go at the next area map - never past the list's cap, where the oldest ask would be dropped */
inline int BgWaitAskNow(int hits, size_t outUsed, size_t outCap)
{
    if (hits <= 0 || outUsed >= outCap) return 0;
    const size_t room = outCap - outUsed;
    return ((size_t)hits < room) ? hits : (int)room;
}
/* The kept list's line: only a wall's FIRST keep writes one; a hit added to a kept wall and a re-keep at an area map (the holder still
   not known, or the holder before its lay) are counted only. 1 = write the line */
inline int BgWaitKeepLine(int created, int rekeep) { return (created != 0 && rekeep == 0) ? 1 : 0; }
/* The asking game's side of the holder's answer to its town WALL REPAIR ask (door -1): 'ruin' - the holder's wall is a ruin while this
   game's own repair ran here (the engine's repair 0x558B10 makes a ruin whole: it resets the progress and calls setDestroyed(0) -
   Read, .modding/05-findings.md:20023) - its wall is laid a ruin again, so it matches the holder (fold 1 D). 1 = lay the ruin */
inline int BgRepairRuinUndo(int repair, int door, int answerRuin) { return (repair != 0 && door < 0 && answerRuin != 0) ? 1 : 0; }
/* The holder's answer to a HIT ask naming a town building: not the holder now - 'not the writer' (the asker asks once more); not loaded
   here - 'not loaded'; a ruined wall takes no hit and no repair gain - 'ruin'; a resend of an ask already run - applied, not run again;
   else run (applied). Values are buildwire.h's HIT verdicts, passed in. */
inline int BgHitAskVerdict(int weHold, int loaded, int ruined, int dup, int vApplied, int vNotWriter, int vNotLoaded, int vRuin)
{
    if (weHold != 1) return vNotWriter;
    if (loaded == 0) return vNotLoaded;
    if (dup != 0) return vApplied;
    if (ruined != 0) return vRuin;
    return vApplied;
}

/* ---- the game: records kept ---- */
/* the next generation this game writes: above the record it knows and above any generation a refusal named */
inline unsigned int BgNextGen(int haveRec, unsigned int gen, unsigned int floorGen) { return doorrec::DoorRecNextGen(haveRec, gen, floorGen); }
/* a received record is kept when nothing is kept for its id or its generation is not below the kept one */
inline int BgArrivalTakes(int haveKept, unsigned int keptGen, unsigned int gen) { return doorrec::DoorRecArrivalTakes(haveKept, keptGen, gen); }
/* a refusal: older / same generation - written again above it (at most doorrec::kDrRewriteCap times); a seed where a record exists -
   the stored record wins (sent back and laid); anything else dropped (the door record's rule) */
inline int BgRefuseNext(int reason, int rewrites) { return doorrec::DoorRecRefuseNext(reason, rewrites); }
/* A town REBUILT here for a new version (TOWN_VER): at the area's first lay after it, the area's kept building records are never laid
   (they describe the old town); the holder removes them from the world server, every game forgets them. A removal whose holder is not
   known yet waits for the area map (an event); another game holding the area drops it (that game removes them). */
const int kBgGoneWait = 0, kBgGoneSend = 1, kBgGoneDrop = 2;
inline int BgGoneDecide(int weHold) { return (weHold < 0) ? kBgGoneWait : ((weHold == 1) ? kBgGoneSend : kBgGoneDrop); }

/* ---- TEST-ONLY lever `buildtest townhit <key> <cut> <blunt> [door | door=<i>] [times]` (build.cpp BdTestTownHit) ----
   The words after `townhit`: a town building's whole position key (no space; at most kBgKeyMax bytes), the two damage numbers the
   engine's melee hit weighs (cut, blunt: plain decimals in [0, maxDamage]), then optionally `door` (the building's door 0) or
   `door=<i>` (door i, 0..maxDoor), then optionally how many hits in one safe point (1..200, default 1). door -1 = the building itself
   (a wall). 1 = parsed into *out, 0 = refused (*out untouched). */
struct BgTownHitLever { std::string key; float cut; float blunt; int door; int times; };
inline int BgLeverNumber(const std::string& s, float lo, float hi, float* out)
{
    if (s.empty() || s.size() > 24u) return 0;
    int dot = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '.') { if (++dot > 1) return 0; }
        else if (s[i] < '0' || s[i] > '9') return 0;
    }
    if (s == ".") return 0;
    const double v = std::atof(s.c_str());
    if (!(v >= (double)lo && v <= (double)hi)) return 0;
    *out = (float)v;
    return 1;
}
inline int BgTownHitLeverParse(const std::vector<std::string>& w, float maxDamage, int maxDoor, BgTownHitLever* out)
{
    if (w.size() < 3u || w.size() > 5u) return 0;
    const std::string& key = w[0];
    if (key.empty() || key.size() > (size_t)kBgKeyMax || BgKeyArea(key) < 0) return 0;
    for (size_t i = 0; i < key.size(); ++i) if ((unsigned char)key[i] <= 0x20u) return 0;
    BgTownHitLever r;
    r.key = key; r.door = -1; r.times = 1;
    if (!BgLeverNumber(w[1], 0.0f, maxDamage, &r.cut) || !BgLeverNumber(w[2], 0.0f, maxDamage, &r.blunt)) return 0;
    size_t at = 3;
    if (at < w.size() && w[at].compare(0, 4, "door") == 0)
    {
        if (w[at] == "door") r.door = 0;
        else if (w[at].size() > 5u && w[at][4] == '=')
        {
            float d = 0.0f;
            const std::string n = w[at].substr(5);
            if (n.find('.') != std::string::npos || !BgLeverNumber(n, 0.0f, (float)maxDoor, &d)) return 0;
            r.door = (int)d;
        }
        else return 0;
        ++at;
    }
    if (at < w.size())
    {
        float t = 0.0f;
        if (w[at].find('.') != std::string::npos || !BgLeverNumber(w[at], 1.0f, 200.0f, &t)) return 0;
        r.times = (int)t;
        ++at;
    }
    if (at != w.size()) return 0;
    *out = r;
    return 1;
}

}   /* namespace bldgrec */
