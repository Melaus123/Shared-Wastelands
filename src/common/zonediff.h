/* T-1351 (owner decision 1664, 2026-11-14): WHAT DIFFERS between a refused save of an area and the newest saved copy - for the log only.

   When a game's save of an area is refused because its copy does not match the newest saved copy, the newest copy is kept (as before)
   and one log line lists what differs between the two. This header builds that line from the two files' bytes. LOG ONLY: it decides
   nothing - no game state, record, mark, gate or counter reads its answer.

   Both copies are read by zonekeep.h's ParseContainer (the one reader of an area file's every field; no second parser here) and walked
   with zonekeep.h's helpers. Compared, per kind, each thing named by a key that does not change from one write to the next:
     buildings     every building and furniture piece of the area's building list (players', towns', nobody's), keyed by its base
                   record and position (the position key, poskey.h, as ExtractKeptPieces builds it); fields: the owner (a player's
                   mark read as that player's slot with each side's own writer slot, slotwire.h OwnerSlotOfId, so one player's piece
                   reads the same in both games' copies), build progress, delivered materials, complete / destroyed / broken, floor,
                   the host building (by its key)
     state         the piece's own state (zonekeep.h StateOf: name, layout names, pause, power, production, farm, own lock, input stocks)
     doors         a building's doors (zonekeep.h DoorsOf: state, locks, gate code, owner as a slot, hardness, broken)
     town link     whether the piece names a town (yes / no; the town's handle words are not compared)
     box contents  the item templates in a box, its packs' items with them, tallied (zonekeep.h TallyItems)
     ground items  the area's item list (1-itemlist): base record + position in tenths of a unit; fields: quantity, quality
     town note     the "built with" note's key / value pairs (townverwire.h kTvNoteId)
   NEVER compared: record ids and engine handle numbers (handle*, town*, isInsideBuilding*, persist*, ownedby*) - the game renumbers
   them at every write, so every copy would differ in them. Per kind the line gives the count on each side, the first kShow keys found on
   one side only, and the first kShow changed keys with the names of the fields that differ (keys in sorted order). The line is capped
   at kLineCap bytes and says when it was cut. It cannot tell which game made each change: no common base copy is kept.

   Pure: no Windows call, no file touched; every read bounds-checked (ParseContainer); never throws by design (a caller that can run
   out of memory wraps it). Header-only. No C++11. */
#ifndef ZONEDIFF_H_INCLUDED
#define ZONEDIFF_H_INCLUDED

#include <string>
#include <vector>
#include <map>
#include <set>
#include "zonekeep.h"     /* ParseContainer and the piece helpers (and slotwire.h, poskey.h) */
#include "zonerec.h"      /* ZoneEmptyIsRaw: an area that wrote nothing is held as the EMPTY mark */
#include "townverwire.h"  /* kTvNoteId, kTvNoteCap: the "built with" note inside an area file */

