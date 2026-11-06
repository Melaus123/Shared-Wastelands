/* src/common/hitwire.h - the decided blow on MSG_HIT, the attack move on MSG_SWING and MSG_SWING_END (game-to-game protocol 185).
 *
 * The victim's owner decides each melee blow through its own engine (combat.cpp detour_hitByMelee) and reports it on HIT; every
 * other game plays the received blow through its own engine on the victim's copy (combat.cpp ApplyRemoteHit). That engine would
 * roll the block and pick the stumble again by its own dice, so the HIT carries what the deciding game's engine chose and the
 * replaying game plays that:
 *   result   - what the deciding hit answered (meleewire.h: 0 dodged / no hit, 2 landed, 5 blocked);
 *   move     - the attacker's attack move, block - the victim's block move, stumble - the stumble the hit played; each by name.
 * Each SWING carries the attack move its owner's swing started, so the copy plays the same move when its own list of moves
 * for that gap holds it.
 *
 * HIT, after the attackerPlayed byte:
 *   tag u8 (kBlowTag) | result u8 | flags u8 | moveLen u8 | move | blockLen u8 | block | stumbleLen u8 | stumble
 * SWING, after uid u32 | targetUid u32:
 *   tag u8 (kSwingTag) | moveLen u8 | move
 * SWING_END (81): the owner's swing that sent SWING ended - uid u32 | targetUid u32 (the target its SWING named), little-endian,
 *   | end u8 (what the swing met on the owner's game, kSwingEnd*), kSwingEndBytes; bytes after them are ignored. uid 0, or a uid
 *   swinging at itself, is refused (kDecodeBadUid); target 0 = none; an end word above kSwingEndUntouched is refused (kDecodeBadEnd).
 * A name is 0..kNameMax bytes, each printable ASCII (0x20..0x7E), no NUL on the wire; length 0 = none (no move, no block, no
 * stumble). flags bit 0 (kBlowStumbleKnown): the stumble field is the deciding game's answer (with length 0: the hit played no
 * stumble); clear = the deciding game could not read it, and the replaying game lets its own engine choose. Other flag bits
 * are 0.
 *
 * Decode answers kDecodeAbsent when the block is missing (a HIT the sender did not decide through its engine, or a SWING whose
 * move could not be read), and refuses one that carries another tag, is cut short, names a name longer than kNameMax or with
 * an unprintable byte, carries unknown flag bits or a result other than 0 / 2 / 5. Nothing is written on any answer but
 * kDecodeOk; the caller plays the blow as its own engine decides.
 *
 * Pure: no engine memory, no Windows; the offline suite runs the same functions. C++03 (VS2010 v100).
 */
#pragma once

#include <cstddef>
#include <cstring>
#include <vector>

namespace hitwire {

const int           kNameMax  = 48;
const int           kNameBuf  = kNameMax + 1;
const unsigned char kBlowTag  = 0xB1;
const unsigned char kSwingTag = 0xB2;
const unsigned char kBlowStumbleKnown = 1;

const int kDecodeOk        = 0;
const int kDecodeAbsent    = 1;   /* no block at all: not carried */
const int kDecodeBadTag    = 2;
const int kDecodeTruncated = 3;
const int kDecodeBadName   = 4;
const int kDecodeBadResult = 5;
const int kDecodeBadFlags  = 6;
const int kDecodeBadUid    = 7;   /* SWING_END: uid 0, or the uid its own target */
const int kDecodeBadEnd    = 8;   /* SWING_END: an end word this build does not know */

struct Blow
{
    int  result;                  /* 0 / 2 / 5 */
    int  stumbleKnown;            /* 1: stumble is the deciding game's answer ("" = it played none); 0: not known */
    char move[kNameBuf];          /* "" = none */
    char block[kNameBuf];
    char stumble[kNameBuf];
};

inline bool ResultOk(int r) { return r == 0 || r == 2 || r == 5; }

/* 0..kNameMax printable ASCII bytes, NUL-terminated within kNameBuf. */
inline bool NameOk(const char* s)
{
    if (s == 0) return false;
    for (int i = 0; i < kNameBuf; ++i)
    {
        const unsigned char ch = (unsigned char)s[i];
        if (ch == 0) return true;
        if (i >= kNameMax || ch < 0x20 || ch > 0x7E) return false;
    }
    return false;
}

inline void ClearBlow(Blow* w)
{
    std::memset(w, 0, sizeof(*w));
}

/* out (kNameBuf bytes) = src when NameOk(src), else "" and false. */
inline bool SetName(char* out, const char* src)
{
    out[0] = 0;
    if (!NameOk(src)) return false;
    const size_t n = std::strlen(src);
    std::memcpy(out, src, n);
    out[n] = 0;
    return true;
}

inline bool BlowOk(const Blow& w)
{
    return ResultOk(w.result) && (w.stumbleKnown == 0 || w.stumbleKnown == 1) && NameOk(w.move) && NameOk(w.block)
        && NameOk(w.stumble) && (w.stumbleKnown == 1 || w.stumble[0] == 0);
}

inline void PutName(std::vector<char>* b, const char* s)
{
    const size_t n = std::strlen(s);
    b->push_back((char)(unsigned char)n);
    b->insert(b->end(), s, s + n);
}

/* One name at p[*at], at most size; 0 kDecodeOk (out filled, *at moved past it) or a refusal. */
inline int TakeName(const char* p, size_t size, size_t* at, char* out)
{
    if (*at >= size) return kDecodeTruncated;
    const size_t n = (unsigned char)p[*at];
    if (n > (size_t)kNameMax) return kDecodeBadName;
    if (*at + 1 + n > size) return kDecodeTruncated;
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char ch = (unsigned char)p[*at + 1 + i];
        if (ch < 0x20 || ch > 0x7E) return kDecodeBadName;
        out[i] = (char)ch;
    }
    out[n] = 0;
    *at += 1 + n;
    return kDecodeOk;
}

