/* dupsquad.h - THE DUPLICATE REPORT over one squad record's engine bytes (<id>.platoon).

   READ ONLY. It lists a squad's members, groups the ones that are the same person, and says how many a trim
   would remove and whether anything else inside the same file points at those copies. It changes nothing;
   the world server prints what it finds once at start-up.

   THE FILE, as the engine writes it (every value little-endian; a string is an int length then that many bytes):
     header: int version, int last id, int record count, then that many records, and nothing after them -
     except in a zone file (zone.<x>.<y>.platoon), which ends with an int count and that many ints.
     record: int instance count, int type, int numeric id, string name, string record id ("<n>--INGAME"),
             int flag, then seven keyed lists, each an int count then its entries:
               bools (key, 1 byte), floats (key, float), ints (key, int), vec3s (key, 3 floats),
               vec4s (key, 4 floats), strings (key, string), files (key, string),
             then the references: int category count, each a string category and an int count of
               (string record id, int, int, int),
             then the instances: int count, each a string instance id, string target, 3 floats position,
               4 floats rotation, and an int count of strings.
   The record kinds this report reads:
     30  the squad: one instance per member; the instance's strings are the member's six record ids.
     36  the character: strings "name"; ints "portrait_serial" and the handle "handleTYPE/C/CS/S/I" (the
         character's own engine handle); bool "is leader". Further int groups ending in TYPE/C/CS/S/I name
         OTHER things by handle: "slaver", "carrying", "in what", "isindoors", "TI town".
     66  the appearance: strings "head" and "hair style"; vec3 "Skin Tone"; bool "sex female"; the "race"
         reference.
     41/42 the inventory and its items (reached through instance targets); items carry "ownedby" and
         "insideBuilding" handle groups. 67 (AI) may carry "pjobT0".
   Record ids are renumbered at every write, so they never identify a person across files; inside ONE file
   they are what the squad's member entries and the inventories use to name records.

   THE SAME PERSON (the strict key): name, portrait_serial, head, hair style, skin tone (to 3 decimals),
   race and sex all equal. portrait_serial alone is not enough - different people can share one.

   A LINK to a member is, inside the same file: the member's "is leader" flag (the squad's leader marker);
   any handle group of any record, other than a record's own "handle", that equals the member's handle; or
   any string, reference or instance target of any record that names one of the member's records. A
   member's own records (its six, and whatever their instances reach) never count as linking to it.
   Links from OTHER records - other squads, towns, the player's faction - cannot be seen from one file.

   Pure: no Windows calls, no file access. VS2010 C++03. Results are written into the caller's struct. */
