/* src/common/givewire.h - T-1135 (owner 1349 a / 1350 a): a player gives one or several of their own characters to another player.
 *
 * THE ROAD. Three message kinds ride MSG_HIRE (55, reliable, game to game, addressed by slot) after the hire handshake's kinds 1-4:
 *   OFFER  (giver -> receiver)   the offer id, the characters' uids and names;
 *   ANSWER (receiver -> giver)   accept / refuse / timeout (the box closed itself after 60 s) / failed (the receiver's game could not
 *                                take them; index = the first one) / done (taken: the uids that joined, the rest could not be given);
 *   RESULT (giver -> receiver)   go (every character passed the giver's live check at the accept; each uid's ownership generation for
 *                                the take) / failed (index = the first one that no longer qualifies) / over (the giver's offer ended
 *                                with no answer: it ran out).
 * The hand-over itself is the hire road's take: the receiver's game takes each person over, recruits it with the engine's own
 * PlayerInterface::recruit at no price and sends the hire DONE {taken 1, joined} with the offer id to every game; the giver's game
 * releases the person and moves its copy into the receiver's stand-in, every other game moves its copy too (hire.cpp Finish).
 * All or nothing: the giver checks every character at one instant (the accept), the receiver checks every copy at one instant (the
 * go) before taking any.
 *
 *   kind u8 (5 OFFER, 6 ANSWER, 7 RESULT) | offerId u32 | giverSlot u8 | receiverSlot u8 | word u8 | index u8 | n u8 (<= kGiveMax)
 *   | n x ( uid u32 | gen u32 | len u8 (<= 64) + name )
 *
 * Also here: what the PLAYERS tab's GIVE button shows (each condition greys it and names who is blocking), the boxes' and the grey
 * chat lines' words (build/mockups-t1135.md, exactly), the 60 s limit and the guard exception that lets a recruit of another player's
 * character through only for an accepted offer.
 *
 * Pure: no engine memory, no Windows; the offline suite tests the same decisions. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace swgive {

/* the kinds, after coophire's 1-4 on the same message */
const int kGiveOffer  = 5;
const int kGiveAnswer = 6;
const int kGiveResult = 7;

/* ANSWER words */
const int kAnsAccept  = 1;
const int kAnsRefuse  = 2;
const int kAnsTimeout = 3;
const int kAnsFailed  = 4;
const int kAnsDone    = 5;
/* RESULT words */
const int kResGo     = 1;
const int kResFailed = 2;
const int kResOver   = 3;

const int kGiveMax = 32;                       /* characters in one offer */
const size_t kGiveNameMax = 64;
const size_t kGiveFixedSize = 1 + 4 + 1 + 1 + 1 + 1 + 1;   /* 10 */
const unsigned long kOfferMs = 60000;          /* the receiver's box closes itself after 60 s */
const unsigned long kGraceMs = 10000;          /* the giver waits this much longer for the answer before its offer runs out */
const int kNoIndex = 255;                      /* index: no one character (the offer itself) */

const int kGiveDecodeOk        = 0;
const int kGiveDecodeTooShort  = 1;
const int kGiveDecodeBadKind   = 2;
const int kGiveDecodeBadField  = 3;
const int kGiveDecodeBadLength = 4;

struct GiveMsg
{
    int kind;
    unsigned int offerId;
    int giverSlot;
    int receiverSlot;
    int word;
    int index;
    std::vector<unsigned int> uids;
    std::vector<unsigned int> gens;            /* RESULT go: each uid's generation on the giver's game; else 0s */
    std::vector<std::string> names;
    GiveMsg() : kind(0), offerId(0), giverSlot(-1), receiverSlot(-1), word(0), index(kNoIndex) {}
};

/* the first byte of a MSG_HIRE payload names a give kind */
inline bool IsGiveKind(int firstByte) { return firstByte >= kGiveOffer && firstByte <= kGiveResult; }

