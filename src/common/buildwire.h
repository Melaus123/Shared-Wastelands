/* src/common/buildwire.h - build1-b/c/d/e/f (docs/design-build1.md 2-3; session protocol 63). A construction THIS game's player
 * faction placed crosses to the other game, which creates a COPY owned by coop-peer at its K2 safe point
 * (src/coop-plugin/build.cpp BuildCopyDrain, the "Safe call recipe" of build/answers-build1b.md).
 *
 *   kind u8 (1 PLACE) | u8 len + P7n key | u8 len + record sid | 3 x f32 pos | 4 x f32 rot (Ogre w,x,y,z)
 *   | u8 complete (0/1) | f32 progress | f32 needed | u8 len + host key (empty for a free-standing piece)
 *   | u8 host form | i32 floor | u8 outside (build1-e, protocol 62: furniture - the host's P7n key, which form the
 *   builder's furnitureOf took (0 *(host+0x1F0)+0x30, 1 that layout's vt+0x40, 2 none: isIndoorsOf only), and
 *   createBuilding's floor / outsideFurniture; pos / rot are then in the HOST'S LOCAL frame)
 *   kind u8 (2 STATE, build1-c) | u8 len + P7n key | f32 progress | f32 needed | u8 complete (0/1)
 *   | u8 n (<= 16) | n x f32 delivered (ConstructionState mats[i]+0xC, in the engine's own material order)
 *   P15 (protocol 98): the STATE's complete byte carries bit 1 = DESTROYED (the owner's piece is a ruin, Building +0x1A1); any
 *   other bit is refused
 *   kind u8 (3 REMOVE, build1-d) | u8 len + P7n key | u8 reason (1 dismantled, 2 destroyed): the builder's piece is gone,
 *   and the other game removes its copy (zeroed delivered mats, then the engine's vt+0x248(FLT_MAX))
 *   kind u8 (4 ROSTER_REQUEST, build1-f fold, protocol 63) and nothing else: the sender's world runs and the link is up;
 *   the receiver owes it a whole roster (a PLACE + STATE for every own live piece)
 *
 * A PLACE's rotation must be a plausible quaternion: |q|^2 in [0.5, 2] (review-build1b note 4).
 *
 * pos / rot are the exact floats the builder's createBuilding received. Every message carries the piece's WHOLE state.
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <string>
#include <map>        /* house2 fold: BuildOwedMergeRow */
#include <set>        /* house2 fold 2: the keys erased while the owed file was unread */
#include <vector>
#include <algorithm>  /* help1 fold 2: BuildHelpResync sorts */

namespace coopbuild {

const unsigned char kBuildPlace = 1;
const unsigned char kBuildState = 2;     /* build1-c */
const unsigned char kBuildRemove = 3;    /* build1-d */
const unsigned char kBuildRosterRequest = 4;
const unsigned char kBuildHandAck = 5;         /* house1b (protocol 75): the house owner holds a piece handed to it - the key only */   /* build1-f fold (review-build1f H2): the kind byte only */
const unsigned char kBuildHelpWork = 8;        /* help1 (owner 172, protocol 90): a helper's work on its copy of another player's piece, to the owner's game (6 / 7 are the farm's, 10 MINE_OP);
                                                  help1 fold 3 (protocol 95): + a trailing u32 nonce - the owner's placement the work was done on */
const unsigned char kBuildHelpGone = 9;        /* help1 fold (review MED 3, protocol 91): the owner's answer to work for a piece it no longer holds as its own (removed, dismantled,
                                                  handed, its removed row swept) - key | u32 seq: the helper drops every entry at or below seq and hands their materials back.
                                                  help1 fold 2 (protocol 92): key | u32 seq | u32 applied - the owner's last applied seq from that helper for that key
                                                  (0 = none): entries at or below it WERE applied (dropped, never handed back); only those above it come back.
                                                  help1 fold 3 (protocol 95): key | u32 seq | u32 applied | u32 nonce | u8 n | n x f32 refused - the placement the
                                                  work was done on (the HELP_WORK's nonce) and that helper slot's CUMULATIVE refused amounts for it (the STATE tail's),
                                                  handed back less what was already handed back; sent only once the owner's record holding them has landed */
const unsigned int  kBuildMaxHelpers = 8;      /* help1: helper slots a STATE's confirmation tail carries */
const unsigned char kBuildHelpTailLive = 0x80;  /* help1 fold 3 (protocol 95): set on a STATE tail's row count - each row carries u32 live after seq */
const unsigned char kBuildHelpTailWide = 0x40;  /* set on a STATE tail's row count: each row's helper slot is two bytes (little-endian, 0..1023);
                                                  without it (a pp.build record written before) the slot is one byte */
const float         kBuildHelpMax = 1.0e6f;    /* help1: a HELP_WORK progress or material delta above this (or below 0) is refused */
const unsigned char kBuildReasonDismantled = 1;   /* build1-d REMOVE reasons */
const unsigned char kBuildReasonDestroyed = 2;
const unsigned char kBuildStateDestroyedBit = 2;   /* P15 (protocol 98): a STATE's complete byte, bit 1 = destroyed */
const unsigned char kBuildHostPlain = 0;        /* build1-e: furnitureOf = *(host+0x1F0)+0x30 (the host's interior layout) */
const unsigned char kBuildHostSub = 1;          /* build1-e: that layout's vt+0x40 (the commit's other branch, 4d6810:176) */
const unsigned char kBuildHostIndoorsOnly = 2;  /* build1-e: no furnitureOf, isIndoorsOf = host only */
const int           kBuildMaxFloor = 1024;      /* build1-e: |floor| limit */
const unsigned int  kBuildMaxMats = 16;  /* build1-c: delivered-material slots a STATE carries */
const unsigned int  kBuildMaxKey = 63;   /* build.cpp kKeyCap (64) less its NUL */
const unsigned int  kBuildMaxSid = 95;

const int kBuildDecodeOk       = 0;
const int kBuildDecodeTooShort = 1;
const int kBuildDecodeBadKind  = 2;   /* PLACE (1), STATE (2), REMOVE (3, build1-d) and ROSTER_REQUEST (4, build1-f) exist */
const int kBuildDecodeBadKey   = 3;   /* an empty key or sid, or a key / sid / host key over its limit */
const int kBuildDecodeBadValue = 4;   /* a float that is not finite, complete not 0/1, |rot|^2 outside [0.5, 2], n > 16, or a REMOVE reason not 1/2 */

/* help1: one row of a STATE's optional confirmation tail - per helper slot, the last HELP_WORK seq the owner's game applied to this piece
   and the CUMULATIVE material amounts it refused (past a material's total); the helper's game hands those back as items.
   help1 fold 3 (finding 1): on the wire seq is the LANDED last applied (on the owner's disk) and live the owner's live last applied */
struct BuildHelpAck
{
    unsigned short slot;    /* the helper's notebook slot, 0..kBuildOwnerSlotMax */
    unsigned int seq;
    unsigned int live;      /* help1 fold 3: a STATE tail's live last applied (>= seq; 0 in the owner's own rows, which hold the live seq in seq) */
    unsigned char nEx;
    float ex[kBuildMaxMats];
    BuildHelpAck() : slot(0), seq(0), live(0), nEx(0) { for (unsigned int i = 0; i < kBuildMaxMats; ++i) ex[i] = 0.0f; }
};

/* P87 fold 1: a REMOVE's optional tail - kind | key | reason [| u32 nonce [| u8 flags [| hostKey when kBuildRmFurniture]]], each part
   written only when it or a later one is set. The nonce is the placement the REMOVE ends (0 = unknown). An older receiver's decode
   stops after the reason (it never read past it), so the tail costs it nothing. */
const unsigned char kBuildRmResend = 1;      /* wire: a tombstone's REMOVE sent again (a roster round) - the receiver counts a miss apart */
const unsigned char kBuildRmTombTail = 2;    /* pp.build tombstone row: written with the fold-1 tail (its nonce and furniture-ness known) */
const unsigned char kBuildRmFurniture = 4;   /* pp.build tombstone row: layout furniture - its host's key follows */
struct BuildMsg
{
    unsigned char kind;
    std::string key, sid, hostKey;
    float pos[3];
    float rot[4];
    unsigned char complete;
    unsigned char destroyed;        /* P15 (protocol 98) STATE: 1 = the owner's piece is a ruin (+0x1A1) - the complete byte's bit 1 */
    float progress, needed;
    unsigned char nMats;            /* build1-c STATE: how many of mats[] are carried */
    unsigned char reason;           /* build1-d REMOVE: kBuildReasonDismantled / kBuildReasonDestroyed */
    unsigned char hostForm;         /* build1-e PLACE: kBuildHost* (0 when hostKey is empty) */
    int floor;                      /* build1-e PLACE: createBuilding's floor */
    unsigned char outside;          /* build1-e PLACE: createBuilding's outsideFurniture (0/1) */
    unsigned short ownerSlot;       /* PLACE: the piece's owner - 0..kBuildOwnerSlotMax = handed to that player (a house owner), kBuildOwnerSender = the sender; two bytes on the wire */
    unsigned int nonce;             /* house2 fold PLACE (protocol 76): the placer's per-placement nonce, 0 = none (written only when non-zero); help1 fold 3 HELP_WORK / HELP_GONE: the placement the work was done on */
    unsigned int seq;               /* help1 HELP_WORK: the helper's per-piece sequence (> 0); progress = dProgress, nMats / mats = dMat */
    unsigned char reset;            /* help1 HELP_WORK: 1 = the helper's copy was a ruin and the work rebuilt it (the engine's reset) */
    unsigned int applied;           /* help1 fold 2 HELP_GONE (protocol 92): the owner's last applied seq from that helper for that key (0 = none); fold 3: nMats / mats = its refused tail */
    unsigned char rmFlags;          /* P87 fold 1 REMOVE: kBuildRm* (0 = none); nonce = the placement it ends, hostKey = a furniture tombstone's host */
    std::vector<BuildHelpAck> helpAcks;   /* help1 STATE: the confirmation tail (empty = not written); fold 3: each row landed seq + live */
    float mats[kBuildMaxMats];
    BuildMsg() : kind(0), complete(0), destroyed(0), progress(0.0f), needed(0.0f), nMats(0), reason(0), hostForm(0), floor(0), outside(0), ownerSlot(0xFFFF),
                 nonce(0), seq(0), reset(0), applied(0), rmFlags(0)
    {
        for (int i = 0; i < 3; ++i) pos[i] = 0.0f;
        rot[0] = 1.0f; rot[1] = 0.0f; rot[2] = 0.0f; rot[3] = 0.0f;
        for (unsigned int i = 0; i < kBuildMaxMats; ++i) mats[i] = 0.0f;
    }
};

const unsigned short kBuildOwnerSender = 0xFFFF;   /* a PLACE's ownerSlot meaning "the sender's own piece" */
const unsigned short kBuildOwnerSlotMax = 1023;   /* the notebook's slots 0..1023 (slotwire.h kSlotMax) */
const unsigned char kBuildOwnerSenderOneByte = 0xFF;   /* the sender, in a PLACE whose owner number is one byte (pp.build rows and owed-file rows saved by builds that wrote one byte) */

inline bool BuildFloatOk(float v) { return v == v && v > -1.0e30f && v < 1.0e30f; }
/* review-build1b note 4: (0,0,0,0) or a huge quaternion is refused; an honest builder sends a unit one */
inline bool BuildRotOk(const float* q)
{
    for (int i = 0; i < 4; ++i) if (!BuildFloatOk(q[i]) || q[i] > 2.0f || q[i] < -2.0f) return false;
    const float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    return n >= 0.5f && n <= 2.0f;
}

/* help1: a HELP_WORK delta - finite, 0..kBuildHelpMax */
inline bool BuildHelpAmountOk(float v) { return BuildFloatOk(v) && v >= 0.0f && v <= kBuildHelpMax; }
/* help1: a STATE's tail - at most kBuildMaxHelpers rows, distinct slots, <= 16 finite non-negative amounts; help1 fold 3: a row names a
   landed seq or a live one (seq 0 = nothing of that slot's on disk yet, live > 0 = applied since) - never both 0 */
inline bool BuildHelpAcksOk(const std::vector<BuildHelpAck>& a)
{
    if (a.size() > kBuildMaxHelpers) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if ((a[i].seq == 0 && a[i].live == 0) || a[i].nEx > kBuildMaxMats || a[i].slot > kBuildOwnerSlotMax) return false;
        for (size_t j = 0; j < i; ++j) if (a[j].slot == a[i].slot) return false;
        for (unsigned int k = 0; k < a[i].nEx; ++k) if (!BuildFloatOk(a[i].ex[k]) || a[i].ex[k] < 0.0f) return false;
    }
    return true;
}

inline bool BuildEncodable(const BuildMsg& m)
{
    if (m.kind == kBuildHelpWork)   /* help1 */
    {
        if (m.key.empty() || m.key.size() > kBuildMaxKey || m.seq == 0 || m.reset > 1 || m.nMats > kBuildMaxMats) return false;
        for (unsigned int i = 0; i < m.nMats; ++i) if (!BuildHelpAmountOk(m.mats[i])) return false;
        return BuildHelpAmountOk(m.progress);
    }
    if (m.kind == kBuildHelpGone)   /* help1 fold: key + seq; fold 3: + the refused amounts (<= 16, finite, non-negative) */
    {
        if (m.key.empty() || m.key.size() > kBuildMaxKey || m.seq == 0 || m.nMats > kBuildMaxMats) return false;
        for (unsigned int i = 0; i < m.nMats; ++i) if (!BuildFloatOk(m.mats[i]) || m.mats[i] < 0.0f) return false;
        return true;
    }
    if (m.kind == kBuildRosterRequest) return m.key.empty() && m.sid.empty() && m.hostKey.empty();   /* build1-f: no body */
    if (m.kind == kBuildHandAck) return !m.key.empty() && m.key.size() <= kBuildMaxKey;   /* house1b: the key only */
    if (m.kind == kBuildRemove)   /* build1-d; P87 fold 1: a furniture tail's host key fits */
        return !m.key.empty() && m.key.size() <= kBuildMaxKey
            && (m.reason == kBuildReasonDismantled || m.reason == kBuildReasonDestroyed)
            && ((m.rmFlags & kBuildRmFurniture) == 0 || m.hostKey.size() <= kBuildMaxKey);
    if (m.kind == kBuildState)
    {
        if (m.key.empty() || m.key.size() > kBuildMaxKey || m.complete > 1 || m.destroyed > 1 || m.nMats > kBuildMaxMats) return false;
        for (unsigned int i = 0; i < m.nMats; ++i) if (!BuildFloatOk(m.mats[i])) return false;
        if (!BuildHelpAcksOk(m.helpAcks)) return false;   /* help1 */
        return BuildFloatOk(m.progress) && BuildFloatOk(m.needed);
    }
    if (m.kind != kBuildPlace || m.key.empty() || m.sid.empty() || m.key.size() > kBuildMaxKey
        || m.sid.size() > kBuildMaxSid || m.hostKey.size() > kBuildMaxKey || m.complete > 1)
        return false;
    if (m.hostForm > kBuildHostIndoorsOnly || (m.hostKey.empty() && m.hostForm != 0) || m.outside > 1
        || m.floor < -kBuildMaxFloor || m.floor > kBuildMaxFloor)   /* build1-e */
        return false;
    if (m.ownerSlot != kBuildOwnerSender && m.ownerSlot > kBuildOwnerSlotMax) return false;
    for (int i = 0; i < 3; ++i) if (!BuildFloatOk(m.pos[i])) return false;
    if (!BuildRotOk(m.rot)) return false;
    return BuildFloatOk(m.progress) && BuildFloatOk(m.needed);
}

inline void BuildPutF(char** p, float v) { std::memcpy(*p, &v, 4); *p += 4; }
inline void BuildPutS(char** p, const std::string& s)
{
    **p = (char)(unsigned char)s.size(); *p += 1;
    if (!s.empty()) std::memcpy(*p, s.data(), s.size());
    *p += s.size();
}

