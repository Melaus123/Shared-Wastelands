/* M14 (T-197, more than two players, piece #2) - THE WORLD SERVER'S WRITE QUEUE, AS PURE BOOKKEEPING.
 *
 * SharedWastelandsServer.exe hands every record write to ONE writer thread (store_main.cpp, "M14 (T-197): THE WRITER
 * THREAD"), so its one loop never waits on a record's disk flushes. The two pieces that decide ORDER live
 * here - no Windows, no files, no threads - so the offline suite drives them directly (coop-test, m14_*):
 *
 *   KeyGate  LOOP SIDE. At most ONE job per record key is out (queued or being written) at any moment. A
 *            message for a key whose job is out is HELD, in arrival order, and handled only after that
 *            job's completion has been applied - so it is decided against the state the disk then holds,
 *            exactly as it was when every write finished before the next message was read.
 *   Fifo     SHARED. The jobs in the order they were queued, handed to the single writer ONE AT A TIME
 *            (a second job is not handed out while one is still being written), and a failed job is
 *            counted and followed by the next exactly as a successful one is.
 *
 * Together: two writes to one record's files are never in the queue at once, and they land in the order
 * their messages arrived; a later write can never land before an earlier one. The caller holds its own
 * lock around every Fifo call; KeyGate is touched by the loop thread only.
 */
#ifndef COOP_WRITEQUEUE_H
#define COOP_WRITEQUEUE_H

#include <cctype>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace coopwq {

/* M14 fold (review finding 8): THE GATE IS KEYED ON THE FILE, NOT ON THE ID. A record's files are named by
   FileName(id) (the world server's Sanitize in store_main.cpp and the game's in src/coop-plugin/store.cpp both call
   this very function), and Windows file names ignore case, so two ids that differ only in case, or only in an ASCII
   character FileName turns into '_', name ONE pair of files. GateKey = FileName, lower-cased: every id that lands on
   one file gets one gate key, so no two jobs for one file are ever out together. FileName's output is ASCII and its
   hex digits are upper-case, so lower-casing it joins exactly the names Windows joins and no others. */
inline bool FileNameKeeps(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
}
/* THE FILE NAME OF A RECORD ID. A-Z a-z 0-9 '-' '_' '.' are kept; any other ASCII byte becomes '_' (an English id keeps
   the name it has always had); every byte from 0x80 up - the bytes of a letter outside English in UTF-8, three for a
   Chinese letter - becomes '~' and its two upper-case hex digits. So two ids that differ in such a letter never share a
   file: the Chinese 'Shek Kingdom_14' and 'Dust Bandits_14' are two squads of two factions and get two names. '~' is never
   in a name FileNameBefore makes, so a name made here never equals a name an earlier build made for another id. */
/* A LONG NON-ENGLISH ID GETS A BOUNDED NAME. A byte from 0x80 up takes three characters, and paths are plain Windows paths
   (260 characters at most), so a Chinese id of 30-odd letters would make a path no file call can open. When an id holds such a
   byte and its whole name is longer than kFileNameMax, its name is the longest front part of the whole name that ends at a
   whole piece (one kept character or one '~XX') and leaves room for the tail, then "~H" and FileNameHash of the whole id in 8
   upper-case hex digits. 'H' is never a hex digit, so a bounded name never equals a whole one. kFileNameMax = 96 leaves 134
   characters for the folder beside the longest suffixes written (".platoon.prev" and a ".<process id>.tmp" write name); the
   default folder (%LOCALAPPDATA%\kenshi\Shared Wastelands\worlds\<world>) is about 70 to 100. Two long ids share a name only
   when their front parts and their 32-bit hashes both agree. An id with no byte from 0x80 up is never cut. */
