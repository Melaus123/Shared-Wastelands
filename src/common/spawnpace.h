/* src/common/spawnpace.h - COPIES OF ANOTHER GAME'S CHARACTERS ARE MADE UNDER A FRAME BUDGET, EACH COPY'S MESSAGES KEPT IN ORDER.
 * The pure decisions only: no engine memory, no Windows, no globals. The plugin (store.cpp InQueueDrain) and the offline
 * suite compile the same functions. C++03 (VS2010 v100).
 *
 * WHY. When another player brings a whole town into an area this game holds, its SPAWNs arrive together; making every copy
 * in one frame (about 2 ms each) froze the game for half a second at a time.
 *
 * WHAT IS DECIDED HERE.
 *  - MessageSubjects: the characters a message applies to, read per type where its encoder writes them (lists for XFER,
 *    XFER_ACK, RELEASE, RELEASE_ACK, SQUAD_LEAD and a ROSTER CHECK or ANSWER). A message whose list is cut or counts more than
 *    its type allows, and a ROSTER HASH (it sums its sender's characters by area), is scoped to its SENDER instead.
 *  - Route: a SPAWN for a character with no copy here is held once this frame has made a copy and spent the budget, or while
 *    older SPAWNs are held (oldest first). A message naming a character that a held row of any kind names is held behind that
 *    row, in arrival order. A sender-scoped message waits for its sender's held SPAWNs and sender-scoped rows, and every later
 *    message of that sender waits behind it; a catch-up's END waits for its sender's SPAWNs; the world server's PLAYER_GONE
 *    waits for every held SPAWN. Everything else applies at once, so pacing never delays a copy that is already shown, and
 *    never holds one sender's messages behind another sender's copies.
 *  - HeldUrgentStep, then HeldStep: at the start of each frame the held rows are worked oldest first under the same budget (at
 *    least one copy a frame). First the SPAWNs that a held hand-over (XFER, XFER_ACK, RELEASE, RELEASE_ACK) names are made
 *    ahead of older SPAWNs, because the giver abandons a hand-over left unanswered for 10 s; then every row, a held message
 *    applying in the same pass as the rows it waits for.
 *
 * THE BURST BOOK. Frames in which copies were made, and the frames after them while anything is still held or queued, form
 * one burst; it ends on the first frame that made no copy and left nothing waiting. The plugin logs one line per burst.
 */
#ifndef SPAWNPACE_H
#define SPAWNPACE_H

#include <cstddef>
#include <map>
#include <set>
#include <vector>