/* false (nothing appended) when the message is not BuildEncodable. */
inline bool EncodeBuild(std::vector<char>* b, const BuildMsg& m)
{
    if (b == 0 || !BuildEncodable(m)) return false;
    if (m.kind == kBuildRosterRequest) { b->push_back((char)m.kind); return true; }   /* build1-f: the kind byte only */
    const size_t at = b->size();
    if (m.kind == kBuildHelpWork)   /* help1: kind | key | u32 seq | f32 dProgress | u8 reset | u8 n | n x f32 dMat; fold 3 (protocol 95): | u32 nonce */
    {
        b->resize(at + 1 + 1 + m.key.size() + 4 + 4 + 1 + 1 + 4 * (size_t)m.nMats + 4);
        char* w = &(*b)[at];
        *w++ = (char)m.kind;
        BuildPutS(&w, m.key);
        std::memcpy(w, &m.seq, 4); w += 4;
        BuildPutF(&w, m.progress);
        *w++ = (char)m.reset;
        *w++ = (char)m.nMats;
        for (unsigned int i = 0; i < m.nMats; ++i) BuildPutF(&w, m.mats[i]);
        std::memcpy(w, &m.nonce, 4);   /* help1 fold 3: the placement the work was done on */
        return true;
    }
    if (m.kind == kBuildHelpGone)   /* help1 fold (protocol 91): kind | key | u32 seq; fold 2 (protocol 92): | u32 applied; fold 3 (protocol 95): | u32 nonce | u8 n | n x f32 refused */
    {
        b->resize(at + 1 + 1 + m.key.size() + 4 + 4 + 4 + 1 + 4 * (size_t)m.nMats);
        char* g = &(*b)[at];
        *g++ = (char)m.kind;
        BuildPutS(&g, m.key);
        std::memcpy(g, &m.seq, 4); g += 4;
        std::memcpy(g, &m.applied, 4); g += 4;
        std::memcpy(g, &m.nonce, 4); g += 4;
        *g++ = (char)m.nMats;
        for (unsigned int i = 0; i < m.nMats; ++i) BuildPutF(&g, m.mats[i]);
        return true;
    }
    if (m.kind == kBuildHandAck)   /* house1b: kind + key */
    {
        b->resize(at + 1 + 1 + m.key.size());
        char* a = &(*b)[at];
        *a++ = (char)m.kind;
        BuildPutS(&a, m.key);
        return true;
    }
    if (m.kind == kBuildRemove)   /* build1-d; P87 fold 1: | u32 nonce [| u8 flags [| hostKey]] - nothing past the reason when all are unset */
    {
        const int withFlags = (m.rmFlags != 0) ? 1 : 0;
        const int withNonce = (m.nonce != 0 || withFlags != 0) ? 1 : 0;
        const int withHost = ((m.rmFlags & kBuildRmFurniture) != 0) ? 1 : 0;
        b->resize(at + 1 + 1 + m.key.size() + 1 + (withNonce != 0 ? 4 : 0) + (withFlags != 0 ? 1 : 0) + (withHost != 0 ? 1 + m.hostKey.size() : 0));
        char* r = &(*b)[at];
        *r++ = (char)m.kind;
        BuildPutS(&r, m.key);
        *r++ = (char)m.reason;
        if (withNonce != 0) { std::memcpy(r, &m.nonce, 4); r += 4; }
        if (withFlags != 0) *r++ = (char)m.rmFlags;
        if (withHost != 0) BuildPutS(&r, m.hostKey);
        return true;
    }
    if (m.kind == kBuildState)
    {
        size_t tail = 0;   /* u8 rows (| kBuildHelpTailLive | kBuildHelpTailWide) | per row u16 slot, u32 seq, u32 live, u8 n, n x f32 refused
                              (cumulative) - only when rows exist */
        if (!m.helpAcks.empty())
        {
            tail = 1;
            for (size_t j = 0; j < m.helpAcks.size(); ++j) tail += 2 + 4 + 4 + 1 + 4 * (size_t)m.helpAcks[j].nEx;
        }
        b->resize(at + 1 + 1 + m.key.size() + 4 + 4 + 1 + 1 + 4 * (size_t)m.nMats + tail);
        char* q = &(*b)[at];
        *q++ = (char)m.kind;
        BuildPutS(&q, m.key);
        BuildPutF(&q, m.progress);
        BuildPutF(&q, m.needed);
        *q++ = (char)(m.complete | (m.destroyed != 0 ? kBuildStateDestroyedBit : 0));   /* P15: bit 1 = destroyed */
        *q++ = (char)m.nMats;
        for (unsigned int i = 0; i < m.nMats; ++i) BuildPutF(&q, m.mats[i]);
        if (tail != 0)
        {
            *q++ = (char)(m.helpAcks.size() | kBuildHelpTailLive | kBuildHelpTailWide);   /* the rows carry live and a two-byte slot */
            for (size_t j = 0; j < m.helpAcks.size(); ++j)
            {
                const BuildHelpAck& a = m.helpAcks[j];
                const unsigned int live = (a.live > a.seq) ? a.live : a.seq;   /* help1 fold 3: never below the landed seq */
                *q++ = (char)(a.slot & 0xFF);
                *q++ = (char)((a.slot >> 8) & 0xFF);
                std::memcpy(q, &a.seq, 4); q += 4;
                std::memcpy(q, &live, 4); q += 4;
                *q++ = (char)a.nEx;
                for (unsigned int k = 0; k < a.nEx; ++k) BuildPutF(&q, a.ex[k]);
            }
        }
        return true;
    }
    b->resize(at + 1 + 1 + m.key.size() + 1 + m.sid.size() + 12 + 16 + 1 + 4 + 4 + 1 + m.hostKey.size() + 1 + 4 + 1 + 2 + (m.nonce != 0 ? 4 : 0));   /* + the two owner-number bytes; + the nonce */
    char* p = &(*b)[at];
    *p++ = (char)m.kind;
    BuildPutS(&p, m.key);
    BuildPutS(&p, m.sid);
    for (int i = 0; i < 3; ++i) BuildPutF(&p, m.pos[i]);
    for (int i = 0; i < 4; ++i) BuildPutF(&p, m.rot[i]);
    *p++ = (char)m.complete;
    BuildPutF(&p, m.progress);
    BuildPutF(&p, m.needed);
    BuildPutS(&p, m.hostKey);
    *p++ = (char)m.hostForm;   /* build1-e */
    std::memcpy(p, &m.floor, 4); p += 4;
    *p++ = (char)m.outside;
    *p++ = (char)(m.ownerSlot & 0xFF); *p++ = (char)((m.ownerSlot >> 8) & 0xFF);   /* the owner number, little-endian */
    if (m.nonce != 0) { std::memcpy(p, &m.nonce, 4); p += 4; }   /* house2 fold (protocol 76) */
    return true;
}

/* Every length test is `size - off < n` with off already <= size. */
inline int BuildGetS(const char* p, size_t size, size_t* off, unsigned int cap, std::string* out)
{
    if (size - *off < 1) return kBuildDecodeTooShort;
    const unsigned int n = (unsigned char)p[*off];
    *off += 1;
    if (n > cap) return kBuildDecodeBadKey;
    if (size - *off < (size_t)n) return kBuildDecodeTooShort;
    out->assign(p + *off, n);
    *off += n;
    return kBuildDecodeOk;
}
/* P87 fold 1: a REMOVE's optional tail (nonce, flags, a furniture tombstone's host key). Lenient, as the REMOVE decode always was about
   trailing bytes: a part that is absent or does not parse is left unset, never a refusal. */
inline void BuildRemoveTail(const char* p, size_t size, size_t off, BuildMsg* m)
{
    if (size - off < 4) return;
    unsigned int n = 0;
    std::memcpy(&n, p + off, 4); off += 4;
    m->nonce = n;
    if (size - off < 1) return;
    m->rmFlags = (unsigned char)p[off]; off += 1;
    if ((m->rmFlags & kBuildRmFurniture) == 0) return;
    std::string hk;
    if (BuildGetS(p, size, &off, kBuildMaxKey, &hk) == kBuildDecodeOk) m->hostKey = hk;
}

