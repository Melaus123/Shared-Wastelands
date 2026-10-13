/* src/common/crimewire.h - crime3 (docs/design-crime.md section 3 A): A CHARACTER'S CURRENT CRIME, SENT BY ITS OWNER.
 *
 * Kenshi records a crime on the OFFENDER (BountyManager at Character+0xF0; cloud/ANSWERS.md crime1/crime2, Read):
 *   Character +0x148 int   committingCrime (CrimeEnum, 0 = none)
 *   Character +0x150 ptr   crimeAgainstFaction
 *   Character +0x168..+0x178  crimeAgainst (a hand: the victim)
 *   Character +0x180 float crimeExpiry (game seconds; senses alone pick the crime up while it is > 5.0)
 * The act that sets it runs on the game that DRIVES the offender, so every other game's copy of the offender carries
 * no crime and that game's own witnesses have nothing to react to.  The owner sends the four values when they change;
 * the other game writes them onto its copy through the engine's notifyCrimeWitnessed (0x851F40) at the K2 safe point.
 *
 *   uid u32 | crime i32 | expiry f32 | victimUid u32 (0 = none / not replicated) | len u8 (0..kCrimeMaxSid) | faction sid
 *
 * The faction travels as a relations sid (relations.cpp SidOf: a stringID, "@player:<name>" or "@peer").  The length
 * test on decode is a MINIMUM: bytes after the sid are ignored, so a later build may append a field.
 * A KIND byte may follow the sid (MSG_CRIME kinds below); a message that ends with the sid is kind 0.
 *
 * Pure: no engine memory, no Windows.  The offline suite (src/coop-test/test_main.cpp) hits the same bytes and the same
 * send decision the plugin uses.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace coopcrime {

const unsigned int kCrimeMaxSid = 96;         /* bytes; a longer sid is not sent */
const size_t kCrimeFixedBytes = 17;           /* uid 4 + crime 4 + expiry 4 + victim 4 + len 1 */
const int kCrimeMaxEnum = 64;                 /* CrimeEnum is small (STEALING 3, ASSAULT 5, TRESPASSING 11); a bigger value is refused */
const float kCrimeMaxExpiry = 3600.0f;        /* game seconds; the engine sets 20 - a larger value is refused (review-crime3 8) */

const int kCrimeDecodeOk        = 0;
const int kCrimeDecodeTooShort  = 1;          /* fewer than the 17 fixed bytes */
const int kCrimeDecodeBadCrime  = 2;          /* crime < 0 or > kCrimeMaxEnum */
const int kCrimeDecodeBadExpiry = 3;          /* expiry NaN, infinite, negative (caught by its bits) or above kCrimeMaxExpiry */
const int kCrimeDecodeSidTrunc  = 4;          /* the payload ends before the sid does, or the length names > kCrimeMaxSid */

struct CrimeState
{
    int crime;               /* CrimeEnum, 0 = none */
    float expiry;            /* game seconds left */
    unsigned int victimUid;  /* 0 = none / not a replicated character */
    std::string factionSid;  /* the victim's faction as a relations sid, empty = none */
    CrimeState() : crime(0), expiry(0.0f), victimUid(0) {}
};

/* A float's bits say whether it is finite and not negative (a `v != v` test is not safe under /fp:fast). */
inline bool CrimeExpiryOk(float f)
{
    unsigned int b = 0;
    std::memcpy(&b, &f, 4);
    if ((b & 0x7F800000u) == 0x7F800000u) return false;   /* NaN or infinity */
    if ((b & 0x80000000u) != 0 && (b & 0x7FFFFFFFu) != 0) return false;   /* negative (-0.0 passes) */
    return true;
}

/* false (and nothing appended) when the sid is longer than kCrimeMaxSid. */
inline bool EncodeCrime(std::vector<char>* b, unsigned int uid, const CrimeState& s)
{
    if (b == 0 || s.factionSid.size() > (size_t)kCrimeMaxSid) return false;
    const unsigned char n = (unsigned char)s.factionSid.size();
    const size_t at = b->size();
    b->resize(at + kCrimeFixedBytes + n);
    std::memcpy(&(*b)[at], &uid, 4);
    std::memcpy(&(*b)[at + 4], &s.crime, 4);
    std::memcpy(&(*b)[at + 8], &s.expiry, 4);
    std::memcpy(&(*b)[at + 12], &s.victimUid, 4);
    (*b)[at + 16] = (char)n;
    if (n > 0) std::memcpy(&(*b)[at + 17], s.factionSid.data(), n);
    return true;
}

