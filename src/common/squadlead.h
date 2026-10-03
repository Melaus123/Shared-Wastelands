/* src/common/squadlead.h - T-1 B1 restructure (re-check N1-N5 of ff655c5, 2026-09-28): MSG_SQUAD_LEAD (57, reliable, session
 * protocol 85). ONE AUTHORITY, ONE ANNOUNCED VALUE: for every NPC squad a game runs members of, it announces the squad's acting
 * leader AS ITS OWN ENGINE CHOOSES IT (coopsquad::EngineLeaderChoice, computed once - the value it uses itself), the squad's
 * formal leader (+0xA0) and the members it runs - on first sight, on any change, and again to a new link. The other game uses
 * the announced acting leader AS IS (it never re-judges "standing" on its copies), so both games feed coopsquad::AgreedSquadLeader
 * the SAME pair. Replaces CONTEXT's member type 2 as the leader carrier (CONTEXT is otherwise unchanged).
 *
 *   squadKey u32 (the sender's own key for one of its squads, never 0) | acting u32 (0 = none) | formal u32 (0 = none)
 *   | seq u32 (the sender's announcement counter, never 0; the newest per squadKey wins)
 *   | cats i32 | hasCats u8 (T-1 B3 restructure, protocol 89: 1 = the sender runs the squad's formal leader - it is listed -
 *     and cats is the squad's Ownerships::money +0x88, the trade window's pot; 0 = no value, cats 0) | n u16 (<= 64)
 *   | n x u32 member uids the sender runs in that squad (none 0). n = 0: the sender runs no member of that squad any more.
 *
 * Pure: no engine memory, no Windows, no globals; the plugin (handoff.cpp) and the offline suite compile the same code.
 * C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <map>
#include <vector>

namespace coopsquad {

const size_t       kSquadLeadHead       = 23;   /* T-1 B3 restructure (protocol 89): 18 + cats i32 + hasCats u8 */
const unsigned int kSquadLeadMaxMembers = 64;

struct SquadLeadMsg
{
    unsigned int squadKey, acting, formal, seq;
    int cats;                            /* T-1 B3 restructure: the squad's money (hasCats 1), else 0 */
    unsigned char hasCats;               /* 1 = the sender runs the formal leader (listed) and read its pot */
    std::vector<unsigned int> members;   /* the uids the SENDER runs in that squad */
    SquadLeadMsg() : squadKey(0), acting(0), formal(0), seq(0), cats(0), hasCats(0) {}
};

const int kSquadLeadDecodeOk        = 0;
const int kSquadLeadDecodeTooShort  = 1;
const int kSquadLeadDecodeBadCount  = 2;   /* n above kSquadLeadMaxMembers */
const int kSquadLeadDecodeBadSize   = 3;   /* the payload is not exactly head + 4 n */
const int kSquadLeadDecodeBadKey    = 4;   /* squadKey or seq 0 */
const int kSquadLeadDecodeBadMember = 5;   /* a member uid 0 */
const int kSquadLeadDecodeBadFlag   = 6;   /* T-1 B3 restructure: hasCats neither 0 nor 1 */
const int kSquadLeadDecodeBadCats   = 7;   /* hasCats 1 without the formal leader listed, or hasCats 0 with cats */

/* T-1 B3 restructure: an announcement may carry the squad's money only from the game that runs its formal leader - it lists it. */
inline bool LeadCarriesCats(unsigned int formal, const std::vector<unsigned int>& members)
{
    if (formal == 0) return false;
    for (size_t i = 0; i < members.size(); ++i) if (members[i] == formal) return true;
    return false;
}

/* false (nothing appended) for a key or seq of 0, more than 64 members, or a member uid 0. */
inline bool EncodeSquadLead(std::vector<char>* b, const SquadLeadMsg& m)
{
    if (b == 0 || m.squadKey == 0 || m.seq == 0 || m.members.size() > kSquadLeadMaxMembers) return false;
    for (size_t i = 0; i < m.members.size(); ++i) if (m.members[i] == 0) return false;
    if (m.hasCats > 1 || (m.hasCats == 0 && m.cats != 0) || (m.hasCats == 1 && !LeadCarriesCats(m.formal, m.members))) return false;   /* T-1 B3 restructure */
    const unsigned short n = (unsigned short)m.members.size();
    const size_t at = b->size();
    b->resize(at + kSquadLeadHead + 4 * (size_t)n);
    char* p = &(*b)[at];
    std::memcpy(p, &m.squadKey, 4); std::memcpy(p + 4, &m.acting, 4); std::memcpy(p + 8, &m.formal, 4); std::memcpy(p + 12, &m.seq, 4);
    std::memcpy(p + 16, &m.cats, 4); std::memcpy(p + 20, &m.hasCats, 1);   /* T-1 B3 restructure */
    std::memcpy(p + 21, &n, 2);
    for (unsigned short i = 0; i < n; ++i) std::memcpy(p + kSquadLeadHead + 4 * (size_t)i, &m.members[i], 4);
    return true;
}

