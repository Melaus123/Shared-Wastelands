/* ownrec.h - mmo1 (e47-mmo-design.md sections 1, 2, 5 and 7 item 1). THE OWN-SQUAD RECORD WRITER'S PURE PARTS.
 *
 * Everything here is a decision or an encoding with no clock, no disk, no engine and no global, so the offline suite
 * (src/coop-test) sweeps the same code the plugin compiles:
 *   - the file names  (one file per squad: pp.squad.<name>.rec, its temp and its kept previous version, the index);
 *   - the index line  (o1 <key> <seq> <len> <crc> <writtenAt> <chars>, tab-separated, one per squad);
 *   - the write decision (a record whose bytes CRC-match the last committed one is not written - skippedSame);
 *   - the commit verdict (the written file is read back; length + CRC must match or it is verifyFail);
 *   - the heartbeat pacing (the 30 s cursor, the 2 s dirty gap, the 16-per-second cap, the slow-squad 5x rule).
 *
 * THE INDEX LINE IS NOT the notebook's STORE-FILE v7 line (storemeta.h). v7 carries owner/position/faction/world
 * fields that have no meaning for a player's own record and whose validators (MetaOwnerFieldValid, the world id)
 * would refuse or invent them. It carries the three things v7's commit rule needs - length, CRC-32, sequence - so a
 * later converter (mmo9, the server backend) is a field copy. Named in the mmo1 report as a deviation from section 1.
 *
 * C++03. Header-only (inline) so it needs no build-line change in either program. CRC-32 comes from storemeta.cpp,
 * which both programs already link.
 */
#ifndef COOP_OWNREC_H
#define COOP_OWNREC_H

#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>   /* mmo1c: memcpy for PodPutBytes */
#include <algorithm>   /* mmo2: std::sort for a character's hand keys */
#include <set>         /* mmo2: the roster check's after-load roster */
#include <map>         /* mmo2 fold: the roster's per-character count, a record's hands by prefix */
#include "storemeta.h"
#include "queuejournal.h"   /* mmo6: the ledger line is the outage journal's q1 shape */

