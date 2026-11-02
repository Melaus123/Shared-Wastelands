/* src/common/livefirst.h - T-1118 step (b) (owner decisions 1184 / 1193 b): BIG MESSAGES IN PIECES, LIVE MESSAGES FIRST, on every link
   between a game and the world server. PURE: no Windows.h, no ENet, no globals, no I/O, so the offline suite sweeps exactly what the
   game and the world server compile.

   WHY. An area file (0.3-1.4 MB, up to several MB) went to the network as ONE message, and the network layer (ENet) sends in queue
   order: every live message sent after it (a character's movement, a swing) waited until the whole file had left (T1656, T1660:
   MOVESTOP 0.8-1.2 s behind a 517 KB save). Now:
     - a frame longer than kChunkBytes travels as CHUNK messages (kMsgChunk) of up to kChunkBytes of it each, cut only as the link
       has room and each carrying at most the room left under the budget (PieceBytes; sendbound.h Outbox, live-first mode) and joined back into the whole frame at the far end (Joiner) before anything
       reads it, so every caller above sees exactly the whole message it saw before;
     - the waiting line has a LIVE line (live messages: the LIVE envelope 53, except bug-report LOG_PART pieces) that goes before the
       REST line (records and everything else, in order), so a live message passes a record's pieces that have not gone yet;
     - record pieces go to the network only while what it holds is under a BUDGET - about 100 ms of what the link drains (Rate), at
       least kBudgetMin - so a live message waits behind at most that much that was already handed over;
     - BARRIERS: a message a later live message depends on (WELCOME 30, AREAMAP 35, PLAYER_GONE 57 toward a game - the T-1118
       overtake audit; JOIN_STAGE 55 toward the world server) stays on the REST line in order, and no live message queued after it
       passes it - except an AREAMAP whose bytes equal the last one put on the link, which changes nothing at the game but its map
       clock (MapRepeat below);
     - an ANNOUNCE line (a character's CONTEXT, SPAWN, APPEARANCE and CLOTHING) between them: after the LIVE line, before the REST line, under the same
       budget as REST; a live message naming a character (any of them, as the receiver reads them - SubjectsOf) whose announcement
       still waits on this link is held until that announcement has gone (sendbound.h), so the far end never hears of a character
       before its announcement.
   THE CHUNK WIRE. payload = whole frame length u32 | this piece's offset in the frame u32 | the piece (1..kChunkBytes bytes). The pieces
   of one frame go in order, one frame at a time (the REST line is first-in first-out); live messages may come between them, never
   another frame's pieces. The frame is the transport's own frame (type u8 | length u32 | payload), so the joined frame is read
   exactly as a received packet is. Guaranteed lane only.
   THE STEAM LEG (T-1118 (b) stage 2). A Steam joiner's link to the host's bridge runs the same mode (net/steam_transport.cpp): the
   joiner's link cuts its own big frames and joins the world server's pieces; the bridge's links do NEITHER - a CHUNK the bridge takes
   from one side goes on to the other as it came (HandOn below), never joined there and never cut again (sendbound.h: a CHUNK is
   never cut), so the world server and the joiner join what the other cut. Every CHUNK is a whole Steam message (at most kChunkBytes,
   32 KB, under relaysplit's 256 KB piece), so relaysplit's own pieces never meet a CHUNK. The Steam budget is 100 ms of the link's send rate
   (BudgetOf), counted against the bytes Steam has not sent yet. */
#ifndef SW_LIVEFIRST_H
#define SW_LIVEFIRST_H

#include <cstddef>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include "spawnpace.h"   /* MessageSubjects: the characters a message names, per type - the receiver's own list */

