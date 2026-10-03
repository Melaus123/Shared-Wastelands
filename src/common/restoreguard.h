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
   repairEpoch is fixed at 0 until restore1b (the repair). */
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
               kRefuseUnnumbered = 6, kRefuseKeyless = 7, kRefuseUndone = 8 };   /* restore1c: 8 = a save from a timeline a repair undid */

inline bool IsRefusal(int v) { return v == kRefuseOlderWorld || v == kRefuseNotebookBehind || v == kRefuseUnnumbered || v == kRefuseKeyless || v == kRefuseUndone; }
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
    case kRefuseUndone: return "refuseUndone";
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
   operator alike. Only the host's repair (restore1b part 2) may lower a number.
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
    return "This game's records are older than the world remembers for you - an old records folder was put back. Start a new profile, or ask the host to repair the world";
}
/* ui1 (ui-polish-audit 3.2; the owner's wording): what the PLAYER reads for the same refusal - no "records" jargon. The
   log keeps OwnHighRefusalWords. No full stop: the callers add it. */
inline const char* OwnHighRefusalPlayerWords()
{
    return "Your save is older than the host's world. Start a new profile, or ask the host to restore the world";
}
/* words1b (owner 2026-09-27): the same refusal on the HOST's own game names no host (store.cpp OwnHighWordsForRole picks). No
   full stop: the callers add it. */
inline const char* OwnHighRefusalHostWords()
{
    return "Your save is older than the world. Start a new profile, or restore the world";
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
   folder within 1% of the drive and never above 1 GiB (the profile-delete rule, store.cpp ProfileRecycleSave). Otherwise the
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

/* ==== restore1c (design s4 "Repair a broken world", s5 Run W; owner rules: the host-only repair rolls the WHOLE world back, every
   player's records included, never partially; running players restart; records always win otherwise) ====
   repair.txt (the notebook): one `rp1<TAB>epoch<TAB>restoreGen<TAB>restoreSeq<TAB>profile<TAB>unix time` row per repair.
   REPAIR (store message 50, store protocol 53 - nothing at 53 shipped): {u32 kind, u32 epoch, u32 gen, u32 seq lo, u32 seq hi,
   u32 recycled, u32 result, u32 len + profile}. Up (kind 0): the operator's request {gR, sR, its profile}. Down (kind 1): the answer
   and the broadcast to every linked game {new epoch, gR, sR, notebook records recycled, result 0} or a refusal to the sender alone.
   The WELCOME's world tail gains the repair rows after the WorldMsg: {u32 count, count x (u32 epoch, u32 gen, u32 seq lo, u32 hi)}.
   A SAVE OR A RECORD STAMPED (epoch e, seq s) IS UNDONE when some repair with a later epoch restored to a seq below s. */
struct RepairRow
{
    unsigned int epoch, gen; unsigned long long seq; std::string profile; long long at;
    RepairRow() : epoch(0), gen(0), seq(0ULL), at(0) {}
};
/* The restore seq of the FIRST repair (in epoch order) after `epoch` that undoes seq; false = every later repair keeps it. */
inline bool FirstUndoing(const std::vector<RepairRow>& rows, unsigned int epoch, unsigned long long seq, unsigned long long* sR)
{
    bool found = false; unsigned int best = 0;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].epoch > epoch && seq > rows[i].seq && (!found || rows[i].epoch < best)) { found = true; best = rows[i].epoch; *sR = rows[i].seq; }
    return found;
}
inline bool UndoneBy(const std::vector<RepairRow>& rows, unsigned int epoch, unsigned long long seq)
{
    unsigned long long sR = 0ULL;
    return FirstUndoing(rows, epoch, seq, &sR);
}
/* The load gate with the repair list: an otherwise allowed OPERATOR save from an undone timeline is refused. */
inline int DecideRepaired(bool isOperator, const SaveStamp& s, const WorldState& w, const std::vector<RepairRow>& rows)
{
    const int v = Decide(isOperator, s, w);
    if (v == kAllow && s.hasGen && UndoneBy(rows, s.epoch, s.seq)) return kRefuseUndone;
    return v;
}
/* THE NOTEBOOK'S RULE FOR ONE RECORD: seq <= sR keeps it; otherwise its previous version comes back only if that version's seq
   is <= sR; otherwise the record goes to the Recycle Bin (its key then loads from the host's save R). */