namespace spawnpace {

/* The main-thread time a frame may spend draining once it has made a copy, in microseconds. */
const long long kFrameBudgetUs = 4000;

enum { kPaceGo = 0, kPaceWait = 1 };

/* makesCopy: a SPAWN for a uid this game shows no copy of. madeThisFrame: copies already made in this frame. usedUs: the
   drain time spent in this frame so far. kPaceWait only when all three say so. */
inline int PaceDecide(int makesCopy, long long madeThisFrame, long long usedUs, long long budgetUs)
{
    if (makesCopy == 0) return kPaceGo;
    if (madeThisFrame < 1) return kPaceGo;
    if (budgetUs < 0) budgetUs = 0;
    return usedUs >= budgetUs ? kPaceWait : kPaceGo;
}

/* ---- which characters a message names ----
   The game-to-game message numbers (the plugin's net::MSG_*; a relayed message carries the same inner number; the plugin
   checks these against its own enum at compile time). */
enum
{
    kMsgSpawn = 10, kMsgTask = 11, kMsgMove = 12, kMsgHit = 13, kMsgState = 14, kMsgAppearance = 15, kMsgClothing = 16,
    kMsgCombatMode = 17, kMsgDespawn = 18, kMsgSwing = 19, kMsgIntent = 21, kMsgContext = 22, kMsgXfer = 25, kMsgXferAck = 26,
    kMsgUnload = 27, kMsgItemMove = 37, kMsgItemRequest = 38, kMsgItemConfirm = 39, kMsgItemPlaced = 40, kMsgItemRevoke = 41,
    kMsgSay = 43, kMsgStats = 44, kMsgCrime = 45, kMsgBounty = 46, kMsgCarryBreak = 47, kMsgPrison = 48, kMsgTreat = 49,
    kMsgName = 51, kMsgSlave = 52, kMsgHire = 55, kMsgSquadLead = 57, kMsgTalk = 59, kMsgCapture = 60, kMsgCaptureDone = 61,
    kMsgCapturePlaced = 62, kMsgShot = 63, kMsgEffect = 65, kMsgInside = 66, kMsgRelease = 68, kMsgReleaseAck = 69,
    kMsgRoster = 70, kMsgMoveStop = 72, kMsgOwnerMoved = 202
};
const unsigned int kXferMemberSize = 48;   /* one XFER member: uid u32 | x,y,z,fx,fz f32 | intentType i32 | intentSubject u32 | ... */
const unsigned int kXferMaxMembers = 256;
enum { kSubjectsSender = -1 };
/* The fixed layouts of the other lists (liveowner.h RELEASE / RELEASE_ACK / ROSTER, squadlead.h SQUAD_LEAD; the offline suite
   checks these against those headers when it compiles). */
const size_t kSubjRelHead = 16, kSubjRelRow = 52, kSubjRelAckHead = 10;   /* RELEASE: count u8 at 11, rows with the uid at 0; RELEASE_ACK: counts u16 at 4, 6, 8 */
const unsigned int kSubjRelMax = 64;
const size_t kSubjLeadHead = 23;                                           /* SQUAD_LEAD: acting 4, formal 8, count u16 at 21, member uids from 23 */
const unsigned int kSubjLeadMax = 64;
enum { kSubjRosterCheck = 2, kSubjRosterAnswer = 3 };                      /* the ROSTER kind, u8 at 0 */
const size_t kSubjCheckHead = 16, kSubjCheckRow = 8, kSubjAnswerHead = 20, kSubjAnswerRow = 12;   /* count u32 at 12 / 16; the uid at row offset 0 */
const unsigned int kSubjCheckMax = 6000, kSubjAnswerMax = 5000;

inline bool SubjectU32(const unsigned char* p, size_t size, size_t at, unsigned int* v)
{
    if (p == 0 || at > size || size - at < 4) return false;
    *v = (unsigned int)p[at] | ((unsigned int)p[at + 1] << 8) | ((unsigned int)p[at + 2] << 16) | ((unsigned int)p[at + 3] << 24);
    return true;
}
inline void SubjectAdd(std::vector<unsigned int>* out, unsigned int uid)
{
    if (uid == 0) return;
    for (size_t i = 0; i < out->size(); ++i) if ((*out)[i] == uid) return;
    out->push_back(uid);
}
inline void SubjectAt(const unsigned char* p, size_t size, size_t at, std::vector<unsigned int>* out)
{
    unsigned int v = 0;
    if (SubjectU32(p, size, at, &v)) SubjectAdd(out, v);
}
inline bool SubjectU16(const unsigned char* p, size_t size, size_t at, unsigned int* v)
{
    if (p == 0 || at > size || size - at < 2) return false;
    *v = (unsigned int)p[at] | ((unsigned int)p[at + 1] << 8);
    return true;
}
/* One list entry: the set keeps a long list (a ROSTER CHECK carries up to 6000) free of repeats without a search per entry. */
inline void SubjectListAt(const unsigned char* p, size_t size, size_t at, std::vector<unsigned int>* out, std::set<unsigned int>* seen)
{
    unsigned int v = 0;
    if (SubjectU32(p, size, at, &v) && v != 0 && seen->insert(v).second) out->push_back(v);
}
inline int SubjectsSender(std::vector<unsigned int>* out) { out->clear(); return kSubjectsSender; }
/* Fills *out with the characters the message applies to (no zero, no repeat) and returns how many (0: it names no character,
   applied at once), or kSubjectsSender: its list is cut or counts more than its type allows, or it speaks for all of its
   sender's characters (a ROSTER HASH), and it waits for its sender. Only the bytes present are read. */
inline int MessageSubjects(unsigned int type, const unsigned char* p, size_t size, std::vector<unsigned int>* out)
{
    out->clear();
    switch (type)
    {
    /* the character at 0 and nothing else that names one (STATE's carried body is applied by the carry tick once both
       copies exist, so it is not read) */
    case kMsgSpawn: case kMsgTask: case kMsgMove: case kMsgState: case kMsgAppearance: case kMsgClothing: case kMsgDespawn:
    case kMsgContext: case kMsgUnload: case kMsgSay: case kMsgStats: case kMsgBounty: case kMsgPrison: case kMsgTreat:
    case kMsgName: case kMsgInside: case kMsgMoveStop: case kMsgOwnerMoved: case kMsgItemMove:
        SubjectAt(p, size, 0, out); break;
    case kMsgHit: case kMsgSwing: case kMsgCarryBreak: case kMsgShot:   /* victim / swinger / body, then attacker / target / carrier / shooter */
        SubjectAt(p, size, 0, out); SubjectAt(p, size, 4, out); break;
    case kMsgCombatMode: case kMsgIntent:   /* the character | on / task type | the target / subject */
        SubjectAt(p, size, 0, out); SubjectAt(p, size, 8, out); break;
    case kMsgSlave: SubjectAt(p, size, 0, out); SubjectAt(p, size, 5, out); break;    /* the slave | state u8 | its owner */
    case kMsgCrime: SubjectAt(p, size, 0, out); SubjectAt(p, size, 12, out); break;   /* the offender | crime | expiry | the victim */
    case kMsgHire: SubjectAt(p, size, 5, out); SubjectAt(p, size, 9, out); break;     /* kind u8 | request id | the hired | the hirer */
    case kMsgTalk: SubjectAt(p, size, 5, out); SubjectAt(p, size, 9, out); break;     /* kind u8 | conversation id | the NPC | the target */
    case kMsgEffect: SubjectAt(p, size, 6, out); SubjectAt(p, size, 10, out); break;  /* dir u8 | kind u8 | request id | target | actor */
    case kMsgCapture: SubjectAt(p, size, 4, out); SubjectAt(p, size, 8, out); break;  /* request id | the victim | the slaver */
    case kMsgCaptureDone: case kMsgCapturePlaced: SubjectAt(p, size, 4, out); break; /* request id | the victim */
    case kMsgItemRequest:   /* id | dir u8 | the owner | section (u32 length + bytes) | x | y | quantity | the taker */
    {
        SubjectAt(p, size, 5, out);
        unsigned int len = 0;
        if (SubjectU32(p, size, 9, &len) && len <= size) SubjectAt(p, size, (size_t)13 + len + 12, out);
        break;
    }
    case kMsgXfer:   /* leader | reason | count | count members of kXferMemberSize (uid at 0). The intent subject at 28 is not waited for:
                        the taker skips an intent whose subject has no copy and still takes the member (handoff.cpp TakeMemberFromPeer) */
    {
        unsigned int n = 0;
        if (!SubjectU32(p, size, 8, &n) || n > kXferMaxMembers || size < 12 + (size_t)n * kXferMemberSize) return SubjectsSender(out);
        std::set<unsigned int> seen;
        SubjectListAt(p, size, 0, out, &seen);
        for (unsigned int i = 0; i < n; ++i) SubjectListAt(p, size, 12 + (size_t)i * kXferMemberSize, out, &seen);
        break;
    }
    case kMsgXferAck:   /* leader | count | count taken uids */
    {
        unsigned int n = 0;
        if (!SubjectU32(p, size, 4, &n) || n > kXferMaxMembers || size < 8 + (size_t)n * 4) return SubjectsSender(out);
        std::set<unsigned int> seen;
        SubjectListAt(p, size, 0, out, &seen);
        for (unsigned int i = 0; i < n; ++i) SubjectListAt(p, size, 8 + (size_t)i * 4, out, &seen);
        break;
    }
    case kMsgRelease:   /* id | the key character | sector u16 | flags u8 | count u8 | winner u16 | 0 u16 | count rows of 52 (uid at 0) */
    {
        if (p == 0 || size < kSubjRelHead) return SubjectsSender(out);
        const unsigned int n = (unsigned int)p[11];
        if (n == 0 || n > kSubjRelMax || size < kSubjRelHead + (size_t)n * kSubjRelRow) return SubjectsSender(out);
        std::set<unsigned int> seen;
        SubjectListAt(p, size, 4, out, &seen);
        for (unsigned int i = 0; i < n; ++i) SubjectListAt(p, size, kSubjRelHead + (size_t)i * kSubjRelRow, out, &seen);
        break;
    }
    case kMsgReleaseAck:   /* id | adopted u16 | dropped u16 | deferred u16 | the three uid lists */
    {
        unsigned int na = 0, nd = 0, nf = 0;
        if (!SubjectU16(p, size, 4, &na) || !SubjectU16(p, size, 6, &nd) || !SubjectU16(p, size, 8, &nf)) return SubjectsSender(out);
        const size_t total = (size_t)na + (size_t)nd + (size_t)nf;
        if (total > (size_t)kSubjRelMax || size < kSubjRelAckHead + total * 4) return SubjectsSender(out);
        std::set<unsigned int> seen;
        for (size_t i = 0; i < total; ++i) SubjectListAt(p, size, kSubjRelAckHead + i * 4, out, &seen);
        break;
    }
    case kMsgSquadLead:   /* squad key | acting leader | formal leader | seq | cats i32 | hasCats u8 | count u16 | count member uids */
    {
        unsigned int n = 0;
        if (!SubjectU16(p, size, 21, &n) || n > kSubjLeadMax || size < kSubjLeadHead + (size_t)n * 4) return SubjectsSender(out);
        std::set<unsigned int> seen;
        SubjectListAt(p, size, 4, out, &seen);
        SubjectListAt(p, size, 8, out, &seen);
        for (unsigned int i = 0; i < n; ++i) SubjectListAt(p, size, kSubjLeadHead + (size_t)i * 4, out, &seen);
        break;
    }
    case kMsgRoster:   /* kind u8 | 0 | chunk u16 | ...: a CHECK and an ANSWER list their characters; a HASH names none but sums its sender's */
    {
        if (p == 0 || size < 4) return SubjectsSender(out);
        size_t head = 0, row = 0, countAt = 0; unsigned int cap = 0;
        if (p[0] == (unsigned char)kSubjRosterCheck) { head = kSubjCheckHead; countAt = 12; row = kSubjCheckRow; cap = kSubjCheckMax; }
        else if (p[0] == (unsigned char)kSubjRosterAnswer) { head = kSubjAnswerHead; countAt = 16; row = kSubjAnswerRow; cap = kSubjAnswerMax; }
        else return SubjectsSender(out);
        unsigned int n = 0;
        if (!SubjectU32(p, size, countAt, &n) || n > cap || size < head + (size_t)n * row) return SubjectsSender(out);
        std::set<unsigned int> seen;
        for (unsigned int i = 0; i < n; ++i) SubjectListAt(p, size, head + (size_t)i * row, out, &seen);
        break;
    }
    default:   /* ITEM_CONFIRM / PLACED / REVOKE name only a request id; the rest name no character */
        return 0;
    }
    return (int)out->size();
}

/* ---- the held list ---- */
enum { kKindSpawn = 1, kKindFollow = 2, kKindBarrier = 3 };
enum { kScopeNone = 0, kScopeAll = 1, kScopeSender = 2 };   /* what a barrier waits for: nothing, every held SPAWN, its sender's */
const unsigned int kNoSender = 0xFFFFFFFFu;                 /* a message that came on no other game's road (never sender-ordered) */
struct HeldRow
{
    int kind;
    int scope;                          /* a barrier's scope */
    unsigned int sender;
    std::vector<unsigned int> names;    /* a SPAWN: its character; a follower: the characters it waits for */
    long long ticket;                   /* the caller's key for the held message */
    int urgent;                         /* a follower that hands characters over (UrgentType): the SPAWNs it names are made first */
    HeldRow() : kind(0), scope(0), sender(0), ticket(0), urgent(0) {}
};
struct HeldBook
{
    std::vector<HeldRow> rows;                     /* arrival order */
    std::map<unsigned int, int> uids;              /* the characters whose SPAWN is held */
    std::map<unsigned int, long long> spawnsFrom;  /* held SPAWNs per sender */
    long long spawns;                              /* SPAWN rows held */
    std::map<unsigned int, long long> named;       /* held rows of any kind naming each character */
    std::map<unsigned int, long long> urgentNamed; /* held hand-overs naming each character */
    std::map<unsigned int, long long> senderRows;  /* held sender-scoped rows per sender */
    HeldBook() : spawns(0) {}
};
inline long long CountOf(const std::map<unsigned int, long long>& m, unsigned int k)
{
    std::map<unsigned int, long long>::const_iterator it = m.find(k);
    return it == m.end() ? 0 : it->second;
}
inline void CountDown(std::map<unsigned int, long long>* m, unsigned int k)
{
    std::map<unsigned int, long long>::iterator it = m->find(k);
    if (it != m->end() && --it->second <= 0) m->erase(it);
}
inline bool UidHeld(const HeldBook& b, unsigned int uid) { return uid != 0 && b.uids.find(uid) != b.uids.end(); }
inline bool AnyHeld(const HeldBook& b, const std::vector<unsigned int>& names)
{
    for (size_t i = 0; i < names.size(); ++i) if (UidHeld(b, names[i])) return true;
    return false;
}
inline bool AnyNamed(const HeldBook& b, const std::vector<unsigned int>& names)
{
    for (size_t i = 0; i < names.size(); ++i) if (names[i] != 0 && CountOf(b.named, names[i]) > 0) return true;
    return false;
}
inline long long SpawnsFrom(const HeldBook& b, unsigned int sender) { return CountOf(b.spawnsFrom, sender); }
inline long long SenderRows(const HeldBook& b, unsigned int sender) { return sender == kNoSender ? 0 : CountOf(b.senderRows, sender); }
/* The messages that hand characters from one game to another: the giver gives up on one left unanswered (XFER after 10 s). */
inline int UrgentType(unsigned int type)
{
    return (type == kMsgXfer || type == kMsgXferAck || type == kMsgRelease || type == kMsgReleaseAck) ? 1 : 0;
}
/* a held SPAWN that a held hand-over names */
inline bool UrgentSpawnRow(const HeldBook& b, const HeldRow& r)
{
    return r.kind == kKindSpawn && !r.names.empty() && CountOf(b.urgentNamed, r.names[0]) > 0;
}

enum { kRouteApply = 0, kRouteHoldSpawn = 1, kRouteFollow = 2, kRouteBarrier = 3 };
/* newCopySpawn: a SPAWN for a uid with no copy here. names: the characters the message names. barrierScope: kScopeNone, or
   what it must wait for. sender: the sending game, kNoSender for a message on no game's road. made / usedUs: this frame's
   copies and drain time so far (usedUs is only read for a new copy). A message is held behind any held row naming one of its
   characters, and behind its sender's held sender-scoped row; a new copy's SPAWN held so is held as a SPAWN (it stays under
   the budget). */
inline int RouteNames(const HeldBook& b, int newCopySpawn, const std::vector<unsigned int>& names, int barrierScope, unsigned int sender,
                      long long made, long long usedUs, long long budgetUs)
{
    if (barrierScope == kScopeAll && b.spawns > 0) return kRouteBarrier;
    if (barrierScope == kScopeSender && (SpawnsFrom(b, sender) > 0 || SenderRows(b, sender) > 0)) return kRouteBarrier;
    if (AnyHeld(b, names)) return kRouteFollow;
    const int behind = (AnyNamed(b, names) || SenderRows(b, sender) > 0) ? 1 : 0;
    if (newCopySpawn != 0)
    {
        if (b.spawns > 0 || behind != 0) return kRouteHoldSpawn;
        return PaceDecide(1, made, usedUs, budgetUs) == kPaceWait ? kRouteHoldSpawn : kRouteApply;
    }
    return behind != 0 ? kRouteFollow : kRouteApply;
}
/* urgent: the message hands characters over (UrgentType); kept on a follower only */
inline void HeldPutRow(HeldBook* b, int kind, int scope, unsigned int sender, const std::vector<unsigned int>& names, long long ticket, int urgent)
{
    if (b == 0) return;
    HeldRow r; r.kind = kind; r.scope = scope; r.sender = sender; r.names = names; r.ticket = ticket;
    r.urgent = (kind == kKindFollow && urgent != 0) ? 1 : 0;
    if (kind == kKindSpawn && r.names.size() > 1) r.names.resize(1);
    b->rows.push_back(r);
    if (kind == kKindSpawn && !r.names.empty()) { b->uids[r.names[0]] = 1; ++b->spawns; ++b->spawnsFrom[sender]; }
    for (size_t i = 0; i < r.names.size(); ++i)
    {
        if (r.names[i] == 0) continue;
        ++b->named[r.names[i]];
        if (r.urgent != 0) ++b->urgentNamed[r.names[i]];
    }
    if (kind == kKindBarrier && scope == kScopeSender && sender != kNoSender) ++b->senderRows[sender];
}
inline void HeldPutNames(HeldBook* b, int kind, int scope, unsigned int sender, const std::vector<unsigned int>& names, long long ticket)
{
    HeldPutRow(b, kind, scope, sender, names, ticket, 0);
}
/* the single-character forms (sender 0; a barrier waits for every held SPAWN) */
inline int Route2(const HeldBook& b, int newCopySpawn, unsigned int uidA, unsigned int uidB, int isBarrier, long long made, long long usedUs, long long budgetUs)
{
    std::vector<unsigned int> n; if (uidA != 0) n.push_back(uidA); if (uidB != 0) n.push_back(uidB);
    return RouteNames(b, newCopySpawn, n, isBarrier != 0 ? kScopeAll : kScopeNone, 0, made, usedUs, budgetUs);
}
inline int Route(const HeldBook& b, int newCopySpawn, unsigned int namedUid, int isBarrier, long long made, long long usedUs, long long budgetUs)
{
    return Route2(b, newCopySpawn, namedUid, 0, isBarrier, made, usedUs, budgetUs);
}
inline void HeldPut2(HeldBook* b, int kind, unsigned int uid, unsigned int uid2, long long ticket)
{
    std::vector<unsigned int> n; if (uid != 0) n.push_back(uid); if (uid2 != 0) n.push_back(uid2);
    HeldPutNames(b, kind, kind == kKindBarrier ? kScopeAll : kScopeNone, 0, n, ticket);
}
inline void HeldPut(HeldBook* b, int kind, unsigned int uid, long long ticket) { HeldPut2(b, kind, uid, 0, ticket); }
/* Takes row i out of the book (the caller applies or discards its message): every count it added is taken back. */
inline void HeldTake(HeldBook* b, size_t i)
{
    if (b == 0 || i >= b->rows.size()) return;
    const HeldRow r = b->rows[i];
    b->rows.erase(b->rows.begin() + (std::vector<HeldRow>::difference_type)i);
    if (r.kind == kKindSpawn && !r.names.empty())
    {
        b->uids.erase(r.names[0]);
        if (b->spawns > 0) --b->spawns;
        CountDown(&b->spawnsFrom, r.sender);
    }
    for (size_t k = 0; k < r.names.size(); ++k)
    {
        if (r.names[k] == 0) continue;
        CountDown(&b->named, r.names[k]);
        if (r.urgent != 0) CountDown(&b->urgentNamed, r.names[k]);
    }
    if (r.kind == kKindBarrier && r.scope == kScopeSender && r.sender != kNoSender) CountDown(&b->senderRows, r.sender);
}

enum { kHeldKeep = 0, kHeldApply = 1 };
struct HeldPassState
{
    int budgetHit;                       /* a SPAWN was kept for the budget: every later SPAWN is too */
    int barrierKept;                     /* a barrier waiting for every held SPAWN was kept: every later SPAWN waits */
    int anySpawnKept;
    std::set<unsigned int> spawnKeptFrom;    /* senders with a kept SPAWN */
    std::set<unsigned int> barrierKeptFrom;  /* senders with a kept sender-scoped barrier: their later rows wait */
    std::set<unsigned int> keptUids;         /* the characters named by rows kept so far in this pass: later rows naming one wait */
    HeldPassState() : budgetHit(0), barrierKept(0), anySpawnKept(0) {}
};
inline void KeepNames(HeldPassState* st, const std::vector<unsigned int>& names)
{
    for (size_t i = 0; i < names.size(); ++i) if (names[i] != 0) st->keptUids.insert(names[i]);
}
inline bool AnyKept(const HeldPassState& st, const std::vector<unsigned int>& names)
{
    for (size_t i = 0; i < names.size(); ++i) if (names[i] != 0 && st.keptUids.count(names[i]) != 0) return true;
    return false;
}
/* One row of the frame's pass over the book, oldest first. usedUs is only read for a SPAWN row. A row waits while an earlier
   row of this pass that names one of its characters was kept, or its sender's sender-scoped row was. */
inline int HeldStep(const HeldBook& b, const HeldRow& r, HeldPassState* st, long long made, long long usedUs, long long budgetUs)
{
    (void)b;
    if (st == 0) return kHeldKeep;
    if (r.kind == kKindSpawn)
    {
        const int ordered = (st->barrierKept != 0 || st->barrierKeptFrom.count(r.sender) != 0 || AnyKept(*st, r.names)) ? 1 : 0;
        if (ordered == 0 && st->budgetHit == 0 && PaceDecide(1, made, usedUs, budgetUs) != kPaceWait) return kHeldApply;
        if (ordered == 0) st->budgetHit = 1;
        st->anySpawnKept = 1; st->spawnKeptFrom.insert(r.sender); KeepNames(st, r.names);
        return kHeldKeep;
    }
    if (r.kind == kKindFollow)
    {
        if (st->barrierKeptFrom.count(r.sender) == 0 && !AnyKept(*st, r.names)) return kHeldApply;
        KeepNames(st, r.names);
        return kHeldKeep;
    }
    if (r.scope == kScopeSender)
    {
        if (st->spawnKeptFrom.count(r.sender) != 0 || st->barrierKeptFrom.count(r.sender) != 0) { st->barrierKeptFrom.insert(r.sender); return kHeldKeep; }
        return kHeldApply;
    }
    if (st->anySpawnKept != 0) { st->barrierKept = 1; return kHeldKeep; }
    return kHeldApply;
}
/* The frame's first pass, run before HeldStep's while a held hand-over names a held SPAWN (b.urgentNamed not empty), oldest
   first, with its own HeldPassState: a SPAWN a held hand-over names is made now, ahead of older SPAWNs, while the budget allows
   (at least one a frame) - unless a row before it must apply first: a row naming the same character, a barrier waiting for every
   held SPAWN, or its sender's sender-scoped row. Every other row is kept for HeldStep's pass. usedUs is only read for a SPAWN
   row UrgentSpawnRow names. Once budgetHit is set nothing more applies in this pass. */
inline int HeldUrgentStep(const HeldBook& b, const HeldRow& r, HeldPassState* st, long long made, long long usedUs, long long budgetUs)
{
    if (st == 0) return kHeldKeep;
    if (r.kind == kKindSpawn)
    {
        if (UrgentSpawnRow(b, r) && st->budgetHit == 0 && st->barrierKept == 0 && st->barrierKeptFrom.count(r.sender) == 0 && !AnyKept(*st, r.names))
        {
            if (PaceDecide(1, made, usedUs, budgetUs) != kPaceWait) return kHeldApply;
            st->budgetHit = 1;
        }
        KeepNames(st, r.names);
        return kHeldKeep;
    }
    if (r.kind == kKindFollow) { KeepNames(st, r.names); return kHeldKeep; }
    if (r.scope == kScopeSender) st->barrierKeptFrom.insert(r.sender);
    else st->barrierKept = 1;
    return kHeldKeep;
}

/* ---- one frame's tally, filled by the drains of that frame ---- */
struct PaceFrame
{
    long long applied;   /* SPAWN messages applied */
    long long made;      /* of them, a new copy now exists */
    long long repeat;    /* of them, the copy already existed */
    long long notMade;   /* of them, no copy existed before or after (refused - its own log line says why) */
    long long usedUs;    /* drain time in the frame */
    int waited;          /* a SPAWN was held to a later frame */
    int left;            /* entries were still queued or held when the frame's last drain ended */
};
inline void FrameReset(PaceFrame* f)
{
    if (f == 0) return;
    f->applied = 0; f->made = 0; f->repeat = 0; f->notMade = 0; f->usedUs = 0; f->waited = 0; f->left = 0;
}
/* What applying one SPAWN did, from whether a copy existed before and after it. */
inline void FrameNoteSpawn(PaceFrame* f, int hadCopy, int hasCopy)
{
    if (f == 0) return;
    ++f->applied;
    if (hadCopy != 0) ++f->repeat;
    else if (hasCopy != 0) ++f->made;
    else ++f->notMade;
}

struct Burst
{
    int open;
    long long arrived;   /* SPAWNs queued since the last burst ended (repeats included) */
    long long applied, made, repeat, notMade;
    long long frames;    /* frames the burst lasted */
    long long waits;     /* frames in which a SPAWN was held */
    long long worstUs;   /* the most drain time one of its frames spent */
};
inline void BurstReset(Burst* b)
{
    if (b == 0) return;
    b->open = 0; b->arrived = 0; b->applied = 0; b->made = 0; b->repeat = 0; b->notMade = 0; b->frames = 0; b->waits = 0; b->worstUs = 0;
}
/* A finished frame. A burst opens on a frame that made a copy and stays open while copies are made or anything waits.
   Returns 1 when this frame ends an open burst (an empty frame is not counted in it): the caller logs and resets it. */
inline int BurstFrame(Burst* b, const PaceFrame& f)
{
    if (b == 0) return 0;
    if (b->open == 0 && f.made < 1) return 0;
    if (b->open != 0 && f.made < 1 && f.waited == 0 && f.left == 0 && f.applied == 0) return 1;
    b->open = 1;
    b->applied += f.applied; b->made += f.made; b->repeat += f.repeat; b->notMade += f.notMade;
    ++b->frames;
    if (f.waited != 0) ++b->waits;
    if (f.usedUs > b->worstUs) b->worstUs = f.usedUs;
    if (f.made < 1 && f.waited == 0 && f.left == 0) return 1;   /* this frame applied only repeats and left nothing waiting */
    return 0;
}

}   /* namespace spawnpace */

#endif