namespace livefirst {

const unsigned int kProtocol = 134;     /* the world-server protocol that carries CHUNK (store_main.cpp kProtocol, store.cpp kStoreProtocol) */

const unsigned int kMsgChunk = 88;      /* CHUNK - one piece of a frame too big to go whole; either way, world-server link only */
const size_t kFrameHeader = 5;          /* the transport's frame header: type u8 | payload length u32 */
const size_t kChunkHeader = 8;          /* whole frame length u32 | offset u32 */
const size_t kChunkBytes = 32768;       /* the most frame bytes in one CHUNK; a frame of up to this many bytes goes whole */
const size_t kChunkMinBytes = 4096;     /* the fewest frame bytes a piece cut to the room left carries: each piece costs a header and a send */
const size_t kJoinMax = 33554432;       /* 32 MiB: ENet's packet cap (= relaysplit::kRejoinMax) - the longest frame cut or joined */
const long long kBudgetMin = 16384;     /* record bytes the network may always hold (16 KB) */
const long long kBudgetMax = 1048576;   /* = coopsb::kNetBytesMax: never more than the old bound */
const double kAheadSec = 0.100;         /* the most a live message should wait behind record pieces already handed over */
const double kRateWindowSec = 0.250;    /* the drain rate is measured over windows of at least this long */

enum { kDirUp = 0, kDirDown = 1 };      /* = coopsb::kDirUp / kDirDown: a game's frames to the world server / the world server's to a game */
enum { kLineRest = 0, kLineLive = 1, kLineAnnounce = 2, kLineBulk = 3 };

/* T-1118 (c) (owner decision 1231 c) - BULK ON THE LIVE LINE, DECIDED FROM THE BYTES. The LIVE line has no byte limit, so a burst of
   one kind of live message (T1716: a game's town price tables, 130 frames of about 1.85 KB, 264 KB in front of faction-relation
   messages for 511 ms) went into the network back to back and every live message after it waited. No list of bulk kinds (a new bulk
   kind would fall back to the LIVE line: the same bug again). Instead the waiting line (sendbound.h Outbox) moves a guaranteed frame
   of the LIVE line onto the BULK line - after ANNOUNCE, before REST, under the same byte budget, held by barriers like ANNOUNCE - when
   (size) the one frame is longer than kBulkFrameBytes, or (share) its kind (the LIVE envelope's inner type) from its sender has put
   more than 1 / kBulkShareDiv of the budget on the link within the last kBulkWindowSec, or (sticky) a frame of that kind from that
   sender still waits on the BULK line (no same-kind overtaking). A waiting BULK frame holds later live frames naming the same
   characters (the ANNOUNCE line's per-character gate). Only LIVE envelopes are measured (KindOf); announcements stay on ANNOUNCE.
     kBulkFrameBytes 1024 (the whole frame, header included): a MOVE, a STATE, a HIT, a TASK or a SWING is a fixed record of one
     character, well under it; a town price table (about 1.85 KB) is over it, and a box answer or a big squad's hand-over (up to
     64 KB) is bulk by any measure (APPEARANCE, about 1.6 KB, rides the ANNOUNCE line and is never measured).
   ORDER KEPT (review of e8d97198): an announcement never leaves before a waiting BULK frame put before it that names one of its
   characters, nor before a live frame held behind one (sendbound.h AnnBlocked); a BULK frame naming no character holds its
   sender's later live frames that name no character only within its ORDER FAMILY (BareFamily: its own kind, or the item road for
   a box's contents) - what such a frame is about cannot be read, so its family keeps its order, but a burst of town price tables
   on a slow link holds no item answer, door or building for the second or more it takes to drain; a ROSTER list (ListOnly)
   is a comparison of lists, so on the BULK line it holds no other frame (it still holds back announcements of its characters).
   THE FIT RULE (sendbound.h Fits): an ANNOUNCE or BULK frame starts only when what the network holds plus the frame itself fits the
   budget, or the network holds nothing - so these lines never take the network past one budget, and a frame longer than the
   budget still goes, alone. The LIVE line keeps no byte limit.
     kBulkShareDiv 4: one kind of one sender may put at most a quarter of the budget (about 25 ms of the link) in front of the other
     live messages per 100 ms - at the 16 KB floor 4 KB, about fifty faction-relation frames of 78 bytes. */
const size_t kBulkFrameBytes = 1024;
const long long kBulkShareDiv = 4;
const double kBulkWindowSec = 0.100;
enum { kBulkNot = 0, kBulkSize = 1, kBulkShare = 2, kBulkSticky = 3 };

const unsigned int kTypeLive = 53, kTypeWelcome = 30, kTypeAreaMap = 35, kTypePlayerGone = 57, kTypeJoinStage = 55;   /* the world-server link's numbers */
const unsigned int kTypePlayerSectors = 37;                   /* PLAYERSECTORS: sent right after each AREAMAP (MapRepeat) */
const unsigned int kInnerLogPart = 204;                       /* = cooplive::kInnerLogPart (liverelay.h; the offline suite checks) */
const unsigned int kInnerSpawn = 10, kInnerContext = 22;      /* = cooplive::kInnerSpawn / kInnerContext (liveenvelope.h; the offline suite checks) */
const unsigned int kInnerCatchupEnd = 201;                    /* = cooplive::kInnerCatchupEnd (liverelay.h; the offline suite checks) */
const unsigned int kInnerAppearance = 15, kInnerClothing = 16; /* = cooplive::kInnerAppearance / kInnerClothing (liveenvelope.h; the offline suite checks) */
const unsigned int kInnerItemMove = 37, kInnerItemRequest = 38, kInnerItemConfirm = 39, kInnerItemPlaced = 40, kInnerItemRevoke = 41,
                   kInnerParityReq = 53, kInnerParityBox = 54, kInnerItemHandover = 79;   /* the item road: = cooplive's (liveenvelope.h; the offline suite checks) */
const unsigned int kInnerDoorState = 42, kInnerBuild = 50;    /* = cooplive::kInnerDoorState / kInnerBuild (liveenvelope.h; the offline suite checks) */
const size_t kLiveUpHeader = 12, kLiveDownHeader = 8;         /* = cooplive::kLiveUpHeader / kLiveDownHeader */

/* The line of a LIVE envelope's inner type, from one table; an inner type not in it is LIVE.
     - LOG_PART (bug-report pieces): REST.
     - CONTEXT and SPAWN, a character's announcement: ANNOUNCE. 60-120 characters are announced at a join or an area entry (120-512
       KB together); on the LIVE line every live message put after them waited 0.6-2.1 s behind them.
     - APPEARANCE (about 1.6 KB) and CLOTHING (about 0.4 KB), the looks sent once after each SPAWN: ANNOUNCE too. On the LIVE line,
       which has no byte limit, a 131-character catch-up's looks (about 257 KB) went back to back and every live message for another
       character waited behind them. Here they follow their own SPAWN in put order under the budget, and count as that character's
       announcements for the per-character gate. Both are guaranteed and never replaced (sendbound.h ClassOf: must-deliver).
     - NAME (51), SLAVE (52) and CRIME (45) are LIVE here: the ones that follow a SPAWN name its character, so the per-character gate
       (SubjectsOf; sendbound.h Outbox) holds them until it has gone; a rename or a crime change on its own stays live. */
struct InnerLine { unsigned int inner; int line; };
inline int LineOfInner(unsigned int inner)
{
    static const InnerLine k[] = { { kInnerLogPart, kLineRest }, { kInnerSpawn, kLineAnnounce }, { kInnerContext, kLineAnnounce },
                                   { kInnerAppearance, kLineAnnounce }, { kInnerClothing, kLineAnnounce } };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i)
        if (k[i].inner == inner) return k[i].line;
    return kLineLive;
}
/* Which line a guaranteed frame waits on: a well-formed LIVE envelope goes on its inner type's line (LineOfInner); every other frame
   (records, answers, anything unreadable) on the REST line - the safe side. */