/* ownerBytes: how many bytes a PLACE's owner number takes - 2 (EncodeBuild, the wire), or 1 (DecodeBuildSaved's older rows) */
inline int DecodeBuildLayout(const char* p, size_t size, BuildMsg* out, int ownerBytes)
{
    if (p == 0 || size < 1) return kBuildDecodeTooShort;
    BuildMsg m;
    m.kind = (unsigned char)p[0];
    if (m.kind != kBuildPlace && m.kind != kBuildState && m.kind != kBuildRemove && m.kind != kBuildRosterRequest && m.kind != kBuildHandAck
        && m.kind != kBuildHelpWork && m.kind != kBuildHelpGone) return kBuildDecodeBadKind;
    if (m.kind == kBuildRosterRequest)   /* build1-f: exactly the kind byte */
    {
        if (size != 1) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    size_t off = 1;
    int r = BuildGetS(p, size, &off, kBuildMaxKey, &m.key);
    if (r != kBuildDecodeOk) return r;
    if (m.kind == kBuildHelpWork)   /* help1: exactly kind | key | seq | dProgress | reset | n | n x dMat; fold 3 (protocol 95): | u32 nonce */
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (size - off < (size_t)(4 + 4 + 1 + 1)) return kBuildDecodeTooShort;
        std::memcpy(&m.seq, p + off, 4); off += 4;
        std::memcpy(&m.progress, p + off, 4); off += 4;
        m.reset = (unsigned char)p[off]; off += 1;
        const unsigned int n = (unsigned char)p[off]; off += 1;
        if (n > kBuildMaxMats) return kBuildDecodeBadValue;
        if (size - off != (size_t)(4 * n + 4)) return (size - off < (size_t)(4 * n + 4)) ? kBuildDecodeTooShort : kBuildDecodeBadValue;   /* help1 fold 3: + the nonce */
        m.nMats = (unsigned char)n;
        for (unsigned int i = 0; i < n; ++i) { std::memcpy(&m.mats[i], p + off, 4); off += 4; }
        std::memcpy(&m.nonce, p + off, 4); off += 4;   /* help1 fold 3: the placement the work was done on */
        if (m.seq == 0 || m.reset > 1 || !BuildHelpAmountOk(m.progress)) return kBuildDecodeBadValue;
        for (unsigned int i = 0; i < n; ++i) if (!BuildHelpAmountOk(m.mats[i])) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    if (m.kind == kBuildHelpGone)   /* help1 fold (protocol 91): kind | key | u32 seq (> 0); fold 2 (protocol 92): | u32 applied (any);
                                       fold 3 (protocol 95): | u32 nonce | u8 n | exactly n x f32 refused (finite, >= 0) */
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (size - off < 13) return kBuildDecodeTooShort;
        std::memcpy(&m.seq, p + off, 4); off += 4;
        std::memcpy(&m.applied, p + off, 4); off += 4;
        std::memcpy(&m.nonce, p + off, 4); off += 4;
        const unsigned int n = (unsigned char)p[off]; off += 1;
        if (n > kBuildMaxMats) return kBuildDecodeBadValue;
        if (size - off != (size_t)(4 * n)) return (size - off < (size_t)(4 * n)) ? kBuildDecodeTooShort : kBuildDecodeBadValue;
        m.nMats = (unsigned char)n;
        for (unsigned int i = 0; i < n; ++i) { std::memcpy(&m.mats[i], p + off, 4); off += 4; if (!BuildFloatOk(m.mats[i]) || m.mats[i] < 0.0f) return kBuildDecodeBadValue; }
        if (m.seq == 0) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    if (m.kind == kBuildHandAck)   /* house1b: exactly kind + key */
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (off != size) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    if (m.kind == kBuildRemove)   /* build1-d */
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (size - off < 1) return kBuildDecodeTooShort;
        m.reason = (unsigned char)p[off]; off += 1;
        if (m.reason != kBuildReasonDismantled && m.reason != kBuildReasonDestroyed) return kBuildDecodeBadValue;
        BuildRemoveTail(p, size, off, &m);   /* P87 fold 1: the optional tail - anything else after the reason is ignored, as before */
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    if (m.kind == kBuildState)
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (size - off < (size_t)(4 + 4 + 1 + 1)) return kBuildDecodeTooShort;
        std::memcpy(&m.progress, p + off, 4); off += 4;
        std::memcpy(&m.needed, p + off, 4); off += 4;
        const unsigned int cb = (unsigned char)p[off]; off += 1;   /* P15: bit 0 complete, bit 1 destroyed; any other bit is refused */
        if ((cb & ~(1u | (unsigned int)kBuildStateDestroyedBit)) != 0) return kBuildDecodeBadValue;
        m.complete = (unsigned char)(cb & 1u);
        m.destroyed = (unsigned char)((cb & kBuildStateDestroyedBit) != 0 ? 1 : 0);
        const unsigned int n = (unsigned char)p[off]; off += 1;
        if (n > kBuildMaxMats) return kBuildDecodeBadValue;
        if (size - off < (size_t)(4 * n)) return kBuildDecodeTooShort;
        m.nMats = (unsigned char)n;
        for (unsigned int i = 0; i < n; ++i) { std::memcpy(&m.mats[i], p + off, 4); off += 4; }
        if (size - off > 0)   /* help1 (protocol 90): the confirmation tail - written only when it has a row, then nothing follows it */
        {
            const unsigned int h0 = (unsigned char)p[off]; off += 1;
            const int withLive = (h0 & kBuildHelpTailLive) != 0 ? 1 : 0;   /* help1 fold 3 (protocol 95): each row u32 live after seq; without the flag (a pp.build record written before) live = seq */
            const int wide = (h0 & kBuildHelpTailWide) != 0 ? 1 : 0;   /* a two-byte helper slot; without the flag (a pp.build record written before) one byte */
            const unsigned int h = h0 & 0x3Fu;
            if (h == 0 || h > kBuildMaxHelpers) return kBuildDecodeBadValue;
            for (unsigned int j = 0; j < h; ++j)
            {
                if (size - off < (size_t)((wide != 0 ? 2 : 1) + 4 + (withLive != 0 ? 4 : 0) + 1)) return kBuildDecodeTooShort;
                BuildHelpAck a;
                if (wide != 0) { a.slot = (unsigned short)((unsigned char)p[off] | ((unsigned int)(unsigned char)p[off + 1] << 8)); off += 2; }
                else { a.slot = (unsigned char)p[off]; off += 1; }
                std::memcpy(&a.seq, p + off, 4); off += 4;
                if (withLive != 0) { std::memcpy(&a.live, p + off, 4); off += 4; if (a.live < a.seq) return kBuildDecodeBadValue; }   /* help1 fold 3 */
                else a.live = a.seq;
                const unsigned int ne = (unsigned char)p[off]; off += 1;
                if (ne > kBuildMaxMats) return kBuildDecodeBadValue;
                if (size - off < (size_t)(4 * ne)) return kBuildDecodeTooShort;
                a.nEx = (unsigned char)ne;
                for (unsigned int k = 0; k < ne; ++k) { std::memcpy(&a.ex[k], p + off, 4); off += 4; }
                m.helpAcks.push_back(a);
            }
            if (off != size || !BuildHelpAcksOk(m.helpAcks)) return kBuildDecodeBadValue;
        }
        if (m.complete > 1) return kBuildDecodeBadValue;
        if (!BuildFloatOk(m.progress) || !BuildFloatOk(m.needed)) return kBuildDecodeBadValue;
        for (unsigned int i = 0; i < n; ++i) if (!BuildFloatOk(m.mats[i])) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    r = BuildGetS(p, size, &off, kBuildMaxSid, &m.sid);
    if (r != kBuildDecodeOk) return r;
    if (m.key.empty() || m.sid.empty()) return kBuildDecodeBadKey;
    if (size - off < (size_t)(12 + 16 + 1 + 4 + 4)) return kBuildDecodeTooShort;
    for (int i = 0; i < 3; ++i) { std::memcpy(&m.pos[i], p + off, 4); off += 4; }
    for (int i = 0; i < 4; ++i) { std::memcpy(&m.rot[i], p + off, 4); off += 4; }
    m.complete = (unsigned char)p[off]; off += 1;
    std::memcpy(&m.progress, p + off, 4); off += 4;
    std::memcpy(&m.needed, p + off, 4); off += 4;
    r = BuildGetS(p, size, &off, kBuildMaxKey, &m.hostKey);
    if (r != kBuildDecodeOk) return r;
    if (size - off < (size_t)(1 + 4 + 1 + 1)) return kBuildDecodeTooShort;   /* host form, floor, outside, the owner number */
    m.hostForm = (unsigned char)p[off]; off += 1;
    std::memcpy(&m.floor, p + off, 4); off += 4;
    m.outside = (unsigned char)p[off]; off += 1;
    if (ownerBytes == 2)
    {
        if (size - off < 2) return kBuildDecodeTooShort;
        m.ownerSlot = (unsigned short)((unsigned char)p[off] | ((unsigned short)(unsigned char)p[off + 1] << 8)); off += 2;   /* little-endian */
        if (m.ownerSlot != kBuildOwnerSender && m.ownerSlot > kBuildOwnerSlotMax) return kBuildDecodeBadValue;
    }
    else
    {
        const unsigned char o = (unsigned char)p[off]; off += 1;   /* the one-byte owner number: 0xFF the sender, else the slot */
        m.ownerSlot = (o == kBuildOwnerSenderOneByte) ? kBuildOwnerSender : (unsigned short)o;
    }
    if (size - off >= 4)   /* the per-placement nonce; absent = 0 */
    {
        std::memcpy(&m.nonce, p + off, 4); off += 4;
        if (m.nonce == 0) return kBuildDecodeBadValue;   /* one encoding: a zero nonce is never written */
        if (off != size) return kBuildDecodeBadValue;     /* nothing follows the nonce */
    }
    else if (size - off != 0) return kBuildDecodeTooShort;
    if (m.hostForm > kBuildHostIndoorsOnly || (m.hostKey.empty() && m.hostForm != 0) || m.outside > 1
        || m.floor < -kBuildMaxFloor || m.floor > kBuildMaxFloor)
        return kBuildDecodeBadValue;
    if (m.complete > 1) return kBuildDecodeBadValue;
    for (int i = 0; i < 3; ++i) if (!BuildFloatOk(m.pos[i])) return kBuildDecodeBadValue;
    if (!BuildRotOk(m.rot)) return kBuildDecodeBadValue;
    if (!BuildFloatOk(m.progress) || !BuildFloatOk(m.needed)) return kBuildDecodeBadValue;
    if (out) *out = m;
    return kBuildDecodeOk;
}
/* a message as it arrives on the wire (and as EncodeBuild writes it): a PLACE's owner number is two bytes */
inline int DecodeBuild(const char* p, size_t size, BuildMsg* out) { return DecodeBuildLayout(p, size, out, 2); }
/* a message as this game saved it (pp.build rows, the owed file): EncodeBuild's layout, else - for a PLACE - the layout with a
   one-byte owner number (0xFF = the sender) that rows saved by earlier builds carry */
inline int DecodeBuildSaved(const char* p, size_t size, BuildMsg* out)
{
    const int r = DecodeBuildLayout(p, size, out, 2);
    if (r == kBuildDecodeOk || p == 0 || size < 1 || (unsigned char)p[0] != kBuildPlace) return r;
    return DecodeBuildLayout(p, size, out, 1) == kBuildDecodeOk ? kBuildDecodeOk : r;
}

/* house2: the order gate (0x7F9280) and the Shift-job gate (0x7F4EF0) share one rule - on a piece this game handed to another
   player's house owner a build, add-materials, repair or dismantle task is refused; every other task (use, per base access) passes */
const int kBuildTaskBuild = 2, kBuildTaskAddMaterials = 71, kBuildTaskRepair = 95, kBuildTaskDismantle = 96;   /* TaskType values */
inline bool BuildTaskGated(int task)
{
    return task == kBuildTaskBuild || task == kBuildTaskAddMaterials || task == kBuildTaskRepair || task == kBuildTaskDismantle;
}
inline bool BuildTaskRefused(int task, int handedAway) { return BuildTaskGated(task) && handedAway != 0; }

/* P15: what a copy does with the owner's destroyed state (a STATE's destroyed bit) against its own +0x1A1. A STATE made from a PLACE
   carries no destroyed state and changes nothing. */
const int kBuildDestroyKeep = 0;      /* nothing to change */
const int kBuildDestroyApply = 1;     /* the owner's piece is a ruin, the copy is whole: the engine's setDestroyed(1) on the copy */
const int kBuildDestroyRepair = 2;    /* the owner's piece is whole again, the copy is a ruin: the engine's repair road on the copy */
const int kBuildDestroyAlready = 3;   /* both ruins: materials only */
inline int BuildDestroyedAction(int fromPlace, int msgDestroyed, int liveDestroyed)
{
    if (fromPlace != 0) return kBuildDestroyKeep;
    if (msgDestroyed != 0) return liveDestroyed != 0 ? kBuildDestroyAlready : kBuildDestroyApply;
    return liveDestroyed != 0 ? kBuildDestroyRepair : kBuildDestroyKeep;
}
/* P15: a repair order (TaskType 95) on a RUIN copy (a stand-in owns it) is help - the work reaches the owner's game as HELP_WORK with
   the ruin reset; on a wall ruin copy it is refused (wall work is not synced, T-221). Any other order is not this rule's. */
const int kBuildRepairOrderNone = 0, kBuildRepairOrderHelp = 1, kBuildRepairOrderRefused = 2;
inline int BuildRepairOrderVerdict(int task, int standInOwned, int destroyed, int wall)
{
    if (task != kBuildTaskRepair || standInOwned == 0 || destroyed == 0) return kBuildRepairOrderNone;
    return wall != 0 ? kBuildRepairOrderRefused : kBuildRepairOrderHelp;
}
/* P15: a copy made a ruin HERE by this game's own engine (not the mod's apply). The owner's game is the writer: its last applied STATE
   goes back on when that says whole; with none applied yet the copy waits for the owner's next STATE. */
const int kBuildLocalDestroyIgnore = 0, kBuildLocalDestroyRevert = 1, kBuildLocalDestroyWait = 2;
inline int BuildLocalDestroyVerdict(int isCopy, int modsOwn, int destroyedNow, int ownerKnown, int ownerDestroyed)
{
    if (isCopy == 0 || modsOwn != 0 || destroyedNow == 0) return kBuildLocalDestroyIgnore;
    if (ownerKnown == 0) return kBuildLocalDestroyWait;
    return ownerDestroyed != 0 ? kBuildLocalDestroyIgnore : kBuildLocalDestroyRevert;
}
/* house2: orderKept - the placement commit issues ONE build order (4d6810:452), so an ungated commit that handed any piece counts once */
inline int BuildCommitOrderKept(int handedInCommit, int gateInstalled) { return (handedInCommit > 0 && gateInstalled == 0) ? 1 : 0; }
/* house2 + fold (review-house2 MED2): a hand-over PLACE (an owner slot named) is LATE - re-sent before the REMOVE reached the placer -
   when the very placement it names was removed here: its (key, nonce) is on the removed list (exactListed). A new placement at the same
   spot draws a new nonce and is accepted. A PLACE with no nonce (an old sender, an old owed-file row) keeps the key-only rule
   (keyListed = the key is on the list with any nonce). An ordinary PLACE after a REMOVE stays a new piece at the same spot (build1-d). */
inline bool BuildLatePlaceRefused(unsigned short ownerSlot, unsigned int nonce, int exactListed, int keyListed)
{
    if (ownerSlot == kBuildOwnerSender) return false;
    return (nonce != 0) ? (exactListed != 0) : (keyListed != 0);
}
/* house2 fold (review-house2 MED1): an owed-file read that finally succeeds MERGES the file's rows into the map - a row recorded in
   memory meanwhile wins over the file's row for the same key. fold 2 (recheck-house2): a key whose row a REMOVE ended while the file
   was unread (`erased`, may be 0) is never merged back. 1 = the file row was taken, 0 = the in-memory row kept, 2 = dropped (erased). */
template <class Row>
inline int BuildOwedMergeRow(std::map<std::string, Row>* mem, const std::set<std::string>* erased, const std::string& key, const Row& fileRow)
{
    if (erased != 0 && erased->find(key) != erased->end()) return 2;
    if (mem->find(key) != mem->end()) return 0;
    (*mem)[key] = fileRow;
    return 1;
}
/* house2 fold 2 (recheck-house2): a row the load scan rebuilt has no nonce (0); a hand-over PLACE re-sent for it carrying a nonce gives
   the row that nonce, so a later removal lists the real (key, nonce). A row that has a nonce keeps it. */
inline unsigned int BuildAdoptNonce(unsigned int rowNonce, unsigned short ownerSlot, unsigned int placeNonce)
{
    return (rowNonce == 0 && ownerSlot != kBuildOwnerSender && placeNonce != 0) ? placeNonce : rowNonce;
}

/* par13 (parity P13, parity-audit-buildings-world.md H2): the owed-REMOVE list. An own piece dismantled while the link is down (its
   REMOVE send failed) is owed that REMOVE; build.cpp's BdRosterTick sends the list once the link is up, ahead of any PLACE, and a
   world load forgets it (the dismantle belongs to the world it was made in). Add: once per key - 1 = added, 0 = already owed. */
inline int BuildOweRemoveAdd(std::vector<std::string>* owed, const std::string& key)
{
    for (size_t i = 0; i < owed->size(); ++i) if ((*owed)[i] == key) return 0;
    owed->push_back(key);
    return 1;
}
/* par13: a key placed again is no longer owed a REMOVE. 1 = it was owed */
inline int BuildOweRemoveDrop(std::vector<std::string>* owed, const std::string& key)
{
    int was = 0;
    for (size_t i = 0; i < owed->size(); )
    {
        if ((*owed)[i] == key) { owed->erase(owed->begin() + (std::vector<std::string>::difference_type)i); was = 1; }
        else ++i;
    }
    return was;
}
/* par13: send from the front, at most cap, stopping at the first failed send (the rest wait, in order, for the next tick). A sent
   entry leaves the list, so each owed REMOVE goes once. send(key) is true when the REMOVE went. Returns how many went. */
template <class Send>
inline size_t BuildOweRemoveFlush(std::vector<std::string>* owed, size_t cap, Send& send)
{
    size_t k = 0;
    while (k < owed->size() && k < cap && send((*owed)[k])) ++k;
    owed->erase(owed->begin(), owed->begin() + (std::vector<std::string>::difference_type)k);
    return k;
}
/* par13: a world load forgets the list. Returns how many were dropped */
inline size_t BuildOweRemoveForget(std::vector<std::string>* owed)
{
    const size_t n = owed->size();
    owed->clear();
    return n;
}

/* ---- help1 (owner 172, p51-help1-design.md): the pure decisions (suite-tested) ------------------------------------------------
   The helper works on its copy; its game sends the work (HELP_WORK) and the owner's game applies it to the real piece. */
const int kBuildHelpApply = 0, kBuildHelpDup = 1, kBuildHelpHold = 2;
/* the owner: a seq at or below the last one applied from that slot for that piece is a duplicate (a resend) - dropped, its confirmation
   goes again. help1 fold (review HIGH 1): ONLY the next one (last + 1) is applied; a higher one is HELD until the gap fills (the helper
   re-sends every unconfirmed entry) - never applied out of order, so an earlier entry is never confirmed away unapplied */
inline int BuildHelpSeqVerdict(unsigned int lastApplied, unsigned int seq)
{
    if (seq == 0 || seq <= lastApplied) return kBuildHelpDup;
    return (seq == lastApplied + 1) ? kBuildHelpApply : kBuildHelpHold;
}
/* help1 fold (review MED 3): the owner's piece for a HELP_WORK key. A row that is removed, dismantled, not own or a copy here - or no row
   but the key is on the list of own rows swept after their removal (goneListed) - is GONE: answered HELP_GONE (the helper drops the
   work and hands its materials back). No row and not listed is UNKNOWN (not yet seen this session): dropped unconfirmed, sent again.
   help1 fold 3: rowExists = a row of the work's OWN placement (same nonce); goneListed = the key has a row of another placement, a gone
   record or a swept mark here */
const int kBuildHelpRowOk = 0, kBuildHelpRowGone = 1, kBuildHelpRowUnknown = 2;
inline int BuildHelpRowVerdict(int rowExists, int own, int copyHere, int removed, int dismantled, int goneListed)
{
    if (rowExists == 0) return goneListed != 0 ? kBuildHelpRowGone : kBuildHelpRowUnknown;
    if (removed != 0 || dismantled != 0 || own == 0 || copyHere != 0) return kBuildHelpRowGone;
    return kBuildHelpRowOk;
}
/* help1 fold 4 (re-check #1): the owner's own row after a restart - the load scan registers the save's piece with nonce 0; its placement
   nonce comes back from the pp.build PLACE record of that key (whichever of the two runs first). A row that has a nonce keeps it; a copy
   never takes one from here; no record (or a record with none) gives none. */
inline unsigned int BuildOwnNonceRestore(unsigned int rowNonce, int own, int copyHere, int placeKnown, unsigned int placeNonce)
{
    return (rowNonce == 0 && own != 0 && copyHere == 0 && placeKnown != 0) ? placeNonce : rowNonce;
}
/* help1 fold 5 (T-268): a pp.build restore found its piece standing (the loaded area brought it in) and owned by this game. An own row
   takes its PLACE's nonce; NO row (the load scan ran before the area loaded) - the live piece is registered as this game's own, as the
   scan does; a copy / not-own row is left alone; another owner's piece does nothing here (refused). */
const int kBuildRsPresentNothing = 0;
const int kBuildRsPresentNonce = 1;
const int kBuildRsPresentRegister = 2;
inline int BuildRestorePresentAct(int mineLive, int rowExists, int own, int copyHere)
{
    if (mineLive == 0) return kBuildRsPresentNothing;
    if (rowExists == 0) return kBuildRsPresentRegister;
    return (own != 0 && copyHere == 0) ? kBuildRsPresentNonce : kBuildRsPresentNothing;
}
/* T-274 (t274-zone-scan.md S3): the per-zone scan's table - one byte per map sector (index sx * 64 + sy, 64 x 64), 1 = that sector's
   zone was walked in this world while it stayed active. The plugin passes the active sectors (ActiveZoneSectors) and each one's engine
   "loaded" byte (ZoneMap+0xB1: 1 loaded, 0 not yet, -1 the read was refused). A poll of the engine's own flag each tick, no timer. */
const int kBuildZoneSectorSide = 64;
const int kBuildZoneSectorCells = 64 * 64;
inline int BuildZoneSectorIndex(int sx, int sy)   /* -1 off the map */
{
    return (sx < 0 || sx >= kBuildZoneSectorSide || sy < 0 || sy >= kBuildZoneSectorSide) ? -1 : sx * kBuildZoneSectorSide + sy;
}
/* a sector no longer active loses its mark, so a re-arrival is walked again. `scratch` is kBuildZoneSectorCells bytes (overwritten).
   Returns the count cleared.
   T-274 fold (LOW-2): `moves` (kBuildZoneSectorCells bytes) is each sector's count of moved walks in a row; a sector that left
   loses it too. */
inline int BuildZoneScanClearGone(unsigned char* marks, unsigned char* moves, unsigned char* scratch, const int* sx, const int* sy, int n)
{
    for (int c = 0; c < kBuildZoneSectorCells; ++c) scratch[c] = 0;
    for (int i = 0; i < n; ++i) { const int c = BuildZoneSectorIndex(sx[i], sy[i]); if (c >= 0) scratch[c] = 1; }
    int cleared = 0;
    for (int c = 0; c < kBuildZoneSectorCells; ++c)
    {
        if (scratch[c] != 0) continue;
        moves[c] = 0;
        if (marks[c] != 0) { marks[c] = 0; ++cleared; }
    }
    return cleared;
}
/* the ONE zone walked this tick: an active sector that is unmarked and whose loaded byte reads 1 (0 = still loading, -1 = refused:
   neither is walked, both are asked again next tick). T-274 fold (LOW-2): round-robin - the search starts at the entry after the
   sector picked last (`lastCell`, its BuildZoneSectorIndex; -1 or no longer active = from the front) and wraps, so a zone whose
   walk keeps moving does not hold every later zone back. Returns its index in the active list, -1 = none. */
inline int BuildZoneScanPick(const unsigned char* marks, const int* sx, const int* sy, const int* loaded, int n, int lastCell)
{
    int start = 0;
    for (int i = 0; i < n && lastCell >= 0; ++i)
        if (BuildZoneSectorIndex(sx[i], sy[i]) == lastCell) { start = i + 1; break; }
    for (int j = 0; j < n; ++j)
    {
        const int i = (start + j) % n;
        const int c = BuildZoneSectorIndex(sx[i], sy[i]);
        if (c >= 0 && marks[c] == 0 && loaded[i] == 1) return i;
    }
    return -1;
}
/* T-274 fold (LOW-2): a zone whose walk moved this many times in a row is marked anyway (given up) */
const int kBuildZoneScanGiveUp = 8;
/* after the walk: marked, unless the zone list moved under it (BoxWalkGen) - then it stays unmarked and is picked again on its turn.
   T-274 fold (LOW-2): the kBuildZoneScanGiveUp-th moved walk in a row marks it anyway. Returns 1 = given up (marked after moved
   walks), 0 = otherwise (marked by a whole walk, left unmarked, or off the map). */
inline int BuildZoneScanDone(unsigned char* marks, unsigned char* moves, int sx, int sy, int walkMoved)
{
    const int c = BuildZoneSectorIndex(sx, sy);
    if (c < 0) return 0;
    if (walkMoved == 0) { marks[c] = 1; moves[c] = 0; return 0; }
    if (moves[c] < 255) ++moves[c];
    if (moves[c] < kBuildZoneScanGiveUp) return 0;
    marks[c] = 1; moves[c] = 0;
    return 1;
}
/* help1 fold 5 (T-269): an own piece owes one real STATE once its PLACE went out - the other game's copy is made from the PLACE, and
   help on it waits for a real STATE (its tail). 1 = it goes now (a copy, a not-own row or a piece whose PLACE did not go never) */
inline int BuildOwnStateAnnounce(int own, int copyHere, int sentOk, int owed)
{
    return (own != 0 && copyHere == 0 && sentOk != 0 && owed != 0) ? 1 : 0;
}
/* help1 fold 4: work carrying a placement nonce for an own row whose nonce is not known yet (0) is HELD - re-judged at the next safe
   point, never answered GONE against a nonce the owner cannot compare. 1 = hold */
inline int BuildHelpNonceWait(int rowExists, int own, int copyHere, unsigned int rowNonce, unsigned int workNonce)
{
    return (rowExists != 0 && own != 0 && copyHere == 0 && rowNonce == 0 && workNonce != 0) ? 1 : 0;
}
/* help1 fold 4: the confirmation rows pp.build brought back (with their PLACE's nonce) - READ only for their own placement; WRITTEN back
   into pp.build also while the row's nonce is still 0 (not learned yet), so they are never erased before it is */
inline int BuildAckRestoreFor(unsigned int rowNonce, unsigned int restoreNonce, int forWrite)
{
    return (rowNonce == restoreNonce || (forWrite != 0 && rowNonce == 0)) ? 1 : 0;
}
/* help1 fold 4 (re-check #2, #3): how the owner answers work for a placement that is not (or no longer) its live own piece.
   record: 0 no (key, nonce) gone record, 1 its record has landed, 2 not on disk yet. samePlRow: this placement's own row is still here;
   rowAcks: it holds helper rows; rowSettled: its landed rows equal its live ones and no write is pending (nothing applied past what
   pp.build holds); rowWriting: a pp.build write holding them is on its way.
   ANSWER = HELP_GONE from the record (or, no record kept at the cap, from the row's landed rows - only when settled, never from landed
   alone); WAIT = kept, retried; DROP = not answered, not confirmed - the helper keeps it owed and sends it again (counted);
   ZERO = GONE applied 0, only where it is proven: this placement's own row is here (its nonce known) and holds no helper row - nothing
   from any helper was applied to it. A key with no record for the nonce (another placement's row, a swept mark) is never ZERO. */
const int kBuildGoneAnswer = 0, kBuildGoneWait = 1, kBuildGoneDrop = 2, kBuildGoneZero = 3;
inline int BuildHelpGoneVerdict(int record, int samePlRow, int rowAcks, int rowSettled, int rowWriting)
{
    if (record == 1) return kBuildGoneAnswer;
    if (record == 2) return kBuildGoneWait;
    if (samePlRow == 0) return kBuildGoneDrop;
    if (rowAcks == 0) return kBuildGoneZero;
    if (rowSettled != 0) return kBuildGoneAnswer;
    return (rowWriting != 0) ? kBuildGoneWait : kBuildGoneDrop;
}
/* help1 fold 4 (re-check #5): a piece registered over a key a COPY row holds stays that copy (build.cpp BdOnNewPiece) - it keeps the
   copy's nonce (the owner's placement, which this game's help rows and entries are bound to; 0 stays 0 so the owner's PLACE can still
   teach it, BuildAdoptNonce); only a piece that is not a copy draws a fresh one */
inline unsigned int BuildNewPieceNonce(int wasCopy, unsigned int rowNonce, unsigned int fresh) { return (wasCopy != 0) ? rowNonce : fresh; }
/* help1 fold 4 (leftover 3): a replaced placement's helper row moves to the replaced rows when it still owes work OR a refund of that
   placement is still due (the inventory refused it) - then one empty entry goes with it, so the owner's HELP_GONE for that placement
   (which carries its refused tail) comes back. 1 = keep */
inline int BuildHelpOldKeep(size_t owed, int refundDue) { return (owed != 0 || refundDue != 0) ? 1 : 0; }
/* help1 fold (review MED 5/6): the helper's next seq = the highest of its own, its records' and the owner's last applied + 1 (the
   owner's STATE tail, read before the first entry of a session) - a fresh row never re-uses a seq the owner applied */
inline unsigned int BuildHelpStartSeq(unsigned int local, unsigned int recorded, unsigned int lastApplied)
{
    unsigned int s = (local > recorded) ? local : recorded;
    if (lastApplied + 1 > s) s = lastApplied + 1;
    return s == 0 ? 1 : s;
}
/* help1 fold (review LOW 13): the frequent help lines (work sent / owed, copy capped, predicted) - the first 10, then every 30th */
inline bool BuildHelpFreqLine(long long n) { return n >= 1 && (n <= 10 || n % 30 == 0); }
/* the owner: a material delta onto the piece, capped at the material's total (total < 0 = unread: no cap). *excess = the part that did
   not fit - refunded to the helper, never lost */
inline float BuildHelpMatApply(float cur, float d, float total, float* excess)
{
    if (cur < 0.0f) cur = 0.0f;
    if (d < 0.0f) d = 0.0f;
    *excess = 0.0f;
    if (total < 0.0f) return cur + d;
    float room = total - cur;
    if (room < 0.0f) room = 0.0f;
    const float taken = (d < room) ? d : room;
    *excess = d - taken;
    return cur + taken;
}
/* the owner: the work completes the piece (the engine's own vt+0x230(FLT_MAX), never a progress write at needed) */
inline int BuildHelpCompletes(float progress, float dProgress, float needed) { return (needed > 0.0f && progress + dProgress >= needed) ? 1 : 0; }
/* the helper: its copy never completes by its own work - capped at 0.999 x needed; never on a ruin (the reset reads needed) */
inline float BuildHelpCopyCap(float progress, float needed, int ruin, int* capped)
{
    *capped = 0;
    if (ruin != 0 || !(needed > 0.0f)) return progress;
    const float cap = needed * 0.999f;
    if (progress > cap) { *capped = 1; return cap; }
    return progress;
}
/* the helper: whole items owed back = the owner's cumulative refused amount (whole units) less those already returned */
inline int BuildHelpRefundOwed(float excessTotal, int refunded)
{
    if (!(excessTotal > 0.0f) || excessTotal > kBuildHelpMax * 16.0f) return 0;
    const int whole = (int)(excessTotal + 0.001f);
    return whole > refunded ? whole - refunded : 0;
}
/* the helper resumes its sequence past the one the owner confirmed (a lost owed file never re-uses a confirmed seq) */
inline unsigned int BuildHelpNextSeq(unsigned int next, unsigned int confirmed) { return (confirmed >= next) ? confirmed + 1 : next; }
/* the helper's owed work: one entry per HELP_WORK, kept until the owner's STATE confirms its seq */
struct BuildHelpEntry
{
    unsigned int seq;
    float dP;
    unsigned char reset, n;
    int sent;               /* the build's own: went on the link (not kept in the file - every entry is re-sent at link-up) */
    unsigned int sentAt;    /* the build's own: GetTickCount of the send (an unconfirmed entry is re-sent after a while) */
    int loaded;             /* help1 fold 2 (MED 4): the build's own - read from the owed file (it may have gone before the restart): FROZEN, never joined */
    unsigned int nonce;     /* help1 fold 3 (finding 2): the owner's placement the work was done on (the copy's PLACE nonce; on the wire and in the owed file) */
    int applied;            /* help1 fold 3 (finding 1): the build's own - the owner's STATE says it applied this seq (live) but its record has not landed:
                               kept, never renumbered, not re-sent */
    float d[kBuildMaxMats];
    BuildHelpEntry() : seq(0), dP(0.0f), reset(0), n(0), sent(0), sentAt(0), loaded(0), nonce(0), applied(0) { for (unsigned int i = 0; i < kBuildMaxMats; ++i) d[i] = 0.0f; }
};
/* the confirmation of seq drops every entry at or below it. Returns how many went */
inline size_t BuildHelpConfirm(std::vector<BuildHelpEntry>* owed, unsigned int seq)
{
    size_t gone = 0;
    for (size_t i = 0; i < owed->size(); )
    {
        if ((*owed)[i].seq <= seq) { owed->erase(owed->begin() + (std::vector<BuildHelpEntry>::difference_type)i); ++gone; }
        else ++i;
    }
    return gone;
}
/* help1 fold (review MED 3): HELP_GONE(seq) - the helper's own delivered amounts of every entry at or below seq ADD into back[] (the
   materials it hands back; *n = the widest). Returns how many entries that is (BuildHelpConfirm drops them once handed back).
   help1 fold 2 (re-check HIGH 2): only entries ABOVE the owner's last applied (`applied`, carried by the GONE) - those at or below it
   were applied to the piece before it went and are dropped as applied, never handed back */
inline size_t BuildHelpGoneSum(const std::vector<BuildHelpEntry>& owed, unsigned int seq, unsigned int applied, float* back, int* n)
{
    size_t k = 0;
    for (size_t i = 0; i < owed.size(); ++i)
    {
        if (owed[i].seq > seq || owed[i].seq <= applied) continue;
        ++k;
        if ((int)owed[i].n > *n) *n = (int)owed[i].n;
        for (unsigned int j = 0; j < owed[i].n && j < kBuildMaxMats; ++j) back[j] += owed[i].d[j];
    }
    return k;
}
/* help1 fold 2 (HIGH 2): the entries a HELP_GONE settles - every one at or below the higher of its seq and the owner's last applied */
inline unsigned int BuildHelpGoneSettled(unsigned int seq, unsigned int applied) { return (applied > seq) ? applied : seq; }
/* help1 fold 4 (leftover 6): a HELP_GONE refund the inventory refused is not lost - the whole units not given become one more entry of
   that placement (a seq above every one sent, so above the owner's applied), sent again, answered GONE again and handed back then: the
   STATE path's "the next one tries again". given[i]: 1 given, 0 refused (carried), < 0 the engine's add faulted (never retried).
   1 = an entry was made */
inline int BuildHelpGoneCarry(const int* whole, const int* given, unsigned int seq, unsigned int nonce, BuildHelpEntry* e)
{
    BuildHelpEntry c;
    c.seq = seq; c.nonce = nonce;
    int any = 0;
    for (int i = 0; i < (int)kBuildMaxMats; ++i)
    {
        if (whole[i] <= 0 || given[i] != 0) continue;
        c.d[i] = ((float)whole[i] > kBuildHelpMax) ? kBuildHelpMax : (float)whole[i];
        c.n = (unsigned char)(i + 1);
        any = 1;
    }
    if (any != 0) *e = c;
    return any;
}
inline bool BuildHelpEntrySeqLess(const BuildHelpEntry& a, const BuildHelpEntry& b) { return a.seq < b.seq; }
/* help1 fold 2 (re-check HIGH 1): the owner applies strictly in order, so nothing above its last applied was applied; a gap no entry
   fills (pp.help's next landed ahead of the owed file before a crash) would be held forever - those entries are RENUMBERED (content
   unchanged, seq order kept). The owner drops what it held under the old numbers (BuildHelpSuperseded).
   help1 fold 3 (finding 1): the STATE's tail carries BOTH the landed last applied (on the owner's disk) and the live one (>= landed).
   Entries at or below landed are confirmed (dropped); those in (landed, live] WERE applied - kept (their landed confirmation follows),
   marked applied (never re-sent), never renumbered; entries above live are unmarked, and if the lowest of them is not live + 1 they are
   renumbered live+1, live+2, ... and *next follows them (it never falls to or below live). With no entry above live *next = the higher
   of itself and live + 1 - never lowered. Returns how many entries took a new seq (0 = no gap). */
inline size_t BuildHelpResync(std::vector<BuildHelpEntry>* owed, unsigned int* next, unsigned int landed, unsigned int live)
{
    if (live < landed) live = landed;
    BuildHelpConfirm(owed, landed);
    std::sort(owed->begin(), owed->end(), BuildHelpEntrySeqLess);
    size_t first = owed->size();
    for (size_t i = 0; i < owed->size(); ++i)
    {
        (*owed)[i].applied = ((*owed)[i].seq <= live) ? 1 : 0;
        if (first == owed->size() && (*owed)[i].seq > live) first = i;
    }
    if (first == owed->size()) { if (*next < live + 1) *next = live + 1; return 0; }
    if ((*owed)[first].seq == live + 1)
    {
        const unsigned int top = owed->back().seq + 1;
        if (*next < top) *next = top;
        return 0;
    }
    size_t k = 0;
    for (size_t i = first; i < owed->size(); ++i)
    {
        const unsigned int s = live + 1 + (unsigned int)(i - first);
        if ((*owed)[i].seq != s) { (*owed)[i].seq = s; ++k; }
    }
    *next = live + 1 + (unsigned int)(owed->size() - first);
    return k;
}
/* help1 fold 2 (re-check MED 4): new work joins only an entry never on the link and not read back from the owed file */
inline bool BuildHelpJoinable(const BuildHelpEntry& e) { return e.sent == 0 && e.loaded == 0; }
/* help1 fold 2 (HIGH 1), the owner: an entry queued EARLIER (held) from the same helper for the same key whose seq is at or above a
   later arrival's is superseded - the helper renumbered or re-sent from lower; it re-sends whatever it still owes */
inline bool BuildHelpSuperseded(unsigned int earlierSeq, unsigned int laterSeq) { return earlierSeq >= laterSeq; }
/* help1 fold 2 (HIGH 1): an entry that would not encode is made encodable, never erased (its seq would leave a gap): a non-finite or
   negative amount -> 0, one past kBuildHelpMax -> kBuildHelpMax, n past kBuildMaxMats -> kBuildMaxMats, reset past 1 -> 1. Returns how
   many fields changed */
inline int BuildHelpEntrySanitize(BuildHelpEntry* e)
{
    int k = 0;
    if (e->n > kBuildMaxMats) { e->n = (unsigned char)kBuildMaxMats; ++k; }
    if (e->reset > 1) { e->reset = 1; ++k; }
    if (!BuildFloatOk(e->dP) || e->dP < 0.0f) { e->dP = 0.0f; ++k; }
    else if (e->dP > kBuildHelpMax) { e->dP = kBuildHelpMax; ++k; }
    for (unsigned int i = 0; i < kBuildMaxMats; ++i)
    {
        if (!BuildFloatOk(e->d[i]) || e->d[i] < 0.0f) { e->d[i] = 0.0f; ++k; }
        else if (e->d[i] > kBuildHelpMax) { e->d[i] = kBuildHelpMax; ++k; }
    }
    return k;
}
/* help1 fold 2 (re-check HIGH 1): the helper's row for a key names the placement it helped (the owner's nonce); a PLACE of ANOTHER
   placement at that key (both known, different) makes the row stale. help1 fold 3 (finding 2): its entries stay owed to THAT placement
   (moved aside under its nonce, settled by its HELP_GONE); the key's row starts empty for the new one */
inline bool BuildHelpRowStale(unsigned int rowNonce, unsigned int pieceNonce) { return rowNonce != 0 && pieceNonce != 0 && rowNonce != pieceNonce; }
/* help1 fold 3 (finding 6): pp.help's goneSeq floor (entries at or below it were settled by a HELP_GONE) joins a row only when both
   nonces are known and equal - never an unknown placement's floor onto another's entries */
inline bool BuildHelpFloorApplies(unsigned int rowNonce, unsigned int recNonce) { return rowNonce != 0 && recNonce != 0 && rowNonce == recNonce; }
/* help1 (owner 172) + fold (HIGH 2) + fold 2 (LOW 7): a build / add-materials order on a copy owned by another player's stand-in is help
   (1) - refused on a wall copy (2) on EVERY order road (click, commit, Shift job, on the main thread or off it); anything else 0 */
const int kBuildHelpOrderNone = 0, kBuildHelpOrderGiven = 1, kBuildHelpOrderWallRefused = 2;
inline int BuildHelpOrderVerdict(int task, int standIn, int wall)
{
    if (task != kBuildTaskBuild && task != kBuildTaskAddMaterials) return kBuildHelpOrderNone;
    if (standIn == 0) return kBuildHelpOrderNone;
    return (wall != 0) ? kBuildHelpOrderWallRefused : kBuildHelpOrderGiven;
}
/* the unconfirmed work the helper puts back on its copy after the owner's STATE (no flicker back): *dP and d[] ADD, *n = the widest;
   help1 fold 3: not an entry the owner already applied (live) - the STATE's progress holds it */
inline void BuildHelpSum(const std::vector<BuildHelpEntry>& owed, float* dP, int* n, float* d)
{
    for (size_t i = 0; i < owed.size(); ++i)
    {
        if (owed[i].applied != 0) continue;
        *dP += owed[i].dP;
        if ((int)owed[i].n > *n) *n = (int)owed[i].n;
        for (unsigned int k = 0; k < owed[i].n && k < kBuildMaxMats; ++k) d[k] += owed[i].d[k];
    }
}
/* the owner's confirmation row for a slot; create != 0 adds it (0 when the tail is full) */
inline BuildHelpAck* BuildHelpAckFor(std::vector<BuildHelpAck>* acks, unsigned short slot, int create)
{
    for (size_t i = 0; i < acks->size(); ++i) if ((*acks)[i].slot == slot) return &(*acks)[i];
    if (create == 0 || acks->size() >= kBuildMaxHelpers) return 0;
    BuildHelpAck a;
    a.slot = slot;
    acks->push_back(a);
    return &acks->back();
}

/* help1 fold 3 (finding 4): a HELP_GONE's refused tail - per material the whole items still owed (the cumulative refused amount less
   those already handed back, BuildHelpRefundOwed - the STATE's rule); owe[] gets each (0 past n), returns their total */
inline int BuildHelpGoneRefused(const float* ex, int n, const int* refunded, int* owe)
{
    int t = 0;
    for (int i = 0; i < (int)kBuildMaxMats; ++i)
    {
        owe[i] = (i < n) ? BuildHelpRefundOwed(ex[i], refunded[i]) : 0;
        t += owe[i];
    }
    return t;
}
/* help1 fold 3: the owner's rows changed (a gone record is written again only then) */
inline bool BuildHelpAcksSame(const std::vector<BuildHelpAck>& a, const std::vector<BuildHelpAck>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].slot != b[i].slot || a[i].seq != b[i].seq || a[i].nEx != b[i].nEx) return false;
        for (unsigned int k = 0; k < a[i].nEx && k < kBuildMaxMats; ++k) if (a[i].ex[k] != b[i].ex[k]) return false;
    }
    return true;
}
/* help1 fold 3 (finding 1): a STATE's tail - each landed row (seq = what pp.build holds on disk; its refused amounts) with live = the
   owner's live last applied for that slot (never below seq), then a row for each slot applied live with nothing landed yet (seq 0, no
   amounts - refunds follow only what is on disk). landed / live may be 0 (none) */