/* false (nothing appended) for a blow that would not decode. */
inline bool EncodeBlow(std::vector<char>* b, const Blow& w)
{
    if (b == 0 || !BlowOk(w)) return false;
    b->push_back((char)kBlowTag);
    b->push_back((char)(unsigned char)w.result);
    b->push_back((char)(w.stumbleKnown ? kBlowStumbleKnown : 0));
    PutName(b, w.move);
    PutName(b, w.block);
    PutName(b, w.stumble);
    return true;
}

/* p / size: the HIT's bytes after the attackerPlayed byte (size 0 = absent). Bytes after the block are ignored. */
inline int DecodeBlow(const char* p, size_t size, Blow* out)
{
    if (p == 0 || size == 0) return kDecodeAbsent;
    if ((unsigned char)p[0] != kBlowTag) return kDecodeBadTag;
    if (size < 3) return kDecodeTruncated;
    Blow w;
    ClearBlow(&w);
    w.result = (unsigned char)p[1];
    if (!ResultOk(w.result)) return kDecodeBadResult;
    const unsigned char flags = (unsigned char)p[2];
    if ((flags & ~kBlowStumbleKnown) != 0) return kDecodeBadFlags;
    w.stumbleKnown = (flags & kBlowStumbleKnown) ? 1 : 0;
    size_t at = 3;
    int rc = TakeName(p, size, &at, w.move);
    if (rc == kDecodeOk) rc = TakeName(p, size, &at, w.block);
    if (rc == kDecodeOk) rc = TakeName(p, size, &at, w.stumble);
    if (rc != kDecodeOk) return rc;
    if (!w.stumbleKnown && w.stumble[0] != 0) return kDecodeBadFlags;
    if (out != 0) *out = w;
    return kDecodeOk;
}

/* false (nothing appended) for a move name that would not decode; an empty name appends nothing (absent). */
inline bool EncodeSwingMove(std::vector<char>* b, const char* move)
{
    if (b == 0 || !NameOk(move)) return false;
    if (move[0] == 0) return true;
    b->push_back((char)kSwingTag);
    PutName(b, move);
    return true;
}

/* p / size: the SWING's bytes after targetUid (size 0 = absent). out: kNameBuf bytes, written only on kDecodeOk. */
inline int DecodeSwingMove(const char* p, size_t size, char* out)
{
    if (p == 0 || size == 0) return kDecodeAbsent;
    if ((unsigned char)p[0] != kSwingTag) return kDecodeBadTag;
    char name[kNameBuf];
    size_t at = 1;
    const int rc = TakeName(p, size, &at, name);
    if (rc != kDecodeOk) return rc;
    if (out != 0) std::memcpy(out, name, kNameBuf);
    return kDecodeOk;
}

const size_t kSwingEndBytes = 9;

/* SWING_END's end word: what the owner's swing met on the owner's game, for its copies' getting-ready pause after the swing
   (fightready.h CopyDecide; the same numbers as fightready::kOwnerEnd*, the suite checks it). */
const unsigned char kSwingEndUnknown   = 0;   /* the owner's game cannot tell */
const unsigned char kSwingEndResult    = 1;   /* its last contact got the victim's game's result, or was a hit the owner's game ran */
const unsigned char kSwingEndNoResult  = 2;   /* it touched a character whose game decides the blow, and no result came by its end */
const unsigned char kSwingEndUntouched = 3;   /* it touched nothing */

/* false (nothing appended) for uid 0, a uid that is its own target, or an end word above kSwingEndUntouched. */
inline bool EncodeSwingEnd(std::vector<char>* b, unsigned int uid, unsigned int targetUid, unsigned char end)
{
    if (b == 0 || uid == 0 || uid == targetUid || end > kSwingEndUntouched) return false;
    for (int i = 0; i < 4; ++i) b->push_back((char)(unsigned char)(uid >> (8 * i)));
    for (int i = 0; i < 4; ++i) b->push_back((char)(unsigned char)(targetUid >> (8 * i)));
    b->push_back((char)end);
    return true;
}

/* uid / targetUid / end written only on kDecodeOk. */
inline int DecodeSwingEnd(const char* p, size_t size, unsigned int* uid, unsigned int* targetUid, unsigned char* end)
{
    if (p == 0 || size < kSwingEndBytes) return kDecodeTruncated;
    unsigned int u = 0, g = 0;
    for (int i = 3; i >= 0; --i) { u = (u << 8) | (unsigned char)p[i]; g = (g << 8) | (unsigned char)p[4 + i]; }
    if (u == 0 || u == g) return kDecodeBadUid;
    const unsigned char e = (unsigned char)p[8];
    if (e > kSwingEndUntouched) return kDecodeBadEnd;
    if (uid != 0) *uid = u;
    if (targetUid != 0) *targetUid = g;
    if (end != 0) *end = e;
    return kDecodeOk;
}

/* A [FIGHT]-line name (fightlog.h: '-' no object, '?' unreadable) as a wire name: "" for '-' (known: none), "" and false for
   '?' or anything NameOk refuses (not known). */
inline bool WireNameOf(const char* fightName, char* out)
{
    out[0] = 0;
    if (fightName == 0 || (fightName[0] == '?' && fightName[1] == 0)) return false;
    if (fightName[0] == '-' && fightName[1] == 0) return true;
    return SetName(out, fightName);
}

}   /* namespace hitwire */