inline int LineOf(unsigned int type, const char* p, size_t n, int dir)
{
    if (type != kTypeLive || p == 0) return kLineRest;
    unsigned int inner = 0;
    if (dir == kDirUp) { if (n < kLiveUpHeader) return kLineRest; inner = (unsigned char)p[1]; }
    else if (dir == kDirDown) { if (n < kLiveDownHeader) return kLineRest; inner = (unsigned char)p[2]; }
    else return kLineRest;
    return LineOfInner(inner);
}
/* T-1118 (c): the kind a frame's bytes are measured under on the LIVE line (sendbound.h Outbox, with its sender Item.origin):
   0x100 | the inner type for a well-formed LIVE envelope, 0 for every other frame (never measured: an unchanged AREAMAP or
   PLAYERSECTORS that goes LIVE keeps its place next to its own map). */
inline unsigned int KindOf(unsigned int type, const char* p, size_t n, int dir)
{
    if (type != kTypeLive || p == 0) return 0;
    if (dir == kDirUp) return n < kLiveUpHeader ? 0u : (0x100u | (unsigned int)(unsigned char)p[1]);
    if (dir == kDirDown) return n < kLiveDownHeader ? 0u : (0x100u | (unsigned int)(unsigned char)p[2]);
    return 0;
}
/* T-1118 (c): the ORDER FAMILY of a measured kind (KindOf) among frames that name no character (sendbound.h Bare): a waiting BULK
   frame naming no character holds only its sender's later no-character frames of its own family. Each kind is its own family (a
   town price table, a faction relation; one kind of one sender also keeps its order by the sticky rule), except the WORLD OBJECTS -
   the item road (ITEM_MOVE, ITEM_REQUEST, ITEM_CONFIRM, ITEM_PLACED, ITEM_REVOKE, PARITY_REQ, PARITY_BOX, ITEM_HANDOVER), DOOR_STATE
   and BUILD - one family: a box's contents (PARITY_BOX) put before a move of an item in that box (ITEM_MOVE, owner 0) or an answer
   about one (ITEM_PLACED, a request id only) must reach the receiver first, or it lays the older list over the newer move; a building
   placed (BUILD) must arrive before its door's state or a box inside it, or the receiver does not know what they belong to. An
   unmeasured frame (kind 0) has no family (0). */
const unsigned int kFamilyItems = 0x200;   /* above every KindOf value (0x100 | inner) */
inline unsigned int BareFamily(unsigned int kind)
{
    static const unsigned int k[] = { kInnerItemMove, kInnerItemRequest, kInnerItemConfirm, kInnerItemPlaced, kInnerItemRevoke,
                                      kInnerParityReq, kInnerParityBox, kInnerItemHandover, kInnerDoorState, kInnerBuild };
    if ((kind & ~0xFFu) != 0x100u) return kind;
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i)
        if ((kind & 0xFFu) == k[i]) return kFamilyItems;
    return kind;
}
/* T-1118 (c), review MED: a frame whose characters are a LIST TO COMPARE, not characters it acts on - a ROSTER (70) CHECK (the copies
   a game holds as an owner's), ANSWER (the rows that differ) or HASH (a sum per sector). Its characters' orders, hits and speech do
   not depend on it, so waiting on the BULK line it holds none of them back (sendbound.h BulkNote); it still waits for their
   announcements itself, and still holds back a later announcement of one of them (AnnBlocked). Read from the message's own type,
   as SubjectsOf reads its characters. */