inline bool GiveFieldsValid(const GiveMsg& m)
{
    if (!IsGiveKind(m.kind) || m.offerId == 0) return false;
    if (m.giverSlot < 0 || m.giverSlot > 254 || m.receiverSlot < 0 || m.receiverSlot > 254 || m.giverSlot == m.receiverSlot) return false;
    if (m.index < 0 || m.index > kNoIndex) return false;
    if (m.uids.size() > (size_t)kGiveMax) return false;
    if (!m.gens.empty() && m.gens.size() != m.uids.size()) return false;
    if (!m.names.empty() && m.names.size() != m.uids.size()) return false;
    for (size_t i = 0; i < m.uids.size(); ++i) if (m.uids[i] == 0) return false;
    for (size_t i = 0; i < m.names.size(); ++i) if (m.names[i].size() > kGiveNameMax) return false;
    if (m.kind == kGiveOffer) return m.word == 0 && !m.uids.empty();
    if (m.kind == kGiveAnswer) return m.word >= kAnsAccept && m.word <= kAnsDone;
    return m.word >= kResGo && m.word <= kResOver && (m.word != kResGo || !m.uids.empty());
}

/* false (nothing appended) when a field is out of range */
inline bool EncodeGive(std::vector<char>* b, const GiveMsg& m)
{
    if (b == 0 || !GiveFieldsValid(m)) return false;
    size_t size = kGiveFixedSize;
    for (size_t i = 0; i < m.uids.size(); ++i) size += 4 + 4 + 1 + (m.names.empty() ? 0 : m.names[i].size());
    const size_t at = b->size();
    b->resize(at + size);
    char* p = &(*b)[at];
    p[0] = (char)(unsigned char)m.kind;
    std::memcpy(p + 1, &m.offerId, 4);
    p[5] = (char)(unsigned char)m.giverSlot;
    p[6] = (char)(unsigned char)m.receiverSlot;
    p[7] = (char)(unsigned char)m.word;
    p[8] = (char)(unsigned char)m.index;
    p[9] = (char)(unsigned char)m.uids.size();
    size_t o = kGiveFixedSize;
    for (size_t i = 0; i < m.uids.size(); ++i)
    {
        const unsigned int gen = m.gens.empty() ? 0u : m.gens[i];
        const std::string name = m.names.empty() ? std::string() : m.names[i];
        std::memcpy(p + o, &m.uids[i], 4); o += 4;
        std::memcpy(p + o, &gen, 4); o += 4;
        p[o++] = (char)(unsigned char)name.size();
        if (!name.empty()) { std::memcpy(p + o, name.data(), name.size()); o += name.size(); }
    }
    return true;
}

/* Nothing is written on a refusal. The gens and names always come back one per uid. */
inline int DecodeGive(const char* p, size_t size, GiveMsg* out)
{
    if (p == 0 || out == 0 || size < kGiveFixedSize) return kGiveDecodeTooShort;
    GiveMsg m;
    m.kind = (int)(unsigned char)p[0];
    if (!IsGiveKind(m.kind)) return kGiveDecodeBadKind;
    std::memcpy(&m.offerId, p + 1, 4);
    m.giverSlot = (int)(unsigned char)p[5];
    m.receiverSlot = (int)(unsigned char)p[6];
    m.word = (int)(unsigned char)p[7];
    m.index = (int)(unsigned char)p[8];
    const int n = (int)(unsigned char)p[9];
    if (n > kGiveMax) return kGiveDecodeBadField;
    size_t o = kGiveFixedSize;
    for (int i = 0; i < n; ++i)
    {
        if (o + 9 > size) return kGiveDecodeBadLength;
        unsigned int uid = 0, gen = 0;
        std::memcpy(&uid, p + o, 4); o += 4;
        std::memcpy(&gen, p + o, 4); o += 4;
        const size_t len = (size_t)(unsigned char)p[o++];
        if (len > kGiveNameMax || o + len > size) return kGiveDecodeBadLength;
        m.uids.push_back(uid); m.gens.push_back(gen); m.names.push_back(std::string(p + o, len));
        o += len;
    }
    if (o != size) return kGiveDecodeBadLength;
    if (!GiveFieldsValid(m)) return kGiveDecodeBadField;
    *out = m;
    return kGiveDecodeOk;
}

/* ---- the conditions: what keeps one character from being given ---- */
const int kBlockNone    = 0;
const int kBlockOut     = 1;   /* knocked out */
const int kBlockFight   = 2;   /* in combat mode */
const int kBlockCarried = 3;
const int kBlockBed     = 4;
const int kBlockCage    = 5;
const int kBlockGone    = 6;   /* not this game's own character any more, not loaded, dead, promised elsewhere, or a state that cannot be read */

/* knockedOut / dead: the character's two bytes (1 / 0); fight: combat mode 1 / 0; carried 1 / 0; inSomething: 1 bed, 2 cage, other
   values free; -1 anywhere = unreadable. Checked in this order. */
