/* restore1a (.modding/investigations/restore1-older-save-guard-design.md sections 1 and 6; rules "One true world - records win"
   and "Restores and resets"; owner rulings 2026-09-27 at the review-restore1a fold): THE WORLD GUARD'S PURE DECISIONS, the
   key-file fields, the notebook's world.gen book and the WORLD_SAVED wire. The SAME header the plugin (store.cpp), the notebook
   (store_main.cpp) and the offline suite compile. No C++11.

   THE SAVE NUMBER IS PER PROFILE (owner ruling 1). Every FINISHED save by the OPERATOR's game (owner.txt) gets the next number
   of ITS OWN PROFILE (worldGen), taken in REQUEST order, plus the highest notebook sequence number that game has seen (nbSeq),
   written into the save's multiplayer-world.key as fields 5-7 {worldGen, nbSeq, repairEpoch} and reported with WORLD_SAVED, which
   names the profile. The notebook keeps {profile -> highest worldGen} and ONE world-wide seqHigh (never above its own index
   high-water) in world.gen, never steps back, and its WELCOME carries the connecting profile's row. At load, operator only:
     - save worldGen <  its own profile's worldGen                   -> refuse "older world save"      (kRefuseOlderWorld)
     - save nbSeq > seqHigh, or save epoch > epoch  (world-wide)     -> refuse "notebook is older"      (kRefuseNotebookBehind)
     - the save's PROFILE is numbered (its own number > 0; fold 2)
       and the save has no field 5                                   -> refuse                          (kRefuseUnnumbered)
       or no key file at all                                         -> refuse                          (kRefuseKeyless)
     - not numbered and no field 5 / no key file                     -> allow, the feature is new       (kAllowNoGen, fail open)
     - a joiner                                                      -> never refused by this arm       (kAllowJoiner)
   An allowed save whose number is ABOVE the notebook's (a lost WORLD_SAVED) is adopted, so a number is never handed out twice.
   repairEpoch is the notebook's epoch: 0 in every world no repair ever touched (the repair itself was removed in T-716). */
#ifndef KENSHI_COOP_RESTOREGUARD_H
#define KENSHI_COOP_RESTOREGUARD_H