inline bool ListOnly(unsigned int type, const char* p, size_t n, int dir)
{
    const unsigned int k = KindOf(type, p, n, dir);
    return k == (0x100u | (unsigned int)spawnpace::kMsgRoster);
}
/* The characters a LIVE envelope's message names, for the per-character gate: EVERY character, read per inner type exactly as the
   receiver reads them before it makes or uses a copy (spawnpace::MessageSubjects - a target, an attacker, a hirer, a hand-over's
   members, a squad's members ...). kSubjNamed: *uids holds them (no zero, no repeat). kSubjSender: the message speaks for every
   character of its sender (a ROSTER HASH, a list cut short, or a CATCHUP_END - it closes the catch-up whose CONTEXT
   and SPAWN messages its sender put before it), *uids empty. kSubjNone: it names none, or it is not a LIVE envelope.
   *origin: the sender - the origin slot toward a game (kDirDown), 0 toward the world server (every frame there is this game's). */
enum { kSubjNone = 0, kSubjNamed = 1, kSubjSender = 2 };
inline int SubjectsOf(unsigned int type, const char* p, size_t n, int dir, std::vector<unsigned int>* uids, unsigned int* origin)
{
    if (uids == 0 || origin == 0) return kSubjNone;
    uids->clear(); *origin = 0;
    if (type != kTypeLive || p == 0) return kSubjNone;
    size_t at = 0; unsigned int inner = 0;
    if (dir == kDirUp) { if (n < kLiveUpHeader) return kSubjNone; at = kLiveUpHeader; inner = (unsigned char)p[1]; }
    else if (dir == kDirDown)
    {
        if (n < kLiveDownHeader) return kSubjNone;
        at = kLiveDownHeader; inner = (unsigned char)p[2];
        *origin = (unsigned int)(unsigned char)p[0] | ((unsigned int)(unsigned char)p[1] << 8);
    }
    else return kSubjNone;
    if (inner == kInnerCatchupEnd) return kSubjSender;   /* after every announcement of its sender put before it */
    const int r = spawnpace::MessageSubjects(inner, (const unsigned char*)p + at, n - at, uids);
    if (r == spawnpace::kSubjectsSender) { uids->clear(); return kSubjSender; }
    return uids->empty() ? kSubjNone : kSubjNamed;
}
/* A REST frame no live frame sent after it may pass: toward a game the overtake audit's three (WELCOME - a LIVE before it is dropped;
   AREAMAP - item moves judged against a stale holder; PLAYER_GONE - a returning player's new copies destroyed); toward the world
   server JOIN_STAGE (the world server refuses a world-road game's LIVE until its stage says it is in the world). */
inline bool IsBarrier(unsigned int type, int dir)
{
    if (dir == kDirDown) return type == kTypeWelcome || type == kTypeAreaMap || type == kTypePlayerGone;
    return dir == kDirUp && type == kTypeJoinStage;
}

inline bool CutsInto(size_t frameBytes) { return frameBytes > kChunkBytes; }
/* The pieces a frame is cut into when every piece carries kChunkBytes - the fewest; pieces cut to the room left (PieceBytes) are more. */
inline size_t ChunkCount(size_t frameBytes) { return frameBytes <= kChunkBytes ? 1 : (frameBytes + kChunkBytes - 1) / kChunkBytes; }

/* The frame bytes the next piece carries. `any` (a graceful close hands everything over, room or not): kChunkBytes. Otherwise the
   room left under the budget at this moment (budget - netBytes, less the piece's own 13 header bytes), between kChunkMinBytes and
   kChunkBytes: a piece cut here takes what the network holds past the budget by at most one floor-sized piece and its header
   (kChunkMinBytes + 13), so a live message handed over next waits behind about the budget, not the budget plus a whole kChunkBytes
   piece. Not bounded by this: a frame of up to kChunkBytes goes whole, and a CHUNK a bridge passes on is never cut again, so either can
   still take it past the budget by up to kChunkBytes + 13 bytes. */
inline size_t PieceBytes(long long budget, long long netBytes, bool any)
{
    if (any) return kChunkBytes;
    const long long room = budget - netBytes - (long long)(kFrameHeader + kChunkHeader);
    if (room <= (long long)kChunkMinBytes) return kChunkMinBytes;
    if (room >= (long long)kChunkBytes) return kChunkBytes;
    return (size_t)room;
}

/* One CHUNK frame (its own 5-byte header included) carrying frame[offset .. offset + maxBytes) of a frameBytes-long frame, into *out;
   maxBytes 0 or over kChunkBytes counts as kChunkBytes. Answers the frame bytes it carries; 0 (and *out untouched) for bad arguments
   or a frame longer than kJoinMax. The receiver (Joiner) joins pieces of any size from 1 to kChunkBytes. */