inline int CharBlock(int knockedOut, int dead, int fight, int carried, int inSomething)
{
    if (knockedOut < 0 || dead < 0 || fight < 0 || carried < 0 || inSomething < 0) return kBlockGone;
    if (dead != 0) return kBlockGone;
    if (knockedOut != 0) return kBlockOut;
    if (fight != 0) return kBlockFight;
    if (carried != 0) return kBlockCarried;
    if (inSomething == 1) return kBlockBed;
    if (inSomething == 2) return kBlockCage;
    return kBlockNone;
}

/* ---- the words (build/mockups-t1135.md) ---- */
inline std::string Num(long long v) { std::ostringstream o; o << v; return o.str(); }
inline const char* GiveWord()   { return "GIVE"; }
inline const char* CancelWord() { return "CANCEL"; }
inline const char* AcceptWord() { return "ACCEPT"; }
inline const char* RefuseWord() { return "REFUSE"; }
inline const char* KeepOneLine() { return "You must keep at least one character."; }
inline const char* MoneyStaysGiver() { return "Skills, gear and bounties go with them. Money stays with you."; }

inline std::string NamesList(const std::vector<std::string>& names)
{
    std::string s;
    for (size_t i = 0; i < names.size(); ++i) s += (i == 0 ? "" : ", ") + names[i];
    return s;
}
inline std::string ButtonCaption(const std::vector<std::string>& names)
{
    if (names.size() == 1) return std::string("GIVE ") + names[0];
    return "GIVE " + Num((long long)names.size()) + " CHARACTERS";
}
/* the blocking line under a greyed button for one character */
inline std::string BlockLine(const std::string& name, int reason)
{
    switch (reason)
    {
    case kBlockOut:     return name + " is knocked out.";
    case kBlockFight:   return name + " is in a fight.";
    case kBlockCarried: return name + " is being carried.";
    case kBlockBed:     return name + " is in a bed.";
    case kBlockCage:    return name + " is in a cage.";
    case kBlockNone:    return std::string();
    default:            return name + " could not be given.";
    }
}
/* the giver's confirm box */
inline std::string ConfirmText(const std::vector<std::string>& names, const std::string& player)
{
    std::string s = names.size() == 1 ? "Give " + names[0] + " to " + player + "?"
                                      : "Give these " + Num((long long)names.size()) + " characters to " + player + "?\n" + NamesList(names);
    return s + "\n" + MoneyStaysGiver();
}
/* the receiver's box */
inline std::string OfferText(const std::string& player, const std::vector<std::string>& names)
{
    std::string s = names.size() == 1 ? player + " wants to give you " + names[0] + "."
                                      : player + " wants to give you " + Num((long long)names.size()) + " characters:\n" + NamesList(names);
    return s + "\nSkills, gear and bounties come with them. Money stays with " + player + ".";
}
/* the grey chat lines, on both players' games */
inline std::string JoinedLine(const std::vector<std::string>& names, const std::string& receiver) { return NamesList(names) + " joined " + receiver + "'s squad."; }
inline std::string RefusedLine(const std::string& receiver, const std::vector<std::string>& names) { return receiver + " refused " + NamesList(names) + "."; }
inline std::string RanOutLine(const std::vector<std::string>& names) { return "The offer for " + NamesList(names) + " ran out."; }
inline std::string FailedLine(const std::string& name) { return name + " could not be given."; }
/* the name an index names: that character's, or the first one's when the index names none */
inline std::string NameAt(const std::vector<std::string>& names, int index)
{
    if (names.empty()) return std::string();
    return (index >= 0 && (size_t)index < names.size()) ? names[(size_t)index] : names[0];
}

/* ---- the GIVE button ---- */
const int kButtonHidden = 0;
const int kButtonLit    = 1;
const int kButtonGrey   = 2;
struct ButtonView { int state; std::string caption; std::string line; ButtonView() : state(kButtonHidden) {} };

/* reasons / names: one per selected own character (CharBlock); own: this game's own characters that are not dead (the selected ones
   among them); receiverInWorld: the picked player is in the world; busy: an offer of this player's is still waiting. Hidden with
   nothing selected; greyed with no line over kGiveMax, while an offer waits or with the other player out of the world; greyed with
   the at-least-one line when every own character is selected; greyed with the first blocking character's line. */