/* Every test is `size - off < n` with `off` already bounded by `size`, so a hostile length cannot wrap. */
inline int DecodeCrime(const char* p, size_t size, unsigned int* uid, CrimeState* out)
{
    CrimeState s;
    unsigned int u = 0;
    if (p == 0 || size < kCrimeFixedBytes) return kCrimeDecodeTooShort;
    std::memcpy(&u, p, 4);
    std::memcpy(&s.crime, p + 4, 4);
    std::memcpy(&s.expiry, p + 8, 4);
    std::memcpy(&s.victimUid, p + 12, 4);
    const unsigned int n = (unsigned char)p[16];
    if (s.crime < 0 || s.crime > kCrimeMaxEnum) return kCrimeDecodeBadCrime;
    if (!CrimeExpiryOk(s.expiry) || s.expiry > kCrimeMaxExpiry) return kCrimeDecodeBadExpiry;
    if (n > kCrimeMaxSid || size - kCrimeFixedBytes < (size_t)n) return kCrimeDecodeSidTrunc;
    s.factionSid.assign(p + kCrimeFixedBytes, (size_t)n);
    if (uid) *uid = u;
    if (out) *out = s;
    return kCrimeDecodeOk;
}

/* The owner's send decision, one call per owned character per sample.  1 = send `cur` now.
 *   - the crime, the victim or the victim's faction changed from what was last SENT (a new crime, a cleared one, a new
 *     target);
 *   - or the expiry went UP by more than kCrimeReSetSec since the last SAMPLE (the same crime set again - a second theft of
 *     the same kind; the engine re-sets it to 20, so comparing with the last SENT expiry, 20, never fired - review-crime3 2):
 *     the copy's expiry would otherwise run out while the owner's is fresh.
 * A falling expiry is the engine counting down on both games and is never sent. */
const float kCrimeReSetSec = 1.0f;
inline int CrimeShouldSend(const CrimeState& lastSent, float lastSampledExpiry, const CrimeState& cur)
{
    if (cur.crime != lastSent.crime || cur.victimUid != lastSent.victimUid || cur.factionSid != lastSent.factionSid) return 1;
    if (cur.crime != 0 && cur.expiry > lastSampledExpiry + kCrimeReSetSec) return 1;
    return 0;
}

/* MSG_CRIME KINDS. A byte after the sid says which; a message that ends with the sid (every kind-0 send, and every older
 * build's) reads as kind 0. Bytes after a kind's own fields are ignored.
 *   0  the owner's current crime for its character, to every game (the layout above, nothing after the sid);
 *   1  a crime THIS game's engine recorded on its copy of the receiver's character (this game's witnesses saw the copy act), to
 *      that character's owner only: the layout above, uid = the character, sid = the victim's faction, then the byte 1;
 *   2  a standing event THIS game's engine raised against the receiver's player (FactionRelations::affectRelations by event,
 *      called with that player's stand-in faction), to that player only:
 *        uid 0 | crime 0 | expiry 0 | victim 0 | len | ownerSid (the faction whose standing moves) | 2 | seq u32 | ev i32 |
 *        mult f32 | len2 u8 | otherSid (the player's wire name, "@slot:<n>")
 * An older build reads kinds 1 and 2 as kind 0 and refuses them (kind 1: the sender is not the uid's owner; kind 2: uid 0). */
const unsigned char kCrimeKindState     = 0;
const unsigned char kCrimeKindCopyWrite = 1;
const unsigned char kCrimeKindRelEvent  = 2;
const int kCrimeDecodeBadKind  = 5;    /* a kind byte above kCrimeKindRelEvent */
const int kCrimeDecodeRelTrunc = 6;    /* kind 2 ends before its fields or its second sid do, or len2 names > kCrimeMaxSid */
const int kCrimeDecodeRelBad   = 7;    /* kind 2 with an event outside 0..kRelEvMaxEv, a multiplier not finite or beyond
                                          kRelEvMaxMult, or an empty sid */