inline size_t ChunkEncodeUpTo(const char* frame, size_t frameBytes, size_t offset, size_t maxBytes, std::vector<char>* out)
{
    if (out == 0 || frame == 0 || offset >= frameBytes || frameBytes > kJoinMax) return 0;
    if (maxBytes == 0 || maxBytes > kChunkBytes) maxBytes = kChunkBytes;
    size_t n = frameBytes - offset;
    if (n > maxBytes) n = maxBytes;
    const unsigned int body = (unsigned int)(kChunkHeader + n), total = (unsigned int)frameBytes, at = (unsigned int)offset;
    out->resize(kFrameHeader + body);
    char* p = &(*out)[0];
    p[0] = (char)(unsigned char)kMsgChunk;
    std::memcpy(p + 1, &body, 4);
    std::memcpy(p + 5, &total, 4);
    std::memcpy(p + 9, &at, 4);
    std::memcpy(p + 13, frame + offset, n);
    return n;
}
/* ChunkEncodeUpTo with a full piece: frame[offset .. offset + kChunkBytes). */
inline size_t ChunkEncode(const char* frame, size_t frameBytes, size_t offset, std::vector<char>* out)
{
    return ChunkEncodeUpTo(frame, frameBytes, offset, kChunkBytes, out);
}

/* A joined frame's own header: true when it is a whole frame (declared length = the bytes after the header); *type its type. */
inline bool FrameOk(const std::vector<char>& f, unsigned int* type)
{
    if (f.size() < kFrameHeader) return false;
    unsigned int n = 0;
    std::memcpy(&n, &f[1], 4);
    if ((size_t)n != f.size() - kFrameHeader) return false;
    if (type != 0) *type = (unsigned char)f[0];
    return true;
}

/* The receiver's verdict on one CHUNK. More / Done / Dup (a piece already joined - ignored) are not faults; the rest are, and each
   throws away the frame being joined (a guaranteed stream with a hole in it cannot be trusted past the hole): Short (no header or an
   empty piece), NoFirst (a piece of a frame whose first piece never came), Gap (a piece is missing - lost), Total (the frame length
   changed mid-frame, or the piece runs past it), TooBig (over kJoinMax, or under one frame header), Restart (a new frame's first
   piece while one was open: the open one lost its tail; the new one is now being joined). */
enum { kJoinMore = 0, kJoinDone = 1, kJoinDup = 2, kJoinShort = 3, kJoinNoFirst = 4, kJoinGap = 5, kJoinTotal = 6, kJoinTooBig = 7, kJoinRestart = 8 };
inline bool JoinFault(int v) { return v >= kJoinShort; }
inline const char* JoinWord(int v)
{
    switch (v)
    {
    case kJoinMore: return "more";
    case kJoinDone: return "done";
    case kJoinDup: return "a piece already joined (ignored)";
    case kJoinShort: return "a piece with no header or no bytes";
    case kJoinNoFirst: return "a piece of a message whose first piece never came";
    case kJoinGap: return "a missing piece (the message is lost)";
    case kJoinTotal: return "a piece that disagrees with its message's length";
    case kJoinTooBig: return "a message longer than 32 MiB or shorter than its header";
    case kJoinRestart: return "a new message began before the open one ended (the open one is lost)";
    default: return "unknown";
    }
}

/* One link's joiner: at most one frame open at a time (the sender cuts one frame at a time). Lost() counts every frame thrown away
   part-joined (a fault verdict with a frame open, or a Restart). */
class Joiner
{
public:
    Joiner() : m_total(0), m_open(false), m_lost(0), m_pieces(0), m_lastPieces(0) {}
    void Reset() { if (m_open) ++m_lost; Drop(); }
    bool Open() const { return m_open; }
    size_t Have() const { return m_buf.size(); }
    long long Lost() const { return m_lost; }
    /* The pieces the last whole frame was joined from (repeats not counted); 0 before the first. For the receivers' 'joined from N
       pieces' log lines: pieces are cut to the room left (PieceBytes), so the frame's size no longer says how many there were. */
    size_t LastPieces() const { return m_lastPieces; }
    /* One CHUNK's payload. kJoinDone hands the whole frame over in *whole (swapped out; the joiner is empty again). A Restart whose new
       frame is a single piece answers kJoinDone (the lost open frame is in Lost()). */
    int Feed(const char* p, size_t n, std::vector<char>* whole)
    {
        if (p == 0 || n <= kChunkHeader) { Reset(); return kJoinShort; }
        unsigned int total = 0, at = 0;
        std::memcpy(&total, p, 4);
        std::memcpy(&at, p + 4, 4);
        const size_t piece = n - kChunkHeader;
        if ((size_t)total > kJoinMax || (size_t)total < kFrameHeader) { Reset(); return kJoinTooBig; }
        if ((size_t)at + piece > (size_t)total) { Reset(); return kJoinTotal; }
        int verdict = kJoinMore;
        if (m_open)
        {
            if (at == 0)
            {
                if ((size_t)total == m_total && piece <= m_buf.size() && std::memcmp(&m_buf[0], p + kChunkHeader, piece) == 0) return kJoinDup;
                Reset(); verdict = kJoinRestart;   /* the new frame begins below */
            }
            else if ((size_t)total != m_total) { Reset(); return kJoinTotal; }
            else if ((size_t)at < m_buf.size())
            {
                if ((size_t)at + piece <= m_buf.size()) return kJoinDup;
                Reset(); return kJoinTotal;
            }
            else if ((size_t)at > m_buf.size()) { Reset(); return kJoinGap; }
        }
        if (!m_open)
        {
            if (at != 0) return kJoinNoFirst;
            m_open = true; m_total = total; m_buf.clear();
            /* at most a few pieces up front: the claimed length is only the first piece's word, up to 32 MiB */
            m_buf.reserve((size_t)total < 4 * kChunkBytes ? (size_t)total : 4 * kChunkBytes);
        }
        m_buf.insert(m_buf.end(), p + kChunkHeader, p + n);
        ++m_pieces;
        if (m_buf.size() < m_total) return verdict;
        if (whole != 0) whole->swap(m_buf);
        m_lastPieces = m_pieces;
        Drop();
        return kJoinDone;
    }
private:
    void Drop() { m_buf.clear(); m_total = 0; m_open = false; m_pieces = 0; }
    std::vector<char> m_buf;
    size_t m_total;
    bool m_open;
    long long m_lost;
    size_t m_pieces;       /* pieces joined into the open frame */
    size_t m_lastPieces;   /* LastPieces */
};