namespace coopown {

/* THE NUMBERS, in one place. */
const unsigned int kPeriodMs     = 30000;   /* the heartbeat: every owned squad is visited once per 30 s */
const unsigned int kDirtyGapMs   = 2000;    /* a squad an event marked is written at most once per 2 s per key */
const unsigned int kMaxPerSec    = 16;      /* at most 16 squad serialises in any one second, whatever the clock */
const unsigned int kSlowUs       = 4000;    /* a squad whose serialise took longer than 4 ms is a SLOW squad ... */
const unsigned int kSlowMul      = 5;       /* ... and its heartbeat runs at 5x the period (design 5.2) */
const unsigned int kKeyPartMax   = 64;      /* the longest squad-name part of a file name */

/* ---- names ------------------------------------------------------------------------------------------------ */

inline std::string Hex8(unsigned int v)
{
    char b[16]; std::sprintf(b, "%08x", v); return std::string(b);
}
/* T-251: THE PLAYER'S RECORDS STORE NAME - one store per world + profile. Made from the same two things that pick the
   profile's save folder (its key file's world key and profile id - SaveFolderScanChoose), not from the folder's shown name,
   which the owner-168 rename can change: "mp-" + the world key LOWER-CASED (fold item 5) with every character outside [A-Za-z0-9_-] as '_' (at most 30
   of them) + "-" + 8 hex digits of FNV-1a over "<world key>\t<profile id>". 42 characters at most - always a store name the
   wire accepts (restoreguard::OwnStoreNameOk). "" = no world key or no profile. T-490: with a world id the hash is over
   "<world key>\t<profile id>\t<world id>", so two worlds of one name keep apart stores; no id = the name above. */
inline std::string PlayerStoreName(const std::string& worldKey, const std::string& profileId, const std::string& worldId = std::string())
{
    if (worldKey.empty() || profileId.empty()) return std::string();
    std::string part;
    for (std::string::size_type i = 0; i < worldKey.size() && part.size() < 30; ++i)
    {
        const char c = worldKey[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        part += ok ? ((c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c) : '_';   /* T-251 fold item 5: lower case - store folders, like save folders, ignore case */
    }
    const std::string all = worldKey + "\t" + profileId + (worldId.empty() ? std::string() : "\t" + worldId);
    unsigned int h = 2166136261u;
    for (std::string::size_type i = 0; i < all.size(); ++i) { h ^= (unsigned char)all[i]; h *= 16777619u; }
    return "mp-" + part + "-" + Hex8(h);
}
/* Every records store name for these world spellings and these profile ids (PlayerStoreName of each pair), each once and never "" -
   the stores a deleted world leaves on this computer (coopprof::WorldLeftoversFind gives the two lists). */
inline std::vector<std::string> PlayerStoreNamesFor(const std::vector<std::string>& worldKeys, const std::vector<std::string>& profileIds,
                                                    const std::string& worldId = std::string())
{
    std::vector<std::string> out;
    for (size_t w = 0; w < worldKeys.size(); ++w)
        for (size_t p = 0; p < profileIds.size(); ++p)
        {
            const std::string s = PlayerStoreName(worldKeys[w], profileIds[p], worldId);
            if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
        }
    return out;
}
/* T-251: OPEN THE PLAYER'S STORE NOW? The cfg lever on (the default), the profile's save folder decided (SaveRedirectWanted:
   multiplayer, a picked profile, its folder), nothing holding the records off (this store refused, a repaired world's restart,
   a writer fault), a name to open - and either no store open yet, or the open store is one this opened by itself for another
   world or profile. A store a test chose with `ownsave <name>` is never replaced.
   T-251 fold item 1: NO LIVE WORLD IS NEEDED. The store opens at the title as soon as the folder is decided (the WELCOME that
   admits the pick) - BEFORE the automatic load, whose overlay step (OwnOverlayPrepare) lays the records over the save it
   loads. Only WRITING needs the live world (OwnTick's OwnWorldLive). */
inline bool AutoStoreOpenWanted(bool cfgOn, bool folderDecided, bool heldOff, const std::string& want,
                                const std::string& current, bool currentIsAuto)
{
    if (!cfgOn || !folderDecided || heldOff || want.empty()) return false;
    if (current.empty()) return true;
    return currentIsAuto && current != want;
}
/* T-251 fold item 3: CLOSE THE PLAYER'S STORE? It was opened by itself, no folder is decided any more (the game left the
   multiplayer world, or its profile is gone), and nothing it owes is still in flight (the writer queue and the leave
   checkpoint drained, no leave save running). A test's store is never closed here. */
inline bool AutoStoreCloseWanted(bool currentIsAuto, const std::string& current, const std::string& want, bool writerIdle, bool leaveIdle)
{
    return currentIsAuto && !current.empty() && want.empty() && writerIdle && leaveIdle;
}
/* T-251 fold 2 (re-check item 1): THE LOAD-TIME HOLD. When the open at the load (OwnAutoStoreBeforeLoad) fails while a profile
   folder is decided, that load goes on with the save as it is - so the store must NOT open later in that world (its writer
   would lay the loaded, OLDER state over the newer records). The failed store's name is held; while anything is held the
   automatic open (OwnAutoStoreTick) opens nothing. The hold ends when the next load begins (the load-time open is tried
   again there) or when no folder is decided any more (the game left the world - back to the title). */
inline std::string AutoStoreHoldAfterLoadOpen(bool tried, bool opened, const std::string& want)
{
    return (tried && !opened) ? want : std::string();
}
inline bool AutoStoreLoadHeld(const std::string& held)
{
    return !held.empty();
}
inline std::string AutoStoreHoldKept(const std::string& held, const std::string& want, bool loadBegins)
{
    return (loadBegins || want.empty()) ? std::string() : held;
}
/* T-251 fold item 3: multiplayer.mark is written only for a save made in a multiplayer world with a records store open. */
inline bool MarkWanted(bool singlePlayer, const std::string& store)
{
    return !singlePlayer && !store.empty();
}

/* THE KEY OF ONE SQUAD'S RECORD. The squad's engine id (a platoon sid such as "Holy Nation Outlaws_28") may hold spaces
   and characters a file name must not; every character outside [A-Za-z0-9_-] becomes '_' and the part is cut at
   kKeyPartMax. When ANYTHING was changed, ".<crc32 of the raw id>" is appended, so two ids that clean to the same text
   ("a b" / "a_b") never share a file. An empty id is refused (returns ""). */
inline std::string SquadKey(const std::string& sid)
{
    if (sid.empty()) return std::string();
    std::string part; bool changed = false;
    for (std::string::size_type i = 0; i < sid.size(); ++i)
    {
        const char c = sid[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (part.size() >= kKeyPartMax) { changed = true; break; }
        part += ok ? c : '_';
        if (!ok) changed = true;
    }
    std::string key = "pp.squad." + part;
    if (changed) key += "." + Hex8(coopstore::Crc32(sid.data(), sid.size()));
    return key;
}

inline std::string RecFile(const std::string& key)  { return key + ".rec"; }
inline std::string PrevFile(const std::string& key) { return key + ".rec.prev"; }
inline std::string TmpFile(const std::string& key, unsigned long pid)
{
    char b[24]; std::sprintf(b, "%lu", pid); return key + ".rec." + b + ".tmp";
}
inline const char* IndexFile() { return "own.index"; }

/* ---- the index line --------------------------------------------------------------------------------------- */

struct IndexLine
{
    std::string        key;
    unsigned long long seq;        /* ownSeq: +1 per committed record, never reused */
    long long          len;        /* bytes of <key>.rec */
    unsigned int       crc;        /* CRC-32 (storemeta's) of those bytes */
    long long          writtenAt;  /* unix seconds, this PC's clock */
    int                chars;      /* characters in the squad when it was written */
    bool               stamped;    /* restore1c: o2 - the repair epoch and the notebook seq this game had seen at the write */
    unsigned int       epoch;
    unsigned long long nbSeq;
    IndexLine() : seq(0), len(0), crc(0), writtenAt(0), chars(0), stamped(false), epoch(0), nbSeq(0) {}
};

/* "o1\t<key>\t<seq>\t<len>\t<crc hex8>\t<writtenAt>\t<chars>\n". Refuses (false, *out untouched) a key that is empty
   or carries a TAB, CR, LF or space - a key comes from SquadKey, which cannot produce one, so a refusal is a defect. */
inline bool EncodeIndexLine(const IndexLine& l, std::string* out)
{
    if (l.key.empty() || l.len < 0) return false;
    for (std::string::size_type i = 0; i < l.key.size(); ++i)
    {
        const char c = l.key[i];
        if (c == '\t' || c == '\r' || c == '\n' || c == ' ') return false;
    }
    char b[160];
    std::sprintf(b, "\t%llu\t%lld\t%08x\t%lld\t%d\n", l.seq, l.len, l.crc, l.writtenAt, l.chars);
    if (l.stamped)   /* restore1c: o2 = o1 + {epoch, nbSeq}; an unstamped line stays o1 */
    {
        char e[64]; std::sprintf(e, "%u\t%llu\n", l.epoch, l.nbSeq);
        std::string body(b); body.erase(body.size() - 1); body += "\t"; body += e;
        *out = "o2\t" + l.key + body;
        return true;
    }
    *out = "o1\t" + l.key + b;
    return true;
}

inline bool ParseULL(const std::string& s, unsigned long long* v)
{
    if (s.empty() || s.size() > 20) return false;
    unsigned long long r = 0;
    for (std::string::size_type i = 0; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9') return false;
        r = r * 10ULL + (unsigned long long)(s[i] - '0');
    }
    *v = r; return true;
}

/* The inverse. A trailing CR/LF is tolerated. false for anything that is not exactly seven fields of the right kind. */
inline bool ParseIndexLine(const std::string& lineIn, IndexLine* out)
{
    std::string line = lineIn;
    while (!line.empty() && (line[line.size() - 1] == '\n' || line[line.size() - 1] == '\r')) line.erase(line.size() - 1);
    std::vector<std::string> f;
    std::string::size_type at = 0;
    for (;;)
    {
        const std::string::size_type t = line.find('\t', at);
        if (t == std::string::npos) { f.push_back(line.substr(at)); break; }
        f.push_back(line.substr(at, t - at)); at = t + 1;
    }
    if (!((f.size() == 7 && f[0] == "o1") || (f.size() == 9 && f[0] == "o2")) || f[1].empty()) return false;   /* restore1c: o2 */
    IndexLine l; unsigned long long u = 0;
    l.key = f[1];
    if (!ParseULL(f[2], &l.seq)) return false;
    if (!ParseULL(f[3], &u)) return false; l.len = (long long)u;
    if (f[4].size() != 8) return false;
    unsigned int crc = 0;
    for (int i = 0; i < 8; ++i)
    {
        const char c = f[4][i]; unsigned int d;
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
        else return false;
        crc = (crc << 4) | d;
    }
    l.crc = crc;
    if (!ParseULL(f[5], &u)) return false; l.writtenAt = (long long)u;
    if (!ParseULL(f[6], &u) || u > 100000ULL) return false; l.chars = (int)u;
    if (f.size() == 9)   /* restore1c */
    {
        if (!ParseULL(f[7], &u) || u > 0xFFFFFFFFULL || !ParseULL(f[8], &l.nbSeq)) return false;
        l.epoch = (unsigned int)u; l.stamped = true;
    }
    *out = l; return true;
}

/* mmo2 fold (review-mmo2 item 5): THE STORE'S IDENTITY - the first line of own.index, "oh1\t<world key>\t<profile id>\n"
   (the profile may be empty). The overlay refuses the whole store unless both match the loading game's. */
inline bool IdentityFieldOk(const std::string& s)
{
    for (std::string::size_type i = 0; i < s.size(); ++i)
        if (s[i] == '\t' || s[i] == '\r' || s[i] == '\n') return false;
    return true;
}
inline bool IsIndexHeader(const std::string& line) { return line.compare(0, 4, "oh1\t") == 0; }
inline bool EncodeIndexHeader(const std::string& world, const std::string& profile, std::string* out)
{
    if (world.empty() || !IdentityFieldOk(world) || !IdentityFieldOk(profile)) return false;
    *out = "oh1\t" + world + "\t" + profile + "\n";
    return true;
}
inline bool ParseIndexHeader(const std::string& lineIn, std::string* world, std::string* profile)
{
    std::string line = lineIn;
    while (!line.empty() && (line[line.size() - 1] == '\n' || line[line.size() - 1] == '\r')) line.erase(line.size() - 1);
    if (!IsIndexHeader(line)) return false;
    const std::string rest = line.substr(4);
    const std::string::size_type tb = rest.find('\t');
    if (tb == std::string::npos || tb == 0 || rest.find('\t', tb + 1) != std::string::npos) return false;
    *world = rest.substr(0, tb); *profile = rest.substr(tb + 1);
    return true;
}
/* both identities known and equal (an empty world key is never a match) */
inline bool IdentityMatches(const std::string& aw, const std::string& ap, const std::string& bw, const std::string& bp)
{
    return !aw.empty() && aw == bw && ap == bp;
}

/* THE SEQUENCE COUNTER SURVIVES A CRASH WITHOUT A FILE OF ITS OWN: at open it is the highest seq in the index. Lines
   that do not parse are counted into *bad (may be 0) and skipped. */
inline unsigned long long MaxSeqOf(const std::vector<std::string>& lines, int* bad)
{
    unsigned long long m = 0;
    for (std::vector<std::string>::size_type i = 0; i < lines.size(); ++i)
    {
        if (lines[i].empty() || lines[i] == "\r") continue;
        if (IsIndexHeader(lines[i])) continue;   /* mmo2 fold: the identity line is not a record line */
        IndexLine l;
        if (!ParseIndexLine(lines[i], &l)) { if (bad) ++*bad; continue; }
        if (l.seq > m) m = l.seq;
    }
    return m;
}

/* ---- the atomic-write decisions ---------------------------------------------------------------------------- */

enum { kSkipSame = 0, kWrite = 1 };
/* A record whose fresh bytes match the last COMMITTED record (length and CRC) is not written, unless `force` (the TEST
   poke). A squad never committed in this process (haveLast false) is always written. */
inline int WriteDecision(bool force, bool haveLast, unsigned int lastCrc, long long lastLen, unsigned int crc, long long len)
{
    if (force || !haveLast) return kWrite;
    return (crc == lastCrc && len == lastLen) ? kSkipSame : kWrite;
}

enum { kCommitOk = 0, kCommitWriteFail = 1, kCommitVerifyFail = 2 };
/* After temp write + rename, the committed file is READ BACK. The index line (the commit point) is written only on
   kCommitOk. A file that could not be written or renamed is a write failure; one that reads back with the wrong
   length or CRC is a verify failure - the previous version (.rec.prev) and the old index line stay authoritative. */
inline int CommitVerdict(bool written, bool readBack, long long readLen, unsigned int readCrc, long long wantLen, unsigned int wantCrc)
{
    if (!written) return kCommitWriteFail;
    if (!readBack || readLen != wantLen || readCrc != wantCrc) return kCommitVerifyFail;
    return kCommitOk;
}

/* mmo1b (review-mmo1 item 3): the live <key>.rec is moved onto <key>.rec.prev ONLY when it is the version the index
   names (it read back, and its length and CRC match that squad's index line). Anything else - no index line yet, or a
   file left behind by a failed verify/index step - is simply replaced by the new record, and .rec.prev (which then
   still holds the indexed version) is kept as it is. */
inline bool RotateToPrev(bool haveIndexLine, bool liveRead, long long liveLen, unsigned int liveCrc, long long idxLen, unsigned int idxCrc)
{
    return haveIndexLine && liveRead && liveLen == idxLen && liveCrc == idxCrc;
}

/* ---- pacing ------------------------------------------------------------------------------------------------ */

/* HOW MANY CURSOR TURNS `elapsedMs` OF WALL TIME EARNS over a ring of `n` squads, so that every squad is visited
   EXACTLY once per `periodMs` - not at least once. The fraction a call does not spend is carried in *credit (units:
   squad-milliseconds), so n turns are paid out per period whatever the call rate. WHY NOT P8f's ceil(): a per-call
   round-up visits a 5-squad ring once a SECOND, i.e. every squad every 5 s - six times the designed cost - and it gets
   worse the faster it is called. Called once per wall-clock second (StoreTick's 1 Hz gate is a frame count, so the
   caller passes measured milliseconds). `cap` bounds one call's turns (a long stall cannot burst); the credit is also
   capped at one whole cycle, so a stall is caught up by at most one extra lap. 0 for n == 0 or period == 0 (and the
   credit is cleared, so a ring that empties does not bank turns). */
inline unsigned int CursorTurns(unsigned int n, unsigned int periodMs, unsigned int elapsedMs, unsigned long long* credit,
                                unsigned int cap)
{
    if (n == 0 || periodMs == 0) { *credit = 0; return 0; }
    const unsigned long long full = (unsigned long long)n * (unsigned long long)periodMs;
    *credit += (unsigned long long)n * (unsigned long long)elapsedMs;
    if (*credit > full) *credit = full;
    unsigned long long q = *credit / (unsigned long long)periodMs;
    if (cap != 0 && q > (unsigned long long)cap) q = (unsigned long long)cap;
    *credit -= q * (unsigned long long)periodMs;
    return (unsigned int)q;
}

/* ON A CURSOR TURN, IS THIS SQUAD WRITTEN (serialised and CRC-compared)? THE RING'S OWN CYCLE IS THE PERIOD, so an
   ordinary squad is always due on its turn; the 2 s gap only stops a squad the dirty arm has just written from being
   serialised twice in a moment. A SLOW squad (its last serialise > kSlowUs) is due only every period * slowMul. */
inline bool TurnDue(bool slow, unsigned int sinceLastMs, unsigned int periodMs, unsigned int slowMul, unsigned int gapMs)
{
    if (slow && slowMul > 1)
    {
        const unsigned long long w = (unsigned long long)periodMs * (unsigned long long)slowMul;
        return (unsigned long long)sinceLastMs >= w;
    }
    return sinceLastMs >= gapMs;
}

/* AN EVENT MARKED THIS SQUAD. dirtyNow (a completed hand-over; the TEST poke) jumps the gap; an ordinary mark waits
   out the 2 s gap so a burst of item moves is one write. The slow rule does NOT gag an event. */
inline bool DirtyDue(bool dirtyNow, unsigned int sinceLastMs, unsigned int gapMs)
{
    return dirtyNow || sinceLastMs >= gapMs;
}

/* May one more serialise run inside the current one-second window? */
inline bool PerSecondAllows(unsigned int doneThisSecond, unsigned int cap)
{
    return cap == 0 || doneThisSecond < cap;
}

inline bool IsSlow(long long serUs, unsigned int slowUs) { return serUs > (long long)slowUs; }

/* bytes per second over a window, rounded; 0 for an empty window. */
inline unsigned long long BytesPerSec(unsigned long long bytes, unsigned long long windowMs)
{
    if (windowMs == 0) return 0;
    return (bytes * 1000ULL + windowMs / 2ULL) / windowMs;
}

/* min / average / max of a measured quantity (microseconds or bytes). */
struct CostStat
{
    long long n, sum, mn, mx;
    CostStat() : n(0), sum(0), mn(0), mx(0) {}
    void Add(long long v) { if (n == 0 || v < mn) mn = v; if (n == 0 || v > mx) mx = v; sum += v; ++n; }
    long long Avg() const { return n ? (sum + n / 2) / n : 0; }
};

/* ---- mmo1c: GameDataContainer::save 0x6BD220's BYTES, WRITTEN TO MEMORY ------------------------------------------
   The engine writes through a std::ostream: an int is 4 bytes little-endian; a string is its length as a 4-byte int,
   then that many bytes (no terminator); a map is its entry count, then per entry the key string and the value (bool
   1 byte, float/int 4, Vector3 12, Quaternion 16, a string value as a string) - the six writers 0x6C7050..0x6C7550.
   PodBuf is a caller-owned buffer. len counts EVERY byte asked for, also past cap, so one pass over a too-small
   buffer gives the exact size to retry with (len > cap = overflow; nothing is ever written past cap).
   POD only, no allocation, nothing that unwinds: the plugin calls these inside __try (C2712). */
struct PodBuf { char* p; size_t cap; size_t len; unsigned int shape; };   /* shape: mmo1c fold, the kShape* bits the walk saw */
inline void PodInit(PodBuf* b, char* p, size_t cap) { b->p = p; b->cap = cap; b->len = 0; b->shape = 0; }
inline bool PodOverflow(const PodBuf* b) { return b->len > b->cap; }
inline void PodPutBytes(PodBuf* b, const void* src, size_t n)
{
    if (n != 0 && b->len <= b->cap && n <= b->cap - b->len) std::memcpy(b->p + b->len, src, n);
    b->len += n;
}
inline void PodPutI32(PodBuf* b, int v)
{
    const unsigned int u = (unsigned int)v;
    char e[4];
    e[0] = (char)(u & 0xFFu); e[1] = (char)((u >> 8) & 0xFFu); e[2] = (char)((u >> 16) & 0xFFu); e[3] = (char)((u >> 24) & 0xFFu);
    PodPutBytes(b, e, 4);
}
/* a string as the engine writes it: (int)length, then that many bytes (a length <= 0 writes no bytes). */
inline void PodPutStr(PodBuf* b, const char* s, size_t n)
{
    const int len = (int)n;
    PodPutI32(b, len);
    if (len > 0) PodPutBytes(b, s, (size_t)len);
}
/* one map entry: the key string, then the value's raw bytes (0x6C7050/7150/7250/7350/7450) or a string (0x6C7550). */
inline void PodPutKeyRaw(PodBuf* b, const char* k, size_t kn, const void* v, size_t vn) { PodPutStr(b, k, kn); PodPutBytes(b, v, vn); }
inline void PodPutKeyStr(PodBuf* b, const char* k, size_t kn, const char* v, size_t vn) { PodPutStr(b, k, kn); PodPutStr(b, v, vn); }
/* the file header: the save version 15, the container's own id (+0x08), its record count. */
const int kGdcSaveVersion = 15;
inline void PodPutGdcHeader(PodBuf* b, int containerId, int records)
{
    PodPutI32(b, kGdcSaveVersion); PodPutI32(b, containerId); PodPutI32(b, records);
}
/* the first offset where two byte strings differ (the shorter length when one is a prefix of the other); -1 = equal. */
inline long long FirstDiff(const char* a, size_t an, const char* b, size_t bn)
{
    const size_t n = an < bn ? an : bn;
    for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return (long long)i;
    return an == bn ? -1 : (long long)n;
}

/* ---- mmo1c fold (review-mmo1c MED2): WHICH WRITES ALSO RUN THE ENGINE COMPARE ----------------------------------------
   Our writer records the SHAPE of what it walked: bit i (0..6) = map i non-empty, in 0x6BD220's order (boolFields, floatFields,
   intFields, vectorFields, rotationFields, stringFields, fileFields); then reference lists with entries, instances present, instance string
   arrays with entries, an inline string (capacity <= 15), a heap string, and >= 2 records of type 0x1E or >= 2 of a
   character type. A write compares when it is one of the first firstN of the process, when its shape has a bit no
   earlier compare saw (the seen mask is cleared on a world reset), or when it is every `every`-th write since then. */
const unsigned int kShapeMap0        = 0x1u;
const unsigned int kShapeRefEntries  = 0x80u;
const unsigned int kShapeInstances   = 0x100u;
const unsigned int kShapeInstStrings = 0x200u;
const unsigned int kShapeInlineStr   = 0x400u;
const unsigned int kShapeHeapStr     = 0x800u;
const unsigned int kShapeTwoRecords  = 0x1000u;
const unsigned int kCompareEvery     = 128;
inline unsigned int ShapeMapBit(int i) { return (i >= 0 && i < 7) ? (kShapeMap0 << i) : 0u; }
inline unsigned int ShapeStrBit(size_t capacity) { return capacity > 15 ? kShapeHeapStr : kShapeInlineStr; }
/* writeNo: this write's 1-based number since the last re-arm; comparesDone: compares so far this process. */
inline bool CompareDue(unsigned int shape, unsigned int seen, unsigned long long writeNo, unsigned int comparesDone,
                       unsigned int firstN, unsigned int every)
{
    if (comparesDone < firstN) return true;
    if ((shape & ~seen) != 0) return true;
    return every != 0 && writeNo != 0 && writeNo % every == 0;
}


/* ---- mmo2 + fold (e47-mmo-design.md 3.2, 3.3, 4.2; owner ruling 2026-09-27 "records win"; review-mmo2): THE SAVE'S
   OWN MARK, THE OVERLAY RULE, A .platoon FILE'S CHARACTERS AND ITEMS, THE CONTAINER NAME CHECK AND THE ROSTER CHECK.
   All pure: no disk, no clock, no engine. The fold marker: GdcScanOut. ------------------------------------------ */

/* The mark is ONE line in the save folder:
   "om2\t<records store>\t<ownSeq at the save's request>\t<unix s>\t<world key>\t<profile id>\n" (profile may be empty).
   At a save REQUEST an existing mark is overwritten with MarkPending() - a line no parser accepts - so a save that
   then fails cannot leave the previous save's mark behind (review-mmo2 item 4). */
inline const char* MarkFile() { return "multiplayer.mark"; }
inline const char* MarkPending() { return "om2-pending"; }
struct MarkInfo
{
    std::string        store, world, profile;
    unsigned long long seq;
    long long          at;
    MarkInfo() : seq(0), at(0) {}
};
inline bool EncodeMarkLine(const MarkInfo& m, std::string* out)
{
    if (m.store.empty() || m.world.empty() || m.at < 0 || !IdentityFieldOk(m.store) || !IdentityFieldOk(m.world) || !IdentityFieldOk(m.profile)) return false;
    char b[80];
    std::sprintf(b, "\t%llu\t%lld\t", m.seq, m.at);
    *out = "om2\t" + m.store + b + m.world + "\t" + m.profile + "\n";
    return true;
}
/* The inverse, over the file's whole text (the first line counts; a trailing CR/LF is tolerated). */
inline bool ParseMarkLine(const std::string& textIn, MarkInfo* out)
{
    std::string line = textIn;
    const std::string::size_type nl = line.find('\n');
    if (nl != std::string::npos) line.erase(nl);
    while (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    std::vector<std::string> f;
    std::string::size_type from = 0;
    for (;;)
    {
        const std::string::size_type tb = line.find('\t', from);
        if (tb == std::string::npos) { f.push_back(line.substr(from)); break; }
        f.push_back(line.substr(from, tb - from)); from = tb + 1;
    }
    if (f.size() != 6 || f[0] != "om2" || f[1].empty() || f[4].empty()) return false;
    unsigned long long s = 0, a = 0;
    if (!ParseULL(f[2], &s) || !ParseULL(f[3], &a)) return false;
    out->store = f[1]; out->seq = s; out->at = (long long)a; out->world = f[4]; out->profile = f[5];
    return true;
}
inline bool IsMarkPending(const std::string& text) { return text.compare(0, 11, "om2-pending") == 0; }

/* THE OVERLAY RULE. With a mark: records whose ownSeq is newer than the mark. Without one (review-mmo2 item 4, the
   manager's decision under "records win"): records WRITTEN after the save's quick.save file time - a record older
   than the save is not newer progress. Neither known: every record (the load has no quick.save to be older than). */
enum { kRuleMark = 0, kRuleSaveTime = 1, kRuleAll = 2 };
inline int OverlayRule(bool haveMark, bool haveSaveTime) { return haveMark ? kRuleMark : (haveSaveTime ? kRuleSaveTime : kRuleAll); }
inline bool OverlayCandidate(int rule, unsigned long long markSeq, long long saveTime, unsigned long long recSeq, long long recWrittenAt)
{
    if (rule == kRuleMark) return recSeq > markSeq;
    if (rule == kRuleSaveTime) return recWrittenAt > saveTime;
    return true;
}
/* a Windows FILETIME (100 ns since 1601) as unix seconds; 0 before 1970 */
inline long long FileTimeToUnix(unsigned long long ft)
{
    const unsigned long long k1970 = 116444736000000000ULL;
    return ft <= k1970 ? 0 : (long long)((ft - k1970) / 10000000ULL);
}

/* review-mmo2 item 6: the container being loaded must be this squad's own save file: its name ends with
   "platoon" + a slash (either) + "<sid>.platoon", and "platoon" is a whole path part. */
inline bool NameMatchesSquadFile(const std::string& name, const std::string& sid)
{
    if (sid.empty()) return false;
    const std::string tail = sid + ".platoon";
    if (name.size() < tail.size() + 8) return false;
    if (name.compare(name.size() - tail.size(), tail.size(), tail) != 0) return false;
    const std::string::size_type sep = name.size() - tail.size() - 1;
    if (name[sep] != '/' && name[sep] != '\\') return false;
    if (name.compare(sep - 7, 7, "platoon") != 0) return false;
    const std::string::size_type before = sep - 7;
    return before == 0 || name[before - 1] == '/' || name[before - 1] == '\\';
}

/* A bounds-checked reader over 0x6BD220's layout (F983: little-endian int32; a string is int32 length + bytes). */
struct GdcCur { const unsigned char* p; size_t n; size_t at; bool bad; };
inline int GdcI32(GdcCur* c)
{
    if (c->bad || c->at > c->n || c->n - c->at < 4) { c->bad = true; return 0; }
    const unsigned char* q = c->p + c->at;
    c->at += 4;
    return (int)((unsigned int)q[0] | ((unsigned int)q[1] << 8) | ((unsigned int)q[2] << 16) | ((unsigned int)q[3] << 24));
}
inline void GdcSkip(GdcCur* c, size_t k)
{
    if (c->bad || c->at > c->n || k > c->n - c->at) { c->bad = true; return; }
    c->at += k;
}
inline void GdcStr(GdcCur* c, std::string* out)
{
    const int len = GdcI32(c);
    if (c->bad) return;
    if (len < 0) { c->bad = true; return; }
    if ((size_t)len > c->n - c->at) { c->bad = true; return; }
    if (out) out->assign((const char*)(c->p + c->at), (size_t)len);
    c->at += (size_t)len;
}
/* a count that the remaining bytes could hold at `minEach` bytes an entry */
inline int GdcCount(GdcCur* c, size_t minEach)
{
    const int k = GdcI32(c);
    if (c->bad) return 0;
    if (k < 0 || (minEach != 0 && (size_t)k > (c->n - c->at) / minEach)) { c->bad = true; return 0; }
    return k;
}

/* WHAT ONE .platoon FILE HOLDS (the save's squad file or an own record - both are 0x6BD220's bytes).
   A HAND is written by 0x6BC730 into a record's intFields as five ints: "<prefix>handle" + TYPE / C / CS / I / S (type,
   container, container serial, index, serial; decomp_6bc730, the suffix strings read from .rdata). The engine keeps no
   other character uid (F793).
   chars:     per record of type 0x24 (Character::serialise's main record, decomp_627610) its OWN hand (prefix "")
              as "S=<serial>;T=<type>" - serial + type only (review-mmo2 item 7: the container fields are not part of the
              identity). A 0x24 record without S and TYPE is counted in noHandle and gets "?<src>#<record>" (item 8).
   charsFull: the same characters, every hand field, "C=..;CS=..;I=..;S=..;TYPE=..;" (logged, never compared).
   items:     every OTHER hand in the file whose type is not 1 (CHARACTER) - a record's own hand when it is not a 0x24
              record, and any prefixed hand inside any record - as "S=..;T=..". A diagnostic count only (itemsOnlyIn*).
   false = the bytes do not parse. */
struct GdcScanOut
{
    std::vector<std::string> chars, charsFull;
    std::set<std::string>    items;
    int                      noHandle;
    GdcScanOut() : noHandle(0) {}
};
inline std::string HandId(int serial, int type)
{
    char b[48]; std::sprintf(b, "S=%d;T=%d", serial, type); return std::string(b);
}
inline bool GdcScan(const char* data, size_t n, const std::string& src, GdcScanOut* out)
{
    out->chars.clear(); out->charsFull.clear(); out->items.clear(); out->noHandle = 0;
    GdcCur c; c.p = (const unsigned char*)data; c.n = data ? n : 0; c.at = 0; c.bad = false;
    GdcI32(&c); GdcI32(&c);
    const int recs = GdcCount(&c, 40);
    if (c.bad) return false;
    static const int kWidth[7] = { 1, 4, 4, 12, 16, 0, 0 };   /* boolFields, floatFields, intFields, vectorFields, rotationFields, stringFields, fileFields */
    for (int r = 0; r < recs; ++r)
    {
        GdcI32(&c);
        const int type = GdcI32(&c);
        GdcI32(&c);
        GdcStr(&c, 0); GdcStr(&c, 0);
        GdcI32(&c);
        std::map<std::string, std::map<std::string, int> > hands;   /* prefix -> suffix -> value */
        for (int m = 0; m < 7; ++m)
        {
            const int cnt = GdcCount(&c, 4);
            for (int e = 0; e < cnt && !c.bad; ++e)
            {
                std::string k;
                GdcStr(&c, &k);
                if (kWidth[m] == 0) { GdcStr(&c, 0); continue; }
                const std::string::size_type hp = (m == 2) ? k.rfind("handle") : std::string::npos;
                if (hp != std::string::npos)
                {
                    const int v = GdcI32(&c);
                    hands[k.substr(0, hp)][k.substr(hp + 6)] = v;
                }
                else GdcSkip(&c, (size_t)kWidth[m]);
            }
            if (c.bad) return false;
        }
        const int lists = GdcCount(&c, 8);
        for (int l = 0; l < lists && !c.bad; ++l)
        {
            GdcStr(&c, 0);
            const int ne = GdcCount(&c, 16);
            for (int e = 0; e < ne && !c.bad; ++e) { GdcStr(&c, 0); GdcSkip(&c, 12); }
        }
        const int inst = GdcCount(&c, 40);
        for (int i = 0; i < inst && !c.bad; ++i)
        {
            GdcStr(&c, 0); GdcStr(&c, 0); GdcSkip(&c, 12); GdcSkip(&c, 16);
            const int ns = GdcCount(&c, 4);
            for (int s = 0; s < ns && !c.bad; ++s) GdcStr(&c, 0);
        }
        if (c.bad) return false;
        for (std::map<std::string, std::map<std::string, int> >::const_iterator h = hands.begin(); h != hands.end(); ++h)
        {
            const std::map<std::string, int>& f = h->second;
            std::map<std::string, int>::const_iterator s = f.find("S"), ty = f.find("TYPE");
            const bool own = h->first.empty();
            if (type == 0x24 && own) continue;   /* the character itself: below */
            if (s != f.end() && ty != f.end() && ty->second != 1) out->items.insert(HandId(s->second, ty->second));
        }
        if (type == 0x24)
        {
            std::map<std::string, std::map<std::string, int> >::const_iterator h = hands.find(std::string());
            const bool ok = h != hands.end() && h->second.count("S") != 0 && h->second.count("TYPE") != 0;
            std::string full;
            if (h != hands.end())
                for (std::map<std::string, int>::const_iterator f = h->second.begin(); f != h->second.end(); ++f)
                {
                    char b[32]; std::sprintf(b, "=%d;", f->second);
                    full += f->first + b;
                }
            if (ok) out->chars.push_back(HandId(h->second.find("S")->second, h->second.find("TYPE")->second));
            else
            {
                ++out->noHandle;
                char b[24]; std::sprintf(b, "#%d", r);
                out->chars.push_back("?" + src + b);
            }
            out->charsFull.push_back(full.empty() ? std::string("-") : full);
        }
    }
    return !c.bad;
}

/* THE ROSTER CHECK (design 3.3; review-mmo2 items 2 and 5). One squad the overlay may apply: its characters in the
   save's version and in the record. apply: in = 1 for a candidate; out = 1 when the record is laid over the save.
   conflict / duplicate / empty: why it was refused (the save's version is kept). */
struct RosterSquad
{
    std::string              key;
    std::vector<std::string> saveChars, recChars;
    int                      apply, conflict, duplicate, empty;
    RosterSquad() : apply(0), conflict(0), duplicate(0), empty(0) {}
};
struct RosterCounts
{
    int conflict, duplicate, empty;
    RosterCounts() : conflict(0), duplicate(0), empty(0) {}
};
/* THE ROSTER AFTER THE LOAD is every squad file of the save: `fixed` holds the characters of every squad that is not a
   candidate (their save versions), and each candidate adds its record when applied, else its save version. A record
   is applied only if every character its save version had, and every character the record holds, appears EXACTLY
   once in that roster: 0 = a person lost (conflict), 2+ = a person doubled (duplicate). A squad whose save version
   has no character is refused at once (empty: nothing to check it against). Otherwise ONE squad is refused at a time
   (the first in order that fails) and the check is run again, since refusing one changes the roster. Returns the
   number refused. */
inline int RosterResolve(std::vector<RosterSquad>* sq, const std::vector<std::string>& fixed, RosterCounts* rc)
{
    RosterCounts local;
    RosterCounts* out = rc ? rc : &local;
    *out = RosterCounts();
    int refused = 0;
    for (std::vector<RosterSquad>::size_type i = 0; i < sq->size(); ++i)
    {
        RosterSquad& s = (*sq)[i];
        if (s.apply && s.saveChars.empty()) { s.apply = 0; s.empty = 1; ++out->empty; ++refused; }
    }
    for (;;)
    {
        std::map<std::string, int> count;
        for (std::vector<std::string>::size_type k = 0; k < fixed.size(); ++k) ++count[fixed[k]];
        for (std::vector<RosterSquad>::size_type i = 0; i < sq->size(); ++i)
        {
            const RosterSquad& s = (*sq)[i];
            const std::vector<std::string>& v = s.apply ? s.recChars : s.saveChars;
            for (std::vector<std::string>::size_type k = 0; k < v.size(); ++k) ++count[v[k]];
        }
        std::vector<RosterSquad>::size_type bad = sq->size();
        int why = 0;
        for (std::vector<RosterSquad>::size_type i = 0; i < sq->size() && bad == sq->size(); ++i)
        {
            const RosterSquad& s = (*sq)[i];
            if (!s.apply) continue;
            for (std::vector<std::string>::size_type k = 0; k < s.saveChars.size() && bad == sq->size(); ++k)
            {
                std::map<std::string, int>::const_iterator it = count.find(s.saveChars[k]);
                if (it == count.end() || it->second == 0) { bad = i; why = 1; }
                else if (it->second > 1) { bad = i; why = 2; }
            }
            for (std::vector<std::string>::size_type k = 0; k < s.recChars.size() && bad == sq->size(); ++k)
                if (count[s.recChars[k]] > 1) { bad = i; why = 2; }
        }
        if (bad == sq->size()) return refused;
        RosterSquad& b = (*sq)[bad];
        b.apply = 0;
        if (why == 1) { b.conflict = 1; ++out->conflict; } else { b.duplicate = 1; ++out->duplicate; }
        ++refused;
    }
}

/* ---- mmo4 (e47-mmo-design.md 4.1, 7 item 4, decision M5; fold review-mmo4): the one engine save at leave / Exit ----
   The leave save runs as a small phase machine on the main thread: drain the writer -> request the save -> wait for the
   completion edge -> finished (then the leave or the quit continues). Every phase has a bounded give-up that logs. */
const int kLsIdle = 0, kLsDrain = 1, kLsRequest = 2, kLsWait = 3, kLsFinished = 4;
const int kLsContLeave = 1, kLsContQuit = 2;   /* what continues at the completion edge (bits) */
const int kLsWhyNone = 0, kLsWhyCompleted = 1, kLsWhyDrainSlow = 2, kLsWhyNotAccepted = 3, kLsWhyRefused = 4, kLsWhyTimeout = 5,
          kLsWhyFailed = 6, kLsWhyWorldChanged = 7, kLsWhyStopping = 8, kLsWhyStale = 9;
const unsigned int kLsDrainMs = 5000, kLsRequestMs = 30000, kLsWaitMs = 90000;

inline const char* LeaveSaveWhyName(int why)
{
    switch (why)
    {
    case kLsWhyCompleted:    return "completed";
    case kLsWhyDrainSlow:    return "drain-slow";
    case kLsWhyNotAccepted:  return "not-accepted";
    case kLsWhyRefused:      return "refused";
    case kLsWhyTimeout:      return "not-finished";
    case kLsWhyFailed:       return "failed-no-quick.save";
    case kLsWhyWorldChanged: return "world-changed";
    case kLsWhyStopping:     return "stopping";
    case kLsWhyStale:        return "quick.save-older-than-request";
    default:                 return "none";
    }
}
/* WHEN (T-251): a multiplayer world, the profile's save folder is decided (SaveRedirectWanted - PP5 sends every save there),
   a world is loaded and running, and the game is not already quitting. No name rule: a real player's folder is saved. */
/* T-251 part 2 (manager 2026-09-29 decision 3): and the character editor is not open - there is no character to save yet
   (the engine's own SAVE button is greyed then). */
inline bool LeaveSaveWanted(bool coopWorld, const std::string& profileFolder, bool worldRunning, bool quitting, bool editorOpen = false)
{
    return coopWorld && !profileFolder.empty() && worldRunning && !quitting && !editorOpen;
}
/* WHICH SLOT: the profile's save folder under its own name - never the loaded slot's name when the two differ. "" = none. */
inline std::string LeaveSaveTarget(const std::string& profileFolder)
{
    return profileFolder;
}
/* ---- T-251 part 2 (owner design: the MMO save - one save moment is RIGHT AFTER A NEW CHARACTER IS MADE; manager decisions
   2026-09-29) ----
   THE EDGE, each main-thread tick: win = the character editor window (ForgottenGUI+0x1C0, 0 = none), mode = the window's byte
   +0x1C0 (1 = an in-game editor: recruit etc.; 0 = the new-game editor), aiOff = PlayerInterface+0x2F0 (the character-edit
   byte; 0 once the window's destructor ran), world = the world load/reset count. The character was made when the editor goes
   from open to closed, it was the new-game kind, no world load came between its opening and its close (another window is a
   new opening), and the AI is back on: the save is ARMED. A deferred trigger is never consumed by a gate: while armed it waits
   until the leave save is wanted, then starts (or joins) it; it is dropped only on a world change or when the game is stopping. */
struct CreatedSave
{
    int open, mode, pending, running, waitLogged, tries;   /* tries (T-251 review fold L1): not-accepted give-ups since the arm */
    unsigned long long win;
    long worldAtOpen, worldArmed;
    long long endSeen;   /* the leave save's end count when this trigger started or joined a save */
    CreatedSave() : open(0), mode(0), pending(0), running(0), waitLogged(0), tries(0), win(0ULL), worldAtOpen(0), worldArmed(0), endSeen(0) {}
};
/* T-251 review fold L2: aiOff is 1 / 0, or -1 when the AI-off byte could not be read (the PlayerInterface pointer unreadable
   while the editor window read fine). An unknown AI-off at a new-game close in the same world is NOT YET: the close is not
   consumed and is decided again next frame. The in-game and world-change outcomes do not need the byte. */
const int kCrEdgeNone = 0, kCrEdgeArm = 1, kCrEdgeInGame = 2, kCrEdgeWorldChanged = 3, kCrEdgeAiOff = 4, kCrEdgeNotYet = 5;
inline int CreatedEdge(CreatedSave* s, unsigned long long win, int mode, int aiOff, long world)
{
    if (win != 0ULL)
    {
        if (s->open == 0 || win != s->win) { s->open = 1; s->win = win; s->worldAtOpen = world; }
        s->mode = mode;
        return kCrEdgeNone;
    }
    if (s->open == 0) return kCrEdgeNone;
    if (s->mode == 0 && world == s->worldAtOpen && aiOff < 0) return kCrEdgeNotYet;   /* L2: kept open, decided next frame */
    s->open = 0; s->win = 0ULL;
    if (s->mode != 0) return kCrEdgeInGame;
    if (world != s->worldAtOpen) return kCrEdgeWorldChanged;
    if (aiOff != 0) return kCrEdgeAiOff;
    s->pending = 1; s->worldArmed = world; s->waitLogged = 0; s->tries = 0;
    return kCrEdgeArm;
}
/* THE ARMED TRIGGER, each tick: dropped only on a world change or when stopping; started when the leave save is wanted;
   otherwise it waits (stays armed) and is tried again next frame. */
const int kCrWait = 0, kCrStartNow = 1, kCrDropWorld = 2, kCrDropStopping = 3;
inline int CreatedPendingStep(const CreatedSave& s, long world, bool stopping, bool wanted)
{
    if (world != s.worldArmed) return kCrDropWorld;
    if (stopping) return kCrDropStopping;
    return wanted ? kCrStartNow : kCrWait;
}
/* THE END of the save it started or joined (why = the leave save's reason): completed -> done; another save held the
   engine's save slot past the 30 s bound (not-accepted) -> RE-ARMED (manager 2026-09-29 decision 1); world-changed /
   stopping -> dropped; any other give-up (refused, not-finished, failed, stale) ends it - a re-arm there would repeat a save
   that cannot land, each round with a checkpoint. */
/* T-251 review fold L1: AT MOST kCrTries TRIES - the third not-accepted give-up since the arm ENDS the trigger
   (kCrOutTriesSpent) instead of re-arming it again; a new arm starts the count again. */
const int kCrOutDone = 0, kCrOutRearmed = 1, kCrOutDropped = 2, kCrOutGaveUp = 3, kCrOutTriesSpent = 4;
const int kCrTries = 3;
inline int CreatedSaveEnded(CreatedSave* s, int why)
{
    s->running = 0;
    if (why == kLsWhyCompleted) return kCrOutDone;
    if (why == kLsWhyNotAccepted)
    {
        if (++s->tries >= kCrTries) { s->pending = 0; return kCrOutTriesSpent; }
        s->pending = 1; s->waitLogged = 0; return kCrOutRearmed;
    }
    if (why == kLsWhyWorldChanged || why == kLsWhyStopping) return kCrOutDropped;
    return kCrOutGaveUp;
}
/* THE TIMED AUTOSAVE: skipped only when it is an autosave call NAMED "autosave..." (updateAutoSave passes "autosave" +
   index; F5 passes "quicksave" with isAutosave 0; the unidentified caller 0x47B530 is never skipped), in a co-op world,
   with the records writer on - and never for our own request (the save verb and the leave save pass isAutosave = 1). */
inline bool AutosaveSkip(bool isAutosave, const std::string& name, bool coopWorld, bool recordsOn, bool ourRequest)
{
    return isAutosave && name.compare(0, 8, "autosave") == 0 && coopWorld && recordsOn && !ourRequest;
}
/* T-251 fold item 4: THE RECORDS WRITER IS FAILING - kWriterFailN writes/verifies/index updates failed with no commit
   between them, or work has been queued with nothing committed for two heartbeats. While it is, the engine's timed autosave
   is allowed again (AutosaveSkip's recordsOn is false); a commit ends it. */
const unsigned int kWriterFailN = 3;
inline bool WriterFailing(unsigned int consecFails, bool pending, unsigned int msSinceProgress)
{
    return consecFails >= kWriterFailN || (pending && msSinceProgress >= 2u * kPeriodMs);
}
/* T-251 fold item 2: StoreRequestTestSave's answer. before/after = the request code around the call; refused = our save
   detour refused the save (the profile-folder redirect: the open save is another folder). 1 accepted, 0 another save pending
   (retry), -1 unreadable, kReqRefused REFUSED - never retried: LeaveSaveStep ends the leave save at once (kLsWhyRefused). */
const int kReqRefused = -3;
inline int TestSaveRequestResult(int before, bool refused, int after)
{
    if (before != 0) return before < 0 ? -1 : 0;
    if (refused) return kReqRefused;
    return after == 1 ? 1 : (after < 0 ? -1 : 0);
}
/* THE DRAIN (fold item 7): done once no record the leave was asked about is still queued or in flight - oldestPending is
   the lowest seq queued or being written (0 = none), beginSeq the engine half's counter at the begin. */
inline bool LeaveDrainDone(unsigned long long oldestPending, unsigned long long beginSeq)
{
    return oldestPending == 0 || oldestPending > beginSeq;
}
/* NO CANCELS A HELD EXIT (fold item 4): the pending resume is dropped; the save itself still finishes. */
inline int LeaveCancelQuit(int cont) { return cont & ~kLsContQuit; }
/* THE CONTINUATION (fold items 1, 2): the leave only for the session link it was asked about; the quit only when the
   world is the one it was asked in and the game is not already stopping. */
inline void LeaveContinue(int cont, int why, long genAtBegin, long genNow, int* doLeave, int* doQuit)
{
    *doLeave = ((cont & kLsContLeave) != 0 && genAtBegin == genNow) ? 1 : 0;
    *doQuit = ((cont & kLsContQuit) != 0 && why != kLsWhyWorldChanged && why != kLsWhyStopping) ? 1 : 0;
}
/* ONE STEP. drainDone: LeaveDrainDone; req: this frame's request result (1 accepted, 0 not yet, < 0 refused/unreadable;
   kLsRequest only); done: the completion check (1 finished with a fresh quick.save, 2 without one, 3 quick.save older than
   the request, 0 running, -1 unreadable; kLsWait only); worldSame: no load/reset since the begin; stopping: quitting or
   the engine is tearing the world down (drain and request phases only - a requested save is still watched). *why names
   a give-up or the completion (kLsWhyDrainSlow is a note: the save is still requested). Returns the next phase. */
inline int LeaveSaveStep(int phase, unsigned int msInPhase, int drainDone, int req, int done, bool worldSame, bool stopping, int* why)
{
    *why = kLsWhyNone;
    if (phase != kLsDrain && phase != kLsRequest && phase != kLsWait) return phase;
    if (!worldSame) { *why = kLsWhyWorldChanged; return kLsFinished; }
    if (stopping && phase != kLsWait) { *why = kLsWhyStopping; return kLsFinished; }
    if (phase == kLsDrain)
    {
        if (drainDone) return kLsRequest;
        if (msInPhase >= kLsDrainMs) { *why = kLsWhyDrainSlow; return kLsRequest; }
        return kLsDrain;
    }
    if (phase == kLsRequest)
    {
        if (req == 1) return kLsWait;
        if (req < 0) { *why = kLsWhyRefused; return kLsFinished; }
        if (msInPhase >= kLsRequestMs) { *why = kLsWhyNotAccepted; return kLsFinished; }
        return kLsRequest;
    }
    if (done == 1) { *why = kLsWhyCompleted; return kLsFinished; }
    if (done == 2) { *why = kLsWhyFailed; return kLsFinished; }
    if (done == 3) { *why = kLsWhyStale; return kLsFinished; }
    if (msInPhase >= kLsWaitMs) { *why = kLsWhyTimeout; return kLsFinished; }
    return kLsWait;
}

/* ---- mmo5 (e47-mmo-design.md 6, 7 item 5; owner rulings 2026-09-26): WHEN THE HOST LEAVES -------------------------
   A JOINER (never the host) whose host is gone runs the leave save, pauses, and shows one window whose one button exits the
   game. THE TWO WAYS TO KNOW IT: the host's SESSION_CLOSING (a deliberate leave or exit), or the game link staying down
   through link1's whole backoff (the 2, 4, 8 and 16 s re-dials, gamelink.h) after the host was seen in this world. */
const int kHlNone = 0, kHlAnnounced = 1, kHlLost = 2;
const unsigned int kHlLostRedials = 4;      /* link1's backoff steps; after them link1 only re-dials every 30 s, for ever */
const unsigned int kHlAutoExitMs = 20000;   /* the fallback's automatic exit, counted from the moment the save settled */
const int kHlSaveNone = 0, kHlSaving = 1, kHlSaved = 2, kHlSaveGaveUp = 3;

inline int HostLeftDecide(bool joiner, bool worldRunning, bool hostSeenInWorld, bool stopping, bool closingSeen, bool linkUp,
                          bool dialing, unsigned int redialAttempts)
{
    if (!joiner || !worldRunning || !hostSeenInWorld || stopping) return kHlNone;
    if (closingSeen) return kHlAnnounced;
    (void)dialing; (void)redialAttempts;   /* ui1 (owner 2026-09-27): logged only - the link DOWN is the CONNECTION LOST edge at once */
    return linkUp ? kHlNone : kHlLost;
}
/* M11a S3 (T-197; manager decisions 4(a), 6(a), 7(a), 2026-09-30) - WHICH ROAD FEEDS HostLeftDecide, AND THE SOURCE OF AN EDGE.
   ROAD SESSION (this game is a session-link joiner): today's inputs exactly - the session link, link1's re-dials, its
   SESSION_CLOSING latch - and they keep priority while that link is up, so a game with both roads never takes a second edge from
   the copy of the close that came through the world server. Only with the session link DOWN is that copy read (the edge is then
   announced instead of lost). ROAD WORLD (no session link as joiner or host, not the world's operator): the world-server link is
   the link - down = lost, today's lost branch (on link1's timeout terms since S3); the roster's operator absent (PLAYER_GONE for its
   slot after the world server's 10 s hold) = lost; the operator's SESSION_CLOSING through the world server = announced. Either
   road reaches the same kinds, so the same window and the same sentences. Neither road: not a joiner (nothing). */
const int kHlRoadNone = 0, kHlRoadSession = 1, kHlRoadWorld = 2;
const int kHlSrcNone = 0, kHlSrcClosingSession = 1, kHlSrcClosingLive = 2, kHlSrcOperatorAbsent = 3, kHlSrcWorldLinkLost = 4,
          kHlSrcSessionLinkLost = 5, kHlSrcCount = 6;
struct HostLeftFeed
{
    int road, joiner, linkUp, dialing, closingSeen, closingSrc;   /* closingSrc: the latch closingSeen came from, kHlSrcNone when 0 */
    unsigned int redials;
    HostLeftFeed() : road(0), joiner(0), linkUp(0), dialing(0), closingSeen(0), closingSrc(0), redials(0) {}
};
inline HostLeftFeed HostLeftFeedPick(bool sJoiner, bool sUp, bool sDialing, unsigned int sRedials, bool sClosing,
                                     bool worldRoad, bool wUp, bool wOpAbsent, bool wClosingLive)
{
    HostLeftFeed f;
    if (sJoiner)
    {
        f.road = kHlRoadSession; f.joiner = 1; f.linkUp = sUp ? 1 : 0; f.dialing = sDialing ? 1 : 0; f.redials = sRedials;
        if (sClosing) { f.closingSeen = 1; f.closingSrc = kHlSrcClosingSession; }
        else if (!sUp && wClosingLive) { f.closingSeen = 1; f.closingSrc = kHlSrcClosingLive; }
        return f;
    }
    if (!worldRoad) return f;
    f.road = kHlRoadWorld; f.joiner = 1; f.linkUp = (wUp && !wOpAbsent) ? 1 : 0;
    if (wClosingLive) { f.closingSeen = 1; f.closingSrc = kHlSrcClosingLive; }
    return f;
}
inline int HostLeftSourceOf(const HostLeftFeed& f, int kind, bool wUp)
{
    if (kind == kHlAnnounced) return f.closingSrc;
    if (kind != kHlLost || f.road == kHlRoadNone) return kHlSrcNone;
    if (f.road == kHlRoadSession) return kHlSrcSessionLinkLost;
    return wUp ? kHlSrcOperatorAbsent : kHlSrcWorldLinkLost;
}
/* M11a S3 review fold (F1, F2, F8, 2026-09-30). HostSeenAcrossRoad: "the host was seen" belongs to the road it was seen on - when
   the host-left feed's road changes (a session joiner that left, a game that moved to the other road) it starts over, so a game
   never shows a host-left window for a host it saw on another road. ClosingLiveHolds: the operator's SESSION_CLOSING that came
   through the world server (the relayed close latch) holds only for the SESSION EPISODE it arrived in - the episode moves when the
   session link comes UP and when the session is left, the same events that clear the session's own close flag
   (coopgl::HostClosingFlagStep) - so a later ordinary session drop reads as reconnecting, never as the host ending the game. */
inline int HostSeenAcrossRoad(int sawHost, int roadBefore, int roadNow) { return roadNow != roadBefore ? 0 : sawHost; }
inline bool ClosingLiveHolds(int latch, long latchEpisode, long episodeNow) { return latch != 0 && latchEpisode == episodeNow; }
inline const char* HostLeftSourceName(int s)
{
    if (s == kHlSrcClosingSession) return "closingSession";
    if (s == kHlSrcClosingLive) return "closingLive";
    if (s == kHlSrcOperatorAbsent) return "operatorAbsent";
    if (s == kHlSrcWorldLinkLost) return "worldLinkLost";
    if (s == kHlSrcSessionLinkLost) return "sessionLinkLost";
    return "none";
}
/* ui1 (ui-polish-audit 3.1; owner rulings 2026-09-27): THE DISCONNECTED DIALOG - Kenshi's message-box look, title
   DISCONNECTED, ONE line saying what happened, ONE button EXIT GAME: greyed while the leave save runs (and while the exit is
   under way), enabled once the save settles WHATEVER the outcome. The save's outcome is never shown (owner: if it fails,
   say nothing - the player cannot act on it). */
/* ui1 (owner 2026-09-27, 02-project-rules "Connection trouble in three steps"): a deliberate leave (SESSION_CLOSING) is
   DISCONNECTED / "The host has ended the game." with no reconnecting; the link declared dead (HostLeftDecide: link DOWN,
   ENet's 30-60 s timeout) is CONNECTION LOST / "Reconnecting to host..." while link1 keeps re-dialling - a
   reconnect closes it (HostLeftCameBack). */
inline const char* HostLeftTitle(int kind) { return kind == kHlLost ? "CONNECTION LOST" : "DISCONNECTED"; }
inline const char* HostLeftSentence(int kind)
{
    if (kind == kHlAnnounced) return "The host has ended the game.";
    if (kind == kHlLost) return "Reconnecting to host...";
    return "";
}
inline bool HostLeftButtonEnabled(int saveState, bool exiting) { return saveState != kHlSaving && !exiting; }
/* ui1 follow-up: only DISCONNECTED (a deliberate leave) saves at once. CONNECTION LOST starts NO leave save - a reconnect
   resumes with everything running (the records writer on); its EXIT GAME, enabled from the start, goes through mmo4's
   held exit, which saves first on the way out. */
inline bool HostLeftStartsSave(int kind) { return kind == kHlAnnounced; }
/* The fallback's message line when no dialog can be built - never the save's outcome; only a DISCONNECTED game closes on
   its own (a lost one keeps re-dialling). */
inline std::string HostLeftFallbackLine(int kind, int saveState, bool exiting, bool harnessDriven)
{
    std::string s = kind == kHlLost ? std::string("Connection lost. Reconnecting to host...") : std::string(HostLeftSentence(kind));
    if (exiting) s += " Closing the game...";
    else if (saveState == kHlSaving) s += " Saving your game...";
    else if (kind == kHlLost) s += " Press Escape to open the menu and quit.";   /* ui1 review MED: say how to leave when nothing closes on its own; ui1c: Escape opens the pause menu, the player still chooses quit */
    else if (kind == kHlAnnounced && !harnessDriven) s += " Kenshi will close in 20 seconds.";
    return s;
}
/* ui1 review MED: with no dialog (the fallback), a LOST host's line is shown again every 30 s while the pause holds, so the
   way out stays on screen. `exiting`: any exit under way - our EXIT GAME, or (ui1c) the engine's quit from the Escape menu. */
const unsigned int kHlLineRepeatMs = 30000;
inline bool HostLeftLineRepeatDue(bool fallback, bool lost, bool exiting, unsigned int msSinceLine)
{
    return fallback && lost && !exiting && msSinceLine >= kHlLineRepeatMs;
}
/* The host came back before EXIT GAME was clicked (a lost host only): the dialog closes and this line is shown. */
inline const char* HostLeftBackLine() { return "Reconnected to the host."; }
/* STEP 2: the host silent for kHostQuietMs while the link is still up - a small notice that does not pause or block, gone
   as soon as the host is heard. THE STEP DECISION for one joiner tick (the host never shows any of these). */
const unsigned int kHostQuietMs = 10000;
const int kCtNothing = 0, kCtQuietNotice = 1, kCtDisconnected = 2, kCtConnectionLost = 3;
inline const char* HostQuietLine() { return "Connection problem - waiting for the host..."; }
inline int ConnTroubleStep(bool joiner, bool worldRunning, bool hostSeenInWorld, bool stopping, bool closingSeen, bool linkUp,
                           unsigned int silenceMs)
{
    const int k = HostLeftDecide(joiner, worldRunning, hostSeenInWorld, stopping, closingSeen, linkUp, false, 0);
    if (k == kHlAnnounced) return kCtDisconnected;
    if (k == kHlLost) return kCtConnectionLost;
    if (joiner && worldRunning && hostSeenInWorld && !stopping && linkUp && silenceMs >= kHostQuietMs) return kCtQuietNotice;
    return kCtNothing;
}
/* THE FALLBACK'S AUTOMATIC EXIT: the fallback path only, once the save settled (or none ran) and 20 s have passed since -
   and never in a harness-driven game (a command-channel verb has run in this process): the harness reads it afterwards. */
inline bool HostLeftAutoExitDue(bool fallback, bool settled, unsigned int msSinceSettled, bool harnessDriven, bool announced = true)
{
    return fallback && settled && msSinceSettled >= kHlAutoExitMs && !harnessDriven && announced;   /* ui1: a LOST host is re-dialled, never auto-exited */
}
/* mmo5 LOW (the mmo4 re-check): a confirm result != 2 from the SAME sender that raised the held quit does not cancel it. */
/* mmo5 fold item 5: a hold raised by sender 0 - the Exit button, the fallback's automatic exit, the quitmenu lever - ignores
   every cancel (no dialog of its own can answer No). */
inline bool LeaveCancelIgnored(bool holdSenderSet, const void* holdSender, const void* sender)
{
    return holdSenderSet && (holdSender == 0 || holdSender == sender);
}
/* mmo5 fold item 1: SESSION_CLOSING is acted on only in a running world where the host was seen; otherwise it is dropped. */
inline bool HostLeftClosingActionable(bool worldRunning, bool hostSeenInWorld) { return worldRunning && hostSeenInWorld; }
/* mmo5 fold item 2: a LOST host whose game link is up again before Exit was clicked is back - the window closes, the game
   resumes. An announced leave is final. */
inline bool HostLeftCameBack(int kind, bool exiting, bool linkUp) { return kind == kHlLost && !exiting && linkUp; }
/* mmo5 fold item 5: an exit that has not happened 10 s after the leave save went idle (and the quit byte is not up) - the
   button comes back (the fallback tries again 20 s later). */
const unsigned int kHlExitStallMs = 10000;
inline bool HostLeftExitStalled(bool exiting, bool saveIdleSeen, bool quitRaised, unsigned int msSinceSaveIdle)
{
    return exiting && saveIdleSeen && !quitRaised && msSinceSaveIdle >= kHlExitStallMs;
}

/* ---- mmo3 (e47-mmo-design.md 1.1 rows B-E, 7 item 3; owner rule: records always win over any save) ----------------
   Four more records in the same store, index and writer thread: pp.money, pp.research, pp.map, pp.faction. Each payload
   is OUR small encoding in the GdcCur layout (int32 little-endian; a string is int32 length + bytes; a float is its 4 bit
   bytes), opening with a 4-letter tag string. Pure: no disk, no clock, no engine. */
const int kCatMoney = 0, kCatResearch = 1, kCatMap = 2, kCatFaction = 3, kCatCount = 4;   /* also the restore order */
const unsigned int kCatMoneyMs = 1000;   /* money: a 1 Hz compare (no setter has callers - F696) */
const unsigned int kCatSlowMs  = 30000;  /* research (only while a queue is non-empty), map, faction: the 30 s compare */
const unsigned int kCatGapMs   = 2000;   /* an event (a tech completed, a standing moved) writes at most once per 2 s */
inline const char* CatName(int c)
{
    switch (c) { case kCatMoney: return "money"; case kCatResearch: return "research"; case kCatMap: return "map"; case kCatFaction: return "faction"; default: return "?"; }
}
inline const char* CatKey(int c)
{
    switch (c) { case kCatMoney: return "pp.money"; case kCatResearch: return "pp.research"; case kCatMap: return "pp.map"; case kCatFaction: return "pp.faction"; default: return ""; }
}
/* the mmo2 squad overlay only ever looks at squad records */
inline bool IsSquadKey(const std::string& k) { return k.compare(0, 9, "pp.squad.") == 0; }

inline void RecPutI32(std::vector<char>* v, int x)
{
    const unsigned int u = (unsigned int)x;
    v->push_back((char)(u & 0xFFu)); v->push_back((char)((u >> 8) & 0xFFu)); v->push_back((char)((u >> 16) & 0xFFu)); v->push_back((char)((u >> 24) & 0xFFu));
}
inline void RecPutF32(std::vector<char>* v, float f) { unsigned int u = 0; std::memcpy(&u, &f, 4); RecPutI32(v, (int)u); }
inline void RecPutStr(std::vector<char>* v, const std::string& s) { RecPutI32(v, (int)s.size()); v->insert(v->end(), s.begin(), s.end()); }
inline float GdcF32(GdcCur* c) { const unsigned int u = (unsigned int)GdcI32(c); float f = 0.0f; std::memcpy(&f, &u, 4); return f; }
inline unsigned char GdcU8(GdcCur* c)
{
    if (c->bad || c->at >= c->n) { c->bad = true; return 0; }
    return c->p[c->at++];
}
inline void RecCur(GdcCur* c, const std::vector<char>& b) { c->p = b.empty() ? 0 : (const unsigned char*)&b[0]; c->n = b.size(); c->at = 0; c->bad = false; }
inline bool RecTag(GdcCur* c, const char* tag) { std::string s; GdcStr(c, &s); return !c->bad && s == tag; }
inline bool RecEnd(const GdcCur* c) { return !c->bad && c->at == c->n; }

/* B money: "om3m" + int32 */
inline void EncodeMoney(int m, std::vector<char>* out) { out->clear(); RecPutStr(out, "om3m"); RecPutI32(out, m); }
inline bool DecodeMoney(const std::vector<char>& b, int* m)
{
    GdcCur c; RecCur(&c, b);
    if (!RecTag(&c, "om3m")) return false;
    const int v = GdcI32(&c);
    if (!RecEnd(&c)) return false;
    *m = v; return true;
}

/* C research: the floatFields and stringFields maps Research::save 0x8301E0 writes into a 0x15 record ("num currents", "num finished",
   "current prog<i>" / "current<i>", "finished<i>" and the paid list), sorted by key: "om3r", count, (key, float)...,
   count, (key, string)... */
struct ResearchRec
{
    std::vector<std::pair<std::string, float> >       f;
    std::vector<std::pair<std::string, std::string> > s;
};
inline bool ResFLess(const std::pair<std::string, float>& a, const std::pair<std::string, float>& b) { return a.first < b.first; }
inline bool ResSLess(const std::pair<std::string, std::string>& a, const std::pair<std::string, std::string>& b) { return a.first < b.first; }
inline void SortResearch(ResearchRec* r) { std::sort(r->f.begin(), r->f.end(), ResFLess); std::sort(r->s.begin(), r->s.end(), ResSLess); }
inline void EncodeResearch(const ResearchRec& r, std::vector<char>* out)
{
    out->clear(); RecPutStr(out, "om3r");
    RecPutI32(out, (int)r.f.size());
    for (size_t i = 0; i < r.f.size(); ++i) { RecPutStr(out, r.f[i].first); RecPutF32(out, r.f[i].second); }
    RecPutI32(out, (int)r.s.size());
    for (size_t i = 0; i < r.s.size(); ++i) { RecPutStr(out, r.s[i].first); RecPutStr(out, r.s[i].second); }
}
inline bool DecodeResearch(const std::vector<char>& b, ResearchRec* r)
{
    GdcCur c; RecCur(&c, b);
    if (!RecTag(&c, "om3r")) return false;
    ResearchRec o;
    const int nf = GdcCount(&c, 8);
    for (int i = 0; i < nf && !c.bad; ++i) { std::string k; GdcStr(&c, &k); const float v = GdcF32(&c); o.f.push_back(std::make_pair(k, v)); }
    const int ns = GdcCount(&c, 8);
    for (int i = 0; i < ns && !c.bad; ++i) { std::string k, v; GdcStr(&c, &k); GdcStr(&c, &v); o.s.push_back(std::make_pair(k, v)); }
    if (!RecEnd(&c)) return false;
    *r = o; return true;
}
/* THE TRAP (decomp_835220.txt:157-183, 267-271): Research::load resolves "finished<i>" for i < "num finished"; if none
   resolves it calls setResearched("_Default Start") - a toast. The restore is REFUSED unless this is >= 1: the number of
   i < num finished whose "finished<i>" is present and non-empty. */
inline int ResearchFinishedUsable(const ResearchRec& r)
{
    float nf = 0.0f;
    for (size_t i = 0; i < r.f.size(); ++i) if (r.f[i].first == "num finished") nf = r.f[i].second;
    if (!(nf >= 1.0f)) return 0;
    const int n = nf > 100000.0f ? 100000 : (int)nf;
    std::set<std::string> have;
    for (size_t i = 0; i < r.s.size(); ++i)
        if (r.s[i].first.compare(0, 8, "finished") == 0 && !r.s[i].second.empty()) have.insert(r.s[i].first);
    int k = 0;
    for (int i = 0; i < n; ++i) { char b[32]; std::sprintf(b, "finished%d", i); if (have.count(b) != 0) ++k; }
    return k;
}

/* D map knowledge: per town (stringID) the discovered (+0x180) and explored (+0x181) bytes, sorted by town: "om3d",
   count, (sid, byte, byte)... */
struct MapRow
{
    std::string   sid;
    unsigned char disc, expl;
    MapRow() : disc(0), expl(0) {}
};
inline bool MapRowLess(const MapRow& a, const MapRow& b)
{
    if (a.sid != b.sid) return a.sid < b.sid;
    if (a.disc != b.disc) return a.disc < b.disc;
    return a.expl < b.expl;
}
inline void EncodeMap(const std::vector<MapRow>& rowsIn, std::vector<char>* out)
{
    std::vector<MapRow> rows = rowsIn;
    std::sort(rows.begin(), rows.end(), MapRowLess);
    out->clear(); RecPutStr(out, "om3d");
    RecPutI32(out, (int)rows.size());
    for (size_t i = 0; i < rows.size(); ++i) { RecPutStr(out, rows[i].sid); out->push_back((char)rows[i].disc); out->push_back((char)rows[i].expl); }
}
inline bool DecodeMap(const std::vector<char>& b, std::vector<MapRow>* rows)
{
    GdcCur c; RecCur(&c, b);
    if (!RecTag(&c, "om3d")) return false;
    std::vector<MapRow> o;
    const int n = GdcCount(&c, 6);
    for (int i = 0; i < n && !c.bad; ++i) { MapRow r; GdcStr(&c, &r.sid); r.disc = GdcU8(&c); r.expl = GdcU8(&c); o.push_back(r); }
    if (!RecEnd(&c)) return false;
    rows->swap(o); return true;
}

/* E the player faction row: its name, platoonIDs (Faction+0x260), and per NPC faction (stringID) this faction's standing
   with it (relation, trust, trustNeg) and its standing with this faction (the *Back fields): "om3f", name, platoonIDs,
   count, (sid, 6 floats)... sorted by sid. */
struct FacRow
{
    std::string sid;
    float       rel, trust, trustNeg, relBack, trustBack, trustNegBack;
    int         flags, flagsBack;   /* mmo3 fold 7: ally (1) / atWar (2) bits as WriteEntry keeps them; -1 = not recorded (an om3f record) */
    FacRow() : rel(0), trust(0), trustNeg(0), relBack(0), trustBack(0), trustNegBack(0), flags(-1), flagsBack(-1) {}
};
struct FactionRec
{
    std::string         name;
    int                 platoonIds;
    std::vector<FacRow> rows;
    FactionRec() : platoonIds(0) {}
};
inline bool FacRowLess(const FacRow& a, const FacRow& b) { return a.sid < b.sid; }
inline void EncodeFaction(const FactionRec& f, std::vector<char>* out)
{
    std::vector<FacRow> rows = f.rows;
    std::sort(rows.begin(), rows.end(), FacRowLess);
    out->clear(); RecPutStr(out, "om3g"); RecPutStr(out, f.name); RecPutI32(out, f.platoonIds);   /* mmo3 fold 7: om3g = with the flags */
    RecPutI32(out, (int)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        RecPutStr(out, rows[i].sid);
        RecPutF32(out, rows[i].rel); RecPutF32(out, rows[i].trust); RecPutF32(out, rows[i].trustNeg);
        RecPutF32(out, rows[i].relBack); RecPutF32(out, rows[i].trustBack); RecPutF32(out, rows[i].trustNegBack);
        RecPutI32(out, rows[i].flags); RecPutI32(out, rows[i].flagsBack);   /* mmo3 fold 7 */
    }
}
inline bool DecodeFaction(const std::vector<char>& b, FactionRec* f)
{
    GdcCur c; RecCur(&c, b);
    std::string tag; GdcStr(&c, &tag);
    const bool v2 = tag == "om3g";   /* mmo3 fold 7: om3g carries the flags; an om3f record (mmo3) still reads, flags -1 */
    if (c.bad || (!v2 && tag != "om3f")) return false;
    FactionRec o;
    GdcStr(&c, &o.name);
    o.platoonIds = GdcI32(&c);
    const int n = GdcCount(&c, v2 ? 36 : 28);   /* mmo3 fold 7 */
    for (int i = 0; i < n && !c.bad; ++i)
    {
        FacRow r; GdcStr(&c, &r.sid);
        r.rel = GdcF32(&c); r.trust = GdcF32(&c); r.trustNeg = GdcF32(&c);
        r.relBack = GdcF32(&c); r.trustBack = GdcF32(&c); r.trustNegBack = GdcF32(&c);
        if (v2) { r.flags = GdcI32(&c); r.flagsBack = GdcI32(&c); }   /* mmo3 fold 7 */
        o.rows.push_back(r);
    }
    if (!RecEnd(&c)) return false;
    *f = o; return true;
}
/* platoonIDs is also decision 31(a)'s number-block counter: never stepped back, or a group number is reused */
inline int PlatoonIdsAfterRestore(int live, int rec) { return rec > live ? rec : live; }

/* THE RESTORE GATE, per category: mmo2's overlay rule (OverlayCandidate: with a mark, seq > mark; with none, written after
   quick.save's file time; neither known, every record), then the record's bytes must pass the index line's length + CRC. */
const int kRsRestore = 0, kRsAbsent = 1, kRsOlder = 2, kRsBad = 3, kRsDecode = 4, kRsNoFinished = 5, kRsNoResolvable = 6;   /* 6: mmo3 fold 4 */
inline int RestoreGate(bool haveLine, int rule, unsigned long long markSeq, long long saveTime, unsigned long long recSeq,
                       long long recWrittenAt, bool bytesOk)
{
    if (!haveLine) return kRsAbsent;
    if (!OverlayCandidate(rule, markSeq, saveTime, recSeq, recWrittenAt)) return kRsOlder;
    if (!bytesOk) return kRsBad;
    return kRsRestore;
}
inline const char* RestoreWhyName(int w)
{
    switch (w)
    {
    case kRsRestore:    return "restore";
    case kRsAbsent:     return "no-record";
    case kRsOlder:      return "older-than-save";
    case kRsBad:        return "record-crc";
    case kRsDecode:     return "decode";
    case kRsNoFinished: return "no-finished-tech";
    case kRsNoResolvable: return "no-resolvable-tech";   /* mmo3 fold 4 */
    default:            return "?";
    }
}

/* ---- mmo3 fold (review-mmo3) ---------------------------------------------------------------------------------------
   1. A category whose restore was refused for a TRANSIENT reason keeps its (newer) record: its writes are HELD. The first
      read after the refusal is the baseline (the save's value); after a 60 s grace a read that differs from it is a real
      change and releases the hold (it is newer progress); the next load decides again. A FINAL refusal (the record is not
      newer, absent, of another world, a lone game, or unusable) writes as before. */
const unsigned int kHoldGraceMs = 60000;
inline bool RestoreRefusalFinal(const std::string& why)
{
    return why == "older-than-save" || why == "no-record" || why == "identity" || why == "single" || why == "record-crc"
        || why == "decode" || why == "no-finished-tech" || why == "no-resolvable-tech";
}
enum { kHoldSetBase = 0, kHoldSkip = 1, kHoldRelease = 2 };
inline int HeldWriteDecision(bool haveBase, unsigned int sinceHoldMs, unsigned int graceMs, unsigned int baseCrc, long long baseLen,
                             unsigned int crc, long long len)
{
    if (!haveBase) return kHoldSetBase;
    if (sinceHoldMs < graceMs) return kHoldSkip;
    return (crc == baseCrc && len == baseLen) ? kHoldSkip : kHoldRelease;
}
/* 2. a town's discovered / explored byte after the restore: never lower than it is live */
inline unsigned char MapMergeByte(unsigned char live, unsigned char rec) { return rec > live ? rec : live; }
/* 3. the restore runs only into a newly loaded world: the generation moved since the restore was armed at loadGame */
inline bool RestoreWorldChanged(long genAtArm, long genNow) { return genAtArm != genNow; }
/* 4. the tech stringIDs Research::load would try (finished<i>, i < num finished, non-empty), in order */
inline std::vector<std::string> ResearchFinishedSids(const ResearchRec& r)
{
    std::vector<std::string> out;
    float nf = 0.0f;
    for (size_t i = 0; i < r.f.size(); ++i) if (r.f[i].first == "num finished") nf = r.f[i].second;
    if (!(nf >= 1.0f)) return out;
    const int n = nf > 100000.0f ? 100000 : (int)nf;
    std::map<std::string, std::string> s;
    for (size_t i = 0; i < r.s.size(); ++i) s[r.s[i].first] = r.s[i].second;
    for (int i = 0; i < n; ++i)
    {
        char b[32]; std::sprintf(b, "finished%d", i);
        std::map<std::string, std::string>::const_iterator it = s.find(b);
        if (it != s.end() && !it->second.empty()) out.push_back(it->second);
    }
    return out;
}
/* restore only when at least one of them resolves to a RESEARCH record in this game (else the engine would fall back to
   setResearched("_Default Start") and toast) */
inline int ResearchRestoreVerdict(const ResearchRec& r, const std::set<std::string>& resolvable)
{
    const std::vector<std::string> sids = ResearchFinishedSids(r);
    if (sids.empty()) return kRsNoFinished;
    for (size_t i = 0; i < sids.size(); ++i) if (resolvable.count(sids[i]) != 0) return kRsRestore;
    return kRsNoResolvable;
}

/* decision M7, TEST only: "poke money <n>" (n a 32-bit signed integer) */
inline bool ParsePokeMoney(const std::string& arg, int* out)
{
    const std::string p = "poke money ";
    if (arg.size() <= p.size() || arg.compare(0, p.size(), p) != 0) return false;
    const std::string n = arg.substr(p.size());
    std::string::size_type i = 0;
    bool neg = false;
    if (n[0] == '-') { neg = true; i = 1; }
    if (i >= n.size() || n.size() - i > 10) return false;
    long long v = 0;
    for (; i < n.size(); ++i) { if (n[i] < '0' || n[i] > '9') return false; v = v * 10 + (long long)(n[i] - '0'); }
    if (neg) v = -v;
    if (v > 2147483647LL || v < -2147483647LL - 1LL) return false;
    *out = (int)v; return true;
}

/* ---- mmo6 (e47-mmo-design.md 1.3, 7 item 6): THE HAND-OVER LEDGER pp.ledger --------------------------------------------
   One line per completed cross-player hand-over, appended by each side BEFORE its squad is marked write-now, in the outage
   journal's line shape (queuejournal.h):  q1 <TAB> 16 <TAB> <peer> <TAB> <message id> <TAB> <unix time> <TAB> <payload-hex>
   <peer> is the other player's notebook slot ("p<n>", "p?" when unknown); the payload is "h1|in|<uid>|<qty>|<item sid>" or
   "h1|out|..." (our own character's uid; qty 0 = not known on this side). Family 16 is NOT an outage-queue family, so the queue's
   parser refuses a ledger line and this one refuses a queue line. The file keeps the last kLedgerCap lines. inv7c reads it. */
const int          kLedgerFamily = 16;
const unsigned int kLedgerCap = 512;
inline std::string LedgerFile() { return "pp.ledger"; }
/* mmo6 fold (review-mmo6): the line's direction and the writer's role. rev = the giver cancelled a give this game had built
   (the same message id as its IN line); the role lets inv7c pair the two sides' lines (message ids restart per launch). */
const int kLedgerOut = 0, kLedgerIn = 1, kLedgerRev = 2;
const int kLedgerRoleUnknown = 0, kLedgerRoleRequester = 1, kLedgerRoleOwner = 2;
struct LedgerEntry
{
    std::string  peer;      /* "p<slot>" of the other player, "p?" unknown */
    unsigned int msgId;     /* the hand-over's request id (the one both messages carry) */
    long long    atUnix;
    int          dirIn;     /* kLedgerIn the item came IN to our character, kLedgerOut it went OUT, kLedgerRev a cancelled IN */
    int          role;      /* mmo6 fold: kLedgerRole* - requester or owner of the hand-over (unknown on an h1 line) */
    unsigned int uid;       /* our own character */
    int          qty;       /* 0 = not known on this side */
    std::string  itemSid;   /* "" = not known on this side (the owner of a TAKE) */
    LedgerEntry() : msgId(0), atUnix(0), dirIn(0), role(0), uid(0), qty(0) {}   /* mmo6 fold: role */
};
inline std::string LedgerPeerKey(int slot)
{
    if (slot < 0) return "p?";
    std::string s = "p";
    coopqueue::QueueAppendUnsigned(&s, (unsigned long long)slot);
    return s;
}
/* 0 (and *out untouched) for an empty peer, a peer carrying TAB/CR/LF, a negative time or quantity. The newline IS included. */
inline int LedgerLineFormat(const LedgerEntry& e, std::string* out)
{
    if (out == 0 || e.peer.empty() || e.atUnix < 0 || e.qty < 0) return 0;
    for (std::string::size_type i = 0; i < e.peer.size(); ++i)
        if (e.peer[i] == '\t' || e.peer[i] == '\r' || e.peer[i] == '\n') return 0;
    if (e.dirIn < kLedgerOut || e.dirIn > kLedgerRev || e.role < kLedgerRoleUnknown || e.role > kLedgerRoleOwner) return 0;
    std::string p = "h2|";   /* mmo6 fold: h2 = direction, then role */
    p += (e.dirIn == kLedgerIn) ? "in|" : ((e.dirIn == kLedgerRev) ? "rev|" : "out|");
    p += (e.role == kLedgerRoleRequester) ? "req|" : ((e.role == kLedgerRoleOwner) ? "own|" : "unk|");
    coopqueue::QueueAppendUnsigned(&p, (unsigned long long)e.uid);
    p += '|';
    coopqueue::QueueAppendUnsigned(&p, (unsigned long long)e.qty);
    p += '|';
    p += e.itemSid;
    std::string s = "q1\t";
    coopqueue::QueueAppendUnsigned(&s, (unsigned long long)kLedgerFamily);
    s += '\t';
    s += e.peer;
    s += '\t';
    coopqueue::QueueAppendUnsigned(&s, (unsigned long long)e.msgId);
    s += '\t';
    coopqueue::QueueAppendUnsigned(&s, (unsigned long long)e.atUnix);
    s += '\t';
    for (std::string::size_type b = 0; b < p.size(); ++b)
    {
        const int byte = (int)(unsigned char)p[b];
        s += coopqueue::QueueHexDigit((byte >> 4) & 0x0F);
        s += coopqueue::QueueHexDigit(byte & 0x0F);
    }
    s += '\n';
    *out = s;
    return 1;
}
/* 0 on ANY malformed field (never half-read): the tag, six fields, family 16, a non-empty peer, the numbers, even hex, the
   payload's h1 tag and direction, and its two numbers. A trailing CR/LF is stripped. */
inline int LedgerLineParse(const std::string& line, LedgerEntry* out)
{
    if (out == 0) return 0;
    std::vector<std::string> f;
    std::string::size_type at = 0;
    for (;;)
    {
        const std::string::size_type tb = line.find('\t', at);
        if (tb == std::string::npos) { f.push_back(line.substr(at)); break; }
        f.push_back(line.substr(at, tb - at));
        at = tb + 1;
    }
    if (f.size() != 6 || f[0] != "q1") return 0;
    std::string& hx = f[5];
    while (!hx.empty() && (hx[hx.size() - 1] == '\r' || hx[hx.size() - 1] == '\n')) hx.erase(hx.size() - 1);
    unsigned long long fam = 0ULL, id = 0ULL, when = 0ULL;
    if (coopqueue::QueueParseUnsigned(f[1], &fam) == 0 || fam != (unsigned long long)kLedgerFamily) return 0;
    if (f[2].empty()) return 0;
    if (coopqueue::QueueParseUnsigned(f[3], &id) == 0 || id > 0xFFFFFFFFULL) return 0;
    if (coopqueue::QueueParseUnsigned(f[4], &when) == 0 || when > 0x7FFFFFFFFFFFFFFFULL) return 0;
    if (hx.empty() || (hx.size() % 2) != 0) return 0;
    std::string p;
    for (std::string::size_type i = 0; i + 1 < hx.size(); i += 2)
    {
        const int hi = coopqueue::QueueHexValue(hx[i]), lo = coopqueue::QueueHexValue(hx[i + 1]);
        if (hi < 0 || lo < 0) return 0;
        p += (char)((hi << 4) | lo);
    }
    int dirIn = kLedgerOut, role = kLedgerRoleUnknown;
    std::string::size_type k = 0;
    if (p.compare(0, 6, "h1|in|") == 0) { dirIn = kLedgerIn; k = 6; }          /* mmo6 (T440) lines: no role */
    else if (p.compare(0, 7, "h1|out|") == 0) { dirIn = kLedgerOut; k = 7; }
    else if (p.compare(0, 3, "h2|") == 0)                                          /* mmo6 fold */
    {
        const std::string::size_type d = p.find('|', 3);
        if (d == std::string::npos) return 0;
        const std::string dir = p.substr(3, d - 3);
        if (dir == "in") dirIn = kLedgerIn; else if (dir == "out") dirIn = kLedgerOut; else if (dir == "rev") dirIn = kLedgerRev; else return 0;
        const std::string::size_type rr = p.find('|', d + 1);
        if (rr == std::string::npos) return 0;
        const std::string ro = p.substr(d + 1, rr - d - 1);
        if (ro == "req") role = kLedgerRoleRequester; else if (ro == "own") role = kLedgerRoleOwner; else if (ro == "unk") role = kLedgerRoleUnknown; else return 0;
        k = rr + 1;
    }
    else return 0;
    const std::string::size_type b1 = p.find('|', k);
    if (b1 == std::string::npos) return 0;
    const std::string::size_type b2 = p.find('|', b1 + 1);
    if (b2 == std::string::npos) return 0;
    unsigned long long uid = 0ULL, qty = 0ULL;
    if (coopqueue::QueueParseUnsigned(p.substr(k, b1 - k), &uid) == 0 || uid > 0xFFFFFFFFULL) return 0;
    if (coopqueue::QueueParseUnsigned(p.substr(b1 + 1, b2 - b1 - 1), &qty) == 0 || qty > 0x7FFFFFFFULL) return 0;
    LedgerEntry e;
    e.peer = f[2]; e.msgId = (unsigned int)id; e.atUnix = (long long)when; e.dirIn = dirIn;
    e.uid = (unsigned int)uid; e.qty = (int)qty; e.itemSid = p.substr(b2 + 1); e.role = role;   /* mmo6 fold: role */
    *out = e;
    return 1;
}
/* THE CAP: only the last `cap` lines are kept (oldest dropped first). Returns how many were dropped. */
inline int LedgerKeepLast(std::vector<std::string>* lines, unsigned int cap)
{
    if (lines == 0 || lines->size() <= (size_t)cap) return 0;
    const size_t drop = lines->size() - (size_t)cap;
    lines->erase(lines->begin(), lines->begin() + (std::vector<std::string>::difference_type)drop);
    return (int)drop;
}
/* The file's text -> its lines that parse (without their newline); an empty line is skipped, a malformed one counted. */
inline int LedgerSplit(const std::string& text, std::vector<std::string>* good)
{
    int bad = 0;
    std::string cur;
    for (std::string::size_type i = 0; i <= text.size(); ++i)
    {
        if (i < text.size() && text[i] != '\n') { cur += text[i]; continue; }
        while (!cur.empty() && cur[cur.size() - 1] == '\r') cur.erase(cur.size() - 1);
        if (!cur.empty())
        {
            LedgerEntry e;
            if (LedgerLineParse(cur, &e) != 0) { if (good != 0) good->push_back(cur); } else ++bad;
        }
        cur.clear();
    }
    return bad;
}

/* mmo6 fold (review-mmo6 MED 4). THE APPEND'S VIEW OF THE FILE: every non-empty line is KEPT AS IT IS - a line this build
   cannot parse may be a later format, and dropping it at the next rewrite would lose it; *bad counts them. */
inline void LedgerSplitKeep(const std::string& text, std::vector<std::string>* all, int* bad)
{
    if (bad != 0) *bad = 0;
    std::string cur;
    for (std::string::size_type i = 0; i <= text.size(); ++i)
    {
        if (i < text.size() && text[i] != '\n') { cur += text[i]; continue; }
        while (!cur.empty() && cur[cur.size() - 1] == '\r') cur.erase(cur.size() - 1);
        if (!cur.empty())
        {
            LedgerEntry e;
            if (LedgerLineParse(cur, &e) == 0 && bad != 0) ++*bad;
            if (all != 0) all->push_back(cur);
        }
        cur.clear();
    }
}
/* ---- mmo8a (mmo8-buildings-read.md 5): pp.build - THE BUILDINGS THIS PLAYER OWNS ----------------------------------------
   One record for all of this profile's own pieces: every build-registry row with own=1, via!=3 (not a copy) and not
   removed. Per row: the piece's P7n key, its PLACE bytes and its STATE bytes exactly as buildwire.h EncodeBuild writes them
   (owner slot 0xFF = "the sender's own", the placement's nonce). help1 (protocol 90): the STATE bytes carry their optional helper tail
   (the last HELP_WORK seq applied per helper slot, the refused materials), so the confirmation survives in this record unchanged in form. "om8b" + int32 count + per row three strings (key, PLACE,
   STATE), sorted by key. The key is carried beside the PLACE so this header merges rows without the wire decoder.
   THE RECORD IS MERGED, NOT REWRITTEN FROM THE REGISTRY: the registry only knows the pieces of the zones this process has
   seen, so a row stays until its key is retired (its piece was taken down here). Pure: no disk, no clock, no engine. */
inline std::string BuildRecKey() { return "pp.build"; }
const unsigned int kBuildRecMaxKey = 63;        /* build.cpp kKeyCap less its NUL (buildwire.h kBuildMaxKey) */
const unsigned int kBuildRecMaxBytes = 4096;    /* one PLACE / STATE is a few hundred bytes */
struct BuildRow { std::string key, place, state; };
inline bool BuildRowLess(const BuildRow& a, const BuildRow& b) { return a.key < b.key; }
inline void EncodeBuildRec(const std::vector<BuildRow>& rows, std::vector<char>* out)
{
    std::vector<BuildRow> s(rows);
    std::sort(s.begin(), s.end(), BuildRowLess);
    out->clear(); RecPutStr(out, "om8b");
    RecPutI32(out, (int)s.size());
    for (size_t i = 0; i < s.size(); ++i) { RecPutStr(out, s[i].key); RecPutStr(out, s[i].place); RecPutStr(out, s[i].state); }
}
/* false (and *rows empty) on ANY bad field - never half-read: the tag, the count, an empty or over-long key / PLACE / STATE,
   a key twice, trailing bytes */
inline bool DecodeBuildRec(const std::vector<char>& b, std::vector<BuildRow>* rows)
{
    rows->clear();
    GdcCur c; RecCur(&c, b);
    if (!RecTag(&c, "om8b")) return false;
    const int n = GdcCount(&c, 12);
    if (c.bad) return false;
    std::set<std::string> seen;
    for (int i = 0; i < n; ++i)
    {
        BuildRow r;
        GdcStr(&c, &r.key); GdcStr(&c, &r.place); GdcStr(&c, &r.state);
        if (c.bad || r.key.empty() || r.key.size() > kBuildRecMaxKey || r.place.empty() || r.state.empty()
            || r.place.size() > kBuildRecMaxBytes || r.state.size() > kBuildRecMaxBytes || !seen.insert(r.key).second) { rows->clear(); return false; }
        rows->push_back(r);
    }
    if (!RecEnd(&c)) { rows->clear(); return false; }
    return true;
}
/* THE MERGE: out = the file's rows less every key in `retired`, with each live row replacing (or adding) its key. A key both
   live and retired is live (a new piece placed where a removed one stood). *retiredN = file rows dropped. Sorted by key. */
inline void MergeBuildRows(const std::vector<BuildRow>& file, const std::vector<BuildRow>& live, const std::set<std::string>& retired,
                           std::vector<BuildRow>* out, int* retiredN)
{
    std::set<std::string> liveKeys;
    for (size_t i = 0; i < live.size(); ++i) liveKeys.insert(live[i].key);
    std::map<std::string, BuildRow> m;
    int dropped = 0;
    for (size_t i = 0; i < file.size(); ++i)
    {
        if (retired.find(file[i].key) != retired.end() && liveKeys.find(file[i].key) == liveKeys.end()) { ++dropped; continue; }
        m[file[i].key] = file[i];
    }
    for (size_t i = 0; i < live.size(); ++i) m[live[i].key] = live[i];
    out->clear();
    for (std::map<std::string, BuildRow>::const_iterator it = m.begin(); it != m.end(); ++it) out->push_back(it->second);
    if (retiredN != 0) *retiredN = dropped;
}

/* ---- help1 fold (review MED 5/6): pp.help - THE HELPER'S OWN COUNTS --------------------------------------------------------
   One record for this player's help on OTHER players' pieces in this world: per owner key the owner's slot, the next HELP_WORK seq
   and, per material, the whole items already handed back (the owner's refused amounts). Kept here, not only in the loose owed file,
   so a lost or unreadable owed file never hands items back twice nor re-uses a seq. "omh1" + int32 count + per row: key, int32
   slot, int32 nextSeq, 16 x int32 given; sorted by key. Pure: no disk, no clock, no engine.
   help1 fold 2 (re-check LOW 5 / HIGH 1): written "omh2" - each row also int32 goneSeq (the entries at or below it were settled by a
   HELP_GONE and their materials handed back: an owed file read later never hands them back again) and int32 nonce (the owner's
   placement the row helped; 0 = unknown). An "omh1" record still reads (goneSeq 0, nonce 0).
   help1 fold 3 (finding 2): a key may carry one row per placement nonce (the rows of a replaced placement settle apart) - (key, nonce)
   is what must not repeat. */
inline std::string HelpRecKey() { return "pp.help"; }
const unsigned int kHelpRecMats = 16;   /* buildwire.h kBuildMaxMats */
const int kHelpRecSlotMax = 1023;        /* the notebook's slots 0..1023 (buildwire.h kBuildOwnerSlotMax) */
struct HelpRecRow
{
    std::string key;
    int slot;
    unsigned int nextSeq;
    int given[kHelpRecMats];
    unsigned int goneSeq;   /* help1 fold 2 (omh2) */
    unsigned int nonce;     /* help1 fold 2 (omh2) */
    HelpRecRow() : slot(-1), nextSeq(1), goneSeq(0), nonce(0) { for (unsigned int i = 0; i < kHelpRecMats; ++i) given[i] = 0; }
};
inline bool HelpRecRowLess(const HelpRecRow& a, const HelpRecRow& b) { return a.key < b.key; }
inline void EncodeHelpRec(const std::vector<HelpRecRow>& rows, std::vector<char>* out)
{
    std::vector<HelpRecRow> s(rows);
    std::sort(s.begin(), s.end(), HelpRecRowLess);
    out->clear(); RecPutStr(out, "omh2");
    RecPutI32(out, (int)s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        RecPutStr(out, s[i].key); RecPutI32(out, s[i].slot); RecPutI32(out, (int)s[i].nextSeq);
        for (unsigned int k = 0; k < kHelpRecMats; ++k) RecPutI32(out, s[i].given[k]);
        RecPutI32(out, (int)s[i].goneSeq); RecPutI32(out, (int)s[i].nonce);   /* help1 fold 2 */
    }
}
/* false (and *rows empty) on ANY bad field: the tag, the count, an empty or over-long key, a key twice, a slot outside -1..1023,
   seq 0, a negative count, trailing bytes */
inline bool DecodeHelpRec(const std::vector<char>& b, std::vector<HelpRecRow>* rows)
{
    rows->clear();
    GdcCur c; RecCur(&c, b);
    GdcCur c2; RecCur(&c2, b);
    const bool v2 = RecTag(&c2, "omh2");   /* help1 fold 2: omh2 carries goneSeq + nonce; omh1 still reads */
    if (v2) c = c2;
    else if (!RecTag(&c, "omh1")) return false;
    const int n = GdcCount(&c, (size_t)(4 + 4 + 4 + 4 * kHelpRecMats + (v2 ? 8 : 0)));
    if (c.bad) return false;
    std::set<std::pair<std::string, unsigned int> > seen;   /* help1 fold 3: one row per (key, nonce) */
    for (int i = 0; i < n; ++i)
    {
        HelpRecRow r;
        GdcStr(&c, &r.key);
        r.slot = GdcI32(&c);
        r.nextSeq = (unsigned int)GdcI32(&c);
        bool neg = false;
        for (unsigned int k = 0; k < kHelpRecMats; ++k) { r.given[k] = GdcI32(&c); if (r.given[k] < 0) neg = true; }
        if (v2) { r.goneSeq = (unsigned int)GdcI32(&c); r.nonce = (unsigned int)GdcI32(&c); }
        if (c.bad || r.key.empty() || r.key.size() > kBuildRecMaxKey || r.slot < -1 || r.slot > kHelpRecSlotMax || r.nextSeq == 0 || neg
            || !seen.insert(std::make_pair(r.key, r.nonce)).second) { rows->clear(); return false; }
        rows->push_back(r);
    }
    if (!RecEnd(&c)) { rows->clear(); return false; }
    return true;
}
/* a row from elsewhere (the record, the owed file, memory) joins a row: the higher nextSeq and given counts win - never lower. A row of
   ANOTHER owner slot at the same key (both slots known and different) is not joined: 0. 1 = joined. */
inline int HelpRecJoin(HelpRecRow* into, const HelpRecRow& from)
{
    if (into->slot >= 0 && from.slot >= 0 && into->slot != from.slot) return 0;
    if (into->nonce != 0 && from.nonce != 0 && into->nonce != from.nonce) return 0;   /* help1 fold 2: another placement at the key */
    if (into->slot < 0) into->slot = from.slot;
    if (into->nonce == 0) into->nonce = from.nonce;
    if (from.goneSeq > into->goneSeq) into->goneSeq = from.goneSeq;
    if (from.nextSeq > into->nextSeq) into->nextSeq = from.nextSeq;
    for (unsigned int k = 0; k < kHelpRecMats; ++k) if (from.given[k] > into->given[k]) into->given[k] = from.given[k];
    return 1;
}

/* mmo8a2 (review-mmo8a H1, M1, M2): the pure halves of build.cpp's world-scoped rows, the restore's settle and the retired keys
   owed to the record - swept by the offline suite. */
const int kBuildRowSkip = 0, kBuildRowLive = 1, kBuildRowRetire = 2;
/* H1: a registry row goes into pp.build only when it was registered (or last touched live) in the CURRENT world generation;
   an own row kept across a world unload names the world before's piece and is neither live nor retired in this one */
inline int BuildRowEmit(int own, int via, int removed, int dismantled, long rowGen, long curGen)
{
    if (own == 0 || via == 3 || rowGen != curGen) return kBuildRowSkip;
    if (removed != 0 || dismantled != 0) return kBuildRowRetire;
    return kBuildRowLive;
}
/* M2: own keys retired here and owed to the record (key -> the world generation of the removal). The registry's 60 s sweep
   does not clear it; a key leaves when the writer has taken a record that drops it (BuildRetireTaken), or with its world. */
inline void BuildRetireNote(std::map<std::string, long>* owed, const std::string& key, long gen) { (*owed)[key] = gen; }
inline void BuildRetireEmit(const std::map<std::string, long>& owed, long curGen, std::vector<std::string>* out)
{
    for (std::map<std::string, long>::const_iterator it = owed.begin(); it != owed.end(); ++it)
        if (it->second == curGen) out->push_back(it->first);
}
inline void BuildRetireTaken(std::map<std::string, long>* owed, const std::vector<std::string>& taken, long curGen)
{
    for (size_t i = 0; i < taken.size(); ++i)
    {
        std::map<std::string, long>::iterator it = owed->find(taken[i]);
        if (it != owed->end() && it->second == curGen) owed->erase(it);
    }
}

/* mmo6 fold (review-mmo6 MED 4): ONLY a ledger that does not exist is empty; one that exists and cannot be read REFUSES the
   append, because rewriting it from nothing would truncate every line it holds. */
const int kLedgerLoadEmpty = 0, kLedgerLoadUse = 1, kLedgerLoadRefuse = 2;
inline int LedgerLoadVerdict(bool notFound, bool readOk)
{
    if (notFound) return kLedgerLoadEmpty;
    return readOk ? kLedgerLoadUse : kLedgerLoadRefuse;
}

/* T-251 (H-OVL-COLD) - THE OWN OVERLAY'S CLAIM WINDOW CLOSES ON AN EVENT, WITH ONE CAP. At once when every published entry
   has been claimed AND its load has returned (done), else at a hard cap from the first gameplay frame, so a game left
   paused, or a squad that never comes through loadFromDisk, still closes it and is still logged BROKEN. The cap only runs
   while a first frame has been seen. entries <= done also covers entries == 0 (nothing to wait for). The old rule - a fixed
   10 s from the first frame - closed before a COLD load's squads came in (T596 life 3: close +10.0 s, squads +14.1 s). */
/* T-251 review fold M1: worldChanged = the store's world-reset count moved since the window's first gameplay frame (a new
   game or a quit to the title - only a LOAD's prepare reopens the window): the window closes SILENTLY (world-changed), ahead
   of every other rule - no BROKEN and no late check, because that load's squads say nothing about the new world. */
const int kOvWinOpen = 0, kOvWinAllClaimed = 1, kOvWinCap = 2, kOvWinWorldChanged = 3;
inline int OverlayWindowDecide(long entries, long done, bool firstFrameSeen, unsigned long sinceFirstFrameMs, unsigned long capMs,
                               bool worldChanged = false)
{
    if (worldChanged) return kOvWinWorldChanged;
    if (entries <= done) return kOvWinAllClaimed;
    if (firstFrameSeen && sinceFirstFrameMs >= capMs) return kOvWinCap;
    return kOvWinOpen;
}
inline const char* OverlayWindowReason(int r)
{
    return r == kOvWinWorldChanged ? "world-changed" : r == kOvWinAllClaimed ? "allClaimed" : r == kOvWinCap ? "cap" : "open";
}
/* T-251 review fold M2: an entry's state (OwnOvEntry::claimed, set once from Free by compare-exchange). A squad load that
   matches an entry's key but is TURNED DOWN (the engine would not read it: notLoading; the container's file name is not the
   squad's: nameMismatch) SETTLES the entry: never claimable again (a later reload cannot take the old record late) and done
   for the all-claimed close, its reason counted in the close lines (a settled squad was not laid over: BROKEN). */
const int kOvEntryFree = 0, kOvEntryClaimed = 1, kOvEntrySettledNotLoading = 2, kOvEntrySettledNameMismatch = 3;
inline bool OverlayEntryClaimable(long state) { return state == kOvEntryFree; }
inline bool OverlayEntryDone(long state, bool logged)
{
    return state == kOvEntrySettledNotLoading || state == kOvEntrySettledNameMismatch || (state == kOvEntryClaimed && logged);
}
/* The engine's load pause, for the window's log line: the pause flag RISES just after the first gameplay frame (T596 life 3:
   first frame 43.272, stopped 0->1 at 43.326), so a 0 read before any 1 is NOT the end of the load. The end is the first 0
   after a 1; -1 (not sampled this frame) changes nothing. True on that one edge only. */
inline bool OverlayLoadPauseEnded(int stopped, int* stopSeen, int* ended)
{
    if (stopped == 1) { *stopSeen = 1; return false; }
    if (stopped == 0 && *stopSeen != 0 && *ended == 0) { *ended = 1; return true; }
    return false;
}

}   /* namespace coopown */

#endif
