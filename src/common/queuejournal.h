/* src/common/queuejournal.h - B12 (decision 52): THE OUTAGE QUEUE'S TWO DECISIONS, AS PURE ARITHMETIC.
 *
 * Decision 44 stopped this game writing containers, doors and zone files while the notebook process is
 * unreachable, and said so out loud.  Decision 52 keeps the player's move instead of losing it: the write
 * that was refused is written down in a journal file beside the notebook's folder and re-issued when the
 * notebook comes back.  Two things in that road are decisions over small values rather than engine work, so
 * they live here and are swept offline by the same compiler that builds the plugin (src/coop-test).  The
 * plugin and the offline suite CALL these; neither carries a second copy of them (lesson 11).
 *
 * THE CONFLICT RULE.  A replay must LOSE to a record another writer changed during the outage, and the only
 * field that can order two writes is the notebook's own per-record sequence number (STORE-FILE format 7,
 * store protocol 41): writtenAt is whole seconds of wall clock stamped at WRITE time on two separate
 * machines, so a replayed record stamped at replay would always look newer and would silently win the
 * conflict it must lose (read-b12's top risk).  seqSeen is the seq this game last heard for that key; seqNow
 * is the seq in the notebook's index push-down that has just landed.  Equal means nobody wrote since, so the
 * incumbent is this game's own entry and the replay goes ahead; higher means somebody else did, so the entry
 * is dropped, counted and told; LOWER means the notebook has lost history (a restored folder, a wiped index),
 * which is never a licence to replay over it.
 *
 * THE JOURNAL LINE.  One entry per line, tab-separated, so a human can read the file and a corrupt line can
 * be dropped by itself rather than taking the file with it:
 *
 *   q1<TAB><family><TAB><key><TAB><seqSeen><TAB><queuedAtUnix><TAB><payload-hex>
 *
 * <family> is one of the kQueueFamily* numbers; <key> is the RECORD key the conflict is decided against
 * (a box move's key is its sector's zone record, because a box reaches the notebook only inside that record,
 * and a door has no notebook record at all so its key never resolves and its entries always replay);
 * <payload-hex> is the family's own saved write in lowercase hex, two characters per byte, empty for a
 * zero-byte payload.  Parse returns 0 on ANY malformed field - a line is never half-read and never guessed.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no C++11 anything.
 */
#ifndef COOP_COMMON_QUEUEJOURNAL_H
#define COOP_COMMON_QUEUEJOURNAL_H

#include <string>
#include <vector>

