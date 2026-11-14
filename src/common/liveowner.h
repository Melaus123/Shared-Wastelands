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
   BUILD 2 (the switch-over, [a1b2-lo0], the block at the end): 3.2 AdoptDecide, 3.3 RingKind (ReceiverRingPick retired: T-650 AreaReceiverOrder), 3.4 ReleaseStep /
   PutAwayDuringFlight / ReleaseOfferGen, 3.10 GiverSettleOnAck, the late-adopter and REVOKE rules, 3.11 SquadGivenEffective and the
   RELEASE (68) / RELEASE_ACK (69) codecs. */
#ifndef COOP_LIVEOWNER_H
#define COOP_LIVEOWNER_H

#include <vector>
#include <map>
#include <set>
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
/* the player-sector table carries a row per player-character area, so one slot can have several rows: `slot` is added to *out only
   when it is not there yet. 1 = added. */
inline int SlotAddOnce(std::vector<int>* out, int slot)
{
    for (size_t i = 0; i < out->size(); ++i) if ((*out)[i] == slot) return 0;
    out->push_back(slot);
    return 1;
}
/* is area (ax, ay) within `within` areas (Chebyshev) of ANY of the n areas (xs[i], ys[i]) - this game's own player-character rows */
inline int NearAnyArea(const int* xs, const int* ys, int n, int ax, int ay, int within)
{
    for (int i = 0; i < n; ++i) if (Cheb(xs[i], ys[i], ax, ay) <= within) return 1;
    return 0;
}
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
/* T-1122 lent flag (game-to-game protocol 224): the XFER's reason word carries, above its reason code (the low byte), the "lent squad -
   whole or none" bit - the receiver takes all of its people or none, judged from this message alone (the squad's raid block may have
   been dropped). Any other bit set: malformed. */
/* T-1318 attempt 2 (design D3, game-to-game protocol 226): a second bit, kXferFlagWorldWhole - "a world squad, sent whole: take all of
   its people or none" - set on every forced XFER this game sends (handoff.cpp, the forced hand-over). The lent bit keeps its T-1122
   meaning (a lent squad), so the receiver's T-1122 counters still count lent squads only. In code the two bits travel as one int,
   wholeBits: kWholeBitLent (1) and kWholeBitWorld (2). */
const unsigned int kXferReasonMask = 0xFFu, kXferFlagLentWhole = 0x100u, kXferFlagWorldWhole = 0x200u;
enum { kWholeBitLent = 1, kWholeBitWorld = 2 };
inline unsigned int XferReasonWord(unsigned int reason, int wholeBits)
{
    return (reason & kXferReasonMask) | ((wholeBits & kWholeBitLent) != 0 ? kXferFlagLentWhole : 0u) | ((wholeBits & kWholeBitWorld) != 0 ? kXferFlagWorldWhole : 0u);
}
inline int XferReasonSplit(unsigned int word, unsigned int* reason, int* wholeBits)
{
    if ((word & ~(kXferReasonMask | kXferFlagLentWhole | kXferFlagWorldWhole)) != 0u) return 0;
    *reason = word & kXferReasonMask;
    *wholeBits = ((word & kXferFlagLentWhole) != 0u ? (int)kWholeBitLent : 0) | ((word & kXferFlagWorldWhole) != 0u ? (int)kWholeBitWorld : 0);
    return 1;
}

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
   neither offered nor adopted. 0 would be the whole ring. NO LONGER IN USE since the T-650 fold (manager, 2026-10-09): the giver
   offers by the world server's loaded map (AreaReceiverOrder) and the receiver takes by its engine's loaded list, so the receiver
   passes no corner and keepMargin 0 to AdoptDecide; the constant stays for AdoptDecide's offline tests. */
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
/* a put-away of a person a same-id retire withdraws (this game's copy of a squad that another game's running copy replaces: the same-id yield,
   sameidpair.h CopyYield, or the reloaded-copy retire - handoff.cpp g_sameIdWd) is never offered: the other game already runs that person,
   so its UNLOAD goes (removed from the other games). Asked first; else the flight rule above */
enum { kPadUnloadSameId = 2 };
inline int PutAwayRoad(int sameIdWithdrawn, int inXferFlight) { return sameIdWithdrawn != 0 ? kPadUnloadSameId : PutAwayDuringFlight(inXferFlight); }
/* the ACK path's road for a member of this game's XFER that was not taken and whose body was put away while it flew: a person a same-id
   retire withdraws has its held UNLOAD sent (never offered); one with no squad key sleeps here and its held UNLOAD goes; else a release */
enum { kFprRelease = 0, kFprUnloadNoKey = 1, kFprUnloadSameId = 2 };
inline int FlightPutAwayRoad(int sameIdWithdrawn, int hasKey) { return sameIdWithdrawn != 0 ? kFprUnloadSameId : (hasKey != 0 ? kFprRelease : kFprUnloadNoKey); }
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
/* T-1318 fold 2 (re-check B1, T2316 Western Hive_13 offered twice): a put-away of people of a squad another game took at an accepted
   ACK - every person the squad index has of it given, none asleep or kept here (the people being put away now are left out: they are
   new bodies this game made of a squad it gave away) - is NOT offered again: the people settle asleep here (counted, a capped line).
   A squad given in part (people of it asleep or kept here) is offered as before. 1 = not offered. */