const int kRelEvMaxEv = 16;            /* FactionEvent is small (0..12 used) */
const float kRelEvMaxMult = 100.0f;

struct CrimeRelEvent
{
    unsigned int seq;        /* the sender's own count: the receiver's duplicate test */
    int ev;                  /* FactionEvent */
    float mult;
    std::string ownerSid;    /* the faction whose standing towards the player moves (a world faction's stringID) */
    std::string otherSid;    /* the player's faction as relations names it ("@slot:<n>") */
    CrimeRelEvent() : seq(0), ev(0), mult(0.0f) {}
};

/* kind 0 or 1: the layout above and the kind byte. false (and nothing appended) when the sid is too long. */
inline bool EncodeCrimeKind(std::vector<char>* b, unsigned int uid, const CrimeState& s, unsigned char kind)
{
    if (!EncodeCrime(b, uid, s)) return false;
    b->push_back((char)kind);
    return true;
}

/* finite (by its bits) and within kRelEvMaxMult */
inline bool RelEvMultOk(float f)
{
    unsigned int bits = 0;
    std::memcpy(&bits, &f, 4);
    if ((bits & 0x7F800000u) == 0x7F800000u) return false;
    return f <= kRelEvMaxMult && f >= -kRelEvMaxMult;
}

/* kind 2. false (and nothing appended) for an empty or too long sid, an event outside 0..kRelEvMaxEv or a bad multiplier. */
inline bool EncodeCrimeRelEvent(std::vector<char>* b, const CrimeRelEvent& e)
{
    if (b == 0 || e.ownerSid.empty() || e.otherSid.empty() || e.otherSid.size() > (size_t)kCrimeMaxSid) return false;
    if (e.ev < 0 || e.ev > kRelEvMaxEv || !RelEvMultOk(e.mult)) return false;
    CrimeState s;
    s.factionSid = e.ownerSid;
    if (!EncodeCrime(b, 0u, s)) return false;
    const unsigned char n2 = (unsigned char)e.otherSid.size();
    const size_t at = b->size();
    b->resize(at + 14 + n2);
    (*b)[at] = (char)kCrimeKindRelEvent;
    std::memcpy(&(*b)[at + 1], &e.seq, 4);
    std::memcpy(&(*b)[at + 5], &e.ev, 4);
    std::memcpy(&(*b)[at + 9], &e.mult, 4);
    (*b)[at + 13] = (char)n2;
    std::memcpy(&(*b)[at + 14], e.otherSid.data(), n2);
    return true;
}

/* Every kind. The kind-0 fields are decoded (and refused) exactly as DecodeCrime; *kind is 0 when nothing follows the sid; *rel
 * is filled for kind 2 only (its ownerSid = the sid above). */
inline int DecodeCrimeMsg(const char* p, size_t size, unsigned int* uid, CrimeState* out, unsigned char* kind, CrimeRelEvent* rel)
{
    unsigned int u = 0;
    CrimeState s;
    const int r = DecodeCrime(p, size, &u, &s);
    if (r != kCrimeDecodeOk) return r;
    const size_t off = kCrimeFixedBytes + s.factionSid.size();
    unsigned char k = kCrimeKindState;
    CrimeRelEvent e;
    if (size > off)
    {
        k = (unsigned char)p[off];
        if (k > kCrimeKindRelEvent) return kCrimeDecodeBadKind;
        if (k == kCrimeKindRelEvent)
        {
            if (size - off < 14) return kCrimeDecodeRelTrunc;
            std::memcpy(&e.seq, p + off + 1, 4);
            std::memcpy(&e.ev, p + off + 5, 4);
            std::memcpy(&e.mult, p + off + 9, 4);
            const unsigned int n2 = (unsigned char)p[off + 13];
            if (n2 > kCrimeMaxSid || size - off - 14 < (size_t)n2) return kCrimeDecodeRelTrunc;
            e.otherSid.assign(p + off + 14, (size_t)n2);
            e.ownerSid = s.factionSid;
            if (e.ev < 0 || e.ev > kRelEvMaxEv || !RelEvMultOk(e.mult) || e.ownerSid.empty() || e.otherSid.empty())
                return kCrimeDecodeRelBad;
        }
    }
    if (uid) *uid = u;
    if (out) *out = s;
    if (kind) *kind = k;
    if (rel) *rel = e;
    return kCrimeDecodeOk;
}