/* Nothing is written on a refusal. */
inline int DecodeSquadLead(const char* p, size_t size, SquadLeadMsg* out)
{
    if (p == 0 || out == 0 || size < kSquadLeadHead) return kSquadLeadDecodeTooShort;
    unsigned int key = 0, acting = 0, formal = 0, seq = 0; unsigned short n = 0; int cats = 0; unsigned char has = 0;
    std::memcpy(&key, p, 4); std::memcpy(&acting, p + 4, 4); std::memcpy(&formal, p + 8, 4); std::memcpy(&seq, p + 12, 4);
    std::memcpy(&cats, p + 16, 4); std::memcpy(&has, p + 20, 1);   /* T-1 B3 restructure */
    std::memcpy(&n, p + 21, 2);
    if (key == 0 || seq == 0) return kSquadLeadDecodeBadKey;
    if (n > kSquadLeadMaxMembers) return kSquadLeadDecodeBadCount;
    if (size != kSquadLeadHead + 4 * (size_t)n) return kSquadLeadDecodeBadSize;
    std::vector<unsigned int> mem(n);
    for (unsigned short i = 0; i < n; ++i)
    {
        std::memcpy(&mem[i], p + kSquadLeadHead + 4 * (size_t)i, 4);
        if (mem[i] == 0) return kSquadLeadDecodeBadMember;
    }
    if (has > 1) return kSquadLeadDecodeBadFlag;   /* T-1 B3 restructure */
    if ((has == 0 && cats != 0) || (has == 1 && !LeadCarriesCats(formal, mem))) return kSquadLeadDecodeBadCats;
    out->squadKey = key; out->acting = acting; out->formal = formal; out->seq = seq; out->cats = cats; out->hasCats = has;
    out->members.swap(mem);
    return kSquadLeadDecodeOk;
}

/* The receiver's book of the other game's announcements: one entry per sender squadKey (the newest seq wins), and for every uid
   the key of the newest announcement that lists it. A newer announcement for a key that no longer lists a uid drops that uid;
   n = 0 drops the key. The book is cleared with the link (a new link hears every announcement again). */
const int kLeadApplied = 0;   /* stored */
const int kLeadStale   = 1;   /* seq not newer than the stored one for that key: ignored */
const int kLeadDropped = 2;   /* n = 0: the key (and its uids) forgotten */
const int kLeadFull    = 3;   /* a new key past the cap: ignored */
const size_t kLeadBookCap = 65536;

struct PeerLeadEntry { unsigned int acting, formal, seq; int cats; unsigned char hasCats; std::vector<unsigned int> members; unsigned int sender; PeerLeadEntry() : acting(0), formal(0), seq(0), cats(0), hasCats(0), sender(0) {} };   /* T-1 B3 restructure: cats. M7a A1 build 1 [a1b1-sl0]: + sender, the announcing game's player key (fold 1 [a1b1f1-sl0] R8: two comments were on one line) */

