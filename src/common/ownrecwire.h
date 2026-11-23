/* ownrecwire.h - T-885 piece 1 "Upload" (docs/design-ownrec.md sections 1, 2, 5, 6, 8; owner 1734 / 1745 a / 1746 c / 1747 a / 1752 a).
 *
 * WHAT IT IS FOR. Every record a player's game writes into its own records folder (squads, squad settings, money, research, map,
 * standings, buildings, help, and the three small text files) is sent to the world server right after it lands on the player's
 * disk, and the world server keeps a copy in <world folder>\own\<profile id>\, answering each one only once the copy is on its own
 * disk. In this piece NOTHING READS THE SERVER'S COPY: the player's own folder is still the only source.
 *
 * PIECE 2 "Take" (protocol 163, design sections 3 and 6): BEFORE EVERY LOAD the game asks the world server what it holds for this
 * player (INDEX_ASK -> INDEX), fetches every record the world holds newer than its own (GET -> REC ... END), writes them into its own
 * folder as committed records with the world's W, raises its write counter to the world's highest W, and sets own.sent to the
 * world's INDEX (the class "a cache fed only by received messages"). A record whose local W is above the world's is an unconfirmed
 * write: kept and sent (owner 1745 a). An older folder put back is replaced by the world's copies (owner 1753, no new player text).
 *
 * THE MESSAGE. OWNREC, store-link message 105, world-server protocol 163 (piece 1 was 162). Kinds:
 *   PUT (up)  {u8 1, str store, str key, u64 W, u32 len, u32 crc, u32 people, u64 at, len bytes}   (at: the record's own write time,
 *             unix seconds of the writing PC's own index line; 0 for a text file - piece 2 review fold, finding 5)
 *   OK (down) {u8 2, str key, u64 W, u64 R, u8 verdict, u8 why}
 *   INDEX_ASK (up)  {u8 3, u32 token, str store}
 *   INDEX (down)    {u8 4, u32 token, u8 status, u32 n, n * (str key, u64 W, u64 R, u32 len, u32 crc, u32 people, u64 at)}
 *   GET (up)        {u8 5, u32 token, u32 n, n * str key}
 *   REC (down)      {u8 6, u32 token, str key, u8 found, u64 W, u64 R, u32 crc, u32 people, u64 at, u32 len, len bytes}
 *   END (down)      {u8 7, u32 token, u32 asked, u32 found, u8 status}   (every GET is answered with an END - a malformed one too, status
 *                   malformed, carrying the exchange number read from its first bytes)
 *   str = u16 length + bytes (at most kStrMax); integers little-endian. A PUT over 32 KB goes as CHUNKs (livefirst.h) like any frame.
 * W is the game's write number for that record (own.index's seq; for the three text files the store's write counter when sent);
 * R is the world server's receipt number, taken from the world's own record counter (world.gen keeps it across a reset), never
 * issued twice. The profile is NEVER in the message: the world server takes it from the admitted connection.
 *
 * Pure: no disk, no clock, no engine, no Windows. C++03. The game (src/coop-plugin/ownupload.cpp), the world server
 * (src/coop-store/store_main.cpp) and the offline suite compile this same header.
 */
#ifndef SW_OWNRECWIRE_H
#define SW_OWNRECWIRE_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <cstdio>
#include "storemeta.h"   /* coopstore::Crc32 */
#include "ownrec.h"      /* the game's own readers of each record kind */
#include "handrec.h"     /* pp.handover's line reader */
#include "moneywire.h"   /* pp.money's reader as the game's load reads it (moneyw::DecodeRec, versions 1 and 2) - review fold 6 */

