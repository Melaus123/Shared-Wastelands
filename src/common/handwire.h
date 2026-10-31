/* src/common/handwire.h - the item a character holds in its hands, carried from the game that runs it to its copies
 * (MSG_HAND 84, game-to-game protocol 186).
 *
 * THE FACT. The owner's hand item is the engine's weaponInHands (CharacterHuman +0x6D8). A draw leaves the item in its inventory
 * section (marked in hands, attached to the hands bone - decomp_5db4f0.txt:62, :96-105, :106), so the item never travels as an item
 * move: the copy's own item is already in the same section at the same slot once the item moves have arrived, and the copy draws
 * it through the engine's own draw (Character vtable +0x3D8). Bare hands - no item, or the engine's own fists object (+0x6D0,
 * decomp_5db4f0.txt:32-35) - send 'empty', and a copy holding an item puts it away through the engine's own put-away (vtable
 * +0x2D0, decomp_5cbd90.txt). The item is named the way an item move names it: section name + slot x,y + the item's base sid.
 *
 * MSG_HAND (RELIABLE; the game that runs the character -> every game in its area, on the character stream's road; taken only for
 * a copy, from the game that runs it). Sent once per change of the owner's hand item. Little-endian:
 *     uid u32 | has u8 (0 empty hands, 1 an item) | when has = 1: secLen u8 | section | x i32 | y i32 | sidLen u8 | sid
 * Bytes after the last field are ignored.
 *
 * THE COPY'S SIDE (Decide), once a safe-point pass per copy: the wanted item found in the copy's own section and not in its hands
 * -> draw; the owner's hands empty and the copy holding an item -> put away; the wanted item not (yet) in the copy's section ->
 * wait (it arrives by an item move); a hand the copy's own engine changed back after this game set it is set again, at most
 * kRevertCap times in a row; the count starts again when the copy's own planner is held again after its fight window, or after
 * kRevertQuietPasses passes with no re-apply (RearmReverts).
 *
 * THE OWNER'S SIDE: a change of the hand item, or of its section or slot, is sent; a catch-up or announce resend sends the
 * current word too, bare hands included (OwnerWord, ResendSends).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include <cstring>
#include <vector>

namespace handwire {

const unsigned int kMsgHand = 84;   /* net/transport.h MSG_HAND, liveenvelope.h kInnerHand, spawnpace.h kMsgHand (store.cpp asserts) */
const int kSecMax     = 31;   /* section name bytes on the wire */
const int kSidMax     = 47;   /* base sid bytes on the wire (items.cpp kSidCap 48 with its terminator) */
const int kDrawSecMax = 15;   /* the engine's draw takes the section name by value: at most 15 bytes stays in the string's own buffer */
const int kRevertCap  = 20;   /* re-applies in a row before the copy's own hand is left alone */
/* safe-point passes (one per frame: about 10 s at 60 frames a second) with no re-apply before the count starts again. A planner
   that redraws every frame spends the 20 re-applies in about a third of a second, so a re-arm every 10 s keeps the contest to a
   few percent of frames; a planner that has settled gets the owner's item back within 10 s; a re-apply every few seconds never
   adds up to the cap. */
const unsigned int kRevertQuietPasses = 600;

const int kDecodeOk       = 0;
const int kDecodeTooShort = 1;
const int kDecodeBadUid   = 2;   /* uid 0 */
const int kDecodeBadHas   = 3;   /* has not 0 / 1 */
const int kDecodeBadText  = 4;   /* an item with an empty or overlong section or sid */

struct Hand
{
    unsigned int uid;
    int  has;                      /* 1 an item in hands, 0 bare hands */
    char section[kSecMax + 1];     /* has = 1: the inventory section the item is in */
    int  x, y;                     /* has = 1: its slot in that section */
    char sid[kSidMax + 1];         /* has = 1: the item's base record sid */
};

inline void Clear(Hand* h)
{
    h->uid = 0; h->has = 0; h->section[0] = 0; h->x = 0; h->y = 0; h->sid[0] = 0;
}

/* the same owner word: both bare, or the same section, slot and sid */
inline bool Same(const Hand& a, const Hand& b)
{
    if (a.has != b.has) return false;
    if (a.has == 0) return true;
    return a.x == b.x && a.y == b.y && std::strcmp(a.section, b.section) == 0 && std::strcmp(a.sid, b.sid) == 0;
}

inline bool TextOk(const char* s, int cap)
{
    const size_t n = std::strlen(s);
    return n > 0 && n <= (size_t)cap;
}

inline bool HandOk(const Hand& h)
{
    if (h.uid == 0 || (h.has != 0 && h.has != 1)) return false;
    return h.has == 0 || (TextOk(h.section, kSecMax) && TextOk(h.sid, kSidMax));
}

inline void PutU32(std::vector<char>* b, unsigned int v) { for (int i = 0; i < 4; ++i) b->push_back((char)((v >> (8 * i)) & 0xFF)); }
inline unsigned int GetU32(const char* p)
{
    unsigned int v = 0;
    for (int i = 0; i < 4; ++i) v |= (unsigned int)(unsigned char)p[i] << (8 * i);
    return v;
}
inline void PutText(std::vector<char>* b, const char* s)
{
    const size_t n = std::strlen(s);
    b->push_back((char)(unsigned char)n);
    b->insert(b->end(), s, s + n);
}

