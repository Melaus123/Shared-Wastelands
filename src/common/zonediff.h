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
#include "groundkey.h"    /* T-1391 (F1611): kGroundNearTenths and GroundNearBetter - the ground road's own item match */
#include "piecerec.h"     /* T-1391 (F1754): PieceTownFact - the town a piece record carries, which the town link is judged against */

namespace zonediff {

const size_t kLineCap = 1536;               /* the whole line, its "[ZONE] ..." head included */
const int kShow = 3;                        /* keys shown per list */
const int kShowFields = 4;                  /* field names shown per changed key */
const char* const kItemList = "1-itemlist"; /* the area's ground items: instances naming item records, at their positions */
const char* const kLineHead = "[ZONE] T-1351 refused copy differs from newest record ";

typedef std::map<std::string, std::string> Fields;   /* field name -> value as text */
typedef std::map<std::string, Fields> Things;        /* key -> its fields */

/* T-1391 (F1754): a piece's town words as its copy wrote them (townTYPE, townC, townCS, townI, townS) - never compared (they are this
   game's handle words); the own-work check names the town they point at here, to judge the town link against the piece record */
struct TownWords { int h[5]; TownWords() { for (int i = 0; i < 5; ++i) h[i] = 0; } };
/* One copy, read for comparing. */
struct Side
{
    Things pieces, state, doors, town, box, ground, note;
    std::map<std::string, TownWords> townWords;   /* T-1391 (F1754): by the town link's key; not compared */
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
                TownWords& tw = s->townWords[key];
                for (int q = 0; q < 5; ++q) tw.h[q] = th[q];
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
   side only is already listed under buildings). T-1391 (F1611): aWord / bWord name the two sides in "only in <word>" - side a is
   always this game's copy ("refused" on the refusal line, "adopted" on the adopt line), side b the record's ("newest" / "replaced"). */
inline std::string CompareKind(const char* name, const Things& a, const Things& b, const std::set<std::string>* onlyAmong,
                               const std::string& aWord = "refused", const std::string& bWord = "newest")
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
    if (!onlyA.empty()) o += " only in " + aWord + " " + Num((long long)onlyA.size()) + KeyList(onlyA) + ";";
    if (!onlyB.empty()) o += " only in " + bWord + " " + Num((long long)onlyB.size()) + KeyList(onlyB) + ";";
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
/* T-1391 (F1611): a copy's name's first word ("refused copy" -> "refused", "replaced record" -> "replaced") - the side's word in "only in" */
inline std::string ShortWord(const char* name)
{
    const std::string n(name != 0 ? name : "");
    const size_t sp = n.find(' ');
    return sp == std::string::npos ? n : n.substr(0, sp);
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
/* THE LINE when a copy cannot be compared: which copy, and why. T-1391: tag / aName / bName name the line and the two copies
   (FailLine: the refusal's "T-1351", "refused copy", "newest record"). */
inline std::string FailLineAs(const char* tag, const char* aName, const char* bName, const std::string& area, const std::string& which, const std::string& why)
{
    return CapLine("[ZONE] " + std::string(tag) + " " + aName + " of " + Clean(area) + " not compared with the " + bName + ": " + which + " cannot be read - " + Clean(why), kLineCap);
}
inline std::string FailLine(const std::string& area, const std::string& which, const std::string& why)
{
    return FailLineAs("T-1351", "refused copy", "newest record", area, which, why);
}
/* T-1391 (F1577 / F1578): THE COMPARISON with its outcome. Copy a (this game's, written by aWriter) against copy b (a record's,
   written by bWriter), the line named by tag / aName / bName. *outcome (may be 0): zonerec kZcSame (no difference in the kinds
   compared), kZcDiffers or kZcUnread (a copy could not be read: the failure line). *differing (may be 0): the names of the kinds that
   differ, in the line's order, ", " between. The line is the same as before for the refusal (DiffLine). */
inline std::string CompareLine(const char* tag, const char* aName, const char* bName, const std::string& area, const unsigned char* refused, size_t refusedN, int refusedWriter,
                               const unsigned char* newest, size_t newestN, int newestWriter, size_t cap, int* outcome, std::string* differing)
{
    Side a, b;
    std::string why;
    if (outcome != 0) *outcome = zonerec::kZcUnread;
    if (differing != 0) differing->clear();
    if (!ReadSide(refused, refusedN, refusedWriter, &a, &why)) return FailLineAs(tag, aName, bName, area, "the " + std::string(aName), why);
    if (!ReadSide(newest, newestN, newestWriter, &b, &why)) return FailLineAs(tag, aName, bName, area, "the " + std::string(bName) + "'s copy", why);
    std::set<std::string> both;
    for (Things::const_iterator i = a.pieces.begin(); i != a.pieces.end(); ++i) if (b.pieces.find(i->first) != b.pieces.end()) both.insert(i->first);
    const std::string aw = ShortWord(aName), bw = ShortWord(bName);   /* T-1391 (F1611): the adopt line said "only in refused / newest" */
    const std::string kinds[7] = {
        CompareKind("buildings", a.pieces, b.pieces, 0, aw, bw), CompareKind("state", a.state, b.state, &both, aw, bw), CompareKind("doors", a.doors, b.doors, &both, aw, bw),
        CompareKind("town link", a.town, b.town, &both, aw, bw), CompareKind("box contents", a.box, b.box, &both, aw, bw), CompareKind("ground items", a.ground, b.ground, 0, aw, bw),
        CompareKind("town note", a.note, b.note, 0, aw, bw) };
    static const char* const names[7] = { "buildings", "state", "doors", "town link", "box contents", "ground items", "town note" };
    bool any = false;
    for (int k = 0; k < 7; ++k) if (kinds[k].size() < 5 || kinds[k].compare(kinds[k].size() - 5, 5, " same") != 0)
    {
        if (differing != 0) { if (any) *differing += ", "; *differing += names[k]; }
        any = true;
    }
    if (outcome != 0) *outcome = any ? zonerec::kZcDiffers : zonerec::kZcSame;
    std::string o = "[ZONE] " + std::string(tag) + " " + aName + " differs from " + bName + " " + Clean(area) + ": " + SideText(aName, a, refusedWriter) + " vs " + SideText(bName, b, newestWriter)
                  + "; which game made each change cannot be told (no common base copy is kept); record ids and engine handles are not compared";
    if (a.noKey != 0 || b.noKey != 0) o += "; entries with no key, not compared " + Num(a.noKey) + "/" + Num(b.noKey);
    o += any ? std::string(". ") : std::string(". No difference in the kinds compared (the copies differ only in what is not compared). ");
    for (int k = 0; k < 7; ++k) o += (k ? " | " : "") + kinds[k];
    return CapLine(o, cap);
}
/* THE LINE: the refused copy (this game's save, written by refusedWriter) against the newest record's copy (written by newestWriter).
   A writer slot of -1 = not known (a player's own mark in that copy then reads as "the writer's player (slot not known)"). */
inline std::string DiffLine(const std::string& area, const unsigned char* refused, size_t refusedN, int refusedWriter,
                            const unsigned char* newest, size_t newestN, int newestWriter, size_t cap)
{
    return CompareLine("T-1351", "refused copy", "newest record", area, refused, refusedN, refusedWriter, newest, newestN, newestWriter, cap, 0, 0);
}

/* ---- T-1391 (F1611, 2026-11-20): THIS GAME'S OWN WORK ----
   T2689: the FAULT label compared a refused save with the NEWEST record in both directions, so the record's own extra things (another
   game's items: "0/7 only in newest") and engine noise read as this game's loss. The label now decides from what THIS game's copy
   (mine: the save refused, or the copy an adopt keeps) has that the copy this game LOADED did not - its own work since its loaded copy
   (store.cpp ZoneLoadedCopy*: the bytes Kenshi was given at the area's read, or this game's own save written since) - in that direction
   only:
     added    a thing (per kind, keyed as above) in mine and not in the loaded copy
     changed  a thing in both whose fields differ - except a building's 'inside count', never counted. The engine writes a building's
              inside list from its live set, each listed handle written only when its object is found at that moment
              (build/decomp_553d60.txt:403-433), and reads the list back from the file at the load (decomp_554bc0.txt:214-240): the count
              is what THIS game had loaded inside at its save, so two games write different counts for the same untouched building in
              either direction (T2689 log-A:5756 lower; T2691 log-A:6394-6395 / log-B:3146 higher - town buildings no player touched). A
              player's own work inside a building is the inside pieces themselves (their keys), counted as buildings above.
     gone     in the loaded copy and not in mine: said, never counted (another game's or the engine's; never this game's loss)
   Ground items are paired by the ground road's own match (groundname.h NameLookup): the same key first, then an item of the same record
   within coopground::kGroundNearTenths measured flat, at any height (the better quantity / quality rank, then the smaller height
   difference, then the smaller flat distance: coopground::GroundNearBetter) - so an item the engine settled a tenth of a unit lower
   (T2689 log-A:4464-4468, '... given to the item listed as ... (by key)') is the same item. A paired item whose quantity or quality
   differs is changed. Pure, as the rest of this header. */
const int kKinds = 7;
const int kKindBuildings = 0, kKindTown = 3, kKindBox = 4, kKindGround = 5, kKindNote = 6;
inline const char* KindName(int k)
{
    static const char* const n[kKinds] = { "buildings", "state", "doors", "town link", "box contents", "ground items", "town note" };
    return (k >= 0 && k < kKinds) ? n[k] : "?";
}
struct OwnWork
{
    int added[kKinds], changed[kKinds], gone[kKinds];
    std::vector<std::string> show[kKinds];   /* "+key" for an added thing, "key{fields}" for a changed one, in key order */
    std::vector<std::string> keys[kKinds];   /* T-1391 (F1653): the same things' keys as they are (the own-work check finds their records by them) */
    OwnWork() { for (int k = 0; k < kKinds; ++k) { added[k] = 0; changed[k] = 0; gone[k] = 0; } }
    int Any() const { for (int k = 0; k < kKinds; ++k) if (added[k] != 0 || changed[k] != 0) return 1; return 0; }
};
/* a whole number written by Num (optional '-', digits); anything else reads as 0 */
inline long long IntOf(const std::string& s)
{
    long long v = 0; size_t i = 0; const bool neg = !s.empty() && s[0] == '-';
    if (neg) i = 1;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) v = v * 10 + (s[i] - '0');
    return neg ? -v : v;
}
/* the fields of mine that are this game's work against the loaded copy's (insideRule: a building's 'inside count' is never counted - T2691) */
inline void OwnFields(const Fields& mine, const Fields& loaded, int insideRule, std::vector<std::string>* out)
{
    for (Fields::const_iterator i = mine.begin(); i != mine.end(); ++i)
    {
        Fields::const_iterator j = loaded.find(i->first);
        if (insideRule != 0 && i->first == "inside count") continue;   /* T2691 (F1619): either direction is the writing game's loaded set, not its work */
        if (j == loaded.end() || j->second != i->second) out->push_back(i->first);
    }
    for (Fields::const_iterator j = loaded.begin(); j != loaded.end(); ++j)
        if (mine.find(j->first) == mine.end() && !(insideRule != 0 && j->first == "inside count")) out->push_back(j->first);
}
inline std::string ChangedText(const std::string& key, const std::vector<std::string>& df)
{
    std::string t = Clean(key) + "{";
    for (size_t k = 0; k < df.size() && k < (size_t)kShowFields; ++k) t += (k ? "," : "") + Clean(df[k]);
    if (df.size() > (size_t)kShowFields) t += ",+" + Num((long long)(df.size() - (size_t)kShowFields));
    return t + "}";
}
/* one kind keyed exactly. onlyAmong: as CompareKind's (a sub-kind's thing on one side only counts only for a piece on both sides) */
inline void OwnKind(int k, const Things& mine, const Things& loaded, const std::set<std::string>* onlyAmong, OwnWork* w)
{
    for (Things::const_iterator i = mine.begin(); i != mine.end(); ++i)
    {
        Things::const_iterator j = loaded.find(i->first);
        if (j == loaded.end() && k == kKindBox && i->second.empty()) continue;   /* T-1391 (F1754 sibling): an EMPTY box new since the load holds no own work (ReadSide makes a box entry for every inventory record, items or not) */
        if (j == loaded.end()) { if (onlyAmong == 0 || onlyAmong->count(i->first) != 0) { ++w->added[k]; w->show[k].push_back("+" + Clean(i->first)); w->keys[k].push_back(i->first); } continue; }
        std::vector<std::string> df;
        OwnFields(i->second, j->second, k == kKindBuildings ? 1 : 0, &df);
        if (!df.empty()) { ++w->changed[k]; w->show[k].push_back(ChangedText(i->first, df)); w->keys[k].push_back(i->first); }
    }
    for (Things::const_iterator j = loaded.begin(); j != loaded.end(); ++j)
        if (mine.find(j->first) == mine.end() && (onlyAmong == 0 || onlyAmong->count(j->first) != 0)) ++w->gone[k];
}
/* a key without FreeKey's "#<n>" (a hashed sid field's leading '#' is left: it stands before the spot) */
inline std::string BareKey(const std::string& k)
{
    const size_t h = k.rfind('#'), at = k.rfind('@');
    return (h != std::string::npos && at != std::string::npos && h > at) ? k.substr(0, h) : k;
}
struct GroundSpot { int ok; char sid[poskey::kSidRoom]; long x10, z10, y10; };
inline void GroundSpotOf(const std::string& key, GroundSpot* g)
{
    int sx = 0, sy = 0;
    g->x10 = g->z10 = g->y10 = 0;
    g->ok = poskey::ParseKeySpotH(BareKey(key).c_str(), g->sid, poskey::kSidRoom, &sx, &sy, &g->x10, &g->z10, &g->y10);
}
/* coopground::GroundBagRank on the fields as text: 0 = quantity and quality equal, 1 = the quantity, 2 = the quality, 3 = neither */
inline int GroundRankOf(const Fields& a, const Fields& b)
{
    Fields::const_iterator qa = a.find("quantity"), qb = b.find("quantity"), la = a.find("quality"), lb = b.find("quality");
    const int sq = (qa != a.end() && qb != b.end() && qa->second == qb->second) ? 1 : 0, sl = (la != a.end() && lb != b.end() && la->second == lb->second) ? 1 : 0;
    return (sq && sl) ? 0 : sq ? 1 : sl ? 2 : 3;
}
/* 1 = the loaded copy's item (spot l) may stand for mine's (spot m) by the ground road's near match: the same record, within
   kGroundNearTenths flat. *d2 / *dy: the flat distance squared and the height difference, in tenths */
inline int GroundNearPair(const GroundSpot& m, const GroundSpot& l, long long* d2, long long* dy)
{
    if (m.ok == 0 || l.ok == 0 || poskey::SidFieldsSame(m.sid, l.sid) == 0) return 0;
    const long long dx = (long long)m.x10 - l.x10, dz = (long long)m.z10 - l.z10;
    *d2 = dx * dx + dz * dz;
    *dy = (long long)m.y10 - l.y10; if (*dy < 0) *dy = -*dy;
    return *d2 <= (long long)coopground::kGroundNearTenths * coopground::kGroundNearTenths ? 1 : 0;
}
/* the ground items: each of mine paired with one of the loaded copy's (the same key, then near), one to one */
inline void OwnGround(const Things& mine, const Things& loaded, OwnWork* w)
{
    std::vector<Things::const_iterator> mi, li;
    std::vector<std::string> mb, lb;
    for (Things::const_iterator i = mine.begin(); i != mine.end(); ++i) { mi.push_back(i); mb.push_back(BareKey(i->first)); }
    for (Things::const_iterator j = loaded.begin(); j != loaded.end(); ++j) { li.push_back(j); lb.push_back(BareKey(j->first)); }
    std::vector<int> pairOf(mi.size(), -1);
    std::vector<unsigned char> taken(li.size(), 0);
    for (size_t a = 0; a < mi.size(); ++a)
        for (size_t b = 0; b < li.size(); ++b) if (taken[b] == 0 && lb[b] == mb[a]) { pairOf[a] = (int)b; taken[b] = 1; break; }
    std::vector<GroundSpot> ms(mi.size()), ls(li.size());
    for (size_t a = 0; a < mi.size(); ++a) GroundSpotOf(mb[a], &ms[a]);
    for (size_t b = 0; b < li.size(); ++b) GroundSpotOf(lb[b], &ls[b]);
    for (size_t a = 0; a < mi.size(); ++a)
    {
        if (pairOf[a] >= 0) continue;
        int best = -1, bestRank = 0; long long bestD2 = 0, bestDy = 0;
        for (size_t b = 0; b < li.size(); ++b)
        {
            long long d2 = 0, dy = 0;
            if (taken[b] != 0 || GroundNearPair(ms[a], ls[b], &d2, &dy) == 0) continue;
            const int rk = GroundRankOf(mi[a]->second, li[b]->second);
            if (best < 0 || coopground::GroundNearBetter(rk, dy, d2, bestRank, bestDy, bestD2) != 0) { best = (int)b; bestRank = rk; bestDy = dy; bestD2 = d2; }
        }
        if (best >= 0) { pairOf[a] = best; taken[(size_t)best] = 1; }
    }
    for (size_t a = 0; a < mi.size(); ++a)
    {
        if (pairOf[a] < 0) { ++w->added[kKindGround]; w->show[kKindGround].push_back("+" + Clean(mi[a]->first)); w->keys[kKindGround].push_back(mi[a]->first); continue; }
        std::vector<std::string> df;
        OwnFields(mi[a]->second, li[(size_t)pairOf[a]]->second, 0, &df);
        if (!df.empty()) { ++w->changed[kKindGround]; w->show[kKindGround].push_back(ChangedText(mi[a]->first, df)); w->keys[kKindGround].push_back(mi[a]->first); }
    }
    for (size_t b = 0; b < li.size(); ++b) if (taken[b] == 0) ++w->gone[kKindGround];
}
/* THE RULE: this game's own work in `mine` since `loaded`. zonerec kZcDiffers = some (the kinds in *w), kZcSame = none */
inline int OwnWorkOf(const Side& mine, const Side& loaded, OwnWork* w)
{
    std::set<std::string> both;
    for (Things::const_iterator i = mine.pieces.begin(); i != mine.pieces.end(); ++i) if (loaded.pieces.find(i->first) != loaded.pieces.end()) both.insert(i->first);
    OwnKind(0, mine.pieces, loaded.pieces, 0, w);
    OwnKind(1, mine.state, loaded.state, &both, w);
    OwnKind(2, mine.doors, loaded.doors, &both, w);
    OwnKind(3, mine.town, loaded.town, &both, w);
    OwnKind(4, mine.box, loaded.box, &both, w);
    OwnGround(mine.ground, loaded.ground, w);
    OwnKind(6, mine.note, loaded.note, 0, w);
    return w->Any() != 0 ? zonerec::kZcDiffers : zonerec::kZcSame;
}
/* T-1391 (F1653, THE OWN-WORK CHECK before any drop of an own area save): each kind of own work is CARRIED when its own item record
   reached the world (held here, written at or after the copy the save was made from) or waits on its own road (a piece record marked
   for writing, a box / door / ground entry in the outage journal). The road per kind: buildings and their state - the piece record
   (build.cpp PcrW: a piece placed while this game is not its writer is held and written once it is); doors - the door record or the
   piece's; box contents - the box record; ground items - the area's ground record; the town link - the piece record's town part, and
   the town note - the placed town's town version record (F1754, both below: each is carried only when its record names the same town /
   version as the save). */
/* review of e3082c6cc (S2): COARSER WHERE SAID. Per record where the record names the thing: a piece and its state (piece record of its
   key), a door (door record of its key, or its piece's), box contents (the box record of its key). Coarser: ground items - one ground
   record per area, so any own ground item is carried only by that area's record newer than the base.
   T-1391 fold (F1754, 2026-11-20; replaces review F1664 M1's 'the town link has no road - never carried', which made every piece placed
   with the link down a FAULT): THE TOWN LINK IS CARRIED BY THE PIECE RECORD'S TOWN PART, JUDGED AGAINST THE SAVE. Since T-1427 fold 1
   the piece record names its town by a key every game resolves (piecerec.h: bit 5 an outpost id; bit 8 a world town's stringID or
   'Nowhere'); the area file's link is this game's handle words (ReadSide: Side::townWords), named here as a record names a town (the
   game's town lists: store.cpp ZoneOwnWorkCheck). Carried only when the piece record (of the piece's key; a piece inside a building:
   its host's - its town is its host's, build.cpp PcrTownFill) is held at or after the base AND carries the SAME town as the save, or
   waits on its road (as the building item does). A record with no town part ('NOT KNOWN', piecerec.h), another town, a save whose town
   cannot be named here, or no record: NOT carried, and the FAULT line says which (TownLinkWhy). F1664 M1's worry - a lost link logged as
   carried because the record's town was never compared - is met: it is compared.
   THE TOWN NOTE ("built with", townverwire.h kTvNoteId: per placed town the version its buildings in the area are built with) is carried
   by that placed town's TOWN_VER record (the same key; one record per placed town) naming the same version - this game's own write of it
   still waiting for the world server, else the record held here. A TOWN_VER record has no stamp (its revision rule keeps one truth), so
   there is no base test. Review fold: no record and no own write waiting, with the note naming the town's own version ('=') - carried:
   no record already means the normal town (townverwire.h TvPublishRev: no record + the own version = nothing to publish). Another
   version, or a version other than '=' with no record: NOT carried (OwnNoteCarried). */
enum { kRoadNone = 0, kRoadPiece = 1, kRoadDoor = 2, kRoadBox = 3, kRoadGround = 4, kRoadTown = 5, kRoadTownVer = 6 };
inline int OwnKindRoad(int kind)
{
    return (kind == 0 || kind == 1) ? (int)kRoadPiece : kind == 2 ? (int)kRoadDoor : kind == kKindTown ? (int)kRoadTown : kind == kKindBox ? (int)kRoadBox
         : kind == kKindGround ? (int)kRoadGround : kind == kKindNote ? (int)kRoadTownVer : (int)kRoadNone;
}
inline const char* RoadName(int road)
{
    return road == kRoadPiece ? "piece record" : road == kRoadDoor ? "door record" : road == kRoadBox ? "box record" : road == kRoadGround ? "ground record"
         : road == kRoadTown ? "piece record's town" : road == kRoadTownVer ? "town version record" : "no record";
}
/* recordHeld / recordAt: a record of the thing is held here, with its stamp; baseAt: the stamp of the copy the save was made from (0 =
   not known: any record held counts); waits: an entry for it waits on its own road */
inline int OwnCarried(int road, int recordHeld, long long recordAt, long long baseAt, int waits)
{
    if (road == kRoadNone || road == kRoadTown || road == kRoadTownVer) return 0;   /* F1754: the town link and note are judged by OwnTownCarried / OwnNoteCarried, which compare the town */
    if (waits != 0) return 1;
    return (recordHeld != 0 && baseAt > 0 && recordAt >= baseAt) ? 1 : 0;   /* S2: a base stamp not known never lets a held record carry */
}
/* T-1391 fold (F1754): the piece record's town against the save's. recRead: 1 = the held record's town part was read here (rec: its
   town, kPtfNone = the record carries no town part); saveRead: 1 = the save's town was named here (save: kPtfNone = the save names no
   town; an outpost with id 0 = a player's town whose outpost id is not known here) */
enum { kTmSame = 0, kTmNoPart = 1, kTmOther = 2, kTmSaveUnknown = 3, kTmRecUnread = 4, kTmOwed = 5, kTmOwedSame = 6 };
inline int TownLinkMatch(int recRead, const piecerec::PieceTownFact& rec, int saveRead, const piecerec::PieceTownFact& save)
{
    if (recRead == 0) return kTmRecUnread;
    if (rec.kind == piecerec::kPtfNone) return kTmNoPart;
    if (saveRead == 0 || (save.kind == piecerec::kPtfOutpost && save.id == 0u)) return kTmSaveUnknown;
    return piecerec::PieceTownFactSame(rec, save) != 0 ? (int)kTmSame : (int)kTmOther;
}
/* T-1391 fix attempt 2 (F1774 / F1775): the town an OWED write will carry against the save's - owed 1: a player's town whose outpost
   id is not known here yet (the record will carry it once the id is learned - not carried now: kTmOwed); else rec as TownLinkMatch */
inline int TownLinkMatchOwed(int owed, const piecerec::PieceTownFact& rec, int saveRead, const piecerec::PieceTownFact& save)
{
    if (owed != 0) return kTmOwed;
    return TownLinkMatch(1, rec, saveRead, save);
}
/* review fold (MAJOR 1): an owed part (a player's town whose outpost id is not known here) is compared by the TOWN WORDS it was captured
   with against the save's town words (both this game's handle words for its own towns): the same words = kTmOwedSame (carried - the
   owed write names that town once its id is learned), other words or none read = kTmOwed (not carried) */
inline int TownLinkMatchOwedWords(int owed, const int* capWords, const int* saveWords, const piecerec::PieceTownFact& rec, int saveRead, const piecerec::PieceTownFact& save)
{
    if (owed == 0) return TownLinkMatch(1, rec, saveRead, save);
    if (capWords == 0 || saveWords == 0) return kTmOwed;
    for (int q = 0; q < 5; ++q) if (capWords[q] != saveWords[q]) return kTmOwed;
    return kTmOwedSame;
}
/* the town link: carried when the record carries the save's town (match kTmSame) - an owed write by what it WILL carry (match from
   TownLinkMatchOwed; T-1391 fix attempt 2, F1774: no longer carried merely because a write waits), else a record held at or after the
   base (OwnCarried's rule) */
inline int OwnTownCarried(int recordHeld, long long recordAt, long long baseAt, int waits, int match)
{
    if (waits != 0) return (match == kTmSame || match == kTmOwedSame) ? 1 : 0;
    if (OwnCarried(kRoadPiece, recordHeld, recordAt, baseAt, 0) == 0) return 0;
    return match == kTmSame ? 1 : 0;
}
inline std::string TownFactText(const piecerec::PieceTownFact& f)
{
    if (f.kind == piecerec::kPtfOutpost) return f.id != 0u ? "outpost " + Num((long long)f.id) : std::string("a player's town whose outpost id is not known here");
    if (f.kind == piecerec::kPtfWorld) return "world town " + Clean(f.sid);
    if (f.kind == piecerec::kPtfNowhere) return "'Nowhere'";
    return "no town";
}
/* the FAULT line's (and the carried list's) words for a town link: what the record carries against what the save has */
inline std::string TownLinkWhy(int match, const piecerec::PieceTownFact& rec, int saveRead, const piecerec::PieceTownFact& save, const std::string& saveWhy)
{
    const std::string sv = saveRead != 0 ? TownFactText(save) : "a town not named here (" + Clean(saveWhy) + ")";
    if (match == kTmSame) return "carries town " + TownFactText(rec) + ", as the save";
    if (match == kTmNoPart) return "piece record carries no town, the save has " + sv;
    if (match == kTmOther) return "carries town " + TownFactText(rec) + ", the save has " + sv;
    if (match == kTmSaveUnknown) return "carries town " + TownFactText(rec) + ", the save's town cannot be told: " + sv;
    if (match == kTmOwed) return "its town part is OWED (a player's town whose outpost id is not known here yet), the save has " + sv;
    if (match == kTmOwedSame) return "its town part is owed (the outpost id not yet known here) - the same town as the save by its town words";
    return "its town part was not read here (no decoded copy of the record)";
}
/* ---- T-1391 fix attempt 2 (F1774 / F1775): A DOOR IS JUDGED BY WHAT ITS OWN RECORD CONTAINS ----
   The area file keeps a building's doors inside the building's record ('door<i> state', 'door<i> locked', 'door<i> wants lock' - ReadSide
   files them under the BUILDING's key); each door's record is named by the DOOR's own key (doorrec.h MakeDoorRecId), found by door index
   (doors.cpp DoorsKeysOfPiece). A door is carried when its record - this game's own write waiting in the outage journal (the newest), else
   the record held here - contains the save's door: its state (settled: opening counts as open, closing as closed, as the record stores
   it) and its lock word (locked, wants lock). A door at its starting state is carried by the record seeded at its placement - no
   'defaults' rule. No door key known here, or no record: NOT carried. */
enum { kDmSame = 0, kDmNoRecord = 1, kDmOther = 2, kDmKeyUnknown = 3 };
inline int DoorSettled(int s) { return s == 2 ? 1 : (s == 3 ? 0 : s); }
/* the door indexes a doors item names (its 'door<i> state' fields) */
inline std::vector<int> DoorIndexesOf(const Fields& f)
{
    std::vector<int> v;
    for (Fields::const_iterator i = f.begin(); i != f.end(); ++i)
    {
        const std::string& n = i->first;
        if (n.size() < 11 || n.compare(0, 4, "door") != 0 || n.compare(n.size() - 6, 6, " state") != 0) continue;
        const std::string d = n.substr(4, n.size() - 10);
        if (d.empty() || d.size() > 3) continue;
        bool ok = true;
        for (size_t c = 0; c < d.size(); ++c) if (d[c] < '0' || d[c] > '9') ok = false;
        if (ok) v.push_back((int)IntOf(d));
    }
    return v;
}
inline int DoorContentMatch(int keyKnown, int recHave, int recState, int recLockWord, const Fields& save, int index)
{
    if (keyKnown == 0) return kDmKeyUnknown;
    if (recHave == 0) return kDmNoRecord;
    const std::string p = "door" + Num((long long)index) + " ";
    Fields::const_iterator st = save.find(p + "state"), lk = save.find(p + "locked"), wl = save.find(p + "wants lock");
    if (st == save.end()) return kDmOther;
    const int locked = lk != save.end() ? (IntOf(lk->second) != 0 ? 1 : 0) : 0;
    const int wants = wl != save.end() ? (IntOf(wl->second) != 0 ? 1 : 0) : 0;
    const int sameState = DoorSettled((int)IntOf(st->second)) == DoorSettled(recState) ? 1 : 0;
    const int sameLock = (locked == (recLockWord & 1) && wants == ((recLockWord >> 1) & 1)) ? 1 : 0;
    return (sameState != 0 && sameLock != 0) ? (int)kDmSame : (int)kDmOther;
}
inline std::string DoorMatchWhy(int m, const char* src)
{
    if (m == kDmSame) return std::string("(") + src + " record contains the save's door)";
    if (m == kDmNoRecord) return "(no door record held, none queued)";
    if (m == kDmOther) return std::string("(") + src + " record holds another state or lock)";
    return "(its door key is not known here)";
}
/* a door change journaled with the link down (doors.cpp DoorStateToBytes: "d1<TAB>key<TAB>state<TAB>lockWord"): 1 = read */
inline int DoorQueuedContent(const std::vector<char>& pay, int* state, int* lockWord)
{
    const std::string s(pay.begin(), pay.end());
    const size_t t1 = s.find('\t'), t2 = t1 == std::string::npos ? t1 : s.find('\t', t1 + 1), t3 = t2 == std::string::npos ? t2 : s.find('\t', t2 + 1);
    if (s.compare(0, 3, "d1\t") != 0 || t3 == std::string::npos || t3 + 2 != s.size() || t3 != t2 + 2) return 0;
    const char a = s[t2 + 1], b = s[t3 + 1];
    if ((a != '0' && a != '1') || b < '0' || b > '3') return 0;
    *state = a - '0'; *lockWord = b - '0';
    return 1;
}
/* the town note: noteValue = the note's "=<version>" field; waits / waitVer: this game's own TOWN_VER write for the placed town, not yet
   seen held by the world server (the world's version once it lands); held / heldVer: the record held here. 1 = carried. No record and
   none waiting: carried only for the town's own version ('=': no record means the normal town, townverwire.h TvPublishRev) */
inline int OwnNoteCarried(const std::string& noteValue, int held, const std::string& heldVer, int waits, const std::string& waitVer)
{
    std::string ver;
    if (cooptv::TvNoteVerParse(noteValue, &ver) == 0) return 0;
    if (waits != 0) return waitVer == ver ? 1 : 0;
    if (held == 0) return ver.empty() ? 1 : 0;
    return heldVer == ver ? 1 : 0;
}
/* S2: THE CHECK'S OWN-WORK RULE - as OwnWorkOf, but a NEW piece's state, doors, town link and box contents count as own work too (OwnWorkOf
   counts a sub-kind's thing only for a piece on both sides, which suits a label but would let a new piece's box contents go uncounted) */
inline int OwnWorkOfAll(const Side& mine, const Side& loaded, OwnWork* w)
{
    std::set<std::string> any;
    for (Things::const_iterator i = mine.pieces.begin(); i != mine.pieces.end(); ++i) any.insert(i->first);
    for (Things::const_iterator i = loaded.pieces.begin(); i != loaded.pieces.end(); ++i) any.insert(i->first);
    OwnKind(0, mine.pieces, loaded.pieces, 0, w);
    OwnKind(1, mine.state, loaded.state, &any, w);
    OwnKind(2, mine.doors, loaded.doors, &any, w);
    OwnKind(3, mine.town, loaded.town, &any, w);
    OwnKind(4, mine.box, loaded.box, &any, w);
    OwnGround(mine.ground, loaded.ground, w);
    OwnKind(6, mine.note, loaded.note, 0, w);
    return w->Any() != 0 ? zonerec::kZcDiffers : zonerec::kZcSame;
}
/* the label: kOcNothing - no own work since the base (nothing lost); kOcCarried - every own thing is carried by its record; kOcFault -
   own work no record carries (a FAULT line, the bytes set aside); kOcUnknown - the base or the copy cannot be read (also a FAULT with
   the bytes set aside: not knowing is never read as nothing lost) */
enum { kOcNothing = 0, kOcCarried = 1, kOcFault = 2, kOcUnknown = 3 };
inline int OwnCheckLabel(int compared, int anyOwn, int anyLost)
{
    if (compared == 0) return kOcUnknown;
    if (anyOwn == 0) return kOcNothing;
    return anyLost != 0 ? (int)kOcFault : (int)kOcCarried;
}
inline const char* OwnCheckName(int label)
{
    return label == kOcNothing ? "nothing of this game's own since its base" : label == kOcCarried ? "CARRIED - every own thing by its own record"
         : label == kOcFault ? "FAULT - own work no record carries" : "NOT KNOWN - the base copy or the save could not be read";
}
/* THE OWN-WORK LINE: mine (this game's copy, named mineName, written by mineWriter) against this game's loaded copy (loadedFrom says
   where it came from; written by loadedWriter). *outcome: kZcDiffers (own work: *kinds names the kinds), kZcSame (none), kZcUnread (a
   copy could not be read). Per kind with work: added / changed counts and the first kShow keys; then what is gone since the load. */
inline std::string OwnWorkLine(const char* tag, const char* mineName, const std::string& area, const std::string& loadedFrom,
                               const unsigned char* mine, size_t mineN, int mineWriter, const unsigned char* loaded, size_t loadedN, int loadedWriter,
                               size_t cap, int* outcome, std::string* kinds)
{
    Side a, b;
    std::string why;
    if (outcome != 0) *outcome = zonerec::kZcUnread;
    if (kinds != 0) kinds->clear();
    const std::string head = "[ZONE] " + std::string(tag) + " own work in the " + mineName + " of " + Clean(area);
    if (!ReadSide(mine, mineN, mineWriter, &a, &why)) return CapLine(head + " NOT KNOWN: the " + mineName + " cannot be read - " + Clean(why), cap);
    if (!ReadSide(loaded, loadedN, loadedWriter, &b, &why)) return CapLine(head + " NOT KNOWN: this game's loaded copy (" + Clean(loadedFrom) + ") cannot be read - " + Clean(why), cap);
    OwnWork w;
    const int oc = OwnWorkOf(a, b, &w);
    if (outcome != 0) *outcome = oc;
    std::string o = head + " since this game's loaded copy (" + Clean(loadedFrom) + "; " + SideText("loaded copy", b, loadedWriter) + " vs " + SideText(mineName, a, mineWriter) + "): ";
    if (oc != zonerec::kZcDiffers) o += "none - nothing added or changed in the kinds compared";
    else
    {
        bool first = true;
        for (int k = 0; k < kKinds; ++k)
        {
            if (w.added[k] == 0 && w.changed[k] == 0) continue;
            if (kinds != 0) { if (!kinds->empty()) *kinds += ", "; *kinds += KindName(k); }
            o += (first ? "" : " | ") + std::string(KindName(k)) + " added " + Num((long long)w.added[k]) + " changed " + Num((long long)w.changed[k]) + " [";
            for (size_t i = 0; i < w.show[k].size() && i < (size_t)kShow; ++i) o += (i ? ", " : "") + w.show[k][i];
            if (w.show[k].size() > (size_t)kShow) o += ", +" + Num((long long)(w.show[k].size() - (size_t)kShow));
            o += "]";
            first = false;
        }
    }
    std::string gone;
    for (int k = 0; k < kKinds; ++k) if (w.gone[k] != 0) gone += (gone.empty() ? "" : ", ") + std::string(KindName(k)) + " " + Num((long long)w.gone[k]);
    o += "; gone since the load, never counted as this game's work: " + (gone.empty() ? std::string("none") : gone);
    return CapLine(o, cap);
}

}   /* namespace zonediff */

#endif