namespace coopqueue {

/* THE THREE FAMILIES THAT HAVE A PAYLOAD TO REPLAY.  Restock is deliberately absent: its write is an engine
   call on a live object that re-reads the shop at call time, there is no payload and there never was one, and
   a skipped restock re-asks on the next update pass - it is self-correcting and so is out of decision 52's
   scope by its own property (read-b12 Q2). */
const int kQueueFamilyBox   = 0;
const int kQueueFamilyDoor  = 1;
const int kQueueFamilyZone  = 2;
const int kQueueFamilyCount = 3;

/* B12-c (review-b12 M-1). WHICH FAMILIES SUPERSEDE PER KEY, AND WHY IT IS A PROPERTY OF THE FAMILY.
   A zone entry is a whole SAVED FILE and a door entry is a whole STATE: only the newest one for a key can
   be right, and keeping the older ones would replay a sector's contents twice and write the state a player
   has since changed. A BOX MOVE IS AN EVENT - remove eight iron plates, then add three - and the second
   does not describe the first, so every box entry is kept in the order it happened. */
inline int QueueFamilySupersedes(int family)
{
    return (family == kQueueFamilyDoor || family == kQueueFamilyZone) ? 1 : 0;
}

/* B12-c (review-b12 M-1). THE CAP. The journal is rewritten whole on every change and a zone payload is
   hex-doubled inside it, so an unbounded list turns a long outage into a growing stall on the main thread.
   256 entries and 4 MB are a session's refused writes with room to spare; past either, a write is REFUSED
   and the player is told, because a silent drop is the one thing decision 52 exists to remove. */
const int          kQueueMaxEntries = 256;
const unsigned int kQueueMaxBytes   = 4u * 1024u * 1024u;

inline int QueueFamilyValid(int family)
{
    return (family >= 0 && family < kQueueFamilyCount) ? 1 : 0;
}

/* ============================ THE CONFLICT RULE ============================ */
const int kReplay           = 0;   /* nobody wrote this key since it was queued - re-issue the saved write */
const int kDropNewerWriter  = 1;   /* another writer changed it meanwhile: the entry is lost, and SAID so */
const int kDropUnknown      = 2;   /* the notebook's seq went BACKWARD - it lost history; never replay */

/* hasRecordNow is 0 when the notebook's index holds no record for this key at all, and seqNow is then not
   read: a key with no record is seq 0, so an entry queued against no record (seqSeen 0) replays and an
   entry queued against a real seq finds its record gone and counts as kDropUnknown.
   B12-c (review-b12 M-6). THE TWO EXTRA INPUTS, AND THE HOLE THEY CLOSE. Two entries can name one key -
   a box move is an event and they are all kept - so the FIRST can replay, be accepted and advance the
   notebook's seq, and the SECOND then finds a seq higher than the one it saw and is dropped as a foreign
   writer's when the writer was THIS GAME, one entry earlier. `ownAhead` is set on an entry left in the
   journal behind another entry of the same key that replayed; `ownerIsMe` is whether the notebook's record
   for that key names THIS game's slot. With both, the seq difference is this game's own and the entry
   replays; with ownAhead set and the owner somebody else, a real writer got in and the entry loses exactly
   as before - the mark is never a licence on its own. */
inline int QueueReplayDecide(unsigned long long seqSeen, unsigned long long seqNow, int hasRecordNow,
                             int ownerIsMe, int ownAhead)
{
    const unsigned long long now = (hasRecordNow != 0) ? seqNow : 0ULL;
    if (ownAhead != 0 && ownerIsMe != 0 && now > seqSeen) return kReplay;
    if (now == seqSeen) return kReplay;
    return (now > seqSeen) ? kDropNewerWriter : kDropUnknown;
}

/* ============ B12-b: WHAT A SEQUENCE ECHO IS FOR, AND ITS ONE DECISION ============
   B12 as first built left a hole in its own conflict rule. The notebook stamps seq on every record it
   accepts, but it forwarded a game's own RECORD to the OTHER games unchanged and told the writer nothing -
   so a game's cached seq for a key IT had written was one behind until the next index push-down. A change
   queued later in the same session for that same key then compared a stale seqSeen against the notebook's
   real seq and was dropped as ANOTHER WRITER'S. It was conservative (nothing was ever written over) but it
   was still the player's own move thrown away, which is precisely what decision 52 exists to stop.
   The notebook now ECHOES the stamped number back to the writer, and only to the writer. The decision here
   is the whole of what the receiving game may do with it: a key it knows takes the number, and a key it
   does not know is COUNTED AND IGNORED - the echo carries no payload and may not create a record, because
   a record invented from a number has no position, no owner and no file. */
const int kSeqEchoApply      = 0;   /* this game holds a record for that key: take the number, and nothing else */
const int kSeqEchoUnknownKey = 1;   /* no record here for it - counted and ignored, never created */

inline int QueueSeqEchoDecide(int haveRecordHere)
{
    return (haveRecordHere != 0) ? kSeqEchoApply : kSeqEchoUnknownKey;
}

/* ============ B12-c (review-b12 H-1): A SEQUENCE OF ZERO CARRIES NO INFORMATION ============
   The notebook stamps every record it accepts, so a record that has BEEN accepted never has seq 0. A 0 on
   an arriving RECORD therefore means one of two things and neither is a fact about the record: the message
   came from the SESSION PEER (a game always sends 0 in that field - it may not choose the number that
   orders the folder's writes), or it came from a notebook one build behind. Writing it into this game's
   cached seq would reset the number to nothing and the next queued change for that key would be dropped as
   a foreign writer's - which is review-b12's H-1, measured on the relay's live forward. */
const int kSeqNoteApply       = 0;
const int kSeqNoteZeroIgnored = 1;

inline int QueueSeqNoteDecide(unsigned long long seqIn)
{
    return (seqIn == 0ULL) ? kSeqNoteZeroIgnored : kSeqNoteApply;
}

/* ============================ THE JOURNAL LINE ============================ */
struct QueueEntry
{
    int                family;       /* one of kQueueFamily* */
    std::string        key;          /* the RECORD key the conflict is decided against */
    unsigned long long seqSeen;      /* the seq this game last heard for that key when the write was refused */
    long long          queuedAtUnix; /* wall clock, whole seconds, for the readout only - it decides nothing */
    std::vector<char>  payload;      /* the family's own saved write; may be empty */
    int                ownAhead;     /* B12-c: 1 when an entry of this same key replayed ahead of this one */
    QueueEntry() : family(-1), seqSeen(0), queuedAtUnix(0), ownAhead(0) {}
};

inline char QueueHexDigit(int v)
{
    return (char)((v < 10) ? ('0' + v) : ('a' + (v - 10)));
}

/* -1 for a character that is not a lowercase or uppercase hex digit. */
inline int QueueHexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

inline void QueueAppendUnsigned(std::string* out, unsigned long long v)
{
    char buf[24];
    int n = 0;
    if (v == 0ULL) { buf[n++] = '0'; }
    while (v != 0ULL) { buf[n++] = (char)('0' + (int)(v % 10ULL)); v /= 10ULL; }
    while (n > 0) { *out += buf[--n]; }
}

/* Digits only, and the whole field or nothing.  An empty field, a sign, a space and a field that overflows
   a u64 are all REFUSED rather than read as a partial number: a seq read short is a conflict decided wrong. */
inline int QueueParseUnsigned(const std::string& f, unsigned long long* out)
{
    if (f.empty() || f.size() > 20) return 0;
    unsigned long long v = 0ULL;
    for (std::string::size_type i = 0; i < f.size(); ++i)
    {
        const char c = f[i];
        if (c < '0' || c > '9') return 0;
        const unsigned long long d = (unsigned long long)(c - '0');
        if (v > (0xFFFFFFFFFFFFFFFFULL - d) / 10ULL) return 0;
        v = v * 10ULL + d;
    }
    *out = v;
    return 1;
}

/* Returns 0 - and writes nothing - for a family this build does not know, an empty key, a key or a field
   carrying a TAB, a CR or an LF (the line is tab-separated and newline-terminated, so such a field would
   split into two fields or two lines and be read back as a different entry), or a negative queuedAtUnix.
   The trailing newline IS included in *out, so a caller concatenates entries and has the file. */
inline int QueueLineFormat(const QueueEntry& e, std::string* out)
{
    if (out == 0) return 0;
    if (QueueFamilyValid(e.family) == 0) return 0;
    if (e.key.empty() || e.queuedAtUnix < 0) return 0;
    for (std::string::size_type i = 0; i < e.key.size(); ++i)
    {
        const char c = e.key[i];
        if (c == '\t' || c == '\r' || c == '\n') return 0;
    }
    std::string s = "q1\t";
    QueueAppendUnsigned(&s, (unsigned long long)e.family);
    s += '\t';
    s += e.key;
    s += '\t';
    QueueAppendUnsigned(&s, e.seqSeen);
    s += '\t';
    QueueAppendUnsigned(&s, (unsigned long long)e.queuedAtUnix);
    s += '\t';
    for (std::vector<char>::size_type b = 0; b < e.payload.size(); ++b)
    {
        const int byte = (int)(unsigned char)e.payload[b];
        s += QueueHexDigit((byte >> 4) & 0x0F);
        s += QueueHexDigit(byte & 0x0F);
    }
    /* B12-c: THE SEVENTH FIELD IS WRITTEN ONLY WHEN IT IS SET. Absent means 0, so a journal written before
       B12-c parses unchanged and the ordinary line does not grow a field that says nothing. */
    if (e.ownAhead != 0) s += "\t1";
    s += '\n';
    *out = s;
    return 1;
}

/* Returns 0 on ANY malformed field, and *out is then not to be read.  A line with the wrong tag, the wrong
   number of fields, a family this build does not know, an empty key, a non-numeric seq or time, or a hex
   payload of odd length or with a non-hex character is one dropped and counted entry, never a guess.
   A trailing CR is stripped from the payload field, so a journal that has been through a text-mode round
   trip on one machine still parses on the other. */
inline int QueueLineParse(const std::string& line, QueueEntry* out)
{
    if (out == 0) return 0;
    std::vector<std::string> f;
    std::string::size_type at = 0;
    while (at <= line.size())
    {
        const std::string::size_type t = line.find('\t', at);
        if (t == std::string::npos) { f.push_back(line.substr(at)); break; }
        f.push_back(line.substr(at, t - at));
        at = t + 1;
    }
    if (f.size() != 6 && f.size() != 7) return 0;   /* B12-c: six fields, or seven with ownAhead */
    if (f[0] != "q1") return 0;
    {
        std::string& p = f[f.size() - 1];
        while (!p.empty() && (p[p.size() - 1] == '\r' || p[p.size() - 1] == '\n')) p.erase(p.size() - 1);
    }
    int ownAhead = 0;
    if (f.size() == 7)
    {
        if (f[6] == "1") ownAhead = 1;
        else if (f[6] != "0") return 0;   /* anything else is a line this build does not understand */
    }
    unsigned long long fam = 0ULL, seq = 0ULL, when = 0ULL;
    if (QueueParseUnsigned(f[1], &fam) == 0) return 0;
    if (QueueFamilyValid((int)fam) == 0) return 0;
    if (f[2].empty()) return 0;
    if (QueueParseUnsigned(f[3], &seq) == 0) return 0;
    if (QueueParseUnsigned(f[4], &when) == 0) return 0;
    if (when > 0x7FFFFFFFFFFFFFFFULL) return 0;
    if ((f[5].size() % 2) != 0) return 0;
    QueueEntry e;
    e.payload.reserve(f[5].size() / 2);
    for (std::string::size_type i = 0; i + 1 < f[5].size(); i += 2)
    {
        const int hi = QueueHexValue(f[5][i]);
        const int lo = QueueHexValue(f[5][i + 1]);
        if (hi < 0 || lo < 0) return 0;
        e.payload.push_back((char)(unsigned char)((hi << 4) | lo));
    }
    e.family = (int)fam;
    e.key = f[2];
    e.seqSeen = seq;
    e.queuedAtUnix = (long long)when;
    e.ownAhead = ownAhead;
    *out = e;
    return 1;
}

/* T-490 J0: TWO JOURNALS OF ONE WORLD MADE ONE. The journal replays in file order, and QueueLoad lets a later entry of a superseding
   family (door, zone) replace the earlier one for its key in place. So the OLDER journal's entries come first and the newer journal's
   after them, a newer superseding entry replacing the older one for its key in place (keeping its ownAhead fact) - the same rule
   QueueLoad applies to entries held before the index read. *superseded counts the replaced entries. */
inline std::vector<QueueEntry> QueueMergeOlderFirst(const std::vector<QueueEntry>& older, const std::vector<QueueEntry>& newer, long long* superseded)
{
    std::vector<QueueEntry> out(older);
    long long sup = 0;
    for (size_t h = 0; h < newer.size(); ++h)
    {
        bool replaced = false;
        if (QueueFamilySupersedes(newer[h].family) != 0)
        {
            for (size_t k = 0; k < out.size(); ++k)
            {
                if (out[k].family != newer[h].family || out[k].key != newer[h].key) continue;
                QueueEntry m = newer[h];
                if (out[k].ownAhead != 0) m.ownAhead = out[k].ownAhead;
                out[k] = m; ++sup; replaced = true;
                break;
            }
        }
        if (!replaced) out.push_back(newer[h]);
    }
    if (superseded != 0) *superseded = sup;
    return out;
}

}   /* namespace coopqueue */

#endif