class PeerLeadBook
{
public:
    /* M7a A1 build 1 [a1b1-sl1] (design 1.3 [review F7]): keyed by (SENDER, squadKey) - a squadKey and a seq are one game's own
       counters, so two games' announcements never replace each other; a uid follows the newest announcement listing it. */
    int Apply(const SquadLeadMsg& m) { return Apply(m, 0u); }
    int Apply(const SquadLeadMsg& m, unsigned int sender)
    {
        const unsigned long long bk = BookKey(sender, m.squadKey);
        std::map<unsigned long long, PeerLeadEntry>::iterator it = m_squads.find(bk);
        if (it != m_squads.end() && m.seq <= it->second.seq) return kLeadStale;
        if (it != m_squads.end()) Unlist(bk, it->second.members);
        if (m.members.empty())
        {
            if (it != m_squads.end()) m_squads.erase(it);
            return kLeadDropped;
        }
        if (it == m_squads.end() && m_squads.size() >= kLeadBookCap) return kLeadFull;
        PeerLeadEntry& e = m_squads[bk];
        e.acting = m.acting; e.formal = m.formal; e.seq = m.seq; e.members = m.members; e.sender = sender;   /* [a1b1-sl2] */
        e.cats = m.cats; e.hasCats = m.hasCats;   /* T-1 B3 restructure */
        for (size_t i = 0; i < m.members.size(); ++i) m_uidKey[m.members[i]] = bk;   /* a uid moved here from another key (or game) follows */
        return kLeadApplied;
    }
    /* The announcement that lists `uid` (the other game runs it), 0 = none. Valid until the next Apply / Clear. */
    const PeerLeadEntry* ForUid(unsigned int uid) const
    {
        std::map<unsigned int, unsigned long long>::const_iterator k = m_uidKey.find(uid);
        if (k == m_uidKey.end()) return 0;
        std::map<unsigned long long, PeerLeadEntry>::const_iterator e = m_squads.find(k->second);
        return e == m_squads.end() ? 0 : &e->second;
    }
    void Clear() { m_squads.clear(); m_uidKey.clear(); }
    size_t Squads() const { return m_squads.size(); }
    size_t Uids() const { return m_uidKey.size(); }
    /* T-1 B3 restructure: the squad's money for `uid` as the squad's formal leader - the newest announcement listing it, when that
       one names it formal leader and carries cats (the other game runs it). 0 = none. Valid until the next Apply / Clear. */
    const PeerLeadEntry* ForLeader(unsigned int uid) const
    {
        const PeerLeadEntry* e = ForUid(uid);
        return (e != 0 && e->hasCats == 1 && e->formal == uid) ? e : 0;
    }
    /* The formal leaders of every announcement that carries cats. */
    void CatsLeaders(std::vector<unsigned int>* out) const
    {
        out->clear();
        for (std::map<unsigned long long, PeerLeadEntry>::const_iterator it = m_squads.begin(); it != m_squads.end(); ++it)
            if (it->second.hasCats == 1 && it->second.formal != 0) out->push_back(it->second.formal);
    }
private:
    static unsigned long long BookKey(unsigned int sender, unsigned int squadKey) { return ((unsigned long long)sender << 32) | (unsigned long long)squadKey; }   /* [a1b1-sl5] */
    void Unlist(unsigned long long key, const std::vector<unsigned int>& members)
    {
        for (size_t i = 0; i < members.size(); ++i)
        {
            std::map<unsigned int, unsigned long long>::iterator u = m_uidKey.find(members[i]);
            if (u != m_uidKey.end() && u->second == key) m_uidKey.erase(u);   /* listed by a newer key since: kept */
        }
    }
    std::map<unsigned long long, PeerLeadEntry> m_squads;   /* [a1b1-sl6] (sender << 32 | squadKey) -> the newest announcement */
    std::map<unsigned int, unsigned long long> m_uidKey;
};

/* ---- T-1 B3 restructure (review F1 / F5 of 2534248; protocol 89): the squad's money ----
   The TAKING game (it runs the announced formal leader now). hasRec 0: the other game's last announcement for that leader
   (entrySeq / entryCats) is not written here yet - write it as it is (kCatsAbsolute), before this game announces the squad.
   hasRec 1: recSeq / recCats is the announcement last written; a newer one is the giving game's own change inside the hand-over
   round trip (both games run the leader until the ACK) - added as *delta (kCatsDelta), never overwritten; else kCatsNone. */
const int kCatsNone = 0, kCatsAbsolute = 1, kCatsDelta = 2;
inline int HandoverCatsAction(unsigned int entrySeq, int entryCats, int hasRec, unsigned int recSeq, int recCats, long long* delta)
{
    *delta = 0;
    if (hasRec == 0) return kCatsAbsolute;
    if (entrySeq <= recSeq) return kCatsNone;
    *delta = (long long)entryCats - (long long)recCats;
    return kCatsDelta;
}
/* The GIVING game: an XFER of a squad whose money it announces goes only when that announcement is current (sent in this very
   pass, or unchanged) - it then precedes the XFER on the same ordered reliable channel. 1 = the XFER may go. */
inline int HandoverXferMayGo(int carriesCats, int announceCurrent)
{
    return (carriesCats == 0 || announceCurrent != 0) ? 1 : 0;
}
/* The COPY game: the announced value against its copy squad's pot. inFlight 1 = this game's own priced requests against that pot
   are in flight (the local engine's move is a prediction): held, and written by the first pass after they settle. */
const int kPotEqual = 0, kPotHold = 1, kPotWrite = 2;
inline int CopyPotAction(int announced, int current, int inFlight)
{
    if (announced == current) return kPotEqual;
    return inFlight != 0 ? kPotHold : kPotWrite;
}