inline void BuildHelpTailOf(const std::vector<BuildHelpAck>* landed, const std::vector<BuildHelpAck>* live, std::vector<BuildHelpAck>* out)
{
    out->clear();
    for (size_t i = 0; landed != 0 && i < landed->size(); ++i)
    {
        BuildHelpAck t = (*landed)[i];
        t.live = t.seq;
        for (size_t j = 0; live != 0 && j < live->size(); ++j) if ((*live)[j].slot == t.slot && (*live)[j].seq > t.live) t.live = (*live)[j].seq;
        out->push_back(t);
    }
    for (size_t j = 0; live != 0 && j < live->size() && out->size() < kBuildMaxHelpers; ++j)
    {
        if ((*live)[j].seq == 0) continue;
        int have = 0;
        for (size_t i = 0; i < out->size(); ++i) if ((*out)[i].slot == (*live)[j].slot) have = 1;
        if (have != 0) continue;
        BuildHelpAck t;
        t.slot = (*live)[j].slot; t.seq = 0; t.live = (*live)[j].seq;
        out->push_back(t);
    }
}
/* help1 fold 3 (findings 2, 3): the owner's gone marker row in pp.build is keyed per (key, placement nonce) - "~g" + 8 hex nonce + 8 hex
   FNV-1a of the key + ':' + the key's first 44 bytes (<= kBuildMaxKey); its REMOVE names the whole key. BuildGoneRowOf reads the nonce
   back and accepts the row only when its key is exactly the one this key and nonce make */