enum { kNbKeep = 0, kNbRevert = 1, kNbRecycle = 2 };
inline int RepairRecordAction(unsigned long long curSeq, bool havePrev, unsigned long long prevSeq, unsigned long long sR)
{
    if (curSeq <= sR) return kNbKeep;
    if (havePrev && prevSeq <= sR) return kNbRevert;
    return kNbRecycle;
}
/* The request's numbers must be ones this notebook has seen for that profile. */
enum { kRpOk = 0, kRpNotOperator = 1, kRpProfile = 2, kRpNumbers = 3, kRpNoCopy = 4, kRpNoBin = 5 };
inline const char* RepairResultName(unsigned int r)
{
    switch (r)
    {
    case kRpOk: return "ok";
    case kRpNotOperator: return "notOperator";
    case kRpProfile: return "profile";
    case kRpNumbers: return "numbers";
    case kRpNoCopy: return "noRepairCopy";
    case kRpNoBin: return "recycleBinCannotTake";
    }
    return "?";
}
inline int RepairValidate(unsigned int bookGen, unsigned long long seqHigh, unsigned int gR, unsigned long long sR)
{
    if (gR == 0 || gR > bookGen || sR > seqHigh) return kRpNumbers;
    return kRpOk;
}
/* The non-record files the repair puts back from repair\<tag>\ - world.gen is not: the book keeps every other profile's number,
   lowers the repairing profile's to gR and raises the epoch. A file absent from the copy did not exist at R (it goes to the bin). */