/* ---- T-1 B3 fold (money review of 97c1d61, 2026-09-29) ---- */
/* M1: the COPY branch never writes the other game's value into a pot this game announces cats for (announcedHere: one of this
   game's own squads' formal-leader pots) or into a squad with a member this game runs (memberRunHere) - a squad split between the
   games with a different formal leader on each made both games write each other's value into their own real pot. 1 = skip. */
inline int CopyPotOwnSkip(int announcedHere, int memberRunHere)
{
    return (announcedHere != 0 || memberRunHere != 0) ? 1 : 0;
}
/* M2 (the TAKING game): its absolute take-over write waits while this game's own priced requests against that pot are in flight
   (their local move is a prediction the write would wipe); a round-trip delta never wipes it and is not held. 1 = held. */
inline int TakeoverAdoptHeld(int action, int potInFlight)
{
    return (action == kCatsAbsolute && potInFlight != 0) ? 1 : 0;
}
/* M2 (the REQUESTING game): the keeper half of a refused / lapsed / cancelled priced request, decided from what was recorded at
   the SEND - ranAtSend 1 = this game ran the counter's keeper then (its own engine moved that real pot). kUndoNotRunner: handed
   over since - nothing is owed here, because this game never announced that prediction (KeeperPotAnnounced nets it out) and the
   holder, the runner now, pays its real pot only on acceptance. kUndoPotChanged: the squad's pot is another object - not written. */
const int kUndoNone = 0, kUndoApply = 1, kUndoNotRunner = 2, kUndoPotChanged = 3;
inline int KeeperUndoDecision(int ranAtSend, int runsNow, int samePot)
{
    if (ranAtSend == 0) return kUndoNone;
    if (runsNow == 0) return kUndoNotRunner;
    return samePot != 0 ? kUndoApply : kUndoPotChanged;
}
/* M2: the pot as this game ANNOUNCES it - `cur` less `predicted`, the sum of this game's own in-flight predicted moves on it (a
   purchase +price, a sale -price) recorded as run here at their send. 1 = *out; 2 = the pot was spent below the prediction,
   *out 0 (never negative); 0 = out of range, nothing announced. */
inline int SettledPotValue(int cur, long long predicted, int* out)
{
    const long long v = (long long)cur - predicted;
    if (v > 2147483647LL || v < -2147483647LL - 1LL) { *out = 0; return 0; }
    if (v < 0) { *out = 0; return 2; }
    *out = (int)v;
    return 1;
}
/* L1: the undo is a raw pot adjustment of exactly the price, never the engine's afford-checked takeMoney. trade 1 (a purchase: the
   keeper had gained the price) takes it back, never below 0 - what the pot cannot give is *shortfall (owner 106's saleDebitShort
   rule), and a pot at or below 0 is never raised; any other trade (a sale: the keeper had paid the price) gives it back.
   1 = *newPot to write (it may equal cur); 0 = no price, or out of range - nothing written. */
inline int KeeperUndoTarget(int cur, int trade, int price, int* newPot, int* shortfall)
{
    *newPot = cur; *shortfall = 0;
    if (price <= 0) return 0;
    if (trade == 1)
    {
        if (cur <= 0) { *shortfall = price; return 1; }
        if (cur < price) { *newPot = 0; *shortfall = price - cur; return 1; }
        *newPot = cur - price;
        return 1;
    }
    const long long v = (long long)cur + (long long)price;
    if (v > 2147483647LL) return 0;
    *newPot = (int)v;
    return 1;
}
/* M3 (the GIVING game, at the ACK): before a formal leader whose squad carries cats (has) is released, its pot is re-read; a value
   other than the last sent goes as a final announcement (the taker adds it as a round-trip delta). 1 = send. */
inline int FinalAnnounceDue(int has, int lastSentCats, int readOk, int cur)
{
    return (has != 0 && readOk != 0 && cur != lastSentCats) ? 1 : 0;
}
/* L3: an XFER resend goes only from a pass that ran its squads (passRan), and for a squad seen in it only when a first send
   would (gate = HandoverXferMayGo) - after a reconnect the re-announcements precede the resends. A squad not seen: as before. */
inline int ResendMayGo(int passRan, int squadSeen, int gate)
{
    if (passRan == 0) return 0;
    return squadSeen != 0 ? gate : 1;
}
/* L4: the record's carries-cats flag once `releasedUid` is released - cleared when it is the record's formal leader. */
inline int LeadHasAfterRelease(unsigned int recordFormal, unsigned int releasedUid, int has)
{
    return (has != 0 && recordFormal == releasedUid) ? 0 : has;
}

}   /* namespace coopsquad */