/* What a link drains, measured: the bytes that LEFT the network queue (acknowledged, or put on the wire - whichever the caller counts)
   per second, over windows of at least kRateWindowSec in which record pieces waited (a quiet link keeps the last rate, so the next file
   starts at the speed the last one reached). Budget(aheadSec): the record bytes the network may hold - rate x (aheadSec + kAheadSec),
   between kBudgetMin and kBudgetMax; aheadSec is the round trip when the caller's count keeps bytes until they are acknowledged
   (the game's ENet link), 0 when it counts only bytes not yet sent (the world server's). */
class Rate
{
public:
    Rate() : m_at(-1.0), m_left(0), m_rate(0.0), m_busy(false) {}
    void Note(double now, long long leftTotal, bool busy)
    {
        if (m_at < 0.0 || now < m_at || leftTotal < m_left) { m_at = now; m_left = leftTotal; m_busy = busy; return; }
        m_busy = m_busy || busy;
        const double dt = now - m_at;
        if (dt < kRateWindowSec) return;
        const long long d = leftTotal - m_left;
        if (m_busy && d > 0)
        {
            const double r = (double)d / dt;
            m_rate = m_rate <= 0.0 ? r : 0.5 * m_rate + 0.5 * r;
        }
        m_at = now; m_left = leftTotal; m_busy = busy;
    }
    double BytesPerSec() const { return m_rate; }
    long long Budget(double aheadSec) const;
private:
    double m_at;
    long long m_left;
    double m_rate;
    bool m_busy;
};

/* task 6 (0.4.0 contributor brief): WHEN THE GAME'S WORLD-SERVER LINK MEASURES ITS DRAIN RATE. Rate::Note measures only in windows in
   which `busy` was true. The game used to pass "record pieces wait", so a burst of introductions and live frames after entering an area
   (no record pieces) kept the last, low rate: the budget sat at kBudgetMin and live updates waited behind the introductions for ~2 s. The
   game now passes "any frame waits in the send line" (record, live, introduction, BULK, held or unreliable - the outbox's own count).
   Rate itself is unchanged, so the world server's call (store_main.cpp, record pieces only) gives the same results as before. */
inline bool GameDrainBusy(bool restWaiting, long long framesWaiting) { return restWaiting || framesWaiting > 0; }

/* The record bytes a link's network queue may hold at a drain rate of `bytesPerSec`: rate x (aheadSec + kAheadSec), between kBudgetMin
   and kBudgetMax. A Steam link knows its rate (the send rate it set on Steam) and counts only bytes Steam has not sent: aheadSec 0. */
inline long long BudgetOf(double bytesPerSec, double aheadSec)
{
    if (aheadSec < 0.0) aheadSec = 0.0;
    if (!(bytesPerSec > 0.0)) bytesPerSec = 0.0;
    const double b = bytesPerSec * (aheadSec + kAheadSec);
    if (b <= (double)kBudgetMin) return kBudgetMin;
    if (b >= (double)kBudgetMax) return kBudgetMax;
    return (long long)b;
}
inline long long Rate::Budget(double aheadSec) const { return BudgetOf(m_rate, aheadSec); }

/* T-1118 (b) stage 2 - A LINK THAT HANDS PIECES ON (the host's Steam bridge: from the world server to a Steam joiner and back). The
   bridge never sees a cut frame whole, so it cannot read a barrier's type from the CHUNK it hands on; this remembers the type from
   each frame's first piece and says which piece is the LAST of a barrier frame - that piece is put on the far side as the barrier.
   Why the last: the sender holds every live frame put after a barrier until the barrier's last piece has gone, so on the wire those
   live frames come after that piece; live frames between the pieces were put before the barrier and may pass it. A last piece of a
   frame whose first piece was not seen here is a barrier too (the safe side: it only holds live frames a little). */
