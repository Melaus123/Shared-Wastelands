#pragma once
// T-1383 (decision 1704; F1068 / F1069): THE CREATOR'S IMPORT BRINGS THE SAVE'S FACTION STANDINGS - the pure half.
//
// Kenshi's import with 0x20 (importGame 0x3788F0 -> 0x9EE610) loads each faction's saved relations through FactionRelations'
// load 0x6B4A70 (1.0.68 0x6B4DC0) with its last argument 1, which keeps a pair only when one side is a player's faction
// (+0x250) - so every NPC-vs-NPC pair keeps the new-game value and the world's table (worldrelwire.h) was seeded from those.
// The save holds every pair: quick.save's faction state records (type 0x25, "gamedata stringID" = the faction's sid) carry
// relationSID<n> (string) with relation<n> / trust<n> / trustNeg<n> (floats), n = 0, 1, ... until a relationSID<n> is missing
// (decomp_6b4a70.txt, decomp_6b3580.txt: the engine's own loop and field names).
//
// THE DESIGN COMPARE (each judged by whether it decides from the real fact - the save's own pairs):
//  (A) PICKED. The mod reads the save's faction records itself, with the engine's own record reader (GameDataContainer::load
//      on the source save's quick.save into a scratch container - the rows GdcLoad / ClearAndDestroy / listRecordsOfType this
//      project already uses), decodes the four fields exactly as 0x6B4A70's loop names them, and the world walk offers the
//      save's relation / trust / trustNeg for every world pair the save holds (and writes it here, so the creator plays on it).
//      Decides from the save itself; only the creator's import with "bring relations" ticked; no new engine address, no new
//      hook, nothing the engine runs differently. This is how proven games seed a shared world from one player's save: the
//      save FILE is the record and the server side reads it (Factorio, Valheim, Terraria / tShock load the host's save as the
//      world) - not a replay of the single-player game's own import with one of its switches flipped.
//  (B) After the import, call the engine's relation load once more per faction with the last argument 0 on the save's
//      records. Also decides from the save, but needs the same save records loaded (A's first half), two new address rows
//      (1.0.65 0x6B4A70 / 1.0.68 0x6B4DC0) and runs more engine code than wanted: the load clears the whole map when the record
//      has a "relations" key (+0x2B8) and rewrites each pair's stateVar list - effects on live AI state not read.
//  (C) Hook 0x6B4A70 and pass 0 while 0x9EE610 runs for the creator's import. Decides from the save too, but adds a hook on a
//      function every load calls and rests on the import call site in 1.0.68 (Inferred only, F1069), with the same clear.
//  A wins: the same fact as B / C with the least engine surface, and every pure piece (the decode, the table, the merge and
//  the world binding) is tested offline.
//
// The FLAGS (ally +0, at-war +2) are not in the save's four fields (0x6B3580 reads only the three floats): a pair's flags stay
// what this game's engine holds. A pair the save does not hold keeps today's value. Player pairs: Kenshi's own import already
// loads every pair with a player's faction on one side (the filter keeps them), and they are never world rows.
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdio>
#include "worldrelwire.h"