#include <algorithm>   /* restore1b1: std::sort in PruneSelect */
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace restoreguard {

/* What a save's key file says (fields 5-7). hasGen false = no field 5, or fields 5-7 that are not whole digits. */
struct SaveStamp
{
    bool hasGen; unsigned int gen; unsigned long long seq; unsigned int epoch;
    SaveStamp() : hasGen(false), gen(0), seq(0ULL), epoch(0) {}
};
/* What this game knows of the notebook, for ITS OWN profile. known false = no 52 WELCOME tail seen. numbered = THIS profile's
   worldGen is above 0 (fold 2: per profile). */
struct WorldState
{
    bool known; unsigned int gen; bool numbered; unsigned long long seqHigh; unsigned int epoch;
    WorldState() : known(false), gen(0), numbered(false), seqHigh(0ULL), epoch(0) {}
};

enum Verdict { kAllow = 0, kAllowJoiner = 1, kAllowNoGen = 2, kAllowNoTail = 3, kRefuseOlderWorld = 4, kRefuseNotebookBehind = 5,
               kRefuseUnnumbered = 6, kRefuseKeyless = 7 };

inline bool IsRefusal(int v) { return v == kRefuseOlderWorld || v == kRefuseNotebookBehind || v == kRefuseUnnumbered || v == kRefuseKeyless; }
inline const char* VerdictName(int v)
{
    switch (v)
    {
    case kAllow: return "allow";
    case kAllowJoiner: return "allowJoiner";
    case kAllowNoGen: return "allowNoGen";
    case kAllowNoTail: return "allowNoTail";
    case kRefuseOlderWorld: return "refuseOlderWorld";
    case kRefuseNotebookBehind: return "refuseNotebookBehind";
    case kRefuseUnnumbered: return "refuseUnnumbered";
    case kRefuseKeyless: return "refuseKeyless";
    }
    return "?";
}

/* A save WITH a key file. isOperator = this game is the notebook's operator (the WELCOME/AUTH authority flag); w = the row of
   the save's own profile (the other-profile refusal runs first, so that is this game's profile). */
inline int Decide(bool isOperator, const SaveStamp& s, const WorldState& w)
{
    if (!isOperator) return kAllowJoiner;
    if (!w.known) return kAllowNoTail;
    if (!s.hasGen) return w.gen > 0 ? kRefuseUnnumbered : kAllowNoGen;   /* fold 2: numbered = THIS profile's number > 0 */
    if (s.gen < w.gen) return kRefuseOlderWorld;
    if (s.seq > w.seqHigh || s.epoch > w.epoch) return kRefuseNotebookBehind;
    return kAllow;
}
/* A save with NO key file at all (owner ruling 2): refused for the operator once the world is numbered. */
inline int DecideKeyless(bool isOperator, const WorldState& w)
{
    if (!isOperator) return kAllowJoiner;
    if (!w.known) return kAllowNoTail;
    return w.gen > 0 ? kRefuseKeyless : kAllowNoGen;   /* fold 2: a profile whose own number is 0 is not numbered yet */
}
/* review-restore1a a: an ALLOWED save whose number is above the notebook's (the WORLD_SAVED was lost) is adopted - the pending
   stamp is raised to it and the caller re-sends WORLD_SAVED(save gen, save seq). True = adopt. */
inline bool AdoptHigher(const SaveStamp& s, const WorldState& w, unsigned int* pendGen, unsigned long long* pendSeq)
{
    if (!s.hasGen || s.gen <= w.gen) return false;
    if (s.gen > *pendGen) { *pendGen = s.gen; *pendSeq = s.seq; }
    return true;
}
/* review-restore1a c: THE NUMBER IS TAKEN AT REQUEST TIME, in request order, above everything this game knows of. */
inline unsigned int ReserveGen(unsigned int* reserved, unsigned int notebookGen, unsigned int pendGen)
{
    unsigned int base = *reserved;
    if (notebookGen > base) base = notebookGen;
    if (pendGen > base) base = pendGen;
    *reserved = base + 1u;
    return *reserved;
}
/* review-restore1a d: a notebook restarted from an older copy mid-session - its seqHigh is below what this game has already
   seen, or this profile's number is below the highest this game already knows. */
inline bool NotebookBehindLive(const WorldState& w, unsigned long long seqSeen, unsigned int ourGen)
{
    return w.seqHigh < seqSeen || w.gen < ourGen;   /* fold 2: ourGen = the number the NOTEBOOK confirmed for this profile, never our own saves */
}
/* fold 2: the pending stamp is re-sent whenever it is above the notebook's number - operator only, never into an older notebook. */
inline bool ResendDecide(bool isOperator, bool notebookBehind, unsigned int pendGen, unsigned int notebookGen)
{
    return isOperator && !notebookBehind && pendGen > notebookGen;
}
/* fold 2: THE PENDING NUMBER SURVIVES A QUIT - the highest field 5 among this profile's key files is adopted (with its nbSeq).
   Returns that highest number (0 = none found). */
inline unsigned int ScanAdopt(const std::vector<SaveStamp>& found, unsigned int* pendGen, unsigned long long* pendSeq)
{
    unsigned int top = 0; unsigned long long topSeq = 0ULL;
    for (size_t i = 0; i < found.size(); ++i) if (found[i].hasGen && found[i].gen > top) { top = found[i].gen; topSeq = found[i].seq; }
    if (top > *pendGen) { *pendGen = top; *pendSeq = topSeq; }
    return top;
}

/* Whole decimal digits only, 1..20 of them, no overflow. */
inline bool ParseU64(const std::string& t, unsigned long long* out)
{
    if (t.empty() || t.size() > 20) return false;
    unsigned long long v = 0ULL;
    for (std::string::size_type i = 0; i < t.size(); ++i)
    {
        if (t[i] < '0' || t[i] > '9') return false;
        const unsigned long long d = (unsigned long long)(t[i] - '0');
        if (v > (18446744073709551615ULL - d) / 10ULL) return false;
        v = v * 10ULL + d;
    }
    *out = v; return true;
}
inline bool ParseU32(const std::string& t, unsigned int* out)
{
    unsigned long long v = 0ULL;
    if (!ParseU64(t, &v) || v > 0xFFFFFFFFULL) return false;
    *out = (unsigned int)v; return true;
}
inline std::string U64Text(unsigned long long v)
{
    char b[24]; int n = 0;
    do { b[n++] = (char)('0' + (int)(v % 10ULL)); v /= 10ULL; } while (v != 0ULL && n < 23);
    std::string s;
    while (n > 0) s += b[--n];
    return s;
}
inline std::vector<std::string> SplitTabs(const std::string& line)
{
    std::vector<std::string> f; std::string::size_type at = 0;
    for (;;)
    {
        const std::string::size_type t = line.find('\t', at);
        f.push_back(line.substr(at, t == std::string::npos ? std::string::npos : t - at));
        if (t == std::string::npos) break;
        at = t + 1;
    }
    return f;
}
/* A profile id as the key file's field 4 writes it ("<person>.<n>"): 1..64 printable characters, no space or tab. */
inline bool ProfileIdOk(const std::string& p)
{
    if (p.empty() || p.size() > 64) return false;
    for (std::string::size_type i = 0; i < p.size(); ++i) if (p[i] <= ' ' || p[i] > '~') return false;
    return true;
}

/* KEY FILE FIELDS 5-7: `kcworld2<TAB>key<TAB>slot<TAB>profile<TAB>worldGen<TAB>nbSeq<TAB>repairEpoch`. f = the line's tab fields
   (field 1 = f[0]). Older builds read fields 1-4 only. True = a stamp was read (gen >= 1). */
inline bool StampFromFields(const std::vector<std::string>& f, SaveStamp* out)
{
    *out = SaveStamp();
    if (f.size() < 7 || f[0] != "kcworld2") return false;
    SaveStamp s;
    if (!ParseU32(f[4], &s.gen) || s.gen == 0 || !ParseU64(f[5], &s.seq) || !ParseU32(f[6], &s.epoch)) return false;
    s.hasGen = true; *out = s; return true;
}
/* KEY FILE FIELD 8 (T-490): the world id. A joiner's file carries fields 5-7 empty before it (`...<profile><TAB><TAB><TAB><TAB><id>`),
   which StampFromFields reads as no stamp. Builds that read fields 1-7 only are unaffected. */
inline std::string KeyWorldIdOf(const std::vector<std::string>& f) { return (f.size() >= 8 && f[0] == "kcworld2") ? f[7] : std::string(); }
/* The text after field 4: the stamp (StampTail, or "" for none) and then the world id, when there is one. */
inline std::string KeyTailWithId(const std::string& stampTail, const std::string& worldId)
{
    if (worldId.empty()) return stampTail;
    return (stampTail.empty() ? std::string("\t\t\t") : stampTail) + "\t" + worldId;
}
/* The text appended after field 4 (starts with a TAB). */
inline std::string StampTail(unsigned int gen, unsigned long long seq, unsigned int epoch)
{
    return "\t" + U64Text(gen) + "\t" + U64Text(seq) + "\t" + U64Text(epoch);
}

/* ---- THE NOTEBOOK'S BOOK (world.gen): {profile -> highest worldGen}, one world-wide seqHigh, the epoch ---- */
struct WorldBook
{
    std::map<std::string, unsigned int> gen; unsigned long long seqHigh; unsigned int epoch;
    WorldBook() : seqHigh(0ULL), epoch(0) {}
};
inline unsigned int GenOf(const WorldBook& b, const std::string& profile)
{
    std::map<std::string, unsigned int>::const_iterator it = b.gen.find(profile);
    return it == b.gen.end() ? 0u : it->second;
}
inline unsigned int NumberedCount(const WorldBook& b)
{
    unsigned int n = 0;
    for (std::map<std::string, unsigned int>::const_iterator it = b.gen.begin(); it != b.gen.end(); ++it) if (it->second > 0) ++n;
    return n;
}
inline unsigned int MaxGen(const WorldBook& b)
{
    unsigned int m = 0;
    for (std::map<std::string, unsigned int>::const_iterator it = b.gen.begin(); it != b.gen.end(); ++it) if (it->second > m) m = it->second;
    return m;
}
/* THE NOTEBOOK KEEPS THE MAXIMUM AND NEVER STEPS BACK. seqHigh is raised only to the notebook's OWN index high-water - a game's
   claimed seq is never trusted (review-restore1a d). Returns a bit mask: 1 = the profile's gen raised, 2 = seqHigh raised. */
inline int MergeBook(WorldBook* b, const std::string& profile, unsigned int gen, unsigned long long indexHigh)
{
    int ch = 0;
    if (!profile.empty() && gen > GenOf(*b, profile)) { b->gen[profile] = gen; ch |= 1; }
    if (indexHigh > b->seqHigh) { b->seqHigh = indexHigh; ch |= 2; }
    return ch;
}
/* world.gen: `wg2<TAB>seqHigh<TAB>epoch`, then one `p<TAB>profile<TAB>worldGen` line per profile. */
inline std::string BookFormat(const WorldBook& b)
{
    std::string s = "wg2\t" + U64Text(b.seqHigh) + "\t" + U64Text(b.epoch) + "\n";
    for (std::map<std::string, unsigned int>::const_iterator it = b.gen.begin(); it != b.gen.end(); ++it)
        s += "p\t" + it->first + "\t" + U64Text(it->second) + "\n";
    return s;
}
inline bool BookParse(const std::string& text, WorldBook* out)
{
    WorldBook b; bool head = false;
    std::string::size_type at = 0;
    while (at < text.size())
    {
        std::string::size_type nl = text.find('\n', at);
        std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = (nl == std::string::npos) ? text.size() : nl + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        const std::vector<std::string> f = SplitTabs(line);
        if (!head)
        {
            if (f.size() != 3 || f[0] != "wg2" || !ParseU64(f[1], &b.seqHigh) || !ParseU32(f[2], &b.epoch)) return false;
            head = true; continue;
        }
        unsigned int g = 0;
        if (f.size() != 3 || f[0] != "p" || !ProfileIdOk(f[1]) || !ParseU32(f[2], &g)) return false;
        if (g > GenOf(b, f[1])) b.gen[f[1]] = g;
    }
    if (!head) return false;
    *out = b; return true;
}

/* ---- WORLD_SAVED (store message 48, store protocol 52) and the WELCOME tail: ONE shape ----
   {u32 gen, u32 seq lo, u32 seq hi, u32 epoch, u32 numbered, u32 profileLen + profile bytes}.
   Up: the operator's stamp (its profile, the gen, the nbSeq it wrote; numbered 0). Down (the answer) and the WELCOME tail: the
   named profile's worldGen, the world-wide seqHigh and epoch, and how many profiles are numbered. */
struct WorldMsg
{
    unsigned int gen; unsigned long long seq; unsigned int epoch, numbered; std::string profile;
    WorldMsg() : gen(0), seq(0ULL), epoch(0), numbered(0) {}
};
const size_t kWireMin = 24;
inline void PutU32(std::vector<char>* out, unsigned int v) { char b[4]; std::memcpy(b, &v, 4); out->insert(out->end(), b, b + 4); }
inline void EncodeWorldMsg(std::vector<char>* out, const WorldMsg& m)   /* APPENDS */
{
    const size_t n = m.profile.size() > 64 ? 64 : m.profile.size();
    PutU32(out, m.gen); PutU32(out, (unsigned int)(m.seq & 0xFFFFFFFFULL)); PutU32(out, (unsigned int)(m.seq >> 32));
    PutU32(out, m.epoch); PutU32(out, m.numbered); PutU32(out, (unsigned int)n);
    out->insert(out->end(), m.profile.begin(), m.profile.begin() + n);
}
inline bool DecodeWorldMsg(const char* p, size_t n, WorldMsg* out)
{
    if (p == 0 || n < kWireMin) return false;
    unsigned int g = 0, lo = 0, hi = 0, e = 0, num = 0, len = 0;
    std::memcpy(&g, p, 4); std::memcpy(&lo, p + 4, 4); std::memcpy(&hi, p + 8, 4); std::memcpy(&e, p + 12, 4);
    std::memcpy(&num, p + 16, 4); std::memcpy(&len, p + 20, 4);
    if (len > 64 || n < kWireMin + (size_t)len) return false;
    WorldMsg m; m.gen = g; m.seq = ((unsigned long long)hi << 32) | lo; m.epoch = e; m.numbered = num;
    m.profile.assign(p + kWireMin, p + kWireMin + len);
    *out = m; return true;
}
/* The notebook's word (an answer or a WELCOME tail) as this game's view of its own profile's row. */
inline WorldState ViewOf(const WorldMsg& m)
{
    WorldState w; w.known = true; w.gen = m.gen; w.numbered = m.gen > 0; w.seqHigh = m.seq; w.epoch = m.epoch;   /* fold 2: per profile */
    return w;
}
/* The notebook's answer / WELCOME row for one profile. */
inline WorldMsg RowOf(const WorldBook& b, const std::string& profile)
{
    WorldMsg m; m.gen = GenOf(b, profile); m.seq = b.seqHigh; m.epoch = b.epoch; m.numbered = NumberedCount(b); m.profile = profile;
    return m;
}

/* ==== restore1b1 (design s2 Q1 and s4 first bullet; owner rulings 2026-09-27): part 1 of restore1b ====
   OWN_HIGH (store message 49, store protocol 53 - the fold's bump: a 52 build would misread it): each game reports its OWN records' highest COMMITTED
   sequence number (own.index's ownSeq high-water) for the records store it has chosen. The notebook keeps the MAXIMUM per
   (profile, store) in own_high.txt, never steps back, answers every report with its number and pushes every row of the
   connecting profile right after the WELCOME. A game whose records are BELOW the notebook's number (an old records folder copied
   back in) is refused: its records writer stays off and its loads are refused. No number (0) fails open. Joiners and the
   operator alike. Nothing lowers a number.
   CHECKPOINTS: a folder per tag - a world save "g<gen>-s<nbSeq>-<profile>" (the OPERATOR's profile and stamp: numbers are per
   profile since the restore1a fold), a leave "l-s<nbSeq>-o<ownHigh>". The prune keeps the newest kCheckpointCap world tags per
   profile and the newest kCheckpointCap leave tags; a name that is not a tag (a ".part" folder, anything else) is never touched.
   THE BROADCAST: WORLD_SAVED down with one trailing u32 word (1) after the profile = "another world save was taken - checkpoint
   your records"; without it, WORLD_SAVED down is the operator's answer, as in restore1a. */
const unsigned int kCheckpointCap = 10;
const unsigned int kOwnHighPeriodMs = 30000;   /* a changed high-water is reported at most once per 30 s (and at once at a leave) */

enum { kOhAllow = 0, kOhAllowNoNumber = 1, kOhAllowNoStore = 2, kOhRefuseOlderRecords = 3 };
inline int OwnHighDecide(bool storeChosen, unsigned long long ownHigh, unsigned long long notebookHigh)
{
    if (!storeChosen) return kOhAllowNoStore;
    if (notebookHigh == 0ULL) return kOhAllowNoNumber;   /* fail open: the notebook has no number for these records */
    return ownHigh < notebookHigh ? kOhRefuseOlderRecords : kOhAllow;
}
inline const char* OwnHighRefusalWords()
{
    return "This game's records are older than the world remembers for you - an old records folder was put back";
}
/* ui1 (ui-polish-audit 3.2; the owner's wording): what the PLAYER reads for the same refusal - no "records" jargon. The
   log keeps OwnHighRefusalWords. No full stop: the callers add it. */
inline const char* OwnHighRefusalPlayerWords()
{
    return "Your save is older than the host's world";
}
/* words1b (owner 2026-09-27): the same refusal on the HOST's own game names no host (store.cpp OwnHighWordsForRole picks). No
   full stop: the callers add it. */
inline const char* OwnHighRefusalHostWords()
{
    return "Your save is older than the world";
}
/* A records store's name as it crosses the wire and names a book row: 1..48 of [A-Za-z0-9_-]. */
inline bool OwnStoreNameOk(const std::string& s)
{
    if (s.empty() || s.size() > 48) return false;
    for (std::string::size_type i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}
/* OWN_HIGH: {u32 kind, u32 high lo, u32 high hi, u32 len + store}. kind 0 = up (the game's report), 1 = down (the notebook's number). */
const unsigned int kOhUp = 0, kOhDown = 1;
const size_t kOhWireMin = 16;
struct OwnHighMsg
{
    unsigned int kind; unsigned long long high; std::string store;
    OwnHighMsg() : kind(0), high(0ULL) {}
};
inline void EncodeOwnHigh(std::vector<char>* out, const OwnHighMsg& m)   /* APPENDS */
{
    const size_t n = m.store.size() > 48 ? 48 : m.store.size();
    PutU32(out, m.kind); PutU32(out, (unsigned int)(m.high & 0xFFFFFFFFULL)); PutU32(out, (unsigned int)(m.high >> 32));
    PutU32(out, (unsigned int)n);
    out->insert(out->end(), m.store.begin(), m.store.begin() + n);
}
inline bool DecodeOwnHigh(const char* p, size_t n, OwnHighMsg* out)
{
    if (p == 0 || n < kOhWireMin) return false;
    unsigned int k = 0, lo = 0, hi = 0, len = 0;
    std::memcpy(&k, p, 4); std::memcpy(&lo, p + 4, 4); std::memcpy(&hi, p + 8, 4); std::memcpy(&len, p + 12, 4);
    if (k > kOhDown || len > 48 || n != kOhWireMin + (size_t)len) return false;
    OwnHighMsg m; m.kind = k; m.high = ((unsigned long long)hi << 32) | lo;
    m.store.assign(p + kOhWireMin, p + kOhWireMin + len);
    if (!OwnStoreNameOk(m.store)) return false;
    *out = m; return true;
}
/* THE NOTEBOOK'S BOOK (own_high.txt): (profile, store) -> the highest committed own seq ever reported. */
struct OwnBook { std::map<std::string, unsigned long long> high; };
inline std::string OwnBookKey(const std::string& profile, const std::string& store) { return profile + "\t" + store; }
inline unsigned long long OwnHighOf(const OwnBook& b, const std::string& profile, const std::string& store)
{
    std::map<std::string, unsigned long long>::const_iterator it = b.high.find(OwnBookKey(profile, store));
    return it == b.high.end() ? 0ULL : it->second;
}
/* THE MAXIMUM, NEVER BACK. 0 is no number. True = raised. */
inline bool MergeOwnHigh(OwnBook* b, const std::string& profile, const std::string& store, unsigned long long high)
{
    if (high == 0ULL || !ProfileIdOk(profile) || !OwnStoreNameOk(store)) return false;
    if (high <= OwnHighOf(*b, profile, store)) return false;
    b->high[OwnBookKey(profile, store)] = high;
    return true;
}
/* Every row of one profile: {store, high} - the WELCOME push. */
inline std::vector<std::pair<std::string, unsigned long long> > OwnRowsOf(const OwnBook& b, const std::string& profile)
{
    std::vector<std::pair<std::string, unsigned long long> > out;
    const std::string pre = profile + "\t";
    for (std::map<std::string, unsigned long long>::const_iterator it = b.high.begin(); it != b.high.end(); ++it)
        if (it->first.size() > pre.size() && it->first.compare(0, pre.size(), pre) == 0) out.push_back(std::make_pair(it->first.substr(pre.size()), it->second));
    return out;
}
/* own_high.txt: one `oh1<TAB>profile<TAB>store<TAB>high` line per row. */
inline std::string OwnBookFormat(const OwnBook& b)
{
    std::string s;
    for (std::map<std::string, unsigned long long>::const_iterator it = b.high.begin(); it != b.high.end(); ++it)
        s += "oh1\t" + it->first + "\t" + U64Text(it->second) + "\n";
    return s;
}
/* A line that does not read is skipped and counted (*bad); a repeated row keeps its maximum. */
inline void OwnBookParse(const std::string& text, OwnBook* out, int* bad)
{
    OwnBook b; int nbad = 0;
    std::string::size_type at = 0;
    while (at < text.size())
    {
        std::string::size_type nl = text.find('\n', at);
        std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = (nl == std::string::npos) ? text.size() : nl + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        const std::vector<std::string> f = SplitTabs(line);
        unsigned long long v = 0ULL;
        if (f.size() != 4 || f[0] != "oh1" || !ProfileIdOk(f[1]) || !OwnStoreNameOk(f[2]) || !ParseU64(f[3], &v) || v == 0ULL) { ++nbad; continue; }
        MergeOwnHigh(&b, f[1], f[2], v);
    }
    *out = b; if (bad != 0) *bad = nbad;
}

/* ---- THE CHECKPOINT TAG RULE ---- */
inline bool TagProfileOk(const std::string& p)
{
    if (p.empty() || p.size() > 64) return false;
    for (std::string::size_type i = 0; i < p.size(); ++i)
    {
        const char c = p[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    }
    return true;
}
inline std::string CheckpointTagWorld(unsigned int gen, unsigned long long seq, const std::string& profile)   /* "" = cannot name a folder */
{
    if (gen == 0 || !TagProfileOk(profile)) return std::string();
    return "g" + U64Text(gen) + "-s" + U64Text(seq) + "-" + profile;
}
inline std::string CheckpointTagLeave(unsigned long long seq, unsigned long long own) { return "l-s" + U64Text(seq) + "-o" + U64Text(own); }
/* T-251 part 2 (manager 2026-09-29 decision 2): the save right after a new character is made has its own tag */
inline std::string CheckpointTagCreated(unsigned long long seq, unsigned long long own) { return "c-s" + U64Text(seq) + "-o" + U64Text(own); }
enum { kCpNone = 0, kCpWorld = 1, kCpLeave = 2, kCpCreated = 3 };
struct CheckpointTag
{
    int kind; unsigned int gen; unsigned long long seq, own; std::string profile;
    CheckpointTag() : kind(kCpNone), gen(0), seq(0ULL), own(0ULL) {}
};
/* One spelling only (no leading zeros, a nameable profile); a ".part" folder (a copy in progress or cut short) is never a tag. */
inline bool ParseCheckpointTag(const std::string& name, CheckpointTag* out)
{
    CheckpointTag t;
    if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".part") == 0) return false;
    if (name.size() > 1 && name[0] == 'g')
    {
        const std::string::size_type d1 = name.find('-', 1);
        if (d1 == std::string::npos || d1 + 2 > name.size() || name[d1 + 1] != 's') return false;
        const std::string::size_type d2 = name.find('-', d1 + 2);
        if (d2 == std::string::npos) return false;
        if (!ParseU32(name.substr(1, d1 - 1), &t.gen) || !ParseU64(name.substr(d1 + 2, d2 - d1 - 2), &t.seq)) return false;
        t.profile = name.substr(d2 + 1); t.kind = kCpWorld;
        if (CheckpointTagWorld(t.gen, t.seq, t.profile) != name) return false;
        *out = t; return true;
    }
    if (name.compare(0, 3, "l-s") == 0)
    {
        const std::string::size_type d = name.find("-o", 3);
        if (d == std::string::npos) return false;
        if (!ParseU64(name.substr(3, d - 3), &t.seq) || !ParseU64(name.substr(d + 2), &t.own)) return false;
        t.kind = kCpLeave;
        if (CheckpointTagLeave(t.seq, t.own) != name) return false;
        *out = t; return true;
    }
    if (name.compare(0, 3, "c-s") == 0)   /* T-251 part 2: the same spelling as a leave tag, its own letter */
    {
        const std::string::size_type d = name.find("-o", 3);
        if (d == std::string::npos) return false;
        if (!ParseU64(name.substr(3, d - 3), &t.seq) || !ParseU64(name.substr(d + 2), &t.own)) return false;
        t.kind = kCpCreated;
        if (CheckpointTagCreated(t.seq, t.own) != name) return false;
        *out = t; return true;
    }
    return false;
}
/* THE PRUNE SELECTION: the names to move to the Recycle Bin - world tags beyond the newest `cap` of their profile (by gen, then
   seq), leave tags beyond the newest `cap` (fold: by ownHigh FIRST, then nbSeq). Nothing that is not a tag is ever selected. */
inline std::vector<std::string> PruneSelect(const std::vector<std::string>& names, unsigned int cap)
{
    typedef std::pair<std::pair<unsigned long long, unsigned long long>, std::string> Row;
    std::map<std::string, std::vector<Row> > groups;
    for (size_t i = 0; i < names.size(); ++i)
    {
        CheckpointTag t;
        if (!ParseCheckpointTag(names[i], &t)) continue;
        if (t.kind == kCpWorld) groups["w\t" + t.profile].push_back(Row(std::make_pair((unsigned long long)t.gen, t.seq), names[i]));
        else groups[t.kind == kCpCreated ? "c" : "l"].push_back(Row(std::make_pair(t.own, t.seq), names[i]));   /* fold (review item 5): the OWN number first; T-251 part 2: created tags are their own group */
    }
    std::vector<std::string> out;
    for (std::map<std::string, std::vector<Row> >::iterator it = groups.begin(); it != groups.end(); ++it)
    {
        std::vector<Row>& v = it->second;
        std::sort(v.begin(), v.end());
        if (v.size() > (size_t)cap) for (size_t i = 0; i < v.size() - (size_t)cap; ++i) out.push_back(v[i].second);
    }
    return out;
}
/* fold (review item 1): A REFUSAL IS RE-JUDGED AT EACH WELCOME - it must not follow the player into another world. The store
   stays chosen; the new WELCOME's numbers judge it again (rejudge = 1 until then). */
inline void OwnHighOnWelcomeReset(int* blocked, int* rejudge) { if (*blocked != 0) { *blocked = 0; *rejudge = 1; } }
/* The load gate: a store refused on THIS link, or records below the notebook's number, refuse the load. */
inline bool OwnHighLoadRefused(int blocked, bool storeChosen, unsigned long long ownHigh, unsigned long long notebookHigh)
{
    return blocked != 0 || OwnHighDecide(storeChosen, ownHigh, notebookHigh) == kOhRefuseOlderRecords;
}
/* fold (review item 5): the world's part of <store>\checkpoint\<world>\<tag>: 1..64 of [A-Za-z0-9._ -], not "." / "..", no
   trailing dot or space (Windows would drop it). */
inline bool CheckpointWorldOk(const std::string& w)
{
    if (w.empty() || w.size() > 64 || w == "." || w == ".." || w[w.size() - 1] == '.' || w[w.size() - 1] == ' ') return false;
    for (std::string::size_type i = 0; i < w.size(); ++i)
    {
        const char c = w[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ' ')) return false;
    }
    return true;
}
/* fold (review item 6): THE RECYCLE BIN CAN TAKE THE FOLDER - a local fixed drive whose bin answered SHQueryRecycleBinA, and the
   folder within 1% of the drive and never above 1 GiB (under the smallest bin Windows sets up by itself). Otherwise the
   caller moves nothing: never a hard delete, never a prompt. */