namespace zonediff {

const size_t kLineCap = 1536;               /* the whole line, its "[ZONE] ..." head included */
const int kShow = 3;                        /* keys shown per list */
const int kShowFields = 4;                  /* field names shown per changed key */
const char* const kItemList = "1-itemlist"; /* the area's ground items: instances naming item records, at their positions */
const char* const kLineHead = "[ZONE] T-1351 refused copy differs from newest record ";

typedef std::map<std::string, std::string> Fields;   /* field name -> value as text */
typedef std::map<std::string, Fields> Things;        /* key -> its fields */

/* One copy, read for comparing. */
struct Side
{
    Things pieces, state, doors, town, box, ground, note;
    int empty;        /* 1 = the EMPTY mark (the area wrote nothing) */
    int records;
    size_t bytes;
    int noKey;        /* building-list entries with no state record of their own, or no position key: not compared */
    Side() : empty(0), records(0), bytes(0), noKey(0) {}
};

/* a float as text with two decimals, laid out by hand (no CRT locale); not a plausible value = "bad" */
inline std::string Num2(float v)
{
    if (!(v > -1.0e9f && v < 1.0e9f)) return "bad";
    const double t = (double)v * 100.0;
    long long c = (long long)(t >= 0.0 ? (t + 0.5) : (t - 0.5));
    std::string s = c < 0 ? "-" : "";
    if (c < 0) c = -c;
    s += zonekeep::ZkNum(c / 100);
    const int f = (int)(c % 100);
    s += "."; s += (char)('0' + f / 10); s += (char)('0' + f % 10);
    return s;
}
inline std::string Num(long long v) { return zonekeep::ZkNum(v); }
/* text for the log line: one line, no control bytes */
inline std::string Clean(const std::string& s)
{
    std::string o(s);
    for (size_t i = 0; i < o.size(); ++i) { const unsigned char c = (unsigned char)o[i]; if (c < 0x20 || c == 0x7F) o[i] = '?'; }
    return o;
}
/* a piece's owner as one text that reads the same in both games' copies: a player's mark as that player's slot */
inline std::string OwnerText(const std::string& id, int writerSlot)
{
    const int slot = coopslot::OwnerSlotOfId(id, zonekeep::kPlayerFactionSid, writerSlot);
    if (slot >= 0) return "player " + Num(slot);
    if (slot == coopslot::kOwnerNoViewer) return "the writer's player (slot not known)";
    if (slot == coopslot::kOwnerLegacy) return "the old two-player 'other player' mark";
    return id.empty() ? std::string("nobody") : id;
}
/* a key not yet taken in `t`: a second thing with the same key gets "#2", a third "#3" ... */
inline std::string FreeKey(const Things& t, const std::string& key)
{
    if (t.find(key) == t.end()) return key;
    for (int n = 2; n < 100000; ++n) { const std::string k = key + "#" + Num(n); if (t.find(k) == t.end()) return k; }
    return key + "#many";
}

/* ONE COPY'S THINGS. false = it cannot be read (*why says where); the EMPTY mark reads as an area with nothing in it. */
inline bool ReadSide(const unsigned char* b, size_t n, int writerSlot, Side* s, std::string* why)
{
    s->bytes = n;
    if (b != 0 && zonerec::ZoneEmptyIsRaw(b, n) != 0) { s->empty = 1; return true; }
    if (b == 0 || n == 0) { *why = "the copy is empty (0 bytes)"; return false; }
    zonekeep::Container c;
    if (!zonekeep::ParseContainer(b, n, &c, why)) return false;
    s->records = (int)c.recs.size();
    std::map<std::string, int> bySid;
    for (size_t i = 0; i < c.recs.size(); ++i) if (bySid.find(c.recs[i].sid) == bySid.end()) bySid[c.recs[i].sid] = (int)i;
    zonekeep::KeepStats st;   /* the helpers count into it; not reported */

    /* the building list: as ExtractKeptPieces keys it, but every piece (not only the players') */
    std::map<std::string, int>::const_iterator bl = bySid.find(zonekeep::kBuildingList);
    if (bl != bySid.end())
    {
        const zonekeep::Rec& list = c.recs[(size_t)bl->second];
        std::map<std::string, int> stateByHandle;   /* within one copy only: the host's handle names its state record */
        for (size_t i = 0; i < c.recs.size(); ++i)
        {
            const zonekeep::Rec& r = c.recs[i];
            if (r.type != zonekeep::kTypeState || r.ints.find("handleS") == r.ints.end()) continue;
            const std::string h = zonekeep::HandleKey(zonekeep::RecInt(r, "handleS", 0), zonekeep::RecInt(r, "handleI", 0), zonekeep::RecInt(r, "handleC", 0));
            if (stateByHandle.find(h) == stateByHandle.end()) stateByHandle[h] = (int)i;
        }
        std::map<int, std::string> keyOfState;
        for (size_t i = 0; i < list.insts.size(); ++i)
        {
            const zonekeep::Inst& in = list.insts[i];
            const int si = zonekeep::InstRecOfType(c, bySid, in, zonekeep::kTypeState);
            if (si < 0 || keyOfState.find(si) != keyOfState.end()) continue;
            const zonekeep::Rec& r = c.recs[(size_t)si];
            const float y = zonekeep::RecHasFloat(r, "world Y pos") ? zonekeep::RecFloat(r, "world Y pos", 0.0f) : in.pos[1];
            char key[poskey::kKeyCap];
            const int kr = poskey::KeyOfPos(in.target.c_str(), in.pos[0], y, in.pos[2], key, poskey::kKeyCap);
            keyOfState[si] = (kr == poskey::kKeyOk || kr == poskey::kKeyHashed) ? std::string(key) : std::string();
        }
        std::set<int> takenState, takenInv;
        for (size_t i = 0; i < list.insts.size(); ++i)
        {
            const zonekeep::Inst& in = list.insts[i];
            const int si = zonekeep::InstRecOfType(c, bySid, in, zonekeep::kTypeState);
            if (si < 0)
            {   /* an entry with no state record (a resource node's): compared by its key alone, at the list's own position */
                char nk[poskey::kKeyCap];
                const int kr = poskey::KeyOfPos(in.target.c_str(), in.pos[0], in.pos[1], in.pos[2], nk, poskey::kKeyCap);
                if (kr != poskey::kKeyOk && kr != poskey::kKeyHashed) { ++s->noKey; continue; }
                s->pieces[FreeKey(s->pieces, nk)]["state record"] = "none";
                continue;
            }
            if (!takenState.insert(si).second || keyOfState[si].empty()) { ++s->noKey; continue; }
            const zonekeep::Rec& r = c.recs[(size_t)si];
            const std::string key = FreeKey(s->pieces, keyOfState[si]);
            Fields& f = s->pieces[key];
            f["owner"] = OwnerText(zonekeep::RecStr(r, "owner faction ID"), writerSlot);
            f["progress"] = Num2(zonekeep::RecFloat(r, "construction progress", 0.0f));
            f["complete"] = Num(zonekeep::RecBool(r, "is complete"));
            f["destroyed"] = Num(zonekeep::RecBool(r, "destroyed"));
            f["broken"] = Num(zonekeep::RecBool(r, "broken"));
            f["floor"] = Num(zonekeep::RecInt(r, "floornum", 0));
            {   /* how many pieces the building lists inside it ('internalbuildings-<n>TYPE'): the count, never the handle words */
                static const char kPre[] = "internalbuildings-";
                const size_t pl = sizeof(kPre) - 1;
                int inside = 0;
                for (std::map<std::string, int>::const_iterator k = r.ints.begin(); k != r.ints.end(); ++k)
                    if (k->first.size() > pl + 4 && k->first.compare(0, pl, kPre) == 0 && k->first.compare(k->first.size() - 4, 4, "TYPE") == 0) ++inside;
                if (inside != 0) f["inside count"] = Num(inside);
            }
            std::vector<zonekeep::KeptMat> mats;
            zonekeep::MatsOf(r, &mats);
            for (size_t m = 0; m < mats.size(); ++m) f["mats" + Num(mats[m].index)] = Num2(mats[m].count);
            if (zonekeep::RecBool(r, "is inside building"))
            {
                const std::string h = zonekeep::HandleKey(zonekeep::RecInt(r, "isInsideBuildingS", 0), zonekeep::RecInt(r, "isInsideBuildingI", 0), zonekeep::RecInt(r, "isInsideBuildingC", 0));
                std::map<std::string, int>::const_iterator hs = stateByHandle.find(h);
                std::map<int, std::string>::const_iterator hk = hs == stateByHandle.end() ? keyOfState.end() : keyOfState.find(hs->second);
                f["host"] = (hk != keyOfState.end() && !hk->second.empty() && hs->second != si) ? hk->second : std::string("not found");
            }
            /* its own state */
            zonekeep::KeptPiece kp;
            zonekeep::StateOf(r, &kp, &st);
            if (kp.has != 0)
            {
                Fields& g = s->state[key];
                if (kp.has & zonekeep::kHasName) g["name"] = kp.name;
                if (kp.has & zonekeep::kHasLayout) { g["interior layout"] = kp.interiorName; g["exterior layout"] = kp.exteriorName; }
                if (kp.has & zonekeep::kHasPause) g["pause"] = Num(kp.pause);
                if (kp.has & zonekeep::kHasPower) { g["gets battery"] = Num(kp.getsBattery); g["power on"] = Num(kp.powerOn); g["battery"] = Num2(kp.battery); }
                if (kp.has & zonekeep::kHasProduction) g["production"] = Num2(kp.production);
                if (kp.has & zonekeep::kHasFarm)
                { g["plant growth"] = Num2(kp.growth); g["plant death"] = Num2(kp.death); g["plant start"] = Num2(kp.start); g["plants harvested"] = Num(kp.harvested); }
                if (kp.has & zonekeep::kHasOwnLock) { g["lock lvl"] = Num(kp.lockLevel); g["lock hardness"] = Num2(kp.lockHardness); g["locked"] = Num(kp.lockLocked); }
                for (size_t k = 0; k < kp.inputs.size(); ++k) g["input" + Num(kp.inputs[k].index)] = Num2(kp.inputs[k].amount);
            }
            /* its doors */
            std::vector<zonekeep::KeptDoor> doors;
            zonekeep::DoorsOf(r, writerSlot, &doors);
            if (!doors.empty())
            {
                Fields& g = s->doors[key];
                for (size_t k = 0; k < doors.size(); ++k)
                {
                    const zonekeep::KeptDoor& d = doors[k];
                    const std::string p = "door" + Num(d.index) + " ";
                    g[p + "state"] = Num(d.state); g[p + "locked"] = Num(d.locked); g[p + "wants lock"] = Num(d.wantsLock);
                    g[p + "lock lvl"] = Num(d.lockLevel); g[p + "gatecode"] = Num(d.gateCode);
                    g[p + "owner"] = d.ownerSlot >= 0 ? "player " + Num(d.ownerSlot) : std::string("not a player");
                    g[p + "hardness"] = Num2(d.hardness); g[p + "broken"] = Num(d.broken);
                }
            }
            /* its town link: named or not - never the handle words */
            if (r.ints.find("townTYPE") != r.ints.end())
            {
                int th[5];
                s->town[key]["linked"] = zonekeep::TownHandOf(r, th) != 0 ? "yes" : "no";
            }
            /* its box: the item templates, its packs' items with them */
            const int inv = zonekeep::InstRecOfType(c, bySid, in, zonekeep::kTypeInventory);
            if (inv >= 0 && takenInv.insert(inv).second)
            {
                const zonekeep::Rec& ir = c.recs[(size_t)inv];
                std::vector<zonekeep::KeptItem> items;
                for (size_t j = 0; j < ir.insts.size(); ++j)
                {
                    std::map<std::string, int>::const_iterator fi = bySid.find(ir.insts[j].target);
                    if (fi == bySid.end() || c.recs[(size_t)fi->second].type != zonekeep::kTypeItem) continue;
                    const zonekeep::Rec& itr = c.recs[(size_t)fi->second];
                    zonekeep::KeptItem it = zonekeep::ItemOf(itr);
                    zonekeep::InnerItemsOf(c, bySid, itr, 1, &it.inner, &st);
                    items.push_back(it);
                }
                std::map<std::string, long long> tally;
                zonekeep::TallyItems(items, &tally);
                Fields& g = s->box[key];
                for (std::map<std::string, long long>::const_iterator t = tally.begin(); t != tally.end(); ++t) g[t->first.empty() ? std::string("(no template)") : t->first] = Num(t->second);
            }
        }
    }
    /* the ground items */
    std::map<std::string, int>::const_iterator il = bySid.find(kItemList);
    if (il != bySid.end())
    {
        const zonekeep::Rec& list = c.recs[(size_t)il->second];
        for (size_t i = 0; i < list.insts.size(); ++i)
        {
            const zonekeep::Inst& in = list.insts[i];
            std::map<std::string, int>::const_iterator fi = bySid.find(in.target);
            if (fi == bySid.end()) { ++s->noKey; continue; }
            const zonekeep::Rec& itr = c.recs[(size_t)fi->second];
            const std::string tmpl = zonekeep::RecStr(itr, "base data sid");
            char key[poskey::kKeyCap];
            const int kr = poskey::KeyOfPos(tmpl.c_str(), in.pos[0], in.pos[1], in.pos[2], key, poskey::kKeyCap);
            if (kr != poskey::kKeyOk && kr != poskey::kKeyHashed) { ++s->noKey; continue; }
            Fields& g = s->ground[FreeKey(s->ground, key)];
            g["quantity"] = Num(zonekeep::RecInt(itr, "quantity", 1));
            g["quality"] = Num2(zonekeep::RecFloat(itr, "quality", 0.0f));
        }
    }
    /* the "built with" note */
    std::map<std::string, int>::const_iterator tn = bySid.find(cooptv::kTvNoteId);
    if (tn != bySid.end())
    {
        const zonekeep::Rec& r = c.recs[(size_t)tn->second];
        for (int i = 0; i < cooptv::kTvNoteCap; ++i)
        {
            const std::string kk = "k" + Num(i);
            if (r.strs.find(kk) == r.strs.end()) continue;
            s->note[FreeKey(s->note, zonekeep::RecStr(r, kk.c_str()))]["value"] = zonekeep::RecStr(r, ("v" + Num(i)).c_str());
        }
    }
    if (why != 0) why->clear();
    return true;
}

/* the names of the fields that differ between two things with one key (one side lacking a field counts) */
inline void DifferingFields(const Fields& a, const Fields& b, std::vector<std::string>* out)
{
    for (Fields::const_iterator i = a.begin(); i != a.end(); ++i)
    {
        Fields::const_iterator j = b.find(i->first);
        if (j == b.end() || j->second != i->second) out->push_back(i->first);
    }
    for (Fields::const_iterator j = b.begin(); j != b.end(); ++j) if (a.find(j->first) == a.end()) out->push_back(j->first);
}
inline std::string KeyList(const std::vector<std::string>& keys)
{
    std::string o = " [";
    for (size_t i = 0; i < keys.size() && i < (size_t)kShow; ++i) o += (i ? ", " : "") + Clean(keys[i]);
    if (keys.size() > (size_t)kShow) o += ", +" + Num((long long)(keys.size() - (size_t)kShow));
    return o + "]";
}
/* ONE KIND: "<name> <a>/<b> same", or its counts, the keys on one side only and the changed keys with their differing fields.
   onlyAmong (when given): a key on one side only is listed only when it is among these (the pieces on both sides - a piece on one
   side only is already listed under buildings). */
inline std::string CompareKind(const char* name, const Things& a, const Things& b, const std::set<std::string>* onlyAmong)
{
    std::vector<std::string> onlyA, onlyB, changed;
    for (Things::const_iterator i = a.begin(); i != a.end(); ++i)
    {
        Things::const_iterator j = b.find(i->first);
        if (j == b.end()) { if (onlyAmong == 0 || onlyAmong->count(i->first) != 0) onlyA.push_back(i->first); continue; }
        std::vector<std::string> df;
        DifferingFields(i->second, j->second, &df);
        if (df.empty()) continue;
        std::string t = Clean(i->first) + "{";
        for (size_t k = 0; k < df.size() && k < (size_t)kShowFields; ++k) t += (k ? "," : "") + Clean(df[k]);
        if (df.size() > (size_t)kShowFields) t += ",+" + Num((long long)(df.size() - (size_t)kShowFields));
        changed.push_back(t + "}");
    }
    for (Things::const_iterator j = b.begin(); j != b.end(); ++j)
        if (a.find(j->first) == a.end() && (onlyAmong == 0 || onlyAmong->count(j->first) != 0)) onlyB.push_back(j->first);
    std::string o = std::string(name) + " " + Num((long long)a.size()) + "/" + Num((long long)b.size());
    if (onlyA.empty() && onlyB.empty() && changed.empty()) return o + " same";
    o += ":";
    if (!onlyA.empty()) o += " only in refused " + Num((long long)onlyA.size()) + KeyList(onlyA) + ";";
    if (!onlyB.empty()) o += " only in newest " + Num((long long)onlyB.size()) + KeyList(onlyB) + ";";
    if (!changed.empty())
    {
        o += " changed " + Num((long long)changed.size()) + " [";
        for (size_t i = 0; i < changed.size() && i < (size_t)kShow; ++i) o += (i ? ", " : "") + changed[i];   /* already cleaned */
        if (changed.size() > (size_t)kShow) o += ", +" + Num((long long)(changed.size() - (size_t)kShow));
        o += "];";
    }
    if (o[o.size() - 1] == ';') o.erase(o.size() - 1);
    return o;
}
/* the line cut to `cap` bytes with a note saying so (never inside a UTF-8 character) */
inline std::string CapLine(const std::string& line, size_t cap)
{
    if (line.size() <= cap) return line;
    const std::string tail = " ... [cut: the line was " + Num((long long)line.size()) + " bytes, capped at " + Num((long long)cap) + "]";
    size_t keep = cap > tail.size() ? cap - tail.size() : 0;
    while (keep > 0 && ((unsigned char)line[keep] & 0xC0) == 0x80) --keep;
    return line.substr(0, keep) + tail;
}
inline std::string SideText(const char* name, const Side& s, int writerSlot)
{
    std::string o = std::string(name) + " (writer " + (writerSlot >= 0 ? "slot " + Num(writerSlot) : std::string("not known")) + ", " + Num((long long)s.bytes) + " bytes, ";
    if (s.empty != 0) return o + "the EMPTY mark: the area wrote nothing)";
    return o + Num(s.records) + " records)";
}
/* THE LINE when a copy cannot be compared: which copy, and why */
inline std::string FailLine(const std::string& area, const std::string& which, const std::string& why)
{
    return CapLine("[ZONE] T-1351 refused copy of " + Clean(area) + " not compared with the newest record: " + which + " cannot be read - " + Clean(why), kLineCap);
}
/* THE LINE: the refused copy (this game's save, written by refusedWriter) against the newest record's copy (written by newestWriter).
   A writer slot of -1 = not known (a player's own mark in that copy then reads as "the writer's player (slot not known)"). */
inline std::string DiffLine(const std::string& area, const unsigned char* refused, size_t refusedN, int refusedWriter,
                            const unsigned char* newest, size_t newestN, int newestWriter, size_t cap)
{
    Side a, b;
    std::string why;
    if (!ReadSide(refused, refusedN, refusedWriter, &a, &why)) return FailLine(area, "the refused copy", why);
    if (!ReadSide(newest, newestN, newestWriter, &b, &why)) return FailLine(area, "the newest record's copy", why);
    std::set<std::string> both;
    for (Things::const_iterator i = a.pieces.begin(); i != a.pieces.end(); ++i) if (b.pieces.find(i->first) != b.pieces.end()) both.insert(i->first);
    const std::string kinds[7] = {
        CompareKind("buildings", a.pieces, b.pieces, 0), CompareKind("state", a.state, b.state, &both), CompareKind("doors", a.doors, b.doors, &both),
        CompareKind("town link", a.town, b.town, &both), CompareKind("box contents", a.box, b.box, &both), CompareKind("ground items", a.ground, b.ground, 0),
        CompareKind("town note", a.note, b.note, 0) };
    bool any = false;
    for (int k = 0; k < 7; ++k) if (kinds[k].size() < 5 || kinds[k].compare(kinds[k].size() - 5, 5, " same") != 0) any = true;
    std::string o = kLineHead + Clean(area) + ": " + SideText("refused copy", a, refusedWriter) + " vs " + SideText("newest record", b, newestWriter)
                  + "; which game made each change cannot be told (no common base copy is kept); record ids and engine handles are not compared";
    if (a.noKey != 0 || b.noKey != 0) o += "; entries with no key, not compared " + Num(a.noKey) + "/" + Num(b.noKey);
    o += any ? std::string(". ") : std::string(". No difference in the kinds compared (the copies differ only in what is not compared). ");
    for (int k = 0; k < 7; ++k) o += (k ? " | " : "") + kinds[k];
    return CapLine(o, cap);
}

}   /* namespace zonediff */

#endif