inline int GivenSquadNotOffered(int givenCount, int asleepHereCount, int keptHereCount)
{
    return SquadWholeGiven(givenCount, asleepHereCount, keptHereCount, 0, 0);
}
/* [a1b2f1-lo1] [review F2] (the retired t_m7a3f4_take_back_keeps_mark rule): the sweep met a person of the squad this game's engine
   re-created from its own world data. If any person of it exists only in this game's world data (asleep here, kept here, or an open offer
   this re-wake cancelled) that part of the index entry goes: its re-woken people are adopted here, never noted as orphans and deleted.
   Every person given: the entry stays (the sweep skips them - the other game runs them). 1 = this game's own part goes.
   T-1318 attempt 3 (owner 1615 a; F-T2324-WHOLEKEPT): the GIVEN part never goes with it (it went too before - with given 7 and kept 5
   under one key, a re-made kept person dropped the 7 given and a re-made body of a given person was adopted and offered again, the T2315
   road) - SquadRebuiltAction below says which part goes. */
inline int SquadRebuiltDropsEntry(int asleepHereCount, int keptHereCount, int cancelledNow)
{
    return (asleepHereCount > 0 || keptHereCount > 0 || cancelledNow > 0) ? 1 : 0;
}
/* ==== T-1318 attempt 2 (design D3, owner rules 1059 / 1556; run T2315, F-T2315-PARTGIVEN): A WORLD SQUAD IS HANDED TO ANOTHER GAME WHOLE
   OR NOT AT ALL ====
   T2315 (Confirmed in logs; cause Read): A gave B one of Dust Bandits_8's five people; A's engine re-made the squad; the sweep's re-wake drop
   took the WHOLE index entry, the given person too (SquadRebuiltDropsEntry above), adopted A's re-made copy of that person and offered it
   again - B ran one person twice. Every road that broke stood on a squad given IN PART (people of it given and people of it asleep or kept
   here). Broken assumption: "the people this game's engine re-woke are all this game's own" - true only for a squad no part of which is
   given. D3 removes the part-given state instead of patching each road:
   THE SENDER - every RELEASE offer and every forced XFER says "world squad, whole or none" (kRelFlagWorldWhole / kXferFlagWorldWhole), and
   a squad is not offered at all when the offer could not be the whole squad (WorldReleaseOffer / WorldXferWhole below).
   THE RECEIVER - such a message is judged by T-1122's whole-or-none judge (raidsq::LentTakeWhole): all rows taken or none.
   THE TRIPWIRE - PartGivenAtAck: an accepted hand-over that leaves a squad with people given AND people asleep or kept here is counted
   (it must stay 0). */
const int kReleaseMaxMembers = 64;   /* a RELEASE message's rows (1.3 below) */
/* the sender's RELEASE: keptOfSquad - people of the batch's squad this game put away and did NOT offer (the squad index's keptHere and the
   not-yet-indexed kept list, handoff.cpp IdxKeep - a body not announced, unread, off-thread with a known key); batchCount - the people
   offered. 0 = offered whole (with the whole bit); else NOT offered - every person settles asleep here, whole (as T-1122's RelLentAsleep). */
enum { kWroOffer = 0, kWroKeptOut = 1, kWroOver64 = 2 };
inline int WorldReleaseOffer(int batchCount, int keptOfSquad)
{
    if (keptOfSquad > 0) return kWroKeptOut;
    if (batchCount > kReleaseMaxMembers) return kWroOver64;   /* one message holds 64 rows: never split into two offers */
    return kWroOffer;
}
/* the sender's XFER - the forced one and (fold 1, review HIGH-1) the follow-leader one alike: built - the members the message names after
   the hold-back (PassHoldElsewhere) and the 64 cap; runHere - the squad's people this game runs. 1 = whole: sent with the world bit; 0 = a
   member held back: NOT sent this pass (the squad stays here; its engine puts it away and the RELEASE decides, or a later pass sends it
   whole). */
inline int WorldXferWhole(int built, int runHere) { return (built > 0 && built == runHere) ? 1 : 0; }
/* T-1318 attempt 3 (owner 1615 a; run T2324, F-T2324-WHOLEKEPT): the XFER's whole test - the forced one and the follow-leader one - also
   counts the people of the squad (same squad id: the squad index's key) this game keeps asleep un-offered: the index's keptHere and
   asleepHere and the not-yet-indexed kept list (handoff.cpp KeptAsleepOf). T2324: the engine put Traders Guild_1 away a few people per
   frame; 5 were kept, and the awake 7 went as "whole" - the squad was left given in part. keptAsleepHere > 0: NOT sent this pass; the
   squad goes when the engine's put-away offers it whole, or when all its people are awake here again (the sweep's re-wake clears the
   kept people, HandedOverLeftBehindRebuilt). kWxHeldPart: a member held back (WorldXferWhole 0); kWxKeptAsleep: people of it kept asleep
   here. */
/* T-1318 attempt 3c (owner 1617 a, 2026-11-13: "hold the early sleepers undecided until the whole group is asleep, then offer it whole").
   A world squad's put-away batch while people of that squad are still AWAKE here (live, run by this game - ReleaseAwakeHolds, one body at a
   time: not this game's copies of another game's people, not the batch) is HELD UNDECIDED - not offered, not settled, its UNLOADs held -
   beside the squad's earlier held batches. Each later batch of the squad (and the 1 Hz re-check) decides the held people and the batch
   TOGETHER (HeldReleaseDecide over all of them): people of the squad kept / asleep here -> every one settles asleep here (kHdKeptOut, as
   today); over 64 -> every one settles (kHdOver64); still awake here -> held on (kHdHold); none -> ONE whole offer (kHdOffer). T2324:
   4194618 put away alone while 11 of its squad were awake, then 617+619, 616, 615 - each held, offered with the last batch as one squad. */
/* T-1318 attempt 3 fold 1 (review MED-2b): dead - the body's engine dead flag (Character::hasDied: 1 dead, 0 alive, -1 unreadable). A corpse
   of the squad (a world faction's corpses stay in its member list) is not awake and holds nothing; unreadable counts as awake (the hold is
   the safe side). */