class HandOn
{
public:
    HandOn() : m_type(0), m_open(false) {}
    void Reset() { m_type = 0; m_open = false; }
    /* One CHUNK's payload handed on toward `dir` (kDirUp / kDirDown): true = put it as a barrier. */
    bool LastOfBarrier(const char* p, size_t n, int dir)
    {
        if (p == 0 || n <= kChunkHeader) { Reset(); return false; }
        unsigned int total = 0, at = 0;
        std::memcpy(&total, p, 4);
        std::memcpy(&at, p + 4, 4);
        if (at == 0) { m_type = (unsigned char)p[kChunkHeader]; m_open = true; }
        if ((size_t)at + (n - kChunkHeader) < (size_t)total) return false;   /* more pieces follow */
        const bool known = m_open;
        const unsigned int type = m_type;
        Reset();
        return !known || IsBarrier(type, dir);
    }
private:
    unsigned int m_type;
    bool m_open;
};

/* THE AREAMAP REPEAT. The world server sends every game its AREAMAP (35) every second and PLAYERSECTORS (37) right after it. The
   game judges zone records against the holders in its latest map, so a map whose CONTENT changed is a barrier on the REST line (it
   may not overtake zone files put before it). A map whose bytes equal the last one put on the link changes nothing at the game but
   the map's 5-second clock (every holder answer reads -1 once the last map is over 5 s old) - yet as a barrier it would hold every
   later live message behind the whole of a big download, about a second into it. So, per link (Reset at a new connection):
     - an AREAMAP whose bytes differ from the last one put here (or the first one): REST, a barrier - kMapChanged;
     - the same bytes, no AREAMAP waiting on this link's REST line (whole or partly cut), and it goes whole (not over kChunkBytes):
       the LIVE line, not a barrier - kMapRepeatLive (the game already holds this exact map, or is handed it ahead of this one);
     - the same bytes otherwise: REST, not a barrier - kMapRepeatRest (it keeps its place behind the waiting map, which keeps its
       own barrier);
     - a PLAYERSECTORS goes LIVE only when the last AREAMAP put here since the previous PLAYERSECTORS went LIVE, its bytes equal the
       last PLAYERSECTORS put here, none waits on the REST line and it goes whole; otherwise REST as before - so it never arrives
       ahead of its own map, nor ahead of an earlier different PLAYERSECTORS.
   A link that hands frames on (the host's Steam bridge) sees a big map only as CHUNK pieces: the first piece of an AREAMAP or a
   PLAYERSECTORS forgets everything remembered here, so the next whole one counts as changed (the safe side). Frames are whole frames,
   header included. */
enum { kMapChanged = 0, kMapRepeatLive = 1, kMapRepeatRest = 2 };
class MapRepeat
{
public:
    MapRepeat() : m_haveMap(false), m_haveSectors(false), m_mapLive(false) {}
    void Reset() { m_map.clear(); m_sectors.clear(); m_haveMap = false; m_haveSectors = false; m_mapLive = false; }
    /* An AREAMAP put on this link; mapWaiting = an AREAMAP (whole or partly cut) still waits on this link's REST line. */
    int Map(const char* frame, size_t n, bool mapWaiting)
    {
        if (frame == 0 || n == 0) { Reset(); return kMapChanged; }
        if (!Same(m_haveMap, m_map, frame, n))
        {
            m_map.assign(frame, frame + n); m_haveMap = true; m_mapLive = false;
            return kMapChanged;
        }
        m_mapLive = !mapWaiting && !CutsInto(n);
        return m_mapLive ? kMapRepeatLive : kMapRepeatRest;
    }
    /* A PLAYERSECTORS put on this link: true = the LIVE line. Its bytes are remembered either way. */
    bool SectorsLive(const char* frame, size_t n, bool sectorsWaiting)
    {
        const bool mapLive = m_mapLive;
        m_mapLive = false;
        if (frame == 0 || n == 0) { m_sectors.clear(); m_haveSectors = false; return false; }
        if (!Same(m_haveSectors, m_sectors, frame, n))
        {
            m_sectors.assign(frame, frame + n); m_haveSectors = true;
            return false;
        }
        return mapLive && !sectorsWaiting && !CutsInto(n);
    }
    /* One guaranteed frame toward a game, whole (header included). An AREAMAP or a PLAYERSECTORS: *line and *barrier set as above,
       true. A CHUNK (a link handing pieces on): the first piece of an AREAMAP or a PLAYERSECTORS forgets what is remembered; false,
       *line and *barrier untouched. Any other frame: false, untouched. */
    bool Classify(unsigned int type, const char* frame, size_t n, bool mapWaiting, bool sectorsWaiting, int* line, bool* barrier)
    {
        if (type == kMsgChunk)
        {
            if (frame != 0 && n > kFrameHeader + kChunkHeader)
            {
                unsigned int at = 0;
                std::memcpy(&at, frame + kFrameHeader + 4, 4);
                const unsigned int inner = (unsigned char)frame[kFrameHeader + kChunkHeader];
                if (at == 0 && (inner == kTypeAreaMap || inner == kTypePlayerSectors)) Reset();
            }
            return false;
        }
        if (type == kTypeAreaMap)
        {
            const int v = Map(frame, n, mapWaiting);
            *line = v == kMapRepeatLive ? kLineLive : kLineRest;
            *barrier = v == kMapChanged;
            return true;
        }
        if (type == kTypePlayerSectors)
        {
            *line = SectorsLive(frame, n, sectorsWaiting) ? kLineLive : kLineRest;
            *barrier = false;
            return true;
        }
        return false;
    }
private:
    static bool Same(bool have, const std::vector<char>& last, const char* frame, size_t n)
    {
        return have && last.size() == n && std::memcmp(&last[0], frame, n) == 0;
    }
    std::vector<char> m_map, m_sectors;
    bool m_haveMap, m_haveSectors;
    bool m_mapLive;   /* the last AREAMAP put here since the last PLAYERSECTORS went LIVE */
};