inline std::string BuildGoneRowKey(const std::string& key, unsigned int nonce)
{
    static const char hx[] = "0123456789abcdef";
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < key.size(); ++i) { h ^= (unsigned char)key[i]; h *= 16777619u; }
    std::string s("~g");
    for (int i = 7; i >= 0; --i) s += hx[(nonce >> (4 * i)) & 15u];
    for (int i = 7; i >= 0; --i) s += hx[(h >> (4 * i)) & 15u];
    s += ':';
    s += key.substr(0, kBuildMaxKey - 19);
    return s;
}
inline bool BuildGoneRowOf(const std::string& rowKey, const std::string& key, unsigned int* nonce)
{
    if (rowKey.size() < 19 || rowKey[0] != '~' || rowKey[1] != 'g') return false;
    unsigned int v = 0;
    for (int i = 2; i < 10; ++i)
    {
        const char c = rowKey[(size_t)i];
        const int d = (c >= '0' && c <= '9') ? c - '0' : ((c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1);
        if (d < 0) return false;
        v = v * 16u + (unsigned int)d;
    }
    if (BuildGoneRowKey(key, v) != rowKey) return false;
    *nonce = v;
    return true;
}

/* P87 (to-do P87): pieces dismantled while the other player was away. THE OWNER'S TOMBSTONES - the keys of this world's own pieces
   dismantled here, oldest first, kept in pp.build as "~t" rows and sent again as REMOVEs at every roster round. Add: once per key (a
   repeat moves it to the back); past cap the oldest leave into *evicted (may be 0). 1 = a new tombstone. A tombstone ends with
   BuildOweRemoveDrop (a live own piece stands at the key again). */
const size_t kBuildTombCap = 256;
inline int BuildTombAdd(std::vector<std::string>* t, const std::string& key, size_t cap, std::vector<std::string>* evicted)
{
    for (size_t i = 0; i < t->size(); ++i)
        if ((*t)[i] == key)
        {
            t->erase(t->begin() + (std::vector<std::string>::difference_type)i);
            t->push_back(key);
            return 0;
        }
    t->push_back(key);
    while (cap > 0 && t->size() > cap)
    {
        if (evicted != 0) evicted->push_back(t->front());
        t->erase(t->begin());
    }
    return 1;
}
/* P87: a tombstone's pp.build row key - "~t" + 8 hex FNV-1a of the key + ':' + the key's first bytes (<= kBuildMaxKey); its REMOVE
   names the whole key. Never a gone marker's "~g" row, never a bare key. */
inline std::string BuildTombRowKey(const std::string& key)
{
    static const char hx[] = "0123456789abcdef";
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < key.size(); ++i) { h ^= (unsigned char)key[i]; h *= 16777619u; }
    std::string s("~t");
    for (int i = 7; i >= 0; --i) s += hx[(h >> (4 * i)) & 15u];
    s += ':';
    s += key.substr(0, kBuildMaxKey - 11);
    return s;
}
inline bool BuildTombRowOf(const std::string& rowKey, const std::string& key)
{
    return rowKey.size() > 11 && rowKey[0] == '~' && rowKey[1] == 't' && BuildTombRowKey(key) == rowKey;
}
/* P87 root (T661): THE RECEIVER'S COPY LIST - one entry per copy of another game's piece made or adopted here, kept in pp.build so a
   restart still knows which keys are whose copies (the engine does not keep the owner: an unresolved 'coop-p<slot>' becomes a town's
   faction at load). Row key "~c" + 8 hex FNV-1a of the key + ':' + the key's first bytes (<= kBuildMaxKey), as the "~t" tombstone row -
   never a "~t" / "~g" row, never a bare key. Row layout version 1 (kBuildCopyRowVersion): the PLACE bytes are a PLACE of the WHOLE key -
   sid, the PLACE's pose, the placement nonce, host / form / floor / outside for furniture - whose ownerSlot is the copy OWNER's slot
   (0..kBuildOwnerSlotMax; an own row's PLACE carries kBuildOwnerSender), the STATE bytes an empty STATE of the key. A later layout takes a new row-key letter; a row
   that does not read as this layout is refused (counted, left in the record). */
const int kBuildCopyRowVersion = 1;
/* P87 rf1: THE LIST'S CEILING. pp.build's codec caps each row (ownrec.h kBuildRecMaxBytes, 4096 per PLACE / STATE; DecodeBuildRec's
   GdcCount bounds the row count by the bytes present), not the record; the store reads a record back only up to kBuildStoreRecReadMax
   bytes (store.cpp ReadFileBytes) - past it the whole pp.build is unreadable and held, own rows included. A '~c' row is at most
   kBuildCopyRowMaxBytes (a wire-cap key, sid and host key come to ~423; the offline test p87_copy_list_ceiling measures it), so a full
   list fills at most half of that limit, the other half left to own rows, tombstones and gone markers. Every row is written again at
   each pp.build write (~283 bytes a typical row), as every own row is. Stale entries no longer park: one whose key answered a complete empty
   search at kBuildCopyEmptyLoads loads in a row is evicted (BuildCopyEvict). rf1 STATE facts (a pre-rf1 row reads 0/0/0): destroyed bit =
   REMOVED here (kept until a save asked after the removal has landed), progress = loads in a row with a complete empty search, complete
   bit = an ALIAS entry (the key where the copy really stands when it differs from the owner's key). */
const size_t kBuildStoreRecReadMax = (size_t)64 << 20;   /* store.cpp ReadFileBytes: a store record longer than this is not read back */
const size_t kBuildCopyRowMaxBytes = 512;                /* one '~c' row in pp.build at most: row key + PLACE + STATE + three 4-byte lengths */
const size_t kBuildCopyListCap = kBuildStoreRecReadMax / 2 / kBuildCopyRowMaxBytes;   /* 65 536; past it a new copy is not listed (counted, logged): a missing entry only falls back to the owner rule */
const int kBuildCopyEmptyLoads = 3;
struct BuildCopyRec
{
    std::string key, sid, hostKey;
    int slot;                /* the owner's slot, 0..kBuildOwnerSlotMax */
    unsigned int nonce;      /* the placement's nonce, 0 = none */
    float pos[3];
    float rot[4];
    unsigned char hostForm;
    int floor;
    unsigned char outside;
    int removed;             /* P87 rf1 (STATE destroyed bit) */
    int emptyLoads;          /* P87 rf1 (STATE progress) */
    int alias;               /* P87 rf1 (STATE complete bit) */
    BuildCopyRec() : slot(-1), nonce(0), hostForm(0), floor(0), outside(0), removed(0), emptyLoads(0), alias(0)
    {
        for (int i = 0; i < 3; ++i) pos[i] = 0.0f;
        rot[0] = 1.0f; rot[1] = 0.0f; rot[2] = 0.0f; rot[3] = 0.0f;
    }
};
inline std::string BuildCopyRowKey(const std::string& key)
{
    static const char hx[] = "0123456789abcdef";
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < key.size(); ++i) { h ^= (unsigned char)key[i]; h *= 16777619u; }
    std::string s("~c");
    for (int i = 7; i >= 0; --i) s += hx[(h >> (4 * i)) & 15u];
    s += ':';
    s += key.substr(0, kBuildMaxKey - 11);
    return s;
}
inline bool BuildCopyRowIs(const std::string& rowKey)
{
    return rowKey.size() > 11 && rowKey[0] == '~' && rowKey[1] == 'c' && rowKey[10] == ':';
}
inline bool BuildCopyRowOf(const std::string& rowKey, const std::string& key)
{
    return BuildCopyRowIs(rowKey) && BuildCopyRowKey(key) == rowKey;
}
/* the text every near key shares with key (items.cpp BoxKeyNearKey: the same record sid and sector, x / z within a tenth): the key up
   to and including its last '@'. false when key has no sid before two '@' (BoxKeyNearKey answers no for such a key). The copy list
   asks BoxKeyNearKey only for the run of its sorted keys that starts with this text. */
inline bool BuildKeyNearPrefix(const std::string& key, std::string* prefix)
{
    const std::string::size_type at2 = key.rfind('@');
    if (at2 == std::string::npos || at2 == 0) return false;
    const std::string::size_type at1 = key.rfind('@', at2 - 1);
    if (at1 == std::string::npos || at1 == 0) return false;
    *prefix = key.substr(0, at2 + 1);
    return true;
}
inline bool BuildCopyRecSame(const BuildCopyRec& a, const BuildCopyRec& b)
{
    if (a.key != b.key || a.sid != b.sid || a.hostKey != b.hostKey || a.slot != b.slot || a.nonce != b.nonce) return false;
    if (a.hostForm != b.hostForm || a.floor != b.floor || a.outside != b.outside) return false;
    if (a.removed != b.removed || a.emptyLoads != b.emptyLoads || a.alias != b.alias) return false;   /* P87 rf1 */
    for (int i = 0; i < 3; ++i) if (a.pos[i] != b.pos[i]) return false;
    for (int i = 0; i < 4; ++i) if (a.rot[i] != b.rot[i]) return false;
    return true;
}
/* false (nothing usable) when the entry cannot be a v1 row: no key / sid, a slot outside 0..kBuildOwnerSlotMax, or a PLACE / STATE not encodable */
inline bool BuildCopyRowMake(const BuildCopyRec& c, std::string* rowKey, std::vector<char>* place, std::vector<char>* state)
{
    if (c.key.empty() || c.key.size() > kBuildMaxKey || c.sid.empty() || c.slot < 0 || c.slot > (int)kBuildOwnerSlotMax) return false;
    BuildMsg m;
    m.kind = kBuildPlace; m.key = c.key; m.sid = c.sid;
    for (int i = 0; i < 3; ++i) m.pos[i] = c.pos[i];
    for (int i = 0; i < 4; ++i) m.rot[i] = c.rot[i];
    m.complete = 0; m.ownerSlot = (unsigned short)c.slot; m.nonce = c.nonce;
    if (!c.hostKey.empty()) { m.hostKey = c.hostKey; m.hostForm = c.hostForm; m.floor = c.floor; m.outside = c.outside; }
    BuildMsg st;
    st.kind = kBuildState; st.key = c.key;
    st.destroyed = (unsigned char)(c.removed != 0 ? 1 : 0); st.complete = (unsigned char)(c.alias != 0 ? 1 : 0);   /* P87 rf1 */
    st.progress = (float)(c.emptyLoads < 0 ? 0 : (c.emptyLoads > 255 ? 255 : c.emptyLoads));
    place->clear(); state->clear();
    if (!BuildEncodable(m) || !BuildEncodable(st) || !EncodeBuild(place, m) || !EncodeBuild(state, st)) { place->clear(); state->clear(); return false; }
    *rowKey = BuildCopyRowKey(c.key);
    return true;
}
/* a pp.build row as a v1 copy-list entry: its PLACE decodes, names the key its row key was made from, carries a real owner slot and a
   sid; its STATE is a STATE of the same key. Anything else is false (*out untouched). */
inline bool BuildCopyRowRead(const std::string& rowKey, const std::string& place, const std::string& state, BuildCopyRec* out)
{
    if (!BuildCopyRowIs(rowKey) || place.empty() || state.empty()) return false;
    BuildMsg m, st;
    if (DecodeBuildSaved(place.data(), place.size(), &m) != kBuildDecodeOk || m.kind != kBuildPlace) return false;
    if (!BuildCopyRowOf(rowKey, m.key) || m.ownerSlot == kBuildOwnerSender || m.sid.empty()) return false;
    if (DecodeBuild(state.data(), state.size(), &st) != kBuildDecodeOk || st.kind != kBuildState || st.key != m.key) return false;
    BuildCopyRec c;
    c.key = m.key; c.sid = m.sid; c.slot = (int)m.ownerSlot; c.nonce = m.nonce;
    for (int i = 0; i < 3; ++i) c.pos[i] = m.pos[i];
    for (int i = 0; i < 4; ++i) c.rot[i] = m.rot[i];
    if (!m.hostKey.empty()) { c.hostKey = m.hostKey; c.hostForm = m.hostForm; c.floor = m.floor; c.outside = m.outside; }
    c.removed = st.destroyed != 0 ? 1 : 0;   /* P87 rf1: a pre-rf1 row carries 0 / 0 / 0 */
    c.alias = st.complete != 0 ? 1 : 0;
    c.emptyLoads = (st.progress >= 0.0f && st.progress <= 255.0f) ? (int)st.progress : 0;
    *out = c;
    return true;
}
/* P87 rf1: an entry whose key answered a complete empty search at kBuildCopyEmptyLoads loads in a row is evicted */
inline int BuildCopyEvict(int emptyLoads) { return emptyLoads >= kBuildCopyEmptyLoads ? 1 : 0; }
/* P87 rf1 (HIGH): a REMOVED mark retires once a save ASKED AFTER the mark (request number > markReq) has landed; a mark read back from
   pp.build (ready 0) waits until this world has settled it (the copy removed again, or a complete empty search at its key) */
inline int BuildCopyMarkRetires(int removed, int ready, unsigned long markReq, unsigned long landedReq)
{
    return (removed != 0 && ready != 0 && landedReq > markReq) ? 1 : 0;
}
/* P87: THE RECEIVER'S DECISION for a REMOVE whose key has no row here (the piece this game's save brought back is not a copy row).
   listed: the owner lists the key here (a row, a pending PLACE or REMOVE) - its own road, never touched. found: the key answered one
   live piece; completeNone: a complete search of the loaded zones found none (else the search was incomplete). mine: the live piece
   is this game's own - never removed. sender: the sender's stand-in owns it. dismantling: its dismantled flag is set - looked at again. */
