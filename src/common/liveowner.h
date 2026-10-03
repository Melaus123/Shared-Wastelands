/* M7a stage A1 build 1 (owner decision 347 a, Design A; .modding/investigations/m7a-a1-design-2026-10-02.md sections 1-3) - LIVE
   OWNERSHIP: every decision of the build as a pure function and every new or changed wire as one codec, in one header the plugin and
   the offline suite (src/coop-test/test_main.cpp) both compile. C++03, no globals, no engine types.

   What lives here (design numbers):
     3.1  WorldFirstRoute     - the road of an A1 message addressed to ONE game: the world server by slot first, the session link only
                                while this game's notebook link is down and the target is the session peer, else none (outbox keeps it)
     3.5  Mix64 / RosterRowHash / RosterHashStep / RosterHashOf - the per-sector roster hash (XOR of a mixed (uid, gen) per person)
     3.6  RosterCheckDue      - does a copy holder ask an owner for a CHECK now
     3.7  RosterAnswerFor     - the owner's answer for one uid it was asked about
     3.8  RosterApply         - the holder's action on one ANSWER row (bounded conflict exit; the recorded owner's NOT-LIVE withdraws)
     3.9  DualRunResolve      - two games run one person: the higher generation keeps it, a tie goes to the lower slot
     3.12 AgreedSquadLeaderN  - the follow-leader choice folded over several games' announcements
     3.13 ReceiptExpected     - does a withdrawal owe a receipt from that game
     3.14 ReceiptAction       - one outbox row's next step for one game (resend by slot 2/4/8/8 s, done four ways, give up at 60 s)
     3.16 OrphanCopyAction    - a copy with no owner record: kept for 20 s of link-up time, then withdrawn
     3.15 codecs              - ROSTER (70; HASH / CHECK / ANSWER, chunked under the envelope cap), RECEIPT (71), UNLOAD / DESPAWN with
                                seq + gen + expected receivers, OWNER_MOVED 16 bytes with gen, the XFER generation tail
   BUILD 2 (the switch-over, [a1b2-lo0], the block at the end): 3.2 AdoptDecide, 3.3 ReceiverRingPick (+ RingKind), 3.4 ReleaseStep /
   PutAwayDuringFlight / ReleaseOfferGen, 3.10 GiverSettleOnAck, the late-adopter and REVOKE rules, 3.11 SquadGivenEffective and the
   RELEASE (68) / RELEASE_ACK (69) codecs. */
#ifndef COOP_LIVEOWNER_H
#define COOP_LIVEOWNER_H

#include <vector>
#include <map>
#include <cstring>
#include <cstddef>