const size_t kFileNameMax = 96;
const size_t kFileNameHashTail = 10;   /* "~H" + 8 hex digits */
/* FNV-1a, 32 bits (offset basis 2166136261, prime 16777619), over the id's bytes as they are. */
inline unsigned int FileNameHash(const std::string& s)
{
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < s.size(); ++i) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}
inline std::string FileName(const std::string& s)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string o;
    bool high = false;
    size_t cut = 0;   /* the longest front part, at a whole piece, that leaves room for the tail */
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c >= 0x80) { o += '~'; o += kHex[c >> 4]; o += kHex[c & 0x0F]; high = true; }
        else o += FileNameKeeps(c) ? (char)c : '_';
        if (o.size() <= kFileNameMax - kFileNameHashTail) cut = o.size();
    }
    if (!high || o.size() <= kFileNameMax) return o;
    const unsigned int h = FileNameHash(s);
    o.resize(cut);
    o += "~H";
    for (int k = 7; k >= 0; --k) o += kHex[(h >> (4 * k)) & 0x0F];
    return o;
}
/* True when `name`, a FileName result, is the bounded form. */
inline bool FileNameIsBounded(const std::string& name) { return name.find("~H") != std::string::npos; }
/* THE NAME AN EARLIER BUILD GAVE THE SAME ID: every byte outside the kept set, those from 0x80 up included, as '_'. Worlds
   those builds wrote still hold record files under it. For an ASCII id it IS FileName(id). */