const int kBuildReconRemove = 1;      /* taken as the sender's copy and removed the REMOVE way (no refund) */
const int kBuildReconKeepOwn = 2;
const int kBuildReconKeepOther = 3;
const int kBuildReconListed = 4;
const int kBuildReconPark = 5;        /* nothing in the loaded zones: looked up again when a zone finishes loading */
const int kBuildReconRetry = 6;
inline int BuildReconcileAct(int listed, int found, int completeNone, int mine, int sender, int dismantling)
{
    if (listed != 0) return kBuildReconListed;
    if (found == 0) return completeNone != 0 ? kBuildReconPark : kBuildReconRetry;
    if (mine != 0) return kBuildReconKeepOwn;
    if (sender == 0) return kBuildReconKeepOther;
    if (dismantling != 0) return kBuildReconRetry;
    return kBuildReconRemove;
}
/* P87 fold 3 (T651): THE ONE OWNER RULE the PLACE adopt (BdCopyOne), the reconcile (BdReconDrain) and the REMOVE apply (BdRemoveOne)
   share - "whose is this live piece, seen from the game of slot senderSlot?". A game's save names a copy's owner by the stand-in's
   RECORD id "coop-p<slot>"; after a restart the stand-in is a NEW object, so a pointer compare alone called the save's copy another
   owner's (T651: KEPT). isNull: no owner. isMine: this game's player faction. isSenderLive: the sender's live stand-in (the pointer, or
   the stand-in registered here for senderSlot). isStandInLive: any registered stand-in (or a protocol-67 coop-peer). recordSlot: the
   owner's record id as StandInRecordSlot reads it (n = coop-p<n>, -1 another id, -2 coop-peer, -3 unreadable). senderSlot: -1 unknown. */
const int kBuildLiveOwnNone = 0;
const int kBuildLiveOwnMine = 1;
const int kBuildLiveOwnSender = 2;          /* the sender's live stand-in */
const int kBuildLiveOwnSenderRecord = 3;    /* a faction whose record id is coop-p<sender slot> - a save's stand-in: the sender's too */
const int kBuildLiveOwnOtherStandIn = 4;    /* another slot's stand-in, or a protocol-67 coop-peer - never the sender's */
const int kBuildLiveOwnThird = 5;           /* anybody else (an unreadable record included) - never the sender's */
const int kBuildLiveOwnSenderList = 6;      /* P87 root: the key is in this game's copy list as the sender's slot's copy, same sid - the sender's */
/* P87 root (T661): the FIRST test. listSlot: the owner slot the copy list holds for the live piece's key (-1 not listed); listSidOk: the
   listed sid is the live piece's; ownRowHere: this game holds its own piece at the key (an own row, or its own pp.build row) - own rows
   always win, so the list is not asked. The list outranks the owner pointer: the engine gives a restored copy a town's faction (or, in a
   player town, this game's), never the stand-in. Every other answer is fold 3's, unchanged (the fallback for keys not listed). */
inline int BuildOwnerClass(int isNull, int isMine, int isSenderLive, int isStandInLive, int recordSlot, int senderSlot,
                           int listSlot = -1, int listSidOk = 0, int ownRowHere = 0)
{
    if (ownRowHere == 0 && senderSlot >= 0 && listSlot == senderSlot && listSidOk != 0) return kBuildLiveOwnSenderList;
    if (isNull != 0) return kBuildLiveOwnNone;
    if (isMine != 0) return kBuildLiveOwnMine;
    if (isSenderLive != 0) return kBuildLiveOwnSender;
    if (senderSlot >= 0 && recordSlot == senderSlot) return kBuildLiveOwnSenderRecord;
    if (isStandInLive != 0 || recordSlot >= 0 || recordSlot == -2) return kBuildLiveOwnOtherStandIn;
    return kBuildLiveOwnThird;
}
inline int BuildOwnerIsSender(int cls) { return (cls == kBuildLiveOwnSender || cls == kBuildLiveOwnSenderRecord || cls == kBuildLiveOwnSenderList) ? 1 : 0; }
/* P18 (parity P18): A BUILDING'S OWNER CHANGE. THE AUTHORITY: the game whose PLAYER the building now belongs to - the buyer's game for
   a house bought in town (Building::buyMeCallback 0x7ACB20 runs this->setFaction(playerFaction, 0), build/read-locks.md 1.3). That is
   the building road's own rule (the WRITER of a piece is its owner's game; a town building nobody owns is not synced - P12 / P19). The
   buyer's game registers the building as its own piece (the zone scan's rule) and sends its PLACE + STATE; the other game ADOPTS its copy
   through THE ONE OWNER RULE above (a town / NPC owner = kBuildLiveOwnNone / kBuildLiveOwnThird -> the engine's own setFaction to the
   sender's stand-in). No new message kind. WHO SENDS - one setFaction call seen on this game (Building vt+0xA0, 0x556EC0):
   onMain: the main thread (a purchase is a UI callback; a zone loading off the main thread is only counted); modWrite: the mod's own write
   is running (a received owner applied here, a hand-over, an owner fix) - THE LOOP REFUSAL, first; depth: the setFaction nesting (1 = the
   outermost call; the engine recurses over the building's doors and interior - the same change); wasMine / nowMine: the owner before / after
   the call is this game's player faction. */
const int kBuildOcRegister = 0;   /* queued: at the tick the building is registered as this game's own and its PLACE + STATE go out */
const int kBuildOcOffMain = 1;
const int kBuildOcModWrite = 2;   /* never sent: a received change must not go back */
const int kBuildOcNested = 3;
const int kBuildOcNotMine = 4;    /* to anybody but this game's player (a town override, an NPC faction, a stand-in) - not this road */
const int kBuildOcWasMine = 5;
inline int BuildOwnerChangeAct(int onMain, int modWrite, int depth, int wasMine, int nowMine)
{
    if (onMain == 0) return kBuildOcOffMain;
    if (modWrite != 0) return kBuildOcModWrite;
    if (depth > 1) return kBuildOcNested;
    if (nowMine == 0) return kBuildOcNotMine;
    if (wasMine != 0) return kBuildOcWasMine;
    return kBuildOcRegister;
}
/* P18: a queued change at the tick. hold: engine writes blocked, this world's copy list unread, or no player faction - wait (no try spent);
   tries / maxTries: ticks spent waiting for the key to resolve; live: the key resolves; nowMine: still this game's player's; rowOwn /
   rowCopy: the registry already holds the key as this game's own piece / as another game's piece. */
const int kBuildOdScan = 0;       /* registered as an own piece (BdScanPiece), its PLACE + STATE owed */
const int kBuildOdWait = 1;
const int kBuildOdGiveUp = 2;
const int kBuildOdNotMine = 3;
const int kBuildOdCopyRow = 4;    /* another game's piece the engine gave this game: never claimed (the other game is its writer) */
const int kBuildOdKnown = 5;      /* already this game's own piece: its own road (PLACE / STATE / REMOVE) carries it */
inline int BuildOwnerDrainAct(int hold, int tries, int maxTries, int live, int nowMine, int rowOwn, int rowCopy)
{
    if (hold != 0) return kBuildOdWait;
    if (live == 0) return (tries + 1 >= maxTries) ? kBuildOdGiveUp : kBuildOdWait;
    if (nowMine == 0) return kBuildOdNotMine;
    if (rowCopy != 0) return kBuildOdCopyRow;
    if (rowOwn != 0) return kBuildOdKnown;
    return kBuildOdScan;
}
/* P18, WHO APPLIES: the receiver's PLACE adopt of a live building whose owner class (BuildOwnerClass) is nobody or a third party (the town,
   an NPC faction) is an owner change applied here - counted and logged as P18's. 'mine' is never adopted (the receiver's own building
   stays its own; a duplicate is left alone) and the sender's own stand-in is no change. */
inline int BuildOwnerAdoptFromWorld(int cls) { return (cls == kBuildLiveOwnNone || cls == kBuildLiveOwnThird) ? 1 : 0; }
/* P18 fold 1 (item 4): ONLY A BUILDING WITH A KEY IS QUEUED. The buy callback 0x7ACB20 makes its own OUTERMOST setFaction calls over
   the building's doors (+0x1B8) and its interior's +0x88 list (furniture), which the hook would otherwise queue and give up on.
   The zone scan's own refusals decide it (BdScanPiece): layout furniture (+0x238 isFurnitureOf) or an interior object (+0xF8), and
   a door a building made for itself (+0x1A0 imADoor). Any other verdict is kept as it was. */
const int kBuildOcNotBuilding = 6;
inline int BuildOwnerChangeQueueAct(int act, int furniture, int door)
{
    if (act == kBuildOcRegister && (furniture != 0 || door != 0)) return kBuildOcNotBuilding;
    return act;
}
/* P18 fold 1 (item 1): THE NESTING COUNT AT THE TICK. The per-tick step (BdRosterTick, from BuildTick in the command channel's
   main-thread pump) never runs inside a setFaction, so the count is 0 there unless a fault inside a guarded owner change unwound
   past the hook's decrement - then every later purchase would be judged nested. The tick puts it back to 0; 1 = it had to. */
inline int BuildOcDepthReset(int* depth) { const int was = *depth; *depth = 0; return was != 0 ? 1 : 0; }
/* P18 fold 1 (items 2 and 6): an adopt from the world (nobody / a town or NPC owner) is a PURCHASE only when this game's copy was
   for sale (the engine's own Building::isForSale vt+0x2C0, asked BEFORE the owner is written: 1 yes, 0 no, -1 faulted), it is no
   furniture, and the owner really changes to a named owner (the sender's stand-in). Only a purchase gets the bought house's world
   side and the `P18 owner applied` counter / line; every other adopt from the world is counted adoptOther. */
inline int BuildOwnerAdoptIsPurchase(int adoptFromWorld, int furniture, int forSale, int ownerChanges, int targetNamed)
{
    return (adoptFromWorld != 0 && furniture == 0 && forSale == 1 && ownerChanges != 0 && targetNamed != 0) ? 1 : 0;
}
/* P18 fold 1 (item 6): the applied line's outcome from the owner write (fixed: 1 written, 0 not written - already the named owner or
   an empty target, -1 faulted). Not written is a SKIP, never a fault. */
const int kBuildOaApplied = 0;
const int kBuildOaSkip = 1;
const int kBuildOaFaulted = 2;
inline int BuildOwnerApplyOutcome(int fixed) { return fixed == 1 ? kBuildOaApplied : (fixed == 0 ? kBuildOaSkip : kBuildOaFaulted); }
/* P18 fold 1 (item 2): the residents are collected by the engine's own 0x6BA220 into a list this mod holds with a fixed room. That
   function grows a full list through the game's allocator (it would free this mod's buffer), so the step runs only when every
   platoon of the owner (the active +0x210 and unloaded +0x228 counts, plus the one its last branch may add) fits. */
inline int BuildBuyResidentsFit(unsigned active, unsigned unloaded, unsigned room)
{
    return (active < room && unloaded < room && active + unloaded + 1 < room) ? 1 : 0;
}
/* P18 fold 1 (item 5): Building::isForSale (vt+0x2C0, 1.0.65 0x29C4C0) offers "buy" on a house whose owner is not a player faction
   (+0x250) and not allied with the player - a stand-in is no player faction, so another player's house WAS for sale. As in single
   player (a player-owned house is not for sale) the engine's yes is kept only when the owner is not a stand-in. */
inline int BuildForSaleKeep(int engineSays, int ownerIsStandIn) { return (engineSays != 0 && ownerIsStandIn == 0) ? 1 : 0; }
/* P18 fold 2 (P19 review 2026-09-30): a town rebuild (ZoneMapContent::_activate 0x9FEC00 -> 0x9FD470, build/decomp_9fd470.txt) drops
   each saved world building (instance 'created' -1) whose FIRST state the engine finds is a building state (GAMESTATE_BUILDING 0x23)
   whose "owner faction ID" is not this game's player faction's id and whose townC / townCS are the rebuilt town's - and each whose
   first state is the other kind (0x53). A house another player owns names that player's stand-in RECORD id ("coop-p<n>", a
   protocol-67 save's "coop-peer"): it is held out of the drop, as the engine holds the local player's. recordSlot: n for coop-p<n>,
   -2 coop-peer, -1 any other id (an NPC, a town, this game's player - the engine's own answer stands). */
inline int BuildRebuildHold(int firstIsBuildingState, int recordSlot) { return (firstIsBuildingState != 0 && (recordSlot >= 0 || recordSlot == -2)) ? 1 : 0; }
inline int BuildRebuildOwnerSlot(int standInSlotOfId, int isLegacyPeerId) { return isLegacyPeerId != 0 ? -2 : (standInSlotOfId >= 0 ? standInSlotOfId : -1); }
/* WHOSE STAND-IN A COPY BELONGS TO: the slot of the player whose PLACE made it (or whose REMOVE names a piece with no row here). fromSlot:
   the sender's slot as recorded when the message arrived (>= 0 a slot; kBuildSenderSessionPeer = the old game-to-game link's peer before
   its slot was known, ownerroute.h kAddrOwnerSessionPeer; -1 none). linkPeerSlot: that link peer's slot now (-1 unknown). The answer is
   the slot whose stand-in owns the copy, or -1 = not known yet - the copy waits; it is never given another player's stand-in. */
const int kBuildSenderSessionPeer = -2;
inline int BuildSenderSlotNow(int fromSlot, int linkPeerSlot)
{
    if (fromSlot >= 0) return fromSlot;
    if (fromSlot == kBuildSenderSessionPeer && linkPeerSlot >= 0) return linkPeerSlot;
    return -1;
}
inline const char* BuildOwnerWord(int cls)
{
    switch (cls)
    {
    case kBuildLiveOwnNone: return "none";
    case kBuildLiveOwnMine: return "mine";
    case kBuildLiveOwnSender: return "peer";
    case kBuildLiveOwnSenderRecord: return "stand-in-record";
    case kBuildLiveOwnOtherStandIn: return "other-stand-in";
    case kBuildLiveOwnSenderList: return "copy-list";
    default: return "other";
    }
}

/* P87 fold 1 (H1): THE OWNER'S RE-SEND RULE for one tombstone. readBack: read from pp.build (the owner did not see the dismantle in this
   world - after a crash or an older save the piece may stand again); verified: the zone holding the key was walked in this world and
   the live lookup found no own piece there. ownHere: a live own row at the key (or the lookup found one); placePending: a PLACE for
   the key waits here (e.g. a hand-over to this game). */
const int kBuildTombSend = 0;   /* its REMOVE is owed again */
const int kBuildTombDrop = 1;   /* the tombstone ends (the owner's list wins) */
const int kBuildTombHold = 2;   /* kept in pp.build, not sent (yet) */
inline int BuildTombSendAct(int readBack, int verified, int ownHere, int placePending)
{
    if (ownHere != 0 || placePending != 0) return kBuildTombDrop;
    if (readBack != 0 && verified == 0) return kBuildTombHold;
    return kBuildTombSend;
}
/* P87 fold 1 (H1, H1b): verifying a read-back tombstone. tail: the row carried the fold-1 tail (else its furniture-ness is unknown -
   never verified). walked: the zone scan walked the key's zone (furniture: its host's) in this world. hostFound: furniture - its host
   resolved. look: 0 a complete search found nothing at the key, 1 one piece answered, 2 anything else. own / dismantling: that piece.
   1 = verified absent, 0 = not yet, -1 = an own piece stands at the key (the tombstone ends). */
inline int BuildTombVerifyVerdict(int tail, int furn, int walked, int hostFound, int look, int own, int dismantling)
{
    if (tail == 0 || walked == 0) return 0;
    if (furn != 0 && hostFound == 0) return 0;
    if (look == 0) return 1;
    if (look != 1) return 0;
    if (own == 0) return 1;
    return dismantling != 0 ? 0 : -1;
}
/* P87 fold 1 (H2): a REMOVE's nonce names the placement it ends; a row / pending PLACE / owed hand-over here of ANOTHER placement (both
   nonces known and different) is not touched. 1 = skip */
inline int BuildRemoveNonceSkip(unsigned int removeNonce, unsigned int hereNonce)
{
    return (removeNonce != 0 && hereNonce != 0 && removeNonce != hereNonce) ? 1 : 0;
}
/* P87 fold 1 (L1): a blueprint placed here in this world, never shared (its PLACE never went) and never worked on, leaves no copy
   anywhere - no tombstone. 1 = tombstone it */