/* KIND 1 ON THE COPY'S GAME, one call each time this game's engine records a crime on the copy. `recorded` = the crime the copy
 * holds now (read after the engine wrote it); `ownerKnown` = the crime of the owner's last state applied to this copy (0 none);
 * `pending` = a crime already sent to the owner and not yet in its state (0 none); `ownerLeftSec` = what is left of the owner's
 * last state for that crime (its expiry less the time since it was applied here). 1 = send it to the owner:
 *   - a crime the owner neither holds nor has been told of;
 *   - or the crime the owner holds, recorded here AGAIN while the owner's is within kCopyReSendLeadSec of its end: the witnesses
 *     here still see it (a trespass is re-recorded by the senses with a 3 s expiry), so the owner records it again before it
 *     runs out there - as CrimeShouldSend re-sends a crime set again on the owner. One re-send per uid until the owner's state
 *     holds it again or the re-send's own mark runs out (`pending`). */
const float kCopyReSendLeadSec = 1.5f;
inline int CopyWriteShouldSend(int recorded, int ownerKnown, int pending, float ownerLeftSec)
{
    if (recorded <= 0 || recorded > kCrimeMaxEnum) return 0;
    if (recorded == pending) return 0;
    if (recorded == ownerKnown) return (ownerLeftSec <= kCopyReSendLeadSec) ? 1 : 0;
    return 1;
}

/* KIND 1 ON THE COPY'S GAME, when the owner's state is written onto its copy and the engine keeps the crime the copy held
 * (`before`, read before the write; `after`, read after it) instead of the owner's `requested` one - setCrime refuses an ASSAULT
 * while any other crime is set. A held crime the owner's PREVIOUS state (`prevKnown`) did not hold was recorded here: 1 = note it
 * as a copy-side write, decided as any other (CopyWriteShouldSend). One the owner's previous state held is the owner's own and is
 * never sent back. */
inline int CopyHeldNotOwners(int before, int after, int requested, int prevKnown)
{
    if (before <= 0 || before > kCrimeMaxEnum) return 0;
    if (after != before || requested == before) return 0;
    return before != prevKnown ? 1 : 0;
}

/* A MSG_CRIME arriving while engine writes are blocked (a load): kind 2 is kept (relations.cpp holds it, bounded, until a world
 * runs); every other kind - and a message that does not decode - names a world being rebuilt and is dropped. */
inline bool CrimeMsgKeptThroughLoad(const char* p, size_t size)
{
    unsigned char k = kCrimeKindState;
    if (p == 0 || DecodeCrimeMsg(p, size, 0, 0, &k, 0) != kCrimeDecodeOk) return false;
    return k == kCrimeKindRelEvent;
}

/* A COPY'S OWN DEATH. declareDead 0x7A5660 raises FactionEvent 5 (killed indirectly) from the dead character's faction towards
 * its killer hand's faction. On a copy it runs once the owner's state says dead, and the owner's own engine has already raised
 * its own events for that death (kind 2 brings this game's share here). So inside a copy's declareDead, event 5:
 *   towards THIS game's player faction - the engine's change is skipped (the owner's kind 2 moves this pair);
 *   towards anything else - the engine's call runs as before, but nothing is sent as kind 2 (the owner raised it already). */
const int kDeathEvPass = 0, kDeathEvSkipEngine = 1, kDeathEvNotSent = 2;
inline int CopyDeathEvDecide(bool inCopyDeath, int ev, bool towardsThisGamesPlayer)
{
    if (!inCopyDeath || ev != 5) return kDeathEvPass;
    return towardsThisGamesPlayer ? kDeathEvSkipEngine : kDeathEvNotSent;
}

/* KIND 1 ON THE OWNER'S GAME. The owner records the crime on its own character with notifyCrimeWitnessed (pvp1's refusal and the
 * engine's own still apply inside that call), except: not this game's character any more; a crime outside 1..kCrimeMaxEnum; a
 * crime against a PLAYER - by its faction or by the character it names (decision 4, as the owner's own sampler holds back). */