inline int ReleaseAwakeHolds(unsigned int bodyUid, int bodyUidMine, int inBatch, int sameSquad, int dead = 0)
{
    return (bodyUid != 0 && bodyUidMine != 0 && inBatch == 0 && sameSquad != 0 && dead != 1) ? 1 : 0;
}
/* T-1318 attempt 3 fold 2 (re-check MED-A): NEVER A SECOND OFFER OF ONE SQUAD. openOfSquad - people of the squad in a hand-over of this
   game still open (an offer record waiting for its answer, an XFER waiting for its ACK): the batch is HELD (it waits for that answer, as for
   people awake here) - a second whole-or-none offer of the same squad could go to another game and split it. wholeGiven - the squad index
   says another game took the squad whole (GivenSquadNotOffered: the open offer was accepted while these waited): kHdGiven - every person
   settles asleep here (counted; the tripwire notes the squad), never offered again. Kept / over 64 decide first, as before. */
enum { kHdOffer = 0, kHdHold = 1, kHdKeptOut = 2, kHdOver64 = 3, kHdGiven = 4 };
inline int HeldReleaseDecide(int count, int keptOfSquad, int awakeOfSquad, int openOfSquad = 0, int wholeGiven = 0)
{
    const int wro = WorldReleaseOffer(count, keptOfSquad);
    if (wro == kWroKeptOut) return kHdKeptOut;
    if (wro == kWroOver64) return kHdOver64;
    if (openOfSquad > 0) return kHdHold;
    if (wholeGiven != 0) return kHdGiven;
    return awakeOfSquad > 0 ? (int)kHdHold : (int)kHdOffer;
}
/* T-1318 attempt 3 fold 2 (re-check MED-A): ONE SQUAD, ONE DECISION PER FRAME. A frame's put-away batches (handoff.cpp ReleaseFlush) are
   filed by different roads - an engine put-away under its engine squad, an ended XFER flight's put-aways under a placeholder - so one squad
   can have several. Before any decision, every batch of one squad (squad[i]: the batch's squad key as a small id, -1 = no key) joins ONE
   base batch: the squad's first engine batch (flight[i] == 0: its key person and the engine's squad decision position), else its first
   batch. base[i] = the batch i joins (i itself for a base or a keyless batch). Returns the batches that join another. */
inline int ReleaseMergeBase(const int* squad, const int* flight, int n, int* base)
{
    int merged = 0;
    for (int i = 0; i < n; ++i)
    {
        base[i] = i;
        if (squad[i] < 0) continue;
        int first = -1, eng = -1;
        for (int j = 0; j < n; ++j)
        {
            if (squad[j] != squad[i]) continue;
            if (first < 0) first = j;
            if (eng < 0 && flight[j] == 0) eng = j;
        }
        base[i] = eng >= 0 ? eng : first;
        if (base[i] != i) ++merged;
    }
    return merged;
}
/* a held person at the 1 Hz re-check: no longer this game's (it died, or another road took it - a dual run's yield, a hire) -> it leaves the
   hold (kHmGone); a body of THAT person awake here again (handoff.cpp PersonAwakeHere: the engine runs it again) -> it leaves the hold and is
   never offered (kHmWoken - cancelled, as an open offer's re-wake); else it stays held (kHmKeep). */
enum { kHmKeep = 0, kHmGone = 1, kHmWoken = 2 };
inline int HeldMemberStep(int stillMine, int rewokenHere)
{
    if (stillMine == 0) return kHmGone;
    return rewokenHere != 0 ? (int)kHmWoken : (int)kHmKeep;
}
enum { kWxGo = 0, kWxHeldPart = 1, kWxKeptAsleep = 2 };
inline int WorldXferGo(int built, int runHere, int keptAsleepHere)
{
    if (WorldXferWhole(built, runHere) == 0) return kWxHeldPart;
    return keptAsleepHere > 0 ? kWxKeptAsleep : kWxGo;
}
/* ==== T-1318 attempt 4 (owner 1626, design A; run T2343, F-T2343-UNNUMBERED): 'WHOLE SQUAD' FROM THE ENGINE'S OWN MEMBER LIST ====
   Every whole test above counted only the people the mod has numbered (uids). A re-woken squad is put away or forced out before the
   6-per-tick sweep numbers all of it (T2343 A 687.449: Starving Bandits_19 chars=9 known=3, offered n=3, the 6 others lost at the
   supersede; ~70 such part-offers in the run). Each decision now reads the engine's member list (ActivePlatoon +0x58 count / +0x60 array,
   handoff.cpp ReadSquadAt): a LIVING member with no uid of this game's - never numbered, or another game's copy - is UNACCOUNTED, and a
   squad with any is not whole here. A list over 64 (stored < count) is never whole. */
/* one member of the engine's list: dead - its engine dead flag (Character::hasDied: 1 dead, 0 alive, -1 unreadable - counted alive, the
   safe side; a world faction's corpses stay in its list); inRun - it is one of the people the decision is about (this game's people of
   the squad). 1 = a living member outside them. */
/* T-1318 a4 fold 1 (review HIGH-2): otherGame - the member carries ANOTHER game's uid: that game runs the person already (a living copy here
   of a person it runs), so it is never 'outside' the run - only a living member with no uid (never numbered), or with this game's own uid
   not in the run, is. Counted outside, every follow-leader XFER (whose squad here always holds the leader's copy) and every forced XFER of
   a mixed squad was held for good. */