inline std::string FileNameBefore(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) { const unsigned char c = (unsigned char)s[i]; o += FileNameKeeps(c) ? (char)c : '_'; }
    return o;
}
/* Two ASCII file names compared as Windows compares them: case ignored. */
inline bool FileNameSame(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
/* True when `stem` (a listed file's name without its suffix) is the name an earlier build gave `id` and not the name
   FileName gives it: a file left under its earlier name. Always false for an ASCII id (it has one name). */
inline bool EarlierNameOnly(const std::string& stem, const std::string& id)
{
    const std::string now = FileName(id), before = FileNameBefore(id);
    return !FileNameSame(now, before) && FileNameSame(stem, before) && !FileNameSame(stem, now);
}
/* True when `stem` can be an earlier name of an id that has two names: it holds '_' (every byte from 0x80 up was '_' under the
   earlier rule, so an id with ONE such byte, 'caf' + 0xE9 -> 'caf_', has an earlier name with no "__") and no '~' (never in an
   earlier name). The cheap name test only - EarlierStemNamed reads the stem's index lines and decides. */
inline bool EarlierNameMaybe(const std::string& stem)
{
    return stem.find('_') != std::string::npos && stem.find('~') == std::string::npos;
}
/* ONE PAIR OF A RECORD'S FILES LEFT UNDER AN EARLIER NAME (<stem>.meta + <stem>.platoon, or their two .prev copies): what a
   start does with it. wantId = the record the pair would be moved to; lineRead = the pair's index line was read and parsed;
   lineId = the id that line names; newThere = wantId already has that file under the name FileName gives.
   Moved ONLY when the line names wantId itself: under the earlier name ids that differ only in a non-English letter shared
   one pair of files, so a line naming another id means the bytes beside it may be that other record's - never handed to
   wantId. An unread line moves nothing (tried again at the next start), and a file already under the new name is never
   written over. */
enum { kEarlierNothing = 0, kEarlierCarry = 1, kEarlierUnread = 2, kEarlierOtherId = 3, kEarlierNewThere = 4, kEarlierNoLine = 5,
       kEarlierWithCurrent = 6 };   /* NoLine: read, but no index line in it; WithCurrent: stays with a current pair that did not move */
inline int EarlierNameDecide(const std::string& wantId, const std::string& stem, bool lineRead, const std::string& lineId, bool newThere)
{
    if (!lineRead) return kEarlierUnread;
    if (!EarlierNameOnly(stem, wantId)) return kEarlierNothing;
    if (lineId != wantId) return kEarlierOtherId;
    if (newThere) return kEarlierNewThere;
    return kEarlierCarry;
}
inline std::string GateKey(const std::string& id)
{
    std::string k = FileName(id);
    for (size_t i = 0; i < k.size(); ++i) if (k[i] >= 'A' && k[i] <= 'Z') k[i] = (char)(k[i] - 'A' + 'a');
    return k;
}
/* ============ RECORD FILES LEFT UNDER AN EARLIER NAME: WHAT MOVES WHERE, over any folder ============
   The world server runs this at its start over its world folder (store_main.cpp CarryEarlierNames); the offline suite runs it
   over a folder held in memory. Each index file is decided by THE ID ITS OWN LINE NAMES, never by the line beside it:
     <stem>.meta names A          -> A's current pair goes to FileName(A).meta / .platoon.
     <stem>.meta.prev names A too -> it goes behind it to FileName(A).meta.prev / .platoon.prev, only after A's current pair moved.
     <stem>.meta.prev names B     -> B wrote under the shared earlier name and A after it, and the writer's rotation left B's last
                                     whole version here: it goes to B's current names when both are free (B's live version),
                                     else to B's .prev names when both are free, else it is left.
     no <stem>.meta, or one holding no index line (empty, garbled) -> <stem>.meta.prev keeps its shape under its own id's .prev
                                     names, and <stem>.platoon goes with it to the current data name when its length and CRC-32
                                     are the ones that line names (a stop between the writer's two renames left the data the line
                                     describes under the current name). Both are shapes ChooseVersion recovers.
     <stem>.meta on disk and unreadable -> nothing of the stem moves; tried again at the next start.
   A data file always moves before its index line, so a stop between the two leaves the line under the earlier name and the next
   start finishes the move: when the line's data has already gone, it goes to the side whose data file has that line's length and
   CRC-32. Nothing is ever written over: a taken name refuses the move. */
enum { kEarlierFileAbsent = 0, kEarlierFileUnreadable = 1, kEarlierFileReadable = 2 };
struct EarlierLineFacts
{
    int          state;     /* kEarlierFile* */
    bool         parsed;    /* readable, and its first line is an index line */
    std::string  id;        /* the id that line names */
    int          version;   /* the line's format; len and crc mean something from 5 up */
    long long    len;
    unsigned int crc;
    EarlierLineFacts() : state(kEarlierFileAbsent), parsed(false), version(0), len(0), crc(0) {}
};
struct EarlierMove
{
    std::string from, to;   /* file names inside the folder */
    EarlierMove(const std::string& f, const std::string& t) : from(f), to(t) {}
};
/* One pair's outcome. what = kEarlier*; prev = the .meta.prev pair; asLive = a .prev pair carried as its record's current
   version; afterCurrent = it moves only when the current pair before it moved; id = the id its own line names; otherId = the
   id the current line beside it names, when that is another record. */
struct EarlierStep
{
    int what; bool prev; bool asLive; bool afterCurrent;
    std::string id, otherId;
    std::vector<EarlierMove> moves;   /* in order: data first, index line last */
    EarlierStep() : what(kEarlierNothing), prev(false), asLive(false), afterCurrent(false) {}
};
/* What the plan asks of the folder, and how each outcome is done and said. */
struct EarlierFolder
{
    virtual ~EarlierFolder() {}
    virtual bool Has(const std::string& name) = 0;                                       /* a file of this name is there */
    virtual bool Matches(const std::string& name, long long len, unsigned int crc) = 0;  /* readable, with that length and CRC-32 */
    virtual EarlierLineFacts Line(const std::string& name) = 0;                         /* an index file's state and first line */
    virtual bool Move(const std::string& from, const std::string& to, std::string* why) = 0;   /* never over a taken name */
    virtual void Said(const std::string& stem, const EarlierStep& s, bool moved, const std::string& why) = 0;
};
/* The file `name` holds the data the line `l` describes. */
inline bool EarlierDataIs(EarlierFolder* f, const std::string& name, const EarlierLineFacts& l)
{
    return l.parsed && l.version >= 5 && f->Matches(name, l.len, l.crc);
}
/* A data file `from` under the earlier name may go to `to`: `to` is free, or `from` is gone and `to` holds the data the line `l`
   describes (an earlier start moved the data and stopped before the line). */
inline bool EarlierDataFree(EarlierFolder* f, const std::string& from, const std::string& to, const EarlierLineFacts& l)
{
    if (!f->Has(to)) return true;
    return !f->Has(from) && EarlierDataIs(f, to, l);
}
inline std::vector<EarlierStep> EarlierPlan(const std::string& stem, EarlierFolder* f)
{
    std::vector<EarlierStep> out;
    const EarlierLineFacts cur = f->Line(stem + ".meta"), prev = f->Line(stem + ".meta.prev");
    if (cur.state == kEarlierFileUnreadable) { EarlierStep s; s.what = kEarlierUnread; out.push_back(s); return out; }   /* its .prev pair stays with it */
    const bool curLine = cur.state == kEarlierFileReadable && cur.parsed;
    int curWhat = kEarlierNothing;
    if (curLine)
    {
        const std::string to = FileName(cur.id);
        EarlierStep s; s.id = cur.id;
        /* every name the pair would take must be free: a .platoon under the new name with no .meta beside it is another file's
           data, unless it is this line's own (its length and CRC-32) that an earlier start moved there before it stopped */
        const bool taken = f->Has(to + ".meta") || !EarlierDataFree(f, stem + ".platoon", to + ".platoon", cur);
        s.what = EarlierNameDecide(cur.id, stem, true, cur.id, taken);
        if (s.what == kEarlierCarry)
        {
            if (f->Has(stem + ".platoon")) s.moves.push_back(EarlierMove(stem + ".platoon", to + ".platoon"));
            s.moves.push_back(EarlierMove(stem + ".meta", to + ".meta"));
        }
        curWhat = s.what;
        if (s.what != kEarlierNothing) out.push_back(s);
    }
    if (prev.state == kEarlierFileAbsent) return out;
    EarlierStep p; p.prev = true;
    if (prev.state == kEarlierFileUnreadable || !prev.parsed)
    {
        if (curLine && curWhat == kEarlierNothing) return out;   /* beside a current line under its own name: that file's own copy */
        p.what = (prev.state == kEarlierFileUnreadable) ? kEarlierUnread : kEarlierNoLine;
        out.push_back(p);
        return out;
    }
    p.id = prev.id;
    if (!EarlierNameOnly(stem, prev.id)) return out;   /* not an earlier name of the id its own line names */
    const std::string to = FileName(prev.id);
    const bool data = f->Has(stem + ".platoon.prev");
    if (curLine && cur.id == prev.id)
    {
        if (curWhat != kEarlierCarry) { p.what = kEarlierWithCurrent; out.push_back(p); return out; }
        p.afterCurrent = true;
        p.what = (f->Has(to + ".meta.prev") || !EarlierDataFree(f, stem + ".platoon.prev", to + ".platoon.prev", prev))
               ? kEarlierNewThere : kEarlierCarry;   /* both its names free, as for the current pair */
        if (p.what == kEarlierCarry)
        {
            if (data) p.moves.push_back(EarlierMove(stem + ".platoon.prev", to + ".platoon.prev"));
            p.moves.push_back(EarlierMove(stem + ".meta.prev", to + ".meta.prev"));
        }
    }
    else if (curLine)
    {
        p.otherId = cur.id;
        int side = 0;   /* 1 = its record's current names, 2 = its record's .prev names */
        if (!data)
        {
            if (!f->Has(to + ".meta") && EarlierDataIs(f, to + ".platoon", prev)) side = 1;
            else if (!f->Has(to + ".meta.prev") && EarlierDataIs(f, to + ".platoon.prev", prev)) side = 2;
        }
        if (side == 0 && !f->Has(to + ".meta") && !f->Has(to + ".platoon")) side = 1;
        if (side == 0 && !f->Has(to + ".meta.prev") && !f->Has(to + ".platoon.prev")) side = 2;
        p.what = (side == 0) ? kEarlierNewThere : kEarlierCarry;
        p.asLive = (side == 1);
        if (side != 0)
        {
            const std::string tail = (side == 1) ? std::string() : std::string(".prev");
            if (data) p.moves.push_back(EarlierMove(stem + ".platoon.prev", to + ".platoon" + tail));
            p.moves.push_back(EarlierMove(stem + ".meta.prev", to + ".meta" + tail));
        }
    }
    else
    {
        /* the crash shape's .prev line describes the data under the CURRENT name, so a .platoon.prev already under the new name is
           this stem's own only when it went ahead of the line: none left here, and the line's data under the new current name */
        const bool prevDataTaken = f->Has(to + ".platoon.prev")
                                 && (data || !(EarlierDataIs(f, to + ".platoon", prev) && !f->Has(stem + ".platoon")));
        p.what = (f->Has(to + ".meta.prev") || prevDataTaken) ? kEarlierNewThere : kEarlierCarry;
        if (p.what == kEarlierCarry)
        {
            if (f->Has(stem + ".platoon") && !f->Has(to + ".platoon") && EarlierDataIs(f, stem + ".platoon", prev))
                p.moves.push_back(EarlierMove(stem + ".platoon", to + ".platoon"));
            if (data) p.moves.push_back(EarlierMove(stem + ".platoon.prev", to + ".platoon.prev"));
            p.moves.push_back(EarlierMove(stem + ".meta.prev", to + ".meta.prev"));
        }
    }
    out.push_back(p);
    return out;
}
struct EarlierTotals
{
    long long carried, left;   /* pairs moved / pairs looked at and left where they are */
    EarlierTotals() : carried(0), left(0) {}
};
/* One stem's plan, done in order and said. Every name a pair would take is checked before its first move, and when a later
   move of the pair is refused the moves before it are put back: a pair moves whole or not at all, so its data and its line are
   never split between the two names (a split would be refused again at every start and never heal). */
inline void EarlierCarryStem(const std::string& stem, EarlierFolder* f, EarlierTotals* t)
{
    const std::vector<EarlierStep> steps = EarlierPlan(stem, f);
    bool lastMoved = false;
    for (size_t i = 0; i < steps.size(); ++i)
    {
        EarlierStep s = steps[i];
        if (s.what == kEarlierCarry && s.afterCurrent && !lastMoved) { s.what = kEarlierWithCurrent; s.moves.clear(); }
        bool moved = (s.what == kEarlierCarry);
        std::string why;
        for (size_t k = 0; moved && k < s.moves.size(); ++k)
            if (f->Has(s.moves[k].to)) { moved = false; why = s.moves[k].to + " is already taken - nothing of this pair was moved"; }
        size_t made = 0;
        while (moved && made < s.moves.size())
        {
            if (f->Move(s.moves[made].from, s.moves[made].to, &why)) ++made;
            else moved = false;
        }
        if (!moved && made > 0)
        {
            bool back = true;
            while (made > 0)
            {
                --made;
                std::string w;
                if (!f->Move(s.moves[made].to, s.moves[made].from, &w)) { back = false; why += "; putting it back: " + w; }
            }
            why += back ? std::string("; the files this pair had moved were put back under the earlier name")
                        : std::string("; NOT every file this pair had moved could be put back");
        }
        if (moved) ++t->carried; else ++t->left;
        f->Said(stem, s, moved, why);
        lastMoved = moved;
    }
}
/* True when an index line of the stem (<stem>.meta or <stem>.meta.prev) names an id whose earlier name the stem is and whose new
   name it is not. The ONE test: the carry-over looks at a stem by it, and the server's two index walks and the game's LoadIndex
   pass a file over by the same EarlierNameOnly on that file's own line - so every file a walk passes over is in a stem the
   carry-over moved or said. A stem with no such line (English, empty, garbled or unreadable) is not looked at and not said. */
inline bool EarlierStemNamed(const std::string& stem, EarlierFolder* f)
{
    const EarlierLineFacts cur = f->Line(stem + ".meta"), prev = f->Line(stem + ".meta.prev");
    return (cur.parsed && EarlierNameOnly(stem, cur.id)) || (prev.parsed && EarlierNameOnly(stem, prev.id));
}
/* names = the folder's file names. Each stem of a <stem>.meta or <stem>.meta.prev that EarlierNameMaybe passes is read once (case
   ignored, as Windows does) and looked at only when EarlierStemNamed holds. */
inline EarlierTotals EarlierCarryAll(const std::vector<std::string>& names, EarlierFolder* f)
{
    EarlierTotals t;
    std::vector<std::string> stems; std::set<std::string> seen;
    for (size_t i = 0; i < names.size(); ++i)
    {
        const std::string& n = names[i];
        std::string stem;
        if (n.size() > 10 && FileNameSame(n.substr(n.size() - 10), ".meta.prev")) stem = n.substr(0, n.size() - 10);
        else if (n.size() > 5 && FileNameSame(n.substr(n.size() - 5), ".meta")) stem = n.substr(0, n.size() - 5);
        else continue;
        if (EarlierNameMaybe(stem) && seen.insert(GateKey(stem)).second && EarlierStemNamed(stem, f)) stems.push_back(stem);
    }
    for (size_t i = 0; i < stems.size(); ++i) EarlierCarryStem(stems[i], f, &t);
    return t;
}

/* M14 fold (review finding 3): WHAT A HELD MESSAGE IS, for the one merge the gate makes. A POSITION-ONLY update
   held right behind another position-only update for the same key, from the same connection, with a strictly
   NEWER stamp, REPLACES it: handled in turn, the older would be written and then overwritten by the newer (the
   loop refuses a position whose stamp is not newer than the record's, so an equal or older stamp is never
   merged - it is held and decided as before). A full record, a delete, another game, a new connection in the
   same slot or anything held in between is never merged - those are appended, in arrival order. */
enum { kHeldOther = 0, kHeldPosition = 1 };
struct HeldTag
{
    int kind; const void* sender; unsigned connect; long long stamp; long long bytes;
    double at;   /* M16: when it was held (the loop's clock, seconds); below 0 = never expires */
    std::string id;   /* M14 fold 2 (re-check C): the RECORD id - two ids can share one gate key ('Ab' / 'ab') */
    HeldTag() : kind(kHeldOther), sender(0), connect(0), stamp(0), bytes(0), at(-1.0) {}
};
/* M14 fold 2 (re-check C): the gate key is shared by every id that lands on one file, so a merge also needs the
   SAME record id - a position for 'ab' never replaces one held for 'Ab'. */
inline bool HeldMergeable(const HeldTag& older, const HeldTag& newer)
{
    return older.kind == kHeldPosition && newer.kind == kHeldPosition && older.sender == newer.sender
        && older.connect == newer.connect && older.id == newer.id && newer.stamp > older.stamp;
}

template <class Item>
class KeyGate
{
public:
    KeyGate() : m_held(0), m_heldMax(0), m_bytes(0), m_merged(0), m_expired(0) {}
    bool Busy(const std::string& key) const { return m_busy.find(key) != m_busy.end(); }
    /* False when the key already has a job out: the caller must HOLD its message instead. A second job for
       one key is never started - that is the whole of the per-file ordering rule on the loop's side. */
    bool Start(const std::string& key) { return m_busy.insert(key).second; }
    void Finish(const std::string& key) { m_busy.erase(key); }
    bool Hold(const std::string& key, const Item& item) { return Hold(key, item, HeldTag()); }
    /* True when the item REPLACED the newest one held for this key (HeldMergeable) instead of being appended. */
    bool Hold(const std::string& key, const Item& item, const HeldTag& tag)
    {
        std::deque<Entry>& q = m_wait[key];
        if (!q.empty() && HeldMergeable(q.back().tag, tag))
        {
            m_bytes += tag.bytes - q.back().tag.bytes;
            q.back().item = item; q.back().tag = tag;
            ++m_merged;
            return true;
        }
        Entry e; e.item = item; e.tag = tag;
        q.push_back(e);
        ++m_held; m_bytes += tag.bytes;
        if (m_held > m_heldMax) m_heldMax = m_held;
        return false;
    }
    /* The OLDEST held item for a key, and only while that key has no job out. */
    bool TakeHeld(const std::string& key, Item* out)
    {
        if (Busy(key)) return false;
        typename std::map<std::string, std::deque<Entry> >::iterator it = m_wait.find(key);
        if (it == m_wait.end() || it->second.empty()) return false;
        *out = it->second.front().item;
        m_bytes -= it->second.front().tag.bytes;
        it->second.pop_front();
        --m_held;
        if (it->second.empty()) m_wait.erase(it);
        return true;
    }
    /* M16 (T-197): THE ONE DROP THE HELD LIST MAKES. A held POSITION-ONLY update (kHeldPosition - the class its owner
       sends again: its next position update or its next full record supersedes it) held since before `cutoff` is
       removed and COUNTED (Expired); the rest keep their order. Any other kind - a full record, a delete - is never
       removed here: it waits, and the loop's backpressure (kHeldMax / kHeldBytesMax) bounds how many wait. */
    long long ExpireHeld(int kind, double cutoff)
    {
        if (kind != kHeldPosition) return 0;
        long long n = 0;
        typename std::map<std::string, std::deque<Entry> >::iterator it = m_wait.begin();
        while (it != m_wait.end())
        {
            std::deque<Entry> keep;
            for (size_t i = 0; i < it->second.size(); ++i)
            {
                const Entry& e = it->second[i];
                if (e.tag.kind == kHeldPosition && e.tag.at >= 0.0 && e.tag.at < cutoff) { m_bytes -= e.tag.bytes; --m_held; ++n; }
                else keep.push_back(e);
            }
            it->second.swap(keep);
            if (it->second.empty()) m_wait.erase(it++); else ++it;
        }
        m_expired += n;
        return n;
    }
    long long Expired() const { return m_expired; }        /* M16: held position updates expired, since start */
    long long BusyCount() const { return (long long)m_busy.size(); }
    long long HeldCount() const { return m_held; }
    long long HeldBytes() const { return m_bytes; }       /* the held items' tag.bytes, summed */
    long long Merged() const { return m_merged; }          /* position updates that replaced an older held one, since start */
    /* The most held at once since the previous call (one report line), restarting from now. */
    long long TakeHeldMax() { const long long m = m_heldMax; m_heldMax = m_held; return m; }
private:
    struct Entry { Item item; HeldTag tag; };
    std::set<std::string> m_busy;
    std::map<std::string, std::deque<Entry> > m_wait;
    long long m_held, m_heldMax, m_bytes, m_merged, m_expired;
};

/* M14 fold 2 (re-check A): THE DEFERRED EVENTS. While backpressure is on, the loop keeps servicing the network so
   every game's link stays alive, and each event it takes waits here, unhandled: handed out in arrival order, all
   of them, before anything newly read. HasFor names a connection slot that still has events waiting (a new
   connection into that slot must not be answered with an older one's replies). Discard removes one slot's
   events - ONLY for a connection the loop itself has closed (reset, no event), as the network layer would have
   thrown them away; the others keep their order. */
template <class Ev>
class DeferQueue
{
public:
    DeferQueue() : m_bytes(0), m_max(0), m_total(0) {}
    void Push(const Ev& e, const void* slot, long long bytes)
    {
        Entry x; x.ev = e; x.slot = slot; x.bytes = bytes;
        m_q.push_back(x);
        ++m_slot[slot]; m_bytes += bytes; ++m_total;
        if ((long long)m_q.size() > m_max) m_max = (long long)m_q.size();
    }
    bool Pop(Ev* out)
    {
        if (m_q.empty()) return false;
        *out = m_q.front().ev;
        Forget(m_q.front());
        m_q.pop_front();
        return true;
    }
    /* Every event of one slot removed (the rest in order); the removed events are given to `out` (their
       packets are the caller's to free). Returns how many. */
    long long Discard(const void* slot, std::deque<Ev>* out)
    {
        long long n = 0;
        std::deque<Entry> keep;
        for (size_t i = 0; i < m_q.size(); ++i)
        {
            if (m_q[i].slot == slot) { if (out != 0) out->push_back(m_q[i].ev); Forget(m_q[i]); ++n; }
            else keep.push_back(m_q[i]);
        }
        m_q.swap(keep);
        return n;
    }
    bool HasFor(const void* slot) const { return m_slot.find(slot) != m_slot.end(); }
    long long Count() const { return (long long)m_q.size(); }
    long long Bytes() const { return m_bytes; }
    long long Total() const { return m_total; }           /* every event deferred, since start */
    /* The most waiting at once since the previous call (one report line), restarting from now. */
    long long TakeMax() { const long long m = m_max; m_max = (long long)m_q.size(); return m; }
private:
    struct Entry { Ev ev; const void* slot; long long bytes; };
    void Forget(const Entry& x)
    {
        m_bytes -= x.bytes;
        std::map<const void*, long long>::iterator it = m_slot.find(x.slot);
        if (it != m_slot.end() && --it->second <= 0) m_slot.erase(it);
    }
    std::deque<Entry> m_q;
    std::map<const void*, long long> m_slot;
    long long m_bytes, m_max, m_total;
};

template <class Job>
class Fifo
{
public:
    Fifo() : m_inHand(0), m_maxDepth(0), m_jobs(0), m_failed(0) {}
    void Push(const Job& j)
    {
        m_q.push_back(j);
        const long long d = Depth();
        if (d > m_maxDepth) m_maxDepth = d;
    }
    /* ONE AT A TIME: nothing is handed out while the previous job is still in hand, so the writer cannot
       start job N+1 before job N has finished - even if a second writer were ever added by mistake. */
    bool Pop(Job* out)
    {
        if (m_inHand || m_q.empty()) return false;
        *out = m_q.front();
        m_q.pop_front();
        m_inHand = 1;
        return true;
    }
    /* A failure is COUNTED and changes nothing else: the next Pop hands out the next job as after a success. */
    void Done(bool ok) { m_inHand = 0; ++m_jobs; if (!ok) ++m_failed; }
    long long Depth() const { return (long long)m_q.size() + m_inHand; }   /* queued + the one being written */
    bool Idle() const { return Depth() == 0; }
    long long Jobs() const { return m_jobs; }
    long long Failed() const { return m_failed; }
    /* The deepest the queue has been since the previous call (one report line), restarting from now. */
    long long TakeMaxDepth() { const long long m = m_maxDepth; m_maxDepth = Depth(); return m; }
private:
    std::deque<Job> m_q;
    long long m_inHand, m_maxDepth, m_jobs, m_failed;
};

/* THE FINAL SAVE AT QUIT (rule 3): the two files the main loop rewrites whole on its once-a-second tick - the teams' standing
   records (teams.txt) and the world-relations table (world_relations.txt) - are saved once more when the process is asked to
   quit, before the write queue drains. They belong to the main loop's thread, so the save runs there: at once when the quit
   came on that thread (between start-up steps, or at the top of a loop pass); asked of the loop, and waited for, when it came on
   another thread (the console's quit events, the session-end window, the parent Kenshi's watch); and not at all while the
   loop has not started (start-up is still loading on that thread and no game has sent anything). */
enum { kQuitSaveHere = 0, kQuitSaveAskLoop = 1, kQuitSaveNone = 2 };
inline int QuitSaveRoute(bool onLoopThread, bool loopRunning) { return onLoopThread ? kQuitSaveHere : loopRunning ? kQuitSaveAskLoop : kQuitSaveNone; }
/* one file is saved at quit when it holds changes not yet on disk - unless it was on disk and unreadable at start, so it is
   never rewritten this session */
inline bool QuitSaveFile(bool dirty, bool loadDeferred) { return dirty && !loadDeferred; }

}   /* namespace coopwq */

#endif