const int kOwnerWriteApply = 0, kOwnerWriteNotMine = 1, kOwnerWriteBad = 2, kOwnerWritePlayerVictim = 3;
inline int OwnerCopyWriteDecide(bool mine, int crime, bool victimFactionIsPlayer, bool victimIsPlayer)
{
    if (!mine) return kOwnerWriteNotMine;
    if (crime <= 0 || crime > kCrimeMaxEnum) return kOwnerWriteBad;
    if (victimFactionIsPlayer || victimIsPlayer) return kOwnerWritePlayerVictim;
    return kOwnerWriteApply;
}

/* THE OWNER'S CLEAR (kind 0, crime 0) ON A COPY. A copy holding a crime recorded HERE that was sent to the owner and is not yet in
 * its state carries a mark (its crime; it runs out after that crime's own expiry). 1 = keep the copy's crime and skip the clear:
 * the mark is live and the copy still holds that crime (`liveCrime`, read now). The mark ends when the owner's state holds the
 * crime (CopyMarkMet) or runs out. */
inline int CopyClearKeep(bool haveMark, int markCrime, bool markRunOut, int liveCrime)
{
    return (haveMark && !markRunOut && markCrime != 0 && liveCrime == markCrime) ? 1 : 0;
}
inline bool CopyMarkMet(int markCrime, int ownerCrime) { return markCrime != 0 && ownerCrime == markCrime; }

/* KIND 2. The FactionEvents that move a standing and name something the stand-in's player did to the faction (0 ATTACKED_US
 * defensively, 6, 7, 9, 10 and 11 change nothing in 0x6B29D0): 1 attacked aggressively, 2 / 3 defeated one of us, 4 killed
 * directly, 5 killed indirectly, 8 first aid, 12 captured. */
inline bool RelEventForwarded(int ev) { return ev == 1 || ev == 2 || ev == 3 || ev == 4 || ev == 5 || ev == 8 || ev == 12; }

/* The receiver's duplicate test: the last kRelEvSeenCap (sender, seq) pairs applied or refused. */
const size_t kRelEvSeenCap = 256;
struct RelEvSeen
{
    unsigned int peer[kRelEvSeenCap];
    unsigned int seq[kRelEvSeenCap];
    size_t n, at;
    RelEvSeen() : n(0), at(0) {}
    bool Has(unsigned int p, unsigned int s) const
    {
        for (size_t i = 0; i < n; ++i) if (peer[i] == p && seq[i] == s) return true;
        return false;
    }
    void Add(unsigned int p, unsigned int s)
    {
        peer[at] = p; seq[at] = s;
        at = (at + 1) % kRelEvSeenCap;
        if (n < kRelEvSeenCap) ++n;
    }
    void Clear() { n = 0; at = 0; }
};

/* T-356 (more than two players) - sight1's cells. The guards' senses recognise a wanted character on sight only when its faction
 * is a player faction; crime.cpp's stub also passes a faction held in one of these cells. They used to hold ONE faction (the one
 * other game on the session link); now ONE CELL PER OTHER PLAYER'S FACTION - each player's stand-in (coop-p<slot>) and the
 * protocol-67 `coop-peer` an old save carries. kSightCellCap is above the stand-in table's own bound (playerfaction.h
 * kStandInTableCap, plus that old coop-peer) - crime.cpp checks it at compile time - so no player a faction exists for is left
 * out. With one other player the cells hold exactly the one faction the old cell did. */
const int kSightCellCap = 40;

/* Fill cells[0..cap) from n candidates (slot, faction): a null faction and this game's own slot (mySlot >= 0) are skipped, a
 * faction named twice is held once (its first slot), the rest in ascending slot order (an unknown slot, < 0, first) so the cells
 * do not move about between ticks; unused cells are 0. Returns the number held; *dropped = distinct candidates that did not fit
 * (0 while cap is above the stand-in table). cap is clamped to kSightCellCap. */
inline int SightCellsFill(const int* slots, const void* const* facs, int n, int mySlot, const void** cells, int cap, int* dropped)
{
    if (cap > kSightCellCap) cap = kSightCellCap;
    if (cap < 0) cap = 0;
    int cs[kSightCellCap];
    int held = 0, drop = 0;
    for (int i = 0; i < cap; ++i) { cells[i] = 0; cs[i] = 0; }
    for (int i = 0; i < n; ++i)
    {
        const void* f = facs[i];
        const int sl = slots[i];
        if (f == 0) continue;
        if (mySlot >= 0 && sl == mySlot) continue;
        bool dup = false;
        for (int k = 0; k < held; ++k) if (cells[k] == f) { dup = true; break; }
        if (dup) continue;
        if (held >= cap) { ++drop; continue; }
        int at = held;
        while (at > 0 && cs[at - 1] > sl) { cells[at] = cells[at - 1]; cs[at] = cs[at - 1]; --at; }
        cells[at] = f; cs[at] = sl; ++held;
    }
    if (dropped != 0) *dropped = drop;
    return held;
}