inline bool RecycleBinCanTake(bool fixedDrive, bool binAnswered, unsigned long long folderBytes, unsigned long long driveBytes)
{
    if (!fixedDrive || !binAnswered || driveBytes == 0ULL) return false;
    unsigned long long limit = driveBytes / 100ULL;
    if (limit > (1ULL << 30)) limit = 1ULL << 30;
    return folderBytes <= limit;
}
/* fold 2 (recheck item 1): the volume GUID "{........-....-....-....-............}" out of GetVolumeNameForVolumeMountPointA's
   "\\?\Volume{GUID}\" ("" = none), and the bin policy: nothing when it would delete for good, when the folder is above MaxCapacity
   (MB), or when the policy could not be read. */
inline std::string VolumeGuidOf(const std::string& volName)
{
    const std::string::size_type a = volName.find('{');
    if (a == std::string::npos) return std::string();
    const std::string::size_type b = volName.find('}', a);
    if (b == std::string::npos || b - a != 37) return std::string();
    return volName.substr(a, b - a + 1);
}
inline bool BinPolicyAllows(bool policyRead, unsigned long nukeOnDelete, unsigned long maxCapacityMb, unsigned long long folderBytes)
{
    if (!policyRead || nukeOnDelete != 0) return false;
    return folderBytes <= ((unsigned long long)maxCapacityMb << 20);
}
/* fold 2 (recheck item 3): the leave's continuation waits for its checkpoint - done when settled, or after capMs (then said). */
inline bool LeaveCheckpointHoldDone(bool settled, unsigned int heldMs, unsigned int capMs) { return settled || heldMs >= capMs; }
/* fold 2 (recheck item 4): a re-judge after a WELCOME is PENDING until this store's first OWN_HIGH row, or capMs - loads are held
   meanwhile (not fail-open). */