inline int SquadMemberUnaccounted(int dead, int inRun, int otherGame = 0) { return (inRun == 0 && otherGame == 0 && dead != 1) ? 1 : 0; }
inline int SquadLivingOutside(const int* dead, const int* inRun, int n, const int* otherGame = 0)
{
    int k = 0;
    for (int i = 0; i < n && dead != 0 && inRun != 0; ++i) k += SquadMemberUnaccounted(dead[i], inRun[i], otherGame != 0 ? otherGame[i] : 0);
    return k;
}
/* the XFER's runHere from the engine's list (WorldXferGo's second argument): the people this game runs of the squad (inRun - the mirror's
   group, its corpses too: they are in the message) plus the list's living members outside them; a list over 64 (count > 64, or fewer
   read than the count) or an unread list (livingOutside < 0) can never be whole: -1 (WorldXferGo -> kWxHeldPart). */
inline int XferRunHereOf(int inRun, int livingOutside, int count, int stored)
{
    if (count > kReleaseMaxMembers || stored < count || livingOutside < 0) return -1;
    return inRun + livingOutside;
}
/* the RELEASE (handoff.cpp RelHeldDecide): unaccPutAway - never-numbered people of the squad the engine put away here (each once,
   UnaccUnionCount) count as KEPT (every person settles asleep here, whole - kHdKeptOut); unaccAwake - living members of the squad's awake
   engine list with no uid of this game's count as AWAKE (held undecided - kHdHold); neither: HeldReleaseDecide as before. */
inline int HeldReleaseDecideUnacc(int count, int kept, int awake, int unaccPutAway, int unaccAwake, int openOfSquad = 0, int wholeGiven = 0)
{
    return HeldReleaseDecide(count, kept + (unaccPutAway > 0 ? unaccPutAway : 0), awake + (unaccAwake > 0 ? unaccAwake : 0), openOfSquad, wholeGiven);
}
/* T-1318 a4 fold 1 (review HIGH-1): THE SQUAD'S OWN SLEEPING RECORDS at the RELEASE when no awake list reads (the squad asleep, or put
   away this frame). The despawn hook never sees a never-numbered body (NotifyDespawn runs only for an address with a mirror / index row:
   worldgen.cpp detour_destroy -> RetireOnDestroy), so the engine's records are read instead: records - the hands of the squad's 0x24
   records (one per person the put-away serialised; "" = a record with no hand); accounted - the hands the decision already accounts for
   (the batch's and held people, the squad's given people, people in an open offer or XFER of this game, a corpse or another game's person
   seen in its list). Every other record - a never-numbered person among them - is KEPT here (a record with no hand too: the safe side), so
   the squad settles asleep here, whole. -1 = no records (unread). */
/* T-1318 a4 fold 2 (re-check LOW-B): dead - each record's death at the RELEASE, read from the records the engine holds (coopown::GdcCharDeadOf,
   record order; 0 = not given): a record not accounted for whose body is dead (1 - a corpse removed before the living put-aways, so never
   among the corpse hands their put-aways saw) is neither kept nor given (*deadSkipped); one whose death did not read (-1, or no entry) is
   KEPT, the safe side (*deadUnread). A record with no hand is kept whatever its death. */
inline int SleepRecordsKept(const std::vector<std::string>& records, const std::set<std::string>& accounted, const std::vector<int>* dead = 0, int* deadSkipped = 0, int* deadUnread = 0)
{
    if (records.empty()) return -1;
    int k = 0;
    for (size_t i = 0; i < records.size(); ++i)
    {
        if (records[i].empty()) { ++k; continue; }
        if (accounted.count(records[i]) != 0) continue;
        if (dead != 0)
        {
            const int d = i < dead->size() ? (*dead)[i] : -1;
            if (d == 1) { if (deadSkipped != 0) ++*deadSkipped; continue; }
            if (d != 0 && deadUnread != 0) ++*deadUnread;
        }
        ++k;
    }
    return k;
}
/* T-1318 a4 fold 2 (re-check LOW-A): the RELEASE with NO awake list and the squad's sleeping records NOT read, where the numbered people alone
   would be offered whole: HELD - the 1 Hz re-decide (RelHeldTick) reads the records again; never offered as whole on that road. sleeping -
   the squad's sleeping platoon found now (1) or not (0); tries - the re-decides of this hold that met the records unread before this one.
   What ends it: the records read (decided by them, as any release); the squad gone (no sleeping platoon and no awake list) on a re-decide
   from the second on (tries >= 2), or tries reaching cap: settled asleep here, whole - never offered (kRuKeptGone / kRuKeptCap); the world
   teardown clears the hold (ReleaseForgetDrain). */
const int kRuHold = 0, kRuKeptGone = 1, kRuKeptCap = 2, kRuTryCap = 30;
inline int RecordsUnreadStep(int sleeping, int tries, int cap)
{
    if (tries >= cap) return kRuKeptCap;
    if (sleeping == 0 && tries >= 2) return kRuKeptGone;
    return kRuHold;
}
/* T-1318 a4 fold 1 (review LOW-1): the sweep met a never-numbered member of a squad whose put-aways are held (attempt 3c) - it is a re-wake
   (the hold is CANCELLED) only when its awake engine squad (personAp) is not the one the held put-aways came from (heldAp): a member that
   stays awake and unnumbered in the same, never-slept squad is not one. 0 unknown counts as a re-wake (as before). 1 = re-wake. */
inline int RebuiltIsRewake(const void* heldAp, const void* personAp) { return (heldAp != 0 && personAp != 0 && heldAp == personAp) ? 0 : 1; }
/* T-1318 attempt 3 (owner 1615 a): what the sweep's re-wake of a squad does to its index entry. Only this game's own part (asleep / kept
   here, or an offer this re-wake cancelled) leaves; the GIVEN part stays recorded as given - another game runs those people, and a re-made
   body of one is still skipped by its hand (handoff.cpp HandoffPersonGiven). kSrKeep: nothing of this game's own - nothing changes;
   kSrDropOwn: the own part goes, the entry stays with its given people; kSrDropEntry: the own part goes and nothing is given - the entry
   goes. */