/* The sight stub (crime.cpp SightPatch writes it on its own page; cellsOff = the cells' offset on that page: hits at cellsOff,
 * cell k at cellsOff + 8 + 8k). N = 14 + 13 * cells. It touches no register and never dereferences a cell.
 *    0  48 83 B8 50 02 00 00 00   cmp qword [rax+0x250], 0     the original test
 *    8  0F 85 <rel32>             jne BACK                      a player faction: ZF=0, as the original
 *   14  per cell k, at 14+13k:
 *         48 3B 05 <rel32>        cmp rax, [rip -> cell k]      an empty cell is 0 and never matches (rax was just read through)
 *         0F 84 <rel32>           je HIT
 *    N  48 83 B8 50 02 00 00 00   NOTPEER: the original compare again (ZF=1)
 *  N+8  EB 0C                     jmp BACK
 * N+10  F0 48 FF 05 <rel32>       HIT: lock inc qword [rip -> hits]
 * N+18  48 83 F8 00               cmp rax, 0                    ZF=0 CF=0 SF=0 OF=0, as the original on a player faction
 * N+22  FF 25 00000000 <abs64>    BACK: jmp [rip] -> site + 8 (the je, left in place)
 * Returns the length, kSightStubLen for kSightCellCap cells. */
const int kSightStubLen = 50 + 13 * kSightCellCap;
inline void SightPut32(unsigned char* at, int v) { std::memcpy(at, &v, 4); }
inline int SightStubBuild(unsigned char* p, unsigned long long site, int cells, int cellsOff)
{
    static const unsigned char test[8] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00 };
    const int N = 14 + 13 * cells;
    const int hit = N + 10, back = N + 22;
    std::memcpy(p, test, 8);
    p[8] = 0x0F; p[9] = 0x85; SightPut32(p + 10, back - 14);
    for (int k = 0; k < cells; ++k)
    {
        const int o = 14 + 13 * k;
        unsigned char* q = p + o;
        q[0] = 0x48; q[1] = 0x3B; q[2] = 0x05; SightPut32(q + 3, cellsOff + 8 + 8 * k - (o + 7));
        q[7] = 0x0F; q[8] = 0x84; SightPut32(q + 9, hit - (o + 13));
    }
    std::memcpy(p + N, test, 8);
    p[N + 8] = 0xEB; p[N + 9] = 0x0C;
    p[N + 10] = 0xF0; p[N + 11] = 0x48; p[N + 12] = 0xFF; p[N + 13] = 0x05; SightPut32(p + N + 14, cellsOff - (N + 18));
    p[N + 18] = 0x48; p[N + 19] = 0x83; p[N + 20] = 0xF8; p[N + 21] = 0x00;
    p[N + 22] = 0xFF; p[N + 23] = 0x25; SightPut32(p + N + 24, 0);
    const unsigned long long backTo = site + 8;
    std::memcpy(p + N + 28, &backTo, 8);
    return N + 36;
}