inline bool OwnHighRejudgePending(int rejudge, bool rowArrived, unsigned int msSinceWelcome, unsigned int capMs)
{
    return rejudge != 0 && !rowArrived && msSinceWelcome < capMs;
}
inline bool OwnHighLoadHeld(int blocked, bool rejudgePending, bool storeChosen, unsigned long long ownHigh, unsigned long long notebookHigh)
{
    return rejudgePending || OwnHighLoadRefused(blocked, storeChosen, ownHigh, notebookHigh);
}
/* The notebook's small non-record files copied into repair\<tag>\ at each new operator world save (a missing one is skipped). */
inline std::vector<std::string> RepairCopyFiles()
{
    static const char* const k[] = { "clock.txt", "uniques.txt", "options.txt", "research_boxes.txt", "research_takes.txt", "town_bars.txt", "world_relations.txt", "world.gen" };   /* par24: world_relations.txt */
    return std::vector<std::string>(k, k + sizeof(k) / sizeof(k[0]));
}
/* THE BROADCAST WORD after a WORLD_SAVED down's profile. */
const unsigned int kWorldBroadcastWord = 1;
inline void EncodeWorldBroadcast(std::vector<char>* out, const WorldMsg& m) { EncodeWorldMsg(out, m); PutU32(out, kWorldBroadcastWord); }
inline bool IsWorldBroadcast(const char* p, size_t n)
{
    WorldMsg m;
    if (!DecodeWorldMsg(p, n, &m)) return false;
    const size_t at = kWireMin + m.profile.size();
    if (n != at + 4) return false;
    unsigned int w = 0; std::memcpy(&w, p + at, 4);
    return w == kWorldBroadcastWord;
}

}   // namespace restoreguard

#endif
