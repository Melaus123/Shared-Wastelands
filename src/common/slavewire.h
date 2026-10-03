/* src/common/slavewire.h - slave1 (T337/T341, Confirmed: a slave owned by game A read SlaveStateEnum 1 on A all run while
 * B's copy switched to 2 ESCAPING on its own). MSG_SLAVE (52, reliable) carries the owner's slave state for one character -
 * after every SPAWN the owner sends, and when the owner's engine changes it (StateBroadcastData::setSlaveState 0x5A3EB0 and
 * the direct writes in StateBroadcastData::periodicUpdate 0x5A44C0) - and the other game sets it on its copy.
 *
 *   uid u32 | state u8 (SlaveStateEnum: 0 NOT_SLAVE, 1 IS_SLAVE, 2 ESCAPING_SLAVE, 3 EX_SLAVE) | ownerUid u32
 *
 * P11 (protocol 101): ownerUid = the replicated uid of the character the slave-owner hand (Character +0x328) names;
 * 0 = none, kSlaveOwnerUnknown = an owner that is not a replicated character (the receiver leaves its copy's hand alone).
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <vector>

namespace coopslave {

const int    kSlaveStateMax = 3;   /* EX_SLAVE */
const size_t kSlaveWireSize = 9;   /* P11 (protocol 101): + u32 ownerUid */
const unsigned int kSlaveOwnerUnknown = 0xFFFFFFFFu;

const int kSlaveDecodeOk       = 0;
const int kSlaveDecodeTooShort = 1;
const int kSlaveDecodeBadState = 2;   /* state byte above kSlaveStateMax */

/* false (nothing appended) for a state outside 0..kSlaveStateMax. */
inline bool EncodeSlave(std::vector<char>* b, unsigned int uid, int state, unsigned int ownerUid)
{
    if (b == 0 || state < 0 || state > kSlaveStateMax) return false;
    const size_t at = b->size();
    b->resize(at + kSlaveWireSize);
    char* p = &(*b)[at];
    std::memcpy(p, &uid, 4);
    p[4] = (char)(unsigned char)state;
    std::memcpy(p + 5, &ownerUid, 4);
    return true;
}

/* Nothing is written on a refusal. */
inline int DecodeSlave(const char* p, size_t size, unsigned int* uid, int* state, unsigned int* ownerUid)
{
    if (p == 0 || uid == 0 || state == 0 || ownerUid == 0 || size < kSlaveWireSize) return kSlaveDecodeTooShort;
    const int s = (int)(unsigned char)p[4];
    if (s > kSlaveStateMax) return kSlaveDecodeBadState;
    std::memcpy(uid, p, 4);
    *state = s;
    std::memcpy(ownerUid, p + 5, 4);
    return kSlaveDecodeOk;
}

} // namespace coopslave