namespace ownrecw {

const unsigned int kMsgOwnRec = 105u;   /* both ways; the next after PLAYER_MODS 104 */
const unsigned int kProtocol = 177u;    /* the world-server protocol that carries OWNREC (162: PUT / OK; 163: T-885 piece 2's INDEX_ASK / INDEX / GET / REC / END; 164: the merge with effort/t1167b's TRADE) */
enum { kPut = 1, kOk = 2, kIndexAsk = 3, kIndex = 4, kGet = 5, kRec = 6, kEnd = 7 };
/* the four answers (design section 2) */
enum { kStored = 1, kDuplicate = 2, kSuperseded = 3, kRefused = 4 };
/* why a PUT was refused (kWhyNone for every other answer) */
enum { kWhyNone = 0, kWhyMalformed = 1, kWhySize = 2, kWhyCrc = 3, kWhyUnreadable = 4, kWhyKey = 5, kWhyProfile = 6, kWhyConflict = 7,
       kWhyWrite = 8, kWhyStore = 9, kWhyProfileFull = 10 };
const unsigned int kMaxRecord = 1048576u;            /* 1 MB: one record (about 25x the largest seen, ~38 KB - the class "a safety bound set below the real size") */
const unsigned long long kMaxProfile = 33554432ULL;  /* 32 MB: one profile's records together */
const size_t kStrMax = 256;
const size_t kKeyMax = 160;
const char* const kOwnDir = "own";                   /* <world folder>\own\<profile id>\ */
const char* const kServerIndex = "own.index";        /* the world server's per-profile index - its line is the commit point */
const char* const kSentFile = "own.sent";            /* the game's outbox file in its records folder */

inline const char* VerdictName(int v)
{
    return v == kStored ? "stored" : v == kDuplicate ? "duplicate" : v == kSuperseded ? "superseded" : v == kRefused ? "refused" : "?";
}
inline const char* WhyName(int w)
{
    switch (w)
    {
    case kWhyNone: return "none"; case kWhyMalformed: return "malformed"; case kWhySize: return "size"; case kWhyCrc: return "crc";
    case kWhyUnreadable: return "unreadable"; case kWhyKey: return "key"; case kWhyProfile: return "profile"; case kWhyConflict: return "conflict";
    case kWhyWrite: return "write"; case kWhyStore: return "store"; case kWhyProfileFull: return "profileFull";
    default: return "?";
    }
}

/* ---- which records travel (design section 1) ---- */
inline bool KeyCharOk(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'; }
/* the three small text files, sent whole each time they change */
inline bool IsTextKey(const std::string& k) { return k == "pp.left.notes" || k == "pp.ledger" || k == "pp.handover"; }
/* every key that travels: the indexed records (squads, squad settings, the four categories, buildings, help) and the text files.
   A key is a file name in both folders, so only [A-Za-z0-9_.-], never "..", never a leading dot. */
inline bool IsTravelKey(const std::string& k)
{
    if (k.empty() || k.size() > kKeyMax || k[0] == '.' || k.find("..") != std::string::npos) return false;
    for (std::string::size_type i = 0; i < k.size(); ++i) if (!KeyCharOk(k[i])) return false;
    if (IsTextKey(k)) return true;
    if (k == "pp.money" || k == "pp.research" || k == "pp.map" || k == "pp.faction" || k == "pp.build" || k == "pp.help") return true;
    if (k.size() > 9 && k.compare(0, 9, "pp.squad.") == 0) return true;
    if (k.size() > 10 && k.compare(0, 10, "pp.sqhead.") == 0) return true;
    return false;
}
/* the world server's folder name for a profile id (profiles.h "<player id>.<n>"): the same safe characters, at most 64 */
inline bool ProfileFolderOk(const std::string& p)
{
    if (p.empty() || p.size() > 64 || p[0] == '.' || p.find("..") != std::string::npos) return false;
    for (std::string::size_type i = 0; i < p.size(); ++i) if (!KeyCharOk(p[i])) return false;
    return true;
}

/* ---- the content check: read to its end with the reader the game's load uses for that kind (the class "a stored copy trusted
   from its checksum alone"). true = readable. ---- */
inline bool TextLinesAll(const std::string& text, int kind)
{
    std::string::size_type from = 0;
    while (from < text.size())
    {
        std::string::size_type nl = text.find('\n', from);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(from, nl - from);
        from = nl + 1;
        while (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        if (kind == 0) { hovrec::HandLine h; if (hovrec::HandLineParse(line, &h) == 0) return false; }
        else { coopown::LedgerEntry e; if (coopown::LedgerLineParse(line, &e) == 0) return false; }
    }
    return true;
}
inline bool ContentReadable(const std::string& key, const char* p, size_t n)
{
    for (size_t i = 0; IsTextKey(key) && i < n; ++i) if (p[i] == 0) return false;   /* a text file holds no NUL */
    if (key == "pp.left.notes")
    {
        std::vector<coopown::LeftNote> v;
        return coopown::LeftNoteParseAll(std::string(p, p + n), &v) == 0;
    }
    if (key == "pp.ledger") return TextLinesAll(std::string(p, p + n), 1);
    if (key == "pp.handover") return TextLinesAll(std::string(p, p + n), 0);
    if (n == 0) return false;
    const std::vector<char> b(p, p + n);
    if (key.compare(0, 9, "pp.squad.") == 0) { coopown::GdcScanOut o; return coopown::GdcScan(p, n, key, &o); }
    if (key.compare(0, 10, "pp.sqhead.") == 0) { coopown::SquadHead h; return coopown::DecodeSquadHead(b, &h); }
    if (key == "pp.money") { int m = 0, ver = 0; moneyw::Ledger l; return moneyw::DecodeRec(b, &m, &l, &ver); }   /* review fold 6 (T2509): the game writes VERSION 2 (balance + the T-810 ledger, 36 bytes with no rows), which coopown::DecodeMoney (version 1 only) refused */
    if (key == "pp.research") { coopown::ResearchRec r; return coopown::DecodeResearch(b, &r); }
    if (key == "pp.map") { std::vector<coopown::MapRow> r; return coopown::DecodeMap(b, &r); }
    if (key == "pp.faction") { coopown::FactionRec f; return coopown::DecodeFaction(b, &f); }
    if (key == "pp.build") { std::vector<coopown::BuildRow> r; return coopown::DecodeBuildRec(b, &r); }
    if (key == "pp.help") { std::vector<coopown::HelpRecRow> r; return coopown::DecodeHelpRec(b, &r); }
    return false;
}

/* ---- the wire ---- */
inline void PutU8(std::vector<char>* b, unsigned int v) { b->push_back((char)(v & 0xFF)); }
inline void PutU16(std::vector<char>* b, unsigned int v) { PutU8(b, v); PutU8(b, v >> 8); }
inline void PutU32(std::vector<char>* b, unsigned int v) { PutU16(b, v & 0xFFFF); PutU16(b, v >> 16); }
inline void PutU64(std::vector<char>* b, unsigned long long v) { PutU32(b, (unsigned int)(v & 0xFFFFFFFFULL)); PutU32(b, (unsigned int)(v >> 32)); }
inline bool PutStr(std::vector<char>* b, const std::string& s)
{
    if (s.size() > kStrMax) return false;
    PutU16(b, (unsigned int)s.size()); b->insert(b->end(), s.begin(), s.end()); return true;
}
struct Cur { const unsigned char* p; size_t n, at; bool bad; };
inline void CurInit(Cur* c, const char* p, size_t n) { c->p = (const unsigned char*)p; c->n = p ? n : 0; c->at = 0; c->bad = false; }
inline unsigned int GetU8(Cur* c) { if (c->bad || c->at + 1 > c->n) { c->bad = true; return 0; } return c->p[c->at++]; }
inline unsigned int GetU16(Cur* c) { const unsigned int a = GetU8(c); return a | (GetU8(c) << 8); }
inline unsigned int GetU32(Cur* c) { const unsigned int a = GetU16(c); return a | (GetU16(c) << 16); }
inline unsigned long long GetU64(Cur* c) { const unsigned long long a = GetU32(c); return a | ((unsigned long long)GetU32(c) << 32); }
inline void GetStr(Cur* c, std::string* s)
{
    const unsigned int n = GetU16(c);
    if (c->bad || n > kStrMax || c->at + n > c->n) { c->bad = true; return; }
    s->assign((const char*)c->p + c->at, n); c->at += n;
}

struct Put
{
    std::string store, key; unsigned long long w; unsigned int len, crc, people; unsigned long long at; std::vector<char> bytes;
    Put() : w(0), len(0), crc(0), people(0), at(0) {}
};
struct Ok
{
    std::string key; unsigned long long w, r; unsigned int verdict, why;
    Ok() : w(0), r(0), verdict(0), why(0) {}
};
inline bool EncodePut(const Put& m, std::vector<char>* b)
{
    b->clear();
    if (m.bytes.size() != (size_t)m.len) return false;
    PutU8(b, kPut);
    if (!PutStr(b, m.store) || !PutStr(b, m.key)) return false;
    PutU64(b, m.w); PutU32(b, m.len); PutU32(b, m.crc); PutU32(b, m.people); PutU64(b, m.at);
    b->insert(b->end(), m.bytes.begin(), m.bytes.end());
    return true;
}
/* false = malformed: a wrong kind, a string over its bound, a length that is not exactly the bytes that follow */
inline bool DecodePut(const char* p, size_t n, Put* m)
{
    Cur c; CurInit(&c, p, n);
    if (GetU8(&c) != kPut || c.bad) return false;
    GetStr(&c, &m->store); GetStr(&c, &m->key);
    m->w = GetU64(&c); m->len = GetU32(&c); m->crc = GetU32(&c); m->people = GetU32(&c); m->at = GetU64(&c);
    if (c.bad || c.n - c.at != (size_t)m->len) return false;
    m->bytes.assign(p + c.at, p + c.n);
    return true;
}
inline bool EncodeOk(const Ok& m, std::vector<char>* b)
{
    b->clear(); PutU8(b, kOk);
    if (!PutStr(b, m.key)) return false;
    PutU64(b, m.w); PutU64(b, m.r); PutU8(b, m.verdict); PutU8(b, m.why);
    return true;
}
inline bool DecodeOk(const char* p, size_t n, Ok* m)
{
    Cur c; CurInit(&c, p, n);
    if (GetU8(&c) != kOk || c.bad) return false;
    GetStr(&c, &m->key); m->w = GetU64(&c); m->r = GetU64(&c); m->verdict = GetU8(&c); m->why = GetU8(&c);
    if (c.bad || c.at != c.n || m->verdict < kStored || m->verdict > kRefused) return false;
    return true;
}

/* ---- THE PER-KEY VERDICT (world server; design section 2 "The numbers") ----
   The record has already passed the length, CRC and reader checks. held = what this profile's folder holds for the key.
   Higher W = stored; the same W and the same bytes (len + CRC) = duplicate (nothing written, the OK repeats the receipt R given
   the first time); lower W = superseded. A text file is sent whole with the store's write counter, which may not have moved
   between two changes of it, so for a text key the same W with other bytes is a newer copy (stored); for an indexed record the
   same W with other bytes cannot come from one game (W names one write) and is refused as a conflict. Caps: one record over
   kMaxRecord, or a profile whose records would pass kMaxProfile, is refused (size / profileFull). No clock anywhere. */
struct Held
{
    bool have; unsigned long long w, r; unsigned int len, crc, people; unsigned long long at;   /* at: the record's own write time (finding 5); 0 = not known */
    Held() : have(false), w(0), r(0), len(0), crc(0), people(0), at(0) {}
};
inline int Judge(const Held& h, bool textKey, unsigned long long w, unsigned int len, unsigned int crc, unsigned long long profileBytes, int* why)
{
    *why = kWhyNone;
    if (len > kMaxRecord) { *why = kWhySize; return kRefused; }
    if (h.have && w == h.w && len == h.len && crc == h.crc) return kDuplicate;
    if (h.have && w < h.w) return kSuperseded;
    if (h.have && w == h.w && !textKey) { *why = kWhyConflict; return kRefused; }
    const unsigned long long after = profileBytes - (h.have && profileBytes >= h.len ? h.len : 0) + len;
    if (after > kMaxProfile) { *why = kWhyProfileFull; return kRefused; }
    return kStored;
}

/* ---- the world server's per-profile index: "or1\t<key>\t<W>\t<R>\t<len>\t<crc hex8>\t<people>\n", one line per key ---- */
inline bool EncodeServerLine(const std::string& key, const Held& h, std::string* out)
{
    if (!IsTravelKey(key)) return false;
    char b[160];
    if (h.at != 0ULL) std::sprintf(b, "\t%llu\t%llu\t%u\t%08x\t%u\t%llu\n", h.w, h.r, h.len, h.crc, h.people, h.at);   /* T-885 piece 2 review fold (finding 5): the record's own write time */
    else std::sprintf(b, "\t%llu\t%llu\t%u\t%08x\t%u\n", h.w, h.r, h.len, h.crc, h.people);   /* no time known: piece 1's seven fields */
    *out = "or1\t" + key + b;
    return true;
}
inline bool SplitTabs(const std::string& lineIn, std::vector<std::string>* f)
{
    std::string line = lineIn;
    while (!line.empty() && (line[line.size() - 1] == '\n' || line[line.size() - 1] == '\r')) line.erase(line.size() - 1);
    f->clear();
    std::string::size_type at = 0;
    for (;;)
    {
        const std::string::size_type t = line.find('\t', at);
        if (t == std::string::npos) { f->push_back(line.substr(at)); break; }
        f->push_back(line.substr(at, t - at)); at = t + 1;
    }
    return true;
}
inline bool ParseHex8(const std::string& s, unsigned int* v)
{
    if (s.size() != 8) return false;
    unsigned int r = 0;
    for (int i = 0; i < 8; ++i)
    {
        const char c = s[i]; unsigned int d;
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0'); else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10); else return false;
        r = (r << 4) | d;
    }
    *v = r; return true;
}
inline bool ParseU32(const std::string& s, unsigned int* v)
{
    unsigned long long u = 0;
    if (!coopown::ParseULL(s, &u) || u > 0xFFFFFFFFULL) return false;
    *v = (unsigned int)u; return true;
}
inline bool ParseServerLine(const std::string& line, std::string* key, Held* h)
{
    std::vector<std::string> f; SplitTabs(line, &f);
    /* 8 fields: with the write time (finding 5); 7: piece 1's line, read with time 0 (the record then loses the overlay's save-time rule
       to a save). Fold round 2 (2026-11-17), KEPT: a 7-field line is written only by a piece-1 world server, and piece 1 is on main only,
       in no released build and not in the hand build effort/hand1117 (none of f4ac413e6 / 10a93e0bd / 48fd98fba / f454bed98 is its
       ancestor; checked with git merge-base) - so such lines exist only in bench worlds. If piece 1 ever ships alone, the world server
       should give a 7-field line its record file's last-write time instead. */
    if ((f.size() != 7 && f.size() != 8) || f[0] != "or1" || !IsTravelKey(f[1])) return false;
    Held x; x.have = true;
    if (!coopown::ParseULL(f[2], &x.w) || !coopown::ParseULL(f[3], &x.r) || !ParseU32(f[4], &x.len) || !ParseHex8(f[5], &x.crc) || !ParseU32(f[6], &x.people)) return false;
    if (f.size() == 8 && !coopown::ParseULL(f[7], &x.at)) return false;
    *key = f[1]; *h = x; return true;
}
/* the whole index: false = a line that does not parse (the whole file is then UNREADABLE - never written over, every PUT of that
   profile refused "store" until an operator looks; the class "missing and unreadable treated as one answer") */
inline bool ParseServerIndex(const std::string& text, std::map<std::string, Held>* out)
{
    out->clear();
    std::string::size_type from = 0;
    while (from < text.size())
    {
        std::string::size_type nl = text.find('\n', from);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(from, nl - from);
        from = nl + 1;
        if (line.empty() || line == "\r") continue;
        std::string k; Held h;
        if (!ParseServerLine(line, &k, &h)) { out->clear(); return false; }
        (*out)[k] = h;
    }
    return true;
}
inline std::string ServerIndexText(const std::map<std::string, Held>& m)
{
    std::string all, line;
    for (std::map<std::string, Held>::const_iterator it = m.begin(); it != m.end(); ++it) if (EncodeServerLine(it->first, it->second, &line)) all += line;
    return all;
}
inline unsigned long long ProfileBytes(const std::map<std::string, Held>& m)
{
    unsigned long long t = 0;
    for (std::map<std::string, Held>::const_iterator it = m.begin(); it != m.end(); ++it) t += it->second.len;
    return t;
}
inline unsigned long long MaxReceipt(const std::map<std::string, Held>& m)
{
    unsigned long long t = 0;
    for (std::map<std::string, Held>::const_iterator it = m.begin(); it != m.end(); ++it) if (it->second.r > t) t = it->second.r;
    return t;
}
/* review fold 1 (2026-11-17): THE INDEX IS BUILT ON THE WRITER THREAD FROM THE LANDED WRITES ONLY. The loop's memory (what the folder
   will hold once every queued write lands) judges a PUT; it never builds an index text, so a write that fails can never be named by a
   later write's index. IndexWithJob = the index text one job writes (the landed map with this job's copy); LandJob = what the landed
   map becomes after it (the copy taken only when the whole write landed) and *now = what the index names for the key afterwards. */
inline std::string IndexWithJob(const std::map<std::string, Held>& landed, const std::string& key, const Held& nw)
{
    std::map<std::string, Held> next = landed; next[key] = nw;
    return ServerIndexText(next);
}
inline void LandJob(std::map<std::string, Held>* landed, const std::string& key, const Held& nw, bool ok, Held* now)
{
    if (ok) (*landed)[key] = nw;
    std::map<std::string, Held>::const_iterator it = landed->find(key);
    *now = it != landed->end() ? it->second : Held();
}
/* review fold 8 (2026-11-17): THE COPY THE INDEX NAMES IS NEVER MOVED AWAY OR OVERWRITTEN until a new index names its successor. A write
   whose record file landed and whose index did not leaves <key>.rec holding a copy NO index names (the named one is in <key>.rec.prev).
   The next write of that key may move the live file to .prev only when the live file IS the named copy (its length and CRC are the
   landed line's); otherwise the live file is replaced in place and .prev - the named copy - is kept. Content-based, so it holds after a
   restart too. p/n = the live file's bytes (it exists and was read). */
inline bool LiveIsIndexed(const Held& named, const char* p, size_t n)
{
    return named.have && n == (size_t)named.len && coopstore::Crc32(n == 0 ? "" : p, n) == named.crc;
}
/* the loop's memory after a failed write: put back to what the folder's index names (landedNow) - unless a later write of the same key
   was queued meanwhile (its receipt is in memory now, and it is judged by its own landing) */
inline void MemoryAfterFail(std::map<std::string, Held>* mem, const std::string& key, unsigned long long failedR, const Held& landedNow)
{
    std::map<std::string, Held>::iterator cur = mem->find(key);
    if (cur == mem->end() || cur->second.r != failedR) return;
    if (landedNow.have) cur->second = landedNow; else mem->erase(cur);
}

/* ---- the game's outbox: own.sent, "os1\t<key>\t<W>\t<R>\t<crc hex8>\n" - the W and R the world server last confirmed per key, and
   the confirmed bytes' CRC (what tells a changed text file) ---- */
struct SentRow { unsigned long long w, r; unsigned int crc; SentRow() : w(0), r(0), crc(0) {} };
/* the file's three answers (design section 2 "Missing vs unreadable"): no file = nothing confirmed yet (send everything); a file
   that cannot be read, or holds a line that does not parse = send NOTHING this session and say so */
enum { kSentNone = 0, kSentRead = 1, kSentUnreadable = 2 };
inline int SentParse(int probeKind /* coopstore::kFileAbsent / kFileUnreadable / kFileReadable */, const std::string& text, std::map<std::string, SentRow>* out)
{
    out->clear();
    if (probeKind == coopstore::kFileAbsent) return kSentNone;
    if (probeKind != coopstore::kFileReadable) return kSentUnreadable;
    std::string::size_type from = 0;
    while (from < text.size())
    {
        std::string::size_type nl = text.find('\n', from);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(from, nl - from);
        from = nl + 1;
        if (line.empty() || line == "\r") continue;
        std::vector<std::string> f; SplitTabs(line, &f);
        SentRow r;
        if (f.size() != 5 || f[0] != "os1" || !IsTravelKey(f[1]) || !coopown::ParseULL(f[2], &r.w) || !coopown::ParseULL(f[3], &r.r) || !ParseHex8(f[4], &r.crc))
        { out->clear(); return kSentUnreadable; }
        (*out)[f[1]] = r;
    }
    return kSentRead;
}
inline std::string SentText(const std::map<std::string, SentRow>& m)
{
    std::string all;
    for (std::map<std::string, SentRow>::const_iterator it = m.begin(); it != m.end(); ++it)
    {
        if (!IsTravelKey(it->first)) continue;
        char b[96]; std::sprintf(b, "\t%llu\t%llu\t%08x\n", it->second.w, it->second.r, it->second.crc);
        all += "os1\t" + it->first + b;
    }
    return all;
}
/* is this record unconfirmed? An indexed record: its committed W is above the confirmed one (or none is confirmed). A text file:
   its bytes' CRC differs from the confirmed copy's (or none is confirmed). */
inline bool Unconfirmed(const std::map<std::string, SentRow>& sent, const std::string& key, bool textKey, unsigned long long localW, unsigned int localCrc)
{
    std::map<std::string, SentRow>::const_iterator it = sent.find(key);
    if (it == sent.end()) return true;
    return textKey ? it->second.crc != localCrc : localW > it->second.w;
}
/* a text file's W: the store's write counter now, never below the W last confirmed for it (the counter is raised at every open to
   the highest number the store holds, so it does not go back; the max keeps a confirmed W from being undercut regardless) */
inline unsigned long long TextW(unsigned long long seqNow, const std::map<std::string, SentRow>& sent, const std::string& key)
{
    std::map<std::string, SentRow>::const_iterator it = sent.find(key);
    return (it != sent.end() && it->second.w > seqNow) ? it->second.w : seqNow;
}
/* what an OK does to the outbox: stored and duplicate confirm (key, W, R, the sent bytes' CRC); superseded and refused confirm
   nothing - the record is not sent again this session (a superseded one: the world holds a newer W; a refused one: counted and
   logged) and is judged again at the next start. Returns true when the row changed (own.sent is then rewritten). */
inline bool ApplyOk(std::map<std::string, SentRow>* sent, const Ok& ok, unsigned int sentCrc)
{
    if (ok.verdict != kStored && ok.verdict != kDuplicate) return false;
    SentRow& r = (*sent)[ok.key];
    if (r.w == ok.w && r.r == ok.r && r.crc == sentCrc) return false;
    r.w = ok.w; r.r = ok.r; r.crc = sentCrc;
    return true;
}

/* review folds 2 and 3 (2026-11-17): THE GAME'S COPIES IN FLIGHT. ONE copy per key on the wire at a time (a text file's two quick
   changes can share one W, so an OK told by W alone could confirm the wrong bytes; a newer copy WAITS until the one in flight is
   answered, then is sent if it still differs), and at most kWindow unanswered PUTs on a link (the next goes when an OK arrives; the
   re-send at WELCOME goes through the same window). high = the most ever unanswered at once (the REPORT line's window high-water). */
const size_t kWindow = 8;
struct Flight
{
    std::map<std::string, std::pair<unsigned long long, unsigned int> > inflight;   /* key -> (W, CRC) sent, its OK not yet here */
    std::set<std::string> waiting;                                                  /* keys with a newer copy held behind the one in flight */
    size_t high;
    Flight() : high(0) {}
};
enum { kMaySend = 0, kKeyBusy = 1, kWindowFull = 2 };
inline int MaySend(const Flight& f, const std::string& key)
{
    if (f.inflight.count(key) != 0) return kKeyBusy;
    if (f.inflight.size() >= kWindow) return kWindowFull;
    return kMaySend;
}
inline void FlightSent(Flight* f, const std::string& key, unsigned long long w, unsigned int crc)
{
    f->inflight[key] = std::make_pair(w, crc);
    if (f->inflight.size() > f->high) f->high = f->inflight.size();
}
/* an OK: true = the copy in flight for this key (its CRC in *crc), taken off the wire; *wake = a newer copy of the key waited behind it */
inline bool FlightAnswer(Flight* f, const std::string& key, unsigned long long w, unsigned int* crc, bool* wake)
{
    *crc = 0; *wake = false;
    std::map<std::string, std::pair<unsigned long long, unsigned int> >::iterator it = f->inflight.find(key);
    if (it == f->inflight.end() || it->second.first != w) return false;
    *crc = it->second.second;
    f->inflight.erase(it);
    *wake = f->waiting.erase(key) != 0;
    return true;
}
/* a new link (WELCOME): nothing sent on the old one is waiting any more; every unconfirmed key is judged again by the re-send */
inline void FlightNewLink(Flight* f) { f->inflight.clear(); f->waiting.clear(); }

/* ======================= T-885 PIECE 2 "Take" (design sections 3, 6, 8) ======================= */
/* INDEX's status: ok (the rows are what the folder's own.index names - the landed writes only), unreadable (the folder's index could
   not be read at the world server's start: nothing is offered and the game keeps its own copies), profile (the connection has no
   profile the world server can name a folder by) */
enum { kIdxOk = 0, kIdxUnreadable = 1, kIdxProfile = 2, kIdxNoAnswer = 3 /* the game's own: no INDEX came before the take was released - never on the wire */ };
inline const char* IdxStatusName(int s) { return s == kIdxOk ? "ok" : s == kIdxUnreadable ? "unreadable" : s == kIdxProfile ? "profile" : s == kIdxNoAnswer ? "noAnswer" : "?"; }
/* END's status: ok, malformed (the GET could not be read), refused (no profile to name a folder by, or its index unreadable) */
enum { kEndOk = 0, kEndMalformed = 1, kEndRefused = 2 };
inline const char* EndStatusName(int s) { return s == kEndOk ? "ok" : s == kEndMalformed ? "malformed" : s == kEndRefused ? "refused" : "?"; }
const size_t kIndexMaxRows = 8192;             /* far above a real profile (a few dozen keys); a larger count is malformed */
const size_t kGetMaxKeys = 512;                /* one GET's keys */
const unsigned int kGetBatchBytes = 262144u;   /* one GET asks for at most this many bytes of records (always at least one key) */

struct IndexAsk { unsigned int token; std::string store; IndexAsk() : token(0) {} };
struct IndexMsg { unsigned int token, status; std::map<std::string, Held> rows; IndexMsg() : token(0), status(kIdxOk) {} };
struct GetMsg { unsigned int token; std::vector<std::string> keys; GetMsg() : token(0) {} };
struct RecMsg { unsigned int token; std::string key; bool found; Held h; std::vector<char> bytes; RecMsg() : token(0), found(false) {} };
struct EndMsg { unsigned int token, asked, found, status; EndMsg() : token(0), asked(0), found(0), status(kEndOk) {} };

inline unsigned int KindOf(const char* p, size_t n) { return (p != 0 && n > 0) ? (unsigned int)(unsigned char)p[0] : 0u; }

inline bool EncodeIndexAsk(const IndexAsk& m, std::vector<char>* b)
{
    b->clear(); PutU8(b, kIndexAsk); PutU32(b, m.token);
    return PutStr(b, m.store);
}
inline bool DecodeIndexAsk(const char* p, size_t n, IndexAsk* m)
{
    Cur c; CurInit(&c, p, n);
    if (GetU8(&c) != kIndexAsk || c.bad) return false;
    m->token = GetU32(&c); GetStr(&c, &m->store);
    return !c.bad && c.at == c.n;
}
inline bool EncodeIndex(const IndexMsg& m, std::vector<char>* b)
{
    b->clear(); PutU8(b, kIndex); PutU32(b, m.token); PutU8(b, m.status);
    if (m.rows.size() > kIndexMaxRows) return false;
    PutU32(b, (unsigned int)m.rows.size());
    for (std::map<std::string, Held>::const_iterator it = m.rows.begin(); it != m.rows.end(); ++it)
    {
        if (!PutStr(b, it->first)) return false;
        PutU64(b, it->second.w); PutU64(b, it->second.r); PutU32(b, it->second.len); PutU32(b, it->second.crc); PutU32(b, it->second.people); PutU64(b, it->second.at);
    }
    return true;
}
/* false = malformed: a wrong kind, a row count over kIndexMaxRows, a key that does not travel, the same key twice, a byte short or
   more, rows with a status other than ok */
inline bool DecodeIndex(const char* p, size_t n, IndexMsg* m)
{
    Cur c; CurInit(&c, p, n);
    if (GetU8(&c) != kIndex || c.bad) return false;
    m->token = GetU32(&c); m->status = GetU8(&c);
    const unsigned int rows = GetU32(&c);
    if (c.bad || rows > kIndexMaxRows || m->status > (unsigned int)kIdxProfile) return false;
    m->rows.clear();
    for (unsigned int i = 0; i < rows; ++i)
    {
        std::string k; Held h; h.have = true;
        GetStr(&c, &k); h.w = GetU64(&c); h.r = GetU64(&c); h.len = GetU32(&c); h.crc = GetU32(&c); h.people = GetU32(&c); h.at = GetU64(&c);
        if (c.bad || !IsTravelKey(k) || m->rows.count(k) != 0) return false;
        m->rows[k] = h;
    }
    return c.at == c.n && (m->status == (unsigned int)kIdxOk || m->rows.empty());
}
inline bool EncodeGet(const GetMsg& m, std::vector<char>* b)
{
    b->clear(); PutU8(b, kGet); PutU32(b, m.token);
    if (m.keys.empty() || m.keys.size() > kGetMaxKeys) return false;
    PutU32(b, (unsigned int)m.keys.size());
    for (size_t i = 0; i < m.keys.size(); ++i) if (!PutStr(b, m.keys[i])) return false;
    return true;
}
inline bool DecodeGet(const char* p, size_t n, GetMsg* m)
{
    Cur c; CurInit(&c, p, n);
    if (GetU8(&c) != kGet || c.bad) return false;
    m->token = GetU32(&c);
    const unsigned int k = GetU32(&c);
    if (c.bad || k == 0 || k > kGetMaxKeys) return false;
    m->keys.clear();
    for (unsigned int i = 0; i < k; ++i) { std::string s; GetStr(&c, &s); if (c.bad || !IsTravelKey(s)) return false; m->keys.push_back(s); }
    return c.at == c.n;
}
inline bool EncodeRec(const RecMsg& m, std::vector<char>* b)
{
    b->clear(); PutU8(b, kRec); PutU32(b, m.token);
    if (!PutStr(b, m.key)) return false;
    if (!m.found && !m.bytes.empty()) return false;
    PutU8(b, m.found ? 1u : 0u); PutU64(b, m.h.w); PutU64(b, m.h.r); PutU32(b, m.h.crc); PutU32(b, m.h.people); PutU64(b, m.h.at);
    PutU32(b, (unsigned int)m.bytes.size());
    b->insert(b->end(), m.bytes.begin(), m.bytes.end());
    return true;
}
inline bool DecodeRec(const char* p, size_t n, RecMsg* m)
{
    Cur c; CurInit(&c, p, n);
    if (GetU8(&c) != kRec || c.bad) return false;
    m->token = GetU32(&c); GetStr(&c, &m->key);
    const unsigned int f = GetU8(&c);
    m->h = Held(); m->h.w = GetU64(&c); m->h.r = GetU64(&c); m->h.crc = GetU32(&c); m->h.people = GetU32(&c); m->h.at = GetU64(&c);
    const unsigned int len = GetU32(&c);
    if (c.bad || f > 1 || !IsTravelKey(m->key) || len > kMaxRecord || c.n - c.at != (size_t)len || (f == 0 && len != 0)) return false;
    m->found = f == 1; m->h.have = m->found; m->h.len = len;
    m->bytes.assign(p + c.at, p + c.n);
    return true;
}
inline bool EncodeEnd(const EndMsg& m, std::vector<char>* b) { b->clear(); PutU8(b, kEnd); PutU32(b, m.token); PutU32(b, m.asked); PutU32(b, m.found); PutU8(b, m.status); return true; }
inline bool DecodeEnd(const char* p, size_t n, EndMsg* m)
{
    Cur c; CurInit(&c, p, n);
    if (GetU8(&c) != kEnd || c.bad) return false;
    m->token = GetU32(&c); m->asked = GetU32(&c); m->found = GetU32(&c); m->status = GetU8(&c);
    return !c.bad && c.at == c.n && m->found <= m->asked && m->status <= (unsigned int)kEndRefused;
}

/* ---- the world server: which file holds the copy the index names (review fold 8's rule, read side). A write whose record file
   landed and whose index did not leaves <key>.rec holding a copy no index names, and the named copy in <key>.rec.prev; so the live
   file is served only when it IS the named copy (length and CRC), else .prev when it is, else nothing (answered not found, counted;
   the game keeps its own copy). 0 = live, 1 = prev, -1 = neither. ---- */
inline int NamedCopyOf(const Held& named, bool liveRead, const std::vector<char>& live, bool prevRead, const std::vector<char>& prev)
{
    if (!named.have) return -1;
    if (liveRead && LiveIsIndexed(named, live.empty() ? "" : &live[0], live.size())) return 0;
    if (prevRead && LiveIsIndexed(named, prev.empty() ? "" : &prev[0], prev.size())) return 1;
    return -1;
}

/* ---- THE GAME'S DECISION PER KEY AT THE JOIN (design section 3, step 3; owner 1745 a / 1753) ----
   local: the game's committed copy of the key (have, W, CRC); srv: the world's INDEX row; sentRow: the folder's own.sent row for the
   key BEFORE the INDEX corrects it (0 = none).
   - the world holds nothing for the key: kTakeLocalOnly (an unconfirmed record - the outbox sends it).
   - no local copy: kTakeGet.
   - an indexed record: local W above the world's = kTakeLocalNewer (an unconfirmed write: kept and sent, it wins - 1745 a); the same
     W and bytes = kTakeSame; anything else (a lower W - an older folder put back -, or the same W with other bytes, which one game
     cannot make) = kTakeGet: the world's copy is the truth (1753).
   - a text file (sent whole; its W is the store's counter when it was sent, so W does not order two copies): the same bytes =
     kTakeSame; other bytes are a local change only when the folder's own.sent says the world's copy is the one this folder last had
     confirmed (sentRow's CRC is the world's) - then kTakeLocalNewer; otherwise this folder never had the world's copy confirmed (an
     older folder, or one that lost its own.sent) and the world's copy wins: kTakeGet. */
/* T-885 piece 2 review fold (finding 1): an INDEXED record is judged by the store's own committed index entry (W, CRC) - its file is not the deciding fact.
   file says only whether the file is that copy now (1 yes, 0 absent or not the indexed copy - a crash mid-write, -1 could not be read just
   now - an antivirus lock): the same W and CRC with file 0 = kTakeGet (the world holds that very write: the file is restored, nothing
   rolls back); with file -1 the index decides alone. A TEXT file IS its local state: one that cannot be read now = kTakeKeepUnread
   (its local state is not settled: the take refuses the load - TakeRefuses - and the next load judges it again).
   Fold round 2 (re-check fix 1 minor): 'file' is the INDEXED copy, read from <key>.rec or, as the overlay's loader does, from
   <key>.rec.prev (ownupload.cpp LocalOf). A local W above the world's: file 1 or -1 = kTakeLocalNewer (the outbox sends it - a locked
   file is tried again every second until it reads); file 0 = the indexed copy is in neither file, so it exists nowhere on this PC and
   cannot be sent or loaded: kTakeGet - the world's copy is the newest that still exists, and the overlay's save-time rule (the record's
   own write time) still lets a newer Kenshi save win over it. */
enum { kTakeSame = 0, kTakeGet = 1, kTakeLocalNewer = 2, kTakeLocalOnly = 3, kTakeKeepUnread = 4 };
struct LocalVer { bool have; unsigned long long w; unsigned int crc; int file; LocalVer() : have(false), w(0), crc(0), file(1) {} };
inline int TakeDecide(const LocalVer& local, const Held& srv, bool textKey, const SentRow* sentRow)
{
    if (!srv.have) return kTakeLocalOnly;
    if (!local.have) return kTakeGet;
    if (textKey)
    {
        if (local.file < 0) return kTakeKeepUnread;
        if (local.crc == srv.crc) return kTakeSame;
        return (sentRow != 0 && sentRow->crc == srv.crc) ? kTakeLocalNewer : kTakeGet;
    }
    if (local.w > srv.w) return local.file == 0 ? kTakeGet : kTakeLocalNewer;
    if (local.w == srv.w && local.crc == srv.crc) return local.file == 0 ? kTakeGet : kTakeSame;
    return kTakeGet;
}
inline const char* TakeName(int t) { return t == kTakeSame ? "same" : t == kTakeGet ? "get" : t == kTakeLocalNewer ? "localNewer" : t == kTakeLocalOnly ? "localOnly" : t == kTakeKeepUnread ? "keepUnread" : "?"; }
/* the whole plan: every key either side holds, judged once. get = the keys to fetch (in key order), newer = local newer, only = local
   only (the world holds none), same = equal. */
struct TakePlan { std::vector<std::string> get, newer, only, unread; size_t same; TakePlan() : same(0) {} };
inline void PlanTake(const std::map<std::string, LocalVer>& local, const std::map<std::string, Held>& srv, const std::map<std::string, SentRow>& sentBefore, TakePlan* out)
{
    *out = TakePlan();
    std::set<std::string> keys;
    for (std::map<std::string, LocalVer>::const_iterator it = local.begin(); it != local.end(); ++it) if (it->second.have) keys.insert(it->first);
    for (std::map<std::string, Held>::const_iterator it = srv.begin(); it != srv.end(); ++it) keys.insert(it->first);
    for (std::set<std::string>::const_iterator k = keys.begin(); k != keys.end(); ++k)
    {
        std::map<std::string, LocalVer>::const_iterator l = local.find(*k);
        std::map<std::string, Held>::const_iterator s = srv.find(*k);
        std::map<std::string, SentRow>::const_iterator r = sentBefore.find(*k);
        const int t = TakeDecide(l != local.end() ? l->second : LocalVer(), s != srv.end() ? s->second : Held(), IsTextKey(*k), r != sentBefore.end() ? &r->second : 0);
        if (t == kTakeGet) out->get.push_back(*k); else if (t == kTakeLocalNewer) out->newer.push_back(*k); else if (t == kTakeLocalOnly) out->only.push_back(*k); else if (t == kTakeKeepUnread) out->unread.push_back(*k); else ++out->same;
    }
}
/* the next GET: from want[from], keys until their bytes (the INDEX's lengths) would pass kGetBatchBytes, or kGetMaxKeys - always at
   least one. Returns the index after the batch. */
inline size_t NextBatch(const std::vector<std::string>& want, size_t from, const std::map<std::string, Held>& srv, std::vector<std::string>* batch)
{
    batch->clear();
    unsigned long long bytes = 0;
    size_t i = from;
    for (; i < want.size() && batch->size() < kGetMaxKeys; ++i)
    {
        std::map<std::string, Held>::const_iterator s = srv.find(want[i]);
        const unsigned long long len = s != srv.end() ? s->second.len : 0ULL;
        if (!batch->empty() && bytes + len > kGetBatchBytes) break;
        batch->push_back(want[i]); bytes += len;
    }
    return i;
}
/* a REC is taken only when it is the copy the INDEX named (W, length, CRC), its bytes carry that CRC, and the game's own reader for
   its kind reads it to its end (the class "a stored copy trusted from its checksum alone"). kRecTake = take, else why not. */
enum { kRecTake = 0, kRecNotFound = 1, kRecNotNamed = 2, kRecCrc = 3, kRecUnreadable = 4 };
inline const char* RecWhyName(int w) { return w == kRecTake ? "take" : w == kRecNotFound ? "notFound" : w == kRecNotNamed ? "notTheIndexedCopy" : w == kRecCrc ? "crc" : w == kRecUnreadable ? "unreadable" : "?"; }
inline int RecAccept(const Held& named, const RecMsg& r)
{
    if (!r.found) return kRecNotFound;
    if (!named.have || r.h.w != named.w || r.h.crc != named.crc || r.bytes.size() != (size_t)named.len) return kRecNotNamed;
    if (coopstore::Crc32(r.bytes.empty() ? "" : &r.bytes[0], r.bytes.size()) != named.crc) return kRecCrc;
    if (!ContentReadable(r.key, r.bytes.empty() ? "" : &r.bytes[0], r.bytes.size())) return kRecUnreadable;
    return kRecTake;
}
/* OWN.SENT CORRECTED BY THE INDEX (design section 8, "a cache fed only by received messages"): after the take, own.sent says exactly
   what the world holds - per key its W, R and CRC. A key fetched or equal is then confirmed; a local-newer key is unconfirmed against
   the world's row (sent); a key the world does not hold has no row (sent); a row for a key the world no longer holds is dropped.
   Returns how many rows changed (added, changed or dropped). */
inline size_t SentFromIndex(const std::map<std::string, Held>& srv, std::map<std::string, SentRow>* sent)
{
    std::map<std::string, SentRow> next;
    for (std::map<std::string, Held>::const_iterator it = srv.begin(); it != srv.end(); ++it)
    {
        SentRow r; r.w = it->second.w; r.r = it->second.r; r.crc = it->second.crc;
        next[it->first] = r;
    }
    size_t changed = 0;
    for (std::map<std::string, SentRow>::const_iterator it = next.begin(); it != next.end(); ++it)
    {
        std::map<std::string, SentRow>::const_iterator o = sent->find(it->first);
        if (o == sent->end() || o->second.w != it->second.w || o->second.r != it->second.r || o->second.crc != it->second.crc) ++changed;
    }
    for (std::map<std::string, SentRow>::const_iterator it = sent->begin(); it != sent->end(); ++it) if (next.count(it->first) == 0) ++changed;
    sent->swap(next);
    return changed;
}
/* the write counter after the take: at least the world's highest W (design section 3, step 5), never lowered */
inline unsigned long long MaxW(const std::map<std::string, Held>& m)
{
    unsigned long long t = 0;
    for (std::map<std::string, Held>::const_iterator it = m.begin(); it != m.end(); ++it) if (it->second.w > t) t = it->second.w;
    return t;
}
inline unsigned long long CounterAfterTake(unsigned long long seqNow, const std::map<std::string, Held>& srv) { const unsigned long long m = MaxW(srv); return m > seqNow ? m : seqNow; }
/* ======================= T-885 piece 2 REVIEW FOLD (2026-11-17) ======================= */
/* FOLD ROUND 2 (2026-11-17, manager's decision): FAIL CLOSED. When the take ends with an INDEX of status ok and any key it should have
   taken is not taken - a key the world holds newer (or the PC lacks) not fetched in kTakeFetchRounds rounds, not committed, or left when
   the progress timeout gave up, or a text key whose file could not be read (its local state not settled) - the LOAD IS REFUSED with the
   existing older-records words, own.sent is left as it was, and nothing goes up for that store until a later take of it ends clean (the
   player's next load takes again). Loading old copies instead would let the first re-save, numbered above the world's highest W, overwrite
   the world's newer copy (re-check finding). No INDEX (no answer, status not ok) = nothing known to be newer: the load goes with this PC's
   copies and the older-records check as before. This replaces the first fold's bars (TakeBar / SentKeepRows) and its guard switch
   (TakeReplacesGuard / restoreguard::TakeCoversGuard), which did no extra work once a take that is not complete refuses. */
inline bool TakeRefuses(int status, size_t notTaken) { return status == kIdxOk && notTaken > 0; }
/* a key not taken is asked again before the load is released: kTakeFetchRounds rounds in all (roundsDone = rounds finished) */
const unsigned int kTakeFetchRounds = 3;
inline bool TakeFetchAgain(size_t failed, unsigned int roundsDone) { return failed > 0 && roundsDone < kTakeFetchRounds; }
/* FINDING 4 - THE PROGRESS TIMEOUT. While an answer is awaited (INDEX, or a GET's RECs / END), no answer for kTakeStallMs begins the take
   again with a new exchange number (answers to the old one are dropped as stale), at most kTakeRestarts times; then the load is released
   with this PC's own copies and the older-records check as before. 60 s: every answer counts as progress, and the largest single answer is
   one REC (at most 1 MB) or an INDEX (a few KB for a real profile) - seconds on any link; the rest is the world server's writer queue ahead
   of the read. Three tries of 60 s = 180 s at worst, the records-before-load exchange's own backstop (cooppre::kPreStallMs). The same
   bound covers the records writer committing the fetched records (past it, the ones not committed count as not taken). */
const unsigned int kTakeStallMs = 60000;
const unsigned int kTakeRestarts = 2;
enum { kTakeRestart = 1, kTakeRelease = 2 };
inline bool TakeStalled(unsigned int sinceProgressMs) { return sinceProgressMs >= kTakeStallMs; }
/* Fold round 2 (re-check): the world server answers a take only after every write queued before it, so a busy but working server can be
   quiet on the take for longer than kTakeStallMs; ANY message received on the link (heardAt, PumpLink) counts as progress, not only the
   take's answers. The quiet time = the shorter of the two (tick counts, wrap-safe); a message heard before the take began counts nothing
   (answerAt starts at the take's begin). The restart cap is unchanged. */
inline unsigned int TakeQuietMs(unsigned int now, unsigned int answerAt, unsigned int heardAt)
{
    const unsigned int a = now - answerAt, h = now - heardAt;
    return a < h ? a : h;
}
inline int TakeTroubleStep(unsigned int restartsDone) { return restartsDone < kTakeRestarts ? kTakeRestart : kTakeRelease; }
/* the world server: the exchange number of an ask it cannot read (its bytes 1-4, little-endian), so even a malformed GET gets its END */
inline unsigned int PeekToken(const char* p, size_t n)
{
    if (p == 0 || n < 5) return 0u;
    return (unsigned int)(unsigned char)p[1] | ((unsigned int)(unsigned char)p[2] << 8) | ((unsigned int)(unsigned char)p[3] << 16) | ((unsigned int)(unsigned char)p[4] << 24);
}
/* FINDING 3 (b) - THE WORLD SERVER'S DUPLICATE is answered only once the matching write has LANDED (the same rule as OK stored): judged on
   the writer thread after every write queued before it. landedNow = what the folder's index names for the key then. The matching write
   landed = duplicate; it failed (the index names an older copy, or none) = refused (write), so the game confirms nothing and re-judges. */
inline int DupAfterLanding(const Held& landedNow, unsigned long long w, unsigned int len, unsigned int crc, int* why)
{
    *why = kWhyNone;
    if (landedNow.have && landedNow.w == w && landedNow.len == len && landedNow.crc == crc) return kDuplicate;
    *why = kWhyWrite; return kRefused;
}

/* the log cap: the first `first` lines, then every 100th */
inline bool LogThis(long long n, long long first) { return n <= first || n % 100 == 0; }

}   /* namespace ownrecw */

#endif