inline int BuildTombNeeded(int sentOk, float progress, int placedHereThisWorld)
{
    return (sentOk == 0 && !(progress > 0.0f) && placedHereThisWorld != 0) ? 0 : 1;
}
/* P14 (mmo8b, mmo8-buildings-read.md 4b): an own piece found STANDING at a tombstone's key (the load / zone scan, the tombstone
   check's lookup). A READ-BACK tombstone (read from pp.build at this load: the dismantle was recorded after the save this zone came
   from, and its refund is already in the records) with the fold-1 tail: the piece is the save's copy of a piece the records retired -
   REMOVED here with no refund (kBuildTombKill), never registered, the tombstone kept. A live own row or a PLACE waiting at the key (a
   new placement), a tombstone of THIS world (the dismantle was seen here, so a piece standing is a new one) or a row written before
   P87 fold 1: the tombstone ends, as before (kBuildTombDrop). */
const int kBuildTombKill = 3;
inline int BuildTombSeenAct(int readBack, int tail, int ownRow, int placePending)
{
    if (ownRow != 0 || placePending != 0) return kBuildTombDrop;
    return (readBack != 0 && tail != 0) ? kBuildTombKill : kBuildTombDrop;
}
/* P14: one look at a queued removal (build.cpp BdTombKillDrain, the K2 safe point). zoneLoaded: the key's zone (furniture: its host's)
   is loaded NOW. look: 0 a complete search found nothing at the key, 1 one piece answered, 2 anything else. own: that piece is this
   game's faction's; listedCopy: the key is in this game's copy list (another game's copy); dismantling: its dismantled flag is set.
   tries: dismantle calls made; misses: looks that settled nothing. Wait (not counted), Miss (counted - GiveUp at maxTries), Call the
   engine's dismantle (mats zeroed first), Done (nothing stands: removed, or already gone), Cancel (not this game's own piece). */
const int kBuildTombKillWait = 0, kBuildTombKillMiss = 1, kBuildTombKillCall = 2, kBuildTombKillDone = 3, kBuildTombKillCancel = 4, kBuildTombKillGiveUp = 5;
/* P14 fold 1: keyExact - the FOUND piece's own key is the tombstone key (F2: the lookup's 0.1 u window can answer a rebuilt piece);
   rowNamed - a registry row (not removed) names the found piece's key; interior - the found piece has an interior (F4: excluded).
   Exclude: left standing (the tombstone ends, counted); Mismatch: not the retired piece - never touched. F3: our call took (dismantling
   after a call) - the engine frees it next frame, or a wall's deferred dismantle runs: Wait, never a miss. */
const int kBuildTombKillExclude = 6, kBuildTombKillMismatch = 7;
inline int BuildTombKillStep(int zoneLoaded, int look, int own, int listedCopy, int dismantling, int tries, int misses, int maxTries,
                             int keyExact, int rowNamed, int interior)
{
    if (zoneLoaded == 0) return kBuildTombKillWait;
    if (look == 0) return kBuildTombKillDone;
    if (look == 1 && (own == 0 || listedCopy != 0)) return kBuildTombKillCancel;
    if (look == 1 && dismantling != 0 && tries > 0) return kBuildTombKillWait;
    if (look != 1 || dismantling != 0) return (misses + 1 >= maxTries) ? kBuildTombKillGiveUp : kBuildTombKillMiss;
    if (keyExact == 0 || rowNamed != 0) return kBuildTombKillMismatch;
    if (interior != 0 && tries == 0) return kBuildTombKillExclude;
    return (tries >= maxTries) ? kBuildTombKillGiveUp : kBuildTombKillCall;
}

/* P14 fold 1 (manager decision 2026-09-30): THE ESCAPE LEDGER of a tombstone. A piece standing at reload proves its ground refund was
   never recorded (the piece and its refund live in the same zone file), so the P14 removal lets the engine's refund drop again - MINUS
   what provably landed in another record. Kept in the tombstone's pp.build '~t' row, in its STATE bytes (an empty STATE before; no
   reader before fold 1 reads them). refund: the items the engine's refund dropped (the landed ground key "G|<sid>@sx,sy@x,z,y" and the
   quantity; gen 1 = a later re-drop (the P14 removal's own) - its keys are matched, never owed). esc: what left the ground - our pickup
   {ref, qty, the squad record key, the seq that record must reach} or the other player's (kind 1: no record of ours to check).
   missed: refund items the ground road never keyed (> 64 per refund, not on the ground, no key); spill: items that landed in another
   zone than the piece's (that zone's file may hold them - inexact, counted). */
struct BuildLedgerRef { std::string sid, gkey; int qty; unsigned char gen; BuildLedgerRef() : qty(0), gen(0) {} };
struct BuildLedgerEsc { unsigned char ref, kind; int qty; unsigned long long seq; std::string rec; BuildLedgerEsc() : ref(0), kind(0), qty(0), seq(0) {} };
/* P14 fold 2 (D1/D3/D6): a material's line. unkeyed: first-drop items of it no refund entry could key (the entries or the row's bytes
   full) - owed all the same, their pickups matched by material and zone (an esc entry whose ref is kBuildLedgerMatRef + the line's
   index). blind: a pickup of it could not be recorded (the caps full) - its re-drop is REFUSED (a loss of what was not picked, never a
   duplicate of what was). */
struct BuildLedgerMat { std::string sid; int unkeyed; unsigned char blind; BuildLedgerMat() : unkeyed(0), blind(0) {} };
/* P14 fold 3 (N2): refuse - the whole ledger REFUSES every material's re-drop (a blind mark that did not fit, an encoding that failed):
   a loss of what was not picked, never a duplicate of what was */
struct BuildLedger { std::vector<BuildLedgerRef> refund; std::vector<BuildLedgerEsc> esc; std::vector<BuildLedgerMat> mat; int missed, spill; unsigned char refuse; BuildLedger() : missed(0), spill(0), refuse(0) {} };
/* P14 fold 2 (D3): refund entries scaled from 16 to 64, bounded by the row's 4000 bytes - every add checks the encoding still fits, so
   an encoding never fails for size; what does not fit is owed by material (BuildLedgerMat.unkeyed) */
const size_t kBuildLedgerMaxRef = 64, kBuildLedgerMaxEsc = 48, kBuildLedgerMaxRecs = 8, kBuildLedgerMaxBytes = 4000, kBuildLedgerMaxMat = 16;
/* P14 fold 5 (T732): kBuildLedgerEscHolder - a refund item our PUT handed to the area's holder, applied there: on the holder's
   ground (decision 255: the holder's ground is the truth, a stale save never re-owes it) */
const unsigned char kBuildLedgerEscOwn = 0, kBuildLedgerEscPeer = 1, kBuildLedgerEscHolder = 2;
const unsigned char kBuildLedgerMatRef = 0x80;
inline bool BuildLedgerEmpty(const BuildLedger& L) { return L.refund.empty() && L.esc.empty() && L.mat.empty() && L.missed == 0 && L.spill == 0 && L.refuse == 0; }
/* the base sid of a ground key "G|<sid>@..." (a hashed one "#xxxxxxxx" comes back as it is) */
inline bool BuildGroundKeySid(const std::string& gkey, std::string* sid)
{
    if (gkey.size() < 4 || gkey[0] != 'G' || gkey[1] != '|') return false;
    const size_t at = gkey.find('@', 2);
    if (at == std::string::npos || at == 2) return false;
    *sid = gkey.substr(2, at - 2);
    return true;
}
/* P14 fold 2 (D1): a ledger sid names a material slot's sid as it is, or hashed the way the ground key hashes a sid it cannot hold
   (groundkey.h GroundSidHash32: "#" + FNV-1a 32 in 8 lower-case hex) */
inline bool BuildLedgerSidIs(const std::string& led, const std::string& sid)
{
    if (led == sid) return true;
    if (led.size() != 9 || led[0] != '#' || sid.empty()) return false;
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < sid.size(); ++i) { h ^= (unsigned int)(unsigned char)sid[i]; h *= 16777619u; }
    const char* const hex = "0123456789abcdef";
    for (int i = 0; i < 8; ++i) if (led[1 + i] != hex[(h >> ((7 - i) * 4)) & 0xFu]) return false;
    return true;
}
inline int BuildLedgerFind(const BuildLedger& L, const std::string& gkey)
{
    for (size_t i = 0; i < L.refund.size(); ++i) if (L.refund[i].gkey == gkey) return (int)i;
    return -1;
}
inline void BuildLedPut8(std::string* o, unsigned int v) { o->push_back((char)(v & 0xFFu)); }
inline void BuildLedPut32(std::string* o, unsigned int v) { for (int i = 0; i < 4; ++i) o->push_back((char)((v >> (8 * i)) & 0xFFu)); }
inline void BuildLedPutStr(std::string* o, const std::string& s) { BuildLedPut8(o, (unsigned int)s.size()); o->append(s); }
/* "LG3" (P14 fold 3) | u32 missed | u32 spill | u8 flags (1 = refuse every re-drop) | u8 nRecs, nRecs x str | u8 nRef, nRef x (u8 gen, u32 qty, str gkey) | u8 nEsc, nEsc x (u8 ref,
   u8 kind, u32 qty, u32 seqLo, u32 seqHi, u8 rec index (0xFF none)) | u8 nMat, nMat x (str sid, u32 unkeyed, u8 blind); str = u8 length
   + bytes. "LG2" (P14 fold 2) is the same without the flags byte; "LG1" (fold 1) also without the material lines. false: too many rows (Raw: any size) */
inline bool BuildLedgerEncodeRaw(const BuildLedger& L, std::string* out)
{
    out->clear();
    if (L.refund.size() > kBuildLedgerMaxRef || L.esc.size() > kBuildLedgerMaxEsc || L.mat.size() > kBuildLedgerMaxMat) return false;
    std::vector<std::string> recs;
    for (size_t i = 0; i < L.esc.size(); ++i)
        if (!L.esc[i].rec.empty() && std::find(recs.begin(), recs.end(), L.esc[i].rec) == recs.end()) recs.push_back(L.esc[i].rec);
    if (recs.size() > kBuildLedgerMaxRecs) return false;
    out->append("LG3");
    BuildLedPut32(out, (unsigned int)(L.missed < 0 ? 0 : L.missed));
    BuildLedPut32(out, (unsigned int)(L.spill < 0 ? 0 : L.spill));
    BuildLedPut8(out, L.refuse != 0 ? 1u : 0u);   /* P14 fold 3 (N2): the fixed header - never short of room */
    BuildLedPut8(out, (unsigned int)recs.size());
    for (size_t i = 0; i < recs.size(); ++i) { if (recs[i].size() > 255) return false; BuildLedPutStr(out, recs[i]); }
    BuildLedPut8(out, (unsigned int)L.refund.size());
    for (size_t i = 0; i < L.refund.size(); ++i)
    {
        if (L.refund[i].gkey.size() > 255) return false;
        BuildLedPut8(out, L.refund[i].gen); BuildLedPut32(out, (unsigned int)L.refund[i].qty); BuildLedPutStr(out, L.refund[i].gkey);
    }
    BuildLedPut8(out, (unsigned int)L.esc.size());
    for (size_t i = 0; i < L.esc.size(); ++i)
    {
        const BuildLedgerEsc& e = L.esc[i];
        BuildLedPut8(out, e.ref); BuildLedPut8(out, e.kind); BuildLedPut32(out, (unsigned int)e.qty);
        BuildLedPut32(out, (unsigned int)(e.seq & 0xFFFFFFFFull)); BuildLedPut32(out, (unsigned int)(e.seq >> 32));
        size_t ri = 0xFF;
        for (size_t k = 0; k < recs.size(); ++k) if (recs[k] == e.rec) ri = k;
        BuildLedPut8(out, (unsigned int)ri);
    }
    BuildLedPut8(out, (unsigned int)L.mat.size());
    for (size_t i = 0; i < L.mat.size(); ++i)
    {
        if (L.mat[i].sid.size() > 255) return false;
        BuildLedPutStr(out, L.mat[i].sid); BuildLedPut32(out, (unsigned int)(L.mat[i].unkeyed < 0 ? 0 : L.mat[i].unkeyed));
        BuildLedPut8(out, L.mat[i].blind != 0 ? 1u : 0u);
    }
    return true;
}
/* false: too many rows or > 4000 bytes */
inline bool BuildLedgerEncode(const BuildLedger& L, std::string* out) { return BuildLedgerEncodeRaw(L, out) && out->size() <= kBuildLedgerMaxBytes; }
inline bool BuildLedgerFits(const BuildLedger& L) { std::string s; return BuildLedgerEncode(L, &s); }
/* a keyed refund entry or an escape entry leaves room for one material line (1 + 200 + 4 + 1 bytes), so a material can always be owed
   by material or marked blind once the keys are full */
const size_t kBuildLedgerMatReserve = 206;
inline bool BuildLedgerFitsKeyed(const BuildLedger& L) { std::string s; return BuildLedgerEncodeRaw(L, &s) && s.size() + kBuildLedgerMatReserve <= kBuildLedgerMaxBytes; }
inline int BuildLedgerMatFind(const BuildLedger& L, const std::string& sid)
{
    for (size_t i = 0; i < L.mat.size(); ++i) if (L.mat[i].sid == sid) return (int)i;
    return -1;
}
/* the material's line, made when missing; -1 = no room (16 lines, or the row's bytes) */
inline int BuildLedgerMatAt(BuildLedger* L, const std::string& sid)
{
    const int m = BuildLedgerMatFind(*L, sid);
    if (m >= 0) return m;
    if (L->mat.size() >= kBuildLedgerMaxMat || sid.empty() || sid.size() > 200) return -1;
    BuildLedgerMat x; x.sid = sid;
    L->mat.push_back(x);
    if (BuildLedgerFits(*L)) return (int)L->mat.size() - 1;
    L->mat.pop_back();
    return -1;
}
/* a refund item landed: merged into the entry of its key and gen (two items on one spot), else a new entry. 1 = keyed; 2 = P14 fold 2
   (D3): no room for its key (64 entries, or the row's bytes less one material line's room) - a first-drop item is owed by material (its pickups matched by material and
   zone); 0 = not in the ledger (missed: a re-drop, or no material line left) */
inline int BuildLedgerAddRefund(BuildLedger* L, const std::string& sid, const std::string& gkey, int qty, int gen)
{
    if (qty < 1) qty = 1;
    const unsigned char g = (unsigned char)(gen != 0 ? 1 : 0);
    for (size_t i = 0; i < L->refund.size(); ++i)
        if (L->refund[i].gkey == gkey && L->refund[i].gen == g) { L->refund[i].qty += qty; return 1; }
    if (L->refund.size() < kBuildLedgerMaxRef && gkey.size() <= 200 && sid.size() <= 200)
    {
        BuildLedgerRef r; r.sid = sid; r.gkey = gkey; r.qty = qty; r.gen = g;
        L->refund.push_back(r);
        if (BuildLedgerFitsKeyed(*L)) return 1;
        L->refund.pop_back();
    }
    if (g == 0)
    {
        const int m = BuildLedgerMatAt(L, sid);
        if (m >= 0) { L->mat[m].unkeyed += qty; return 2; }
    }
    L->missed += qty;
    return 0;
}
/* the escapes of one ref value (a refund index, or kBuildLedgerMatRef + a material index) */
inline int BuildLedgerEscOf(const BuildLedger& L, size_t ref)
{
    int s = 0;
    for (size_t i = 0; i < L.esc.size(); ++i) if ((size_t)L.esc[i].ref == ref) s += L.esc[i].qty;
    return s;
}
inline size_t BuildLedgerRecCount(const BuildLedger& L, const std::string& rec)
{
    std::vector<std::string> r;
    for (size_t i = 0; i < L.esc.size(); ++i)
        if (!L.esc[i].rec.empty() && std::find(r.begin(), r.end(), L.esc[i].rec) == r.end()) r.push_back(L.esc[i].rec);
    if (!rec.empty() && std::find(r.begin(), r.end(), rec) == r.end()) r.push_back(rec);
    return r.size();
}
/* one escape entry. 1 = an entry of its own; 2 = P14 fold 2 (D6): the entries or the bytes full - merged into an entry of the same ref,
   kind and record, carrying the EARLIER seq (P14 fold 3: it counts once the earlier write landed - inexact toward a LOSS of the later
   part, never a duplicate of the earlier one; counted by the caller); 0 = neither (a 9th squad record, or no entry to merge into) */
inline int BuildLedgerEscPut(BuildLedger* L, unsigned char refv, int take, unsigned char kind, const std::string& rec, unsigned long long seq)
{
    if (rec.size() <= 200 && BuildLedgerRecCount(*L, rec) <= kBuildLedgerMaxRecs && L->esc.size() < kBuildLedgerMaxEsc)
    {
        BuildLedgerEsc e; e.ref = refv; e.kind = kind; e.qty = take; e.seq = seq; e.rec = rec;
        L->esc.push_back(e);
        if (BuildLedgerFitsKeyed(*L)) return 1;
        L->esc.pop_back();
    }
    for (size_t i = 0; i < L->esc.size(); ++i)
        if (L->esc[i].ref == refv && L->esc[i].kind == kind && L->esc[i].rec == rec)
        {
            L->esc[i].qty += take;
            if (seq < L->esc[i].seq) L->esc[i].seq = seq;
            return 2;
        }
    return 0;
}
/* P14 fold 2 (D6): a pickup of the material could not be recorded - its re-drop is refused from now on. 1 = the material BLIND; 2 = P14
   fold 3 (N2): no line for it fits - the whole ledger REFUSES (the flag rides the fixed header) */
