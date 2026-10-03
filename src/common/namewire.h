/* src/common/namewire.h - names1 (investigations/nameplates.md s3, kidnap-cages-save.md s2 Gap 2). A copy of the other
 * player's character showed the name its template rolled here, not the name its owner gave it: MSG_SPAWN carries no name.
 * MSG_NAME (51, reliable) carries the owner's character name - after every SPAWN the owner sends, and when the owner renames
 * one of its characters (the Character::setName 0x5CB840 hook) - and the other game sets it on its copy.
 *
 *   uid u32 | len u8 (0..kNameMax) | len bytes, UTF-8 as the engine stores them (RootObjectBase +0x18, a std::string)
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace coopname {

const size_t kNameMax = 64;   /* bytes; a longer name is cut at a UTF-8 boundary by the sender and counted tooLong */

const int kNameDecodeOk       = 0;
const int kNameDecodeTooShort = 1;
const int kNameDecodeTooLong  = 2;   /* len above kNameMax */

/* The longest prefix of s[0..n) that is at most `cap` bytes and does not split a UTF-8 sequence. */
inline size_t NameCutUtf8(const char* s, size_t n, size_t cap)
{
    if (n <= cap) return n;
    size_t k = cap;
    while (k > 0 && (((unsigned char)s[k]) & 0xC0) == 0x80) --k;   /* s[k] is the first byte dropped: back off to a lead byte */
    return k;
}

/* false (nothing appended) for a name longer than kNameMax. */
inline bool EncodeName(std::vector<char>* b, unsigned int uid, const std::string& name)
{
    if (b == 0 || name.size() > kNameMax) return false;
    const size_t at = b->size();
    b->resize(at + 5 + name.size());
    char* p = &(*b)[at];
    std::memcpy(p, &uid, 4);
    p[4] = (char)(unsigned char)name.size();
    if (!name.empty()) std::memcpy(p + 5, name.data(), name.size());
    return true;
}

inline int DecodeName(const char* p, size_t size, unsigned int* uid, std::string* name)
{
    if (p == 0 || uid == 0 || name == 0 || size < 5) return kNameDecodeTooShort;
    const size_t len = (size_t)(unsigned char)p[4];
    if (len > kNameMax) return kNameDecodeTooLong;
    if (size < 5 + len) return kNameDecodeTooShort;
    std::memcpy(uid, p, 4);
    name->assign(p + 5, len);
    return kNameDecodeOk;
}

} // namespace coopname