enum { kSrKeep = 0, kSrDropOwn = 1, kSrDropEntry = 2 };
inline int SquadRebuiltAction(int givenCount, int asleepHereCount, int keptHereCount, int cancelledNow)
{
    if (SquadRebuiltDropsEntry(asleepHereCount, keptHereCount, cancelledNow) == 0) return kSrKeep;
    return givenCount > 0 ? kSrDropOwn : kSrDropEntry;
}
/* the receiver: which judge a hand-over meets and whose counters it feeds. Lent (T-1122: the message's lent bit, or the squad's raid block
   here) -> the lent counters as before; else the world bit -> whole or none with the world counters; else each person alone. Since fold 1
   (review HIGH-1) every XFER this game sends carries the world bit - the follow-leader one too - and every RELEASE offer does, so kWjEach
   is met only by a message with no whole bit, which no game at protocol 226 sends (an older one is refused at HELLO). */
enum { kWjEach = 0, kWjLent = 1, kWjWorld = 2 };
inline int WholeJudgeOf(int wholeBits, int blockHere)
{
    if ((wholeBits & kWholeBitLent) != 0 || blockHere != 0) return kWjLent;
    return (wholeBits & kWholeBitWorld) != 0 ? kWjWorld : kWjEach;
}
/* the tripwire: after an accepted hand-over's index write (RelAccept, the XFER ACK) or the asleep settle of an offer that had an accepted
   ACK - 1 = the squad is now given in part (counted and one capped line; the run must show 0). */
inline int PartGivenAtAck(int givenCount, int asleepHereCount, int keptHereCount) { return (givenCount > 0 && (asleepHereCount > 0 || keptHereCount > 0)) ? 1 : 0; }
/* T-1318 attempt 2 fold 1 (review MEDIUM-1): the tripwire also sees people of the squad still AWAKE here (run live by this game, read at
   the check - as SquadWholeGiven's liveHere, but fresh) beside people given. Bits: kPgAsleepKept - given and asleep / kept here (as
   PartGivenAtAck); kPgAwake - given and awake here. 0 = not given in part. */
enum { kPgNone = 0, kPgAsleepKept = 1, kPgAwake = 2 };
inline int PartGivenAt(int givenCount, int asleepHereCount, int keptHereCount, int awakeHereCount)
{
    if (givenCount <= 0) return kPgNone;
    return ((asleepHereCount > 0 || keptHereCount > 0) ? (int)kPgAsleepKept : 0) | (awakeHereCount > 0 ? (int)kPgAwake : 0);
}
/* T-1318 attempt 2 fold 1 (review HIGH-1 b): a person of a put-away NOT offered because its squad is wholly given (fold 2's settle). Only a
   person whose engine hand is one of the squad's GIVEN hands is a body this game re-made of a person another game runs - it leaves the
   index (kNoRowErase). Any other person is this game's own (a hand not given) or cannot be told (its hand did not read): it stays asleep
   here, indexed, and is counted - never dropped silently. */