inline std::vector<std::string> RepairPutBackFiles()
{
    std::vector<std::string> all = RepairCopyFiles(), out;
    for (size_t i = 0; i < all.size(); ++i) if (all[i] != "world.gen") out.push_back(all[i]);
    return out;
}
inline std::string RepairRowFormat(const RepairRow& r)
{
    return "rp1\t" + U64Text(r.epoch) + "\t" + U64Text(r.gen) + "\t" + U64Text(r.seq) + "\t" + r.profile + "\t" + U64Text(r.at < 0 ? 0ULL : (unsigned long long)r.at) + "\n";
}
inline std::string RepairsFormat(const std::vector<RepairRow>& rows)
{
    std::string s;
    for (size_t i = 0; i < rows.size(); ++i) s += RepairRowFormat(rows[i]);
    return s;
}
/* Rows in epoch order, one per epoch (the first kept); a line that does not read is skipped and counted (the return). */
inline int RepairsParse(const std::string& text, std::vector<RepairRow>* out)
{
    std::map<unsigned int, RepairRow> byEpoch; int bad = 0;
    std::string::size_type at = 0;
    while (at < text.size())
    {
        std::string::size_type nl = text.find('\n', at);
        std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = (nl == std::string::npos) ? text.size() : nl + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        const std::vector<std::string> f = SplitTabs(line);
        RepairRow r; unsigned long long t = 0ULL;
        if (f.size() != 6 || f[0] != "rp1" || !ParseU32(f[1], &r.epoch) || r.epoch == 0 || !ParseU32(f[2], &r.gen) || !ParseU64(f[3], &r.seq)
            || !ProfileIdOk(f[4]) || !ParseU64(f[5], &t)) { ++bad; continue; }
        r.profile = f[4]; r.at = (long long)t;
        if (byEpoch.find(r.epoch) == byEpoch.end()) byEpoch[r.epoch] = r;
    }
    out->clear();
    for (std::map<unsigned int, RepairRow>::const_iterator it = byEpoch.begin(); it != byEpoch.end(); ++it) out->push_back(it->second);
    return bad;
}
/* A game's list: insert a row it has not seen (epoch order). True = new. */
inline bool RepairLearn(std::vector<RepairRow>* rows, const RepairRow& r)
{
    for (size_t i = 0; i < rows->size(); ++i) if ((*rows)[i].epoch == r.epoch) return false;
    size_t at = 0;
    while (at < rows->size() && (*rows)[at].epoch < r.epoch) ++at;
    rows->insert(rows->begin() + (std::vector<RepairRow>::difference_type)at, r);
    return true;
}
const unsigned int kRpUp = 0, kRpDown = 1;
const size_t kRpWireMin = 32;
struct RepairMsg
{
    unsigned int kind, epoch, gen; unsigned long long seq; unsigned int recycled, result; std::string profile;
    RepairMsg() : kind(0), epoch(0), gen(0), seq(0ULL), recycled(0), result(0) {}
};
inline void EncodeRepairMsg(std::vector<char>* out, const RepairMsg& m)   /* APPENDS */
{
    const size_t n = m.profile.size() > 64 ? 64 : m.profile.size();
    PutU32(out, m.kind); PutU32(out, m.epoch); PutU32(out, m.gen); PutU32(out, (unsigned int)(m.seq & 0xFFFFFFFFULL)); PutU32(out, (unsigned int)(m.seq >> 32));
    PutU32(out, m.recycled); PutU32(out, m.result); PutU32(out, (unsigned int)n);
    out->insert(out->end(), m.profile.begin(), m.profile.begin() + n);
}
inline bool DecodeRepairMsg(const char* p, size_t n, RepairMsg* out)
{
    if (p == 0 || n < kRpWireMin) return false;
    unsigned int w[8];
    for (int i = 0; i < 8; ++i) std::memcpy(&w[i], p + 4 * i, 4);
    if (w[0] > kRpDown || w[7] > 64 || n != kRpWireMin + (size_t)w[7]) return false;
    RepairMsg m; m.kind = w[0]; m.epoch = w[1]; m.gen = w[2]; m.seq = ((unsigned long long)w[4] << 32) | w[3]; m.recycled = w[5]; m.result = w[6];
    m.profile.assign(p + kRpWireMin, p + kRpWireMin + w[7]);
    *out = m; return true;
}
/* The WELCOME tail's repair rows (the newest 64), APPENDED after the WorldMsg. */
inline void EncodeRepairRows(std::vector<char>* out, const std::vector<RepairRow>& rows)
{
    const size_t from = rows.size() > 64 ? rows.size() - 64 : 0;
    PutU32(out, (unsigned int)(rows.size() - from));
    for (size_t i = from; i < rows.size(); ++i)
    {
        PutU32(out, rows[i].epoch); PutU32(out, rows[i].gen);
        PutU32(out, (unsigned int)(rows[i].seq & 0xFFFFFFFFULL)); PutU32(out, (unsigned int)(rows[i].seq >> 32));
    }
}
/* The rows after the tail's WorldMsg. No rows at all (a restore1b1 notebook) reads as an empty list; anything else malformed is false. */
inline bool DecodeRepairTail(const char* p, size_t n, std::vector<RepairRow>* rows)
{
    rows->clear();
    WorldMsg m;
    if (!DecodeWorldMsg(p, n, &m)) return false;
    const size_t at = kWireMin + m.profile.size();
    if (n == at) return true;
    if (n < at + 4) return false;
    unsigned int c = 0; std::memcpy(&c, p + at, 4);
    if (c > 64 || n != at + 4 + (size_t)c * 16) return false;
    for (unsigned int i = 0; i < c; ++i)
    {
        const char* q = p + at + 4 + (size_t)i * 16;
        unsigned int e = 0, g = 0, lo = 0, hi = 0;
        std::memcpy(&e, q, 4); std::memcpy(&g, q + 4, 4); std::memcpy(&lo, q + 8, 4); std::memcpy(&hi, q + 12, 4);
        RepairRow r; r.epoch = e; r.gen = g; r.seq = ((unsigned long long)hi << 32) | lo;
        if (e == 0) return false;
        RepairLearn(rows, r);
    }
    return true;
}
/* ---- THE RULE ON A PLAYER'S PC (own records) ----
   A record with an older epoch survives only if its nbSeq is <= the restore seq of each later repair, applied in order. Otherwise
   the squad is taken from the newest checkpoint tagged <= the restore seq of the first repair that undoes it, whose own copy
   survives every repair; with no such copy the squad did not exist at R and is dropped. A checkpoint copy's nbSeq is its line's
   nbSeq capped by the checkpoint's tag seq (an o1 line: the tag seq, epoch 0). */