/* What a link's waiting line did (sendbound.h Outbox fills it in live-first mode; DrainStats hands it over): chunks = CHUNK messages
   cut, cutFrames = frames cut into them, livePassed = live frames that went to the network while record (REST) frames waited ahead of
   them, liveWaited = live frames that waited at all, liveWaitMaxSec = the longest a live frame waited, barrierHeld = times a live frame
   was held because a barrier waited ahead of it; announceQueued / announceSent = announcements (ANNOUNCE line) that waited / left
   the line, announceWaitMaxSec = the longest an announcement waited on the ANNOUNCE line, gateHeld = live frames held behind an
   announcement of a character they name (their wait counts in liveWaited / liveWaitMaxSec when they leave).
   T-1118 (c): bulkQueued / bulkSent = frames that waited on / left the BULK line, bulkHeld = live frames held behind a waiting BULK
   frame naming one of their characters (also counted in gateHeld), bulkBySize / bulkByShare / bulkBySticky = frames moved off the
   LIVE line (straight out or waiting) by each reason, liveAheadMaxBytes = the most bytes the network held when a guaranteed live frame
   was handed to it, bulkWaitMaxSec = the longest a frame waited on the BULK line. */
struct Stats
{
    long long chunks, cutFrames, livePassed, liveWaited, barrierHeld, announceQueued, announceSent, gateHeld;
    double liveWaitMaxSec, announceWaitMaxSec;
    long long bulkQueued, bulkSent, bulkHeld, bulkBySize, bulkByShare, bulkBySticky, liveAheadMaxBytes;
    double bulkWaitMaxSec;
    Stats() : chunks(0), cutFrames(0), livePassed(0), liveWaited(0), barrierHeld(0), announceQueued(0), announceSent(0), gateHeld(0),
              liveWaitMaxSec(0.0), announceWaitMaxSec(0.0), bulkQueued(0), bulkSent(0), bulkHeld(0), bulkBySize(0), bulkByShare(0),
              bulkBySticky(0), liveAheadMaxBytes(0), bulkWaitMaxSec(0.0) {}
    void Add(const Stats& s)
    {
        chunks += s.chunks; cutFrames += s.cutFrames; livePassed += s.livePassed; liveWaited += s.liveWaited; barrierHeld += s.barrierHeld;
        announceQueued += s.announceQueued; announceSent += s.announceSent; gateHeld += s.gateHeld;
        if (s.liveWaitMaxSec > liveWaitMaxSec) liveWaitMaxSec = s.liveWaitMaxSec;
        if (s.announceWaitMaxSec > announceWaitMaxSec) announceWaitMaxSec = s.announceWaitMaxSec;
        bulkQueued += s.bulkQueued; bulkSent += s.bulkSent; bulkHeld += s.bulkHeld;
        bulkBySize += s.bulkBySize; bulkByShare += s.bulkByShare; bulkBySticky += s.bulkBySticky;
        if (s.liveAheadMaxBytes > liveAheadMaxBytes) liveAheadMaxBytes = s.liveAheadMaxBytes;
        if (s.bulkWaitMaxSec > bulkWaitMaxSec) bulkWaitMaxSec = s.bulkWaitMaxSec;
    }
    /* The maxima restart at each REPORT (the callers' own liveWaitMaxSec / announceWaitMaxSec restart beside these). */
    void RestartBulkMaxima() { liveAheadMaxBytes = 0; bulkWaitMaxSec = 0.0; }
};
/* T-1118 (c): the BULK line's fields, appended at the end of every liveFirst[...] report token (the game's ENet and Steam links, the
   Steam total, the bridge's links, the world server): the names (each with its leading comma) and the values in the same order. */
inline std::string BulkTokenNames() { return ",bulkQueued,bulkSent,bulkHeld,bulkBySize,bulkByShare,bulkBySticky,bulkMaxWaitMs,liveAheadMaxBytes"; }
inline std::string BulkTokenValues(const Stats& s)
{
    std::ostringstream o;
    o << "," << s.bulkQueued << "," << s.bulkSent << "," << s.bulkHeld << "," << s.bulkBySize << "," << s.bulkByShare << "," << s.bulkBySticky
      << "," << (long long)(s.bulkWaitMaxSec * 1000.0) << "," << s.liveAheadMaxBytes;
    return o.str();
}

}   /* namespace livefirst */

#endif   /* SW_LIVEFIRST_H */