enum { kNoRowErase = 0, kNoRowOwn = 1, kNoRowHandUnread = 2 };
inline int GivenNotOfferedRow(int handRead, int handIsGiven)
{
    if (handRead == 0) return kNoRowHandUnread;
    return handIsGiven != 0 ? kNoRowErase : kNoRowOwn;
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
/* T-1268 fold 1 (review M2): AND WHILE NOBODY HOLDS THE AREA YET (heldByOther -2, coopor::RewakeAreaOf), THE LATE ADOPTER WAITS. The
   sweep adopts only where the map names this game and skips 'nobody yet' without an orphan note, so ACCEPT then could leave the
   re-woken body here to be adopted under a fresh id once this game is named - one person shown twice. The member is kept aside and
   decided again by the first area map applied after it (AdoptAfterRewakeDue), with AdoptAfterRewake's rule - the same moment the
   sweep's own wait ends. Not awake here: ACCEPT as before. */
const int kSettleWait = 5;
const int kRewakeNobodyYet = -2;
inline int AdoptAfterRewakeWait(int squadAwakeHere, int heldByOther)
{
    if (squadAwakeHere != 0 && heldByOther == kRewakeNobodyYet) return kSettleWait;
    return AdoptAfterRewake(squadAwakeHere, heldByOther);
}
/* a waiting member is decided again only when an area map was applied since it was set aside (the map's arrival is the event) */
inline int AdoptAfterRewakeDue(unsigned int seqAtWait, unsigned int seqNow) { return seqNow != seqAtWait ? 1 : 0; }
/* T-1268 fold 2: A LATE ADOPTION ACK WHILE A MEMBER WAITS. The member is in no release record and not settled, so the ACK would find
   nothing to answer it. It joins the waiting entry's late adopters (kRwLateAdd) and the wait's decision answers it as the settle always
   did (RelAccept revokes every late adopter but the winner; a cancel revokes them all); the waiting entry's own adopter, or one already
   listed, is a duplicate (kRwLateDup). */
enum { kRwLateDup = 0, kRwLateAdd = 1 };
inline int RewakeLateAck(int entryFrom, int alreadyListed, int ackFrom)
{
    return (ackFrom == entryFrom || alreadyListed != 0) ? kRwLateDup : kRwLateAdd;
}
/* T-1268 fold 2: A PLAYER GONE WHILE A MEMBER WAITS (the release's player-gone drain, HandoffRevokesPlayerGone). Its late adopter rows go;
   an ACK entry whose adopter is the gone player becomes a settle entry (kind 1) with the late adopters left. The wait is needed only while
   an adopter is left to answer (an ACK entry, or a settle with late adopters); with none, the entry is decided at once - the member stays
   this game's (cancelled: the engine runs it here again). Returns the entry's new kind. */
inline int RewakeOnGone(int kind, int from, int goneSlot) { return (kind == 0 && from == goneSlot) ? 1 : kind; }
inline int RewakeWaitNeeded(int kind, int lateLeft) { return (kind == 0 || lateLeft > 0) ? 1 : 0; }
/* [a1b2f1-lo3] [review F4]: a REVOKE the receiver cannot apply now (engine writes blocked) waits for the next safe point - never dropped */
enum { kRvNowApply = 0, kRvNowDefer = 1 };
inline int RevokeWhen(int writesBlocked) { return writesBlocked != 0 ? kRvNowDefer : kRvNowApply; }

/* 1.3 RELEASE (68): u32 releaseId | u32 keyUid | u16 sectorKey | u8 flags | u8 n | u16 winnerSlot | u16 0 | n x { XferMember (48) | u32 gen }
   (gen: the offer's - the gen the candidate holds if it adopts; REVOKE: the winner's). RELEASE_ACK (69): u32 releaseId | u16 nAdopted |
   u16 nDropped | u16 nDeferred | the three uid lists. Exactly those lengths; n 1..64; id 0 refused. */
const unsigned int kRelFlagPutAway = 1u, kRelFlagRevoke = 2u, kRelFlagLentWhole = 4u, kRelFlagWorldWhole = 8u, kRelNoWinner = 0xFFFFu;
/* T-1318 attempt 2 (design D3, protocol 226): kRelFlagWorldWhole, with PUT_AWAY only - a world squad's offer, adopted whole or none (set
   on every RELEASE offer: a RELEASE only ever carries a world squad's people - a player's are never offered, handoff.cpp ReleaseOnPutAway).
   The lent bit (4) keeps its T-1122 meaning; a lent squad's offer carries both. */
/* T-1122 lent flag (protocol 224): kRelFlagLentWhole, with PUT_AWAY only - a lent squad's offer, adopted whole or none from this message
   alone (a REVOKE never carries it) */
/* [a1b2f1-lo4] [review F6]: the flags are exactly PUT_AWAY or REVOKE - 0, both or an unknown bit is malformed (never taken as an offer) */
inline int ReleaseFlagsValid(unsigned int flags)
{
    if (flags == kRelFlagRevoke) return 1;
    return ((flags & kRelFlagPutAway) != 0u && (flags & ~(kRelFlagPutAway | kRelFlagLentWhole | kRelFlagWorldWhole)) == 0u) ? 1 : 0;
}
inline int ReleaseLentWhole(unsigned int flags) { return (flags & kRelFlagLentWhole) != 0u ? 1 : 0; }
inline int ReleaseWorldWhole(unsigned int flags) { return (flags & kRelFlagWorldWhole) != 0u ? 1 : 0; }   /* T-1318 attempt 2 */
const size_t kReleaseHead = 16, kReleaseRowSize = 52, kReleaseAckHead = 10;
/* kReleaseMaxMembers (64): declared above, with T-1318 attempt 2's WorldReleaseOffer */
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

/* T-650 fold 1 (owner decision 580, 2026-10-09: decision 15 kept - a squad is handed over only when it leaves its runner's engine-loaded
   list, and only to a game that has the area loaded). AreaReceiverOrder - THE GAMES THAT MAY RUN A SQUAD STANDING IN AN AREA, in offer
   order. Used by the forced hand-over (once the squad left this game's loaded list) and by the RELEASE after this game's engine put it away.
   rows = one locked read of the world server's area map for the area (coop::AreaSquadViewTS): each seated slot, whether it is IN_WORLD on
   this game's roster, and its bit in the effective loaded map (1 set, 0 clear, -1 read stale); mapFresh = that map is fresh; holderSlot =
   the slot the map names as the area's holder (-1 none); tried = slots this release offered already.
   Returns -1 = HOLD (no fresh map, a row read stale, or this game has no slot of its own yet - review L3: nobody can say who has the area);
   else the number of slots written to order (<= cap): every other IN_WORLD game whose bit is set and that was not tried, the holder first
   when it is one of them, then the lower slot first. 0 = no other in-world game has the area loaded (the forced hand-over keeps the squad;
   the RELEASE ends asleep). Naming the holder alone never makes a receiver: the world server keeps a holder for a while after its game
   stopped listing the area (review HIGH-1). *notInWorld (may be 0) = games skipped only because they are not IN_WORLD.
   The receiver's own engine-loaded check stays the final say: a map lagging it costs one refused offer (the RELEASE adds it to tried). */
/* T-650 fold 2 (T1056; Read from the engine's ActivePlatoon frame check 0x4FE140 and the keep-rectangle builder 0xA088A0, exe constants
   Confirmed). THE ENGINE'S KEEP RULE: each frame a squad's CENTRE is tested against its area's square widened by 10 on every side, then
   narrowed by 170 on each SIDE whose neighbour area is not loaded (the neighbour's ZoneMap +0xB1 and +0xB0 both clear; diagonal areas are
   never read) - a centre within 160 of such a side is put away after a 4 s grace even inside a loaded area. EngineKeepsAt asks that
   question with a margin: 1 = the area is loaded and, for each side, the neighbour is loaded or (x, z) is at least kEngineEdgeStrip +
   margin from that side; 0 = not kept; -1 = the area's own flag, or a side flag that would decide, unreadable (< 0).
   minX / minZ = the area's low corner, size = its side (AreaMinUnits / kAreaSizeUnits); W = low x, E = high x, S = low z, N = high z. */
const float kEngineEdgeStrip = 160.0f;    /* 170 narrowed - 10 widened (the engine's own constants) */
const float kKeepMarginUnits = 250.0f;    /* tuned from the run's P119 samples (manager decision 2026-10-09) */
const float kAreaSizeUnits = 4608.0f, kAreaOriginUnits = 147456.0f;   /* zones.cpp kSectorSize / kSectorOrigin */
inline float AreaMinUnits(int index) { return (float)index * kAreaSizeUnits - kAreaOriginUnits; }
inline int EngineKeepsAt(float x, float z, float minX, float minZ, float size, int selfLoaded, int wLoaded, int eLoaded, int sLoaded, int nLoaded, float margin)
{
    if (selfLoaded <= 0) return selfLoaded < 0 ? -1 : 0;
    const float strip = kEngineEdgeStrip + margin;
    const float dist[4] = { x - minX, (minX + size) - x, z - minZ, (minZ + size) - z };
    const int side[4] = { wLoaded, eLoaded, sLoaded, nLoaded };
    int unknown = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (side[i] > 0 || dist[i] >= strip) continue;
        if (side[i] < 0) unknown = 1; else return 0;
    }
    return unknown != 0 ? -1 : 1;
}
/* T-1046 fix 2 review fold (review 4, cost): the keep question's receiver test is skipped when its answer is already known to be 0 -
   the link live (ReceiversFor's own first test), this game has a slot (AreaReceiverOrder's), and the fresh effective map shows no other
   seat with the area loaded (OtherLoadedTS == 0: the same map, the same 5 s clock - every other row then reads loaded 0 and is passed
   over). Any other case (stale map -1, another game's bit 1, no link, no slot - all of which can make k -1 or >= 1) runs the test.
   1 = skip (k = 0, nobody named). */