struct OwnStampView { unsigned int epoch; unsigned long long nbSeq; OwnStampView() : epoch(0), nbSeq(0ULL) {} };
struct CpView { unsigned long long tagSeq; std::map<std::string, OwnStampView> lines; CpView() : tagSeq(0ULL) {} };
enum { kOrKeep = 0, kOrFromCheckpoint = 1, kOrDrop = 2 };
struct OwnRepairStep { int action; int cp; OwnStampView stamp; OwnRepairStep() : action(kOrKeep), cp(-1) {} };
inline std::map<std::string, OwnRepairStep> OwnRepairPlan(const std::vector<RepairRow>& rows, const std::map<std::string, OwnStampView>& cur,
                                                           const std::vector<CpView>& cps)
{
    std::map<std::string, OwnRepairStep> out;
    for (std::map<std::string, OwnStampView>::const_iterator it = cur.begin(); it != cur.end(); ++it)
    {
        OwnRepairStep st; unsigned long long sR = 0ULL;
        if (!FirstUndoing(rows, it->second.epoch, it->second.nbSeq, &sR)) { st.stamp = it->second; out[it->first] = st; continue; }
        int best = -1; OwnStampView bv;
        for (size_t i = 0; i < cps.size(); ++i)
        {
            if (cps[i].tagSeq > sR) continue;
            std::map<std::string, OwnStampView>::const_iterator c = cps[i].lines.find(it->first);
            if (c == cps[i].lines.end() || UndoneBy(rows, c->second.epoch, c->second.nbSeq)) continue;
            const bool better = best < 0 || cps[i].tagSeq > cps[best].tagSeq
                || (cps[i].tagSeq == cps[best].tagSeq && (c->second.epoch > bv.epoch || (c->second.epoch == bv.epoch && c->second.nbSeq > bv.nbSeq)));
            if (better) { best = (int)i; bv = c->second; }
        }
        if (best >= 0) { st.action = kOrFromCheckpoint; st.cp = best; st.stamp = bv; }
        else st.action = kOrDrop;
        out[it->first] = st;
    }
    return out;
}
/* The ledger's base when a player's records roll back: the newest checkpoint tagged <= the lowest restore seq of the repairs this
   store has not applied (-1 = none: the ledger just goes to the bin). */
inline int LedgerBaseCheckpoint(const std::vector<RepairRow>& rows, unsigned int appliedEpoch, const std::vector<CpView>& cps)
{
    bool any = false; unsigned long long lo = 0ULL;
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].epoch > appliedEpoch && (!any || rows[i].seq < lo)) { any = true; lo = rows[i].seq; }
    if (!any) return -1;
    int best = -1;
    for (size_t i = 0; i < cps.size(); ++i) if (cps[i].tagSeq <= lo && (best < 0 || cps[i].tagSeq > cps[best].tagSeq)) best = (int)i;
    return best;
}
/* own.repair in a records store: `rs1<TAB>epoch` - the highest repair epoch this store has applied. */
inline std::string OwnRepairSeenFormat(unsigned int epoch) { return "rs1\t" + U64Text(epoch) + "\n"; }
inline bool OwnRepairSeenParse(const std::string& text, unsigned int* epoch)
{
    std::string line = text.substr(0, text.find('\n'));
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    const std::vector<std::string> f = SplitTabs(line);
    return f.size() == 2 && f[0] == "rs1" && ParseU32(f[1], epoch);
}
inline const char* RepairStaleWords() { return "The host repaired the world - restart to continue"; }
inline const char* RepairStalePlayerWords() { return "The host has restored the world. Restart Kenshi to continue"; }   /* ui1: the player's words; the log keeps RepairStaleWords */

}   // namespace restoreguard

#endif