/* The TRESPASS stub (crime.cpp, site 0x858D13 inside the senses function 0x8587B0, which keeps its `this` in rbp from 0x8587CF:
 * the looker is [rbp+0x148], the engine's own reads at 0x858D2C and 0x858D80). The same head and cells as the sight stub; a cell
 * hit passes only when the LOOKER is not of this game's player faction - its owner faction ([looker+0x10], what
 * RootObjectBase::getOwnerFactionDirect 0x593BB0 reads) holds no player interface at +0x250. A null looker or a null owner
 * faction passes (the getter's own fallback is a world faction). A looker of this game's player faction leaves ZF=1 (the
 * original compare re-done on the copy's stand-in faction): the engine's not-a-player branch, as for an NPC target in single
 * player - this game's player characters never treat another player's copy as an intruder. rcx is pushed and popped (pop leaves
 * the flags); no other register is touched. The looker refusals are counted at refusedOff = cellsOff + 8 + 8 * kSightCellCap.
 *    0 .. N+9                     as the sight stub (BACK = N+74)
 * N+10  51                        HIT: push rcx
 * N+11  48 8B 8D 48 01 00 00      mov rcx, [rbp+0x148]          the looker
 * N+18  48 85 C9                  test rcx, rcx
 * N+21  74 26                     jz PASS
 * N+23  48 8B 49 10               mov rcx, [rcx+0x10]           its owner faction
 * N+27  48 85 C9                  test rcx, rcx
 * N+30  74 1D                     jz PASS
 * N+32  48 83 B9 50 02 00 00 00   cmp qword [rcx+0x250], 0      this game's player faction?
 * N+40  59                        pop rcx
 * N+41  74 13                     je PASS2                      not a player: the copy is a target
 * N+43  F0 48 FF 05 <rel32>       lock inc qword [rip -> refused]
 * N+51  48 83 B8 50 02 00 00 00   cmp qword [rax+0x250], 0      ZF=1: the stand-in is not a player faction
 * N+59  EB 0D                     jmp BACK
 * N+61  59                        PASS: pop rcx
 * N+62  F0 48 FF 05 <rel32>       PASS2: lock inc qword [rip -> hits]
 * N+70  48 83 F8 00               cmp rax, 0                    ZF=0, as the original on a player faction
 * N+74  FF 25 00000000 <abs64>    BACK: jmp [rip] -> site + 8
 * Returns the length, kTrespassStubLen for kSightCellCap cells. */
const int kTrespassStubLen = 102 + 13 * kSightCellCap;
inline int TrespassStubBuild(unsigned char* p, unsigned long long site, int cells, int cellsOff)
{
    static const unsigned char test[8] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00 };
    static const unsigned char gate[33] = {
        0x51,
        0x48, 0x8B, 0x8D, 0x48, 0x01, 0x00, 0x00,
        0x48, 0x85, 0xC9,
        0x74, 0x26,
        0x48, 0x8B, 0x49, 0x10,
        0x48, 0x85, 0xC9,
        0x74, 0x1D,
        0x48, 0x83, 0xB9, 0x50, 0x02, 0x00, 0x00, 0x00,
        0x59,
        0x74, 0x13 };
    const int N = 14 + 13 * cells;
    const int hit = N + 10, back = N + 74;
    const int refusedOff = cellsOff + 8 + 8 * kSightCellCap;
    std::memcpy(p, test, 8);
    p[8] = 0x0F; p[9] = 0x85; SightPut32(p + 10, back - 14);
    for (int k = 0; k < cells; ++k)
    {
        const int o = 14 + 13 * k;
        unsigned char* q = p + o;
        q[0] = 0x48; q[1] = 0x3B; q[2] = 0x05; SightPut32(q + 3, cellsOff + 8 + 8 * k - (o + 7));
        q[7] = 0x0F; q[8] = 0x84; SightPut32(q + 9, hit - (o + 13));
    }
    std::memcpy(p + N, test, 8);
    p[N + 8] = 0xEB; p[N + 9] = (unsigned char)(back - (N + 10));
    std::memcpy(p + hit, gate, 33);
    p[N + 43] = 0xF0; p[N + 44] = 0x48; p[N + 45] = 0xFF; p[N + 46] = 0x05; SightPut32(p + N + 47, refusedOff - (N + 51));
    std::memcpy(p + N + 51, test, 8);
    p[N + 59] = 0xEB; p[N + 60] = 0x0D;
    p[N + 61] = 0x59;
    p[N + 62] = 0xF0; p[N + 63] = 0x48; p[N + 64] = 0xFF; p[N + 65] = 0x05; SightPut32(p + N + 66, cellsOff - (N + 70));
    p[N + 70] = 0x48; p[N + 71] = 0x83; p[N + 72] = 0xF8; p[N + 73] = 0x00;
    p[N + 74] = 0xFF; p[N + 75] = 0x25; SightPut32(p + N + 76, 0);
    const unsigned long long backTo = site + 8;
    std::memcpy(p + N + 80, &backTo, 8);
    return N + 88;
}

} // namespace coopcrime