inline int KeepReceiversKnownNone(int liveReady, int mySlot, int otherLoaded) { return (liveReady != 0 && mySlot >= 0 && otherLoaded == 0) ? 1 : 0; }
/* T-1046 fix 2 fold 2 (T2305 leg N): A PERSON'S HAND as the sleeping squad's records carry it - Character::serialise writes the
   character's own handle (Character +0x58) into its 0x24 record as int fields "handleS" / "handleTYPE" (decomp_627610:223-224);
   ownrec.h GdcSpans forms the same text (HandId) from those fields. */
inline std::string HandKey(unsigned int serial, unsigned int type) { char b[48]; std::sprintf(b, "S=%d;T=%d", (int)serial, (int)type); return std::string(b); }
/* T-1046 leg-N fold 2: the hand an XFER ACK's given entry carries - read from the body when it is still whole at the ACK, else as read
   at the send (Pending::handOf); "" = neither read */
inline std::string AckHand(const std::string& bodyHand, const std::string& sentHand) { return !bodyHand.empty() ? bodyHand : sentHand; }
/* T-1318 attempt 3 (owner 1615 a; T2324, F-T2324-WHOLEKEPT): a put-away person is "re-woken here" only when a body of THAT person is live
   in this game's engine - one body of the squad id's awake people at a time: bodyUid / bodyUidMine - the body's uid here (0 = none yet)
   and whether this game runs it; bodyHand - its hand ("" unread). It is that person when this game runs it under the person's own uid, or
   it carries the person's hand (a fresh body the engine re-made, before or after the sweep gave it a fresh uid). Another awake member of
   the squad id is not that person (T2324: the squad's own 7 awake people made each put-away of the other 5 read "re-woken" - CANCELLED /
   REVOKED), and this game's copy of a person another game runs (a uid here that is not this game's) never is. 1 = this body is the
   person. */
inline int RewokeBodyIs(unsigned int bodyUid, int bodyUidMine, unsigned int personUid, const std::string& bodyHand, const std::string& personHand)
{
    if (bodyUid != 0 && bodyUidMine == 0) return 0;
    if (bodyUid != 0 && bodyUid == personUid) return 1;
    return (!personHand.empty() && bodyHand == personHand) ? 1 : 0;
}
/* the slot that took EVERY person of a sleeping squad: sleepers = the hands of the squad's own person records (one per 0x24 record);
   slotOfHand = each person of the squad this game gave away (the squad index's given) -> the slot it went to (-1 unknown). Returns
   that one slot, or -1 when the list is empty, a sleeper's hand is unknown or not given (kept here, never offered, or still in an
   offer), or the sleepers went to different games. People kept or given who are NOT among the sleepers do not count. */
/* T-1318 a4 fold 1 (review MEDIUM-1): -1 means only 'read, a living sleeper NOT given' (a person of this game's among them - the wake goes
   on here). Not known - kSleepersUnread (-2): a sleeper record with no hand; a sleeper not found among the given while a given entry has no
   recorded hand (givenHandless: it could be that person - handoff.cpp HandoffPersonGiven's rule); no living sleeper. Every living sleeper
   given but not all to one known slot (two games, or a slot not known) - kSleepersSplit (-3): given, not taken by one. dead - the hands of
   the squad's corpses (seen dead in its engine list at a put-away): a corpse counts as neither given nor kept (a world faction's corpse stays
   in its squad and the wake re-makes it a corpse - Inferred, handoff.cpp SquadBodyAwake's note). */
const int kSleepersUnread = -2, kSleepersSplit = -3;
/* T-1318 attempt 5 (owner 1626, D1-R3): another game's copies seen in the squad (its engine list at a put-away, a copy's own put-away) -
   their hands -> that game's slot (other) - are added to the given hands (slotOfHand) so a sleeping record of such a copy counts as taken
   by the game that runs it, never as 'not given' (T2359 A 1187.376: six of B's copies in the given squad's records let the wake through).
   A hand already given keeps its given slot. Returns the hands added. False assumption fixed: 'a hand not given here belongs to this game'. */
