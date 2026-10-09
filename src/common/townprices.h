/* THE WORLD'S TOWN PRICE TABLES (T-619) AND THE HOLDER'S PRICE-LEG TEST. Pure: no engine, no Windows.
   Each town's per-item local trade multiplier is rolled at random by each game's own engine on every load and never saved, so two
   games quote different prices at one counter. The world's ONE price source (PriceSource: the lowest IN_WORLD slot on the world
   server's roster whenever a roster is read, the session host only with no roster) sends its numbers to every other game, one
   message per town, each game by one road (the session link to its session peer, the world server to the rest), and every other
   game answers Town::getLocalTradePriceMult from them (HookUsesBook, Choose).
   Wire (little-endian): u32 tag 'TPR1' | u32 the sender's world generation | u8 len + the town's stringID | u16 n
                         | n x { u8 len + the item's stringID | u32 the f32's bits }. */
#ifndef TOWNPRICES_H
#define TOWNPRICES_H
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace townprice {

const unsigned int kTag = 0x31525054u;   /* 'TPR1' little-endian */
const int kMaxRows = 4096;               /* items in one town's table */
const int kMaxSid = 255;                 /* a stringID's bytes (u8 length) */

struct Table
{
    unsigned int gen;                                     /* the sender's world generation the table belongs to */
    std::string town;                                     /* the town's stringID */
    std::vector<std::pair<std::string, float> > rows;     /* item stringID -> the multiplier */
    Table() : gen(0) {}
};

inline void PutU32(std::vector<char>* b, unsigned int v) { for (int i = 0; i < 4; ++i) b->push_back((char)(unsigned char)((v >> (8 * i)) & 0xFFu)); }
inline int GetU32(const char* p, size_t size, size_t* at, unsigned int* v)
{
    if (p == 0 || *at + 4 > size) return 0;
    unsigned int r = 0;
    for (int i = 0; i < 4; ++i) r |= ((unsigned int)(unsigned char)p[*at + i]) << (8 * i);
    *v = r; *at += 4;
    return 1;
}
inline int PutSid(std::vector<char>* b, const std::string& s)
{
    if (s.empty() || s.size() > (size_t)kMaxSid) return 0;
    b->push_back((char)(unsigned char)s.size());
    b->insert(b->end(), s.begin(), s.end());
    return 1;
}
inline int GetSid(const char* p, size_t size, size_t* at, std::string* s)
{
    if (p == 0 || *at + 1 > size) return 0;
    const size_t n = (unsigned char)p[*at];
    if (n == 0 || *at + 1 + n > size) return 0;
    s->assign(p + *at + 1, n);
    *at += 1 + n;
    return 1;
}
inline unsigned int FloatBits(float f) { unsigned int u = 0; std::memcpy(&u, &f, 4); return u; }
inline float BitsFloat(unsigned int u) { float f = 0.0f; std::memcpy(&f, &u, 4); return f; }
/* a multiplier an engine could hold: a number, above 0 and below 100 */
inline int ValueOk(float v) { return (v == v && v > 0.0f && v < 100.0f) ? 1 : 0; }

/* 1 = appended; 0 = refused (no town, no rows, too many rows, a bad stringID or value) and nothing appended */
inline int Encode(std::vector<char>* b, const Table& t)
{
    if (t.rows.empty() || t.rows.size() > (size_t)kMaxRows) return 0;
    std::vector<char> w;
    PutU32(&w, kTag); PutU32(&w, t.gen);
    if (!PutSid(&w, t.town)) return 0;
    w.push_back((char)(unsigned char)(t.rows.size() & 0xFFu)); w.push_back((char)(unsigned char)((t.rows.size() >> 8) & 0xFFu));
    for (size_t i = 0; i < t.rows.size(); ++i)
    {
        if (!PutSid(&w, t.rows[i].first) || !ValueOk(t.rows[i].second)) return 0;
        PutU32(&w, FloatBits(t.rows[i].second));
    }
    b->insert(b->end(), w.begin(), w.end());
    return 1;
}
/* 1 = the whole payload is exactly one table; 0 = cut, trailing bytes, another tag, a bad count / stringID / value (refused whole) */
inline int Decode(const char* p, size_t size, Table* t)
{
    size_t a = 0; unsigned int tag = 0, gen = 0;
    if (!GetU32(p, size, &a, &tag) || tag != kTag || !GetU32(p, size, &a, &gen)) return 0;
    Table out; out.gen = gen;
    if (!GetSid(p, size, &a, &out.town) || a + 2 > size) return 0;
    const size_t n = (size_t)(unsigned char)p[a] | ((size_t)(unsigned char)p[a + 1] << 8);
    a += 2;
    if (n == 0 || n > (size_t)kMaxRows) return 0;
    for (size_t i = 0; i < n; ++i)
    {
        std::string sid; unsigned int bits = 0;
        if (!GetSid(p, size, &a, &sid) || !GetU32(p, size, &a, &bits)) return 0;
        const float v = BitsFloat(bits);
        if (!ValueOk(v)) return 0;
        out.rows.push_back(std::make_pair(sid, v));
    }
    if (a != size) return 0;
    *t = out;
    return 1;
}

/* THE ANSWER for one (town, item) on a game that is not the source: the source's number when the source's table for that town names
   the item, else the engine's own */
const int kUseEngine = 0, kUseSource = 1;
inline int Choose(int haveTown, int haveItem) { return (haveTown != 0 && haveItem != 0) ? kUseSource : kUseEngine; }
/* THE HOOK'S CHOICE on each call of Town::getLocalTradePriceMult: the book is asked only on a game that is not the source (worked out
   once per frame on the main thread), with its world running (no load, no teardown) and a book held. 1 = ask the book; 0 = the
   engine's own number. */
inline int HookUsesBook(int iAmSource, int engineBlocked, int bookHave) { return (iAmSource == 0 && engineBlocked == 0 && bookHave != 0) ? 1 : 0; }

/* THE ONE PRICE SOURCE of a world - one rule on every game, so every game names the same one: whenever this game reads the world
   server's roster (haveRoster), the lowest IN_WORLD slot on it (the ordering every game in the world shares - mppresence::
   TieBreakByRoster's), whether or not a session link is up; with no roster (session play only), the session host while the link is
   up. 1 = this game is the source. */
inline int PriceSource(int haveRoster, int lowestInWorld, int mySlot, int linkUp, int isHost)
{
    if (haveRoster != 0) return (mySlot >= 0 && lowestInWorld >= 0 && mySlot == lowestInWorld) ? 1 : 0;
    return (linkUp != 0 && isHost != 0) ? 1 : 0;
}
/* WHO the source is, as a key a received table's sender is compared with: a roster slot (>= 0); with no roster, this game
   (kSrcThisGame) or the session peer of link generation `linkGen` (SessionKey); kSrcNone = no source. */
const long kSrcNone = -1L, kSrcThisGame = -2L;
inline long SessionKey(long linkGen) { return -3L - (linkGen < 0 ? 0L : linkGen); }
inline long SourceKey(int haveRoster, int lowestInWorld, int linkUp, int isHost, long linkGen)
{
    if (haveRoster != 0) return (lowestInWorld >= 0) ? (long)lowestInWorld : kSrcNone;
    if (linkUp == 0) return kSrcNone;
    return (isHost != 0) ? kSrcThisGame : SessionKey(linkGen);
}
/* THE SENDER of one received table, as a key of the same kind SourceKey gives for the same game: a relayed copy's origin slot; a
   session-link delivery, with no roster (haveRoster 0 - the source key is then a session key), the session peer of this link
   generation; with a roster, the peer's slot when this game knows it (LinkPeerSlot), else the session peer of this link generation */
inline long SenderKey(int haveRoster, int viaRelay, int originSlot, int linkPeerSlot, long linkGen)
{
    if (viaRelay != 0) return (originSlot >= 0) ? (long)originSlot : kSrcNone;
    if (haveRoster == 0) return SessionKey(linkGen);
    return (linkPeerSlot >= 0) ? (long)linkPeerSlot : SessionKey(linkGen);
}
/* ONE RECEIVED TABLE: taken, or why not. The source takes none (it prices with its own). Every other game takes a table only from the
   source (sender key == source key). A relayed copy from the session peer's slot while the link is up is the other road's duplicate:
   the source sends its session peer the table on the link. */
const int kTake = 0, kTakeOnSource = 1, kTakeOtherRoad = 2, kTakeNotSource = 3;
inline int TakeFrom(int iAmSource, long sourceKey, long senderKey, int viaRelay, int linkUp, int linkPeerSlot)
{
    if (iAmSource != 0) return kTakeOnSource;
    if (sourceKey == kSrcNone || senderKey != sourceKey) return kTakeNotSource;
    if (viaRelay != 0 && linkUp != 0 && linkPeerSlot >= 0 && senderKey == (long)linkPeerSlot) return kTakeOtherRoad;
    return kTake;
}

/* THE BOOK of a game that is not the source: the tables of ONE sender (SenderKey) and ONE world generation of that sender. A table of
   another sender or generation replaces the whole book, so two sources' tables never merge; Clear empties it (this game became the
   source, or the source changed). Version moves on every change (the hook's answer memo is kept per version). */
class Book
{
public:
    Book() : sender_(kSrcNone), gen_(0), have_(0), version_(0) {}
    /* 1 = this table started a new book (the older tables are gone); 0 = added to the current one */
    int Apply(long sender, const Table& t)
    {
        int fresh = 0;
        if (have_ == 0 || sender != sender_ || t.gen != gen_) { towns_.clear(); sender_ = sender; gen_ = t.gen; have_ = 1; fresh = 1; }
        std::map<std::string, float>& m = towns_[t.town];
        m.clear();
        for (size_t i = 0; i < t.rows.size(); ++i) m[t.rows[i].first] = t.rows[i].second;
        ++version_;
        return fresh;
    }
    void Clear() { towns_.clear(); sender_ = kSrcNone; gen_ = 0; have_ = 0; ++version_; }
    /* 1 = the item is in the sender's table of that town (*v its number); *haveTown = the sender sent that town's table */
    int Lookup(const std::string& town, const std::string& item, int* haveTown, float* v) const
    {
        *haveTown = 0;
        if (have_ == 0) return 0;
        std::map<std::string, std::map<std::string, float> >::const_iterator t = towns_.find(town);
        if (t == towns_.end()) return 0;
        *haveTown = 1;
        std::map<std::string, float>::const_iterator i = t->second.find(item);
        if (i == t->second.end()) return 0;
        *v = i->second;
        return 1;
    }
    size_t Towns() const { return towns_.size(); }
    unsigned int Gen() const { return gen_; }
    long Sender() const { return sender_; }
    int Have() const { return have_; }
    unsigned long Version() const { return version_; }
private:
    long sender_;
    unsigned int gen_;
    int have_;
    unsigned long version_;
    std::map<std::string, std::map<std::string, float> > towns_;
};

/* THE KEPT TABLES of a game: the LAST table it refused from each sender for each town while its own answer to "who is the source"
   was unknown, named another sender, or named this game itself (KeepRefused). One table per (sender, town), and only the sender's newest
   world generation (a table of another generation drops that sender's older ones); at most kKeptCap tables in all (a new
   (sender, town) beyond it is refused, a replacement still goes in). When the source answer names a sender, TakeFor hands back that
   sender's tables and forgets them - they are filed in the book. Clear forgets all (world teardown). */
const size_t kKeptCap = 4096;
/* 1 = a received table is kept: refused as not the source's (kTakeNotSource) or as having reached the source (kTakeOnSource - this
   game may name that sender as the source a frame later), from a sender with a key */
inline int KeepRefused(int take, long senderKey)
{
    return ((take == kTakeNotSource || take == kTakeOnSource) && senderKey != kSrcNone && senderKey != kSrcThisGame) ? 1 : 0;
}
class Kept
{
public:
    Kept() : size_(0) {}
    /* 1 = kept (new, or replacing that (sender, town)'s earlier table); 0 = refused (the cap) */
    int Keep(long sender, const Table& t)
    {
        std::map<long, std::map<std::string, Table> >::iterator s = kept_.find(sender);
        if (s != kept_.end() && !s->second.empty() && s->second.begin()->second.gen != t.gen) { size_ -= s->second.size(); s->second.clear(); }
        const int have = (s != kept_.end() && s->second.count(t.town) != 0) ? 1 : 0;
        if (have == 0 && size_ >= kKeptCap) return 0;
        kept_[sender][t.town] = t;
        if (have == 0) ++size_;
        return 1;
    }
    /* the kept tables of `source` into *out, forgotten here; none for kSrcNone or kSrcThisGame. Returns how many. */
    size_t TakeFor(long source, std::vector<Table>* out)
    {
        out->clear();
        if (source == kSrcNone || source == kSrcThisGame) return 0;
        std::map<long, std::map<std::string, Table> >::iterator s = kept_.find(source);
        if (s == kept_.end()) return 0;
        for (std::map<std::string, Table>::const_iterator i = s->second.begin(); i != s->second.end(); ++i) out->push_back(i->second);
        size_ -= s->second.size();
        kept_.erase(s);
        return out->size();
    }
    void Clear() { kept_.clear(); size_ = 0; }
    size_t Size() const { return size_; }
private:
    std::map<long, std::map<std::string, Table> > kept_;
    size_t size_;
};

/* ONE ROAD of the source's send (the session link, or the world server), per (arrival epoch, world load). The road is due while it can
   carry the send (go), has not yet carried every table for this (epoch, world), and is not waiting. A send on it where a table did not
   go makes it wait for that road's next ready edge (`edge` = the road's ready-edge count; a count other than the one at the failure
   re-arms it) - never for elapsed time or frames; at most kRoadTries re-attempts per (epoch, world), then the road gives up for that
   (epoch, world). A new (epoch, world) starts the road afresh; Clear (this game became the source) does too. */
const int kRoadTries = 5;
const int kRoadSent = 0, kRoadWait = 1, kRoadGaveUp = 2;
class Road
{
public:
    Road() : epoch_(-1), world_(-1), done_(0), wait_(0), at_(0), tries_(0) {}
    /* 1 = send on this road now */
    int Due(int go, long epoch, long world, long long edge)
    {
        if (epoch != epoch_ || world != world_) { epoch_ = epoch; world_ = world; done_ = 0; wait_ = 0; tries_ = 0; }
        if (go == 0 || done_ != 0) return 0;
        return (wait_ != 0 && edge == at_) ? 0 : 1;
    }
    /* 1 = the due send is a re-attempt after a failure */
    int Retrying() const { return wait_; }
    /* the send on this road just made: `failed` tables did not go; `edge` the road's ready-edge count now. kRoadSent, kRoadWait or kRoadGaveUp */
    int Result(long long failed, long long edge)
    {
        if (failed == 0) { done_ = 1; wait_ = 0; return kRoadSent; }
        if (tries_ < kRoadTries) { ++tries_; wait_ = 1; at_ = edge; return kRoadWait; }
        done_ = 1; wait_ = 0;
        return kRoadGaveUp;
    }
    int Tries() const { return tries_; }
    void Clear() { epoch_ = -1; world_ = -1; done_ = 0; wait_ = 0; at_ = 0; tries_ = 0; }
private:
    long epoch_, world_;
    int done_, wait_;
    long long at_;
    int tries_;
};

/* THE HOLDER'S PRICE-LEG TEST. The requester priced `unit` with its culture and local multipliers (cultAsked, localAsked); the holder
   reads the same two legs on its real keeper. The rest of the price (base value, trader and stolen multipliers, the buy / sell side) is
   common to both, so the unit price the holder's legs give is unit x (cultHere x localHere) / (cultAsked x localAsked); more than one
   cat away from `unit` is beyond the engine's rounding. *expected = that price rounded (-1 when unread). */
const int kLegsAgree = 0, kLegsDisagree = 1, kLegsUnread = 2;
inline int LegsVerdict(int unit, float cultAsked, float localAsked, float cultHere, float localHere, int* expected)
{
    *expected = -1;
    if (unit < 0 || ValueOk(cultAsked) == 0 || ValueOk(localAsked) == 0 || ValueOk(cultHere) == 0 || ValueOk(localHere) == 0) return kLegsUnread;
    const double e = (double)unit * ((double)cultHere * (double)localHere) / ((double)cultAsked * (double)localAsked);
    *expected = (int)(e + 0.5);
    const double d = e - (double)unit;
    return (d > 1.0 || d < -1.0) ? kLegsDisagree : kLegsAgree;
}

}   /* namespace townprice */
#endif