inline int BuildLedgerSetBlind(BuildLedger* L, const std::string& sid)
{
    const int m = BuildLedgerMatAt(L, sid);
    if (m < 0) { L->refuse = 1; return 2; }
    L->mat[m].blind = 1;
    return 1;
}
/* P14 fold 3 (N1): a first-drop item owed by MATERIAL (BuildLedgerAddRefund 2) that landed in another zone (spill) - its pickup is never
   charged (the by-material match takes only the piece's own zone), so the material is BLIND (no line: the whole ledger refuses).
   0 = nothing to do; else as BuildLedgerSetBlind */
inline int BuildLedgerSpillUnkeyed(BuildLedger* L, const std::string& sid, int kr, int spill)
{
    if (kr != 2 || spill == 0) return 0;
    return BuildLedgerSetBlind(L, sid);
}
/* P14 fold 4 (e5): 1 only when both sectors parsed and are equal - the zones SHOWN equal. A sector that does not parse counts as
   another zone (the by-material pickup match needs both, so that pickup is never charged: its material goes blind) */
inline int BuildZoneShownSame(int pieceOk, int psx, int psy, int groundOk, int gsx, int gsy)
{
    return (pieceOk != 0 && groundOk != 0 && psx == gsx && psy == gsy) ? 1 : 0;
}
/* P14 fold 4 (L2): a by-material pickup that more than one same-zone ledger has room for - which piece dropped it is not known, and
   charging one lets another re-drop in full (a duplicate): the material is BLIND in every candidate (each re-drop of it refused - a
   loss, never a duplicate). The count of candidates with no line for the mark (those ledgers REFUSE every re-drop) */
inline int BuildLedgerBlindAll(const std::vector<BuildLedger*>& cand, const std::string& sid)
{
    int ra = 0;
    for (size_t i = 0; i < cand.size(); ++i)
        if (cand[i] != 0 && BuildLedgerSetBlind(cand[i], sid) == 2) ++ra;
    return ra;
}
/* a pickup of qty from the ground key: spread over that key's entries with room left, so partial pickups add up to at most the refund's
   quantity. The amount recorded (0: not a refund item or its entries used up). *full: 0; 1 = a part could not be recorded - the material
   is BLIND (P14 fold 2, D6); 2 = merged into an entry (inexact, D6); 3 = not recorded and no line for the blind mark - the whole
   ledger REFUSES (P14 fold 3, N2) */
inline int BuildLedgerAddEsc(BuildLedger* L, const std::string& gkey, int qty, unsigned char kind, const std::string& rec, unsigned long long seq, int* full)
{
    int got = 0, merged = 0, blind = 0;
    if (full != 0) *full = 0;
    for (size_t i = 0; i < L->refund.size() && got < qty; ++i)
    {
        if (L->refund[i].gkey != gkey) continue;
        const int room = L->refund[i].qty - BuildLedgerEscOf(*L, i);
        if (room <= 0) continue;
        const int take = (qty - got < room) ? qty - got : room;
        const int p = BuildLedgerEscPut(L, (unsigned char)i, take, kind, rec, seq);
        if (p == 0) { blind = (BuildLedgerSetBlind(L, L->refund[i].sid) == 1) ? 1 : 3; break; }
        if (p == 2) merged = 1;
        got += take;
    }
    if (full != 0) *full = (blind != 0) ? blind : (merged != 0 ? 2 : 0);
    return got;
}
/* P14 fold 2 (D1/D3): what a material's unkeyed first drop still has room for (its unkeyed quantity less the pickups charged to it) */
inline int BuildLedgerMatRoom(const BuildLedger& L, const std::string& sid)
{
    const int m = BuildLedgerMatFind(L, sid);
    if (m < 0) return 0;
    const int r = L.mat[m].unkeyed - BuildLedgerEscOf(L, (size_t)kBuildLedgerMatRef + (size_t)m);
    return r > 0 ? r : 0;
}
/* P14 fold 2 (D1/D3): a pickup no refund entry keys, charged by MATERIAL to its unkeyed first drop (the caller matched the zone - an item
   of the same material there that was not the refund's is charged too: inexact toward a loss, bounded by the unkeyed quantity).
   The amount recorded; *full as BuildLedgerAddEsc */
inline int BuildLedgerAddEscMat(BuildLedger* L, const std::string& sid, int qty, unsigned char kind, const std::string& rec, unsigned long long seq, int* full)
{
    if (full != 0) *full = 0;
    const int room = BuildLedgerMatRoom(*L, sid);
    if (room <= 0 || qty <= 0) return 0;
    const int m = BuildLedgerMatFind(*L, sid);
    const int take = (qty < room) ? qty : room;
    const int p = BuildLedgerEscPut(L, (unsigned char)(kBuildLedgerMatRef + m), take, kind, rec, seq);
    if (p == 0) { L->mat[m].blind = 1; if (full != 0) *full = 1; return 0; }
    if (p == 2 && full != 0) *full = 2;
    return take;
}
/* at reload: our pickup counts only when the load's overlay APPLIED that squad record at or past the entry's seq (appliedSeq 0 = not
   applied); the other player's always counts (inexact: both games down within ~2 s of it) */
inline int BuildLedgerEscCounts(const BuildLedgerEsc& e, unsigned long long appliedSeq)
{
    if (e.kind == kBuildLedgerEscPeer || e.kind == kBuildLedgerEscHolder) return 1;   /* P14 fold 5: the holder's ground always counts */
    return (appliedSeq != 0 && e.seq != 0 && appliedSeq >= e.seq) ? 1 : 0;
}
/* P14 fold 2 (D2): at the first look after a load, our pickups that did NOT count leave the ledger - their items were re-dropped, and
   after the load the seq restarts from the highest on disk, so a later squad write would reach their seq and a second reload would
   subtract items that never reached the squad. ok[i] = BuildLedgerEscCounts of esc[i]. The number dropped */
inline int BuildLedgerPruneUncounted(BuildLedger* L, const std::vector<int>& ok)
{
    std::vector<BuildLedgerEsc> keep;
    int n = 0;
    for (size_t i = 0; i < L->esc.size(); ++i)
    {
        if (L->esc[i].kind == kBuildLedgerEscOwn && (i >= ok.size() || ok[i] == 0)) { ++n; continue; }
        keep.push_back(L->esc[i]);
    }
    L->esc.swap(keep);
    return n;
}
/* the first drop's quantity of a material: its gen 0 entries plus its unkeyed items (P14 fold 2, D3); a ledger sid may be the hashed form
   (D1). -1 = nothing names the material */
inline int BuildLedgerOwed(const BuildLedger& L, const std::string& sid)
{
    int s = 0, any = 0;
    for (size_t i = 0; i < L.refund.size(); ++i) if (L.refund[i].gen == 0 && BuildLedgerSidIs(L.refund[i].sid, sid)) { s += L.refund[i].qty; any = 1; }
    for (size_t i = 0; i < L.mat.size(); ++i) if (L.mat[i].unkeyed > 0 && BuildLedgerSidIs(L.mat[i].sid, sid)) { s += L.mat[i].unkeyed; any = 1; }
    return any != 0 ? s : -1;
}
inline int BuildLedgerBlindOf(const BuildLedger& L, const std::string& sid)
{
    for (size_t i = 0; i < L.mat.size(); ++i) if (L.mat[i].blind != 0 && BuildLedgerSidIs(L.mat[i].sid, sid)) return 1;
    return 0;
}
/* the counted escapes of a material (ok[i] = BuildLedgerEscCounts of esc[i]) - keyed and by material */
inline int BuildLedgerEscaped(const BuildLedger& L, const std::vector<int>& ok, const std::string& sid)
{
    int s = 0;
    for (size_t i = 0; i < L.esc.size() && i < ok.size(); ++i)
    {
        if (ok[i] == 0) continue;
        const size_t r = (size_t)L.esc[i].ref;
        const std::string* es = 0;
        if (r < (size_t)kBuildLedgerMatRef) { if (r < L.refund.size()) es = &L.refund[r].sid; }
        else if (r - (size_t)kBuildLedgerMatRef < L.mat.size()) es = &L.mat[r - (size_t)kBuildLedgerMatRef].sid;
        if (es != 0 && BuildLedgerSidIs(*es, sid)) s += L.esc[i].qty;
    }
    return s;
}
/* the delivered amount a material slot keeps before the engine's refund. The refund 0x29DBD0 drops (int)(delivered x a random share in
   [lo, 1.0]) items, at least 1 (build/decomp_29dbd0.txt) - so the rest owed (the first drop less the counted escapes) caps it: the
   re-drop never exceeds what was lost. P14 fold 2 (D1): owed unknown (-1: nothing names the material - a tombstone written before
   fold 1, a refund the ledger never saw): REFUSED - 0, a loss of that refund, never a duplicate of what was picked up unrecorded. */
inline float BuildLedgerKeep(float delivered, int owed, int escaped)
{
    if (!(delivered > 0.0f)) return delivered;
    if (owed < 0) return 0.0f;
    const int r = owed - escaped;
    if (r <= 0) return 0.0f;
    return ((float)r < delivered) ? (float)r : delivered;
}
/* P14 fold 2: a slot's kept amount - *refused when nothing names the material or it is blind (its pickups could not be accounted) */
inline float BuildLedgerKeepOf(const BuildLedger& L, const std::vector<int>& ok, const std::string& sid, float delivered, int* owed, int* escaped, int* refused)
{
    *owed = BuildLedgerOwed(L, sid);
    *escaped = BuildLedgerEscaped(L, ok, sid);
    *refused = (L.refuse != 0 || *owed < 0 || BuildLedgerBlindOf(L, sid) != 0) ? 1 : 0;   /* P14 fold 3 (N2): the whole-ledger refuse */
    if (!(delivered > 0.0f)) return delivered;
    return (*refused != 0) ? 0.0f : BuildLedgerKeep(delivered, *owed, *escaped);
}
/* P14 fold 2 (D4 / D7): what a P14 call does to a piece's mats first, and what a removal that ends with the piece standing (a faulted
   call, a give-up) does to them. A wall run's shared state is only read (0x29DBD0 never refunds it). Once a call's refund fired
   (refundsSeen > 0) a retry ZEROES them - the refund went, never twice - and a piece left standing keeps 0; before that a retry keeps
   the ledger's amount and a piece left standing keeps the amounts the removal set (P14 fold 3, D7: the ledger-reduced ones - its full
   delivered mats back would refund the first drop's picked-up items again at a later hand dismantle, a duplicate). */
const int kBuildTombMatsRead = 0, kBuildTombMatsLedger = 1, kBuildTombMatsZero = 2;
inline int BuildTombKillMats(int wall, long long refundsSeen)
{
    if (wall != 0) return kBuildTombMatsRead;
    return (refundsSeen > 0) ? kBuildTombMatsZero : kBuildTombMatsLedger;
}
inline int BuildTombKillRestore(int wall, long long refundsSeen)
{
    if (wall != 0) return kBuildTombMatsRead;
    return (refundsSeen > 0) ? kBuildTombMatsZero : kBuildTombMatsRead;   /* P14 fold 3 (D7): Read = leave the amounts the removal set */
}
/* P14 fold 4 (L1): a P14 call that must first set the mats (the ledger's amounts, or 0 once a refund fired) found them unreadable
   (nz < 0: the slots, or the tomb info missing) - the call would refund the FULL delivered mats while earlier picked-up items are
   in the squad (a duplicate), so it is never made: Retry at a later safe point; GiveUp (the piece stands, as a give-up does) once
   maxTries such looks went by. A wall run (Read) never refunds its shared state - called as before */
const int kBuildTombUnreadCall = 0, kBuildTombUnreadRetry = 1, kBuildTombUnreadGiveUp = 2;
inline int BuildTombMatsUnread(int mode, int nz, int unreadSoFar, int maxTries)
{
    if (nz >= 0 || mode == kBuildTombMatsRead) return kBuildTombUnreadCall;
    return (unreadSoFar + 1 >= maxTries) ? kBuildTombUnreadGiveUp : kBuildTombUnreadRetry;
}
struct BuildLedReader
{
    const std::string& s; size_t at; bool ok;
    explicit BuildLedReader(const std::string& in) : s(in), at(0), ok(true) {}
    unsigned int u8() { if (at + 1 > s.size()) { ok = false; return 0; } return (unsigned char)s[at++]; }
    unsigned int u32() { unsigned int v = 0; for (int i = 0; i < 4; ++i) v |= u8() << (8 * i); return v; }
    std::string str() { const unsigned int n = u8(); if (!ok || at + n > s.size()) { ok = false; return std::string(); } std::string r = s.substr(at, n); at += n; return r; }
private:
    BuildLedReader& operator=(const BuildLedReader&);
};
/* false: not a ledger (the empty STATE of a row before fold 1), or torn - *L is left empty. "LG1" (fold 1), "LG2" (P14 fold 2) and "LG3"
   (P14 fold 3: the flags byte - any non-zero value, an unknown one too, refuses every re-drop; a decoder before fold 3 reads no "LG3",
   so its tombstone has no ledger and refuses too) */
inline bool BuildLedgerDecode(const std::string& in, BuildLedger* L)
{
    *L = BuildLedger();
    if (in.size() < 3 || (in.compare(0, 3, "LG1") != 0 && in.compare(0, 3, "LG2") != 0 && in.compare(0, 3, "LG3") != 0)) return false;
    const bool v3 = (in[2] == '3'), v2 = (in[2] == '2' || v3);
    BuildLedReader r(in);
    r.at = 3;
    BuildLedger x;
    x.missed = (int)r.u32(); x.spill = (int)r.u32();
    if (v3) x.refuse = (unsigned char)(r.u8() != 0 ? 1 : 0);   /* P14 fold 3 (N2): an unknown flag value refuses, never reads as 0 */
    std::vector<std::string> recs;
    const unsigned int nr = r.u8();
    if (nr > kBuildLedgerMaxRecs) return false;
    for (unsigned int i = 0; i < nr && r.ok; ++i) recs.push_back(r.str());
    const unsigned int nf = r.u8();
    if (nf > kBuildLedgerMaxRef) return false;
    for (unsigned int i = 0; i < nf && r.ok; ++i)
    {
        BuildLedgerRef f; f.gen = (unsigned char)(r.u8() != 0 ? 1 : 0); f.qty = (int)r.u32(); f.gkey = r.str();
        if (!r.ok || f.qty < 0 || !BuildGroundKeySid(f.gkey, &f.sid)) return false;
        x.refund.push_back(f);
    }
    const unsigned int ne = r.u8();
    if (ne > kBuildLedgerMaxEsc) return false;
    for (unsigned int i = 0; i < ne && r.ok; ++i)
    {
        BuildLedgerEsc e; e.ref = (unsigned char)r.u8(); e.kind = (unsigned char)r.u8(); e.qty = (int)r.u32();
        const unsigned int lo = r.u32(), hi = r.u32();
        e.seq = ((unsigned long long)hi << 32) | (unsigned long long)lo;
        const unsigned int ri = r.u8();
        if (!r.ok || (e.ref < kBuildLedgerMatRef && (size_t)e.ref >= x.refund.size()) || (!v2 && e.ref >= kBuildLedgerMatRef) || e.qty < 0
            || e.kind > kBuildLedgerEscHolder || (ri != 0xFF && ri >= recs.size())) return false;
        if (ri != 0xFF) e.rec = recs[ri];
        x.esc.push_back(e);
    }
    if (v2)
    {
        const unsigned int nm = r.u8();
        if (nm > kBuildLedgerMaxMat) return false;
        for (unsigned int i = 0; i < nm && r.ok; ++i)
        {
            BuildLedgerMat m; m.sid = r.str(); m.unkeyed = (int)r.u32(); m.blind = (unsigned char)(r.u8() != 0 ? 1 : 0);
            if (!r.ok || m.sid.empty() || m.unkeyed < 0) return false;
            x.mat.push_back(m);
        }
        for (size_t i = 0; i < x.esc.size(); ++i)
            if (x.esc[i].ref >= kBuildLedgerMatRef && (size_t)(x.esc[i].ref - kBuildLedgerMatRef) >= x.mat.size()) return false;
    }
    if (!r.ok || r.at != in.size()) return false;
    *L = x;
    return true;
}

} // namespace coopbuild