/* false: the word is not sendable (uid 0, or an item with an empty or overlong name) */
inline bool Encode(std::vector<char>* out, const Hand& h)
{
    out->clear();
    if (!HandOk(h)) return false;
    PutU32(out, h.uid);
    out->push_back((char)(h.has ? 1 : 0));
    if (h.has)
    {
        PutText(out, h.section);
        PutU32(out, (unsigned int)h.x);
        PutU32(out, (unsigned int)h.y);
        PutText(out, h.sid);
    }
    return true;
}

/* reads one length-prefixed name of at most cap bytes at *at; false: cut, empty or overlong */
inline bool GetText(const char* p, size_t size, size_t* at, char* out, int cap)
{
    if (*at >= size) return false;
    const size_t n = (size_t)(unsigned char)p[*at];
    ++*at;
    if (n == 0 || n > (size_t)cap || size - *at < n) return false;
    std::memcpy(out, p + *at, n);
    out[n] = 0;
    *at += n;
    return true;
}

inline int Decode(const char* p, size_t size, Hand* h)
{
    Clear(h);
    if (p == 0 || size < 5) return kDecodeTooShort;
    h->uid = GetU32(p);
    if (h->uid == 0) return kDecodeBadUid;
    const unsigned char has = (unsigned char)p[4];
    if (has > 1) return kDecodeBadHas;
    h->has = (int)has;
    if (has == 0) return kDecodeOk;
    size_t at = 5;
    if (at >= size) return kDecodeTooShort;
    const size_t secLen = (size_t)(unsigned char)p[at];
    if (secLen == 0 || secLen > (size_t)kSecMax) return kDecodeBadText;
    if (!GetText(p, size, &at, h->section, kSecMax)) return kDecodeTooShort;
    if (size - at < 8) return kDecodeTooShort;
    h->x = (int)GetU32(p + at); at += 4;
    h->y = (int)GetU32(p + at); at += 4;
    if (at >= size) return kDecodeTooShort;
    const size_t sidLen = (size_t)(unsigned char)p[at];
    if (sidLen == 0 || sidLen > (size_t)kSidMax) return kDecodeBadText;
    if (!GetText(p, size, &at, h->sid, kSidMax)) return kDecodeTooShort;
    return kDecodeOk;
}

/* ---- the copy's side ---- */
const int kHeldNone  = 0;   /* bare hands: no item, or the engine's fists object */
const int kHeldWant  = 1;   /* the wanted item (the one found in the copy's section) */
const int kHeldOther = 2;   /* another item */

const int kActNone    = 0;   /* the copy's hands already match */
const int kActDraw    = 1;   /* draw the found item */
const int kActPutAway = 2;   /* put the held item away */
const int kActWait    = 3;   /* the wanted item is not in the copy's section yet */
const int kActCapped  = 4;   /* changed back kRevertCap times under this word: the copy's own hand is left */

/* wantHas: the owner's word; held: kHeld*; found: the wanted item was found in the copy's section (wantHas only); acted: this game
   already drew / put away under this word, so a difference now is the copy's own engine changing it back; reverts: re-applies
   under this word so far. */
inline int Decide(int wantHas, int held, int found, int acted, int reverts)
{
    if (wantHas)
    {
        if (held == kHeldWant) return kActNone;
        if (!found) return kActWait;
    }
    else if (held == kHeldNone) return kActNone;
    if (acted && reverts >= kRevertCap) return kActCapped;
    return wantHas ? kActDraw : kActPutAway;
}

/* true when this action is a re-apply (counted against kRevertCap) */
inline bool IsRevert(int act, int acted) { return acted != 0 && (act == kActDraw || act == kActPutAway); }

/* true: the re-apply count starts again - the copy's own planner was let run (released 1: its fight window) and is held again
   (released 0), or kRevertQuietPasses passes went by since the last re-apply (the one that reached the cap included). released
   -1: not known. */
inline bool RearmReverts(int reverts, int releasedBefore, int releasedNow, unsigned int passesSinceRevert)
{
    if (reverts <= 0) return false;
    if (releasedBefore == 1 && releasedNow == 0) return true;
    return passesSinceRevert >= kRevertQuietPasses;
}

/* ---- the owner's side ---- */
const int kWordUnread  = 0;   /* the hand could not be read: nothing sent */
const int kWordAnimal  = 1;   /* an animal's natural weapons: never sent */
const int kWordBare    = 2;   /* bare hands */
const int kWordItem    = 3;   /* an item named by section, slot and sid */
const int kWordUnnamed = 4;   /* an item whose section, slot or sid could not be read: nothing sent */

inline int OwnerWord(int readable, int animal, int hasItem, int named)
{
    if (!readable) return kWordUnread;
    if (animal) return kWordAnimal;
    if (!hasItem) return kWordBare;
    return named ? kWordItem : kWordUnnamed;
}

/* a catch-up / announce resend sends bare hands as well as an item: a copy built with bare hands loses nothing, a copy kept from
   before is corrected */
inline bool ResendSends(int word) { return word == kWordBare || word == kWordItem; }

}   /* namespace handwire */