namespace saverel {

const int kMaxPerFaction = 4096;        /* the engine's loop index is a u16; 4096 is well past ~104 factions x a few entries */
const unsigned kFactionStateType = 0x25u;   /* the save's faction state records (0x9EE610 lists type 0x25) */

/* the engine's field name: prefix + Ogre::StringConverter::toString(n) (width 0 - plain decimal) */
inline std::string NumKey(const char* prefix, int n)
{
    char b[16]; std::sprintf(b, "%d", n);
    return std::string(prefix) + b;
}

struct Rel { std::string other; float relation, trust, trustNeg; Rel() : relation(0.0f), trust(0.0f), trustNeg(0.0f) {} };

/* ONE faction record's entries, read the way 0x6B4A70 walks them. Lookup: bool Str(const std::string&, std::string*) const and
   bool Flt(const std::string&, float*) const (find, never insert). An entry missing any of its three floats is not used
   (counted in *incomplete) - the engine would read 0 there, which is not the save's value. Returns the entries walked. */
template <class Lookup> int DecodeFaction(const Lookup& lk, std::vector<Rel>* out, int* incomplete)
{
    int n = 0;
    for (; n < kMaxPerFaction; ++n)
    {
        std::string sid;
        if (!lk.Str(NumKey("relationSID", n), &sid)) break;
        Rel r; r.other = sid;
        if (!lk.Flt(NumKey("relation", n), &r.relation) || !lk.Flt(NumKey("trust", n), &r.trust) || !lk.Flt(NumKey("trustNeg", n), &r.trustNeg)) { ++*incomplete; continue; }
        out->push_back(r);
    }
    return n;
}

/* the save's pairs, keyed coopwrel::Key(owner, other); Row.flags unused (0) */
typedef std::map<std::string, coopwrel::Row> Table;
struct ReadStat
{
    long long factions, entries, pairs, incomplete, refused, duplicate;
    ReadStat() : factions(0), entries(0), pairs(0), incomplete(0), refused(0), duplicate(0) {}
};
/* one faction's decoded entries into the table: a bad sid, a pair with itself or a value worldrelwire.h refuses is not taken;
   a pair named twice keeps the later entry (the engine's map ends on the later one). Returns the pairs taken. */
inline int AddFaction(Table* t, const std::string& own, const std::vector<Rel>& rels, ReadStat* st)
{
    ++st->factions;
    int took = 0;
    for (size_t i = 0; i < rels.size(); ++i)
    {
        ++st->entries;
        coopwrel::Row row; row.relation = rels[i].relation; row.trust = rels[i].trust; row.trustNeg = rels[i].trustNeg; row.flags = 0;
        if (own == rels[i].other || !coopwrel::RowOk(own, rels[i].other, row)) { ++st->refused; continue; }
        const std::string k = coopwrel::Key(own, rels[i].other);
        if (t->count(k)) ++st->duplicate; else ++st->pairs;
        (*t)[k] = row;
        ++took;
    }
    return took;
}

/* THE MERGE RULE: a pair the save holds takes the save's relation / trust / trustNeg and keeps this game's flags; a pair the
   save does not hold keeps today's value. true = the row now holds the save's values. */
inline bool FromSave(const Table& t, const std::string& a, const std::string& b, coopwrel::Row* row)
{
    Table::const_iterator it = t.find(coopwrel::Key(a, b));
    if (it == t.end()) return false;
    row->relation = it->second.relation; row->trust = it->second.trust; row->trustNeg = it->second.trustNeg;
    return true;
}
/* the save's pairs the walk did not meet (this game's maps lack them): still offered when both factions resolve here */
inline std::vector<std::string> NotMet(const Table& t, const std::set<std::string>& met)
{
    std::vector<std::string> out;
    for (Table::const_iterator it = t.begin(); it != t.end(); ++it) if (met.count(it->first) == 0) out.push_back(it->first);
    return out;
}

/* WHICH WORLD the save's standings belong to (review fold 1): armed at the import's post with the WORLD ID the import was asked
   of (the welcomed link's world - the real fact) and the world load count then. A walk in a world with another id drops them (the
   import's world never reached its walk); in the armed world the first walk after the import's own load binds them to that load,
   and any later load - of that world too - drops them, so they are used for one world load only. An unknown id now: wait. */
enum { kUseNone = 0, kUseWait = 1, kUseBind = 2, kUseYes = 3, kUseDrop = 4 };
inline int UseFor(bool armed, const std::string& armWorldId, const std::string& worldIdNow, long armLoad, long boundLoad, long loadNow)
{
    if (!armed || armWorldId.empty()) return kUseNone;
    if (worldIdNow.empty()) return kUseWait;
    if (worldIdNow != armWorldId) return kUseDrop;
    if (boundLoad < 0) return loadNow == armLoad ? kUseWait : kUseBind;
    return loadNow == boundLoad ? kUseYes : kUseDrop;
}

/* the walk line's tail */
struct WalkSave
{
    long long fromSave, written, notMet, notMetOffered, unresolved;
    WalkSave() : fromSave(0), written(0), notMet(0), notMetOffered(0), unresolved(0) {}
};
inline std::string WalkTail(bool used, long long savePairs, const WalkSave& w)
{
    if (!used) return " fromSave=0 (not the creator's import with relations)";
    char b[256];
    std::sprintf(b, " fromSave=%lld of the save's %lld pairs (written here %lld; not in this game's maps %lld: offered %lld, not a world pair here %lld)",
                 w.fromSave + w.notMetOffered, savePairs, w.written, w.notMet, w.notMetOffered, w.unresolved);
    return b;
}

}   /* namespace saverel */
