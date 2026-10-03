/* src/common/hirewire.h - recruit1 R0 (docs/design-recruit1.md 4). MSG_HIRE (55, reliable): hiring a recruit the OTHER game
 * runs. The hirer asks (REQ), the owner answers once (OK / NO) after its live check, the hirer takes the person and hires it,
 * then says what happened (DONE). An owner's own local hire also sends DONE {taken 0, joined 1} so the other game re-files
 * its copy.
 *
 *   kind u8 (1 REQ, 2 OK, 3 NO, 4 DONE) | reqId u32 | uid u32 | hirerUid u32 | price i32 | joinType u8 (3 or 18)
 *   | reason u8 | taken u8 (0/1) | joined u8 (0/1) | len u8 (<= 64) + the person's faction name as the hirer saw it
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace coophire {

const int kHireReq  = 1;
const int kHireOk   = 2;
const int kHireNo   = 3;
const int kHireDone = 4;

/* NO reasons (the owner's live check, design 1 step 4) */
const int kNoNone     = 0;
const int kNoNotMine  = 1;   /* the uid is not run by the answering game */
const int kNoGone     = 2;   /* no plausible character for the uid */
const int kNoDowned   = 3;   /* dead, knocked out or otherwise down */
const int kNoFaction  = 4;   /* already in a player / peer faction, or not the faction the hirer saw */
const int kNoCombat   = 5;   /* in combat mode */
const int kNoReserved = 6;   /* already promised to an earlier request */
const int kNoCaged    = 7;   /* in a cage / prison mode (Character+0x2F8 inSomething == 2) */
const int kNoSlave    = 8;   /* slave state != 0 (slave1's reader) */
const int kNoBlocked  = 9;   /* reserved: the owner could not write (never sent in R0-a - it holds instead) */
const int kNoReasonMax = 9;

const size_t kHireFixedSize = 1 + 4 + 4 + 4 + 4 + 1 + 1 + 1 + 1 + 1;   /* 22 */
const size_t kHireNameMax = 64;
const size_t kHireGenSize = 4;   /* M7a A1 build 1 [a1b1-hw4]: the trailing u32 gen */

const int kHireDecodeOk        = 0;
const int kHireDecodeTooShort  = 1;
const int kHireDecodeBadKind   = 2;
const int kHireDecodeBadField  = 3;   /* joinType not 3/18, reason above max, taken/joined above 1 */
const int kHireDecodeBadLength = 4;   /* name length over the max, or past the end */

struct HireMsg
{
    int          kind;
    unsigned int reqId;
    unsigned int uid;
    unsigned int hirerUid;
    int          price;
    int          joinType;
    int          reason;
    int          taken;
    int          joined;
    std::string  faction;
    unsigned int gen;   /* M7a A1 build 1 [a1b1-hw0] (protocol 118): OK - the owner's generation for uid; DONE taken=1 - the hirer's (it holds max(OK's, its copy record) + 1); else 0 */
    HireMsg() : kind(0), reqId(0), uid(0), hirerUid(0), price(0), joinType(18), reason(0), taken(0), joined(0), gen(0) {}
};

inline bool HireFieldsValid(const HireMsg& m)
{
    if (m.kind < kHireReq || m.kind > kHireDone) return false;
    if (m.joinType != 3 && m.joinType != 18) return false;
    if (m.reason < 0 || m.reason > kNoReasonMax) return false;
    if (m.taken < 0 || m.taken > 1 || m.joined < 0 || m.joined > 1) return false;
    return m.faction.size() <= kHireNameMax;
}

/* false (nothing appended) when a field is out of range. */
inline bool EncodeHire(std::vector<char>* b, const HireMsg& m)
{
    if (b == 0 || !HireFieldsValid(m)) return false;
    const size_t at = b->size();
    b->resize(at + kHireFixedSize + m.faction.size() + kHireGenSize);   /* [a1b1-hw1]: + the u32 gen tail */
    char* p = &(*b)[at];
    p[0] = (char)(unsigned char)m.kind;
    std::memcpy(p + 1, &m.reqId, 4);
    std::memcpy(p + 5, &m.uid, 4);
    std::memcpy(p + 9, &m.hirerUid, 4);
    std::memcpy(p + 13, &m.price, 4);
    p[17] = (char)(unsigned char)m.joinType;
    p[18] = (char)(unsigned char)m.reason;
    p[19] = (char)(unsigned char)m.taken;
    p[20] = (char)(unsigned char)m.joined;
    p[21] = (char)(unsigned char)m.faction.size();
    if (!m.faction.empty()) std::memcpy(p + kHireFixedSize, m.faction.data(), m.faction.size());
    std::memcpy(p + kHireFixedSize + m.faction.size(), &m.gen, 4);   /* [a1b1-hw2] */
    return true;
}

/* WHERE A DONE GOES. A DONE that moved the person (taken: the hirer runs it now) or put it in a player's faction (joined) goes to EVERY
   game: the owner releases it, and every other game moves its copy into the hirer's faction (hire.cpp Finish is the only place a copy's
   faction follows a hire - the owner-moved word carries the owner, not the faction). A DONE that moved nothing (an abandoned take, a
   late OK) only clears the owner's promise and goes back to the asker alone. 1 = every game. */
inline int HireDoneToEveryGame(int taken, int joined)
{
    return (taken != 0 || joined != 0) ? 1 : 0;
}

/* Nothing is written on a refusal. */
inline int DecodeHire(const char* p, size_t size, HireMsg* out)
{
    if (p == 0 || out == 0 || size < kHireFixedSize) return kHireDecodeTooShort;
    HireMsg m;
    m.kind = (int)(unsigned char)p[0];
    if (m.kind < kHireReq || m.kind > kHireDone) return kHireDecodeBadKind;
    std::memcpy(&m.reqId, p + 1, 4);
    std::memcpy(&m.uid, p + 5, 4);
    std::memcpy(&m.hirerUid, p + 9, 4);
    std::memcpy(&m.price, p + 13, 4);
    m.joinType = (int)(unsigned char)p[17];
    m.reason = (int)(unsigned char)p[18];
    m.taken = (int)(unsigned char)p[19];
    m.joined = (int)(unsigned char)p[20];
    const size_t len = (size_t)(unsigned char)p[21];
    if (len > kHireNameMax || kHireFixedSize + len + kHireGenSize != size) return kHireDecodeBadLength;   /* [a1b1-hw3]: exactly the name and the gen tail */
    m.faction.assign(p + kHireFixedSize, len);
    std::memcpy(&m.gen, p + kHireFixedSize + len, 4);
    if (!HireFieldsValid(m)) return kHireDecodeBadField;
    *out = m;
    return kHireDecodeOk;
}

} // namespace coophire