inline ButtonView ButtonFor(const std::vector<int>& reasons, const std::vector<std::string>& names, int own, bool receiverInWorld, bool busy)
{
    ButtonView v;
    const size_t n = reasons.size();
    if (n == 0 || names.size() != n) return v;
    v.caption = ButtonCaption(names);
    v.state = kButtonGrey;
    if (n > (size_t)kGiveMax) return v;
    if ((int)n >= own) { v.line = KeepOneLine(); return v; }
    for (size_t i = 0; i < n; ++i) if (reasons[i] != kBlockNone) { v.line = BlockLine(names[i], reasons[i]); return v; }
    if (!receiverInWorld || busy) return v;
    v.state = kButtonLit;
    return v;
}

/* All or nothing at the hand-over: -1 when every character may go, else the index the "could not be given" line names (the first
   blocked one; 0 when the rule that failed is the offer's own - every character selected, or the receiver out of the world). */
inline int HandOverFails(const std::vector<int>& reasons, int own, bool receiverInWorld)
{
    if (reasons.empty() || reasons.size() > (size_t)kGiveMax) return 0;
    for (size_t i = 0; i < reasons.size(); ++i) if (reasons[i] != kBlockNone) return (int)i;
    if ((int)reasons.size() >= own || !receiverInWorld) return 0;
    return -1;
}

/* the 60 s limit (tick counts wrap): true once `limitMs` have passed since `atMs` */
inline bool Expired(unsigned long nowMs, unsigned long atMs, unsigned long limitMs)
{
    return (unsigned long)(nowMs - atMs) >= limitMs;
}

/* ---- the guard exception: an accepted offer, as the receiver's game holds it ---- */
struct Accepted { unsigned int offerId; int giverSlot; int receiverSlot; std::vector<unsigned int> uids; Accepted() : offerId(0), giverSlot(-1), receiverSlot(-1) {} };

/* Another player's character may be recruited ONLY when this offer was accepted here: the offer id, the giver's slot, the receiver's
   slot and the character's uid all match one accepted offer. */
inline bool GiveGuardPass(const std::vector<Accepted>& accepted, unsigned int offerId, int giverSlot, int receiverSlot, unsigned int uid)
{
    if (offerId == 0 || giverSlot < 0 || receiverSlot < 0 || uid == 0) return false;
    for (size_t i = 0; i < accepted.size(); ++i)
    {
        const Accepted& a = accepted[i];
        if (a.offerId != offerId || a.giverSlot != giverSlot || a.receiverSlot != receiverSlot) continue;
        for (size_t k = 0; k < a.uids.size(); ++k) if (a.uids[k] == uid) return true;
    }
    return false;
}

/* ---- T-1135 review fold: the hand-over's outcomes ---- */
/* the giver's offer (hire.cpp GiveOut.state): none, waiting for the answer, GO sent (each character promised), "done" heard - waiting
   until every character's hire DONE has cleared its promise */
const int kOutNone = 0, kOutOffered = 1, kOutGoSent = 2, kOutDoneHeard = 3;

/* the receiver's word counts only in the state it answers: ACCEPT, REFUSE and the box's timeout while the offer waits for its answer;
   "failed" until "done" (the receiver's game takes nothing after that word); "done" only after GO */
inline bool AnswerFits(int state, int word)
{
    if (word == kAnsAccept || word == kAnsRefuse || word == kAnsTimeout) return state == kOutOffered;
    if (word == kAnsFailed) return state == kOutOffered || state == kOutGoSent;
    if (word == kAnsDone) return state == kOutGoSent;
    return false;
}

/* the receiver carries out a GO only for an offer it accepted (its state 2) whose accepted wait (kOfferMs from the ACCEPT) has not run
   out: once that wait ran out it answered "failed", and the giver may have released its promises on that word */
inline bool GoTakes(int state, unsigned long nowMs, unsigned long acceptedAtMs) { return state == 2 && !Expired(nowMs, acceptedAtMs, kOfferMs); }

/* how the giver's offer ended, and whether its promises (made only at GO) are kept: released on the receiver's "failed" (nothing is
   taken after that word) and on a world change (the world they were made in is gone); KEPT when GO went out and no word says what
   happened - the receiver left, or no word came in time - because the take may have happened (Finish clears each one if its hire
   DONE comes). 1 kept, 0 released (or none were made). */
const int kEndFailedWord = 1, kEndPeerGone = 2, kEndTimedOut = 3, kEndWorldChanged = 4;
inline int PromisesKept(int state, int end)
{
    if (state < kOutGoSent) return 0;
    return (end == kEndPeerGone || end == kEndTimedOut) ? 1 : 0;
}

