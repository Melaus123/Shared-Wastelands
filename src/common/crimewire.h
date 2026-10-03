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

} // namespace coopcrime