namespace cooplo {

typedef unsigned long long u64;

/* ---- 3.1 the road ------------------------------------------------------------------------------------------------------------ */
enum { kRoadNone = 0, kRoadLive = 1, kRoadSession = 2 };
struct Road { int road; int slot; Road() : road(kRoadNone), slot(-1) {} };
/* targetInWorld: StoreRosterSlotInWorld(targetSlot) (1 in world, 0 not, -1 no roster). sessionPeerSlot: LinkPeerSlot() (-1 unknown). */
inline Road WorldFirstRoute(int targetSlot, bool liveReady, int targetInWorld, bool sessionUp, int sessionPeerSlot)
{
    Road r;
    if (targetSlot < 0) return r;
    if (liveReady && targetInWorld == 1) { r.road = kRoadLive; r.slot = targetSlot; return r; }
    if (!liveReady && sessionUp && sessionPeerSlot >= 0 && sessionPeerSlot == targetSlot) { r.road = kRoadSession; r.slot = targetSlot; }
    return r;
}

/* ---- 3.5 the roster hash ----------------------------------------------------------------------------------------------------- */
inline u64 Mix64(u64 z)   /* splitmix64's finalizer */
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
inline u64 RosterRowHash(unsigned int uid, unsigned int gen) { return Mix64(((u64)uid << 32) | (u64)gen); }
/* incremental: XOR the old row out (was), the new row in (is) */
inline u64 RosterHashStep(u64 h, unsigned int uid, unsigned int oldGen, unsigned int newGen, int was, int is)
{
    if (was) h ^= RosterRowHash(uid, oldGen);
    if (is) h ^= RosterRowHash(uid, newGen);
    return h;
}
inline u64 RosterHashOf(const unsigned int* uids, const unsigned int* gens, size_t n)
{
    u64 h = 0;
    for (size_t i = 0; i < n; ++i) h ^= RosterRowHash(uids[i], gens[i]);
    return h;
}

/* ---- 3.6 when a holder checks an owner ------------------------------------------------------------------------------------- */
const double kRosterSec = 5.0;        /* the owner's HASH period and the holder's least gap between two CHECKs of one owner */
const double kRosterQuietSec = 30.0;  /* a CHECK of every copy of an owner at least this often (a lost HASH, far copies) */
/* differs: the owner's latest hash of a sector near this game differs from this game's copies there. edge: a WELCOME edge (this
   game's notebook link came back) since the last CHECK of that owner. conflictPending: an ANSWER asked for a re-check. */
inline int RosterCheckDue(int copiesFromOwner, int differs, double sinceCheck, int edgeSinceCheck, int conflictPending, double rosterSec, double maxQuietSec)
{
    if (copiesFromOwner <= 0 && conflictPending == 0) return 0;
    if (edgeSinceCheck) return 1;
    if (sinceCheck < rosterSec) return 0;
    if (conflictPending || differs) return 1;
    return sinceCheck >= maxQuietSec ? 1 : 0;
}
/* the full ("far copies") CHECK: at an edge or past the quiet time - every copy of that owner, sectorKey kRosterAllSectors */
inline int RosterCheckIsFull(int edgeSinceCheck, double sinceFullCheck, double maxQuietSec) { return (edgeSinceCheck || sinceFullCheck >= maxQuietSec) ? 1 : 0; }

/* ---- 3.7 the owner's answer -------------------------------------------------------------------------------------------------- */
enum { kAnsAgree = 0, kAnsLive = 1 /* AGREE-NEWER: I run it at gen (row sent only when gen differs from the asker's) */,
       kAnsMoved = 2, kAnsPending = 3, kAnsNotLive = 4 };
struct Answer { int status; unsigned int gen; int slot; Answer() : status(kAnsAgree), gen(0), slot(0xFFFF) {} };
/* iRunLive: this game runs uid and its body stands here. inOpenRelease (build 1 [review F1]): owed record or owed XFER in flight,
   a held UNLOAD, an XFER naming it awaiting its ACK, or this frame's put-away batch. recordOwnerSlot: the slot of the game this one
   recorded as uid's owner (-1 none / this game). */
inline Answer RosterAnswerFor(int iRunLive, unsigned int myGen, int inOpenRelease, int recordOwnerSlot, unsigned int recordGen, unsigned int askerGen)
{
    Answer a;
    if (inOpenRelease) { a.status = kAnsPending; a.gen = myGen; return a; }
    if (iRunLive)
    {
        if (myGen == askerGen) return a;   /* agrees: the row is not sent */
        a.status = kAnsLive; a.gen = myGen; return a;
    }
    if (recordOwnerSlot >= 0) { a.status = kAnsMoved; a.gen = recordGen; a.slot = recordOwnerSlot; return a; }
    a.status = kAnsNotLive; a.gen = myGen > recordGen ? myGen : recordGen; return a;
}

/* ---- 3.8 the holder's action on one ANSWER row ------------------------------------------------------------------------------- */
enum { kApKeep = 0, kApRestamp = 1, kApRekey = 2, kApWithdraw = 3, kApConflict = 4 };
const int kRosterConflictMax = 2;   /* at most two re-checks of one disagreement, then the recorded owner's latest word stands */
/* answerSlot: the slot the answer names (MOVED: the new owner; LIVE: the answering game). answerFromRecordedOwner: the answering game
   is the owner this game recorded for the copy. streamSinceCheck: that owner's stream named uid after the CHECK went. */
inline int RosterApply(unsigned int copyGen, int status, unsigned int answerGen, int answerSlotIsMe, int answerFromRecordedOwner,
                       int streamSinceCheck, int conflictsSoFar)
{
    const int capped = conflictsSoFar >= kRosterConflictMax ? 1 : 0;
    switch (status)
    {
    case kAnsPending: return kApKeep;   /* frozen by design while it is handed on (owner 334 a) - not counted frozen */
    case kAnsLive:
        if (answerGen >= copyGen) return answerFromRecordedOwner ? (int)kApRestamp : (int)kApRekey;
        if (!capped) return kApConflict;
        return answerFromRecordedOwner ? (int)kApRestamp : (int)kApKeep;
    case kAnsMoved:
        /* names THIS game, which does not run it (a run person never reaches here): from the recorded owner nobody runs it - withdrawn
           like its NOT-LIVE (else a frozen copy); from another game nothing a copy here can do */
        if (answerSlotIsMe) return answerFromRecordedOwner ? (streamSinceCheck ? (int)kApKeep : (int)kApWithdraw) : (int)kApKeep;
        if (answerGen >= copyGen) return kApRekey;
        if (!capped) return kApConflict;
        return answerFromRecordedOwner ? (int)kApRekey : (int)kApKeep;
    case kAnsNotLive:
        if (answerFromRecordedOwner) return streamSinceCheck ? (int)kApKeep : (int)kApWithdraw;   /* [review F2] whatever the gens */
        if (copyGen <= answerGen) return kApWithdraw;
        return capped ? (int)kApKeep : (int)kApConflict;
    default: return kApKeep;
    }
}

/* ---- 3.9 a dual run ---------------------------------------------------------------------------------------------------------- */
enum { kIKeep = 0, kIYield = 1 };
inline int DualRunResolve(unsigned int myGen, unsigned int theirGen, int mySlot, int theirSlot)
{
    if (myGen != theirGen) return myGen > theirGen ? (int)kIKeep : (int)kIYield;
    return (mySlot >= 0 && (theirSlot < 0 || mySlot < theirSlot)) ? (int)kIKeep : (int)kIYield;
}

/* ---- 3.12 the follow-leader choice over several games ----------------------------------------------------------------------- */
/* coopsquad::AgreedSquadLeader folded: a named leader beats none, the lower uid wins a difference */
inline unsigned int AgreedSquadLeaderN(unsigned int myLead, const unsigned int* peerLeads, int n)
{
    unsigned int best = myLead;
    for (int i = 0; i < n; ++i)
    {
        const unsigned int p = peerLeads[i];
        if (p == 0) continue;
        if (best == 0 || p < best) best = p;
    }
    return best;
}

/* ---- 3.13 / 3.14 receipts ---------------------------------------------------------------------------------------------------- */
const int kReceiptRadius = 3;            /* ring 1 + delivery margin 1 + 1 slack */
const double kReceiptGiveUpSec = 60.0;   /* of ROAD-UP time */
inline int ReceiptExpected(int slotInWorld, int playerSectorKnown, int cheb, int radius)
{
    return (slotInWorld == 1 && playerSectorKnown && cheb >= 0 && cheb <= radius) ? 1 : 0;
}
inline int Cheb(int ax, int ay, int bx, int by) { const int dx = ax > bx ? ax - bx : bx - ax, dy = ay > by ? ay - by : by - ay; return dx > dy ? dx : dy; }
inline double ReceiptBackoffSec(int sends) { return sends <= 1 ? 2.0 : (sends == 2 ? 4.0 : 8.0); }
enum { kRcWait = 0, kRcResend = 1, kRcDoneAcked = 2, kRcDoneRoster = 3, kRcDoneGone = 4, kRcGiveUp = 5 };
/* per (row, slot). sinceLast: seconds since the last send to that slot; roadUpSec: road-up seconds since the first send. */
inline int ReceiptAction(double sinceLast, int sends, int roadUp, double roadUpSec, int acked, int rosterShows, int slotGone)
{
    if (acked) return kRcDoneAcked;
    if (rosterShows) return kRcDoneRoster;
    if (slotGone) return kRcDoneGone;
    if (roadUpSec >= kReceiptGiveUpSec) return kRcGiveUp;
    if (!roadUp) return kRcWait;
    return sinceLast >= ReceiptBackoffSec(sends) ? (int)kRcResend : (int)kRcWait;
}
/* a withdrawal one holder receives: stale when its copy already carries a HIGHER gen (it heard of a later owner) [review c] */
inline int WithdrawalStale(unsigned int copyGen, unsigned int msgGen) { return (msgGen != 0 && copyGen > msgGen) ? 1 : 0; }

/* ---- 3.16 a copy with no owner record [review a] ----------------------------------------------------------------------------- */
const double kOrphanCopySec = 20.0;
enum { kOrphanKeep = 0, kOrphanWithdraw = 1 };
inline int OrphanCopyAction(int hasOwnerRecord, double orphanLinkUpSec, int writesBlocked, double limitSec)
{
    if (hasOwnerRecord || writesBlocked) return kOrphanKeep;
    return orphanLinkUpSec >= limitSec ? (int)kOrphanWithdraw : (int)kOrphanKeep;
}

/* ---- 3.15 codecs ------------------------------------------------------------------------------------------------------------- */
const unsigned int kEnvelopeCap = 65536;   /* liveenvelope.h kLiveInnerMax - every message below stays under it */
inline void PutU16(std::vector<char>* b, unsigned int v) { const unsigned short s = (unsigned short)v; const size_t at = b->size(); b->resize(at + 2); std::memcpy(&(*b)[at], &s, 2); }
inline void PutU32(std::vector<char>* b, unsigned int v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline void PutU64(std::vector<char>* b, u64 v) { const size_t at = b->size(); b->resize(at + 8); std::memcpy(&(*b)[at], &v, 8); }
inline unsigned int GetU16(const char* p) { unsigned short s = 0; std::memcpy(&s, p, 2); return s; }
inline unsigned int GetU32(const char* p) { unsigned int v = 0; std::memcpy(&v, p, 4); return v; }
inline u64 GetU64(const char* p) { u64 v = 0; std::memcpy(&v, p, 8); return v; }

/* ROSTER (70): u8 kind | u8 0 | u16 chunk | ... (design 1.3, per SECTOR [review F8]) */
enum { kRosterHash = 1, kRosterCheck = 2, kRosterAnswer = 3 };
const unsigned int kRosterAllSectors = 0xFFFF;   /* a CHECK of every copy of that owner held here (edge, 30 s quiet, "far copies") */
const unsigned int kRosterListed = 0xFFFE;       /* a CHECK of the listed uids only (a conflict's re-check, the dual-run detector) - every LIVE row is answered */
const size_t kRosterHashPerChunk = 4000, kRosterCheckPerChunk = 6000, kRosterAnswerPerChunk = 5000;
struct HashRow { unsigned int sectorKey, count; u64 hash; HashRow() : sectorKey(0), count(0), hash(0) {} };
struct CheckRow { unsigned int uid, gen; CheckRow() : uid(0), gen(0) {} };
struct AnswerRow { unsigned int uid; int status; unsigned int slot, gen; AnswerRow() : uid(0), status(0), slot(0xFFFF), gen(0) {} };
struct RosterMsg
{
    int kind; unsigned int chunk, lastChunk;
    unsigned int seq;          /* HASH: rosterSeq; CHECK / ANSWER: checkNo */
    unsigned int rosterSeq;    /* ANSWER only */
    unsigned int sectorKey;    /* CHECK only */
    std::vector<HashRow> hashes; std::vector<CheckRow> checks; std::vector<AnswerRow> answers;
    RosterMsg() : kind(0), chunk(0), lastChunk(0), seq(0), rosterSeq(0), sectorKey(kRosterAllSectors) {}
};
inline void RosterHead(std::vector<char>* b, int kind, unsigned int chunk) { b->push_back((char)kind); b->push_back(0); PutU16(b, chunk); }
/* Each returns the chunks (at least one, an empty list included); false only for a list too long for 65,535 chunks. */
inline bool RosterHashEncode(std::vector<std::vector<char> >* out, unsigned int rosterSeq, const std::vector<HashRow>& rows)
{
    out->clear();
    const size_t chunks = rows.empty() ? 1 : (rows.size() + kRosterHashPerChunk - 1) / kRosterHashPerChunk;
    if (chunks > 0xFFFF) return false;
    for (size_t c = 0; c < chunks; ++c)
    {
        std::vector<char> b; RosterHead(&b, kRosterHash, (unsigned int)c);
        const size_t from = c * kRosterHashPerChunk, to = (from + kRosterHashPerChunk < rows.size()) ? from + kRosterHashPerChunk : rows.size();
        PutU32(&b, rosterSeq); PutU16(&b, (unsigned int)(to - from)); PutU16(&b, (unsigned int)(chunks - 1));
        for (size_t i = from; i < to; ++i) { PutU16(&b, rows[i].sectorKey); PutU16(&b, rows[i].count > 0xFFFF ? 0xFFFF : rows[i].count); PutU64(&b, rows[i].hash); }
        out->push_back(b);
    }
    return true;
}
inline bool RosterCheckEncode(std::vector<std::vector<char> >* out, unsigned int checkNo, unsigned int sectorKey, const std::vector<CheckRow>& rows)
{
    out->clear();
    const size_t chunks = rows.empty() ? 1 : (rows.size() + kRosterCheckPerChunk - 1) / kRosterCheckPerChunk;
    if (chunks > 0xFFFF) return false;
    for (size_t c = 0; c < chunks; ++c)
    {
        std::vector<char> b; RosterHead(&b, kRosterCheck, (unsigned int)c);
        const size_t from = c * kRosterCheckPerChunk, to = (from + kRosterCheckPerChunk < rows.size()) ? from + kRosterCheckPerChunk : rows.size();
        PutU32(&b, checkNo); PutU16(&b, sectorKey); PutU16(&b, (unsigned int)(chunks - 1)); PutU32(&b, (unsigned int)(to - from));
        for (size_t i = from; i < to; ++i) { PutU32(&b, rows[i].uid); PutU32(&b, rows[i].gen); }
        out->push_back(b);
    }
    return true;
}
inline bool RosterAnswerEncode(std::vector<std::vector<char> >* out, unsigned int checkNo, unsigned int rosterSeq, const std::vector<AnswerRow>& rows)
{
    out->clear();
    const size_t chunks = rows.empty() ? 1 : (rows.size() + kRosterAnswerPerChunk - 1) / kRosterAnswerPerChunk;
    if (chunks > 0xFFFF) return false;
    for (size_t c = 0; c < chunks; ++c)
    {
        std::vector<char> b; RosterHead(&b, kRosterAnswer, (unsigned int)c);
        const size_t from = c * kRosterAnswerPerChunk, to = (from + kRosterAnswerPerChunk < rows.size()) ? from + kRosterAnswerPerChunk : rows.size();
        PutU32(&b, checkNo); PutU32(&b, rosterSeq); PutU16(&b, (unsigned int)(chunks - 1)); PutU16(&b, 0); PutU32(&b, (unsigned int)(to - from));
        for (size_t i = from; i < to; ++i) { PutU32(&b, rows[i].uid); b.push_back((char)rows[i].status); b.push_back(0); PutU16(&b, rows[i].slot); PutU32(&b, rows[i].gen); }
        out->push_back(b);
    }
    return true;
}
/* Refuses: short, a length that is not exactly the rows', a chunk past lastChunk, an unknown kind or status, over the envelope cap. */
inline bool RosterDecode(const char* p, size_t n, RosterMsg* out)
{
    if (p == 0 || out == 0 || n < 4 || n > kEnvelopeCap) return false;
    RosterMsg m; m.kind = (int)(unsigned char)p[0]; m.chunk = GetU16(p + 2);
    if (p[1] != 0) return false;
    if (m.kind == kRosterHash)
    {
        if (n < 12) return false;
        m.seq = GetU32(p + 4); const unsigned int k = GetU16(p + 8); m.lastChunk = GetU16(p + 10);
        if (n != 12 + (size_t)k * 12) return false;
        for (unsigned int i = 0; i < k; ++i) { HashRow r; const char* q = p + 12 + (size_t)i * 12; r.sectorKey = GetU16(q); r.count = GetU16(q + 2); r.hash = GetU64(q + 4); m.hashes.push_back(r); }
    }
    else if (m.kind == kRosterCheck)
    {
        if (n < 16) return false;
        m.seq = GetU32(p + 4); m.sectorKey = GetU16(p + 8); m.lastChunk = GetU16(p + 10); const unsigned int k = GetU32(p + 12);
        if (k > kRosterCheckPerChunk || n != 16 + (size_t)k * 8) return false;
        for (unsigned int i = 0; i < k; ++i) { CheckRow r; r.uid = GetU32(p + 16 + (size_t)i * 8); r.gen = GetU32(p + 20 + (size_t)i * 8); m.checks.push_back(r); }
    }
    else if (m.kind == kRosterAnswer)
    {
        if (n < 20) return false;
        m.seq = GetU32(p + 4); m.rosterSeq = GetU32(p + 8); m.lastChunk = GetU16(p + 12); const unsigned int k = GetU32(p + 16);
        if (k > kRosterAnswerPerChunk || n != 20 + (size_t)k * 12) return false;
        for (unsigned int i = 0; i < k; ++i)
        {
            const char* q = p + 20 + (size_t)i * 12; AnswerRow r; r.uid = GetU32(q); r.status = (int)(unsigned char)q[4]; r.slot = GetU16(q + 6); r.gen = GetU32(q + 8);
            if (r.status < kAnsLive || r.status > kAnsNotLive || q[5] != 0) return false;
            m.answers.push_back(r);
        }
    }
    else return false;
    if (m.chunk > m.lastChunk) return false;
    *out = m;
    return true;
}

/* RECEIPT (71): u16 n | u16 0 | n x u32 seq */
const size_t kReceiptMaxSeqs = 16000;
inline bool ReceiptEncode(std::vector<char>* b, const std::vector<unsigned int>& seqs)
{
    if (b == 0 || seqs.empty() || seqs.size() > kReceiptMaxSeqs) return false;
    b->clear(); PutU16(b, (unsigned int)seqs.size()); PutU16(b, 0);
    for (size_t i = 0; i < seqs.size(); ++i) PutU32(b, seqs[i]);
    return true;
}
inline bool ReceiptDecode(const char* p, size_t n, std::vector<unsigned int>* seqs)
{
    if (p == 0 || seqs == 0 || n < 4) return false;
    const unsigned int k = GetU16(p);
    if (k == 0 || k > kReceiptMaxSeqs || GetU16(p + 2) != 0 || n != 4 + (size_t)k * 4) return false;
    seqs->clear();
    for (unsigned int i = 0; i < k; ++i) seqs->push_back(GetU32(p + 4 + (size_t)i * 4));
    return true;
}

/* UNLOAD (27) / DESPAWN (18) [review c]: u32 uid | u32 seq | u32 gen | u16 sectorKey (0xFFFF none) | u8 nExpect | u8 why | nExpect x u16 slot.
   The 4-byte form is refused. `why` says why an UNLOAD was sent: kWdWhyGone - the owner runs the person nowhere (put away asleep, reloaded,
   retired); kWdWhyAnnounce - the owner STILL RUNS the person and only withdraws the receiver's copy (the announce pass: the receiver's player
   left the area). A DESPAWN carries kWdWhyNone. An unknown value is refused. */
enum { kWdWhyNone = 0, kWdWhyGone = 1, kWdWhyAnnounce = 2, kWdWhyMax = 2 };
/* the sender's reason: 1 = the owner still runs the person (the announce pass) */
inline unsigned int UnloadWhy(int ownerStillRuns) { return ownerStillRuns != 0 ? (unsigned int)kWdWhyAnnounce : (unsigned int)kWdWhyGone; }
/* the receiver: 1 = an UNLOAD with this reason takes the person out of the squad index's `given` (the giver's wake refusal). Only
   "the owner runs it nowhere" does; the announce pass is sent while the owner still runs the person, so `given` stays and the giver
   never re-wakes the squad from its own world data. No reason (never sent for an UNLOAD) keeps it too. */
inline int UnloadClearsGiven(unsigned int why) { return why == (unsigned int)kWdWhyGone ? 1 : 0; }
const unsigned int kNoSector = 0xFFFF;
const size_t kWithdrawHead = 16, kWithdrawExpectMax = 255;
struct WithdrawMsg { unsigned int uid, seq, gen, sectorKey, why; std::vector<int> expect; WithdrawMsg() : uid(0), seq(0), gen(0), sectorKey(kNoSector), why(kWdWhyNone) {} };
inline bool WithdrawEncode(std::vector<char>* b, const WithdrawMsg& m)
{
    if (b == 0 || m.uid == 0 || m.expect.size() > kWithdrawExpectMax || m.why > (unsigned int)kWdWhyMax) return false;
    b->clear(); PutU32(b, m.uid); PutU32(b, m.seq); PutU32(b, m.gen); PutU16(b, m.sectorKey > 0xFFFF ? kNoSector : m.sectorKey);
    b->push_back((char)(unsigned char)m.expect.size()); b->push_back((char)(unsigned char)m.why);
    for (size_t i = 0; i < m.expect.size(); ++i) PutU16(b, (unsigned int)m.expect[i]);
    return true;
}
inline bool WithdrawDecode(const char* p, size_t n, WithdrawMsg* out)
{
    if (p == 0 || out == 0 || n < kWithdrawHead) return false;
    WithdrawMsg m; m.uid = GetU32(p); m.seq = GetU32(p + 4); m.gen = GetU32(p + 8); m.sectorKey = GetU16(p + 12);
    const size_t k = (size_t)(unsigned char)p[14];
    m.why = (unsigned int)(unsigned char)p[15];
    if (m.uid == 0 || m.why > (unsigned int)kWdWhyMax || n != kWithdrawHead + k * 2) return false;
    for (size_t i = 0; i < k; ++i) m.expect.push_back((int)GetU16(p + kWithdrawHead + i * 2));
    *out = m;
    return true;
}
inline bool WithdrawListsSlot(const WithdrawMsg& m, int slot)
{
    for (size_t i = 0; i < m.expect.size(); ++i) if (m.expect[i] == slot) return true;
    return false;
}

/* OWNER_MOVED (inner 202), 16 bytes: uid | newSlot | prevSlot | gen. The 8- and 12-byte forms are refused (the protocol bump). */
inline void OwnerMovedEncode16(std::vector<char>* b, unsigned int uid, unsigned int newSlot, unsigned int prevSlot, unsigned int gen)
{
    b->clear(); PutU32(b, uid); PutU32(b, newSlot); PutU32(b, prevSlot); PutU32(b, gen);
}
inline bool OwnerMovedDecode16(const char* p, size_t n, unsigned int* uid, unsigned int* newSlot, unsigned int* prevSlot, unsigned int* gen)
{
    if (p == 0 || uid == 0 || newSlot == 0 || prevSlot == 0 || gen == 0 || n != 16) return false;
    *uid = GetU32(p); *newSlot = GetU32(p + 4); *prevSlot = GetU32(p + 8); *gen = GetU32(p + 12);
    return true;
}
/* the world server road's gate for inner 202 (store.cpp StoreLiveRoadLocal): only the 16-byte form passes */
inline bool OwnerMovedRoadOk(const char* p, size_t n) { unsigned int a = 0, b = 0, c = 0, d = 0; return OwnerMovedDecode16(p, n, &a, &b, &c, &d); }
/* an OWNER_MOVED that names a LOWER gen than the copy's record is stale (a later owner is known) */
inline int OwnerMovedStale(unsigned int copyGen, unsigned int msgGen) { return copyGen > msgGen ? 1 : 0; }

/* XFER: leader | reason | count | count x XferMember (48 bytes) | count x u32 gen. Exactly that length; the gen tail is required. */
const size_t kXferMemberSize = 48, kXferHead = 12;
inline bool XferLengthOk(size_t n, unsigned int count) { return count >= 1 && count <= 64 && n == kXferHead + (size_t)count * (kXferMemberSize + 4); }
inline size_t XferGenAt(unsigned int count) { return kXferHead + (size_t)count * kXferMemberSize; }

/* generations: the taker of a person holds the giver's gen + 1; a re-adoption out-ranks every record this game knows */
inline unsigned int GenTake(unsigned int giverGen) { return giverGen + 1; }
inline unsigned int GenReadopt(unsigned int mineGen, unsigned int copyGen) { const unsigned int m = mineGen > copyGen ? mineGen : copyGen; return m + 1; }

/* ---- fold 1 of A1 build 1 (review 2026-10-02) [a1b1f1-lo0] ----------------------------------------------------------------- */
/* [F1] the XFER_ACK's road. cameBySession: the XFER it answers arrived on the session link. toIsRelay: the sender's key is a slot key.
   slotRoad: WorldFirstRoute's road to the sender's slot (kRoadNone when slot < 0). The ACK goes back on the session link when the XFER
   came by it and it is up (a one-sided outage - the sender's world link down, this game's up - would send it by slot to a world server
   that cannot deliver it); else by slot; a slot with no road goes on the session link when that is up and the slot is the session peer's;
   a sender with no slot yet (the session peer before PEER_SLOT) is answered on the session link. */
inline int XferAckRoad(int cameBySession, int toIsRelay, int slot, int slotRoad, int sessionUp, int sessionPeerSlot)
{
    if (cameBySession && sessionUp) return kRoadSession;
    if (slot >= 0)
    {
        if (slotRoad != kRoadNone) return slotRoad;
        return (sessionUp && sessionPeerSlot >= 0 && sessionPeerSlot == slot) ? kRoadSession : kRoadNone;
    }
    return (!toIsRelay && sessionUp) ? kRoadSession : kRoadNone;
}
/* [F3] a squad announcement is due when nothing was sent, what it says changed, or its leader's area key (the AREA it goes to; -1 =
   WORLD) differs from the one the last went with - a squad walking into another player's area is announced there */
inline int LeadAnnounceDue(int sent, int sameContent, int sentAreaKey, int areaKeyNow) { return (sent == 0 || sameContent == 0 || sentAreaKey != areaKeyNow) ? 1 : 0; }
/* [F5] a catch-up ask re-announces only the squads whose last announcement went to one of the asked sectors (a WORLD one, -1, reached
   every game) */
inline int LeadReannounceThis(int sentAreaKey, const int* keys, size_t n)
{
    if (sentAreaKey < 0) return 0;
    for (size_t i = 0; i < n; ++i) if (keys[i] == sentAreaKey) return 1;
    return 0;
}
/* [F4] does the holder rebuild an owner's per-sector hashes from engine positions this tick: only when a CHECK of it could be due */
inline int RosterSectorsNeeded(double sinceCheck, int edgePending, int recheckPending, double rosterSec) { return (sinceCheck >= rosterSec || edgePending || recheckPending) ? 1 : 0; }
/* [F8] a CHECK closes a withdrawal's receipt row only when it is a FULL one (kRosterAllSectors) that does not list the uid - a per-sector
   CHECK lists only the copies the holder sees in that sector */
inline int ReceiptClosedByCheck(unsigned int checkSectorKey, int listed) { return (checkSectorKey == kRosterAllSectors && listed == 0) ? 1 : 0; }
/* [F10] an ANSWER row's slot is read only by a MOVED row (the re-key); a MOVED row naming a slot past the last live slot is refused */
inline int AnswerSlotOk(int status, unsigned int slot, unsigned int maxSlot) { return (status != kAnsMoved || (slot <= maxSlot && slot != 0xFFFFu)) ? 1 : 0; }   /* re-check 2026-10-02: 0xFFFF is "no slot" and the u16 can never exceed kLiveSlotMax (65535) - refuse it explicitly */


/* ==== M7a A1 BUILD 2 [a1b2-lo0] (design 2.4, 2.5, 3.2-3.4, 3.10, 3.11, 1.3) - THE SWITCH-OVER: RELEASE / adopt ======================== */
/* keepMargin (manager decision 2026-10-02, design 7 item 7): run T821's P119 showed this game's engine putting squads away in a CORNER
   sector of its own player's 3x3 on both games (A 737.5 s: 44,11 side x2 and 44,10 corner; B 811.2-811.4 the same shape) - a corner is
   neither offered (ReceiverRingPick) nor adopted (AdoptDecide). 0 would be the whole ring. */
const int kKeepMargin = 1;
const double kReleaseCandidateSec = 5.0;    /* a candidate silent this long (link-up-and-fresh-table time only) is skipped [review F5] */
const double kReleaseResendSec = 1.0;       /* an unanswered offer is sent again (same id: the receiver answers a repeat the same way) */
const double kReleaseDeferResendSec = 4.0;  /* a deferred offer is re-offered to the same candidate this often ... */
const int kReleaseDeferMax = 3;             /* ... at most this many times (about 15 s), then the next candidate */

/* 3.2 the receiver's verdict on one member of a RELEASE offer. takeRing = coopsquad::XferTakeMember's answer (the receiver's own ring
   rule over the squad or the member position, as the XFER uses it); decisionCorner = the sector that answer used is a CORNER of this
   game's player's 3x3; copyOwnerIsGiver = this game's copy is recorded as the giver's. Link down / writes blocked: defer (owner 334 a);
   already run here: ACKed again (a repeat offer); no copy: drop; a copy recorded as another game's, or an offer not newer than the
   copy's gen: stale (refused - the copy is NOT removed, it is someone else's); outside the ring, or a corner with keepMargin 1: drop. */
enum { kAdopt = 0, kAdoptDup = 1, kDrop = 2, kDefer = 3, kStale = 4 };
inline int AdoptDecide(int haveLiveCopy, int copyOwnerIsGiver, int mineAlready, unsigned int myGen, unsigned int offerGen,
                       int takeRing, int decisionCorner, int myWorldLinkUp, int writesBlocked, int keepMargin)
{
    if (writesBlocked != 0 || myWorldLinkUp == 0) return kDefer;
    if (mineAlready != 0) return kAdoptDup;
    if (haveLiveCopy == 0) return kDrop;
    if (copyOwnerIsGiver == 0 || offerGen <= myGen) return kStale;
    if (takeRing == 0) return kDrop;
    if (keepMargin >= 1 && decisionCorner != 0) return kDrop;
    return kAdopt;
}
/* where sector (sx, sy) stands against a player standing in (px, py): its own sector, a side of its 3x3, a corner, or outside */
enum { kRingSelf = 0, kRingSide = 1, kRingCorner = 2, kRingOut = 3 };
inline int RingKind(int px, int py, int sx, int sy)
{
    const int dx = px > sx ? px - sx : sx - px, dy = py > sy ? py - sy : sy - py;
    if (dx == 0 && dy == 0) return kRingSelf;
    if (dx <= 1 && dy <= 1) return (dx == 0 || dy == 0) ? kRingSide : kRingCorner;
    return kRingOut;
}
/* 3.3 the receivers of a squad standing in (sx, sy): every IN_WORLD game but this one whose published player sector is within `ring`
   (Chebyshev) of it - with keepMargin 1 not as a corner - and that was not tried yet; nearest first, then the lower slot. The rows are
   ONE fresh player-sector table (the caller passes none when it is not fresh). Returns how many slots were written to order (<= cap). */
struct RingRow { int slot, x, y, inWorld; RingRow() : slot(-1), x(-1), y(-1), inWorld(0) {} };
inline int ReceiverRingPick(const RingRow* rows, int n, int sx, int sy, int mySlot, int ring, int keepMargin, const int* tried, int nTried, int* order, int cap)
{
    std::vector<int> dist, slot;   /* every eligible game, kept sorted (distance, then slot); the first `cap` are returned */
    for (int i = 0; i < n && rows != 0; ++i)
    {
        const RingRow& r = rows[i];
        if (r.slot < 0 || r.slot == mySlot || r.inWorld == 0 || r.x < 0 || r.y < 0) continue;
        bool skip = false;
        for (int t = 0; t < nTried && tried != 0 && !skip; ++t) if (tried[t] == r.slot) skip = true;
        for (size_t o = 0; o < slot.size() && !skip; ++o) if (slot[o] == r.slot) skip = true;
        if (skip) continue;
        const int c = Cheb(r.x, r.y, sx, sy);
        if (c > ring) continue;
        if (keepMargin >= 1 && RingKind(r.x, r.y, sx, sy) == kRingCorner) continue;
        size_t at = slot.size();
        while (at > 0 && (dist[at - 1] > c || (dist[at - 1] == c && slot[at - 1] > r.slot))) --at;
        dist.insert(dist.begin() + (long)at, c);
        slot.insert(slot.begin() + (long)at, r.slot);
    }
    int k = 0;
    for (size_t i = 0; i < slot.size() && k < cap && order != 0; ++i) order[k++] = slot[i];
    return k;
}
/* 3.4 one open release's next step (the giver, each pass). The candidate clock runs only while this game's notebook link is up AND the
   player table is fresh [review F5] - the caller adds time to it only then; with either down the release HOLDS (owner 334 a). */
enum { kRsHold = 0, kRsWait = 1, kRsOffer = 2, kRsResend = 3, kRsNext = 4, kRsAsleep = 5 };
inline int ReleaseStep(int haveCandidate, int candidatesLeft, int myLinkUp, int tableFresh, int candidateInWorld, double candidateClockSec, int defers, double sinceSend)
{
    if (myLinkUp == 0 || tableFresh == 0) return kRsHold;
    if (haveCandidate == 0) return candidatesLeft > 0 ? kRsOffer : kRsAsleep;
    if (candidateInWorld == 0 || defers > kReleaseDeferMax || candidateClockSec >= kReleaseCandidateSec) return kRsNext;
    if (sinceSend >= (defers > 0 ? kReleaseDeferResendSec : kReleaseResendSec)) return kRsResend;
    return kRsWait;
}
/* a put-away of a person named by this game's XFER still awaiting its ACK is that ACK's (taken: gone; not taken, or abandoned, with no body
   here: the ACK path opens a release for it) */
enum { kPadCollect = 0, kPadLeaveToAck = 1 };
inline int PutAwayDuringFlight(int inXferFlight) { return inXferFlight != 0 ? kPadLeaveToAck : kPadCollect; }
/* the gen a candidate holds if it adopts - strictly rising with each candidate tried [review F6] */
inline unsigned int ReleaseOfferGen(unsigned int baseGen, int nTried) { return baseGen + 1u + (unsigned int)nTried; }

/* 3.10 the giver's answer to one uid of a RELEASE_ACK [review F3]: only the CURRENT offer's adopter is accepted; any other adopter is
   revoked (late, or after the settle / cancel); a drop or defer of anything but the current offer is ignored */
enum { kVerdictAdopted = 0, kVerdictDropped = 1, kVerdictDeferred = 2 };
enum { kSettleAccept = 0, kSettleRevoke = 1, kSettleNext = 2, kSettleRetry = 3, kSettleIgnore = 4 };
inline int GiverSettleOnAck(int verdict, unsigned int ackGen, unsigned int currentOfferGen, int offerOpen)
{
    if (verdict == kVerdictAdopted) return (offerOpen != 0 && ackGen == currentOfferGen) ? kSettleAccept : kSettleRevoke;
    if (offerOpen == 0 || ackGen != currentOfferGen) return kSettleIgnore;
    return verdict == kVerdictDropped ? kSettleNext : kSettleRetry;
}
/* a late adopter while the uid's later offer is still open: its REVOKE waits for the settle; at an asleep settle the late adopter with the
   highest gen (tie: the lower slot) is accepted instead - a person another game runs is never put to sleep. -1 = none. */
inline int LateAdopterPick(const unsigned int* gens, const int* slots, int n)
{
    int best = -1;
    for (int i = 0; i < n && gens != 0 && slots != 0; ++i)
        if (best < 0 || gens[i] > gens[best] || (gens[i] == gens[best] && slots[i] < slots[best])) best = i;
    return best;
}
/* 2.5 item 4b, the revoked game [review F4]: it runs uid at a gen NOT newer than the winner's -> the winner's puppet (no winner: the body
   is removed - no game runs it); never when the winner is this game, it does not run uid, or it holds a newer gen (a later hand-over). */
enum { kRvIgnore = 0, kRvPuppet = 1, kRvRemove = 2 };
inline int RevokeAction(int iRunIt, unsigned int myGen, unsigned int winnerGen, int winnerIsMe, int winnerNone)
{
    if (iRunIt == 0 || winnerIsMe != 0 || myGen > winnerGen) return kRvIgnore;
    return winnerNone != 0 ? kRvRemove : kRvPuppet;
}
/* 3.11 a squad's "handed over" answer from the per-person index (= the former HandedOverEffective(marked, leftBehind)) */
inline int SquadGivenEffective(int givenCount, int asleepHereCount) { return (givenCount > 0 && asleepHereCount == 0) ? 1 : 0; }
/* fold 1 of A1 build 2 [a1b2f1-lo0] [review F5]: a squad is WHOLLY given - the consumers that act on a whole engine squad (the wake refusal and
   drop, the drop queue's re-check, the same-id supersede, the sleep-write and heartbeat refusals, the sweep's skip) may act - only when no
   person of it is known to stay this game's: none asleep here, none kept here (put away and not offered, or its offer cancelled by a
   re-wake), none in an open offer, none run live here. With nothing kept / open / live it equals SquadGivenEffective. */
inline int SquadWholeGiven(int givenCount, int asleepHereCount, int keptHereCount, int openHereCount, int liveHereCount)
{
    return (givenCount > 0 && asleepHereCount == 0 && keptHereCount == 0 && openHereCount == 0 && liveHereCount == 0) ? 1 : 0;
}
/* [a1b2f1-lo1] [review F2] (the retired t_m7a3f4_take_back_keeps_mark rule): the sweep met a person of the squad this game's engine
   re-created from its own world data. If any person of it exists only in this game's world data (asleep here, kept here, or an open offer
   this re-wake cancelled) the squad's WHOLE index entry goes (given too): its re-woken people are adopted here, never noted as orphans and
   deleted. Every person given: the entry stays (the sweep skips them - the other game runs them). 1 = drop the whole entry. */
inline int SquadRebuiltDropsEntry(int asleepHereCount, int keptHereCount, int cancelledNow)
{
    return (asleepHereCount > 0 || keptHereCount > 0 || cancelledNow > 0) ? 1 : 0;
}
/* [a1b2f1-lo2] [review F3] / fold 2 [a1b2f2-lo0] [review G1]: an adoption of a member whose squad this game's engine has re-woken (awake
   here again). Fold 1 revoked every such adoption - the adopter removed its copy while the giver's re-woken body, in an area the adopter
   holds, went to the orphan clean-up (decision 37): nobody ran the person. Now the sweep's own area rule decides which copy lives.
   heldByOther is the sweep's live read of the re-woken squad's area (HeldByOtherTS of its sector - worldsync.cpp AnnounceAreaDecide):
     1  another game holds it (the sweep notes the re-woken body as an orphan)       -> ACCEPT (the adopter's copy lives)
     0  this game holds it (the sweep adopts the re-woken body under a fresh uid)    -> REVOKE, no winner (the member is kept here)
    -1  unknown (no fresh area map, or no readable position)                         -> ACCEPT (a duplicate the dual-run / orphan paths can
                                                                                         still settle - never a loss)
   Not awake here again: ACCEPT. REVOKE exactly when coopor::AnnounceArea(heldByOther) is adoptable. */
inline int AdoptAfterRewake(int squadAwakeHere, int heldByOther)
{
    if (squadAwakeHere == 0) return kSettleAccept;
    return heldByOther == 0 ? kSettleRevoke : kSettleAccept;
}
/* [a1b2f1-lo3] [review F4]: a REVOKE the receiver cannot apply now (engine writes blocked) waits for the next safe point - never dropped */
enum { kRvNowApply = 0, kRvNowDefer = 1 };
inline int RevokeWhen(int writesBlocked) { return writesBlocked != 0 ? kRvNowDefer : kRvNowApply; }

/* 1.3 RELEASE (68): u32 releaseId | u32 keyUid | u16 sectorKey | u8 flags | u8 n | u16 winnerSlot | u16 0 | n x { XferMember (48) | u32 gen }
   (gen: the offer's - the gen the candidate holds if it adopts; REVOKE: the winner's). RELEASE_ACK (69): u32 releaseId | u16 nAdopted |
   u16 nDropped | u16 nDeferred | the three uid lists. Exactly those lengths; n 1..64; id 0 refused. */
const unsigned int kRelFlagPutAway = 1u, kRelFlagRevoke = 2u, kRelNoWinner = 0xFFFFu;
/* [a1b2f1-lo4] [review F6]: the flags are exactly PUT_AWAY or REVOKE - 0, both or an unknown bit is malformed (never taken as an offer) */
inline int ReleaseFlagsValid(unsigned int flags) { return (flags == kRelFlagPutAway || flags == kRelFlagRevoke) ? 1 : 0; }
const size_t kReleaseHead = 16, kReleaseRowSize = 52, kReleaseAckHead = 10;
const int kReleaseMaxMembers = 64;
struct ReleaseRow { unsigned int uid, gen; unsigned char body[48]; ReleaseRow() : uid(0), gen(0) { std::memset(body, 0, sizeof(body)); } };
struct ReleaseMsg
{
    unsigned int id, keyUid, sectorKey, flags, winnerSlot; std::vector<ReleaseRow> rows;
    ReleaseMsg() : id(0), keyUid(0), sectorKey(kNoSector), flags(0), winnerSlot(kRelNoWinner) {}
};
inline bool ReleaseEncode(std::vector<char>* b, const ReleaseMsg& m)
{
    b->clear();
    if (m.id == 0 || m.rows.empty() || (int)m.rows.size() > kReleaseMaxMembers) return false;
    PutU32(b, m.id); PutU32(b, m.keyUid); PutU16(b, m.sectorKey);
    b->push_back((char)(unsigned char)m.flags); b->push_back((char)(unsigned char)m.rows.size());
    PutU16(b, m.winnerSlot); PutU16(b, 0);
    for (size_t i = 0; i < m.rows.size(); ++i)
    {
        const size_t at = b->size(); b->resize(at + 48);
        std::memcpy(&(*b)[at], m.rows[i].body, 48);
        std::memcpy(&(*b)[at], &m.rows[i].uid, 4);   /* the member body's first word is its uid */
        PutU32(b, m.rows[i].gen);
    }
    return true;
}
inline bool ReleaseDecode(const char* p, size_t n, ReleaseMsg* out)
{
    if (p == 0 || n < kReleaseHead) return false;
    const unsigned int cnt = (unsigned int)(unsigned char)p[11];
    if (cnt == 0 || (int)cnt > kReleaseMaxMembers || n != kReleaseHead + (size_t)cnt * kReleaseRowSize) return false;
    out->id = GetU32(p); out->keyUid = GetU32(p + 4); out->sectorKey = GetU16(p + 8);
    out->flags = (unsigned int)(unsigned char)p[10]; out->winnerSlot = GetU16(p + 12);
    if (out->id == 0) return false;
    if (ReleaseFlagsValid(out->flags) == 0 || GetU16(p + 14) != 0) return false;   /* [a1b2f1-lo4] [review F6]: unknown flags / the reserved u16 set */
    out->rows.assign(cnt, ReleaseRow());
    for (unsigned int i = 0; i < cnt; ++i)
    {
        const char* r = p + kReleaseHead + (size_t)i * kReleaseRowSize;
        std::memcpy(out->rows[i].body, r, 48);
        out->rows[i].uid = GetU32(r); out->rows[i].gen = GetU32(r + 48);
    }
    return true;
}
struct ReleaseAckMsg { unsigned int id; std::vector<unsigned int> adopted, dropped, deferred; ReleaseAckMsg() : id(0) {} };
inline bool ReleaseAckEncode(std::vector<char>* b, const ReleaseAckMsg& m)
{
    b->clear();
    const size_t total = m.adopted.size() + m.dropped.size() + m.deferred.size();
    if (m.id == 0 || total > (size_t)kReleaseMaxMembers) return false;
    PutU32(b, m.id); PutU16(b, (unsigned int)m.adopted.size()); PutU16(b, (unsigned int)m.dropped.size()); PutU16(b, (unsigned int)m.deferred.size());
    for (size_t i = 0; i < m.adopted.size(); ++i) PutU32(b, m.adopted[i]);
    for (size_t i = 0; i < m.dropped.size(); ++i) PutU32(b, m.dropped[i]);
    for (size_t i = 0; i < m.deferred.size(); ++i) PutU32(b, m.deferred[i]);
    return true;
}
inline bool ReleaseAckDecode(const char* p, size_t n, ReleaseAckMsg* out)
{
    if (p == 0 || n < kReleaseAckHead) return false;
    const unsigned int na = GetU16(p + 4), nd = GetU16(p + 6), nf = GetU16(p + 8);
    const size_t total = (size_t)na + (size_t)nd + (size_t)nf;
    if (total > (size_t)kReleaseMaxMembers || n != kReleaseAckHead + total * 4) return false;
    out->id = GetU32(p);
    if (out->id == 0) return false;
    out->adopted.clear(); out->dropped.clear(); out->deferred.clear();
    const char* q = p + kReleaseAckHead;
    for (unsigned int i = 0; i < na; ++i, q += 4) out->adopted.push_back(GetU32(q));
    for (unsigned int i = 0; i < nd; ++i, q += 4) out->dropped.push_back(GetU32(q));
    for (unsigned int i = 0; i < nf; ++i, q += 4) out->deferred.push_back(GetU32(q));
    return true;
}

}   /* namespace cooplo */

#endif