/* T-1135 review fold 6: a promise KEPT at an offer's end (PromisesKept) is released later, one character at a time, only when a word or
   a departure says the receiver never took it. Listed only from a GO with no "done" heard (doneHeard 0): after "done" the receiver ran
   its take, and a character's outcome is known only from its own hire DONE, so those stay kept. Released when the character is still
   promised here (no hire DONE came), this game still runs it at the generation its GO named (no hand-back moved it), no OWNER_MOVED word
   named another game its owner (the receiver's take announces one before its hire DONE), AND either the receiver's "failed" for the offer
   came (it sends that word only for an offer it took nothing of) or the receiver left the world and the GO's wait (kOfferMs + kGraceMs)
   is over. 1 release, 0 keep. */
inline int KeptRelease(int doneHeard, int promised, int stillMine, int genSame, int movedSeen, int failedWord, int receiverGone, int windowOver)
{
    if (doneHeard != 0 || promised == 0 || stillMine == 0 || genSame == 0 || movedSeen != 0) return 0;
    if (failedWord != 0) return 1;
    return (receiverGone != 0 && windowOver != 0) ? 1 : 0;
}
/* T-1135 review fold 6: the receiver at a world change (a load, or the world closing): an offer it accepted (state 2) is still listed
   only while nothing of it was taken (GiveTakeAll removes the offer before its take), so it owes the giver "failed"; an offer only
   waiting for its box (state 1) owes nothing - no promise was made for it. 1 owes. */
inline int FailedOwedAtDrop(int inState) { return inState == 2 ? 1 : 0; }

/* room at the game's character limit for every given character: the engine's recruit refuses when the player faction's living
   characters are not below the limit, so n can all join only when alive + n <= limit. 1 room, 0 none, -1 not judged (the limit did
   not read, or reads 0 or less). Nothing given needs no room. */
inline int RoomForAll(int limitRead, int limit, int alive, int n)
{
    if (n <= 0) return 1;
    if (!limitRead || limit <= 0) return -1;
    return ((long long)alive + (long long)n <= (long long)limit) ? 1 : 0;
}

/* ---- the TEST-ONLY lever: givetest offer <slot> <uid> [<uid>..] | givetest answer accept|refuse | givetest show [<slot>] ---- */
const int kLeverBad    = 0;
const int kLeverOffer  = 1;
const int kLeverAnswer = 2;
const int kLeverShow   = 3;
struct Lever { int kind; int slot; bool accept; std::vector<unsigned int> uids; Lever() : kind(kLeverBad), slot(-1), accept(false) {} };

inline bool LeverNumber(const std::string& w, unsigned long long maxV, unsigned long long* out)
{
    if (w.empty() || w.size() > 10) return false;
    unsigned long long v = 0;
    for (size_t i = 0; i < w.size(); ++i) { if (w[i] < '0' || w[i] > '9') return false; v = v * 10 + (unsigned long long)(w[i] - '0'); }
    if (v > maxV) return false;
    *out = v;
    return true;
}
inline Lever ParseLever(const std::string& arg)
{
    std::istringstream is(arg);
    std::vector<std::string> t; std::string w;
    while (is >> w) t.push_back(w);
    Lever l;
    if (t.empty()) return l;
    unsigned long long v = 0;
    if (t[0] == "offer")
    {
        if (t.size() < 3 || t.size() > (size_t)kGiveMax + 2 || !LeverNumber(t[1], 254, &v)) return l;
        l.slot = (int)v;
        for (size_t i = 2; i < t.size(); ++i)
        {
            if (!LeverNumber(t[i], 4294967295ULL, &v) || v == 0) return Lever();
            l.uids.push_back((unsigned int)v);
        }
        l.kind = kLeverOffer;
        return l;
    }
    if (t[0] == "answer")
    {
        if (t.size() != 2 || (t[1] != "accept" && t[1] != "refuse")) return l;
        l.accept = t[1] == "accept";
        l.kind = kLeverAnswer;
        return l;
    }
    if (t[0] == "show")
    {
        if (t.size() > 2) return l;
        if (t.size() == 2) { if (!LeverNumber(t[1], 254, &v)) return l; l.slot = (int)v; }
        l.kind = kLeverShow;
        return l;
    }
    return l;
}

} // namespace swgive