/* T-1318 a5 fold 1 (review MEDIUM-2): a noted other-game hand is read with its uid's CURRENT state - a uid this game runs now (taken back)
   or with no owner record (ownerSlot -2) is skipped (kOtherHandSkip); else its owner's slot now (-1: given, not to one known slot). */
const int kOtherHandSkip = -9;
inline int OtherHandSlotNow(int uidMineNow, int ownerSlot)
{
    if (uidMineNow != 0 || ownerSlot == -2) return kOtherHandSkip;
    return ownerSlot;
}
inline int SlotOfHandAddOther(std::map<std::string, int>* slotOfHand, const std::map<std::string, int>& other)
{
    int n = 0;
    for (std::map<std::string, int>::const_iterator o = other.begin(); o != other.end(); ++o)
    {
        if (o->first.empty() || slotOfHand->count(o->first) != 0) continue;
        (*slotOfHand)[o->first] = o->second; ++n;
    }
    return n;
}
/* T-1318 attempt 5 (owner 1626, D1-R1): the people of a squad this game keeps asleep (kept / asleepHere uids) that a re-wake RE-MADE - those
   whose hand (handOf, read when they were kept) is among the woken squad's living members' hands (woken). A person with no recorded hand,
   or not listed, was not re-made and stays kept (a ghost kept person holds the squad here whole - the safe side). Returns the count;
   *remade gets the uids. False assumption fixed: 'a re-wake re-made the whole squad' (T2359: Herbivore_9's kept 2 were erased, 0 of them
   in the engine's list, and the rest offered as whole). */
inline int KeptRemadeByHands(const std::set<unsigned int>& kept, const std::map<unsigned int, std::string>& handOf, const std::set<std::string>& woken, std::set<unsigned int>* remade)
{
    int n = 0;
    for (std::set<unsigned int>::const_iterator u = kept.begin(); u != kept.end(); ++u)
    {
        const std::map<unsigned int, std::string>::const_iterator h = handOf.find(*u);
        if (h == handOf.end() || h->second.empty() || woken.count(h->second) == 0) continue;
        if (remade != 0) remade->insert(*u);
        ++n;
    }
    return n;
}
inline int SleepersTakenSlot(const std::vector<std::string>& sleepers, const std::map<std::string, int>& slotOfHand, int givenHandless = 0, const std::set<std::string>* dead = 0)
{
    if (sleepers.empty()) return -1;
    int slot = -1, split = 0, living = 0, notGiven = 0, unread = 0;
    for (size_t i = 0; i < sleepers.size(); ++i)
    {
        if (sleepers[i].empty()) { unread = 1; continue; }
        if (dead != 0 && dead->count(sleepers[i]) != 0) continue;
        ++living;
        std::map<std::string, int>::const_iterator g = slotOfHand.find(sleepers[i]);
        if (g == slotOfHand.end()) { if (givenHandless != 0) unread = 1; else notGiven = 1; continue; }
        if (g->second < 0) { split = 1; continue; }
        if (slot >= 0 && g->second != slot) split = 1;
        if (slot < 0) slot = g->second;
    }
    if (notGiven != 0) return -1;
    if (unread != 0 || living == 0) return kSleepersUnread;
    return split != 0 ? kSleepersSplit : slot;
}
/* keeps = that game's engine would keep the squad at its spot (EngineKeepsAt over the game's map bits for the area and its
   side neighbours; 1 by default - a caller without a position reads every loaded game as keeping). A row with keeps 0 is skipped and
   counted in *notKeep (may be 0); keeps < 0 HOLDs like a stale bit. */
struct AreaRow { int slot, inWorld, loaded, keeps; AreaRow() : slot(-1), inWorld(0), loaded(0), keeps(1) {} };
inline int AreaReceiverOrder(const AreaRow* rows, int n, int mapFresh, int holderSlot, int mySlot, const int* tried, int nTried, int* order, int cap, int* notInWorld, int* notKeep = 0)
{
    if (notInWorld != 0) *notInWorld = 0;
    if (notKeep != 0) *notKeep = 0;
    if (mapFresh == 0 || mySlot < 0) return -1;
    std::vector<int> pick;   /* the eligible slots, kept in ascending order */
    for (int i = 0; i < n && rows != 0; ++i)
    {
        const AreaRow& r = rows[i];
        if (r.loaded < 0) return -1;
        if (r.slot < 0 || r.slot == mySlot || r.loaded == 0) continue;
        bool skip = false;
        for (int t = 0; t < nTried && tried != 0 && !skip; ++t) if (tried[t] == r.slot) skip = true;
        for (size_t o = 0; o < pick.size() && !skip; ++o) if (pick[o] == r.slot) skip = true;
        if (skip) continue;
        if (r.inWorld == 0) { if (notInWorld != 0) ++*notInWorld; continue; }
        if (r.keeps < 0) return -1;
        if (r.keeps == 0) { if (notKeep != 0) ++*notKeep; continue; }   /* its engine would put the squad away at this spot */
        size_t at = pick.size();
        while (at > 0 && pick[at - 1] > r.slot) --at;
        pick.insert(pick.begin() + (long)at, r.slot);
    }
    for (size_t i = 1; i < pick.size(); ++i)
        if (pick[i] == holderSlot) { pick.erase(pick.begin() + (long)i); pick.insert(pick.begin(), holderSlot); break; }
    int k = 0;
    for (size_t i = 0; i < pick.size() && k < cap && order != 0; ++i) order[k++] = pick[i];
    return k;
}

}   /* namespace cooplo */

#endif