#ifndef DUPSQUAD_H
#define DUPSQUAD_H

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace dupsquad
{

enum { kUnreadable = 0, kNotSquad = 1, kSquad = 2 };
enum { kRecSquad = 30, kRecCharacter = 36, kRecAppearance = 66 };
const int kMaxString = 100000;   /* longer than any string in the 46,327 store copies read so far */

struct Report
{
    int state;          /* kUnreadable, kNotSquad (readable; no squad record, or one naming no character) or kSquad */
    int members;        /* squad entries whose character record is in the file */
    int groups;         /* groups of two or more members with the same strict key */
    int wouldRemove;    /* members a trim would remove: each group's size minus one, summed */
    int cleanK;         /* k when EVERY member is in a group of exactly k (k > 1); 0 otherwise */
    int largest;        /* the largest group */
    int linkedCopies;   /* members inside duplicate groups that something else in the file links to */
    int linkedRemoved;  /* of those, how many a trim could not keep: per group, linked copies minus one */
    int leaderLinks;    /* linked copies by kind - one copy can count under more than one */
    int handleLinks;
    int idLinks;
};
inline void Clear(Report* r) { std::memset(r, 0, sizeof(Report)); r->state = kUnreadable; }

struct Handle { int t, c, cs, s, i; };
inline bool SameHandle(const Handle& a, const Handle& b) { return a.t == b.t && a.c == b.c && a.cs == b.cs && a.s == b.s && a.i == b.i; }

struct Rec
{
    int type;
    std::string sid;
    std::string name;               /* 36 */
    int portrait;                   /* 36 */
    bool leader;                    /* 36 */
    bool hasHandle;                 /* 36: the record's own handle */
    Handle own;
    std::string head, hair, race;   /* 66 */
    long skin[3];                   /* 66: Skin Tone x 1000, rounded */
    int sexFemale;                  /* 66: -1 = absent */
    std::vector<Handle> handles;    /* handle groups naming something else */
    std::vector<std::string> named; /* string values, file values, reference ids */
    std::vector<std::string> inst;  /* instance targets and instance strings (not for the squad record) */
    std::vector<std::vector<std::string> > entries;   /* 30: each member's record ids */
    Rec() : type(0), portrait(0), leader(false), hasHandle(false), sexFemale(-1) { own.t = own.c = own.cs = own.s = own.i = 0; skin[0] = skin[1] = skin[2] = 0; }
};

struct Reader
{
    const unsigned char* b; size_t n, p; bool ok;
    Reader(const unsigned char* buf, size_t len) : b(buf), n(len), p(0), ok(buf != 0 || len == 0) {}
    bool Need(size_t k) { if (!ok || k > n - p) { ok = false; return false; } return true; }
    int I() { if (!Need(4)) return 0; int v; std::memcpy(&v, b + p, 4); p += 4; return v; }
    float F() { if (!Need(4)) return 0.0f; float v; std::memcpy(&v, b + p, 4); p += 4; return v; }
    unsigned char B() { if (!Need(1)) return 0; return b[p++]; }
    void Skip(size_t k) { if (Need(k)) p += k; }
    void S(std::string* out)
    {
        const int len = I();
        if (!ok) return;
        if (len < 0 || len > kMaxString || !Need((size_t)len)) { ok = false; return; }
        out->assign((const char*)(b + p), (size_t)len); p += (size_t)len;
    }
    /* A count, refused when even entries of the smallest size could not fit in what is left. */
    int Count(size_t minEach)
    {
        const int c = I();
        if (!ok) return 0;
        if (c < 0 || (size_t)c > (n - p) / minEach) { ok = false; return 0; }
        return c;
    }
};

inline long Round1000(float v) { return (long)std::floor((double)v * 1000.0 + 0.5); }

inline bool EndsWith(const std::string& s, const char* suffix)
{
    const size_t k = std::strlen(suffix);
    return s.size() >= k && s.compare(s.size() - k, k, suffix) == 0;
}

inline void ReadRec(Reader& r, Rec* rec)
{
    std::string key, val;
    r.I();                       /* the instance count, read again below with the instances */
    rec->type = r.I();
    r.I();                       /* numeric id */
    r.S(&val);                   /* name */
    r.S(&rec->sid);
    r.I();                       /* flag */
    int c = r.Count(5);
    for (int k = 0; k < c && r.ok; ++k)
    {
        r.S(&key); const unsigned char v = r.B();
        if (rec->type == kRecCharacter && key == "is leader") rec->leader = v != 0;
        if (rec->type == kRecAppearance && key == "sex female") rec->sexFemale = v;
    }
    c = r.Count(8);
    for (int k = 0; k < c && r.ok; ++k) { r.S(&key); r.F(); }
    std::map<std::string, int> ints;
    c = r.Count(8);
    for (int k = 0; k < c && r.ok; ++k) { r.S(&key); const int v = r.I(); ints[key] = v; }
    c = r.Count(16);
    for (int k = 0; k < c && r.ok; ++k)
    {
        r.S(&key); const float x = r.F(), y = r.F(), z = r.F();
        if (rec->type == kRecAppearance && key == "Skin Tone") { rec->skin[0] = Round1000(x); rec->skin[1] = Round1000(y); rec->skin[2] = Round1000(z); }
    }
    c = r.Count(20);
    for (int k = 0; k < c && r.ok; ++k) { r.S(&key); r.Skip(16); }
    c = r.Count(8);
    for (int k = 0; k < c && r.ok; ++k)
    {
        r.S(&key); r.S(&val);
        if (rec->type == kRecCharacter && key == "name") rec->name = val;
        if (rec->type == kRecAppearance && key == "head") rec->head = val;
        if (rec->type == kRecAppearance && key == "hair style") rec->hair = val;
        if (!val.empty()) rec->named.push_back(val);
    }
    c = r.Count(8);
    for (int k = 0; k < c && r.ok; ++k) { r.S(&key); r.S(&val); if (!val.empty()) rec->named.push_back(val); }
    c = r.Count(8);
    for (int k = 0; k < c && r.ok; ++k)
    {
        std::string cat; r.S(&cat);
        const int m = r.Count(16);
        for (int q = 0; q < m && r.ok; ++q)
        {
            r.S(&val); r.I(); r.I(); r.I();
            if (rec->type == kRecAppearance && cat == "race") { rec->race += val; rec->race += '\n'; }
            if (!val.empty()) rec->named.push_back(val);
        }
    }
    c = r.Count(40);
    for (int k = 0; k < c && r.ok; ++k)
    {
        std::string iid, tgt; r.S(&iid); r.S(&tgt); r.Skip(28);
        const int m = r.Count(4);
        std::vector<std::string> ids;
        for (int q = 0; q < m && r.ok; ++q) { r.S(&val); ids.push_back(val); }
        if (rec->type == kRecSquad) rec->entries.push_back(ids);
        else
        {
            if (!tgt.empty()) rec->inst.push_back(tgt);
            for (size_t q = 0; q < ids.size(); ++q) rec->inst.push_back(ids[q]);
        }
    }
    if (!r.ok) return;
    /* Handle groups: <prefix>TYPE with <prefix>C, CS, S and I beside it. */
    for (std::map<std::string, int>::const_iterator it = ints.begin(); it != ints.end(); ++it)
    {
        if (!EndsWith(it->first, "TYPE") || it->first.size() == 4) continue;
        const std::string pre = it->first.substr(0, it->first.size() - 4);
        std::map<std::string, int>::const_iterator ic = ints.find(pre + "C"), ics = ints.find(pre + "CS"), is = ints.find(pre + "S"), ii = ints.find(pre + "I");
        if (ic == ints.end() || ics == ints.end() || is == ints.end() || ii == ints.end()) continue;
        Handle h; h.t = it->second; h.c = ic->second; h.cs = ics->second; h.s = is->second; h.i = ii->second;
        if (pre == "handle")
        {
            if (rec->type == kRecCharacter) { rec->own = h; rec->hasHandle = true; }
            continue;   /* a record's own handle names itself */
        }
        if (h.s == 0 && h.i == 0) continue;   /* an empty handle names nothing */
        rec->handles.push_back(h);
    }
    std::map<std::string, int>::const_iterator ip = ints.find("portrait_serial");
    if (rec->type == kRecCharacter && ip != ints.end()) rec->portrait = ip->second;
}

/* Reads the whole file; false when any part of it does not parse or bytes are left over. */
inline bool ParseAll(const unsigned char* b, size_t n, std::vector<Rec>* out)
{
    out->clear();
    Reader r(b, n);
    r.I(); r.I();   /* version, last id */
    const int count = r.Count(60);
    if (!r.ok) return false;
    out->resize((size_t)count);
    for (int k = 0; k < count && r.ok; ++k) ReadRec(r, &(*out)[(size_t)k]);
    if (r.ok && r.p < n)
    {
        /* the zone file's closing list: an int count and exactly that many ints, ending the file */
        const int tail = r.Count(4);
        r.Skip((size_t)tail * 4);
    }
    return r.ok && r.p == n;
}

inline void AppendPart(std::string* key, const std::string& part)
{
    char len[16]; std::sprintf(len, "%u:", (unsigned)part.size());
    *key += len; *key += part; *key += '|';
}

inline void Analyse(const unsigned char* b, size_t n, Report* out)
{
    Clear(out);
    std::vector<Rec> recs;
    if (!ParseAll(b, n, &recs)) return;
    out->state = kNotSquad;
    int squad = -1;
    for (size_t k = 0; k < recs.size(); ++k) if (recs[k].type == kRecSquad) { squad = (int)k; break; }
    if (squad < 0) return;
    out->state = kSquad;

    std::map<std::string, int> bySid;
    for (size_t k = 0; k < recs.size(); ++k) if (bySid.find(recs[k].sid) == bySid.end()) bySid[recs[k].sid] = (int)k;

    /* The members, and which records belong to which member: its own record ids, then whatever their
       instances reach (the inventory's items, a bag's contents), first claim wins. */
    std::vector<int> owner(recs.size(), -1);
    std::vector<int> charOf, appOf;
    const std::vector<std::vector<std::string> >& entries = recs[(size_t)squad].entries;
    for (size_t e = 0; e < entries.size(); ++e)
    {
        int ch = -1, ap = -1;
        for (size_t q = 0; q < entries[e].size(); ++q)
        {
            std::map<std::string, int>::const_iterator f = bySid.find(entries[e][q]);
            if (f == bySid.end()) continue;
            if (recs[(size_t)f->second].type == kRecCharacter && ch < 0) ch = f->second;
            if (recs[(size_t)f->second].type == kRecAppearance && ap < 0) ap = f->second;
        }
        if (ch < 0) continue;
        const int m = (int)charOf.size();
        charOf.push_back(ch); appOf.push_back(ap);
        std::vector<int> work;
        for (size_t q = 0; q < entries[e].size(); ++q)
        {
            std::map<std::string, int>::const_iterator f = bySid.find(entries[e][q]);
            if (f != bySid.end() && f->second != squad && owner[(size_t)f->second] < 0) { owner[(size_t)f->second] = m; work.push_back(f->second); }
        }
        while (!work.empty())
        {
            const int at = work.back(); work.pop_back();
            for (size_t q = 0; q < recs[(size_t)at].inst.size(); ++q)
            {
                std::map<std::string, int>::const_iterator f = bySid.find(recs[(size_t)at].inst[q]);
                if (f != bySid.end() && f->second != squad && owner[(size_t)f->second] < 0) { owner[(size_t)f->second] = m; work.push_back(f->second); }
            }
        }
    }
    const int members = (int)charOf.size();
    out->members = members;
    if (members == 0) { std::memset(out, 0, sizeof(Report)); out->state = kNotSquad; return; }   /* a zone's list of buildings, not people */

    /* Links, by kind. */
    std::vector<char> lLeader((size_t)members, 0), lHandle((size_t)members, 0), lId((size_t)members, 0);
    for (int m = 0; m < members; ++m) if (recs[(size_t)charOf[(size_t)m]].leader) lLeader[(size_t)m] = 1;
    for (size_t k = 0; k < recs.size(); ++k)
    {
        const Rec& rk = recs[k];   /* the squad record's member entries are kept apart (entries), so its other fields count */
        for (size_t h = 0; h < rk.handles.size(); ++h)
            for (int m = 0; m < members; ++m)
            {
                const Rec& ch = recs[(size_t)charOf[(size_t)m]];
                if (ch.hasHandle && (ch.own.s != 0 || ch.own.i != 0) && owner[k] != m && SameHandle(rk.handles[h], ch.own)) lHandle[(size_t)m] = 1;
            }
        for (int pass = 0; pass < 2; ++pass)
        {
            const std::vector<std::string>& names = pass == 0 ? rk.named : rk.inst;
            for (size_t q = 0; q < names.size(); ++q)
            {
                std::map<std::string, int>::const_iterator f = bySid.find(names[q]);
                if (f == bySid.end()) continue;
                const int m = owner[(size_t)f->second];
                if (m >= 0 && m != owner[k]) lId[(size_t)m] = 1;
            }
        }
    }

    /* The strict key, and the groups. */
    std::map<std::string, std::vector<int> > groups;
    for (int m = 0; m < members; ++m)
    {
        const Rec& ch = recs[(size_t)charOf[(size_t)m]];
        const Rec* ap = appOf[(size_t)m] >= 0 ? &recs[(size_t)appOf[(size_t)m]] : 0;
        char num[96];
        std::string key;
        AppendPart(&key, ch.name);
        std::sprintf(num, "%d", ch.portrait); AppendPart(&key, num);
        AppendPart(&key, ap ? ap->head : std::string());
        AppendPart(&key, ap ? ap->hair : std::string());
        std::sprintf(num, "%ld,%ld,%ld", ap ? ap->skin[0] : 0L, ap ? ap->skin[1] : 0L, ap ? ap->skin[2] : 0L); AppendPart(&key, num);
        AppendPart(&key, ap ? ap->race : std::string());
        std::sprintf(num, "%d", ap ? ap->sexFemale : -1); AppendPart(&key, num);
        groups[key].push_back(m);
    }
    int sameK = -1; bool allSame = true;
    for (std::map<std::string, std::vector<int> >::const_iterator g = groups.begin(); g != groups.end(); ++g)
    {
        const int k = (int)g->second.size();
        if (sameK < 0) sameK = k; else if (k != sameK) allSame = false;
        if (k > out->largest) out->largest = k;
        if (k < 2) continue;
        ++out->groups;
        out->wouldRemove += k - 1;
        int linked = 0;
        for (size_t q = 0; q < g->second.size(); ++q)
        {
            const size_t m = (size_t)g->second[q];
            if (lLeader[m]) ++out->leaderLinks;
            if (lHandle[m]) ++out->handleLinks;
            if (lId[m]) ++out->idLinks;
            if (lLeader[m] || lHandle[m] || lId[m]) ++linked;
        }
        out->linkedCopies += linked;
        if (linked > 1) out->linkedRemoved += linked - 1;
    }
    if (allSame && sameK > 1) out->cleanK = sameK;
}

/* The start-up totals. */
struct Totals
{
    long long squads, withDup, clean, partial, wouldRemove, linkedRemoved, linkedCopies, unreadable, notSquad;
    std::map<int, long long> cleanByK;
    Totals() : squads(0), withDup(0), clean(0), partial(0), wouldRemove(0), linkedRemoved(0), linkedCopies(0), unreadable(0), notSquad(0) {}
};
inline void Add(Totals* t, const Report& r)
{
    if (r.state == kUnreadable) { ++t->unreadable; return; }
    if (r.state == kNotSquad) { ++t->notSquad; return; }
    ++t->squads;
    if (r.groups == 0) return;
    ++t->withDup;
    if (r.cleanK > 1) { ++t->clean; ++t->cleanByK[r.cleanK]; } else ++t->partial;
    t->wouldRemove += r.wouldRemove;
    t->linkedRemoved += r.linkedRemoved;
    t->linkedCopies += r.linkedCopies;
}

/* One line per record that has duplicates (or could not be read); false when the record needs no line. */
inline bool RecordLine(const std::string& id, const Report& r, std::string* out)
{
    char b[512];
    if (r.state == kUnreadable) { *out = "[DUP] " + id + ": unreadable - not reported"; return true; }
    if (r.state != kSquad || r.groups == 0) return false;
    char shape[32];
    if (r.cleanK > 1) std::sprintf(shape, "clean %dx", r.cleanK); else std::sprintf(shape, "partial");
    std::sprintf(b, ": members=%d groups=%d would remove=%d shape=%s referred=%d (copies with a link: %d - leader %d, handle %d, record id %d; largest group %d)",
                 r.members, r.groups, r.wouldRemove, shape, r.linkedRemoved, r.linkedCopies, r.leaderLinks, r.handleLinks, r.idLinks, r.largest);
    *out = "[DUP] " + id + b;
    return true;
}

inline void SummaryLine(const Totals& t, std::string* out)
{
    char b[640];
    std::string kinds;
    for (std::map<int, long long>::const_iterator it = t.cleanByK.begin(); it != t.cleanByK.end(); ++it)
    {
        char one[48]; std::sprintf(one, "%s%dx: %lld", kinds.empty() ? " (" : ", ", it->first, it->second); kinds += one;
    }
    if (!kinds.empty()) kinds += ")";
    std::sprintf(b, "[DUP] report: %lld squad records read, %lld have duplicate members (%lld clean%s, %lld partial), %lld members a trim would remove,"
                    " %lld of them referred to inside their own record, %lld unreadable",
                 t.squads, t.withDup, t.clean, kinds.c_str(), t.partial, t.wouldRemove, t.linkedRemoved, t.unreadable);
    *out = b;
    char tail[320];
    std::sprintf(tail, " - read only, nothing changed; %lld copies carry a link (a trim keeps one linked copy per group); links from other records"
                       " (other squads, towns, the player's faction) cannot be checked from one file; %lld non-squad records skipped",
                 t.linkedCopies, t.notSquad);
    *out += tail;
}

}   /* namespace dupsquad */

#endif
