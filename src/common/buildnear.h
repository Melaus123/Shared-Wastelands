// buildnear.h - T-923 (owner decision 951 a, 2026-10-19): the engine's "Can't build too close to another town." decision, as the
// build cursor's preview makes it (PreviewBuilding vt+0xC8 = 0x4DAFB0, Steam 1.0.65; Read from decomp_4dafb0.txt), so the TEST-ONLY
// lever `buildtest canbuild` (build.cpp BdTestCanBuild) can re-ask it at a spot and say which leg decided.
// The engine's steps, in its order:
//   1. t = the nearest town of any faction and any kind but dead (8) (TownListNearest 0x927040, faction 0, kind 10);
//   2. inside = the spot is in t (TownContainsPoint 0x925E80) at the scale 2.5 (1.0 when t's kind, town+0xD8, is 4 or 9), or
//      t's centre (town+0x48 / +0x50) is nearer than 2000 units on the ground plane;
//   3. (a second search over non-player towns, 0x9279C0, can swap t - not modelled here; the lever says so);
//   4. t is let go when inside is 0, or when t's faction (its vt+0x58) is non-null and a PLAYER faction (Faction+0x250, its
//      PlayerInterface*, non-zero) - the only exemption;
//   5. when t is still held and the piece's game data "creates player town" is false, inside is asked again at the scale 1.0
//      (the centre distance does not count here) and t is let go when it is 0.
//   The message shows while t is held.
// Pure (<cmath> only): build.cpp and the offline suite include this one header.
#ifndef BUILDNEAR_H
#define BUILDNEAR_H
#include <cmath>
namespace buildnear {
const float kScaleWide = 2.5f;       // step 2's scale for most towns
const float kScaleNarrow = 1.0f;     // step 2's scale for kinds 4 and 9, and step 5's scale
const float kCentreRange = 2000.0f;  // step 2: a centre nearer than this counts as inside
const int kKindNarrowA = 4, kKindNarrowB = 9;

// step 2's scale for a town of this kind (town+0xD8)
inline float ScaleForKind(int kind) { return (kind == kKindNarrowA || kind == kKindNarrowB) ? kScaleNarrow : kScaleWide; }
// step 2: in the town's area at its scale, or near its centre
inline int InsideFirst(int insideAtScale, float centreDist) { return (insideAtScale != 0 || centreDist < kCentreRange) ? 1 : 0; }
// the verdict: 1 refused ("Can't build too close to another town."), 0 allowed, -1 unknown (createsPlayerTown unread, -1, and
// the step-5 answer would depend on it). insideFirst is step 2's answer, insideAtOne step 5's.
inline int Refused(int hasTown, int insideFirst, int factionIsPlayer, int createsPlayerTown, int insideAtOne)
{
    if (hasTown == 0 || insideFirst == 0 || factionIsPlayer != 0) return 0;
    if (createsPlayerTown > 0) return 1;            // step 5 skipped: held at step 2's scale
    if (createsPlayerTown == 0) return insideAtOne != 0 ? 1 : 0;
    return insideAtOne != 0 ? 1 : -1;               // unread: refused either way when inside at 1.0, else unknown
}

// T-923 (decision 967 a; T-940 step 4 replaced decision 973 a's ring): the placement confirm's town pick (0x4D6300's call 0x927040 for
// this game's player faction; build.cpp BdTownPickRedirect):
//   inside one of this player's own towns (ownInside 1)               - the engine's own answer stands (the piece is this player's);
//   inside a CURRENT teammate's FIXED zone (mateZone kZoneInside: within kZoneRadius of the outpost's anchor, MateZoneFixed below)
//                                                                     - the teammate's town;
//   elsewhere                                                         - the engine's own answer.
// "Inside an own town" = the engine's own test TownContainsPoint 0x925E80 / 0x926D50 at the scale 1.0. An answer that did not read
// (-1) is never a yes. The caller (0x4D6300) keeps the answer only within 5000 of the spot (TownKept); past that it drops it and goes
// on to this player's own building within 2500, then founds 'Your Outpost' (Read, decomp_4d6300) - so a teammate's town past 5000, or
// whose distance did not read, is never the answer.
// 1 = the teammate's town, 0 = the engine's own answer stands.
const int kZoneOut = 0, kZoneInside = 2;
// the confirm keeps the town it picked only within this distance (0x4D642F, the float 5000.0 at 0x16BECB0, Steam 1.0.65)
const float kTownKeepRange = 5000.0f;
// the caller's keep test on a town at this ground-plane distance: 1 kept (within 5000), 0 none or past it, -1 a town whose distance
// did not read (dist < 0)
inline int TownKept(int haveTown, float dist)
{
    if (haveTown == 0) return 0;
    if (dist < 0.0f) return -1;
    return dist <= kTownKeepRange ? 1 : 0;
}
// why the pick does or does not take the teammate's town (TownPickWhy)
const int kPickTaken = 0, kPickNoMate = 1, kPickOwnInside = 2, kPickOut = 3, kPickMatePast = 4;
inline int TownPickWhy(int haveMate, int mateSlot, int mateZone, int ownInside, int mateKept)
{
    if (haveMate == 0 || mateSlot < 0) return kPickNoMate;
    if (ownInside != 0) return kPickOwnInside;
    if (mateZone != kZoneInside) return kPickOut;
    if (mateKept != 1) return kPickMatePast;
    return kPickTaken;
}
inline int TownPickTakesMate(int haveMate, int mateSlot, int mateZone, int ownInside, int mateKept)
{
    return TownPickWhy(haveMate, mateSlot, mateZone, ownInside, mateKept) == kPickTaken ? 1 : 0;
}

// T-940 (owner 1103 a, 1107 b, 2026-10-25): EVERY PLAYER OUTPOST'S FIXED ZONE. Each outpost has a fixed anchor (its founding spot, kept
// by the world server - townkeep.h THE FIXED ANCHOR - and sent to every game with the outposts, baseshow.h SHOW). Its zone is the circle
// of kZoneRadius around the anchor; around it lies a gap of kZoneGap. The owner's words: "a fixed radius that is considered part of the
// town but also a fixed distance so that the two radiuses can't overlap ... fixed ... to make sure changing the number doesn't make
// someones existing base inside the radius of another blocking their ability to building in their own established base". So:
//   - a spot inside one of this player's own zones, or a CURRENT teammate's, is always buildable (an old world whose anchors are already
//     closer than kAnchorApart keeps building in its own zone - only the other's zone and gap outside it refuse);
//   - else a spot within kZoneRadius + kZoneGap (7000 u = 700 m) of another, non-teammate player's anchor is refused (discovered or
//     not, 1103 a); teammates are never refused (decision 962);
//   - else a placement that would found a new outpost is refused closer than kAnchorApart (12000 u = 1.2 km: two zones and one shared
//     gap) to a non-teammate's anchor, so no two players' zones come to overlap.
// NPC towns keep the game's own rule (Refused above). 10 game units = 1 m. Distances on the ground plane (x, z).
const float kZoneRadius = 5000.0f;                         // 500 m: the zone around an outpost's anchor
const float kZoneGap = 2000.0f;                            // 200 m: the gap around a zone
const float kZoneRefuse = kZoneRadius + kZoneGap;          // 7000 u: a stranger's anchor this close (or closer) refuses the spot
const float kAnchorApart = 2.0f * kZoneRadius + kZoneGap;  // 12000 u: a new outpost's anchor at least this far from a stranger's
const int kRelOwn = 0, kRelMate = 1, kRelOther = 2;        // whose outpost an anchor is, seen from this game's player
const int kRelUnknown = 3;                                 // another player's, while the team table is not held - neither a zone nor a gap
struct ZoneAnchor { int owner; unsigned id; float x, z; int rel; };
// whose an anchor is - own row: kRelOwn; another player's: kRelMate / kRelOther by the CURRENT team table, and
// kRelUnknown while that table is not held (just after joining or a reconnect), so no player's row can refuse a teammate before the
// game knows who its teammates are
inline int ZoneRelOf(int ownRow, int tableKnown, int sameTeam)
{
    if (ownRow != 0) return kRelOwn;
    if (tableKnown == 0) return kRelUnknown;
    return sameTeam != 0 ? kRelMate : kRelOther;
}
inline float ZoneDist(const ZoneAnchor& a, float x, float z) { const float dx = a.x - x, dz = a.z - z; return std::sqrt(dx * dx + dz * dz); }
// the verdict at a spot (ZoneVerdict); *which = the anchor that decided it (-1 none), *dist its distance
const int kZoneFree = 0;          // allowed: no player's zone or gap holds the spot (the game's own rule alone)
const int kZoneOwnInside = 1;     // allowed: inside this player's own zone
const int kZoneMateInside = 2;    // allowed: inside a current teammate's zone (and no own zone)
const int kZoneOwnOverGap = 3;    // allowed: inside an own (or a teammate's) zone although within a stranger's zone + gap (old worlds)
const int kZoneRefusedGap = 4;    // refused: within kZoneRefuse of a stranger's anchor
const int kZoneRefusedFound = 5;  // refused: founds a new outpost closer than kAnchorApart to a stranger's anchor
inline int ZoneAllowed(int v) { return (v == kZoneRefusedGap || v == kZoneRefusedFound) ? 0 : 1; }
// founds: 1 = the placement would found a new outpost (the engine keeps no own outpost for it), 0 = it joins one, -1 unknown (never
// refused by the 12000 test - only the gap test decides it)
inline int ZoneVerdict(const ZoneAnchor* a, int n, float x, float z, int founds, int* which, float* dist)
{
    *which = -1; *dist = -1.0f;
    int own = -1, mate = -1, gap = -1, apart = -1;
    float ownD = 0.0f, mateD = 0.0f, gapD = 0.0f, apartD = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float d = ZoneDist(a[i], x, z);
        if (a[i].rel == kRelOwn) { if (d <= kZoneRadius && (own < 0 || d < ownD)) { own = i; ownD = d; } }
        else if (a[i].rel == kRelMate) { if (d <= kZoneRadius && (mate < 0 || d < mateD)) { mate = i; mateD = d; } }
        else if (a[i].rel == kRelOther)   // kRelUnknown (and anything else) neither grants nor refuses
        {
            if (d <= kZoneRefuse && (gap < 0 || d < gapD)) { gap = i; gapD = d; }
            if (d < kAnchorApart && (apart < 0 || d < apartD)) { apart = i; apartD = d; }
        }
    }
    if (own >= 0 || mate >= 0)
    {
        if (gap >= 0) { *which = gap; *dist = gapD; return kZoneOwnOverGap; }
        if (own >= 0) { *which = own; *dist = ownD; return kZoneOwnInside; }
        *which = mate; *dist = mateD; return kZoneMateInside;
    }
    if (gap >= 0) { *which = gap; *dist = gapD; return kZoneRefusedGap; }
    if (founds == 1 && apart >= 0) { *which = apart; *dist = apartD; return kZoneRefusedFound; }
    return kZoneFree;
}
inline const char* ZoneVerdictName(int v)
{
    return v == kZoneFree ? "free" : v == kZoneOwnInside ? "own-zone" : v == kZoneMateInside ? "teammate-zone" : v == kZoneOwnOverGap ? "own-zone-over-gap"
         : v == kZoneRefusedGap ? "refused-gap" : v == kZoneRefusedFound ? "refused-found" : "?";
}
// decision 973 replaced (T-940 step 4): a teammate's town takes the piece when the spot is inside the teammate's FIXED zone (kZoneRadius
// of its anchor) - kZoneInside, else kZoneOut; anchorDist < 0 (no anchor known) = -1
inline int MateZoneFixed(float anchorDist)
{
    if (anchorDist < 0.0f) return -1;
    return anchorDist <= kZoneRadius ? kZoneInside : kZoneOut;
}
// T-940: WHAT THE BUILD CURSOR'S ZONE HOOK DOES with the town the engine's near-town rule left in preview+0x90 (the 'too close
// to another town' town: set = the cursor shows the refusal and the click refuses, 0x4E2505), given the zone verdict at the spot:
//   held: kHeldNone (0), kHeldNpc (a town of no player), kHeldOwnOrMate (this player's or a current teammate's), kHeldOtherPlayer
//   (another player's outpost copy, a stand-in's town), -1 unread; copyLive: the refusing outpost's copy town stands here, live, with a
//   faction (the town the click's message reads, 0x4D5F30).
//   rule refuses:  an NPC town held - leave (the game's own refusal stands); else with the copy - kActRefuse (+0x90 = the copy); another
//                  player's copy already held and no copy of the refusing outpost - leave (refused already); else kActNoCopy (leave,
//                  counted: no live town can carry the refusal safely)
//   rule allows:   another player's copy held - kActClear (+0x90 = 0: the engine's ring around copies is replaced by the zone rule);
//                  anything else - leave (an NPC town's rule stands; own and teammates' towns are T-923's)
//   unread held:   leave
const int kHeldNone = 0, kHeldNpc = 1, kHeldOwnOrMate = 2, kHeldOtherPlayer = 3;
const int kActLeave = 0, kActRefuse = 1, kActClear = 2, kActNoCopy = 3;
inline int ZoneCursorAct(int verdict, int held, int copyLive)
{
    if (held < 0 || held > kHeldOtherPlayer) return kActLeave;
    if (ZoneAllowed(verdict) == 0)
    {
        if (held == kHeldNpc) return kActLeave;
        if (copyLive != 0) return kActRefuse;
        return held == kHeldOtherPlayer ? kActLeave : kActNoCopy;
    }
    return held == kHeldOtherPlayer ? kActClear : kActLeave;
}
// THE NPC TOWN BEHIND A STRANGER'S COPY: the engine's rule tests only the NEAREST town, so another player's copy standing nearer than an
// NPC town hides that NPC town's ring. The hook therefore asks for the nearest town as the engine would see it with strangers' copies left
// out (BdZoneNpcBehind, each town judged by NpcBehindKind): this player's own town or a current teammate's copy nearest = no NPC town
// (the engine lets a player build by its own town); an NPC town nearest = the engine's own rule (Refused) applied to it. Asked when:
//   the rule clears another player's copy (kActClear), or refuses with no copy to show it (kActNoCopy), or the slot is empty while the
//   nearest town of all is another player's (playerNearest 1: a teammate's copy the T-923 stub forgot, or one outside its own ring).
// Never when the zone rule refuses with the copy (the stranger's refusal stands) or the slot holds an NPC town or did not read.
// one town of that look-up, from its kind (kHeld*): kNbOwn (own or a current teammate's: a candidate that, when nearest, means no NPC
// town), kNbNpc (a candidate), kNbSkip (another player's copy, or unread: left out)
const int kNbSkip = 0, kNbOwn = 1, kNbNpc = 2;
inline int NpcBehindKind(int held)
{
    if (held == kHeldOwnOrMate) return kNbOwn;
    if (held == kHeldNpc) return kNbNpc;
    return kNbSkip;
}
inline int ZoneNpcAsk(int act, int held, int playerNearest)
{
    if (act == kActRefuse || held < 0 || held == kHeldNpc) return 0;
    if (act == kActClear || act == kActNoCopy) return 1;
    return (held == kHeldNone && playerNearest != 0) ? 1 : 0;
}
// what goes into the town slot: kSlotLeave (as the engine set it), kSlotCopy (the refusing outpost's copy), kSlotNone (0), kSlotNpc (the
// NPC town behind, npcRefuses 1: the engine's rule refuses for it)
const int kSlotLeave = 0, kSlotCopy = 1, kSlotNone = 2, kSlotNpc = 3;
inline int ZoneCursorSlot(int act, int held, int playerNearest, int npcRefuses)
{
    if (act == kActRefuse) return kSlotCopy;
    if (ZoneNpcAsk(act, held, playerNearest) != 0 && npcRefuses != 0) return kSlotNpc;
    return act == kActClear ? kSlotNone : kSlotLeave;
}
// founding at the cursor (Inferred from 0x4D6300, the commit's per-piece town choice): the piece's record says 'creates player town'
// (creates 1) and no own outpost is within the confirm's keep range (ownNear 0); unread either way = -1 (never refuses by itself: a
// faulted or missing own-town lookup is ownNear -1). 0x4D6300's second road (an own building within 2500, decomp_4d6300:101-112)
// is not counted: it also needs that building's ground distance from the spot past 0x14168CAE4's value while its 3-D distance is under
// 2500, which cannot hold together if those values are 2500 / 10000 (05-findings:20915, Inferred) - so it never makes a piece join.
inline int ZoneFounds(int creates, int ownNear)
{
    if (creates < 0 || ownNear < 0) return -1;
    return (creates == 1 && ownNear == 0) ? 1 : 0;
}
// THE PATCH SITE (rows PreviewNearTownDone: 1.0.65 0x4DB2F6, 1.0.68 0x4DBD86), where 0x4DAFB0's three town roads join after the town
// decision: `cmp dword [rsi+0x38], 0` (83 7E 38 00) / `jne +0x69` (75 69 -> site + 0x6F). The first 6 bytes become `jmp rel32 stub` + nop
// (ZoneSitePatch). THE STUB (ZoneStubBuild, kZoneStubLen bytes), entered with rsp 16-aligned and rsi = the preview:
//      0  push rax, rcx, rdx, r8, r9, r10, r11              (the volatile registers; 56 bytes pushed)
//     11  sub rsp, 0x88                                     (shadow 0x20 + xmm0..5 at 0x20..0x7F + 8: rsp 16-aligned at the call)
//     18  movdqu [rsp+0x20+16*i], xmm i   (i = 0..5)
//     54  mov rcx, rsi ; mov rax, callee ; call rax         (the rule: void callee(void* preview); it must never throw)
//     69  movdqu xmm i, [rsp+0x20+16*i] ; add rsp, 0x88 ; pop r11 .. rax
//    123  cmp dword [rsi+0x38], 0                           (the replaced compare, re-done: the flags the jne reads)
//    127  jne +14 ; jmp [rip] -> site + 6 ; jmp [rip] -> site + 0x6F
const int kZoneSitePatchLen = 6;
const unsigned char kZoneSiteOrig[kZoneSitePatchLen] = { 0x83, 0x7E, 0x38, 0x00, 0x75, 0x69 };
const int kZoneSiteJneTo = 0x6F;   // the jne's target from the site: site + 6 + 0x69
const int kZoneStubLen = 157;
inline void ZonePutU64(unsigned char* p, unsigned long long v) { for (int i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (8 * i)); }
// the stub's bytes into p (kZoneStubLen); the number written
inline int ZoneStubBuild(unsigned char* p, unsigned long long site, unsigned long long callee)
{
    int n = 0;
    static const unsigned char kPush[11] = { 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53 };
    for (int i = 0; i < 11; ++i) p[n++] = kPush[i];
    static const unsigned char kSub[7] = { 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00 };
    for (int i = 0; i < 7; ++i) p[n++] = kSub[i];
    for (int x = 0; x < 6; ++x) { p[n++] = 0xF3; p[n++] = 0x0F; p[n++] = 0x7F; p[n++] = (unsigned char)(0x44 | (x << 3)); p[n++] = 0x24; p[n++] = (unsigned char)(0x20 + 16 * x); }
    p[n++] = 0x48; p[n++] = 0x89; p[n++] = 0xF1;                      // mov rcx, rsi
    p[n++] = 0x48; p[n++] = 0xB8; ZonePutU64(p + n, callee); n += 8;  // mov rax, callee
    p[n++] = 0xFF; p[n++] = 0xD0;                                     // call rax
    for (int x = 0; x < 6; ++x) { p[n++] = 0xF3; p[n++] = 0x0F; p[n++] = 0x6F; p[n++] = (unsigned char)(0x44 | (x << 3)); p[n++] = 0x24; p[n++] = (unsigned char)(0x20 + 16 * x); }
    static const unsigned char kAdd[7] = { 0x48, 0x81, 0xC4, 0x88, 0x00, 0x00, 0x00 };
    for (int i = 0; i < 7; ++i) p[n++] = kAdd[i];
    static const unsigned char kPop[11] = { 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58 };
    for (int i = 0; i < 11; ++i) p[n++] = kPop[i];
    for (int i = 0; i < 4; ++i) p[n++] = kZoneSiteOrig[i];            // cmp dword [rsi+0x38], 0
    p[n++] = 0x75; p[n++] = 0x0E;                                     // jne over the next jump
    p[n++] = 0xFF; p[n++] = 0x25; p[n++] = 0; p[n++] = 0; p[n++] = 0; p[n++] = 0; ZonePutU64(p + n, site + kZoneSitePatchLen); n += 8;
    p[n++] = 0xFF; p[n++] = 0x25; p[n++] = 0; p[n++] = 0; p[n++] = 0; p[n++] = 0; ZonePutU64(p + n, site + kZoneSiteJneTo); n += 8;
    return n;
}
// the 6 bytes written at the site: jmp rel32 to the stub, nop; 0 = the stub is out of a rel32 jump's reach
inline int ZoneSitePatch(unsigned char* p, unsigned long long site, unsigned long long stub)
{
    const long long rel = (long long)stub - (long long)(site + 5);
    if (rel > 0x7FFFFFF0LL || rel < -0x7FFFFFF0LL) return 0;
    const unsigned int r = (unsigned int)(int)rel;
    p[0] = 0xE9; p[1] = (unsigned char)r; p[2] = (unsigned char)(r >> 8); p[3] = (unsigned char)(r >> 16); p[4] = (unsigned char)(r >> 24); p[5] = 0x90;
    return 1;
}
// the own-zone pick (T-940 step 3): of this player's own anchors, the index whose zone holds the spot (the nearest anchor), -1 none
inline int OwnZoneAt(const ZoneAnchor* a, int n, float x, float z)
{
    int best = -1;
    float bd = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        if (a[i].rel != kRelOwn) continue;
        const float d = ZoneDist(a[i], x, z);
        if (d <= kZoneRadius && (best < 0 || d < bd)) { best = i; bd = d; }
    }
    return best;
}
}   // namespace buildnear
#endif
