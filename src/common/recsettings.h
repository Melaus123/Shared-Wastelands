/* src/common/recsettings.h - T-1429 s10 (F1433, design A): THE RECORD'S SQUAD SETTINGS, put into a group's squad record (type 0x1E) for
 * a record's write and taken out after it, by the money's own rule (squadlead.h coopsquad::RecMoneyFieldAtSave / RecMoneyFieldAfterSave):
 * a field read live is SET for the save; a field not read with one already there (brought by a record read into that container) is
 * ERASED for the save, so a stale value is never written; after the save the field is given back as it was (restored, or erased when
 * it was not there). The six keys and kinds are the engine's own (ownrec.h kRecSet*Key). Templated over the record type: the plugin
 * passes the engine's GameData (its boolFields / intFields / floatFields / stringFields hash maps), the offline suite a stand-in
 * with std::map members of the same names - both compile the same code.
 *
 * Pure: no engine memory, no Windows, no globals. C++03 (VS2010 v100).
 */
#pragma once

#include <string>

#include "ownrec.h"
#include "squadlead.h"

namespace recset {

/* one field: put (`read` 1 = the live value v is set) - *had / *old: what was there; returns the at-save step (kRecField*) */
template <class M, class V>
inline int FieldPut(M& m, const std::string& key, int read, const V& v, int* had, V* old)
{
    typename M::iterator it = m.find(key);
    *had = (it != m.end()) ? 1 : 0;
    if (*had != 0) *old = it->second;
    const int at = coopsquad::RecMoneyFieldAtSave(read, *had);
    if (at == coopsquad::kRecFieldSet) m[key] = v;
    else if (at == coopsquad::kRecFieldErase) m.erase(it);
    return at;
}
/* one field after the save: as it was before the put */
template <class M, class V>
inline void FieldTake(M& m, const std::string& key, int at, int had, const V& old)
{
    const int after = coopsquad::RecMoneyFieldAfterSave(at, had);
    if (after == coopsquad::kRecFieldRestore) m[key] = old;
    else if (after == coopsquad::kRecFieldErase)
    {
        typename M::iterator it = m.find(key);
        if (it != m.end()) m.erase(it);
    }
}
const int kFields = 6;   /* in the order of the kRecSet* bits: type, town, resident, prison, index, restock */
struct Saved
{
    int         at[kFields], had[kFields];
    int         oType, oIndex;
    bool        oResident, oPrison;
    float       oRestock;
    std::string oTown;
    Saved() : oType(0), oIndex(0), oResident(false), oPrison(false), oRestock(0.0f) { for (int i = 0; i < kFields; ++i) { at[i] = coopsquad::kRecFieldKeep; had[i] = 0; } }
};
/* the settings `live` (live.has: the ones read) into the squad record st for a save */
template <class R>
inline Saved Put(R* st, const coopown::RecSettings& live)
{
    Saved s;
    if (st == 0) return s;
    const int h = live.has;
    s.at[0] = FieldPut(st->intFields, std::string(coopown::kRecSetTypeKey), (h & coopown::kRecSetType) != 0 ? 1 : 0, live.type, &s.had[0], &s.oType);
    s.at[1] = FieldPut(st->stringFields, std::string(coopown::kRecSetTownKey), (h & coopown::kRecSetTown) != 0 ? 1 : 0, live.town, &s.had[1], &s.oTown);
    s.at[2] = FieldPut(st->boolFields, std::string(coopown::kRecSetResidentKey), (h & coopown::kRecSetResident) != 0 ? 1 : 0, live.resident != 0, &s.had[2], &s.oResident);
    s.at[3] = FieldPut(st->boolFields, std::string(coopown::kRecSetPrisonKey), (h & coopown::kRecSetPrison) != 0 ? 1 : 0, live.prison != 0, &s.had[3], &s.oPrison);
    s.at[4] = FieldPut(st->intFields, std::string(coopown::kRecSetIndexKey), (h & coopown::kRecSetIndex) != 0 ? 1 : 0, live.index, &s.had[4], &s.oIndex);
    s.at[5] = FieldPut(st->floatFields, std::string(coopown::kRecSetRestockKey), (h & coopown::kRecSetRestock) != 0 ? 1 : 0, live.restock, &s.had[5], &s.oRestock);
    return s;
}
/* after the save: the squad record as it was before Put */
template <class R>
inline void Take(R* st, const Saved& s)
{
    if (st == 0) return;
    FieldTake(st->intFields, std::string(coopown::kRecSetTypeKey), s.at[0], s.had[0], s.oType);
    FieldTake(st->stringFields, std::string(coopown::kRecSetTownKey), s.at[1], s.had[1], s.oTown);
    FieldTake(st->boolFields, std::string(coopown::kRecSetResidentKey), s.at[2], s.had[2], s.oResident);
    FieldTake(st->boolFields, std::string(coopown::kRecSetPrisonKey), s.at[3], s.had[3], s.oPrison);
    FieldTake(st->intFields, std::string(coopown::kRecSetIndexKey), s.at[4], s.had[4], s.oIndex);
    FieldTake(st->floatFields, std::string(coopown::kRecSetRestockKey), s.at[5], s.had[5], s.oRestock);
}
/* how many fields a Put set / erased (for the counts) */
inline int PutCount(const Saved& s, int step)
{
    int n = 0;
    for (int i = 0; i < kFields; ++i) if (s.at[i] == step) ++n;
    return n;
}
/* the settings a record's maps hold (the state record CreateUnknownSquad hands to the engine): which of the six are there - find, never
   inserted */
template <class BM, class IM, class FM, class SM>
inline coopown::RecSettings Of(const BM& bf, const IM& inf, const FM& ff, const SM& sf)
{
    coopown::RecSettings o;
    { typename IM::const_iterator it = inf.find(std::string(coopown::kRecSetTypeKey)); if (it != inf.end()) { o.type = it->second; o.has |= coopown::kRecSetType; } }
    { typename SM::const_iterator it = sf.find(std::string(coopown::kRecSetTownKey)); if (it != sf.end()) { o.town = it->second; o.has |= coopown::kRecSetTown; } }
    { typename BM::const_iterator it = bf.find(std::string(coopown::kRecSetResidentKey)); if (it != bf.end()) { o.resident = it->second ? 1 : 0; o.has |= coopown::kRecSetResident; } }
    { typename BM::const_iterator it = bf.find(std::string(coopown::kRecSetPrisonKey)); if (it != bf.end()) { o.prison = it->second ? 1 : 0; o.has |= coopown::kRecSetPrison; } }
    { typename IM::const_iterator it = inf.find(std::string(coopown::kRecSetIndexKey)); if (it != inf.end()) { o.index = it->second; o.has |= coopown::kRecSetIndex; } }
    { typename FM::const_iterator it = ff.find(std::string(coopown::kRecSetRestockKey)); if (it != ff.end()) { o.restock = it->second; o.has |= coopown::kRecSetRestock; } }
    return o;
}

/* T-1429 copy settings (F1457 design A): A COPY'S SETTINGS FROM ITS RECORD - a copy of another game's squad (built from its announce)
   is given the settings the squad's newest record carries, the way the engine's squad load 0x7EC550 gives a squad built from its file
   (F1433): only the fields the record carries, each as the load sets it. Kept from the load's road:
   - the TYPE is never taken from a record: it is the owner's live fact and comes with the announce, which also gave the copy its home
     town / home building WITH that type (0x623BB0's step, decomp_623bb0:82 / :103) - a type set here would leave those under the old one;
   - the base town is looked up among the game's TOWN records (0xD): an id this game's data does not hold is LEFT AS IT IS and counted
     (the load would set none); "" is set as none, as the load sets it;
   - the restock time only for a squad whose template is a trader (0x7EC550:443-455);
   - THE STALE RULE: a field is set only from a record NEWER than the one it was last set from (fieldVer, per field: 0 = never set from a
     record - the announce's or the engine's value). Fields this game's engine changes while it runs the people (imprisoned, resident)
     are this game's truth only once it is the squad's writer: before that the record wins; after it nothing fills the copy (an arrival
     is refused while this game writes; the take-over fill runs once, before its first write). */
const int kCopyAtMade = 0, kCopyAtRecord = 1, kCopyAtTakeover = 2;
struct CopyFillPlan
{
    int set;                /* kRecSet* bits to set */
    int typeKept;           /* 1 = the record carries a type: not set (the announce's) */
    int townUnknown;        /* 1 = the record's base town is not in this game's data: not set */
    int restockNotTrader;   /* 1 = the record carries a restock time, the squad is no trader: not set (the load reads none) */
    int older;              /* kRecSet* bits not set because the field was last set from this record or a newer one */
    CopyFillPlan() : set(0), typeKept(0), townUnknown(0), restockNotTrader(0), older(0) {}
};
/* recHas: the fields the record carries; recVersion: its stamp; fieldVer[6]: per field (bit index) the stamp of the record it was last
   set from (0 never); townEmpty: its base town is "" (none); townKnown: the id names a TOWN record of this game's data; isTrader: the
   squad's template's "is trader" */
inline CopyFillPlan CopyFillFields(int recHas, long long recVersion, const long long* fieldVer, int townEmpty, int townKnown, int isTrader)
{
    CopyFillPlan p;
    p.set = recHas & coopown::kRecSetAll;
    if ((p.set & coopown::kRecSetType) != 0) { p.set &= ~coopown::kRecSetType; p.typeKept = 1; }
    if ((p.set & coopown::kRecSetTown) != 0 && townEmpty == 0 && townKnown == 0) { p.set &= ~coopown::kRecSetTown; p.townUnknown = 1; }
    if ((p.set & coopown::kRecSetRestock) != 0 && isTrader == 0) { p.set &= ~coopown::kRecSetRestock; p.restockNotTrader = 1; }
    for (int i = 0; i < kFields; ++i)
    {
        const int bit = 1 << i;
        if ((p.set & bit) != 0 && recVersion <= fieldVer[i]) { p.set &= ~bit; p.older |= bit; }
    }
    return p;
}
/* after a fill: the fields it set name this record as their source */
inline void CopyFieldsStamp(int set, long long recVersion, long long* fieldVer)
{
    for (int i = 0; i < kFields; ++i) if ((set & (1 << i)) != 0) fieldVer[i] = recVersion;
}
/* THE DEDUPE - 1 = read the newest record (stamp recVersion) for the copy now. The copy's mark (markValid: one made for the copy's
   current bind) names the newest record version looked at (version) and whether a record's arrival has looked at it (looked). A version
   already looked at is never read again, whatever came of it (filled, nothing to fill, load failed); a record's ARRIVAL looks only once
   per copy (a writer's heartbeat is no event for a copy - the take-over reads the newest record before this game's first write) */
inline int CopyFillDue(int at, int markValid, int looked, long long version, long long recVersion)
{
    if (markValid == 0) return 1;
    if (recVersion <= version) return 0;
    if (at == kCopyAtRecord && looked != 0) return 0;
    return 1;
}
/* the record's settings as far as a fill set them (the line's verdict judges only those) */
inline coopown::RecSettings CopyFillApplied(const coopown::RecSettings& rec, const CopyFillPlan& p)
{
    coopown::RecSettings a = rec;
    a.has = rec.has & p.set;
    return a;
}

}   // namespace recset
