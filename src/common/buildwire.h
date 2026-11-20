/* src/common/buildwire.h - build1-b/c/d/e/f (docs/design-build1.md 2-3; session protocol 63). A construction THIS game's player
 * faction placed crosses to the other game, which creates a COPY owned by coop-peer at its K2 safe point
 * (src/coop-plugin/build.cpp BuildCopyDrain, the "Safe call recipe" of build/answers-build1b.md).
 *
 *   kind u8 (1 PLACE) | u8 len + P7n key | u8 len + record sid | 3 x f32 pos | 4 x f32 rot (Ogre w,x,y,z)
 *   | u8 complete (0/1) | f32 progress | f32 needed | u8 len + host key (empty for a free-standing piece)
 *   | u8 host form | i32 floor | u8 outside (build1-e, protocol 62: furniture - the host's P7n key, which form the
 *   builder's furnitureOf took (0 *(host+0x1F0)+0x30, 1 that layout's vt+0x40, 2 none: isIndoorsOf only), and
 *   createBuilding's floor / outsideFurniture; pos / rot are then in the HOST'S LOCAL frame)
 *   kind u8 (2 STATE, build1-c) | u8 len + P7n key | f32 progress | f32 needed | u8 complete (0/1)
 *   | u8 n (<= 16) | n x f32 delivered (ConstructionState mats[i]+0xC, in the engine's own material order)
 *   P15 (protocol 98): the STATE's complete byte carries bit 1 = DESTROYED (the owner's piece is a ruin, Building +0x1A1); bit 2 =
 *   BROKEN (Building +0x3B6, game-to-game protocol 156); any other bit is refused
 *   kind u8 (3 REMOVE, build1-d) | u8 len + P7n key | u8 reason (1 dismantled, 2 destroyed): the builder's piece is gone,
 *   and the other game removes its copy (zeroed delivered mats, then the engine's vt+0x248(FLT_MAX))
 *   kind u8 (4 ROSTER_REQUEST, build1-f fold, protocol 63) and nothing else: the sender's world runs and the link is up;
 *   the receiver owes it a whole roster (a PLACE + STATE for every own live piece)
 *
 * A PLACE's rotation must be a plausible quaternion: |q|^2 in [0.5, 2] (review-build1b note 4).
 * T-1372 decision 1691 (game-to-game protocol 229): a PLACE may end in ONE more byte, kBuildGroundTag, after everything else (the
 * nonce, the town tail): 'the placement input was measured the engine's way' (placeinput.h ConvertOldInput). Every PLACE this build
 * writes carries it (groundMark 1); one without it (a record written by an earlier build) is converted where it is used.
 *
 * pos / rot are the exact floats the builder's createBuilding received. Every message carries the piece's WHOLE state.
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <cstdlib>    /* strtod: the into lever's offset (BuildIntoOffsetParse) */
#include <string>
#include <map>
#include <set>        /* house2 fold 2: the keys erased while the owed file was unread */
#include <vector>
#include <algorithm>  /* help1 fold 2: BuildHelpResync sorts */
#include <cstdio>     /* T-903: BuildDeadSetSig */

namespace coopbuild {

const unsigned char kBuildPlace = 1;
const unsigned char kBuildState = 2;     /* build1-c */
const unsigned char kBuildRemove = 3;    /* build1-d */
const unsigned char kBuildRosterRequest = 4;
const unsigned char kBuildHandAck = 5;         /* house1b (protocol 75): the house owner holds a piece handed to it - the key only */   /* build1-f fold (review-build1f H2): the kind byte only */
const unsigned char kBuildHelpWork = 8;        /* help1 (owner 172, protocol 90): a helper's work on its copy of another player's piece, to the owner's game (6 / 7 are the farm's, 10 MINE_OP);
                                                  help1 fold 3 (protocol 95): + a trailing u32 nonce - the owner's placement the work was done on */
const unsigned char kBuildDismantleAsk = 12;   /* T-805 (owner 797 a): kind | key | u8 verdict - verdict kBuildDmAsk = a team member's game asks the piece owner's
                                                  game to dismantle that piece; any other verdict is the owner's answer (BuildDismantleAskVerdict).
                                                  T-609: MSG_BUILD's kind byte is shared with farmwire.h (6 / 7 / 10 / 11 / 13-15, coopfarm::IsFarmKind,
                                                  peeked first by session.cpp OnBuild) - a new kind here must take a number IsFarmKind refuses
                                                  (test t609_build_and_farm_kinds_never_share_a_number) */
const unsigned char kBuildHit = 18;            /* kind | key | u32 seq | f32 cut | f32 blunt | u8 verdict | i8 door | u8 repair - verdict kBuildHitAsk = a melee hit on a player's wall
                                                  (the damage struct's +0 / +4), asked of the game that writes that wall (the machines' writer: the owner's game while it is
                                                  online and holds the area, else the area holder); verdicts 1-6 are that game's answer for the seq (BuildHitWord), after
                                                  which the asker stops sending it (6 'not the writer': the asker asks the writer once more).
                                                  The RESULT form, verdict kBuildHitResult: kind | key | u32 seq | f32 drop | f32 needed | u8 7 | u8 flags | f32 after -
                                                  a writer that is not the wall's owner sends what its hit did to every game: drop = the progress the hit took (>= 0, raw:
                                                  the engine's hit removes a fixed amount), after / needed = the owner's health after it as the writer expects it (the
                                                  share after / needed is what is compared), flags bit 0 = the hit made the wall a ruin, bit 1 = owed (sent again to the
                                                  owner's game only: the summed drop of every hit on that wall that game did not take). 16 / 17 are powerwire.h's */
const unsigned char kBuildHitFlagDestroyed = 1, kBuildHitFlagOwed = 2;
/* An ask or an answer (any verdict but kBuildHitResult) ends in i8 door | u8 repair: door = the index of the hit door in its piece's door
   list (-1 = the piece itself, a wall); repair 1 (an ask naming a door only) = not a hit but the health a repair gained on a game that
   does not write the piece (cut = that health, blunt 0), for the writer to add to its door. The RESULT form carries neither. */
const int kBuildHitMaxDoor = 15;
const unsigned char kBuildHitAsk = 0, kBuildHitApplied = 1, kBuildHitGone = 2, kBuildHitNotLoaded = 3, kBuildHitRuin = 4, kBuildHitFaulted = 5,
                    kBuildHitNotWriter = 6, kBuildHitResult = 7;
const unsigned char kBuildHitLastVerdict = 7;
const float         kBuildHitMax = 1.0e6f;      /* a HIT's cut or blunt above this (or below 0) is refused */
const unsigned char kBuildHelpGone = 9;        /* help1 fold (review MED 3, protocol 91): the owner's answer to work for a piece it no longer holds as its own (removed, dismantled,
                                                  handed, its removed row swept) - key | u32 seq: the helper drops every entry at or below seq and hands their materials back.
                                                  help1 fold 2 (protocol 92): key | u32 seq | u32 applied - the owner's last applied seq from that helper for that key
                                                  (0 = none): entries at or below it WERE applied (dropped, never handed back); only those above it come back.
                                                  help1 fold 3 (protocol 95): key | u32 seq | u32 applied | u32 nonce | u8 n | n x f32 refused - the placement the
                                                  work was done on (the HELP_WORK's nonce) and that helper slot's CUMULATIVE refused amounts for it (the STATE tail's),
                                                  handed back less what was already handed back; sent only once the owner's record holding them has landed */
const unsigned char kBuildHelpHeld = 21;       /* T-1408 fold 2 (protocol 234 - the fold's own number, unmerged): kind | key | u8 why - the game that keeps a helper's
                                                  grouped-wall work HELD (the owner's game with its group not wholly loaded, or the copy's T-23 writer) tells the
                                                  helpers: why kBuildHeldNotWhole / kBuildHeldWriter = held there (the helpers' builders stop that job),
                                                  kBuildHeldReleased = no longer held (applied, or the held work dropped). 19 / 20 are powerwire.h's */
const unsigned char kBuildHeldReleased = 0, kBuildHeldNotWhole = 1, kBuildHeldWriter = 2;
const unsigned int  kBuildMaxHelpers = 8;      /* help1: helper slots a STATE's confirmation tail carries */
const unsigned char kBuildHelpTailLive = 0x80;  /* help1 fold 3 (protocol 95): set on a STATE tail's row count - each row carries u32 live after seq */
const unsigned char kBuildHelpTailWide = 0x40;  /* set on a STATE tail's row count: each row's helper slot is two bytes (little-endian, 0..1023);
                                                  without it (a pp.build record written before) the slot is one byte */
const float         kBuildHelpMax = 1.0e6f;    /* help1: a HELP_WORK progress or material delta above this (or below 0) is refused */
const unsigned char kBuildReasonDismantled = 1;   /* build1-d REMOVE reasons */
const unsigned char kBuildReasonDestroyed = 2;
const unsigned char kBuildStateDestroyedBit = 2;   /* P15 (protocol 98): a STATE's complete byte, bit 1 = destroyed */
const unsigned char kBuildStateBrokenBit = 4;      /* a STATE's complete byte, bit 2 = broken (+0x3B6) */
const unsigned char kBuildStateStampBit = 8;       /* a STATE's complete byte, bit 3 = a u32 wall group step stamp (not 0) follows the materials */
const unsigned char kBuildStateGroupBit = 16;      /* T-1408 fold 13 (protocol 235): bit 4 = the wall's GROUP FACT follows the stamp - u8 n + the
                                                     group lead's key (n 1..kBuildMaxKey), u16 the wall's join index (0 = the wall is the lead) */
const unsigned short kBuildGroupIdxMax = 1024;    /* T-1408 fold 13: the highest join index a STATE may carry */
const unsigned char kBuildHostPlain = 0;        /* build1-e: furnitureOf = *(host+0x1F0)+0x30 (the host's interior layout) */
const unsigned char kBuildHostSub = 1;          /* build1-e: that layout's vt+0x40 (the commit's other branch, 4d6810:176) */
const unsigned char kBuildHostIndoorsOnly = 2;  /* build1-e: no furnitureOf, isIndoorsOf = host only */
const int           kBuildMaxFloor = 1024;      /* build1-e: |floor| limit */
const unsigned int  kBuildMaxMats = 16;  /* build1-c: delivered-material slots a STATE carries */
const unsigned int  kBuildMaxKey = 63;   /* build.cpp kKeyCap (64) less its NUL */
const unsigned int  kBuildMaxSid = 95;

const int kBuildDecodeOk       = 0;
const int kBuildDecodeTooShort = 1;
const int kBuildDecodeBadKind  = 2;   /* PLACE (1), STATE (2), REMOVE (3, build1-d) and ROSTER_REQUEST (4, build1-f) exist */
const int kBuildDecodeBadKey   = 3;   /* an empty key or sid, or a key / sid / host key over its limit */
const int kBuildDecodeBadValue = 4;   /* a float that is not finite, complete not 0/1, |rot|^2 outside [0.5, 2], n > 16, or a REMOVE reason not 1/2 */

/* help1: one row of a STATE's optional confirmation tail - per helper slot, the last HELP_WORK seq the owner's game applied to this piece
   and the CUMULATIVE material amounts it refused (past a material's total); the helper's game hands those back as items.
   help1 fold 3 (finding 1): on the wire seq is the LANDED last applied (on the owner's disk) and live the owner's live last applied */
struct BuildHelpAck
{
    unsigned short slot;    /* the helper's notebook slot, 0..kBuildOwnerSlotMax */
    unsigned int seq;
    unsigned int live;      /* help1 fold 3: a STATE tail's live last applied (>= seq; 0 in the owner's own rows, which hold the live seq in seq) */
    unsigned char nEx;
    float ex[kBuildMaxMats];
    BuildHelpAck() : slot(0), seq(0), live(0), nEx(0) { for (unsigned int i = 0; i < kBuildMaxMats; ++i) ex[i] = 0.0f; }
};

/* P87 fold 1: a REMOVE's optional tail - kind | key | reason [| u32 nonce [| u8 flags [| hostKey when kBuildRmFurniture]]], each part
   written only when it or a later one is set. The nonce is the placement the REMOVE ends (0 = unknown). An older receiver's decode
   stops after the reason (it never read past it), so the tail costs it nothing. */
const unsigned char kBuildRmResend = 1;      /* wire: a tombstone's REMOVE sent again (a roster round) - the receiver counts a miss apart */
const unsigned char kBuildRmTombTail = 2;    /* pp.build tombstone row: written with the fold-1 tail (its nonce and furniture-ness known) */
const unsigned char kBuildRmFurniture = 4;   /* pp.build tombstone row: layout furniture - its host's key follows */
struct BuildMsg
{
    unsigned char kind;
    std::string key, sid, hostKey;
    float pos[3];
    float rot[4];
    unsigned char complete;
    unsigned char destroyed;        /* P15 (protocol 98) STATE: 1 = the owner's piece is a ruin (+0x1A1) - the complete byte's bit 1 */
    unsigned char broken;           /* STATE: 1 = the owner's piece is broken (+0x3B6) - the complete byte's bit 2 */
    float progress, needed;
    unsigned char nMats;            /* build1-c STATE: how many of mats[] are carried */
    unsigned char reason;           /* build1-d REMOVE: kBuildReasonDismantled / kBuildReasonDestroyed */
    unsigned char hostForm;         /* build1-e PLACE: kBuildHost* (0 when hostKey is empty) */
    int floor;                      /* build1-e PLACE: createBuilding's floor */
    unsigned char outside;          /* build1-e PLACE: createBuilding's outsideFurniture (0/1) */
    unsigned short ownerSlot;       /* PLACE: the piece's owner - 0..kBuildOwnerSlotMax = handed to that player (a house owner), kBuildOwnerSender = the sender; two bytes on the wire */
    unsigned int nonce;             /* house2 fold PLACE (protocol 76): the placer's per-placement nonce, 0 = none (written only when non-zero); help1 fold 3 HELP_WORK / HELP_GONE: the placement the work was done on */
    unsigned int seq;               /* help1 HELP_WORK: the helper's per-piece sequence (> 0); progress = dProgress, nMats / mats = dMat */
    unsigned char reset;            /* help1 HELP_WORK: 1 = the helper's copy was a ruin and the work rebuilt it (the engine's reset) */
    unsigned int applied;           /* help1 fold 2 HELP_GONE (protocol 92): the owner's last applied seq from that helper for that key (0 = none); fold 3: nMats / mats = its refused tail */
    unsigned char rmFlags;          /* P87 fold 1 REMOVE: kBuildRm* (0 = none); nonce = the placement it ends, hostKey = a furniture tombstone's host */
    unsigned char townHand;         /* T-923 (protocol 167) PLACE: 1 = an outdoor piece handed to ownerSlot because it was placed in that player's outpost town;
                                       townPos = that town's centre (x, z) as the world server's outpost list gives it - the owner's game makes the piece
                                       in its own town standing there, at any distance from the piece (a grouped placement's far piece). 0 = none */
    float townPos[2];
    unsigned char groundMark;       /* T-1372 decision 1691 (protocol 229) PLACE: 1 = the input was measured the engine's way (this build's), the kBuildGroundTag byte; 0 = an older record (converted where used) */
    float hitCut, hitBlunt;         /* HIT: the damage struct's +0 / +4 the hit carried (seq = the asker's number, reason = the verdict) */
    float hitDrop, hitAfter;        /* HIT result: the raw progress the hit (owed: the hits) took, >= 0; the owner's health the writer expects after it, in units of needed */
    unsigned char hitOwed;          /* HIT result: 1 = owed - the writer sends it again to the owner's game only */
    int hitDoor;                    /* HIT ask / answer: the door's index in its piece's door list, -1 = the piece itself */
    unsigned char hitRepair;        /* HIT ask naming a door: 1 = a repair's gained health (hitCut) to add, not a hit */
    std::vector<BuildHelpAck> helpAcks;   /* help1 STATE: the confirmation tail (empty = not written); fold 3: each row landed seq + live */
    unsigned int groupStamp;        /* STATE: a wall group's step stamp (kBuildStateStampBit; 0 = none - not a wall, or an older build's record) */
    std::string groupLead;          /* T-1408 fold 13 STATE: the owner's group fact - the key of the wall's group lead (the wall's own key when it leads
                                       or stands alone); empty = no fact (not a wall, or a record written before fold 13) - kBuildStateGroupBit */
    unsigned short groupIdx;        /* ... and the wall's join index in the lead's member order (0 = the lead; 1.. = the members, in the owner's order) */
    float mats[kBuildMaxMats];
    BuildMsg() : kind(0), complete(0), destroyed(0), broken(0), progress(0.0f), needed(0.0f), nMats(0), reason(0), hostForm(0), floor(0), outside(0), ownerSlot(0xFFFF),
                 nonce(0), seq(0), reset(0), applied(0), rmFlags(0), townHand(0), groundMark(0), hitCut(0.0f), hitBlunt(0.0f), hitDrop(0.0f), hitAfter(0.0f), hitOwed(0), hitDoor(-1), hitRepair(0), groupStamp(0), groupIdx(0)
    {
        townPos[0] = townPos[1] = 0.0f;
        for (int i = 0; i < 3; ++i) pos[i] = 0.0f;
        rot[0] = 1.0f; rot[1] = 0.0f; rot[2] = 0.0f; rot[3] = 0.0f;
        for (unsigned int i = 0; i < kBuildMaxMats; ++i) mats[i] = 0.0f;
    }
};

const unsigned short kBuildOwnerSender = 0xFFFF;   /* a PLACE's ownerSlot meaning "the sender's own piece" */
const size_t kBuildTownTail = 9;                  /* T-923 (protocol 167): a PLACE's town tail - the tag byte, then the town centre's x and z */
const unsigned char kBuildTownTag = 1;
const unsigned char kBuildGroundTag = 0x47;   /* T-1372 decision 1691 (protocol 229): a PLACE's last byte - its input was measured from the engine's ground (0x9B2850) */
const unsigned short kBuildOwnerSlotMax = 1023;   /* the notebook's slots 0..1023 (slotwire.h kSlotMax) */
const unsigned char kBuildOwnerSenderOneByte = 0xFF;   /* the sender, in a PLACE whose owner number is one byte (pp.build rows and owed-file rows saved by builds that wrote one byte) */

inline bool BuildFloatOk(float v) { return v == v && v > -1.0e30f && v < 1.0e30f; }
/* review-build1b note 4: (0,0,0,0) or a huge quaternion is refused; an honest builder sends a unit one */
inline bool BuildRotOk(const float* q)
{
    for (int i = 0; i < 4; ++i) if (!BuildFloatOk(q[i]) || q[i] > 2.0f || q[i] < -2.0f) return false;
    const float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    return n >= 0.5f && n <= 2.0f;
}

/* help1: a HELP_WORK delta - finite, 0..kBuildHelpMax */
inline bool BuildHelpAmountOk(float v) { return BuildFloatOk(v) && v >= 0.0f && v <= kBuildHelpMax; }
/* help1: a STATE's tail - at most kBuildMaxHelpers rows, distinct slots, <= 16 finite non-negative amounts; help1 fold 3: a row names a
   landed seq or a live one (seq 0 = nothing of that slot's on disk yet, live > 0 = applied since) - never both 0 */
inline bool BuildHelpAcksOk(const std::vector<BuildHelpAck>& a)
{
    if (a.size() > kBuildMaxHelpers) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if ((a[i].seq == 0 && a[i].live == 0) || a[i].nEx > kBuildMaxMats || a[i].slot > kBuildOwnerSlotMax) return false;
        for (size_t j = 0; j < i; ++j) if (a[j].slot == a[i].slot) return false;
        for (unsigned int k = 0; k < a[i].nEx; ++k) if (!BuildFloatOk(a[i].ex[k]) || a[i].ex[k] < 0.0f) return false;
    }
    return true;
}

inline bool BuildEncodable(const BuildMsg& m)
{
    if (m.kind == kBuildHelpWork)   /* help1 */
    {
        if (m.key.empty() || m.key.size() > kBuildMaxKey || m.seq == 0 || m.reset > 1 || m.nMats > kBuildMaxMats) return false;
        for (unsigned int i = 0; i < m.nMats; ++i) if (!BuildHelpAmountOk(m.mats[i])) return false;
        return BuildHelpAmountOk(m.progress);
    }
    if (m.kind == kBuildHelpGone)   /* help1 fold: key + seq; fold 3: + the refused amounts (<= 16, finite, non-negative) */
    {
        if (m.key.empty() || m.key.size() > kBuildMaxKey || m.seq == 0 || m.nMats > kBuildMaxMats) return false;
        for (unsigned int i = 0; i < m.nMats; ++i) if (!BuildFloatOk(m.mats[i]) || m.mats[i] < 0.0f) return false;
        return true;
    }
    if (m.kind == kBuildRosterRequest) return m.key.empty() && m.sid.empty() && m.hostKey.empty();   /* build1-f: no body */
    if (m.kind == kBuildHandAck) return !m.key.empty() && m.key.size() <= kBuildMaxKey;   /* house1b: the key only */
    if (m.kind == kBuildHelpHeld) return !m.key.empty() && m.key.size() <= kBuildMaxKey && m.reason <= kBuildHeldWriter;   /* T-1408 fold 2: key + why */
    if (m.kind == kBuildHit && m.reason == kBuildHitResult)   /* key | seq (> 0) | drop (finite, >= 0), needed (finite, >= 0) | destroyed 0/1, owed 0/1 | after (finite) */
        return !m.key.empty() && m.key.size() <= kBuildMaxKey && m.seq != 0 && BuildFloatOk(m.hitDrop) && m.hitDrop >= 0.0f && BuildFloatOk(m.needed) && m.needed >= 0.0f
            && m.destroyed <= 1 && m.hitOwed <= 1 && BuildFloatOk(m.hitAfter) && m.hitDoor == -1 && m.hitRepair == 0;
    if (m.kind == kBuildHit)   /* key | seq (> 0) | cut, blunt (finite, 0..kBuildHitMax) | verdict */
        return !m.key.empty() && m.key.size() <= kBuildMaxKey && m.seq != 0 && m.reason <= kBuildHitLastVerdict
            && BuildFloatOk(m.hitCut) && BuildFloatOk(m.hitBlunt) && m.hitCut >= 0.0f && m.hitBlunt >= 0.0f && m.hitCut <= kBuildHitMax && m.hitBlunt <= kBuildHitMax
            && m.hitDoor >= -1 && m.hitDoor <= kBuildHitMaxDoor && m.hitRepair <= 1
            && (m.hitRepair == 0 || (m.reason == kBuildHitAsk && m.hitBlunt == 0.0f));   /* a repair: an ask naming a door, or (T-1333, door -1) a town wall, its gain in cut */
    if (m.kind == kBuildDismantleAsk) return !m.key.empty() && m.key.size() <= kBuildMaxKey && m.reason <= 7;   /* T-805: key + verdict (kBuildDmBlocked the last) */
    if (m.kind == kBuildRemove)   /* build1-d; P87 fold 1: a furniture tail's host key fits */
        return !m.key.empty() && m.key.size() <= kBuildMaxKey
            && (m.reason == kBuildReasonDismantled || m.reason == kBuildReasonDestroyed)
            && ((m.rmFlags & kBuildRmFurniture) == 0 || m.hostKey.size() <= kBuildMaxKey);
    if (m.kind == kBuildState)
    {
        if (m.key.empty() || m.key.size() > kBuildMaxKey || m.complete > 1 || m.destroyed > 1 || m.broken > 1 || m.nMats > kBuildMaxMats) return false;
        for (unsigned int i = 0; i < m.nMats; ++i) if (!BuildFloatOk(m.mats[i])) return false;
        if (!BuildHelpAcksOk(m.helpAcks)) return false;   /* help1 */
        if (m.groupLead.size() > kBuildMaxKey || m.groupIdx > kBuildGroupIdxMax || (m.groupLead.empty() && m.groupIdx != 0)) return false;   /* T-1408 fold 13 */
        return BuildFloatOk(m.progress) && BuildFloatOk(m.needed);
    }
    if (m.kind != kBuildPlace || m.key.empty() || m.sid.empty() || m.key.size() > kBuildMaxKey
        || m.sid.size() > kBuildMaxSid || m.hostKey.size() > kBuildMaxKey || m.complete > 1)
        return false;
    if (m.hostForm > kBuildHostIndoorsOnly || (m.hostKey.empty() && m.hostForm != 0) || m.outside > 1
        || m.floor < -kBuildMaxFloor || m.floor > kBuildMaxFloor)   /* build1-e */
        return false;
    if (m.ownerSlot != kBuildOwnerSender && m.ownerSlot > kBuildOwnerSlotMax) return false;
    if (m.groundMark > 1) return false;   /* T-1372 */
    if (m.townHand > 1 || (m.townHand == 1 && (m.ownerSlot == kBuildOwnerSender || !m.hostKey.empty()
                                               || !BuildFloatOk(m.townPos[0]) || !BuildFloatOk(m.townPos[1]))))
        return false;   /* T-923: a town hand-over names an owner and is an outdoor piece */
    for (int i = 0; i < 3; ++i) if (!BuildFloatOk(m.pos[i])) return false;
    if (!BuildRotOk(m.rot)) return false;
    return BuildFloatOk(m.progress) && BuildFloatOk(m.needed);
}

inline void BuildPutF(char** p, float v) { std::memcpy(*p, &v, 4); *p += 4; }
inline void BuildPutS(char** p, const std::string& s)
{
    **p = (char)(unsigned char)s.size(); *p += 1;
    if (!s.empty()) std::memcpy(*p, s.data(), s.size());
    *p += s.size();
}

/* false (nothing appended) when the message is not BuildEncodable. */
inline bool EncodeBuild(std::vector<char>* b, const BuildMsg& m)
{
    if (b == 0 || !BuildEncodable(m)) return false;
    if (m.kind == kBuildRosterRequest) { b->push_back((char)m.kind); return true; }   /* build1-f: the kind byte only */
    const size_t at = b->size();
    if (m.kind == kBuildHelpWork)   /* help1: kind | key | u32 seq | f32 dProgress | u8 reset | u8 n | n x f32 dMat; fold 3 (protocol 95): | u32 nonce */
    {
        b->resize(at + 1 + 1 + m.key.size() + 4 + 4 + 1 + 1 + 4 * (size_t)m.nMats + 4);
        char* w = &(*b)[at];
        *w++ = (char)m.kind;
        BuildPutS(&w, m.key);
        std::memcpy(w, &m.seq, 4); w += 4;
        BuildPutF(&w, m.progress);
        *w++ = (char)m.reset;
        *w++ = (char)m.nMats;
        for (unsigned int i = 0; i < m.nMats; ++i) BuildPutF(&w, m.mats[i]);
        std::memcpy(w, &m.nonce, 4);   /* help1 fold 3: the placement the work was done on */
        return true;
    }
    if (m.kind == kBuildHelpGone)   /* help1 fold (protocol 91): kind | key | u32 seq; fold 2 (protocol 92): | u32 applied; fold 3 (protocol 95): | u32 nonce | u8 n | n x f32 refused */
    {
        b->resize(at + 1 + 1 + m.key.size() + 4 + 4 + 4 + 1 + 4 * (size_t)m.nMats);
        char* g = &(*b)[at];
        *g++ = (char)m.kind;
        BuildPutS(&g, m.key);
        std::memcpy(g, &m.seq, 4); g += 4;
        std::memcpy(g, &m.applied, 4); g += 4;
        std::memcpy(g, &m.nonce, 4); g += 4;
        *g++ = (char)m.nMats;
        for (unsigned int i = 0; i < m.nMats; ++i) BuildPutF(&g, m.mats[i]);
        return true;
    }
    if (m.kind == kBuildHandAck)   /* house1b: kind + key */
    {
        b->resize(at + 1 + 1 + m.key.size());
        char* a = &(*b)[at];
        *a++ = (char)m.kind;
        BuildPutS(&a, m.key);
        return true;
    }
    if (m.kind == kBuildHit)   /* kind | key | u32 seq | f32 cut | f32 blunt | u8 verdict; the RESULT: f32 drop | f32 needed | u8 7 | u8 flags | f32 after */
    {
        const int res = (m.reason == kBuildHitResult) ? 1 : 0;
        b->resize(at + 1 + 1 + m.key.size() + 4 + 4 + 4 + 1 + (res != 0 ? 1 + 4 : 2));
        char* h = &(*b)[at];
        *h++ = (char)m.kind;
        BuildPutS(&h, m.key);
        std::memcpy(h, &m.seq, 4); h += 4;
        std::memcpy(h, res != 0 ? &m.hitDrop : &m.hitCut, 4); h += 4;
        std::memcpy(h, res != 0 ? &m.needed : &m.hitBlunt, 4); h += 4;
        *h++ = (char)m.reason;
        if (res != 0)
        {
            *h++ = (char)((m.destroyed != 0 ? kBuildHitFlagDestroyed : 0) | (m.hitOwed != 0 ? kBuildHitFlagOwed : 0));
            std::memcpy(h, &m.hitAfter, 4);
        }
        else { *h++ = (char)(signed char)m.hitDoor; *h++ = (char)m.hitRepair; }   /* the door and the repair word */
        return true;
    }
    if (m.kind == kBuildHelpHeld)   /* T-1408 fold 2: kind + key + why */
    {
        b->resize(at + 1 + 1 + m.key.size() + 1);
        char* q = &(*b)[at];
        *q++ = (char)m.kind;
        BuildPutS(&q, m.key);
        *q++ = (char)m.reason;
        return true;
    }
    if (m.kind == kBuildDismantleAsk)   /* T-805: kind + key + verdict */
    {
        b->resize(at + 1 + 1 + m.key.size() + 1);
        char* d = &(*b)[at];
        *d++ = (char)m.kind;
        BuildPutS(&d, m.key);
        *d++ = (char)m.reason;
        return true;
    }
    if (m.kind == kBuildRemove)   /* build1-d; P87 fold 1: | u32 nonce [| u8 flags [| hostKey]] - nothing past the reason when all are unset */
    {
        const int withFlags = (m.rmFlags != 0) ? 1 : 0;
        const int withNonce = (m.nonce != 0 || withFlags != 0) ? 1 : 0;
        const int withHost = ((m.rmFlags & kBuildRmFurniture) != 0) ? 1 : 0;
        b->resize(at + 1 + 1 + m.key.size() + 1 + (withNonce != 0 ? 4 : 0) + (withFlags != 0 ? 1 : 0) + (withHost != 0 ? 1 + m.hostKey.size() : 0));
        char* r = &(*b)[at];
        *r++ = (char)m.kind;
        BuildPutS(&r, m.key);
        *r++ = (char)m.reason;
        if (withNonce != 0) { std::memcpy(r, &m.nonce, 4); r += 4; }
        if (withFlags != 0) *r++ = (char)m.rmFlags;
        if (withHost != 0) BuildPutS(&r, m.hostKey);
        return true;
    }
    if (m.kind == kBuildState)
    {
        size_t tail = 0;   /* u8 rows (| kBuildHelpTailLive | kBuildHelpTailWide) | per row u16 slot, u32 seq, u32 live, u8 n, n x f32 refused
                              (cumulative) - only when rows exist */
        if (!m.helpAcks.empty())
        {
            tail = 1;
            for (size_t j = 0; j < m.helpAcks.size(); ++j) tail += 2 + 4 + 4 + 1 + 4 * (size_t)m.helpAcks[j].nEx;
        }
        b->resize(at + 1 + 1 + m.key.size() + 4 + 4 + 1 + 1 + 4 * (size_t)m.nMats + (m.groupStamp != 0 ? 4 : 0) + (m.groupLead.empty() ? 0 : 1 + m.groupLead.size() + 2) + tail);
        char* q = &(*b)[at];
        *q++ = (char)m.kind;
        BuildPutS(&q, m.key);
        BuildPutF(&q, m.progress);
        BuildPutF(&q, m.needed);
        *q++ = (char)(m.complete | (m.destroyed != 0 ? kBuildStateDestroyedBit : 0) | (m.broken != 0 ? kBuildStateBrokenBit : 0) | (m.groupStamp != 0 ? kBuildStateStampBit : 0) | (!m.groupLead.empty() ? kBuildStateGroupBit : 0));   /* bit 1 destroyed, bit 2 broken, bit 3 stamp, bit 4 group fact */
        *q++ = (char)m.nMats;
        for (unsigned int i = 0; i < m.nMats; ++i) BuildPutF(&q, m.mats[i]);
        if (m.groupStamp != 0) { std::memcpy(q, &m.groupStamp, 4); q += 4; }   /* the wall group's step stamp */
        if (!m.groupLead.empty())   /* T-1408 fold 13: the owner's group fact - the lead's key and the join index */
        {
            BuildPutS(&q, m.groupLead);
            *q++ = (char)(m.groupIdx & 0xFF);
            *q++ = (char)((m.groupIdx >> 8) & 0xFF);
        }
        if (tail != 0)
        {
            *q++ = (char)(m.helpAcks.size() | kBuildHelpTailLive | kBuildHelpTailWide);   /* the rows carry live and a two-byte slot */
            for (size_t j = 0; j < m.helpAcks.size(); ++j)
            {
                const BuildHelpAck& a = m.helpAcks[j];
                const unsigned int live = (a.live > a.seq) ? a.live : a.seq;   /* help1 fold 3: never below the landed seq */
                *q++ = (char)(a.slot & 0xFF);
                *q++ = (char)((a.slot >> 8) & 0xFF);
                std::memcpy(q, &a.seq, 4); q += 4;
                std::memcpy(q, &live, 4); q += 4;
                *q++ = (char)a.nEx;
                for (unsigned int k = 0; k < a.nEx; ++k) BuildPutF(&q, a.ex[k]);
            }
        }
        return true;
    }
    b->resize(at + 1 + 1 + m.key.size() + 1 + m.sid.size() + 12 + 16 + 1 + 4 + 4 + 1 + m.hostKey.size() + 1 + 4 + 1 + 2 + (m.nonce != 0 ? 4 : 0) + (m.townHand != 0 ? kBuildTownTail : 0) + (m.groundMark != 0 ? 1 : 0));   /* + the two owner-number bytes; + the nonce; + T-923's town tail; + T-1372's ground mark */
    char* p = &(*b)[at];
    *p++ = (char)m.kind;
    BuildPutS(&p, m.key);
    BuildPutS(&p, m.sid);
    for (int i = 0; i < 3; ++i) BuildPutF(&p, m.pos[i]);
    for (int i = 0; i < 4; ++i) BuildPutF(&p, m.rot[i]);
    *p++ = (char)m.complete;
    BuildPutF(&p, m.progress);
    BuildPutF(&p, m.needed);
    BuildPutS(&p, m.hostKey);
    *p++ = (char)m.hostForm;   /* build1-e */
    std::memcpy(p, &m.floor, 4); p += 4;
    *p++ = (char)m.outside;
    *p++ = (char)(m.ownerSlot & 0xFF); *p++ = (char)((m.ownerSlot >> 8) & 0xFF);   /* the owner number, little-endian */
    if (m.nonce != 0) { std::memcpy(p, &m.nonce, 4); p += 4; }   /* house2 fold (protocol 76) */
    if (m.townHand != 0) { *p++ = (char)kBuildTownTag; BuildPutF(&p, m.townPos[0]); BuildPutF(&p, m.townPos[1]); }   /* T-923 (protocol 167) */
    if (m.groundMark != 0) *p++ = (char)kBuildGroundTag;   /* T-1372 decision 1691 (protocol 229): the last byte */
    return true;
}

/* Every length test is `size - off < n` with off already <= size. */
inline int BuildGetS(const char* p, size_t size, size_t* off, unsigned int cap, std::string* out)
{
    if (size - *off < 1) return kBuildDecodeTooShort;
    const unsigned int n = (unsigned char)p[*off];
    *off += 1;
    if (n > cap) return kBuildDecodeBadKey;
    if (size - *off < (size_t)n) return kBuildDecodeTooShort;
    out->assign(p + *off, n);
    *off += n;
    return kBuildDecodeOk;
}
/* P87 fold 1: a REMOVE's optional tail (nonce, flags, a furniture tombstone's host key). Lenient, as the REMOVE decode always was about
   trailing bytes: a part that is absent or does not parse is left unset, never a refusal. */
inline void BuildRemoveTail(const char* p, size_t size, size_t off, BuildMsg* m)
{
    if (size - off < 4) return;
    unsigned int n = 0;
    std::memcpy(&n, p + off, 4); off += 4;
    m->nonce = n;
    if (size - off < 1) return;
    m->rmFlags = (unsigned char)p[off]; off += 1;
    if ((m->rmFlags & kBuildRmFurniture) == 0) return;
    std::string hk;
    if (BuildGetS(p, size, &off, kBuildMaxKey, &hk) == kBuildDecodeOk) m->hostKey = hk;
}

/* ownerBytes: how many bytes a PLACE's owner number takes - 2 (EncodeBuild, the wire), or 1 (DecodeBuildSaved's older rows) */
inline int DecodeBuildLayout(const char* p, size_t size, BuildMsg* out, int ownerBytes)
{
    if (p == 0 || size < 1) return kBuildDecodeTooShort;
    BuildMsg m;
    m.kind = (unsigned char)p[0];
    if (m.kind != kBuildPlace && m.kind != kBuildState && m.kind != kBuildRemove && m.kind != kBuildRosterRequest && m.kind != kBuildHandAck
        && m.kind != kBuildHelpWork && m.kind != kBuildHelpGone && m.kind != kBuildDismantleAsk && m.kind != kBuildHit && m.kind != kBuildHelpHeld) return kBuildDecodeBadKind;
    if (m.kind == kBuildRosterRequest)   /* build1-f: exactly the kind byte */
    {
        if (size != 1) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    size_t off = 1;
    int r = BuildGetS(p, size, &off, kBuildMaxKey, &m.key);
    if (r != kBuildDecodeOk) return r;
    if (m.kind == kBuildHelpWork)   /* help1: exactly kind | key | seq | dProgress | reset | n | n x dMat; fold 3 (protocol 95): | u32 nonce */
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (size - off < (size_t)(4 + 4 + 1 + 1)) return kBuildDecodeTooShort;
        std::memcpy(&m.seq, p + off, 4); off += 4;
        std::memcpy(&m.progress, p + off, 4); off += 4;
        m.reset = (unsigned char)p[off]; off += 1;
        const unsigned int n = (unsigned char)p[off]; off += 1;
        if (n > kBuildMaxMats) return kBuildDecodeBadValue;
        if (size - off != (size_t)(4 * n + 4)) return (size - off < (size_t)(4 * n + 4)) ? kBuildDecodeTooShort : kBuildDecodeBadValue;   /* help1 fold 3: + the nonce */
        m.nMats = (unsigned char)n;
        for (unsigned int i = 0; i < n; ++i) { std::memcpy(&m.mats[i], p + off, 4); off += 4; }
        std::memcpy(&m.nonce, p + off, 4); off += 4;   /* help1 fold 3: the placement the work was done on */
        if (m.seq == 0 || m.reset > 1 || !BuildHelpAmountOk(m.progress)) return kBuildDecodeBadValue;
        for (unsigned int i = 0; i < n; ++i) if (!BuildHelpAmountOk(m.mats[i])) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    if (m.kind == kBuildHelpGone)   /* help1 fold (protocol 91): kind | key | u32 seq (> 0); fold 2 (protocol 92): | u32 applied (any);
                                       fold 3 (protocol 95): | u32 nonce | u8 n | exactly n x f32 refused (finite, >= 0) */
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (size - off < 13) return kBuildDecodeTooShort;
        std::memcpy(&m.seq, p + off, 4); off += 4;
        std::memcpy(&m.applied, p + off, 4); off += 4;
        std::memcpy(&m.nonce, p + off, 4); off += 4;
        const unsigned int n = (unsigned char)p[off]; off += 1;
        if (n > kBuildMaxMats) return kBuildDecodeBadValue;
        if (size - off != (size_t)(4 * n)) return (size - off < (size_t)(4 * n)) ? kBuildDecodeTooShort : kBuildDecodeBadValue;
        m.nMats = (unsigned char)n;
        for (unsigned int i = 0; i < n; ++i) { std::memcpy(&m.mats[i], p + off, 4); off += 4; if (!BuildFloatOk(m.mats[i]) || m.mats[i] < 0.0f) return kBuildDecodeBadValue; }
        if (m.seq == 0) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    if (m.kind == kBuildHandAck)   /* house1b: exactly kind + key */
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (off != size) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    if (m.kind == kBuildHit)   /* exactly kind | key | u32 seq | f32 cut | f32 blunt | u8 verdict; the RESULT: f32 drop | f32 needed | u8 7 | u8 flags | f32 after */
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (size - off < 13) return kBuildDecodeTooShort;
        float f1 = 0.0f, f2 = 0.0f;
        std::memcpy(&m.seq, p + off, 4); off += 4;
        std::memcpy(&f1, p + off, 4); off += 4;
        std::memcpy(&f2, p + off, 4); off += 4;
        m.reason = (unsigned char)p[off]; off += 1;
        if (m.reason == kBuildHitResult)
        {
            if (size - off < 5) return kBuildDecodeTooShort;
            const unsigned char fl = (unsigned char)p[off]; off += 1;
            if ((fl & ~(kBuildHitFlagDestroyed | kBuildHitFlagOwed)) != 0) return kBuildDecodeBadValue;
            m.destroyed = (unsigned char)((fl & kBuildHitFlagDestroyed) != 0 ? 1 : 0);
            m.hitOwed = (unsigned char)((fl & kBuildHitFlagOwed) != 0 ? 1 : 0);
            std::memcpy(&m.hitAfter, p + off, 4); off += 4;
            m.hitDrop = f1; m.needed = f2;
        }
        else
        {   /* an ask or an answer: the door and the repair word follow the verdict */
            if (size - off < 2) return kBuildDecodeTooShort;
            m.hitDoor = (int)(signed char)p[off]; m.hitRepair = (unsigned char)p[off + 1]; off += 2;
            m.hitCut = f1; m.hitBlunt = f2;
        }
        if (off != size || !BuildEncodable(m)) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    if (m.kind == kBuildHelpHeld)   /* T-1408 fold 2: exactly kind + key + why (0..2) */
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (size - off < 1) return kBuildDecodeTooShort;
        m.reason = (unsigned char)p[off]; off += 1;
        if (off != size || m.reason > kBuildHeldWriter) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    if (m.kind == kBuildDismantleAsk)   /* T-805: exactly kind + key + verdict (0..7) */
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (size - off < 1) return kBuildDecodeTooShort;
        m.reason = (unsigned char)p[off]; off += 1;
        if (off != size || m.reason > 7) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    if (m.kind == kBuildRemove)   /* build1-d */
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (size - off < 1) return kBuildDecodeTooShort;
        m.reason = (unsigned char)p[off]; off += 1;
        if (m.reason != kBuildReasonDismantled && m.reason != kBuildReasonDestroyed) return kBuildDecodeBadValue;
        BuildRemoveTail(p, size, off, &m);   /* P87 fold 1: the optional tail - anything else after the reason is ignored, as before */
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    if (m.kind == kBuildState)
    {
        if (m.key.empty()) return kBuildDecodeBadKey;
        if (size - off < (size_t)(4 + 4 + 1 + 1)) return kBuildDecodeTooShort;
        std::memcpy(&m.progress, p + off, 4); off += 4;
        std::memcpy(&m.needed, p + off, 4); off += 4;
        const unsigned int cb = (unsigned char)p[off]; off += 1;   /* bit 0 complete, bit 1 destroyed, bit 2 broken; any other bit is refused */
        if ((cb & ~(1u | (unsigned int)kBuildStateDestroyedBit | (unsigned int)kBuildStateBrokenBit | (unsigned int)kBuildStateStampBit | (unsigned int)kBuildStateGroupBit)) != 0) return kBuildDecodeBadValue;
        m.complete = (unsigned char)(cb & 1u);
        m.destroyed = (unsigned char)((cb & kBuildStateDestroyedBit) != 0 ? 1 : 0);
        m.broken = (unsigned char)((cb & kBuildStateBrokenBit) != 0 ? 1 : 0);
        const unsigned int n = (unsigned char)p[off]; off += 1;
        if (n > kBuildMaxMats) return kBuildDecodeBadValue;
        if (size - off < (size_t)(4 * n)) return kBuildDecodeTooShort;
        m.nMats = (unsigned char)n;
        for (unsigned int i = 0; i < n; ++i) { std::memcpy(&m.mats[i], p + off, 4); off += 4; }
        if ((cb & kBuildStateStampBit) != 0)   /* the wall group's step stamp: written only when not 0 */
        {
            if (size - off < 4) return kBuildDecodeTooShort;
            std::memcpy(&m.groupStamp, p + off, 4); off += 4;
            if (m.groupStamp == 0) return kBuildDecodeBadValue;
        }
        if ((cb & kBuildStateGroupBit) != 0)   /* T-1408 fold 13: the owner's group fact - a lead key (never empty) and a join index */
        {
            const int gr = BuildGetS(p, size, &off, kBuildMaxKey, &m.groupLead);
            if (gr != kBuildDecodeOk) return gr;
            if (m.groupLead.empty()) return kBuildDecodeBadValue;
            if (size - off < 2) return kBuildDecodeTooShort;
            m.groupIdx = (unsigned short)((unsigned char)p[off] | ((unsigned int)(unsigned char)p[off + 1] << 8)); off += 2;
            if (m.groupIdx > kBuildGroupIdxMax) return kBuildDecodeBadValue;
        }
        if (size - off > 0)   /* help1 (protocol 90): the confirmation tail - written only when it has a row, then nothing follows it */
        {
            const unsigned int h0 = (unsigned char)p[off]; off += 1;
            const int withLive = (h0 & kBuildHelpTailLive) != 0 ? 1 : 0;   /* help1 fold 3 (protocol 95): each row u32 live after seq; without the flag (a pp.build record written before) live = seq */
            const int wide = (h0 & kBuildHelpTailWide) != 0 ? 1 : 0;   /* a two-byte helper slot; without the flag (a pp.build record written before) one byte */
            const unsigned int h = h0 & 0x3Fu;
            if (h == 0 || h > kBuildMaxHelpers) return kBuildDecodeBadValue;
            for (unsigned int j = 0; j < h; ++j)
            {
                if (size - off < (size_t)((wide != 0 ? 2 : 1) + 4 + (withLive != 0 ? 4 : 0) + 1)) return kBuildDecodeTooShort;
                BuildHelpAck a;
                if (wide != 0) { a.slot = (unsigned short)((unsigned char)p[off] | ((unsigned int)(unsigned char)p[off + 1] << 8)); off += 2; }
                else { a.slot = (unsigned char)p[off]; off += 1; }
                std::memcpy(&a.seq, p + off, 4); off += 4;
                if (withLive != 0) { std::memcpy(&a.live, p + off, 4); off += 4; if (a.live < a.seq) return kBuildDecodeBadValue; }   /* help1 fold 3 */
                else a.live = a.seq;
                const unsigned int ne = (unsigned char)p[off]; off += 1;
                if (ne > kBuildMaxMats) return kBuildDecodeBadValue;
                if (size - off < (size_t)(4 * ne)) return kBuildDecodeTooShort;
                a.nEx = (unsigned char)ne;
                for (unsigned int k = 0; k < ne; ++k) { std::memcpy(&a.ex[k], p + off, 4); off += 4; }
                m.helpAcks.push_back(a);
            }
            if (off != size || !BuildHelpAcksOk(m.helpAcks)) return kBuildDecodeBadValue;
        }
        if (m.complete > 1) return kBuildDecodeBadValue;
        if (!BuildFloatOk(m.progress) || !BuildFloatOk(m.needed)) return kBuildDecodeBadValue;
        for (unsigned int i = 0; i < n; ++i) if (!BuildFloatOk(m.mats[i])) return kBuildDecodeBadValue;
        if (out) *out = m;
        return kBuildDecodeOk;
    }
    r = BuildGetS(p, size, &off, kBuildMaxSid, &m.sid);
    if (r != kBuildDecodeOk) return r;
    if (m.key.empty() || m.sid.empty()) return kBuildDecodeBadKey;
    if (size - off < (size_t)(12 + 16 + 1 + 4 + 4)) return kBuildDecodeTooShort;
    for (int i = 0; i < 3; ++i) { std::memcpy(&m.pos[i], p + off, 4); off += 4; }
    for (int i = 0; i < 4; ++i) { std::memcpy(&m.rot[i], p + off, 4); off += 4; }
    m.complete = (unsigned char)p[off]; off += 1;
    std::memcpy(&m.progress, p + off, 4); off += 4;
    std::memcpy(&m.needed, p + off, 4); off += 4;
    r = BuildGetS(p, size, &off, kBuildMaxKey, &m.hostKey);
    if (r != kBuildDecodeOk) return r;
    if (size - off < (size_t)(1 + 4 + 1 + 1)) return kBuildDecodeTooShort;   /* host form, floor, outside, the owner number */
    m.hostForm = (unsigned char)p[off]; off += 1;
    std::memcpy(&m.floor, p + off, 4); off += 4;
    m.outside = (unsigned char)p[off]; off += 1;
    if (ownerBytes == 2)
    {
        if (size - off < 2) return kBuildDecodeTooShort;
        m.ownerSlot = (unsigned short)((unsigned char)p[off] | ((unsigned short)(unsigned char)p[off + 1] << 8)); off += 2;   /* little-endian */
        if (m.ownerSlot != kBuildOwnerSender && m.ownerSlot > kBuildOwnerSlotMax) return kBuildDecodeBadValue;
    }
    else
    {
        const unsigned char o = (unsigned char)p[off]; off += 1;   /* the one-byte owner number: 0xFF the sender, else the slot */
        m.ownerSlot = (o == kBuildOwnerSenderOneByte) ? kBuildOwnerSender : (unsigned short)o;
    }
    /* the tail, told apart by its length: none (0), the per-placement nonce (4; absent = 0), T-923's town tail (kBuildTownTail, protocol 167),
       or both, the nonce first; then (T-1372, protocol 229) the one ground-mark byte when present - the lengths with it (1, 5, 10, 14) are
       none of the lengths without it, so the byte is told apart by the length too */
    {
        const size_t t0 = size - off;
        if (t0 == 1 || t0 == 5 || t0 == kBuildTownTail + 1 || t0 == 4 + kBuildTownTail + 1)
        {
            if ((unsigned char)p[size - 1] != kBuildGroundTag) return kBuildDecodeBadValue;
            m.groundMark = 1;
            size -= 1;   /* the rest of the tail ends before the mark */
        }
    }
    const size_t tail = size - off;
    if (tail != 0 && tail != 4 && tail != kBuildTownTail && tail != 4 + kBuildTownTail)
        return tail < 4 ? kBuildDecodeTooShort : kBuildDecodeBadValue;
    if (tail == 4 || tail == 4 + kBuildTownTail)
    {
        std::memcpy(&m.nonce, p + off, 4); off += 4;
        if (m.nonce == 0) return kBuildDecodeBadValue;   /* one encoding: a zero nonce is never written */
    }
    if (size - off == kBuildTownTail)
    {
        if ((unsigned char)p[off] != kBuildTownTag) return kBuildDecodeBadValue;
        off += 1;
        m.townHand = 1;
        std::memcpy(&m.townPos[0], p + off, 4); off += 4;
        std::memcpy(&m.townPos[1], p + off, 4); off += 4;
        if (!BuildFloatOk(m.townPos[0]) || !BuildFloatOk(m.townPos[1]) || m.ownerSlot == kBuildOwnerSender || !m.hostKey.empty()) return kBuildDecodeBadValue;
    }
    if (m.hostForm > kBuildHostIndoorsOnly || (m.hostKey.empty() && m.hostForm != 0) || m.outside > 1
        || m.floor < -kBuildMaxFloor || m.floor > kBuildMaxFloor)
        return kBuildDecodeBadValue;
    if (m.complete > 1) return kBuildDecodeBadValue;
    for (int i = 0; i < 3; ++i) if (!BuildFloatOk(m.pos[i])) return kBuildDecodeBadValue;
    if (!BuildRotOk(m.rot)) return kBuildDecodeBadValue;
    if (!BuildFloatOk(m.progress) || !BuildFloatOk(m.needed)) return kBuildDecodeBadValue;
    if (out) *out = m;
    return kBuildDecodeOk;
}
/* a message as it arrives on the wire (and as EncodeBuild writes it): a PLACE's owner number is two bytes */
inline int DecodeBuild(const char* p, size_t size, BuildMsg* out) { return DecodeBuildLayout(p, size, out, 2); }
/* a message as this game saved it (pp.build rows, the owed file): EncodeBuild's layout, else - for a PLACE - the layout with a
   one-byte owner number (0xFF = the sender) that rows saved by earlier builds carry */
inline int DecodeBuildSaved(const char* p, size_t size, BuildMsg* out)
{
    const int r = DecodeBuildLayout(p, size, out, 2);
    if (r == kBuildDecodeOk || p == 0 || size < 1 || (unsigned char)p[0] != kBuildPlace) return r;
    return DecodeBuildLayout(p, size, out, 1) == kBuildDecodeOk ? kBuildDecodeOk : r;
}

/* T-1372 decision 1690 (TEST-ONLY lever `bldparts`): IS A BUILDING INTACT? What the lever read off the building (F1038, Read 1.0.65:
   scene node +0x200, shown +0x19D, model holder +0x228 parts / entities, doors +0x1C0, interior +0x1F0 and its mask model +0x08,
   material +0x248, the input height +0x278) against what its record says it is made of (the 'parts' list walked deep as 0x55FD60
   walks it plus the 'interior' entries, each counted only when its 'phs or mesh' name holds ".mesh" - 0x55EC50's own test; the 'doors'
   list). -1 = not read. F1046 fold (2026-11-16): parts = the holder's first two lists (+0x08 and +0x20 counts); innerWant = the parts
   the 'interior' entries make - 0x55FF50 makes them with flag 2, which never makes a model (55ff50:395-397, 55ec50:428), so a model
   is expected only on partsWant - innerWant; mask is reported, never judged (0x561280 makes it only while the interior is shown). */
struct BuildPartsRead
{
    int node, shown, complete, destroyed;
    int parts, partsWant, entities, visible, doors, doorsWant;
    int interior, interiorWant, mask, material, lights;
    int innerWant;             /* F1046 fold: parts of the 'interior' entries (no model by design); -1 unread */
    int heightRead;            /* 1 = offset and above were read (a free-standing piece) */
    float offset, above;       /* +0x278, and live y less the engine's ground (0x9B2850) */
    BuildPartsRead() : node(-1), shown(-1), complete(-1), destroyed(-1), parts(-1), partsWant(-1), entities(-1), visible(-1), doors(-1), doorsWant(-1),
                       interior(-1), interiorWant(-1), mask(-1), material(-1), lights(-1), innerWant(-1), heightRead(0), offset(0.0f), above(0.0f) {}
};
const float kBuildPartsHeightTol = 0.05f;
/* "INTACT", or "MISSING " + every missing thing (space-separated, in this order): node, shown, parts, entities, visible, doors, interior,
   material, height, unread. A ruin's or an unfinished piece's state is reported, never missing. entities: at least one model per part
   the record makes outside the interior (partsWant - innerWant). */
inline std::string BuildPartsVerdict(const BuildPartsRead& r)
{
    std::string w;
    if (r.node != 1) w += " node";
    if (r.shown != 1) w += " shown";
    if (r.parts < 0 || r.partsWant < 0 || r.parts < r.partsWant) w += " parts";
    if (r.entities < 0 || r.partsWant < 0 || r.innerWant < 0 || r.innerWant > r.partsWant || r.entities < r.partsWant - r.innerWant) w += " entities";
    if (r.shown == 1 && (r.visible < 0 || r.entities < 0 || r.visible < r.entities)) w += " visible";
    if (r.doors < 0 || r.doorsWant < 0 || r.doors < r.doorsWant) w += " doors";
    if (r.interiorWant != 0 && r.interior != 1) w += " interior";
    if (r.material != 1) w += " material";
    if (r.heightRead == 1)
    {
        const float d = r.above - r.offset;
        if (!(d <= kBuildPartsHeightTol && d >= -kBuildPartsHeightTol)) w += " height";
    }
    if (r.complete < 0 || r.destroyed < 0) w += " unread";
    return w.empty() ? std::string("INTACT") : std::string("MISSING") + w;
}
/* T-1372 (F1057; Read 1.0.68 and 1.0.65): where a part's model pointer sits in each of the model holder's two part lists (+0x10 and
   +0x28 items). A first-list part keeps it at +0x08. The second list holds the parts with a collision shape (filed at 1.0.68 0x1405604BA):
   the 0x70-byte one is made by 1.0.68 0x1404CDE30 (1.0.65 0x1404CD3A0) - +0x08 its physics setup, +0x20 the model; the rarer 0xB0-byte
   one (1.0.68 0x1404CE0C0, 1.0.65 0x1404CD630, through 0x14004FCB4) runs that same maker first with the same model argument, so its
   model is at +0x20 as well. The same offsets on both game versions. -1 = no such list. */
inline int BuildPartModelOffset(int list) { return list == 0 ? 0x08 : (list == 1 ? 0x20 : -1); }

/* house2: the order gate (0x7F9280) and the Shift-job gate (0x7F4EF0) share one rule - on a piece this game handed to another
   player's house owner a build, add-materials, repair or dismantle task is refused; every other task (use, per base access) passes */
const int kBuildTaskBuild = 2, kBuildTaskAddMaterials = 71, kBuildTaskRepair = 95, kBuildTaskDismantle = 96;   /* TaskType values */
inline bool BuildTaskGated(int task)
{
    return task == kBuildTaskBuild || task == kBuildTaskAddMaterials || task == kBuildTaskRepair || task == kBuildTaskDismantle;
}
inline bool BuildTaskRefused(int task, int handedAway) { return BuildTaskGated(task) && handedAway != 0; }
/* T-805: a dismantle order on a piece another player owns. A player of another faction gets none (decision 548); a team member's
   (owner 797 a) is not given to the engine here - it is ASKED of the owner's game, which dismantles its own piece; with the owner's
   game out of reach it is refused at the click. Every other order goes on. */
const int kBuildDmOrderGoes = 0, kBuildDmOrderRefusedOther = 1, kBuildDmOrderRefusedMember = 2, kBuildDmOrderOwnerAway = 3, kBuildDmOrderAsk = 4;
inline int BuildDismantleOrderVerdict(int task, int otherPlayersPiece, int member, int memberMayDismantle, int ownerReachable)
{
    if (task != kBuildTaskDismantle || otherPlayersPiece == 0) return kBuildDmOrderGoes;
    if (member == 0) return kBuildDmOrderRefusedOther;
    if (memberMayDismantle == 0) return kBuildDmOrderRefusedMember;
    return ownerReachable != 0 ? kBuildDmOrderAsk : kBuildDmOrderOwnerAway;
}
/* T-805: the owner's game on a DISMANTLE_ASK - the asker must share this player's team NOW, the piece must be loaded here, be this
   game's own and not dismantled; a wall (or a piece whose class cannot be read) is refused (a wall's dismantle defers, 0x548860); a
   piece the engine's dismantle job itself refuses (furniture in it, a research bench - `blocked`) is refused. kBuildDmDone = the
   engine's dismantle runs. */
const unsigned char kBuildDmAsk = 0, kBuildDmDone = 1, kBuildDmNotMember = 2, kBuildDmGone = 3, kBuildDmNotOwn = 4, kBuildDmWall = 5, kBuildDmFaulted = 6,
                    kBuildDmBlocked = 7;
inline int BuildDismantleAskVerdict(int senderMember, int found, int ownHere, int dismantled, int wall, int blocked)
{
    if (senderMember == 0) return kBuildDmNotMember;
    if (found == 0 || dismantled != 0) return kBuildDmGone;
    if (ownHere == 0) return kBuildDmNotOwn;
    if (wall != 0) return kBuildDmWall;
    if (blocked != 0) return kBuildDmBlocked;
    return kBuildDmDone;
}
inline const char* BuildDmWord(int v)
{
    switch (v)
    {
    case 0: return "ask";
    case 1: return "done - the engine's confirmDismantle ran on the owner's piece (its mounted buildings with it)";
    case 2: return "refused - the asker is not in the owner's team now";
    case 3: return "refused - the piece is not loaded on the owner's game or is already dismantled";
    case 4: return "refused - the piece is not the owner's game's own";
    case 5: return "refused - a wall, or a piece whose class cannot be read (a wall's dismantle defers; not asked across games)";
    case 6: return "refused - the engine's dismantle faulted on the owner's game";
    case 7: return "refused - has furniture or attached pieces (or is a research bench) - dismantle them first";
    default: return "?";
    }
}

/* P15: what a copy does with the owner's destroyed state (a STATE's destroyed bit) against its own +0x1A1. A STATE made from a PLACE
   carries no destroyed state and changes nothing. */
const int kBuildDestroyKeep = 0;      /* nothing to change */
const int kBuildDestroyApply = 1;     /* the owner's piece is a ruin, the copy is whole: the engine's setDestroyed(1) on the copy */
const int kBuildDestroyRepair = 2;    /* the owner's piece is whole again, the copy is a ruin: the engine's repair road on the copy */
const int kBuildDestroyAlready = 3;   /* both ruins: materials only */
inline int BuildDestroyedAction(int fromPlace, int msgDestroyed, int liveDestroyed)
{
    if (fromPlace != 0) return kBuildDestroyKeep;
    if (msgDestroyed != 0) return liveDestroyed != 0 ? kBuildDestroyAlready : kBuildDestroyApply;
    return liveDestroyed != 0 ? kBuildDestroyRepair : kBuildDestroyKeep;
}
/* P15: a repair order (TaskType 95) on a RUIN copy (a stand-in owns it) is help - the work reaches the owner's game as HELP_WORK with
   the ruin reset; on a wall ruin copy it is refused (the owner's ruin reset is not carried for walls). Any other order is not this
   rule's. */
const int kBuildRepairOrderNone = 0, kBuildRepairOrderHelp = 1, kBuildRepairOrderRefused = 2;
inline int BuildRepairOrderVerdict(int task, int standInOwned, int destroyed, int wall)
{
    if (task != kBuildTaskRepair || standInOwned == 0 || destroyed == 0) return kBuildRepairOrderNone;
    return wall != 0 ? kBuildRepairOrderRefused : kBuildRepairOrderHelp;
}
/* T-221: what a WALL copy does with the owner's STATE. ownState: 1 = the copy keeps its own progress record and leads no group (a wall
   made at runtime; BuildWallKeepsOwnRecord), 0 = the record is a group's (shared with it, or held by it as the group's lead). complete = the STATE's complete bit; p / n = the owner's progress and needed (a grouped owner wall
   sends its group's record); liveN = the copy's own needed. A complete STATE completes either kind (the engine's complete-now path).
   Otherwise an own-record copy takes the owner's share of its own needed, kept below 0.999 x liveN (the redraw completes a piece at
   progress >= needed); a shared-record copy, or a needed that is not a usable number, takes materials only. *progress = the value to
   write for kBuildWallProgress and kBuildWallCompleteDamaged.
   A complete STATE whose progress is below its needed is a hit wall (a hit lowers a complete wall's progress, its health): an
   own-record copy is complete and takes the same share of its own needed (kBuildWallCompleteDamaged - a progress write, as the engine's
   hit makes; no 0.999 cap, the piece stays complete). A shared-record copy is completed only: a progress write there would land in
   every member's record, so it keeps materials only, as for an unfinished STATE. A complete STATE at its needed on an own-record copy
   returns kBuildWallComplete with *progress = liveN: a copy a hit had lowered is put back to full health. */
const int kBuildWallMatsOnly = 0, kBuildWallProgress = 1, kBuildWallComplete = 2, kBuildWallCompleteDamaged = 3;
inline int BuildWallCopyAction(int ownState, int complete, float p, float n, float liveN, float* progress)
{
    *progress = 0.0f;
    if (complete != 0)
    {
        if (ownState == 0 || !(n > 0.0f && n < 3.0e38f) || !(liveN > 0.0f && liveN < 3.0e38f)) return kBuildWallComplete;   /* NaN fails too */
        if (!(p < n)) { *progress = liveN; return kBuildWallComplete; }   /* whole again: full health, for a copy a hit had lowered */
        const float hs = p / n;
        *progress = (hs > 0.0f) ? hs * liveN : 0.0f;
        return kBuildWallCompleteDamaged;
    }
    if (ownState == 0) return kBuildWallMatsOnly;
    if (!(n > 0.0f && n < 3.0e38f) || !(liveN > 0.0f && liveN < 3.0e38f)) return kBuildWallMatsOnly;   /* NaN fails too */
    const float share = p / n;
    float v = (share > 0.0f) ? share * liveN : 0.0f;   /* a negative or NaN progress writes 0 */
    const float cap = liveN * 0.999f;
    if (!(v <= cap)) v = cap;
    *progress = v;
    return kBuildWallProgress;
}
/* T-1408 (owner ruling: the shared record is the truth, the save is not the primary source; one source per fact). A wall group's progress
   is ONE fact with ONE clock, a Lamport stamp: every write of it - the owner's wall step, applied help, finish and dismantle step
   (BdWallGroupsMark), the helper's writes on any copy (BdWriterGroupNote, own-record copies included), a holder's write (PcrWriteOne) and
   the owner's apply of a newer record (BdOwnGroupLanded) - carries a GROUP STEP STAMP, the highest stamp this game knows + 1
   (BuildGroupStampNext), and puts the same value and stamp into every record of the group it can write in that step. A STATE carries
   it after its materials (kBuildStateStampBit); 0 = an older build's record. Piece generations count per piece, so only the stamp tells
   a newer group value from an older one.
   BuildWallRecShare: a record's share of its needed (a finished record 1); 0 = not usable (needed not a number above 0). */
inline int BuildWallRecShare(float p, float n, int complete, float* share)
{
    *share = 0.0f;
    if (!(n > 0.0f && n < 3.0e38f)) return 0;
    if (complete != 0) { *share = 1.0f; return 1; }
    const float s = p / n;
    *share = (s > 0.0f) ? (s < 1.0f ? s : 1.0f) : 0.0f;   /* a negative or NaN progress reads 0 */
    return 1;
}
inline unsigned int BuildGroupStampNext(unsigned int known) { return known < 0xFFFFFFFFu ? known + 1u : known; }
/* T-1408 fold 7 (F1283), a Lamport clock: only NEW LOCAL WORK steps a group's stamp (BuildGroupStepWhat); applying a received or loaded
   record ADOPTS its stamp (the clock's max) and never steps. Two values of one group are ordered by stamp, then - an equal stamp from two
   games' concurrent steps (or a helper's own-record copies sharing one step, BuildWriterSameOwnerStep) - by the value itself: the higher
   share wins (the tie key travels inside every record with its stamp, so every game ranks the same pair the same way and an adopted value
   keeps its key; Cassandra's last-write-wins breaks an equal timestamp by the greater value the same way). Shares within
   kBuildGroupShareSame are one value; an unusable share (negative or NaN) ranks below a usable one. STAMP 0 (records of builds before
   fold 6): below every stamped value; among themselves (the live value unstamped too) the older builds' rule decides - the highest share
   among the unstamped records (BuildWallLegacyShare), kBuildRankLegacy. */
const float kBuildGroupShareSame = 0.0005f;
const int kBuildRankOlder = -1, kBuildRankSame = 0, kBuildRankNewer = 1, kBuildRankLegacy = 2;
inline int BuildGroupRank(unsigned int rec, float recShare, unsigned int live, float liveShare)
{
    if (rec == 0 && live == 0) return kBuildRankLegacy;
    if (rec != live) return rec > live ? kBuildRankNewer : kBuildRankOlder;
    const int ru = (recShare >= 0.0f) ? 1 : 0, lu = (liveShare >= 0.0f) ? 1 : 0;   /* NaN fails */
    if (ru == 0 || lu == 0) return ru > lu ? kBuildRankNewer : (ru < lu ? kBuildRankOlder : kBuildRankSame);
    const float d = recShare - liveShare;
    if (d > kBuildGroupShareSame) return kBuildRankNewer;
    if (d < -kBuildGroupShareSame) return kBuildRankOlder;
    return kBuildRankSame;
}
/* a record still queued here can still win over the group's live value (its stamp not below the live one; an unstamped record only while
   the live value is unstamped too): the group's own rows and records are held, and the owner's own steps owed, until it is applied or
   dropped (PieceOwnRowHold, BuildGroupStepWhat) */
inline int BuildGroupStampMayWin(unsigned int rec, unsigned int live) { return rec >= live ? 1 : 0; }
/* T-1408 fold 8 (F1291): the owner's load decides a wall GROUP once, from all its records at hand - one queue turn for the group whatever
   its size (BdOwnGroupTurn): every queued record of the group is taken out together and applied in this order, the WINNER first (the
   highest rank, BuildGroupRank: the stamp, then the share as the tie key), the rest after it, so each later one ranks older or the same
   against the value the winner left (caught up, never written back). Equal ranks keep their queue (key) order; records of the older
   builds (stamp 0) among themselves keep it too (their value is the highest unstamped share whichever applies first). order: n indices.
   Returns the winner's index (-1 = none) */
inline int BuildGroupApplyOrder(const unsigned int* stamps, const float* shares, int n, int* order)
{
    int m = 0;
    for (int i = 0; i < n; ++i)
    {
        int at = m;
        for (int j = 0; j < m; ++j)
            if (BuildGroupRank(stamps[i], shares[i], stamps[order[j]], shares[order[j]]) == kBuildRankNewer) { at = j; break; }
        for (int j = m; j > at; --j) order[j] = order[j - 1];
        order[at] = i;
        ++m;
    }
    return n > 0 ? order[0] : -1;
}
/* T-1408 fold 9 (F1296): a wall group's member areas taken together (every member's area, not only the first wall's): loaded[i] - member
   i's area is loaded here; applied[i] - that area's record list is complete on this link. *allLoaded = every member's area is loaded
   (no member = 0); *allApplied = every LOADED member's area has its record list complete */
inline void BuildGroupAreasFold(const int* loaded, const int* applied, int n, int* allLoaded, int* allApplied)
{
    int al = (n > 0) ? 1 : 0, aa = 1;
    for (int i = 0; i < n; ++i)
    {
        if (loaded[i] == 0) al = 0;
        else if (applied[i] == 0) aa = 0;
    }
    *allLoaded = al; *allApplied = aa;
}
/* T-1408 fold 8: an OWED owner step (new work while a record of the group could still win, BuildGroupStepWhat) is decided WITH the group:
   recordWaits - a record of the group is still queued and may win: kept owed (its apply drops it - the work was written over); fold 9
   (F1296): a member's area not loaded (allLoaded 0, BuildGroupAreasFold) - kept owed WHATEVER the world-server link's state: an
   area-unload drop of the queued records is no decision, the records come back at the area's next load and decide it then (running it
   at the drop would stamp the owner's older value over them); the world's leaving ends it; linked (a world server's link with this
   game's slot) and a loaded member area's record list not complete on this link (allApplied 0) - kept owed; otherwise no record won -
   it runs (a group step). 1 = run */
inline int BuildGroupOwedRuns(int recordWaits, int linked, int allLoaded, int allApplied)
{
    if (recordWaits != 0) return 0;
    if (allLoaded == 0) return 0;
    if (linked != 0 && allApplied == 0) return 0;
    return 1;
}
/* T-1408 fold 8 / 9: help on this player's own GROUPED wall waits behind the group's decision (nothing applied, nothing confirmed; tried
   at the next safe point - the STATE drain decides the group after the help drain), then applies on top and steps the group - never
   confirmed and then written over by a record: while a record of the group is still queued here and may win (recordWaits), and fold 9
   (F1296), as the own-row hold (piecerec::PieceOwnRowHold): while linked and a loaded member area's record list is still loading
   (allApplied 0, BuildGroupAreasFold) - a record still to come may win. 1 = waits */
inline int BuildWallHelpWaitsForRecords(int groupedOwnWall, int recordWaits, int linked, int allApplied)
{
    if (groupedOwnWall == 0) return 0;
    if (recordWaits != 0) return 1;
    return (linked != 0 && allApplied == 0) ? 1 : 0;
}
/* T-1408 fold 9 (F1296), THE STATE DRAIN (BuildCopyDrain): what one queue place does - due: its retry gap is over (nextTry <= the safe
   point); ownRec: an own record's STATE; groupQueued: how many own records of its wall group are queued here (BdOwnGroupQueue, 0 = not
   a wall group). Skip (not due), one group turn (BdOwnGroupTurn: two or more queued), or the record alone (BdApplyOne) */
const int kBuildDrainSkip = 0, kBuildDrainOne = 1, kBuildDrainGroup = 2;
inline int BuildStateDrainStep(int due, int ownRec, int groupQueued)
{
    if (due == 0) return kBuildDrainSkip;
    return (ownRec != 0 && groupQueued >= 2) ? kBuildDrainGroup : kBuildDrainOne;
}
/* T-1408 fold 9 (F1296 NIT), ONE GROUP TURN (BdOwnGroupTurn): the queued records of the group in BuildGroupApplyOrder's order, each
   applied only when its own retry gap is over (nextTry <= drainNo; a record kept by its own road - a deferred engine call, an unfinished
   complete - waits its gap), and none while the WINNER waits its gap (winner first: a later one is never judged before it). order: n
   indices (the full order); pick: the indices to apply now, in order. Returns how many */
inline int BuildGroupTurnPick(const unsigned int* stamps, const float* shares, const unsigned int* nextTry, int n, unsigned int drainNo, int* order, int* pick)
{
    const int win = BuildGroupApplyOrder(stamps, shares, n, order);
    if (win < 0 || nextTry[win] > drainNo) return 0;
    int k = 0;
    for (int i = 0; i < n; ++i)
        if (nextTry[order[i]] <= drainNo) pick[k++] = order[i];
    return k;
}
/* T-1408 fold 9 (F1296), THE OWNER'S GAME: a winning FINISHED own grouped record whose wall the engine's complete-now call left
   unfinished (the copy reads complete=0 after it) is tried again kRetryGap apart until maxTries tries have failed (tries: this one
   counted), then ENDED: the record leaves the queue, its row and record are caught down to the wall's real progress (BdOwnGroupCatchUp)
   and that is logged once - it never holds the wall's help (BuildWallHelpWaitsForRecords) or its group for ever. ownGroupRec: an own
   grouped wall's record on the finish road (any other STATE keeps the older bounded-then-slow retry). 1 = end it now */
inline int BuildWallFinishEnds(int ownGroupRec, int tries, int maxTries)
{
    return (ownGroupRec != 0 && tries >= maxTries) ? 1 : 0;
}
/* T-1408 fold 8: a write that LOWERS a wall (a hit, a dismantle step, a ruin): the share below the last one by more than
   kBuildGroupShareSame (raw progress / needed - a finished wall's health counts), complete 1 -> 0, or whole -> ruin. Such work always
   takes its OWN new stamp, never one shared with the helper's untouched own-record copies (it would tie with them and the higher share
   would win, BuildWriterSameOwnerStep). An unusable needed on either side is no lowering. 1 = lowers */
inline int BuildWallWriteLowers(float p0, float n0, int c0, int x0, float p, float n, int c, int x)
{
    if (x0 == 0 && x != 0) return 1;
    if (c0 != 0 && c == 0) return 1;
    if (!(n0 > 0.0f && n0 < 3.0e38f) || !(n > 0.0f && n < 3.0e38f)) return 0;
    float s0 = p0 / n0, s1 = p / n;
    if (!(s0 > 0.0f)) s0 = 0.0f;
    if (s0 > 1.0f) s0 = 1.0f;
    if (!(s1 > 0.0f)) s1 = 0.0f;
    if (s1 > 1.0f) s1 = 1.0f;
    return (s1 < s0 - kBuildGroupShareSame) ? 1 : 0;
}
/* T-1408 fold 9 (F1296): a LOWERING write (a hit) on one of the helper's own-record copies is the GROUP's (the engine's rule: a hit lowers
   the group's one record): every other own-record copy of that owner step the helper holds (BuildWriterSameOwnerStep) is lowered BY THE
   HIT'S SIZE - fold 10 (F1304 SHOULD 1): the share the hit took (dropShare, 0..1), not down to the hit copy's whole share (a sibling built
   to 0.8 while the hit copy went 0.5 -> 0.45 is 0.75, the engine's one-record value; fold 9 pulled it to 0.45 and lost the build) -
   written into its own engine record as its share less dropShare of ITS needed, never below 0, so a later build on a sibling carries the
   lowered value. Nothing is raised. The lowered copy and its siblings then take ONE step stamp together (fold 7's rule). sibP / sibN: the
   sibling's progress / needed. 1 = lower the sibling to *to */
inline int BuildWallSiblingLowerBy(float sibP, float sibN, float dropShare, float* to)
{
    *to = sibP;
    if (!(sibN > 0.0f && sibN < 3.0e38f) || !(dropShare > kBuildGroupShareSame && dropShare <= 1.0f)) return 0;
    float s = sibP / sibN;
    if (!(s > 0.0f)) return 0;   /* already at 0 (or unreadable): nothing to lower */
    if (s > 1.0f) s = 1.0f;
    float t = s - dropShare;
    if (t < 0.0f) t = 0.0f;
    *to = t * sibN;
    return 1;
}
/* T-1408 fold 10 (F1304 SHOULD 1): the share a lowering write took off a copy - its share before less its share after (0 when not a
   number, a raise or an unusable needed) */
inline float BuildWallDropShare(float p0, float n0, float p1, float n1)
{
    if (!(n0 > 0.0f && n0 < 3.0e38f) || !(n1 > 0.0f && n1 < 3.0e38f)) return 0.0f;
    float s0 = p0 / n0, s1 = p1 / n1;
    if (!(s0 > 0.0f)) s0 = 0.0f;
    if (s0 > 1.0f) s0 = 1.0f;
    if (!(s1 > 0.0f)) s1 = 0.0f;
    if (s1 > 1.0f) s1 = 1.0f;
    const float d = s0 - s1;
    return (d > 0.0f) ? d : 0.0f;
}
/* T-1408 fold 10 (F1331 1c): a piece's progress as it is KEPT - stored in its row, sent, or written to its record: within 0..needed (the
   engine's hit lowers a wall's record with no floor: 60 blows read -1.71). Not a number = 0; a needed that is not a number above 0
   floors at 0 only. *clamped = 1 when the value was changed */
inline float BuildProgressKept(float p, float n, int* clamped)
{
    float v = p;
    if (!(v > -3.0e38f && v < 3.0e38f)) v = 0.0f;
    if (v < 0.0f) v = 0.0f;
    if (n > 0.0f && n < 3.0e38f && v > n) v = n;
    if (clamped != 0) *clamped = (v != p) ? 1 : 0;
    return v;
}
/* T-1408 fold 10 (F1331 3): a helper's work on a ruin its engine rebuilt during the step: the work past the rebuild - progress after the
   step less the value the rebuild left (read at the rebuild's own setDestroyed(0), rebuildRead 1); the owner runs the same rebuild before
   it applies this. Without a rebuild value: 0 (the old rule - nothing but the reset goes) */
inline float BuildHelpRebuildExtra(float p1, int rebuildRead, float rebuildP)
{
    if (rebuildRead == 0 || !(rebuildP > -3.0e38f && rebuildP < 3.0e38f) || !(p1 > -3.0e38f && p1 < 3.0e38f)) return 0.0f;
    const float d = p1 - rebuildP;
    return (d > 0.0f) ? d : 0.0f;
}
/* T-1408 fold 10 (F1331 3): why the owner kept only the materials of a helper's work on a grouped wall - one reason, the first that holds */
const int kBuildWallKeepNone = 0, kBuildWallKeepNoWork = 1, kBuildWallKeepRuin = 2, kBuildWallKeepWriter = 3, kBuildWallKeepNotWhole = 4, kBuildWallKeepNeedUnread = 5;
inline int BuildWallKeepWhy(int wallKeep, float progress, int ruin, int copyWriter, int whole)
{
    if (wallKeep == 0) return kBuildWallKeepNone;
    if (!(progress > 0.0f)) return kBuildWallKeepNoWork;
    if (ruin != 0) return kBuildWallKeepRuin;
    if (copyWriter != 0) return kBuildWallKeepWriter;
    if (whole == 0) return kBuildWallKeepNotWhole;
    return kBuildWallKeepNeedUnread;
}
/* what a change of an own wall's group does to its stamp (BdWallGroupsMark). adopt: a received or loaded record's value was applied - its
   stamp is taken, no step; newWork: the engine's build step with an amount (not the mod's own amount-0 wall step, not the add-materials
   amount-0 call), a dismantle step, a hit, help applied (BuildGroupNewWork); recordWaits: a record of the group queued here may still win
   (BuildGroupStampMayWin) - the step is OWED until that record is applied (then the owed step is dropped: the work it carried was written
   over by the newer record); fold 8: else decided with the group - it runs once no record of the group is queued and its area's
   records were applied here (BuildGroupOwedRuns; an area-unload drop of the records keeps it owed) */
const int kBuildGsNone = 0, kBuildGsStep = 1, kBuildGsOwe = 2, kBuildGsAdopt = 3;
inline int BuildGroupStepWhat(int adopt, int newWork, int recordWaits)
{
    if (adopt != 0) return kBuildGsAdopt;
    if (newWork == 0) return kBuildGsNone;
    return recordWaits != 0 ? kBuildGsOwe : kBuildGsStep;
}
/* a progress event (BuildTick's kinds: 1 the engine's progress call, 2 a dismantle step, 4 a melee hit on an own wall) that is new work;
   recApply: queued while the mod applied an own record's STATE (its complete-now path, amount-0 wall step, threshold crossing) - never */
inline int BuildGroupNewWork(int kind, float amount, int recApply)
{
    if (recApply != 0) return 0;
    return ((kind == 1 && amount != 0.0f) || kind == 2 || kind == 4) ? 1 : 0;
}
/* a helper's wall copies that hold their OWN engine records (made here during play): the owner's game grouped them, this game cannot see
   the group. Two such copies of one owner whose owner's last STATEs carried the same group step stamp were written by ONE step of the
   owner's (a game's steps are unique on it: each sets its clock to the stamp it gives; the helper writes only while the owner's game is
   away, so no other game steps that owner's walls meanwhile) - one group: the helper's write steps them together with ONE stamp, each record
   keeping its own value; at the owner's load the tie key picks the higher share for the group */
inline int BuildWriterSameOwnerStep(int ownerA, unsigned int lastStampA, int ownerB, unsigned int lastStampB)
{
    return (ownerA >= 0 && ownerA == ownerB && lastStampA != 0 && lastStampA == lastStampB) ? 1 : 0;
}
/* the older builds' rule, only among the group's UNSTAMPED records: the highest usable share (a negative or NaN entry skipped).
   *best = that share. Returns its index, -1 = none */
inline int BuildWallLegacyShare(const float* shares, int n, float* best)
{
    int pick = -1;
    *best = -1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float s = shares[i];
        if (!(s >= 0.0f)) continue;
        if (pick < 0 || s > shares[pick]) pick = i;
    }
    if (pick >= 0) *best = shares[pick];
    return pick;
}
/* The owner's own grouped wall when one of its group's records is applied here (BdApplyOne, an own record's STATE): judged by its rank
   (BuildGroupRank) against the group's live value - no waiting for the rest of the group; a member landing later takes the group's current
   value (BdOwnGroupCatchUp). haveLead: the group's lead here is this player's wall (the value is written through it); rec / live: the
   record's stamp and the group's live stamp; share: the record's share (a stamped record) or the BuildWallLegacyShare pick (an unstamped
   one), negative = none; liveP / liveN: the lead's loaded progress / needed (the live share is the tie key's other side). The value is the
   share of the lead's needed, below 0.999 of it (0x558680 completes at needed); within 0.0005 of needed of the loaded value nothing is
   written (the stamp is taken). A newer FINISHED record is the finish road's (kBuildStampFinish, BuildWallCopyAction). *progress = the value */
const int kBuildStampIgnored = 0, kBuildStampWrite = 1, kBuildStampSame = 2, kBuildStampNoLead = 3, kBuildStampNone = 4, kBuildStampFinish = 5;   /* fold 8: no wait verdict */
inline float BuildGroupLiveShare(float liveP, float liveN)
{
    if (!(liveN > 0.0f && liveN < 3.0e38f) || !(liveP == liveP)) return -1.0f;   /* unusable (NaN fails) */
    const float s = liveP / liveN;
    return s > 0.0f ? (s < 1.0f ? s : 1.0f) : 0.0f;
}
inline int BuildWallStampApply(int haveLead, unsigned int rec, unsigned int live, float share, float liveP, float liveN, float* progress)
{
    *progress = 0.0f;
    const int rank = BuildGroupRank(rec, share, live, BuildGroupLiveShare(liveP, liveN));
    if (rank != kBuildRankNewer && rank != kBuildRankLegacy) return kBuildStampIgnored;
    if (haveLead == 0) return kBuildStampNoLead;
    if (!(share >= 0.0f) || !(liveN > 0.0f && liveN < 3.0e38f)) return kBuildStampNone;   /* NaN fails too */
    float v = share * liveN;
    const float cap = liveN * 0.999f;
    if (!(v <= cap)) v = cap;
    *progress = v;
    float d = v - liveP;
    if (d < 0.0f) d = -d;
    if (d <= 0.0005f * liveN) return kBuildStampSame;   /* a NaN live value is written over */
    return kBuildStampWrite;
}
/* T-1408 fold 7 / 8: the WHOLE decision BdApplyOne takes for one own grouped wall's record (the real road calls only this) against the
   group's live value - never a wait (fold 8: the group's queued records come in BuildGroupApplyOrder's order in one turn, BdOwnGroupTurn;
   a record arriving after the group was decided is judged against the group's current stamp the same way); a finished
   record that ranks newer takes the finish road (finishedRec: BuildWallCopyAction did not say materials only); else BuildWallStampApply.
   *after: kBuildGroupAfterAdopt - once the write landed, every record of the group takes the record's value and ITS stamp (no step,
   BdOwnGroupLanded); kBuildGroupAfterCatchUp - the record ranks older (an older stamp, a tie that loses, an unstamped record under a
   stamped value): its row and record take the group's current value and live stamp (BdOwnGroupCatchUp); kBuildGroupAfterNone. */
const int kBuildGroupAfterNone = 0, kBuildGroupAfterAdopt = 1, kBuildGroupAfterCatchUp = 2;
inline int BuildOwnGroupDecide(int haveLead, int finishedRec, unsigned int rec, float share, unsigned int live, float liveP, float liveN,
                               float* progress, int* after)
{
    *progress = 0.0f; *after = kBuildGroupAfterNone;
    const int rank = BuildGroupRank(rec, share, live, BuildGroupLiveShare(liveP, liveN));
    int a = kBuildStampIgnored;
    if (finishedRec != 0 && (rank == kBuildRankNewer || rank == kBuildRankLegacy)) a = kBuildStampFinish;
    else a = BuildWallStampApply(haveLead, rec, live, share, liveP, liveN, progress);
    if (a == kBuildStampWrite || a == kBuildStampFinish) *after = kBuildGroupAfterAdopt;
    else if (rank == kBuildRankOlder) *after = kBuildGroupAfterCatchUp;
    return a;
}
inline const char* BuildWallStampName(int a)
{
    return a == kBuildStampWrite ? "NEWER - the group's value written through the lead (the engine's wall step draws every member)"
         : (a == kBuildStampSame ? "NEWER - equals the loaded value, its stamp taken"
         : (a == kBuildStampFinish ? "NEWER and finished - the finish road"
         : (a == kBuildStampIgnored ? "OLDER than the group's live value (stamp, then share), or the same - ignored, materials only"
         : (a == kBuildStampNoLead ? "newer, but the group's lead here is not this player's - materials only"
         : "newer, but no usable share or needed - materials only"))));
}
/* A melee hit on a wall (WallBuilding's 0x29D520, hooked): wall = the target is a WallBuilding; playerWall = a player owns it (this
   game's player faction, or another player's stand-in); othersKnown = another player is known here (a stand-in exists); damageRead =
   the damage struct's +0 / +4 were read; replay = the mod's own replay of a hit on the main thread. A player wall's hit is not run by
   the engine where it lands: it is queued (kBuildHitRouteQueue) and the main thread asks who writes the wall. A town's or an unowned
   wall, a lone game, the mod's replay and a damage struct that cannot be read take the engine's hit as it comes. */
const int kBuildHitRouteEngine = 0, kBuildHitRouteQueue = 2;
inline int BuildWallHitRoute(int wall, int playerWall, int othersKnown, int damageRead, int replay)
{
    if (wall == 0 || replay != 0 || playerWall == 0 || othersKnown == 0 || damageRead == 0) return kBuildHitRouteEngine;
    return kBuildHitRouteQueue;
}
/* A melee hit on a door (DoorStuff's 0x29D650, hooked): door = the target is a door its parent piece lists, at index (its place in that
   list); playerPiece = a player owns the parent piece (this game's player faction or a stand-in); othersKnown, damageRead and replay as
   for a wall. A player piece's door hit is not run where it lands: it is queued (kBuildHitRouteQueue) and the main thread asks who
   writes the piece's record. Any other door, a door past kBuildHitMaxDoor, a lone game, the mod's replay and an unread damage struct
   take the engine's hit. */
inline int BuildDoorHitRoute(int door, int index, int playerPiece, int othersKnown, int damageRead, int replay)
{
    if (door == 0 || index < 0 || index > kBuildHitMaxDoor || replay != 0 || playerPiece == 0 || othersKnown == 0 || damageRead == 0) return kBuildHitRouteEngine;
    return kBuildHitRouteQueue;
}
/* BuildRepairSumDue: a repair's gains on a door another game writes are summed and asked of that writer at most once every gapMs.
   gain (0 for a look with no new gain; not a number, below 0 or over kBuildHitMax ignored) is added to *sum. 1 = an ask is due now:
   *out = the summed gain (at most kBuildHitMax - the rest stays in *sum for the next ask), *lastAt = now, *haveLast = 1. The first
   ask of a door is due at once. */
inline int BuildRepairSumDue(float* sum, unsigned int* lastAt, int* haveLast, float gain, unsigned int now, unsigned int gapMs, float* out)
{
    if (sum == 0 || lastAt == 0 || haveLast == 0 || out == 0) return 0;
    if (gain > 0.0f && gain <= kBuildHitMax) *sum += gain;
    if (!(*sum > 0.0f)) { *sum = 0.0f; return 0; }
    if (*haveLast != 0 && (unsigned int)(now - *lastAt) < gapMs) return 0;
    float send = *sum;
    if (send > kBuildHitMax) send = kBuildHitMax;
    *out = send;
    *sum -= send;
    if (!(*sum > 0.0f)) *sum = 0.0f;
    *lastAt = now; *haveLast = 1;
    return 1;
}
const unsigned int kBuildDoorRepairGapMs = 250u;   /* a door's repair asks: at most one per this many ms */
/* The game a HIT ask reached: dup = that asker's seq was replayed here already (answered again, not hit again); held = the piece has a
   live registry row here (this game's own, or a copy); loaded = it is loaded here under its key and owned as its row says; destroyed = it
   is a ruin; writer = this game writes the wall now (the machines' writer ask). kBuildHitApplied on a new seq = the hit is replayed
   through the engine's own hit routine; kBuildHitNotWriter = the writer changed while the ask travelled. */
inline int BuildHitVerdict(int dup, int held, int loaded, int destroyed, int writer)
{
    if (dup != 0) return kBuildHitApplied;
    if (held == 0) return kBuildHitGone;
    if (loaded == 0) return kBuildHitNotLoaded;
    if (destroyed != 0) return kBuildHitRuin;
    if (writer == 0) return kBuildHitNotWriter;
    return kBuildHitApplied;
}
/* A HIT's DROP: the progress the writer's hit took, p0 - p1 read around its replay (never below 0; not a number = 0) */
inline float BuildHitDrop(float p0, float p1)
{
    const float d = p0 - p1;
    return (d > 0.0f && d < 3.0e38f) ? d : 0.0f;
}
/* A HIT RESULT on a game that did not run the hit. recOk = this game may write the wall's progress (the owner's own piece - the engine's
   hit writes its record, a group's or not - or a copy holding its own record); drop = the result's drop; liveP = this game's live
   progress. A ruin in the result ruins a whole wall here; otherwise a complete whole wall takes the drop off its own live health, so a
   repair made here since the hit is kept; at <= 0 it is ruined, as the engine's hit ruins it (*to = 0). A ruin here is not made whole. */
const int kBuildHitResNone = 0, kBuildHitResDestroy = 1, kBuildHitResProgress = 2;
inline int BuildHitDropAction(int recOk, int msgDestroyed, int liveDestroyed, int liveComplete, float drop, float liveP, float* to)
{
    *to = liveP;
    if (liveDestroyed != 0) return kBuildHitResNone;
    if (msgDestroyed != 0) { *to = 0.0f; return kBuildHitResDestroy; }
    if (recOk == 0 || liveComplete == 0 || !(drop >= 0.0f && drop < 3.0e38f) || !(liveP > -3.0e38f && liveP < 3.0e38f)) return kBuildHitResNone;
    const float v = liveP - drop;
    if (!(v > 0.0f)) { *to = 0.0f; return kBuildHitResDestroy; }
    *to = v;
    return kBuildHitResProgress;
}
/* Whether a HIT RESULT from slot `from` is taken here. verdict / writerSlot = this game's own writer ask for that wall (1 this game, 2
   another game - writerSlot, 0 none); owed = the writer sends it again for a hit the owner's game did not take; owner = this game owns
   the wall. A result is taken only from the game this game's ask names as the writer - a stale writer (during a holder change) or any
   other game is refused, and the owner refuses one while it writes the wall itself. An owed result is the owner's only: its hit was
   written while the owner's game did not have the piece loaded, whoever writes the wall now (BuildHitOwedApplies guards a double hit). */
inline int BuildHitResultAccept(int verdict, int writerSlot, int from, int owed, int owner)
{
    if (from < 0) return 0;
    if (owed != 0) return owner != 0 ? 1 : 0;
    return (verdict == 2 && writerSlot == from) ? 1 : 0;
}
/* A health as a share of its needed (-1 = the needed or the health is not a usable number). Health is compared across games as a
   share: a writer's copy and the owner's piece can hold records with different needed values (a group's, or the wall's own). */
inline float BuildHitShare(float p, float n)
{
    if (!(n > 0.0f && n < 3.0e38f) || !(p > -3.0e38f && p < 3.0e38f)) return -1.0f;
    return p / n;
}
const float kBuildHitShareEps = 1.0e-4f;   /* shares closer than this are the same health (float rounding across games) */
/* liveP / liveN is above after / n (shares; with either one unusable, the raw values) */
inline int BuildHitAbove(float liveP, float liveN, float after, float n)
{
    const float a = BuildHitShare(liveP, liveN), b = BuildHitShare(after, n);
    if (a < 0.0f || b < 0.0f) return (liveP > after) ? 1 : 0;
    return (a > b + kBuildHitShareEps) ? 1 : 0;
}
/* THE OWNER'S GAME: an owed result is applied once (seen = its seq from that writer was taken here) and only while this game's health
   share is above the share the writer expects after the hits - a zone record the writer saved after them already carries them */
inline int BuildHitOwedApplies(int seen, float liveP, float liveN, float after, float n)
{
    return (seen == 0 && BuildHitAbove(liveP, liveN, after, n) != 0) ? 1 : 0;
}
/* THE OWNER'S GAME: an owed result's new health - the summed raw drop off the live health, never below the writer's expected share of
   this game's own needed, so a record that already carries part of the drop is lowered to the expected health and never past it, and a
   repair made here since is kept. A ruin in the result, or a health at <= 0, ruins it (kBuildHitResDestroy); kBuildHitResProgress when
   it lowers (*to = the value); kBuildHitResNone otherwise. */
inline int BuildHitOwedTarget(int msgDestroyed, int liveComplete, float liveP, float liveN, float drop, float after, float n, float* to)
{
    *to = liveP;
    if (msgDestroyed != 0) { *to = 0.0f; return kBuildHitResDestroy; }
    if (liveComplete == 0 || !(drop >= 0.0f && drop < 3.0e38f) || !(liveP > -3.0e38f && liveP < 3.0e38f)) return kBuildHitResNone;
    float v = liveP - drop;
    const float s = BuildHitShare(after, n);
    if (s >= 0.0f && liveN > 0.0f && liveN < 3.0e38f && v < s * liveN) v = s * liveN;
    if (!(v < liveP)) return kBuildHitResNone;
    if (!(v > 0.0f)) { *to = 0.0f; return kBuildHitResDestroy; }
    *to = v;
    return kBuildHitResProgress;
}
/* THE OWNER'S GAME: an own wall found again when its area loads here (the zone scan's walk of a newly loaded zone) owes one STATE
   for that arrival - a game that wrote hits on it while it was not loaded here holds their drop until this game's STATE for the
   wall reaches it (the writer's BuildHitOwedOnState). Only an own wall (not a copy, not removed) whose PLACE went out.
   1 = its STATE goes now */
inline int BuildOwnWallArriveState(int own, int copyHere, int removed, int sentOk, int isWall)
{
    return (own != 0 && copyHere == 0 && removed == 0 && sentOk != 0 && isWall != 0) ? 1 : 0;
}
/* TEST LEVER `buildtest progress` on a COMPLETE piece that is not a ruin: the engine's progress routine raises a complete piece's
   health past its needed amount, so the lever sets the health itself - the live health plus amount, never above needed (an amount
   at or past the gap repairs it to full; a health already above needed is put back to needed). kBuildTestRepairEngine = not this
   case (an unfinished piece or a ruin keeps the engine's routine), kBuildTestRepairWrite = write *to, kBuildTestRepairNone = nothing
   to change. */
const int kBuildTestRepairEngine = 0, kBuildTestRepairWrite = 1, kBuildTestRepairNone = 2;
inline int BuildTestRepairTarget(int complete, int destroyed, float p, float n, float amount, float* to)
{
    *to = p;
    if (complete == 0 || destroyed != 0 || !(n > 0.0f && n < 3.0e38f) || !(p > -3.0e38f && p < 3.0e38f)) return kBuildTestRepairEngine;
    float t = p;
    if (p > n) t = n;
    else if (amount > 0.0f) t = (amount >= n - p) ? n : p + amount;
    *to = t;
    return (t != p) ? kBuildTestRepairWrite : kBuildTestRepairNone;
}
/* THE WRITER'S GAME: what it owes the owner's game for one wall - ONE entry per (wall key, owner slot): the summed raw drop of every hit
   it wrote there that the owner's game has not taken, and the owner's health it expects after them (expectP in units of needN: the
   owner's last known health minus the sum, never below 0; 0 after a ruin). seq = the owed form's seq, drawn again whenever a hit is
   added; the fresh results in the sum by their own seqs; the last owed send (its seq, sum and the number of hits it held). */
struct BuildHitOwedRow
{
    std::string key;
    int ownerSlot;
    unsigned int seq;
    float sum, expectP, needN;
    unsigned char destroyed;
    std::vector<unsigned int> hitSeqs;
    std::vector<float> hitDrops;
    unsigned int sentSeq;
    float sentSum;
    size_t sentCount;
    int sent;
    unsigned long sentAt;
    BuildHitOwedRow() : ownerSlot(-1), seq(0), sum(0.0f), expectP(0.0f), needN(0.0f), destroyed(0), sentSeq(0), sentSum(0.0f), sentCount(0), sent(0), sentAt(0) {}
};
/* a hit's fresh result into the owed table: added to its wall's entry (the sum grows, the expected health falls by the drop, a new owed
   seq) or a new entry whose expected health starts from baseP / baseN - the owner's last known health for the wall. Past cap entries the
   oldest entry goes; past hitCap hits the oldest hit's seq is forgotten (its drop stays in the sum). Returns the entries dropped. */
inline size_t BuildHitOwedAdd(std::vector<BuildHitOwedRow>* t, const std::string& key, int ownerSlot, unsigned int hitSeq, float drop, unsigned char destroyed,
                              float baseP, float baseN, unsigned int owedSeq, size_t cap, size_t hitCap)
{
    BuildHitOwedRow* e = 0;
    for (size_t i = 0; i < t->size(); ++i) if ((*t)[i].key == key && (*t)[i].ownerSlot == ownerSlot) e = &(*t)[i];
    size_t dropped = 0;
    if (e == 0)
    {
        if (cap == 0) return 1;
        while (t->size() >= cap) { t->erase(t->begin()); ++dropped; }
        BuildHitOwedRow r;
        r.key = key; r.ownerSlot = ownerSlot; r.expectP = (baseP > 0.0f && baseP < 3.0e38f) ? baseP : 0.0f; r.needN = baseN;
        t->push_back(r);
        e = &t->back();
    }
    const float d = (drop > 0.0f && drop < 3.0e38f) ? drop : 0.0f;
    e->sum += d;
    e->expectP = (e->expectP - d > 0.0f) ? e->expectP - d : 0.0f;
    if (destroyed != 0) { e->destroyed = 1; e->expectP = 0.0f; }
    e->seq = owedSeq;
    e->hitSeqs.push_back(hitSeq);
    e->hitDrops.push_back(d);
    while (e->hitSeqs.size() > hitCap)
    {
        e->hitSeqs.erase(e->hitSeqs.begin());
        e->hitDrops.erase(e->hitDrops.begin());
        if (e->sentCount > 0) --e->sentCount;
    }
    return dropped;
}
/* the entry's owed form is sent now */
inline void BuildHitOwedMarkSent(BuildHitOwedRow* e, unsigned long now)
{
    e->sentSeq = e->seq; e->sentSum = e->sum; e->sentCount = e->hitSeqs.size(); e->sent = 1; e->sentAt = now;
}
/* the owner's game at slot answered seq for key. A verdict meaning it took the result (applied, ruin, faulted) takes it out of the
   entry - the last owed send (its sum and its hits) or one fresh hit (its drop; the expected health stays, the owner holds the hit now);
   the entry goes when nothing is left. Any other verdict ('not the writer', gone, not loaded) keeps it for the next owed round. */
const int kBuildHitOwedNoMatch = 0, kBuildHitOwedTaken = 1, kBuildHitOwedKept = 2;
inline int BuildHitOwedAnswer(std::vector<BuildHitOwedRow>* t, const std::string& key, unsigned int seq, int slot, int verdict)
{
    for (size_t i = 0; i < t->size(); ++i)
    {
        BuildHitOwedRow& e = (*t)[i];
        if (e.key != key || e.ownerSlot != slot) continue;
        size_t h = 0;
        while (h < e.hitSeqs.size() && e.hitSeqs[h] != seq) ++h;
        const int isSent = (e.sentSeq != 0 && e.sentSeq == seq) ? 1 : 0;
        if (isSent == 0 && h == e.hitSeqs.size()) continue;
        if (verdict != kBuildHitApplied && verdict != kBuildHitRuin && verdict != kBuildHitFaulted) return kBuildHitOwedKept;
        if (isSent != 0)
        {
            e.sum -= e.sentSum;
            const size_t n = (e.sentCount < e.hitSeqs.size()) ? e.sentCount : e.hitSeqs.size();
            e.hitSeqs.erase(e.hitSeqs.begin(), e.hitSeqs.begin() + (std::ptrdiff_t)n);
            e.hitDrops.erase(e.hitDrops.begin(), e.hitDrops.begin() + (std::ptrdiff_t)n);
            e.sentSeq = 0; e.sentSum = 0.0f; e.sentCount = 0; e.sent = 0;
        }
        else
        {
            e.sum -= e.hitDrops[h];
            if (h < e.sentCount) { --e.sentCount; e.sentSum -= e.hitDrops[h]; }
            e.hitSeqs.erase(e.hitSeqs.begin() + (std::ptrdiff_t)h);
            e.hitDrops.erase(e.hitDrops.begin() + (std::ptrdiff_t)h);
        }
        if (!(e.sum > 1.0e-6f)) e.sum = 0.0f;
        if (e.hitSeqs.empty() && e.sum == 0.0f) t->erase(t->begin() + (std::ptrdiff_t)i);
        return kBuildHitOwedTaken;
    }
    return kBuildHitOwedNoMatch;
}
/* THE WRITER'S GAME: the owner's STATE for a wall it owes (read from the owner's live piece, so the piece is loaded there), compared as
   shares. At or below the expected share, or a ruin: cleared. Above it: sent again (owed) - not while the last send is younger than
   gapMs and the owed seq has not changed since (seqChanged: a hit was added). */
const int kBuildHitOwedKeep = 0, kBuildHitOwedClear = 1, kBuildHitOwedResend = 2;
inline int BuildHitOwedOnState(float stateP, float stateN, int stateDestroyed, float after, float n, unsigned long now, unsigned long sentAt, int sent,
                               int seqChanged, unsigned long gapMs)
{
    if (stateDestroyed != 0 || BuildHitAbove(stateP, stateN, after, n) == 0) return kBuildHitOwedClear;
    if (sent != 0 && seqChanged == 0 && (unsigned long)(now - sentAt) < gapMs) return kBuildHitOwedKeep;
    return kBuildHitOwedResend;
}
/* The writer's game: the seqs replayed for one asker, the newest last (a resend that crossed the answer is not hit twice) */
inline int BuildHitSeen(const std::vector<unsigned int>& recent, unsigned int seq)
{
    for (size_t i = 0; i < recent.size(); ++i) if (recent[i] == seq) return 1;
    return 0;
}
inline void BuildHitRemember(std::vector<unsigned int>* recent, unsigned int seq, size_t cap)
{
    if (BuildHitSeen(*recent, seq) != 0) return;
    recent->push_back(seq);
    while (recent->size() > cap) recent->erase(recent->begin());
}
/* The writer's game: the seqs replayed per asker slot (and, for results, the seqs taken per writer slot) (a seq is that asker's own number, so the slot is part of the key) */
inline int BuildHitSeenBy(const std::map<int, std::vector<unsigned int> >& by, int slot, unsigned int seq)
{
    std::map<int, std::vector<unsigned int> >::const_iterator it = by.find(slot);
    return (it != by.end() && BuildHitSeen(it->second, seq) != 0) ? 1 : 0;
}
inline void BuildHitRememberBy(std::map<int, std::vector<unsigned int> >* by, int slot, unsigned int seq, size_t cap)
{
    BuildHitRemember(&(*by)[slot], seq, cap);
}
/* A HIT is for the moment it was made: an ask (the asking game) or an arrived ask (the writer's game) older than limit ms is dropped,
   never sent or replayed later. Tick counts, so a wrap between since and now still measures right. */
inline int BuildHitExpired(unsigned long now, unsigned long since, unsigned long limit)
{
    return ((unsigned long)(now - since) >= limit) ? 1 : 0;
}
/* A copy WALL that this game's engine made a ruin is put back to the owner's last STATE only when the owner's STATE carries that
   copy's health: the wall hit hook is installed (hits on copies go to the wall's writer) and the copy holds its own progress record
   (a group's record takes no progress writes, so a put-back would stand at progress <= 0 and the next swing ruin it again). Otherwise
   the ruin is kept. */
inline int BuildWallRuinPutBack(int hookInstalled, int ownRecord)
{
    return (hookInstalled != 0 && ownRecord > 0) ? 1 : 0;
}
inline const char* BuildHitWord(int v)
{
    switch (v)
    {
    case 0: return "ask";
    case 1: return "applied - the writer's engine took the hit (its STATE or result carries it)";
    case 2: return "refused - the writer has no live row for the piece (removed, handed or unknown)";
    case 3: return "refused - the piece is not loaded on the writer's game";
    case 4: return "refused - the piece is a ruin on the writer's game";
    case 5: return "refused - the engine's hit faulted on the writer's game";
    case 6: return "refused - that game does not write the wall now (asked once more of the writer)";
    case 7: return "result - the wall's state after the writer's hit";
    default: return "?";
    }
}
/* T-1381 (T2436 fix): where a hit's attacker is read. The engine's melee hit can run off the main thread (T2436: every name read
   '(name not read)'), and the name, faction and goal reads are main-thread reads. 1 = now (the hook is on the main thread),
   2 = later on the main thread through its uid (the mod tracks it), 0 = not readable (off the main thread and no uid). */
const int kBuildAtkReadNone = 0, kBuildAtkReadNow = 1, kBuildAtkReadByUid = 2;
inline int BuildHitAttackerReadWhere(int onMain, unsigned int uid)
{
    if (onMain != 0) return kBuildAtkReadNow;
    return (uid != 0) ? kBuildAtkReadByUid : kBuildAtkReadNone;
}
/* T-1381: who swung a hit on a player wall or door, as the HIT lines name it. known 0 = the engine gave no attacker; whose 1 = this
   game runs it, 0 = another game runs it (slot its game's, -1 not known), -1 no game shares it (it has no uid); name / faction / task
   empty or 0 = not read. Always ends with a 0 inside cap. */
inline void BuildHitAttackerWords(char* out, size_t cap, int known, const char* name, const char* faction, int whose, int slot,
                                  unsigned int uid, const char* task)
{
    if (out == 0 || cap == 0) return;
    std::string w;
    if (known == 0) w = "by nobody named (the engine gave no attacker)";
    else
    {
        char n[32];
        std::string run;
        if (whose == 1) run = "run by this game";
        else if (whose == 0 && slot >= 0) { std::sprintf(n, "%d", slot); run = std::string("run by slot") + n + "'s game"; }
        else if (whose == 0) run = "run by another game (slot not known)";
        else run = "no game shares it";
        std::string u = "no uid";
        if (uid != 0) { std::sprintf(n, "%u", uid); u = std::string("uid ") + n; }
        const std::string nm = (name != 0 && name[0] != 0) ? std::string(name).substr(0, 47) : std::string("(name not read)");
        const std::string fc = (faction != 0 && faction[0] != 0) ? std::string(faction).substr(0, 47) : std::string("(not read)");
        const std::string tk = (task != 0 && task[0] != 0) ? std::string(task).substr(0, 63) : std::string("(not read)");
        w = "by '" + nm + "' (faction '" + fc + "', " + run + ", " + u + ", doing '" + tk + "')";
    }
    const size_t k = (w.size() < cap - 1) ? w.size() : cap - 1;
    std::memcpy(out, w.c_str(), k);
    out[k] = 0;
}
/* T-221: whether a wall's progress record is its alone. recIsOwn: 1 = the record (vt+0x228) is the wall's own (+0x160), 0 = another
   wall's, -1 unreadable; groupCount = the wall's group count (+0x390, the walls the engine's completion 0x561A20 completes after it),
   -1 unreadable. A group's lead also holds the record at its own +0x160, so only a wall with its own record AND no group has the record
   alone. 1 = alone, 0 = a group's, -1 = not known. */
inline int BuildWallKeepsOwnRecord(int recIsOwn, int groupCount)
{
    if (recIsOwn < 0 || groupCount < 0) return -1;
    return (recIsOwn == 1 && groupCount == 0) ? 1 : 0;
}
/* T-221: the own pieces looked at for a wall group's members - the lead's key sector and the eight around it (a wall line placed
   across a sector edge stays one group). */
inline int BuildWallGroupNear(int leadSx, int leadSy, int sx, int sy)
{
    const int dx = sx - leadSx, dy = sy - leadSy;
    return (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) ? 1 : 0;
}
/* T-221: safe points before a wall copy's complete-now call is tried again while the copy stays unfinished: `gap` for the first
   `fastTries` tries, then `gap` x `slowMul` for as long as the STATE is held (a newer STATE replaces it). */
inline unsigned int BuildWallCompleteRetryGap(int tries, int fastTries, unsigned int gap, unsigned int slowMul)
{
    return (tries < fastTries) ? gap : gap * slowMul;
}
/* T-221: whether the kept-piece lay completes this player's own standing, unfinished piece because this player's own record (pp.build)
   holds it finished. Only a wall (a save made by a game that held only copies keeps walls as blueprints), not a ruin here
   (liveDestroyed) and not a ruin in the record (recDestroyed). The engine's wall completion (0x561A20) also completes every wall of
   the group sharing the record, so a grouped wall needs every wall found sharing that record to be this player's, standing whole and
   recorded finished (allOwnFinished), and at least the lead's group count of walls besides the lead found (found counts the lead;
   leadCount -1 = the lead was not found or its count is unreadable). 1 = complete it, 0 = leave it. */
inline int BuildKeptOwnCompleteVerdict(int wall, int liveDestroyed, int recComplete, int recDestroyed, int grouped, int allOwnFinished,
                                       int found, int leadCount)
{
    if (wall == 0 || liveDestroyed != 0 || recComplete == 0 || recDestroyed != 0) return 0;
    if (grouped == 0) return 1;
    if (allOwnFinished == 0 || leadCount < 0) return 0;
    return (found - 1 >= leadCount) ? 1 : 0;
}
/* P15: a copy made a ruin HERE by this game's own engine (not the mod's apply). The owner's game is the writer: its last applied STATE
   goes back on when that says whole; with none applied yet the copy waits for the owner's next STATE. */
const int kBuildLocalDestroyIgnore = 0, kBuildLocalDestroyRevert = 1, kBuildLocalDestroyWait = 2;
inline int BuildLocalDestroyVerdict(int isCopy, int modsOwn, int destroyedNow, int ownerKnown, int ownerDestroyed)
{
    if (isCopy == 0 || modsOwn != 0 || destroyedNow == 0) return kBuildLocalDestroyIgnore;
    if (ownerKnown == 0) return kBuildLocalDestroyWait;
    return ownerDestroyed != 0 ? kBuildLocalDestroyIgnore : kBuildLocalDestroyRevert;
}
/* house2: orderKept - the placement commit issues ONE build order (4d6810:452), so an ungated commit that handed any piece counts once */
inline int BuildCommitOrderKept(int handedInCommit, int gateInstalled) { return (handedInCommit > 0 && gateInstalled == 0) ? 1 : 0; }
/* house2 + fold (review-house2 MED2): a hand-over PLACE (an owner slot named) is LATE - re-sent before the REMOVE reached the placer -
   when the very placement it names was removed here: its (key, nonce) is on the removed list (exactListed). A new placement at the same
   spot draws a new nonce and is accepted. A PLACE with no nonce (an old sender, an old owed-file row) keeps the key-only rule
   (keyListed = the key is on the list with any nonce). An ordinary PLACE after a REMOVE stays a new piece at the same spot (build1-d). */
inline bool BuildLatePlaceRefused(unsigned short ownerSlot, unsigned int nonce, int exactListed, int keyListed)
{
    if (ownerSlot == kBuildOwnerSender) return false;
    return (nonce != 0) ? (exactListed != 0) : (keyListed != 0);
}
/* house2 fold 2 (recheck-house2): a row the load scan rebuilt has no nonce (0); a hand-over PLACE re-sent for it carrying a nonce gives
   the row that nonce, so a later removal lists the real (key, nonce). A row that has a nonce keeps it. */
inline unsigned int BuildAdoptNonce(unsigned int rowNonce, unsigned short ownerSlot, unsigned int placeNonce)
{
    return (rowNonce == 0 && ownerSlot != kBuildOwnerSender && placeNonce != 0) ? placeNonce : rowNonce;
}

/* par13 (parity P13, parity-audit-buildings-world.md H2): the owed-REMOVE list. An own piece dismantled while the link is down (its
   REMOVE send failed) is owed that REMOVE; build.cpp's BdRosterTick sends the list once the link is up, ahead of any PLACE, and a
   world load forgets it (the dismantle belongs to the world it was made in). Add: once per key - 1 = added, 0 = already owed. */
inline int BuildOweRemoveAdd(std::vector<std::string>* owed, const std::string& key)
{
    for (size_t i = 0; i < owed->size(); ++i) if ((*owed)[i] == key) return 0;
    owed->push_back(key);
    return 1;
}
/* par13: a key placed again is no longer owed a REMOVE. 1 = it was owed */
inline int BuildOweRemoveDrop(std::vector<std::string>* owed, const std::string& key)
{
    int was = 0;
    for (size_t i = 0; i < owed->size(); )
    {
        if ((*owed)[i] == key) { owed->erase(owed->begin() + (std::vector<std::string>::difference_type)i); was = 1; }
        else ++i;
    }
    return was;
}
/* par13: send from the front, at most cap, stopping at the first failed send (the rest wait, in order, for the next tick). A sent
   entry leaves the list, so each owed REMOVE goes once. send(key) is true when the REMOVE went. Returns how many went. */
template <class Send>
inline size_t BuildOweRemoveFlush(std::vector<std::string>* owed, size_t cap, Send& send)
{
    size_t k = 0;
    while (k < owed->size() && k < cap && send((*owed)[k])) ++k;
    owed->erase(owed->begin(), owed->begin() + (std::vector<std::string>::difference_type)k);
    return k;
}
/* par13: a world load forgets the list. Returns how many were dropped */
inline size_t BuildOweRemoveForget(std::vector<std::string>* owed)
{
    const size_t n = owed->size();
    owed->clear();
    return n;
}

/* ---- help1 (owner 172, p51-help1-design.md): the pure decisions (suite-tested) ------------------------------------------------
   The helper works on its copy; its game sends the work (HELP_WORK) and the owner's game applies it to the real piece. */
const int kBuildHelpApply = 0, kBuildHelpDup = 1, kBuildHelpHold = 2;
/* the owner: a seq at or below the last one applied from that slot for that piece is a duplicate (a resend) - dropped, its confirmation
   goes again. help1 fold (review HIGH 1): ONLY the next one (last + 1) is applied; a higher one is HELD until the gap fills (the helper
   re-sends every unconfirmed entry) - never applied out of order, so an earlier entry is never confirmed away unapplied */
inline int BuildHelpSeqVerdict(unsigned int lastApplied, unsigned int seq)
{
    if (seq == 0 || seq <= lastApplied) return kBuildHelpDup;
    return (seq == lastApplied + 1) ? kBuildHelpApply : kBuildHelpHold;
}
/* help1 fold (review MED 3): the owner's piece for a HELP_WORK key. A row that is removed, dismantled, not own or a copy here - or no row
   but the key is on the list of own rows swept after their removal (goneListed) - is GONE: answered HELP_GONE (the helper drops the
   work and hands its materials back). No row and not listed is UNKNOWN (not yet seen this session): dropped unconfirmed, sent again.
   help1 fold 3: rowExists = a row of the work's OWN placement (same nonce); goneListed = the key has a row of another placement, a gone
   record or a swept mark here */
const int kBuildHelpRowOk = 0, kBuildHelpRowGone = 1, kBuildHelpRowUnknown = 2;
inline int BuildHelpRowVerdict(int rowExists, int own, int copyHere, int removed, int dismantled, int goneListed)
{
    if (rowExists == 0) return goneListed != 0 ? kBuildHelpRowGone : kBuildHelpRowUnknown;
    if (removed != 0 || dismantled != 0 || own == 0 || copyHere != 0) return kBuildHelpRowGone;
    return kBuildHelpRowOk;
}
/* T-23 (owner 1460 a): the same verdict on a game that WRITES another player's piece (its owner's game away; writerHere = this game is
   the piece's one writer and the row its copy): the work is applied there as on the owner's own piece */
inline int BuildHelpRowVerdictW(int rowExists, int own, int copyHere, int writerHere, int removed, int dismantled, int goneListed)
{
    if (rowExists != 0 && copyHere != 0 && writerHere != 0) return BuildHelpRowVerdict(1, 1, 0, removed, dismantled, goneListed);
    return BuildHelpRowVerdict(rowExists, own, copyHere, removed, dismantled, goneListed);
}
/* help1 fold 4 (re-check #1): the owner's own row after a restart - the load scan registers the save's piece with nonce 0; its placement
   nonce comes back from the pp.build PLACE record of that key (whichever of the two runs first). A row that has a nonce keeps it; a copy
   never takes one from here; no record (or a record with none) gives none. */
inline unsigned int BuildOwnNonceRestore(unsigned int rowNonce, int own, int copyHere, int placeKnown, unsigned int placeNonce)
{
    return (rowNonce == 0 && own != 0 && copyHere == 0 && placeKnown != 0) ? placeNonce : rowNonce;
}
/* help1 fold 5 (T-268): a pp.build restore found its piece standing (the loaded area brought it in) and owned by this game. An own row
   takes its PLACE's nonce; NO row (the load scan ran before the area loaded) - the live piece is registered as this game's own, as the
   scan does; a copy / not-own row is left alone; another owner's piece does nothing here (refused). */
const int kBuildRsPresentNothing = 0;
const int kBuildRsPresentNonce = 1;
const int kBuildRsPresentRegister = 2;
const int kBuildRsPresentDropWorld = 3;   /* the piece standing mine is a world resource node (coopown::WorldNodeOf): its saved row goes */
inline int BuildRestorePresentAct(int mineLive, int rowExists, int own, int copyHere, int worldNode = 0)
{
    if (mineLive == 0) return kBuildRsPresentNothing;
    if (worldNode != 0) return kBuildRsPresentDropWorld;
    if (rowExists == 0) return kBuildRsPresentRegister;
    return (own != 0 && copyHere == 0) ? kBuildRsPresentNonce : kBuildRsPresentNothing;
}
/* T-1335 (H-T1335-buyerrevert, design a): THIS game's own word names it a piece's owner - its own row (a pp.build restore, the purchase
   registered the bought house as its own piece) or its own piece record (owner slot = this game's) - and the piece stands at its key
   with an owner that is not this game's player. That happens when the load brought an area copy older than the purchase (the area
   holder's saves refused as stale, or the holder gone with no save after it) or the town's fresh-area rebuild gave the piece the town's
   faction. ownNames: an own row or own record names this game; liveMine: the standing piece's owner is this game's player;
   livePlayerSide: it is another player's (a stand-in, a player faction, a save's player faction - BdOwnerIsPlayerSide);
   sameBuilding: the standing piece's template is the row's (an unreadable one is not). Only an owner that is no player's (the town, an
   NPC faction, none) is written back; another player's piece is never taken, nor a different building at the key. T-1335 fold
   (review MED 1, owner rule 1444 - no world node is ever a player's): a world resource node standing at the key (worldNode: the live
   piece itself, coopown::WorldNodeOf, as the restore road's later node drop and the PLACE's node refusal test it) is never written. */
const int kBuildOwnRsNone = 0;            /* nothing names this game, or the piece is already its player's */
const int kBuildOwnRsRestore = 1;         /* the owner written back to this game's player (the engine's setFaction road) */
const int kBuildOwnRsOtherPlayer = 2;     /* another player's piece: left */
const int kBuildOwnRsOtherBuilding = 3;   /* no player's owner, but not the row's building (or its template does not read): left */
const int kBuildOwnRsWorldNode = 4;       /* a world resource node (an ore rock): never any player's - left */
inline int BuildOwnRestoreAct(int ownNames, int liveMine, int livePlayerSide, int sameBuilding, int worldNode = 0)
{
    if (ownNames == 0 || liveMine != 0) return kBuildOwnRsNone;
    if (worldNode != 0) return kBuildOwnRsWorldNode;
    if (livePlayerSide != 0) return kBuildOwnRsOtherPlayer;
    return (sameBuilding != 0) ? kBuildOwnRsRestore : kBuildOwnRsOtherBuilding;
}
/* T-1335 fold (review MED 2, single-player parity): what the restore runs around its owner write. The engine's purchase (the buyer's
   own, and its replay on every other game - BdCopyOneBody's place-adopt) tears down the house's town furniture by the keep rule
   (BdBuyTeardown: players' pieces and keyed pieces kept) and moves its residents out; a restore over an older area copy that brought
   that furniture and those residents back does the same, through the same steps the replay runs, in its order: the engine's
   isForSale first; then the residents on hold for the house made (TownGenBuyHeld) so the world side collects them; then the world
   side (residents out, town lists, doors re-owned not opened, the furniture teardown with the room step, the interior's no-player
   pieces re-owned, new homes). Only a restore of a building (not furniture) the engine says is for sale; otherwise the owner write
   alone. */
struct BuildOwnRsPlan { int worldSide; int heldFirst; int teardown; };
inline BuildOwnRsPlan BuildOwnRestorePlanOf(int act, int furniture, int forSale)
{
    BuildOwnRsPlan pl; pl.worldSide = 0; pl.heldFirst = 0; pl.teardown = 0;
    if (act != kBuildOwnRsRestore || furniture != 0 || forSale != 1) return pl;
    pl.worldSide = 1; pl.heldFirst = 1; pl.teardown = 1;
    return pl;
}
/* What a present act writes. A world node's saved row is only retired from this player's records (pp.build): no tombstone (the
   next load would take the rock down here) and no REMOVE (the other game would dismantle its own copy of the rock - a REMOVE
   zeroes its materials). The other acts take the PLACE's nonce or register the live piece, and never write either. */
struct BuildRsPresentPlan { int retireRow; int tombstone; int sendRemove; int registerLive; int takeNonce; };
inline BuildRsPresentPlan BuildRsPresentPlanOf(int act)
{
    BuildRsPresentPlan p;
    p.retireRow = 0; p.tombstone = 0; p.sendRemove = 0; p.registerLive = 0; p.takeNonce = 0;
    if (act == kBuildRsPresentDropWorld) p.retireRow = 1;
    else if (act == kBuildRsPresentRegister) p.registerLive = 1;
    else if (act == kBuildRsPresentNonce) p.takeNonce = 1;
    return p;
}
/* T-274 (t274-zone-scan.md S3): the per-zone scan's table - one byte per map sector (index sx * 64 + sy, 64 x 64), 1 = that sector's
   zone was walked in this world while it stayed active. The plugin passes the active sectors (ActiveZoneSectors) and each one's engine
   "loaded" byte (ZoneMap+0xB1: 1 loaded, 0 not yet, -1 the read was refused). A poll of the engine's own flag each tick, no timer. */
const int kBuildZoneSectorSide = 64;
const int kBuildZoneSectorCells = 64 * 64;
inline int BuildZoneSectorIndex(int sx, int sy)   /* -1 off the map */
{
    return (sx < 0 || sx >= kBuildZoneSectorSide || sy < 0 || sy >= kBuildZoneSectorSide) ? -1 : sx * kBuildZoneSectorSide + sy;
}
/* a sector no longer active loses its mark, so a re-arrival is walked again. `scratch` is kBuildZoneSectorCells bytes (overwritten).
   Returns the count cleared.
   T-274 fold (LOW-2): `moves` (kBuildZoneSectorCells bytes) is each sector's count of moved walks in a row; a sector that left
   loses it too. */
inline int BuildZoneScanClearGone(unsigned char* marks, unsigned char* moves, unsigned char* scratch, const int* sx, const int* sy, int n)
{
    for (int c = 0; c < kBuildZoneSectorCells; ++c) scratch[c] = 0;
    for (int i = 0; i < n; ++i) { const int c = BuildZoneSectorIndex(sx[i], sy[i]); if (c >= 0) scratch[c] = 1; }
    int cleared = 0;
    for (int c = 0; c < kBuildZoneSectorCells; ++c)
    {
        if (scratch[c] != 0) continue;
        moves[c] = 0;
        if (marks[c] != 0) { marks[c] = 0; ++cleared; }
    }
    return cleared;
}
/* the ONE zone walked this tick: an active sector that is unmarked and whose loaded byte reads 1 (0 = still loading, -1 = refused:
   neither is walked, both are asked again next tick). T-274 fold (LOW-2): round-robin - the search starts at the entry after the
   sector picked last (`lastCell`, its BuildZoneSectorIndex; -1 or no longer active = from the front) and wraps, so a zone whose
   walk keeps moving does not hold every later zone back. Returns its index in the active list, -1 = none. */
inline int BuildZoneScanPick(const unsigned char* marks, const int* sx, const int* sy, const int* loaded, int n, int lastCell)
{
    int start = 0;
    for (int i = 0; i < n && lastCell >= 0; ++i)
        if (BuildZoneSectorIndex(sx[i], sy[i]) == lastCell) { start = i + 1; break; }
    for (int j = 0; j < n; ++j)
    {
        const int i = (start + j) % n;
        const int c = BuildZoneSectorIndex(sx[i], sy[i]);
        if (c >= 0 && marks[c] == 0 && loaded[i] == 1) return i;
    }
    return -1;
}
/* T-274 fold (LOW-2): a zone whose walk moved this many times in a row is marked anyway (given up) */
const int kBuildZoneScanGiveUp = 8;
/* after the walk: marked, unless the zone list moved under it (BoxWalkGen) - then it stays unmarked and is picked again on its turn.
   T-274 fold (LOW-2): the kBuildZoneScanGiveUp-th moved walk in a row marks it anyway. Returns 1 = given up (marked after moved
   walks), 0 = otherwise (marked by a whole walk, left unmarked, or off the map). */
inline int BuildZoneScanDone(unsigned char* marks, unsigned char* moves, int sx, int sy, int walkMoved)
{
    const int c = BuildZoneSectorIndex(sx, sy);
    if (c < 0) return 0;
    if (walkMoved == 0) { marks[c] = 1; moves[c] = 0; return 0; }
    if (moves[c] < 255) ++moves[c];
    if (moves[c] < kBuildZoneScanGiveUp) return 0;
    marks[c] = 1; moves[c] = 0;
    return 1;
}
/* help1 fold 5 (T-269): an own piece owes one real STATE once its PLACE went out - the other game's copy is made from the PLACE, and
   help on it waits for a real STATE (its tail). 1 = it goes now (a copy, a not-own row or a piece whose PLACE did not go never) */
inline int BuildOwnStateAnnounce(int own, int copyHere, int sentOk, int owed)
{
    return (own != 0 && copyHere == 0 && sentOk != 0 && owed != 0) ? 1 : 0;
}
/* help1 fold 4: work carrying a placement nonce for an own row whose nonce is not known yet (0) is HELD - re-judged at the next safe
   point, never answered GONE against a nonce the owner cannot compare. 1 = hold */
inline int BuildHelpNonceWait(int rowExists, int own, int copyHere, unsigned int rowNonce, unsigned int workNonce)
{
    return (rowExists != 0 && own != 0 && copyHere == 0 && rowNonce == 0 && workNonce != 0) ? 1 : 0;
}
/* help1 fold 4: the confirmation rows pp.build brought back (with their PLACE's nonce) - READ only for their own placement; WRITTEN back
   into pp.build also while the row's nonce is still 0 (not learned yet), so they are never erased before it is */
inline int BuildAckRestoreFor(unsigned int rowNonce, unsigned int restoreNonce, int forWrite)
{
    return (rowNonce == restoreNonce || (forWrite != 0 && rowNonce == 0)) ? 1 : 0;
}
/* help1 fold 4 (re-check #2, #3): how the owner answers work for a placement that is not (or no longer) its live own piece.
   record: 0 no (key, nonce) gone record, 1 its record has landed, 2 not on disk yet. samePlRow: this placement's own row is still here;
   rowAcks: it holds helper rows; rowSettled: its landed rows equal its live ones and no write is pending (nothing applied past what
   pp.build holds); rowWriting: a pp.build write holding them is on its way.
   ANSWER = HELP_GONE from the record (or, no record kept at the cap, from the row's landed rows - only when settled, never from landed
   alone); WAIT = kept, retried; DROP = not answered, not confirmed - the helper keeps it owed and sends it again (counted);
   ZERO = GONE applied 0, only where it is proven: this placement's own row is here (its nonce known) and holds no helper row - nothing
   from any helper was applied to it. A key with no record for the nonce (another placement's row, a swept mark) is never ZERO. */
const int kBuildGoneAnswer = 0, kBuildGoneWait = 1, kBuildGoneDrop = 2, kBuildGoneZero = 3;
inline int BuildHelpGoneVerdict(int record, int samePlRow, int rowAcks, int rowSettled, int rowWriting)
{
    if (record == 1) return kBuildGoneAnswer;
    if (record == 2) return kBuildGoneWait;
    if (samePlRow == 0) return kBuildGoneDrop;
    if (rowAcks == 0) return kBuildGoneZero;
    if (rowSettled != 0) return kBuildGoneAnswer;
    return (rowWriting != 0) ? kBuildGoneWait : kBuildGoneDrop;
}
/* help1 fold 4 (re-check #5): a piece registered over a key a COPY row holds stays that copy (build.cpp BdOnNewPiece) - it keeps the
   copy's nonce (the owner's placement, which this game's help rows and entries are bound to; 0 stays 0 so the owner's PLACE can still
   teach it, BuildAdoptNonce); only a piece that is not a copy draws a fresh one */
inline unsigned int BuildNewPieceNonce(int wasCopy, unsigned int rowNonce, unsigned int fresh) { return (wasCopy != 0) ? rowNonce : fresh; }
/* help1 fold 4 (leftover 3): a replaced placement's helper row moves to the replaced rows when it still owes work OR a refund of that
   placement is still due (the inventory refused it) - then one empty entry goes with it, so the owner's HELP_GONE for that placement
   (which carries its refused tail) comes back. 1 = keep */
inline int BuildHelpOldKeep(size_t owed, int refundDue) { return (owed != 0 || refundDue != 0) ? 1 : 0; }
/* help1 fold (review MED 5/6): the helper's next seq = the highest of its own, its records' and the owner's last applied + 1 (the
   owner's STATE tail, read before the first entry of a session) - a fresh row never re-uses a seq the owner applied */
inline unsigned int BuildHelpStartSeq(unsigned int local, unsigned int recorded, unsigned int lastApplied)
{
    unsigned int s = (local > recorded) ? local : recorded;
    if (lastApplied + 1 > s) s = lastApplied + 1;
    return s == 0 ? 1 : s;
}
/* help1 fold (review LOW 13): the frequent help lines (work sent / owed, copy capped, predicted) - the first 10, then every 30th */
inline bool BuildHelpFreqLine(long long n) { return n >= 1 && (n <= 10 || n % 30 == 0); }
/* the owner: a material delta onto the piece, capped at the material's total (total < 0 = unread: no cap). *excess = the part that did
   not fit - refunded to the helper, never lost */
inline float BuildHelpMatApply(float cur, float d, float total, float* excess)
{
    if (cur < 0.0f) cur = 0.0f;
    if (d < 0.0f) d = 0.0f;
    *excess = 0.0f;
    if (total < 0.0f) return cur + d;
    float room = total - cur;
    if (room < 0.0f) room = 0.0f;
    const float taken = (d < room) ? d : room;
    *excess = d - taken;
    return cur + taken;
}
/* the owner: the work completes the piece (the engine's own vt+0x230(FLT_MAX), never a progress write at needed) */
inline int BuildHelpCompletes(float progress, float dProgress, float needed) { return (needed > 0.0f && progress + dProgress >= needed) ? 1 : 0; }
/* the helper: its copy never completes by its own work - capped at 0.999 x needed; never on a ruin (the reset reads needed) */
inline float BuildHelpCopyCap(float progress, float needed, int ruin, int* capped)
{
    *capped = 0;
    if (ruin != 0 || !(needed > 0.0f)) return progress;
    const float cap = needed * 0.999f;
    if (progress > cap) { *capped = 1; return cap; }
    return progress;
}
/* T-23: 1 = a step on another player's piece is help (needed raised for the step, the copy capped, the work sent to the piece's writer);
   0 on the piece's writer (its owner's game away, this game the area's holder) - the step is the piece's own there and finishes it */
inline int BuildHelpCapsCopy(int writerHere) { return writerHere != 0 ? 0 : 1; }
/* T-23 fix: a copy that already shows its piece's record (the record apply leaves it standing and never reaches the STATE road) takes
   the help numbers the record's STATE carries - the owner's last applied work from this game, landed and live, the fact a real STATE
   gives - when this session has not read them yet: 1 = take them. A copy row only (never an own piece), the record standing (no
   removal mark) with a STATE that decoded, of the copy's placement (or one of the two numbers unknown) */
/* T-1408 fold 12 (F1383): a copy that already read its help numbers takes a record's again when the record names newer ones for this
   game (seq or live above the known) - a confirmation the owner wrote after this game's first look. 1 = take them (T2589: the record
   carrying seq 1 reached a copy that already showed its state and knew seq 0, and was left with the confirmation unread) */
inline int BuildHelpTailRecordNew(int tailKnown, unsigned int recSeq, unsigned int recLive, unsigned int knownSeq, unsigned int knownLive)
{
    if (tailKnown == 0) return 1;
    return (recSeq > knownSeq || recLive > knownLive) ? 1 : 0;
}
/* T-1408 fold 12b (F1384 NIT 1): a help number taken from a record over one already known - the larger, so neither the landed seq nor
   the live one ever goes back (a record with a newer seq and an older live keeps the known live) */
inline unsigned int BuildHelpTailKeepMax(int known, unsigned int fromRecord, unsigned int knownVal)
{
    return (known != 0 && knownVal > fromRecord) ? knownVal : fromRecord;
}
inline int BuildHelpTailFromRecord(int copyRow, int own, int removed, int body, int tailKnown, unsigned int recPlacement, unsigned int rowPlacement)
{
    if (copyRow == 0 || own != 0 || removed != 0 || body == 0 || tailKnown != 0) return 0;
    if (recPlacement != 0u && rowPlacement != 0u && recPlacement != rowPlacement) return 0;
    return 1;
}
/* T-23 fix: this game became the writer of a piece it was helping build, with work its copy does not show because of the help cap
   (overCap: the work recorded above the copy's cap since the owner's last STATE on it). 1 = the copy's progress and that work reach
   needed - the piece completes through the engine's own complete-now path before its record is written. A copy below its cap shows
   all of its work (an overCap is then stale and never counted); never on a finished piece, a ruin or an unread needed */
inline int BuildHelpRetireCompletes(float copyP, float needed, float overCap, int complete, int ruin)
{
    if (complete != 0 || ruin != 0 || !(needed > 0.0f) || !(overCap > 0.0f)) return 0;
    if (copyP < needed * 0.999f * 0.9995f) return 0;   /* below its cap (BuildHelpCopyCap): every step shows on the copy */
    return (copyP + overCap >= needed) ? 1 : 0;
}
/* T-23 review fold 4: the work one step put above the copy's cap - only the new extra: from the copy's progress before the step when
   that already stood above the cap (the cap's write skipped - no update address, a dismantled piece), else from the cap */
inline float BuildHelpCapExtra(float p0, float p1, float capTo)
{
    if (!BuildFloatOk(p0) || !BuildFloatOk(p1) || !BuildFloatOk(capTo)) return 0.0f;
    const float from = (p0 > capTo) ? p0 : capTo;
    return (p1 > from) ? p1 - from : 0.0f;
}
/* T-23 review fold 2: the work over the copy's cap at the retire, decided from the saved help work: the owner's progress in the last
   STATE (or record) the copy took (baseP, baseKnown) plus this game's help the owner has not applied (savedP: the pending work and the
   owed entries not applied - the owed file keeps them over a reload) beyond what the copy shows (copyP). The in-memory count (memOver,
   0 on a re-made copy) stands when larger, or when no base is known or the saved work is none */
inline float BuildHelpRetireOver(float memOver, int baseKnown, float baseP, float savedP, float copyP)
{
    const float o = (BuildFloatOk(memOver) && memOver > 0.0f) ? memOver : 0.0f;
    if (baseKnown == 0 || !BuildFloatOk(baseP) || !BuildFloatOk(savedP) || !BuildFloatOk(copyP) || !(savedP > 0.0f)) return o;
    const float d = baseP + savedP - copyP;
    return (d > o && d <= kBuildHelpMax) ? d : o;
}
/* T-23 review fold 3: a retire whose completion did not finish the piece keeps the work and tries again (as a HELP the owner's game
   could not apply is sent again) - at most kBuildHelpRetireTries times, then the work retires as the copy shows it. 1 = try again */
const int kBuildHelpRetireTries = 5;
inline int BuildHelpRetireRetry(int fails) { return (fails < kBuildHelpRetireTries) ? 1 : 0; }
/* the helper: whole items owed back = the owner's cumulative refused amount (whole units) less those already returned */
inline int BuildHelpRefundOwed(float excessTotal, int refunded)
{
    if (!(excessTotal > 0.0f) || excessTotal > kBuildHelpMax * 16.0f) return 0;
    const int whole = (int)(excessTotal + 0.001f);
    return whole > refunded ? whole - refunded : 0;
}
/* the helper resumes its sequence past the one the owner confirmed (a lost owed file never re-uses a confirmed seq) */
inline unsigned int BuildHelpNextSeq(unsigned int next, unsigned int confirmed) { return (confirmed >= next) ? confirmed + 1 : next; }
/* the helper's owed work: one entry per HELP_WORK, kept until the owner's STATE confirms its seq */
struct BuildHelpEntry
{
    unsigned int seq;
    float dP;
    unsigned char reset, n;
    int sent;               /* the build's own: went on the link (not kept in the file - every entry is re-sent at link-up) */
    unsigned int sentAt;    /* the build's own: GetTickCount of the send (an unconfirmed entry is re-sent after a while) */
    int loaded;             /* help1 fold 2 (MED 4): the build's own - read from the owed file (it may have gone before the restart): FROZEN, never joined */
    unsigned int nonce;     /* help1 fold 3 (finding 2): the owner's placement the work was done on (the copy's PLACE nonce; on the wire and in the owed file) */
    int applied;            /* help1 fold 3 (finding 1): the build's own - the owner's STATE says it applied this seq (live) but its record has not landed:
                               kept, never renumbered, not re-sent */
    float d[kBuildMaxMats];
    BuildHelpEntry() : seq(0), dP(0.0f), reset(0), n(0), sent(0), sentAt(0), loaded(0), nonce(0), applied(0) { for (unsigned int i = 0; i < kBuildMaxMats; ++i) d[i] = 0.0f; }
};
/* the confirmation of seq drops every entry at or below it. Returns how many went */
inline size_t BuildHelpConfirm(std::vector<BuildHelpEntry>* owed, unsigned int seq)
{
    size_t gone = 0;
    for (size_t i = 0; i < owed->size(); )
    {
        if ((*owed)[i].seq <= seq) { owed->erase(owed->begin() + (std::vector<BuildHelpEntry>::difference_type)i); ++gone; }
        else ++i;
    }
    return gone;
}
/* help1 fold (review MED 3): HELP_GONE(seq) - the helper's own delivered amounts of every entry at or below seq ADD into back[] (the
   materials it hands back; *n = the widest). Returns how many entries that is (BuildHelpConfirm drops them once handed back).
   help1 fold 2 (re-check HIGH 2): only entries ABOVE the owner's last applied (`applied`, carried by the GONE) - those at or below it
   were applied to the piece before it went and are dropped as applied, never handed back */
inline size_t BuildHelpGoneSum(const std::vector<BuildHelpEntry>& owed, unsigned int seq, unsigned int applied, float* back, int* n)
{
    size_t k = 0;
    for (size_t i = 0; i < owed.size(); ++i)
    {
        if (owed[i].seq > seq || owed[i].seq <= applied) continue;
        ++k;
        if ((int)owed[i].n > *n) *n = (int)owed[i].n;
        for (unsigned int j = 0; j < owed[i].n && j < kBuildMaxMats; ++j) back[j] += owed[i].d[j];
    }
    return k;
}
/* help1 fold 2 (HIGH 2): the entries a HELP_GONE settles - every one at or below the higher of its seq and the owner's last applied */
inline unsigned int BuildHelpGoneSettled(unsigned int seq, unsigned int applied) { return (applied > seq) ? applied : seq; }
/* help1 fold 4 (leftover 6): a HELP_GONE refund the inventory refused is not lost - the whole units not given become one more entry of
   that placement (a seq above every one sent, so above the owner's applied), sent again, answered GONE again and handed back then: the
   STATE path's "the next one tries again". given[i]: 1 given, 0 refused (carried), < 0 the engine's add faulted (never retried).
   1 = an entry was made */
inline int BuildHelpGoneCarry(const int* whole, const int* given, unsigned int seq, unsigned int nonce, BuildHelpEntry* e)
{
    BuildHelpEntry c;
    c.seq = seq; c.nonce = nonce;
    int any = 0;
    for (int i = 0; i < (int)kBuildMaxMats; ++i)
    {
        if (whole[i] <= 0 || given[i] != 0) continue;
        c.d[i] = ((float)whole[i] > kBuildHelpMax) ? kBuildHelpMax : (float)whole[i];
        c.n = (unsigned char)(i + 1);
        any = 1;
    }
    if (any != 0) *e = c;
    return any;
}
inline bool BuildHelpEntrySeqLess(const BuildHelpEntry& a, const BuildHelpEntry& b) { return a.seq < b.seq; }
/* help1 fold 2 (re-check HIGH 1): the owner applies strictly in order, so nothing above its last applied was applied; a gap no entry
   fills (pp.help's next landed ahead of the owed file before a crash) would be held forever - those entries are RENUMBERED (content
   unchanged, seq order kept). The owner drops what it held under the old numbers (BuildHelpSuperseded).
   help1 fold 3 (finding 1): the STATE's tail carries BOTH the landed last applied (on the owner's disk) and the live one (>= landed).
   Entries at or below landed are confirmed (dropped); those in (landed, live] WERE applied - kept (their landed confirmation follows),
   marked applied (never re-sent), never renumbered; entries above live are unmarked, and if the lowest of them is not live + 1 they are
   renumbered live+1, live+2, ... and *next follows them (it never falls to or below live). With no entry above live *next = the higher
   of itself and live + 1 - never lowered. Returns how many entries took a new seq (0 = no gap). */
inline size_t BuildHelpResync(std::vector<BuildHelpEntry>* owed, unsigned int* next, unsigned int landed, unsigned int live)
{
    if (live < landed) live = landed;
    BuildHelpConfirm(owed, landed);
    std::sort(owed->begin(), owed->end(), BuildHelpEntrySeqLess);
    size_t first = owed->size();
    for (size_t i = 0; i < owed->size(); ++i)
    {
        (*owed)[i].applied = ((*owed)[i].seq <= live) ? 1 : 0;
        if (first == owed->size() && (*owed)[i].seq > live) first = i;
    }
    if (first == owed->size()) { if (*next < live + 1) *next = live + 1; return 0; }
    if ((*owed)[first].seq == live + 1)
    {
        const unsigned int top = owed->back().seq + 1;
        if (*next < top) *next = top;
        return 0;
    }
    size_t k = 0;
    for (size_t i = first; i < owed->size(); ++i)
    {
        const unsigned int s = live + 1 + (unsigned int)(i - first);
        if ((*owed)[i].seq != s) { (*owed)[i].seq = s; ++k; }
    }
    *next = live + 1 + (unsigned int)(owed->size() - first);
    return k;
}
/* help1 fold 2 (re-check MED 4): new work joins only an entry never on the link and not read back from the owed file */
inline bool BuildHelpJoinable(const BuildHelpEntry& e) { return e.sent == 0 && e.loaded == 0; }
/* help1 fold 2 (HIGH 1), the owner: an entry queued EARLIER (held) from the same helper for the same key whose seq is at or above a
   later arrival's is superseded - the helper renumbered or re-sent from lower; it re-sends whatever it still owes */
inline bool BuildHelpSuperseded(unsigned int earlierSeq, unsigned int laterSeq) { return earlierSeq >= laterSeq; }
/* help1 fold 2 (HIGH 1): an entry that would not encode is made encodable, never erased (its seq would leave a gap): a non-finite or
   negative amount -> 0, one past kBuildHelpMax -> kBuildHelpMax, n past kBuildMaxMats -> kBuildMaxMats, reset past 1 -> 1. Returns how
   many fields changed */
inline int BuildHelpEntrySanitize(BuildHelpEntry* e)
{
    int k = 0;
    if (e->n > kBuildMaxMats) { e->n = (unsigned char)kBuildMaxMats; ++k; }
    if (e->reset > 1) { e->reset = 1; ++k; }
    if (!BuildFloatOk(e->dP) || e->dP < 0.0f) { e->dP = 0.0f; ++k; }
    else if (e->dP > kBuildHelpMax) { e->dP = kBuildHelpMax; ++k; }
    for (unsigned int i = 0; i < kBuildMaxMats; ++i)
    {
        if (!BuildFloatOk(e->d[i]) || e->d[i] < 0.0f) { e->d[i] = 0.0f; ++k; }
        else if (e->d[i] > kBuildHelpMax) { e->d[i] = kBuildHelpMax; ++k; }
    }
    return k;
}
/* help1 fold 2 (re-check HIGH 1): the helper's row for a key names the placement it helped (the owner's nonce); a PLACE of ANOTHER
   placement at that key (both known, different) makes the row stale. help1 fold 3 (finding 2): its entries stay owed to THAT placement
   (moved aside under its nonce, settled by its HELP_GONE); the key's row starts empty for the new one */
inline bool BuildHelpRowStale(unsigned int rowNonce, unsigned int pieceNonce) { return rowNonce != 0 && pieceNonce != 0 && rowNonce != pieceNonce; }
/* help1 fold 3 (finding 6): pp.help's goneSeq floor (entries at or below it were settled by a HELP_GONE) joins a row only when both
   nonces are known and equal - never an unknown placement's floor onto another's entries */
inline bool BuildHelpFloorApplies(unsigned int rowNonce, unsigned int recNonce) { return rowNonce != 0 && recNonce != 0 && rowNonce == recNonce; }
/* a piece's kind for the wall help rules: kBuildWallKindNone (not a wall), kBuildWallKindOwnRecord (a wall holding its own progress
   record, ownRecord > 0 - BuildWallKeepsOwnRecord), kBuildWallKindGroup (a wall whose progress record is a group's, or unreadable).
   The values are kBuildRepairWall*'s. */
const int kBuildWallKindNone = 0, kBuildWallKindGroup = 1, kBuildWallKindOwnRecord = 2;
inline int BuildWallHelpKind(int wall, int ownRecord)
{
    if (wall == 0) return kBuildWallKindNone;
    return ownRecord > 0 ? kBuildWallKindOwnRecord : kBuildWallKindGroup;
}
/* T-1408 (F1216 design read): kBuildWallKindGroupOwner - a wall copy whose record is a group's (ownRecord 0, not unreadable), not a
   ruin, whose group's lead is a wall of the SAME owner (the lead itself, or the wall whose record it holds). The helper's work goes out
   under the key its hook sees (the engine's wall step 0x5594A0 forwards a member's step to its lead, so that is its lead copy) and the
   owner's game resolves its own group (its own wall step on its piece of that key). A group of mixed owners, a ruin or an unreadable
   record stays kBuildWallKindGroup (refused). */
const int kBuildWallKindGroupOwner = 3;
inline int BuildWallHelpKindG(int wall, int ownRecord, int leadSameOwner, int ruin)
{
    const int k = BuildWallHelpKind(wall, ownRecord);
    return (k == kBuildWallKindGroup && ownRecord == 0 && leadSameOwner != 0 && ruin == 0) ? kBuildWallKindGroupOwner : k;
}
/* a build / add-materials order on a copy owned by another player's stand-in is help (1) on EVERY order road (click, commit, Shift
   job, on the main thread or off it) - a wall copy holding its own progress record too: its work reaches the owner's wall as help work
   (F1011). A wall copy whose record is a group's, or unreadable (wallKind kBuildWallKindGroup), is refused (2): its work lands in the
   group's shared record under the lead's key, which is not shown to reach the owner's wall. Anything else 0 */
const int kBuildHelpOrderNone = 0, kBuildHelpOrderGiven = 1, kBuildHelpOrderWallRefused = 2;
inline int BuildHelpOrderVerdict(int task, int standIn, int wallKind)
{
    if (task != kBuildTaskBuild && task != kBuildTaskAddMaterials) return kBuildHelpOrderNone;
    if (standIn == 0) return kBuildHelpOrderNone;
    return (wallKind == kBuildWallKindGroup) ? kBuildHelpOrderWallRefused : kBuildHelpOrderGiven;
}
/* T-1408: the order verdict with the owner's presence - a copy of a group that is the owner's alone (kBuildWallKindGroupOwner) is help
   only while the owner's game is in the world (ownerInWorld 1): the owner's game resolves its own group, so with it away the work is
   refused as any grouped wall copy's. Every other kind: BuildHelpOrderVerdict. */
inline int BuildHelpOrderVerdictW(int task, int standIn, int wallKind, int ownerInWorld)
{
    const int v = BuildHelpOrderVerdict(task, standIn, wallKind);
    if (v == kBuildHelpOrderGiven && wallKind == kBuildWallKindGroupOwner && ownerInWorld != 1) return kBuildHelpOrderWallRefused;
    return v;
}
/* T-1408: one group member's share of its group's progress as the engine's wall step draws it after each step (0x5594A0, Read: the lead's
   progress +0x164 over its needed +0x188, at most kBuildWallShareCap (DAT_14167B308 - Inferred 1.0), times the member's own needed
   +0x188, written through the member's vt+0x238). -1 = the lead's needed is not positive or a value is not finite */
const float kBuildWallShareCap = 1.0f;
inline float BuildWallMemberShare(float leadP, float leadN, float memberN)
{
    if (!BuildFloatOk(leadP) || !BuildFloatOk(leadN) || !BuildFloatOk(memberN) || !(leadN > 0.0f)) return -1.0f;
    float r = leadP / leadN;
    if (r > kBuildWallShareCap) r = kBuildWallShareCap;
    return r * memberN;
}
/* T-1408: the owner's group is WHOLE for the owner's wall step when its lead lists at least one entry and every entry resolves to a loaded
   piece here (the lead itself counted when it is listed): the engine's finish 0x561A20 and its share loop skip an entry that does not
   resolve, so the helper's work goes onto a group only when all of it is here - otherwise materials only, with a line */
inline int BuildWallGroupWhole(int leadCount, int resolved) { return (leadCount >= 1 && resolved == leadCount) ? 1 : 0; }
/* T-1408 fold 1 (review 2): a group is its owner's ALONE when its lead is the piece's owner's and every loaded member reads and has that
   same owner (membersRead 1 = every listed entry that resolved was read; membersOther = how many of them had another owner) - the helper's
   verdict and the owner's apply both ask it */
inline int BuildWallGroupOneOwner(int leadSame, int membersRead, int membersOther) { return (leadSame != 0 && membersRead != 0 && membersOther == 0) ? 1 : 0; }
/* T-1408 fold 1 (review 1): a running help job on a grouped wall copy is ENDED (the road that ends a job the help rule refuses) when its
   step is refused for good: a group not its owner's alone (kBuildWallKindGroup), or a group the owner's alone while the owner's game is
   KNOWN away (ownerInWorld 0, a fresh answer). Not known (-1: no fresh answer) never ends a job - the step refusal still guards the work */
inline int BuildHelpWallJobEnd(int wallKind, int ownerInWorld)
{
    if (wallKind == kBuildWallKindGroup) return 1;
    return (wallKind == kBuildWallKindGroupOwner && ownerInWorld == 0) ? 1 : 0;
}
/* T-1408 fold 1 (review 3, 4; manager's decision 4: the owner resolves its own group): help work carrying build progress for a grouped
   wall here. PLAIN - not a grouped wall, or no progress: the ordinary road. HOLD - kept and judged again later, nothing applied, nothing
   confirmed (as the 'area not loaded' wait): this game is not the owner's own game (the T-23 writer of a copy), or the group is the
   owner's alone but not wholly loaded here. MATS - materials only, confirmed: the group is not its owner's alone (or the scale did not
   read). STEP - the owner's wall step on its whole group. */
const int kBuildWallApplyPlain = 0, kBuildWallApplyStep = 1, kBuildWallApplyHold = 2, kBuildWallApplyMats = 3;
inline int BuildWallGroupApply(int grouped, int hasProgress, int ownerGame, int oneOwner, int whole)
{
    if (grouped == 0 || hasProgress == 0) return kBuildWallApplyPlain;
    if (ownerGame == 0) return kBuildWallApplyHold;
    if (oneOwner == 0) return kBuildWallApplyMats;
    return whole != 0 ? kBuildWallApplyStep : kBuildWallApplyHold;
}
/* T-1408 fold 1 (review 5): the helper measured its work on its lead copy of THIS key, against that piece's own needed; on the owner's game
   the key's piece may be a member of a group whose lead is another wall type. The work as the same fraction of the owner's lead's needed:
   dP x leadNeeded / pieceNeeded (1:1 when the piece is the lead). -1 = a value is not finite or a needed is not positive */
inline float BuildWallHelpScale(float dP, float leadNeeded, float pieceNeeded)
{
    if (!BuildFloatOk(dP) || !BuildFloatOk(leadNeeded) || !BuildFloatOk(pieceNeeded) || !(leadNeeded > 0.0f) || !(pieceNeeded > 0.0f)) return -1.0f;
    return dP * (leadNeeded / pieceNeeded);
}
/* T-1408 fold 2: the helper's step on its grouped wall copy (the lead copy: the engine forwards a member's step to it). RUN - the owner's
   group, the owner's game in the world, not this game the copy's writer, not told HELD. REFUSE (noted: the job road ends the job) - a
   group not its owner's alone, the owner's game known away, this game the copy's writer, or the holding game's HELP_HELD. WAIT - the
   owner's game's status not known (-1, a load): the step is skipped (no work counted) and the job is NOT ended - no stop / restart churn */
const int kBuildWallStepRun = 0, kBuildWallStepWait = 1, kBuildWallStepRefuse = 2;
inline int BuildHelpWallStep(int wallKind, int ownerInWorld, int writerHere, int ownerHeld)
{
    if (wallKind != kBuildWallKindGroupOwner) return kBuildWallStepRefuse;
    if (ownerInWorld == 0 || ownerHeld != 0) return kBuildWallStepRefuse;
    if (ownerInWorld != 1) return kBuildWallStepWait;
    return writerHere != 0 ? kBuildWallStepRefuse : kBuildWallStepRun;
}
/* T-1408 fold 2: the job road's end - fold 1's rule, and a group the owner's alone whose help work the holding game told HELD */
inline int BuildHelpWallJobEndH(int wallKind, int ownerInWorld, int ownerHeld)
{
    if (BuildHelpWallJobEnd(wallKind, ownerInWorld) != 0) return 1;
    return (wallKind == kBuildWallKindGroupOwner && ownerHeld != 0) ? 1 : 0;
}
/* T-1408 fold 2: a HELD grouped-wall entry is PARKED - judged again only on an event that can change the verdict, never on a clock:
   kBuildWakeAreaLaid (an area's pieces laid here - the zone walk finished: a member may have loaded), kBuildWakeAreaLeft (an area left
   here: its piece may be gone), kBuildWakeMap (an area map arrived: the copy's writer may have changed - wakes only entries held as the
   copy's writer) */
const int kBuildWakeAreaLaid = 1, kBuildWakeAreaLeft = 2, kBuildWakeMap = 3;
inline int BuildWallHeldWakes(int why, int event)
{
    if (why != (int)kBuildHeldNotWhole && why != (int)kBuildHeldWriter) return 0;
    if (event == kBuildWakeMap) return why == (int)kBuildHeldWriter ? 1 : 0;
    return (event == kBuildWakeAreaLaid || event == kBuildWakeAreaLeft) ? 1 : 0;
}
/* T-1408 fold 2: a parked entry whose piece is not found here any more (its area unloaded) is DROPPED - not confirmed, nothing applied,
   the helper keeps its materials owed and sends the work again; an entry never held keeps the 'area not loaded' wait */
const int kBuildHeldJudge = 0, kBuildHeldDrop = 1, kBuildHeldWaitArea = 2;
inline int BuildWallHeldPieceGone(int parked, int pieceHere)
{
    if (pieceHere != 0) return kBuildHeldJudge;
    return parked != 0 ? kBuildHeldDrop : kBuildHeldWaitArea;
}
/* T-1408 fold 3: a copy of a HELD entry that replaces it (the helper's 30 s re-send of unconfirmed work, or a later seq superseding it)
   inherits its held state - parked, why, told (HELP_HELD sent) and the HOLD line written - so that copy's later apply / drop sends
   'released' (the helper's mark would otherwise never clear and its builders refuse that wall group for the session), and a re-send
   neither sends HELD again nor re-writes the HOLD line. A never-held entry passes nothing on; an unsent HELD stays unsent (sent at the
   next judgement) */
struct BuildWallHeldCarry { int park; int why; int told; int logged; };
inline void BuildWallHeldInherit(BuildWallHeldCarry* to, const BuildWallHeldCarry& from)
{
    if (to == 0 || from.park == 0) return;
    to->park = 1; to->why = from.why;
    if (from.told != 0) to->told = 1;
    if (from.logged != 0) to->logged = 1;
}
/* T-1408 fold 3: an entry leaving the queue (applied, confirmed or dropped) releases its helper when it was parked and HELD was sent */
inline int BuildWallHeldReleaseOnLeave(int park, int told) { return (park != 0 && told != 0) ? 1 : 0; }
/* T-1408 fold 3: a re-send of a PARKED entry (waiting for an event) replaces its content and stays parked - a re-send is not an event
   that can change the verdict (no 30 s clock in disguise); an entry not parked is judged at the next safe point as before */
inline int BuildWallHeldResendParked(int park, int waitingForEvent) { return (park != 0 && waitingForEvent != 0) ? 1 : 0; }
/* T-1408 fold 2: the helper's HELD mark is kept until released, or until the holding game is KNOWN away (0; -1 unknown keeps it) */
inline int BuildWallHeldMarkKeep(int holderInWorld) { return holderInWorld == 0 ? 0 : 1; }
/* T-903 (decisions 920 / 920 a, owner 2026-10-17): who may help build, add materials to and repair another player's piece - a
   faction-mate (the two players share a team), or a player the piece's owner has set FRIENDLY with the PLAYERS tab's friendly /
   neutral / enemy buttons (or `relate`). kBuildHelpFriendHelps = 0 makes it faction-mates only. Dismantle is not this rule's: it is
   faction-mates only (decision 548, BuildDismantleOrderVerdict). */
const int kBuildHelpFriendHelps = 1;
inline int HelpAllowedFor(int isTeammate, int isFriendly, int friendHelps)
{
    return (isTeammate != 0 || (friendHelps != 0 && isFriendly != 0)) ? 1 : 0;
}
inline int HelpAllowed(int isTeammate, int isFriendly) { return HelpAllowedFor(isTeammate, isFriendly, kBuildHelpFriendHelps); }
/* T-903 (F1041), THE HELPER'S GAME: the help rule's answer for a running build / add-materials job on a copy another player's stand-in
   holds, as the main thread hands it to the job hooks. ALLOWED (the rule says yes), REFUSED only for a KNOWN refusal - this game's own
   slot is known and the team table is current (a faction-mate test is possible) and, while friends may help, the owner's stance towards this player has been read -
   and UNKNOWN otherwise: a job is never ended on missing knowledge (the step refusal in the progress hook still guards the work). */
const int kBuildHelpJobUnknown = 0, kBuildHelpJobAllowed = 1, kBuildHelpJobRefused = 2;
inline int BuildHelpJobVerdictFor(int meKnown, int allowed, int stanceKnown, int friendHelps)
{
    if (allowed != 0) return kBuildHelpJobAllowed;
    if (meKnown == 0) return kBuildHelpJobUnknown;
    if (friendHelps != 0 && stanceKnown == 0) return kBuildHelpJobUnknown;
    return kBuildHelpJobRefused;
}
inline int BuildHelpJobVerdict(int meKnown, int allowed, int stanceKnown) { return BuildHelpJobVerdictFor(meKnown, allowed, stanceKnown, kBuildHelpFriendHelps); }
/* T-903 (F1041), ANY THREAD: end this build / add-materials job now (1) or let the engine run its step (0)? Only a job of THIS game's
   own character (mine) on a piece a player's stand-in holds (standIn), and only on a REFUSED answer the main thread gave at most
   freshMs ago (a stale or unknown answer runs the step, as before this rule). */
inline int BuildHelpJobEnd(int standIn, int mine, int verdict, unsigned int ageMs, unsigned int freshMs)
{
    return (standIn != 0 && mine != 0 && verdict == kBuildHelpJobRefused && ageMs <= freshMs) ? 1 : 0;
}
/* T-903, THE HELPER'S GAME (MAIN THREAD, the K2 safe point): after a helper's job on a piece was ended, is this standing job of that
   helper removed? Only a help job whose target is that piece (samePiece): build, add materials, repair, or the type the engine STORES a
   kept job as - the order system's add-job (0x507C40) swaps a type its record marks 'can be a job' (+0x4) for the record's replacement
   (+0xC), and build and repair are both kept as 125 (T1376: `standingTaskTypes=125` after a build and a repair order) - plus the
   replacement and companion (+0x10) types the engine's own records name for build / add materials / repair, read live (extra, n).
   Any other job stays (hauling, operating). */
const int kBuildTaskKeptWork = 125;
inline int BuildHelpStandingDrop(int task, int samePiece, const int* extra, int extraN)
{
    if (samePiece == 0 || task <= 0) return 0;
    if (task == kBuildTaskBuild || task == kBuildTaskAddMaterials || task == kBuildTaskRepair || task == kBuildTaskKeptWork) return 1;
    for (int i = 0; i < extraN; ++i) if (extra[i] > 0 && extra[i] == task) return 1;
    return 0;
}
/* THE HELPER'S GAME, ANY THREAD: the engine's building finder for the kept build / repair job (TaskType 125) skips every building whose
   faction is not the searching character's. Is the building taken as the character's own? Equal factions: always (the engine's own
   path, kBuildJobFinderSame). Another faction: only when the character is of this game's player faction (charMine), the building's
   faction is a stand-in with a row in the table the main thread writes (standInRow), that row's help answer is ALLOWED and at most
   freshMs old, and the building is not a wall whose progress record is a group's or unreadable (wallGroup 1: help on such a wall copy
   is refused on every order road); a wall holding its own record is taken as any building.
   Unknown or stale answers, a refusal, another game's character or a building of any other faction keep the engine's skip. */
const int kBuildJobFinderSame = 0, kBuildJobFinderAllowed = 1, kBuildJobFinderNotMine = 2, kBuildJobFinderNotStandIn = 3;
const int kBuildJobFinderRefused = 4, kBuildJobFinderStale = 5, kBuildJobFinderWall = 6;
inline int BuildJobFinderVerdict(int sameFaction, int charMine, int standInRow, int verdict, unsigned int ageMs, unsigned int freshMs, int wallGroup)
{
    if (sameFaction != 0) return kBuildJobFinderSame;
    if (charMine == 0) return kBuildJobFinderNotMine;
    if (standInRow == 0) return kBuildJobFinderNotStandIn;
    if (verdict == kBuildHelpJobRefused) return kBuildJobFinderRefused;
    if (verdict != kBuildHelpJobAllowed || ageMs > freshMs) return kBuildJobFinderStale;
    if (wallGroup != 0) return kBuildJobFinderWall;
    return kBuildJobFinderAllowed;
}
/* 1 = the finder goes on as for its own building, 0 = the engine's skip */
inline int BuildJobFinderTakes(int verdict) { return (verdict == kBuildJobFinderSame || verdict == kBuildJobFinderAllowed) ? 1 : 0; }
/* T-903 re-check fold (item 2), THE OWNER'S GAME, help work about to be applied: applied (0) for a faction-mate or a player the owner
   has set friendly; REFUSED (1, refunded) only for a KNOWN non-teammate the owner has not set friendly; HELD (2, tried again at the
   next safe point) while this game's own slot is unknown (no teammate test possible) or the sender's standing is unread (no stand-in
   yet, or its relation not read) - a legitimate helper is never refused for good on missing knowledge. friendHelps 0: faction-mates
   only, so the standing is not needed. */
const int kBuildHelpApplyGo = 0, kBuildHelpApplyRefuse = 1, kBuildHelpApplyHold = 2;
inline int BuildHelpApplyVerdictFor(int meKnown, int isTeammate, int stanceKnown, int isFriendly, int friendHelps)
{
    if (meKnown == 0) return kBuildHelpApplyHold;
    if (HelpAllowedFor(isTeammate, 0, friendHelps) != 0) return kBuildHelpApplyGo;
    if (friendHelps == 0) return kBuildHelpApplyRefuse;
    if (stanceKnown == 0) return kBuildHelpApplyHold;
    return HelpAllowedFor(0, isFriendly, friendHelps) != 0 ? kBuildHelpApplyGo : kBuildHelpApplyRefuse;
}
inline int BuildHelpApplyVerdict(int meKnown, int isTeammate, int stanceKnown, int isFriendly)
{
    return BuildHelpApplyVerdictFor(meKnown, isTeammate, stanceKnown, isFriendly, kBuildHelpFriendHelps);
}
/* T-903: a COMPLETE piece that is not a ruin is damaged when its health is short of full - health is the progress field (state+4)
   against needed (state+0x28); a hit on a door or wall subtracts from progress (design p51-help1). Below 0.999 x needed counts. */
inline int BuildPieceDamaged(int complete, int destroyed, float progress, float needed)
{
    return (complete != 0 && destroyed == 0 && needed > 0.0f && progress < needed * 0.999f) ? 1 : 0;
}
/* T-903: a repair order (TaskType 95) on a copy another player's stand-in owns: on a ruin it is the P15 ruin help (1), on a complete
   damaged piece it is repair help (3) - the work reaches the owner's game as HELP_WORK. wall: kBuildRepairWallNone (not a wall),
   kBuildRepairWallGroup (a wall whose progress record is a group's, or cannot be read) - refused either way (2); kBuildRepairWallOwnRecord
   (a wall copy holding its own record) - a damaged one is repair help (3), its ruin is refused (2: the ruin road resets the owner's
   piece, which is not taken for walls). Anything else 0. */
const int kBuildRepairOrderDamagedHelp = 3;
const int kBuildRepairWallNone = 0, kBuildRepairWallGroup = 1, kBuildRepairWallOwnRecord = 2;
inline int BuildRepairHelpVerdict(int task, int standInOwned, int destroyed, int damaged, int wall)
{
    if (task != kBuildTaskRepair || standInOwned == 0 || (destroyed == 0 && damaged == 0)) return kBuildRepairOrderNone;
    if (wall == kBuildRepairWallOwnRecord && destroyed == 0) return kBuildRepairOrderDamagedHelp;
    if (wall != kBuildRepairWallNone) return kBuildRepairOrderRefused;
    return destroyed != 0 ? kBuildRepairOrderHelp : kBuildRepairOrderDamagedHelp;
}
/* THE OWNER'S GAME: whether help work's progress (build progress or repaired health) applies to this piece - any piece that is not a
   wall; a wall only when it holds its own progress record (ownRecord > 0), so the work lands on this wall alone and the helper's
   own-record copy measures against the same piece's own needed. A wall whose record is a group's (or unreadable) does not take it - a
   write there lands in every member's shared record: repair work is dropped with its materials back to the helper, build work keeps
   its materials only. */
inline int BuildRepairWallApplies(int wall, int ownRecord)
{
    return (wall == 0 || ownRecord > 0) ? 1 : 0;
}
/* a progress step on a copy a stand-in holds, by help kind: an unfinished piece - build (amount above 0) or the add-materials task's
   amount-0 call (materials); a complete ruin or complete damaged piece - repair (amount above 0); anything else -1 (a whole complete
   piece, a negative amount, an amount-0 call on a complete piece). amountSign: -1, 0 or 1. */
const int kBuildOffHelpBuild = 0, kBuildOffHelpMaterials = 1, kBuildOffHelpRepair = 2;
inline int BuildHelpOffMainKind(int complete, int destroyed, int damaged, int amountSign)
{
    if (amountSign < 0) return -1;
    if (complete == 0) return amountSign > 0 ? kBuildOffHelpBuild : kBuildOffHelpMaterials;
    if (amountSign == 0) return -1;
    return (destroyed != 0 || damaged != 0) ? kBuildOffHelpRepair : -1;
}
/* THE HELPER'S GAME: the owner's STATE on a wall copy and the repair-refused marker. A helper's own-record copy accepts its repair, while
   an owner whose wall is grouped refuses it (BuildRepairWallApplies): the materials come back and the owner's health does not rise - the
   marker is set (refundNow: this STATE handed materials back; copyComplete: the copy is complete, so the work was repair), and repair
   orders and work on that wall are refused while it is set. Any change of the owner's health (prevP -> newP, ownerKnown) clears it. */
const int kBuildWallRepHoldNone = 0, kBuildWallRepHoldSet = 1, kBuildWallRepHoldClear = 2;
inline int BuildWallRepairHoldAct(int wall, int ownRecord, int copyComplete, int refundNow, int ownerKnown, float prevP, float newP)
{
    if (wall == 0) return kBuildWallRepHoldNone;
    if (ownerKnown != 0 && newP != prevP) return kBuildWallRepHoldClear;
    if (ownRecord > 0 && copyComplete != 0 && refundNow != 0) return kBuildWallRepHoldSet;
    return kBuildWallRepHoldNone;
}
/* a bounded set of keys (the marker table): set (past cap the oldest go - returns how many), has, clear (1 = it was set) */
inline int BuildKeyMarkHas(const std::vector<std::string>& t, const std::string& k)
{
    for (size_t i = 0; i < t.size(); ++i) if (t[i] == k) return 1;
    return 0;
}
inline size_t BuildKeyMarkSet(std::vector<std::string>* t, const std::string& k, size_t cap)
{
    if (BuildKeyMarkHas(*t, k) != 0) return 0;
    if (cap == 0) return 1;
    size_t dropped = 0;
    while (t->size() >= cap) { t->erase(t->begin()); ++dropped; }
    t->push_back(k);
    return dropped;
}
inline int BuildKeyMarkClear(std::vector<std::string>* t, const std::string& k)
{
    for (size_t i = 0; i < t->size(); ++i) if ((*t)[i] == k) { t->erase(t->begin() + (std::ptrdiff_t)i); return 1; }
    return 0;
}
/* T-903, THE OWNER'S GAME: repair work (a HELP_WORK whose piece is complete here) - the piece's new health, never above needed */
inline float BuildRepairApplyTo(float p0, float dP, float needed)
{
    const float v = p0 + (dP > 0.0f ? dP : 0.0f);
    return v > needed ? needed : v;
}
/* T-903 fold (review HIGH 1), THE OWNER'S GAME: the repair health written through updatePhysicalWithProgress 0x558680, which finishes a
   piece at progress >= needed while the piece's own complete byte (+0x160) is 0 - with that byte set the health may reach needed;
   without it, it stays below 0.999 x needed (the cap every other caller of 0x558680 keeps), so the finish never runs from repair */
inline float BuildRepairFinishSafe(float to, float needed, int ownComplete)
{
    const float cap = needed * 0.999f;
    return (ownComplete == 0 && to > cap) ? cap : to;
}
/* help1, THE HELPER'S GAME: the materials a helper put into its copy, slot by slot, against the copy's baseline (the amounts after the
   mod's own last write): the baseline when known; else the amount read just before the engine's call (nBefore slots read, -1 none);
   else the amount now - no gain. A gain counts above kBuildHelpMatEps; anything else is 0. gain[0..nNow) is written; 1 = a slot gained. */
const float kBuildHelpMatEps = 0.0001f;
inline int BuildHelpMatGain(const float* now, int nNow, int baseOk, const float* base, int nBefore, const float* before, float* gain)
{
    int any = 0;
    for (int i = 0; i < nNow; ++i)
    {
        const float b = (baseOk != 0) ? base[i] : ((i < nBefore) ? before[i] : now[i]);
        gain[i] = now[i] - b;
        if (gain[i] > kBuildHelpMatEps) any = 1; else gain[i] = 0.0f;
    }
    return any;
}
/* T-903, THE HELPER'S GAME: a copy's materials looked at on the main thread after the engine's add-materials task ran off it
   (or before an owner's STATE overwrites them). GONE: no live row or piece here (removed, not found); NOT_COPY: not a copy the owner's
   stand-in holds; SKIP: a grouped wall copy (wallGroup: its record is a group's or unreadable - its work is not carried) or a
   dismantled piece; NO_BASE: the copy's baseline is not known yet - nothing
   can be measured, so nothing is taken and the baseline is left for the owner's next STATE to set; NO_GAIN: nothing above the baseline;
   RECORD: the help rule allows this player - help work for the owner; REFUSE: it does not - the materials come off the copy and the
   items go back. */
const int kBuildHarvestGone = 0, kBuildHarvestNotCopy = 1, kBuildHarvestSkip = 2, kBuildHarvestNoGain = 3, kBuildHarvestRecord = 4, kBuildHarvestRefuse = 5,
          kBuildHarvestNoBase = 6;
inline int BuildHelpHarvestVerdict(int found, int isCopy, int wallGroup, int dismantled, int baseOk, int gained, int allowed)
{
    if (found == 0) return kBuildHarvestGone;
    if (isCopy == 0) return kBuildHarvestNotCopy;
    if (wallGroup != 0 || dismantled != 0) return kBuildHarvestSkip;
    if (baseOk == 0) return kBuildHarvestNoBase;
    if (gained == 0) return kBuildHarvestNoGain;
    return allowed != 0 ? kBuildHarvestRecord : kBuildHarvestRefuse;
}
/* T-1408 fold 14 (F1429), THE HELPER'S GAME: a copy's help baseline belongs to the materials RECORD the copy reads (the engine's state
   accessor vt+0x228), not to the key asked - a grouped wall member reads and writes its lead's record (lead +0x160), so every mod write
   through a member moves the LEAD's baseline, and the lead's harvest measures that one record once. Which row holds the baseline of a
   piece: OWN (not a wall, or a wall reading its own record self + ownOff), LEAD (a wall reading another wall's record: the row of the
   piece rec - ownOff), NONE (a wall whose record does not read: no baseline is usable or written). */
const int kBuildBaseOwn = 0, kBuildBaseLead = 1, kBuildBaseNone = 2;
inline int BuildHelpBaseHolder(int wall, unsigned long long self, unsigned long long rec, unsigned long long ownOff)
{
    if (wall == 0) return kBuildBaseOwn;
    if (rec == 0 || rec < ownOff) return kBuildBaseNone;
    return (rec == self + ownOff) ? kBuildBaseOwn : kBuildBaseLead;
}
/* ... and a held baseline is usable only for the record it was read from (baseRec) while that record's group is unchanged (baseCnt: the
   holding wall's group count +0x390 when read, -1 unread, 0 not a lead / not a wall): a wall that joined a group, or left one, reads another
   record now, and a join or leave by any road (the engine's own walk too) changes the lead's count - the old baseline measures nothing
   (NO_BASE until the mod's next write sets it; a main-thread step falls back to the amounts read just before the engine's call) */
inline int BuildHelpBaseUsable(int baseOk, unsigned long long baseRec, unsigned long long recNow, int baseCnt, int cntNow)
{
    return (baseOk != 0 && recNow != 0 && baseRec == recNow && baseCnt == cntNow && cntNow >= 0) ? 1 : 0;
}
/* T-1408 fold 14d (review 6): ... and its record's join generation unchanged (baseGen: the generation when read; it only rises - every
   join the mod performs or sees on that record, the engine's own walk included, bumps it), so a leave then a join that restores the count
   never passes an old baseline */
inline int BuildHelpBaseUsable(int baseOk, unsigned long long baseRec, unsigned long long recNow, int baseCnt, int cntNow, unsigned int baseGen, unsigned int genNow)
{
    return (baseGen == genNow && BuildHelpBaseUsable(baseOk, baseRec, recNow, baseCnt, cntNow) != 0) ? 1 : 0;
}
/* T-1408 fold 14c (review 2): a mod join's effect on the lead's record carried into its baseline - base + (after - before), slot by
   slot, read just before and after the join; a helper's delivery not yet measured stays above the new baseline (never absorbed). 1 = carried
   (out[0..nAfter) written); 0 = the baseline was not usable before the join or a read failed - it stays unusable (NO_BASE), never re-read */
inline int BuildHelpBaseCarry(int usableBefore, const float* base, const float* before, int nBefore, const float* after, int nAfter, float* out)
{
    if (usableBefore == 0 || nBefore < 0 || nAfter < 0) return 0;
    for (int i = 0; i < nAfter; ++i) out[i] = (i < nBefore ? base[i] - before[i] : 0.0f) + after[i];
    return 1;
}
/* T-1408 fold 14b (review 2): where a harvest of piece b measures - on b's own row, or for a grouped member (holder LEAD) on its lead's row
   when the lead has a live row here (leadRow); a member whose lead has no row is measured nowhere (its harvest skips: a member is never
   measured on its own). 0 own row, 1 the lead's row, 2 none */
const int kBuildMeasureOwn = 0, kBuildMeasureLead = 1, kBuildMeasureNone = 2;
inline int BuildHelpMeasureAt(int holder, int leadRow)
{
    if (holder == kBuildBaseOwn) return kBuildMeasureOwn;
    return (holder == kBuildBaseLead && leadRow != 0) ? kBuildMeasureLead : kBuildMeasureNone;
}
/* help1: the whole items a material gain stands for (a refund hands back whole items; the same rounding as a HELP_GONE's) */
inline int BuildHelpWholeItems(float gain) { return gain > 0.0f ? (int)(gain + 0.001f) : 0; }
/* the unconfirmed work the helper puts back on its copy after the owner's STATE (no flicker back): *dP and d[] ADD, *n = the widest;
   help1 fold 3: not an entry the owner already applied (live) - the STATE's progress holds it */
inline void BuildHelpSum(const std::vector<BuildHelpEntry>& owed, float* dP, int* n, float* d)
{
    for (size_t i = 0; i < owed.size(); ++i)
    {
        if (owed[i].applied != 0) continue;
        *dP += owed[i].dP;
        if ((int)owed[i].n > *n) *n = (int)owed[i].n;
        for (unsigned int k = 0; k < owed[i].n && k < kBuildMaxMats; ++k) d[k] += owed[i].d[k];
    }
}
/* the owner's confirmation row for a slot; create != 0 adds it (0 when the tail is full) */
inline BuildHelpAck* BuildHelpAckFor(std::vector<BuildHelpAck>* acks, unsigned short slot, int create)
{
    for (size_t i = 0; i < acks->size(); ++i) if ((*acks)[i].slot == slot) return &(*acks)[i];
    if (create == 0 || acks->size() >= kBuildMaxHelpers) return 0;
    BuildHelpAck a;
    a.slot = slot;
    acks->push_back(a);
    return &acks->back();
}
/* T-23 (step 4): the helpers' last applied work numbers a piece record carries (written by the piece's writer while its owner's game was
   away) raised into a game's own rows: per helper the higher number wins, with its refused tail; a row's number never goes down. A
   STATE tail's live number counts as applied. Returns how many rows rose. */
inline int BuildHelpAcksRaise(std::vector<BuildHelpAck>* acks, const std::vector<BuildHelpAck>& rec)
{
    int n = 0;
    for (size_t i = 0; i < rec.size(); ++i)
    {
        const unsigned int seq = (rec[i].live > rec[i].seq) ? rec[i].live : rec[i].seq;
        if (seq == 0 || rec[i].slot > kBuildOwnerSlotMax) continue;
        BuildHelpAck* a = BuildHelpAckFor(acks, rec[i].slot, 1);
        if (a == 0 || seq <= a->seq) continue;
        *a = rec[i];
        a->seq = seq; a->live = 0;
        ++n;
    }
    return n;
}

/* help1 fold 3 (finding 4): a HELP_GONE's refused tail - per material the whole items still owed (the cumulative refused amount less
   those already handed back, BuildHelpRefundOwed - the STATE's rule); owe[] gets each (0 past n), returns their total */
inline int BuildHelpGoneRefused(const float* ex, int n, const int* refunded, int* owe)
{
    int t = 0;
    for (int i = 0; i < (int)kBuildMaxMats; ++i)
    {
        owe[i] = (i < n) ? BuildHelpRefundOwed(ex[i], refunded[i]) : 0;
        t += owe[i];
    }
    return t;
}
/* help1 fold 3: the owner's rows changed (a gone record is written again only then) */
inline bool BuildHelpAcksSame(const std::vector<BuildHelpAck>& a, const std::vector<BuildHelpAck>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].slot != b[i].slot || a[i].seq != b[i].seq || a[i].nEx != b[i].nEx) return false;
        for (unsigned int k = 0; k < a[i].nEx && k < kBuildMaxMats; ++k) if (a[i].ex[k] != b[i].ex[k]) return false;
    }
    return true;
}
/* help1 fold 3 (finding 1): a STATE's tail - each landed row (seq = what pp.build holds on disk; its refused amounts) with live = the
   owner's live last applied for that slot (never below seq), then a row for each slot applied live with nothing landed yet (seq 0, no
   amounts - refunds follow only what is on disk). landed / live may be 0 (none) */
inline void BuildHelpTailOf(const std::vector<BuildHelpAck>* landed, const std::vector<BuildHelpAck>* live, std::vector<BuildHelpAck>* out)
{
    out->clear();
    for (size_t i = 0; landed != 0 && i < landed->size(); ++i)
    {
        BuildHelpAck t = (*landed)[i];
        t.live = t.seq;
        for (size_t j = 0; live != 0 && j < live->size(); ++j) if ((*live)[j].slot == t.slot && (*live)[j].seq > t.live) t.live = (*live)[j].seq;
        out->push_back(t);
    }
    for (size_t j = 0; live != 0 && j < live->size() && out->size() < kBuildMaxHelpers; ++j)
    {
        if ((*live)[j].seq == 0) continue;
        int have = 0;
        for (size_t i = 0; i < out->size(); ++i) if ((*out)[i].slot == (*live)[j].slot) have = 1;
        if (have != 0) continue;
        BuildHelpAck t;
        t.slot = (*live)[j].slot; t.seq = 0; t.live = (*live)[j].seq;
        out->push_back(t);
    }
}
/* help1 fold 3 (findings 2, 3): the owner's gone marker row in pp.build is keyed per (key, placement nonce) - "~g" + 8 hex nonce + 8 hex
   FNV-1a of the key + ':' + the key's first 44 bytes (<= kBuildMaxKey); its REMOVE names the whole key. BuildGoneRowOf reads the nonce
   back and accepts the row only when its key is exactly the one this key and nonce make */
inline std::string BuildGoneRowKey(const std::string& key, unsigned int nonce)
{
    static const char hx[] = "0123456789abcdef";
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < key.size(); ++i) { h ^= (unsigned char)key[i]; h *= 16777619u; }
    std::string s("~g");
    for (int i = 7; i >= 0; --i) s += hx[(nonce >> (4 * i)) & 15u];
    for (int i = 7; i >= 0; --i) s += hx[(h >> (4 * i)) & 15u];
    s += ':';
    s += key.substr(0, kBuildMaxKey - 19);
    return s;
}
inline bool BuildGoneRowOf(const std::string& rowKey, const std::string& key, unsigned int* nonce)
{
    if (rowKey.size() < 19 || rowKey[0] != '~' || rowKey[1] != 'g') return false;
    unsigned int v = 0;
    for (int i = 2; i < 10; ++i)
    {
        const char c = rowKey[(size_t)i];
        const int d = (c >= '0' && c <= '9') ? c - '0' : ((c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1);
        if (d < 0) return false;
        v = v * 16u + (unsigned int)d;
    }
    if (BuildGoneRowKey(key, v) != rowKey) return false;
    *nonce = v;
    return true;
}

/* P87 (to-do P87): pieces dismantled while the other player was away. THE OWNER'S TOMBSTONES - the keys of this world's own pieces
   dismantled here, oldest first, kept in pp.build as "~t" rows and sent again as REMOVEs at every roster round. Add: once per key (a
   repeat moves it to the back); past cap the oldest leave into *evicted (may be 0). 1 = a new tombstone. A tombstone ends with
   BuildOweRemoveDrop (a live own piece stands at the key again). */
const size_t kBuildTombCap = 256;
inline int BuildTombAdd(std::vector<std::string>* t, const std::string& key, size_t cap, std::vector<std::string>* evicted)
{
    for (size_t i = 0; i < t->size(); ++i)
        if ((*t)[i] == key)
        {
            t->erase(t->begin() + (std::vector<std::string>::difference_type)i);
            t->push_back(key);
            return 0;
        }
    t->push_back(key);
    while (cap > 0 && t->size() > cap)
    {
        if (evicted != 0) evicted->push_back(t->front());
        t->erase(t->begin());
    }
    return 1;
}
/* P87: a tombstone's pp.build row key - "~t" + 8 hex FNV-1a of the key + ':' + the key's first bytes (<= kBuildMaxKey); its REMOVE
   names the whole key. Never a gone marker's "~g" row, never a bare key. */
inline std::string BuildTombRowKey(const std::string& key)
{
    static const char hx[] = "0123456789abcdef";
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < key.size(); ++i) { h ^= (unsigned char)key[i]; h *= 16777619u; }
    std::string s("~t");
    for (int i = 7; i >= 0; --i) s += hx[(h >> (4 * i)) & 15u];
    s += ':';
    s += key.substr(0, kBuildMaxKey - 11);
    return s;
}
inline bool BuildTombRowOf(const std::string& rowKey, const std::string& key)
{
    return rowKey.size() > 11 && rowKey[0] == '~' && rowKey[1] == 't' && BuildTombRowKey(key) == rowKey;
}
/* P87 root (T661): THE RECEIVER'S COPY LIST - one entry per copy of another game's piece made or adopted here, kept in pp.build so a
   restart still knows which keys are whose copies (the engine does not keep the owner: an unresolved 'coop-p<slot>' becomes a town's
   faction at load). Row key "~c" + 8 hex FNV-1a of the key + ':' + the key's first bytes (<= kBuildMaxKey), as the "~t" tombstone row -
   never a "~t" / "~g" row, never a bare key. Row layout version 1 (kBuildCopyRowVersion): the PLACE bytes are a PLACE of the WHOLE key -
   sid, the PLACE's pose, the placement nonce, host / form / floor / outside for furniture - whose ownerSlot is the copy OWNER's slot
   (0..kBuildOwnerSlotMax; an own row's PLACE carries kBuildOwnerSender), the STATE bytes an empty STATE of the key. A later layout takes a new row-key letter; a row
   that does not read as this layout is refused (counted, left in the record). */
const int kBuildCopyRowVersion = 1;
/* P87 rf1: THE LIST'S CEILING. pp.build's codec caps each row (ownrec.h kBuildRecMaxBytes, 4096 per PLACE / STATE; DecodeBuildRec's
   GdcCount bounds the row count by the bytes present), not the record; the store reads a record back only up to kBuildStoreRecReadMax
   bytes (store.cpp ReadFileBytes) - past it the whole pp.build is unreadable and held, own rows included. A '~c' row is at most
   kBuildCopyRowMaxBytes (a wire-cap key, sid and host key come to ~423; the offline test p87_copy_list_ceiling measures it), so a full
   list fills at most half of that limit, the other half left to own rows, tombstones and gone markers. Every row is written again at
   each pp.build write (~283 bytes a typical row), as every own row is. Stale entries no longer park: one whose key answered a complete empty
   search at kBuildCopyEmptyLoads loads in a row is evicted (BuildCopyEvict). rf1 STATE facts (a pre-rf1 row reads 0/0/0): destroyed bit =
   REMOVED here (kept until a save asked after the removal has landed), progress = loads in a row with a complete empty search, complete
   bit = an ALIAS entry (the key where the copy really stands when it differs from the owner's key). */
const size_t kBuildStoreRecReadMax = (size_t)64 << 20;   /* store.cpp ReadFileBytes: a store record longer than this is not read back */
const size_t kBuildCopyRowMaxBytes = 512;                /* one '~c' row in pp.build at most: row key + PLACE + STATE + three 4-byte lengths */
const size_t kBuildCopyListCap = kBuildStoreRecReadMax / 2 / kBuildCopyRowMaxBytes;   /* 65 536; past it a new copy is not listed (counted, logged): a missing entry only falls back to the owner rule */
const int kBuildCopyEmptyLoads = 3;
struct BuildCopyRec
{
    std::string key, sid, hostKey;
    int slot;                /* the owner's slot, 0..kBuildOwnerSlotMax */
    unsigned int nonce;      /* the placement's nonce, 0 = none */
    float pos[3];
    float rot[4];
    unsigned char hostForm;
    int floor;
    unsigned char outside;
    int removed;             /* P87 rf1 (STATE destroyed bit) */
    int emptyLoads;          /* P87 rf1 (STATE progress) */
    int alias;               /* P87 rf1 (STATE complete bit) */
    unsigned char groundMark;   /* T-1372 decision 1691: the PLACE's ground mark (1 = this build's input; 0 = an older record's, converted where used) */
    int handed;              /* T-23 review fold 5 (STATE broken bit): THIS game placed the piece and handed it to slot at its placement,
                                and has not yet seen the world server hold its record (piecerec::PieceHandLoadChoice). A row written
                                before the bit reads 0 = not handed; a build before the bit ignores it. Same row size. */
    BuildCopyRec() : slot(-1), nonce(0), hostForm(0), floor(0), outside(0), removed(0), emptyLoads(0), alias(0), groundMark(0), handed(0)
    {
        for (int i = 0; i < 3; ++i) pos[i] = 0.0f;
        rot[0] = 1.0f; rot[1] = 0.0f; rot[2] = 0.0f; rot[3] = 0.0f;
    }
};
inline std::string BuildCopyRowKey(const std::string& key)
{
    static const char hx[] = "0123456789abcdef";
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < key.size(); ++i) { h ^= (unsigned char)key[i]; h *= 16777619u; }
    std::string s("~c");
    for (int i = 7; i >= 0; --i) s += hx[(h >> (4 * i)) & 15u];
    s += ':';
    s += key.substr(0, kBuildMaxKey - 11);
    return s;
}
inline bool BuildCopyRowIs(const std::string& rowKey)
{
    return rowKey.size() > 11 && rowKey[0] == '~' && rowKey[1] == 'c' && rowKey[10] == ':';
}
inline bool BuildCopyRowOf(const std::string& rowKey, const std::string& key)
{
    return BuildCopyRowIs(rowKey) && BuildCopyRowKey(key) == rowKey;
}
/* the text every near key shares with key (items.cpp BoxKeyNearKey: the same record sid and sector, x / z within a tenth): the key up
   to and including its last '@'. false when key has no sid before two '@' (BoxKeyNearKey answers no for such a key). The copy list
   asks BoxKeyNearKey only for the run of its sorted keys that starts with this text. */
inline bool BuildKeyNearPrefix(const std::string& key, std::string* prefix)
{
    const std::string::size_type at2 = key.rfind('@');
    if (at2 == std::string::npos || at2 == 0) return false;
    const std::string::size_type at1 = key.rfind('@', at2 - 1);
    if (at1 == std::string::npos || at1 == 0) return false;
    *prefix = key.substr(0, at2 + 1);
    return true;
}
inline bool BuildCopyRecSame(const BuildCopyRec& a, const BuildCopyRec& b)
{
    if (a.key != b.key || a.sid != b.sid || a.hostKey != b.hostKey || a.slot != b.slot || a.nonce != b.nonce) return false;
    if (a.hostForm != b.hostForm || a.floor != b.floor || a.outside != b.outside) return false;
    if (a.removed != b.removed || a.emptyLoads != b.emptyLoads || a.alias != b.alias || a.handed != b.handed) return false;   /* P87 rf1; T-23 fold 5 */
    if (a.groundMark != b.groundMark) return false;   /* T-1372 */
    for (int i = 0; i < 3; ++i) if (a.pos[i] != b.pos[i]) return false;
    for (int i = 0; i < 4; ++i) if (a.rot[i] != b.rot[i]) return false;
    return true;
}
/* false (nothing usable) when the entry cannot be a v1 row: no key / sid, a slot outside 0..kBuildOwnerSlotMax, or a PLACE / STATE not encodable */
inline bool BuildCopyRowMake(const BuildCopyRec& c, std::string* rowKey, std::vector<char>* place, std::vector<char>* state)
{
    if (c.key.empty() || c.key.size() > kBuildMaxKey || c.sid.empty() || c.slot < 0 || c.slot > (int)kBuildOwnerSlotMax) return false;
    BuildMsg m;
    m.kind = kBuildPlace; m.key = c.key; m.sid = c.sid;
    for (int i = 0; i < 3; ++i) m.pos[i] = c.pos[i];
    for (int i = 0; i < 4; ++i) m.rot[i] = c.rot[i];
    m.complete = 0; m.ownerSlot = (unsigned short)c.slot; m.nonce = c.nonce;
    m.groundMark = (unsigned char)(c.groundMark != 0 ? 1 : 0);   /* T-1372 */
    if (!c.hostKey.empty()) { m.hostKey = c.hostKey; m.hostForm = c.hostForm; m.floor = c.floor; m.outside = c.outside; }
    BuildMsg st;
    st.kind = kBuildState; st.key = c.key;
    st.destroyed = (unsigned char)(c.removed != 0 ? 1 : 0); st.complete = (unsigned char)(c.alias != 0 ? 1 : 0);   /* P87 rf1 */
    st.broken = (unsigned char)(c.handed != 0 ? 1 : 0);   /* T-23 review fold 5: 'handed here' - the same byte, no size change */
    st.progress = (float)(c.emptyLoads < 0 ? 0 : (c.emptyLoads > 255 ? 255 : c.emptyLoads));
    place->clear(); state->clear();
    if (!BuildEncodable(m) || !BuildEncodable(st) || !EncodeBuild(place, m) || !EncodeBuild(state, st)) { place->clear(); state->clear(); return false; }
    *rowKey = BuildCopyRowKey(c.key);
    return true;
}
/* a pp.build row as a v1 copy-list entry: its PLACE decodes, names the key its row key was made from, carries a real owner slot and a
   sid; its STATE is a STATE of the same key. Anything else is false (*out untouched). */
inline bool BuildCopyRowRead(const std::string& rowKey, const std::string& place, const std::string& state, BuildCopyRec* out)
{
    if (!BuildCopyRowIs(rowKey) || place.empty() || state.empty()) return false;
    BuildMsg m, st;
    if (DecodeBuildSaved(place.data(), place.size(), &m) != kBuildDecodeOk || m.kind != kBuildPlace) return false;
    if (!BuildCopyRowOf(rowKey, m.key) || m.ownerSlot == kBuildOwnerSender || m.sid.empty()) return false;
    if (DecodeBuild(state.data(), state.size(), &st) != kBuildDecodeOk || st.kind != kBuildState || st.key != m.key) return false;
    BuildCopyRec c;
    c.key = m.key; c.sid = m.sid; c.slot = (int)m.ownerSlot; c.nonce = m.nonce;
    c.groundMark = m.groundMark;   /* T-1372 */
    for (int i = 0; i < 3; ++i) c.pos[i] = m.pos[i];
    for (int i = 0; i < 4; ++i) c.rot[i] = m.rot[i];
    if (!m.hostKey.empty()) { c.hostKey = m.hostKey; c.hostForm = m.hostForm; c.floor = m.floor; c.outside = m.outside; }
    c.removed = st.destroyed != 0 ? 1 : 0;   /* P87 rf1: a pre-rf1 row carries 0 / 0 / 0 */
    c.alias = st.complete != 0 ? 1 : 0;
    c.handed = st.broken != 0 ? 1 : 0;   /* T-23 review fold 5: a row written before the bit reads 0 (not handed) */
    c.emptyLoads = (st.progress >= 0.0f && st.progress <= 255.0f) ? (int)st.progress : 0;
    *out = c;
    return true;
}
/* P87 rf1: an entry whose key answered a complete empty search at kBuildCopyEmptyLoads loads in a row is evicted */
inline int BuildCopyEvict(int emptyLoads) { return emptyLoads >= kBuildCopyEmptyLoads ? 1 : 0; }
/* P87 rf1 (HIGH): a REMOVED mark retires once a save ASKED AFTER the mark (request number > markReq) has landed; a mark read back from
   pp.build (ready 0) waits until this world has settled it (the copy removed again, or a complete empty search at its key) */
inline int BuildCopyMarkRetires(int removed, int ready, unsigned long markReq, unsigned long landedReq)
{
    return (removed != 0 && ready != 0 && landedReq > markReq) ? 1 : 0;
}
/* P87: THE RECEIVER'S DECISION for a REMOVE whose key has no row here (the piece this game's save brought back is not a copy row).
   listed: the owner lists the key here (a row, a pending PLACE or REMOVE) - its own road, never touched. found: the key answered one
   live piece; completeNone: a complete search of the loaded zones found none (else the search was incomplete). mine: the live piece
   is this game's own - never removed. sender: the sender's stand-in owns it. dismantling: its dismantled flag is set - looked at again. */
const int kBuildReconRemove = 1;      /* taken as the sender's copy and removed the REMOVE way (no refund) */
const int kBuildReconKeepOwn = 2;
const int kBuildReconKeepOther = 3;
const int kBuildReconListed = 4;
const int kBuildReconPark = 5;        /* nothing in the loaded zones: looked up again when a zone finishes loading */
const int kBuildReconRetry = 6;
inline int BuildReconcileAct(int listed, int found, int completeNone, int mine, int sender, int dismantling)
{
    if (listed != 0) return kBuildReconListed;
    if (found == 0) return completeNone != 0 ? kBuildReconPark : kBuildReconRetry;
    if (mine != 0) return kBuildReconKeepOwn;
    if (sender == 0) return kBuildReconKeepOther;
    if (dismantling != 0) return kBuildReconRetry;
    return kBuildReconRemove;
}
/* T-1372 A2: MAY A REMOVE FOR ROW `key` TAKE APART THE PIECE A SEARCH BY POSITION FOUND? The lookup by key ignores height for a single
   candidate (items.cpp ItBoxResolve), so an old key a re-key retired resolves the piece another row already holds under the new key
   (F1035: the host's live copy dismantled by the REMOVE for the old key, 31 ms after it was adopted under the new one). pieceKey: the
   found piece's own position key ("" = unread). rowAtPieceKey: 1 = a live row (listed, not removed) is filed under pieceKey.
   aliasRow: the row whose live key is pieceKey (a copy found or made at another key; "" = none); aliasLive: 1 = that row is listed,
   not removed, and still names pieceKey as its live key. Returns 0 = act (the piece is this row's, or no other row is known to hold
   it); 1 = refuse, a different row is filed under the piece's own key; 2 = refuse, a different row holds it by its live key.
   T-1372 review fold 1: a refused REMOVE retires its OWN row (BdRemoveOne: removed, no dismantle, its alias dropped), so that row's
   alias is no longer live for the holder's own later REMOVE. pieceKey == key with another LIVE row holding the piece by its live key
   still refuses: that is a re-key's new key that adopted the old key's piece where it stands (B's copy at the old key, PLACE new key
   found it), and the old key's REMOVE must not take it apart. */
const int kBuildRmHeldNone = 0;
const int kBuildRmHeldByKey = 1;
const int kBuildRmHeldByAlias = 2;
inline int BuildRemoveHeldByOther(const std::string& key, const std::string& pieceKey, int rowAtPieceKey, const std::string& aliasRow, int aliasLive)
{
    if (pieceKey.empty()) return kBuildRmHeldNone;   /* unread: nothing known - the REMOVE's own road decides, as before */
    if (rowAtPieceKey != 0 && pieceKey != key) return kBuildRmHeldByKey;
    if (aliasLive != 0 && !aliasRow.empty() && aliasRow != key) return kBuildRmHeldByAlias;
    return kBuildRmHeldNone;
}
/* T-1372 (H-T1372-removal, F1037): WHICH ENGINE DESTROY ENDS A PLAYER'S BUILDING FOR GOOD. GameWorld::destroy 0x798F50 (1.0.68 0x799AF0)
   takes (object, justUnloaded, the caller's reason literal). The removal mark was written only on the dismantle road (0x2A2820), so a
   piece the engine deleted outright came back at the next load. The reasons, each read at its one caller (1.0.65):
     for good   "Level Editor" - the Level Editor's delete 0x776D60 (decomp_776d60.txt:24), after it tore the interior down through
                0x54EA40 (:19); "Building::confirmDismantle" - the panel's dismantle confirm 0x54F410 (decomp_54f410.txt:35): after the
                piece's own dismantle step (vt+0x248, the marked road) it destroys each building 0x54AF40 gathers from the piece's
                record references (mask 0x9000 - its mounted pieces, Inferred), which no dismantle step marks.
     interior   "_destroyAllInternalBuildings" - the interior teardown 0x54EA40, called by the editor's delete, a purchase and the mod's
                own buyer teardown (build.cpp BdTdDestroyPod): a removal only inside the editor's delete.
     not one    "dynamicDestroyBuildingsList" (a dismantled piece queued on GameWorld+0x628 - already marked), "ZoneMapContent::deactivate"
                (an unload, flag 1), "deleteDuplicatedBuildings", "Building::moveMountedBuildings", "Building::setDestroyed",
                "door in setDestroyed", "destroyDoors", "_clearAndDestroyAllFurniture".
     other      any other reason, or none - a plugin's own delete would arrive here: logged, never marked.
   BuildEngineDestroyDecide: justUnloaded (the engine's flag 1 = put away, never a removal), the reason's class, inEditorDelete (1 = the
   destroy runs inside the Level Editor's delete on the same thread), objType (the object's vt+0x20 data type, 0 = a building; -1
   unread). Returns 0 nothing, 1 the piece is removed for good (its row is marked removed), 2 an unknown reason ended a building (log). */
const int kBuildEngReasonOther = 0;
const int kBuildEngReasonForGood = 1;
const int kBuildEngReasonInterior = 2;
const int kBuildEngReasonNotRemoval = 3;
const int kBuildEngRmNone = 0;
const int kBuildEngRmRemoved = 1;
const int kBuildEngRmOther = 2;
/* T-1372 re-check fold F: MAY A POSTPONED FOR-GOOD CAPTURE STAND FOR THIS DESTROY? The engine postpones a destroy while its world lock is
   busy (decomp_798f50 :72-76: the object alone is kept, not its reason), and what drains that set is not traced - the object may end
   by a road that skips the hooked destroy, and its address be used again. Used only when this destroy is no unload (an unload puts the
   piece away: the capture is freed), the object is a building whose key reads and equals the captured key (the same building, not a
   new object at the old address), and the capture is younger than kBuildEngPostMaxMs. The age: the lock is held for a worker's pass,
   not for game seconds (Inferred), so a capture still waiting after 30 s is taken for one whose destroy went by another road - the key
   check is the guard, the age the backstop for a slot whose address is never seen again. 1 = use it, 0 = free it. */
const unsigned long kBuildEngPostMaxMs = 30000;
inline int BuildEngPostponedUse(int justUnloaded, int isBuilding, int keyOk, int keySame, unsigned long ageMs)
{
    if (justUnloaded != 0) return 0;
    if (ageMs > kBuildEngPostMaxMs) return 0;
    if (isBuilding == 0 || keyOk == 0 || keySame == 0) return 0;
    return 1;
}
inline int BuildEngReasonIs(const char* r, const char* lit)
{
    for (int i = 0; ; ++i) { if (r[i] != lit[i]) return 0; if (lit[i] == 0) return 1; }
}
inline int BuildEngineDestroyReason(const char* r)
{
    if (r == 0) return kBuildEngReasonOther;
    if (BuildEngReasonIs(r, "Level Editor") || BuildEngReasonIs(r, "Building::confirmDismantle")) return kBuildEngReasonForGood;
    if (BuildEngReasonIs(r, "_destroyAllInternalBuildings")) return kBuildEngReasonInterior;
    if (BuildEngReasonIs(r, "dynamicDestroyBuildingsList") || BuildEngReasonIs(r, "ZoneMapContent::deactivate") || BuildEngReasonIs(r, "deleteDuplicatedBuildings")
        || BuildEngReasonIs(r, "Building::moveMountedBuildings") || BuildEngReasonIs(r, "Building::setDestroyed") || BuildEngReasonIs(r, "door in setDestroyed")
        || BuildEngReasonIs(r, "destroyDoors") || BuildEngReasonIs(r, "_clearAndDestroyAllFurniture")) return kBuildEngReasonNotRemoval;
    return kBuildEngReasonOther;
}
inline int BuildEngineDestroyDecide(int justUnloaded, int reasonClass, int inEditorDelete, int objType)
{
    if (justUnloaded != 0) return kBuildEngRmNone;   /* put away: the piece comes back with its area */
    if (objType != 0) return kBuildEngRmNone;        /* not a building (or its type unread) */
    if (reasonClass == kBuildEngReasonForGood) return kBuildEngRmRemoved;
    if (inEditorDelete != 0) return kBuildEngRmRemoved;   /* the editor's delete takes the piece and everything inside it for good */
    if (reasonClass == kBuildEngReasonOther) return kBuildEngRmOther;
    return kBuildEngRmNone;
}
/* P87 fold 3 (T651): THE ONE OWNER RULE the PLACE adopt (BdCopyOne), the reconcile (BdReconDrain) and the REMOVE apply (BdRemoveOne)
   share - "whose is this live piece, seen from the game of slot senderSlot?". A game's save names a copy's owner by the stand-in's
   RECORD id "coop-p<slot>"; after a restart the stand-in is a NEW object, so a pointer compare alone called the save's copy another
   owner's (T651: KEPT). isNull: no owner. isMine: this game's player faction. isSenderLive: the sender's live stand-in (the pointer, or
   the stand-in registered here for senderSlot). isStandInLive: any registered stand-in (or a protocol-67 coop-peer). recordSlot: the
   owner's record id as StandInRecordSlot reads it (n = coop-p<n>, -1 another id, -2 coop-peer, -3 unreadable). senderSlot: -1 unknown. */
const int kBuildLiveOwnNone = 0;
const int kBuildLiveOwnMine = 1;
const int kBuildLiveOwnSender = 2;          /* the sender's live stand-in */
const int kBuildLiveOwnSenderRecord = 3;    /* a faction whose record id is coop-p<sender slot> - a save's stand-in: the sender's too */
const int kBuildLiveOwnOtherStandIn = 4;    /* another slot's stand-in, or a protocol-67 coop-peer - never the sender's */
const int kBuildLiveOwnThird = 5;           /* anybody else (an unreadable record included) - never the sender's */
const int kBuildLiveOwnSenderList = 6;      /* P87 root: the key is in this game's copy list as the sender's slot's copy, same sid - the sender's */
/* P87 root (T661): the FIRST test. listSlot: the owner slot the copy list holds for the live piece's key (-1 not listed); listSidOk: the
   listed sid is the live piece's; ownRowHere: this game holds its own piece at the key (an own row, or its own pp.build row) - own rows
   always win, so the list is not asked. The list outranks the owner pointer: the engine gives a restored copy a town's faction (or, in a
   player town, this game's), never the stand-in. Every other answer is fold 3's, unchanged (the fallback for keys not listed). */
inline int BuildOwnerClass(int isNull, int isMine, int isSenderLive, int isStandInLive, int recordSlot, int senderSlot,
                           int listSlot = -1, int listSidOk = 0, int ownRowHere = 0)
{
    if (ownRowHere == 0 && senderSlot >= 0 && listSlot == senderSlot && listSidOk != 0) return kBuildLiveOwnSenderList;
    if (isNull != 0) return kBuildLiveOwnNone;
    if (isMine != 0) return kBuildLiveOwnMine;
    if (isSenderLive != 0) return kBuildLiveOwnSender;
    if (senderSlot >= 0 && recordSlot == senderSlot) return kBuildLiveOwnSenderRecord;
    if (isStandInLive != 0 || recordSlot >= 0 || recordSlot == -2) return kBuildLiveOwnOtherStandIn;
    return kBuildLiveOwnThird;
}
inline int BuildOwnerIsSender(int cls) { return (cls == kBuildLiveOwnSender || cls == kBuildLiveOwnSenderRecord || cls == kBuildLiveOwnSenderList) ? 1 : 0; }
/* P18 (parity P18): A BUILDING'S OWNER CHANGE. THE AUTHORITY: the game whose PLAYER the building now belongs to - the buyer's game for
   a house bought in town (Building::buyMeCallback 0x7ACB20 runs this->setFaction(playerFaction, 0), build/read-locks.md 1.3). That is
   the building road's own rule (the WRITER of a piece is its owner's game; a town building nobody owns is not synced - P12 / P19). The
   buyer's game registers the building as its own piece (the zone scan's rule) and sends its PLACE + STATE; the other game ADOPTS its copy
   through THE ONE OWNER RULE above (a town / NPC owner = kBuildLiveOwnNone / kBuildLiveOwnThird -> the engine's own setFaction to the
   sender's stand-in). No new message kind. WHO SENDS - one setFaction call seen on this game (Building vt+0xA0, 0x556EC0):
   onMain: the main thread (a purchase is a UI callback; a zone loading off the main thread is only counted); modWrite: the mod's own write
   is running (a received owner applied here, a hand-over, an owner fix) - THE LOOP REFUSAL, first; depth: the setFaction nesting (1 = the
   outermost call; the engine recurses over the building's doors and interior - the same change); wasMine / nowMine: the owner before / after
   the call is this game's player faction. */
const int kBuildOcRegister = 0;   /* queued: at the tick the building is registered as this game's own and its PLACE + STATE go out */
const int kBuildOcOffMain = 1;
const int kBuildOcModWrite = 2;   /* never sent: a received change must not go back */
const int kBuildOcNested = 3;
const int kBuildOcNotMine = 4;    /* to anybody but this game's player (a town override, an NPC faction, a stand-in) - not this road */
const int kBuildOcWasMine = 5;
inline int BuildOwnerChangeAct(int onMain, int modWrite, int depth, int wasMine, int nowMine)
{
    if (onMain == 0) return kBuildOcOffMain;
    if (modWrite != 0) return kBuildOcModWrite;
    if (depth > 1) return kBuildOcNested;
    if (nowMine == 0) return kBuildOcNotMine;
    if (wasMine != 0) return kBuildOcWasMine;
    return kBuildOcRegister;
}
/* T-1222 (owner 1444: a rock is never owned). The engine's job set-up for a worker (1.0.65 0x35B3D0) claims a building whose "is
   foliage" byte (+0xC0) is set for the worker's faction when that faction is a player's and the building's owner is not
   (setFaction through vt+0xA0, off the main thread; the call returns to the row JobClaimSetFactionRet - run T1923 Confirmed). The
   job and the ore yield do not need the claim (setupMiningResourceLevel 0x29C630 reads no owner for a foliage piece). For a world
   resource node (coopown::WorldNodeOf) the claim is skipped - nothing is written, so the rock stays nobody's on every game; any
   other setFaction (a zone loading, a purchase, the mod's own write, the claim of a building that is no world node) runs as before.
   ret: the detour's return address; claimRet: the row's address in the running image (0 = no address: nothing is skipped - a missing
   row in fact stops the mod at load, addresses.cpp, so the hook is never installed);
   worldNode: 1 = the building is a world resource node. Only reads and compares - safe on any thread. */
inline int BuildIsClaimSite(unsigned long long ret, unsigned long long claimRet) { return (claimRet != 0 && ret == claimRet) ? 1 : 0; }
inline int BuildClaimSkip(int claimSite, int worldNode) { return (claimSite == 1 && worldNode == 1) ? 1 : 0; }
/* P18: a queued change at the tick. hold: engine writes blocked, this world's copy list unread, or no player faction - wait (no try spent);
   tries / maxTries: ticks spent waiting for the key to resolve; live: the key resolves; nowMine: still this game's player's; rowOwn /
   rowCopy: the registry already holds the key as this game's own piece / as another game's piece. */
const int kBuildOdScan = 0;       /* registered as an own piece (BdScanPiece), its PLACE + STATE owed */
const int kBuildOdWait = 1;
const int kBuildOdGiveUp = 2;
const int kBuildOdNotMine = 3;
const int kBuildOdCopyRow = 4;    /* another game's piece the engine gave this game: never claimed (the other game is its writer) */
const int kBuildOdKnown = 5;      /* already this game's own piece: its own road (PLACE / STATE / REMOVE) carries it */
inline int BuildOwnerDrainAct(int hold, int tries, int maxTries, int live, int nowMine, int rowOwn, int rowCopy)
{
    if (hold != 0) return kBuildOdWait;
    if (live == 0) return (tries + 1 >= maxTries) ? kBuildOdGiveUp : kBuildOdWait;
    if (nowMine == 0) return kBuildOdNotMine;
    if (rowCopy != 0) return kBuildOdCopyRow;
    if (rowOwn != 0) return kBuildOdKnown;
    return kBuildOdScan;
}
/* P18, WHO APPLIES: the receiver's PLACE adopt of a live building whose owner class (BuildOwnerClass) is nobody or a third party (the town,
   an NPC faction) is an owner change applied here - counted and logged as P18's. 'mine' is never adopted (the receiver's own building
   stays its own; a duplicate is left alone) and the sender's own stand-in is no change. */
inline int BuildOwnerAdoptFromWorld(int cls) { return (cls == kBuildLiveOwnNone || cls == kBuildLiveOwnThird) ? 1 : 0; }
/* P18 fold 1 (item 4): ONLY A BUILDING WITH A KEY IS QUEUED. The buy callback 0x7ACB20 makes its own OUTERMOST setFaction calls over
   the building's doors (+0x1B8) and its interior's +0x88 list (furniture), which the hook would otherwise queue and give up on.
   The zone scan's own refusals decide it (BdScanPiece): layout furniture (+0x238 isFurnitureOf) or an interior object (+0xF8), and
   a door a building made for itself (+0x1A0 imADoor). Any other verdict is kept as it was. */
const int kBuildOcNotBuilding = 6;
inline int BuildOwnerChangeQueueAct(int act, int furniture, int door)
{
    if (act == kBuildOcRegister && (furniture != 0 || door != 0)) return kBuildOcNotBuilding;
    return act;
}
/* P18 fold 1 (item 1): THE NESTING COUNT AT THE TICK. The per-tick step (BdRosterTick, from BuildTick in the command channel's
   main-thread pump) never runs inside a setFaction, so the count is 0 there unless a fault inside a guarded owner change unwound
   past the hook's decrement - then every later purchase would be judged nested. The tick puts it back to 0; 1 = it had to. */
inline int BuildOcDepthReset(int* depth) { const int was = *depth; *depth = 0; return was != 0 ? 1 : 0; }
/* P18 fold 1 (items 2 and 6): an adopt from the world (nobody / a town or NPC owner) is a PURCHASE only when this game's copy was
   for sale (the engine's own Building::isForSale vt+0x2C0, asked BEFORE the owner is written: 1 yes, 0 no, -1 faulted), it is no
   furniture, and the owner really changes to a named owner (the sender's stand-in). Only a purchase gets the bought house's world
   side and the `P18 owner applied` counter / line; every other adopt from the world is counted adoptOther. */
inline int BuildOwnerAdoptIsPurchase(int adoptFromWorld, int furniture, int forSale, int ownerChanges, int targetNamed)
{
    return (adoptFromWorld != 0 && furniture == 0 && forSale == 1 && ownerChanges != 0 && targetNamed != 0) ? 1 : 0;
}
/* T-1430 (F1224 / F1253): APPLY THE PURCHASE HERE - the one decision every road that writes a house's owner makes BEFORE its first
   owner write (the place-adopt, a copy-list adopt included; the area rebuild restore R4; the own-row / own-record restore R6;
   the kept row's hand-back after a world reset, BdKeptHandBack - fold 2). The
   shared record is the truth: a player (another game's stand-in, or this game's own player) owns the house, the house here is not yet
   player-owned, and the ENGINE says it is for sale here - so this game never ran the purchase. The copy-list class no longer blocks it
   (a house the OTHER player bought is on this game's copy list after its first purchase replay; a load whose ordinary save is older
   than the purchase brings the town's house back - F1224). furniture: the piece is furniture; recordNamesPlayer: the owner the record
   names is a player's; livePlayerSide: the live owner here is a player's (BdOwnerIsPlayerSide); forSale: the engine's own isForSale
   (1 yes, 0 no, -1 faulted - an unread answer is no); ownerChanges: the write would change the owner. 1 = run the whole purchase
   (BuildPurchaseHerePlanOf), 0 = the road's owner write alone. A later load whose save holds the purchase finds the house
   player-owned, so it does not run there again; a load with no such save (a crash, a relaunch without saving) brings the town's
   house back for sale and it runs again - see the doors below. NOT decided here (fold 2, Read): a house an OLDER build adopted
   plain and then saved is player-owned here, so no road repairs its furniture. No real fact tells 'the purchase's tail never ran
   on this game': the town list 0x928010 empties (town +0x500) holds 'nest' entries (0x928470, added by the building setup
   0x57E760 for a record with a 'nest' reference, with no owner test, on every creation) and a town-centre entry with no building
   (0x928470's second caller, 0x9344F0) - no ordinary house has one, and a house that has one gets it back on each load whether it
   was bought or not. */
inline int BuildPurchaseApplyHere(int furniture, int recordNamesPlayer, int livePlayerSide, int forSale, int ownerChanges)
{
    return (furniture == 0 && recordNamesPlayer != 0 && livePlayerSide == 0 && forSale == 1 && ownerChanges != 0) ? 1 : 0;
}
/* T-1430: what a 'yes' runs, the same on every road (BdOwnerRestore's world side): the residents and the town lists, the furniture
   teardown 0x54EA40's rule, the interior re-own of pieces no player owns only (interiorTownOnly - fold 2: every road, so a player's
   own piece in the house is never handed to the new owner), the doors re-owned AND opened, and the purchase's tail (location nodes
   0x38A5E0, the town step 0x296900 / 0x928010, the house's interior walking map 0x4D2340). A 'no' runs nothing of it.
   THE DOORS (fold 2): the engine's purchase opens them, so a 'yes' opens them. A 'yes' comes only while the house here is for sale
   and no player's, so a load whose save holds the purchase does not open them again; but a load with NO save that holds it (a crash,
   or a relaunch without saving) brings the town's house back, the purchase runs again and the doors are opened again, a door a
   player had shut included. That stays until an ordinary door's open / shut state is kept in the shared record (T-1429's gap). */
struct BuildPurchaseHerePlan { int worldSide; int teardown; int openDoors; int tail; int interiorTownOnly; };
inline BuildPurchaseHerePlan BuildPurchaseHerePlanOf(int apply)
{
    BuildPurchaseHerePlan p; p.worldSide = 0; p.teardown = 0; p.openDoors = 0; p.tail = 0; p.interiorTownOnly = 0;
    if (apply != 1) return p;
    p.worldSide = 1; p.teardown = 1; p.openDoors = 1; p.tail = 1; p.interiorTownOnly = 1;
    return p;
}
/* P18 fold 1 (item 6): the applied line's outcome from the owner write (fixed: 1 written, 0 not written - already the named owner or
   an empty target, -1 faulted). Not written is a SKIP, never a fault. */
const int kBuildOaApplied = 0;
const int kBuildOaSkip = 1;
const int kBuildOaFaulted = 2;
inline int BuildOwnerApplyOutcome(int fixed) { return fixed == 1 ? kBuildOaApplied : (fixed == 0 ? kBuildOaSkip : kBuildOaFaulted); }
/* P18 fold 1 (item 2): the residents are collected by the engine's own 0x6BA220 into a list this mod holds with a fixed room. That
   function grows a full list through the game's allocator (it would free this mod's buffer), so the step runs only when every
   platoon of the owner (the active +0x210 and unloaded +0x228 counts, plus the one its last branch may add) fits. */
inline int BuildBuyResidentsFit(unsigned active, unsigned unloaded, unsigned room)
{
    return (active < room && unloaded < room && active + unloaded + 1 < room) ? 1 : 0;
}
/* P18 fold 1 (item 5): Building::isForSale (vt+0x2C0, 1.0.65 0x29C4C0) offers "buy" on a house whose owner is not a player faction
   (+0x250) and not allied with the player - a stand-in is no player faction, so another player's house WAS for sale. As in single
   player (a player-owned house is not for sale) the engine's yes is kept only when the owner is not a stand-in. */
inline int BuildForSaleKeep(int engineSays, int ownerIsStandIn) { return (engineSays != 0 && ownerIsStandIn == 0) ? 1 : 0; }
/* P18 fold 2 (P19 review 2026-09-30): a town rebuild (ZoneMapContent::_activate 0x9FEC00 -> 0x9FD470, build/decomp_9fd470.txt) drops
   each saved world building (instance 'created' -1) whose FIRST state the engine finds is a building state (GAMESTATE_BUILDING 0x23)
   whose "owner faction ID" is not this game's player faction's id and whose townC / townCS are the rebuilt town's - and each whose
   first state is the other kind (0x53). A house another player owns names that player's stand-in RECORD id ("coop-p<n>", a
   protocol-67 save's "coop-peer"): it is held out of the drop, as the engine holds the local player's. recordSlot: n for coop-p<n>,
   -2 coop-peer, -1 any other id (an NPC, a town, this game's player - the engine's own answer stands). */
inline int BuildRebuildHold(int firstIsBuildingState, int recordSlot) { return (firstIsBuildingState != 0 && (recordSlot >= 0 || recordSlot == -2)) ? 1 : 0; }
inline int BuildRebuildOwnerSlot(int standInSlotOfId, int isLegacyPeerId) { return isLegacyPeerId != 0 ? -2 : (standInSlotOfId >= 0 ? standInSlotOfId : -1); }
/* A KEPT PIECE'S OWNER AFTER A TOWN REBUILD. The hold above keeps another player's saved piece, but every rebuild (and every first build
   of an area, or a dropped save) raises the area's fresh flag (zone +0xA8, 0x9FEC00), and createBuilding then gives a piece whose owner
   is no player faction the town's faction (F941 / F942) - a stand-in is none. When the area finishes loading here, each row in it is
   judged once. rowCurrent: not removed, not dismantled, this world's generation; rowKnown: a copy row on this world's copy list;
   pending: a PLACE or a REMOVE for the key still waits; found: 1 the key named one live piece (not dismantled), 0 none, -1 more than
   one or a malformed key (final), -2 not yet (a teardown, its host, its inside or its state not readable yet - the area is walked
   again on its event, BuildRebuildRestoreWake), -3 the lookup's walk was cut (final); sidMatch: that piece's record id is the row's;
   ownerClass: the live owner - kBuildRrOwnerRight the row's own, kBuildRrOwnerMine this game's player (on a copy row),
   kBuildRrOwnerOther another stand-in or player, kBuildRrOwnerTown no player's (the town, an NPC faction, nobody); wantHere: the row's
   owner faction exists here. A piece of this game's player or of another player is never taken. */
const int kBuildRrOwnerRight = 0, kBuildRrOwnerMine = 1, kBuildRrOwnerOther = 2, kBuildRrOwnerTown = 3;
const int kBuildRrNone = 0;        /* not looked at, or nothing to put back */
const int kBuildRrRestore = 1;     /* the row's owner is written back */
const int kBuildRrNotMine = 2;     /* the piece is this game's player's or another player's: left alone */
const int kBuildRrAmbiguous = 3;   /* the key did not name exactly one piece of the row's record: left alone */
const int kBuildRrNoOwner = 4;     /* the row's owner faction is not in this world now: looked at again when a stand-in is registered */
const int kBuildRrNotYet = 5;      /* the key could not be answered yet: the area is walked again (BuildRebuildRestoreRetry) */
const int kBuildRrCut = 6;         /* the lookup's walk was cut: a walk of the same area is cut the same way - final, counted */
inline int BuildRebuildRestoreAct(int rowCurrent, int rowKnown, int pending, int found, int sidMatch, int ownerClass, int wantHere)
{
    if (rowCurrent == 0 || rowKnown == 0 || pending != 0 || found == 0) return kBuildRrNone;
    if (found == -3) return kBuildRrCut;
    if (found == -2) return kBuildRrNotYet;
    if (found < 0 || sidMatch == 0) return kBuildRrAmbiguous;
    if (ownerClass == kBuildRrOwnerRight) return kBuildRrNone;
    if (ownerClass != kBuildRrOwnerTown) return kBuildRrNotMine;
    return wantHere != 0 ? kBuildRrRestore : kBuildRrNoOwner;
}
/* the after-load pass with the re-make. A copy row (another player's piece) that is current, on the copy list (rowKnown: not marked
   removed; listMain: its own entry, not an alias), with no PLACE / REMOVE waiting, whose owner's last word is not a ruin (ownerGone), and
   whose key answered a COMPLETE empty search (found 0 with emptyComplete 1 - never a cut, truncated, torn or not-yet answer, nor a piece
   being dismantled) is made again from its copy-list entry: the area was built from a record older than the piece. Every other answer is
   BuildRebuildRestoreAct's, unchanged. */
const int kBuildRrRemake = 7;      /* made again from the copy list (BdRemakeQueue) */
inline int BuildRebuildRestoreChoose(int rowCurrent, int rowKnown, int listMain, int pending, int found, int emptyComplete, int ownerGone,
                                     int sidMatch, int ownerClass, int wantHere)
{
    if (rowCurrent != 0 && rowKnown != 0 && listMain != 0 && pending == 0 && found == 0 && emptyComplete != 0 && ownerGone == 0) return kBuildRrRemake;
    return BuildRebuildRestoreAct(rowCurrent, rowKnown, pending, found, sidMatch, ownerClass, wantHere);
}
/* a re-make queued at the pass is still owed when the PLACE road reaches it: its copy row stands (rowCopy: via 3, not this game's own;
   rowGone: removed or dismantled), the copy list still knows it (not marked removed), and no REMOVE for it waits. 1 = make it */
inline int BuildRemakeStillOwed(int rowCopy, int rowGone, int listKnown, int removePending)
{
    return (rowCopy != 0 && rowGone == 0 && listKnown != 0 && removePending == 0) ? 1 : 0;
}
/* a re-make inside a host counts the host's inside as loaded only on an explicit 'loaded' answer (insideState 1 - never 0, not loaded or
   none, nor -1, a host that does not resolve or does not read); a free-standing re-make (furn 0) has no inside. The after-load pass and
   the run ask the same. 1 = loaded */
inline int BuildRemakeInsideOk(int furn, int insideState)
{
    return (furn == 0 || insideState == 1) ? 1 : 0;
}
/* the keys a free-standing re-make's run asks are the pass's: the builder's key (mainNone: it answered a complete empty search), then the
   copy's own (live) key when it differs (hasOther; otherFound / otherNone: that key's answer). Inside a host both keys go to one search. */
const int kBuildRemakeKeysNone = 0;    /* complete and empty at every key: made */
const int kBuildRemakeKeysFound = 1;   /* the piece stands at its own key: taken on */
const int kBuildRemakeKeysShort = 2;   /* an answer short of complete: waits */
inline int BuildRemakeKeysAnswer(int mainNone, int hasOther, int otherFound, int otherNone)
{
    if (mainNone == 0) return kBuildRemakeKeysShort;
    if (hasOther == 0) return kBuildRemakeKeysNone;
    if (otherFound != 0) return kBuildRemakeKeysFound;
    return (otherNone != 0) ? kBuildRemakeKeysNone : kBuildRemakeKeysShort;
}
/* a re-make with no piece found when it runs - free-standing or inside a host: made only when the host's inside is loaded
   (BuildRemakeInsideOk) and every key asked answered a complete empty search (searchComplete); otherwise it waits, at most cap times
   (triesSoFar: the waits before this one), then is given up - never made beside a piece it could not rule out */
const int kBuildRemakeMake = 0, kBuildRemakeWait = 1, kBuildRemakeGiveUp = 2;
inline int BuildRemakeRunGate(int furn, int insideState, int searchComplete, int triesSoFar, int cap)
{
    if (BuildRemakeInsideOk(furn, insideState) != 0 && searchComplete != 0) return kBuildRemakeMake;
    return (triesSoFar + 1 >= cap) ? kBuildRemakeGiveUp : kBuildRemakeWait;
}
/* an entry ending now is a re-make ending without an outcome (remakeOut 0: not made, taken on, refused or faulted yet) - counted once,
   by whatever ends it (the road: refused; a REMOVE, a PLACE merged in, a restore taking it over, the world torn down: dropped). 1 = count */
inline int BuildRemakeEnds(int remake, int remakeOut)
{
    return (remake != 0 && remakeOut == 0) ? 1 : 0;
}
/* an area whose restore left rows not yet answerable waits for the event its rows wait for (BuildRebuildRestoreWake), then the zone scan
   walks it again - at most cap passes an area a load (passesDone: the walks already woken for it), then its remaining rows are
   counted and dropped. 1 = wait for the event */
inline int BuildRebuildRestoreRetry(int notYetRows, int passesDone, int cap) { return (notYetRows > 0 && passesDone < cap) ? 1 : 0; }
/* what a not-yet row waits for (waitKinds: the area's rows' kinds together). kBuildRrWaitTorn - a teardown answered the lookup: a zone
   taken apart since the pass (tornMoved, ZoneTornGen). kBuildRrWaitInside - a host that did not resolve, or whose inside is not
   loaded: walked again at each second of the zones tick (secondMoved) from the pass on, until the inside reads loaded.
   kBuildRrWaitSecond - a state that would not read: the next second of the zones tick. 1 = walk it again */
const int kBuildRrWaitTorn = 1, kBuildRrWaitInside = 2, kBuildRrWaitSecond = 4;
inline int BuildRebuildRestoreWake(int waitKinds, int tornMoved, int secondMoved)
{
    if ((waitKinds & kBuildRrWaitTorn) != 0 && tornMoved != 0) return 1;
    if ((waitKinds & (kBuildRrWaitInside | kBuildRrWaitSecond)) != 0 && secondMoved != 0) return 1;
    return 0;
}
/* after the owner is back: what lays the row's state. An own row has none to ask (this game is its writer); a copy row with a STATE
   already waiting keeps that one (it is newer); a copy whose owner's state was never applied here has none (owner only, counted); a
   WALL copy made a ruin here whose owner's last word was whole stays a ruin where the owner's STATE does not carry its health
   (wallRuinKept, as BdCopyDestroyedHere and BdApplyOne keep it); otherwise the owner's last STATE is queued on the STATE road. */
const int kBuildRrStateOwn = 0, kBuildRrStateQueue = 1, kBuildRrStateHeld = 2, kBuildRrStateUnknown = 3, kBuildRrStateWallKept = 4;
inline int BuildRebuildRestoreState(int isCopy, int ownerKnown, int stateHeld, int wallRuinKept)
{
    if (isCopy == 0) return kBuildRrStateOwn;
    if (stateHeld != 0) return kBuildRrStateHeld;
    if (ownerKnown == 0) return kBuildRrStateUnknown;
    if (wallRuinKept != 0) return kBuildRrStateWallKept;
    return kBuildRrStateQueue;
}
/* A BOUGHT HOUSE'S FURNITURE ON THE FRIEND'S GAME. The buyer's engine purchase (Building::buyMeCallback, build/decomp_7acb20.txt :166)
   runs 0x54EA40(interior, 1) (build/decomp_54ea40.txt) on the interior's piece set (+0xE8): a hand that no longer resolves is dropped
   from the set; a resolved piece is destroyed (GameWorld::destroy, reason "_destroyAllInternalBuildings") when its data type (vt+0x20)
   is not a building's (0) or the hand its vt+0x438 returns has type 0xB (no object); otherwise it stays. objType: vt+0x20's answer;
   stayHandType: that hand's type (-1 = not read). 1 = the engine destroys it. */
inline int BuildTeardownEngineDestroys(int objType, int stayHandType) { return (objType != 0 || stayHandType == 0xB) ? 1 : 0; }
/* the friend's teardown of one entry of that set (the friend's purchase replay). In this order: an entry that does not resolve is left alone (this
   game does not drop it from the set); an entry whose owner BEFORE the house's owner write was a player's faction or a stand-in is kept
   (ownerKeeps - the engine's Building::setFaction 0x556EC0 gives every building piece of the interior the new owner, so at the teardown
   genuine town furniture already reads as the buyer's stand-in; the owner is recorded before that write); an entry with no key cannot be shown to be nobody's and stays; an entry this mod has a record of is kept (recordMatch: a
   registry row by its live key or alias, a row within BoxKeyNearKey's window of it - a layout key drifts across a reload -, a row naming
   this house as its host whose piece is this one, or a listed copy); else the engine's own rule decides. Entries of every type are
   judged, as the engine judges them (an entry of another type reads objType != 0: the engine destroys it). engineDestroys: 1 the
   engine's rule destroys it, 0 it keeps it, -1 the rule's reads faulted (kept). */
const int kBuildTdDead = 2, kBuildTdKeepNoKey = 3, kBuildTdKeepPlayer = 4, kBuildTdKeepEngine = 5, kBuildTdRemove = 6, kBuildTdKeepOwner = 7, kBuildTdKeepNoErase = 8;
/* the live owner keeps the entry: its read faulted (readOk 0 - not shown to be nobody's) or the owner is a player's or a stand-in */
inline int BuildTeardownOwnerKeeps(int readOk, int ownerIsPlayers) { return (readOk == 0 || ownerIsPlayers != 0) ? 1 : 0; }
/* the owner rule on the owner recorded BEFORE the house's owner write: an entry not in that record (inRecord 0) is judged by the key
   rules only (0); else as BuildTeardownOwnerKeeps on the recorded read */
inline int BuildTeardownOwnerBeforeKeeps(int inRecord, int readOk, int ownerIsPlayers) { return inRecord == 0 ? 0 : BuildTeardownOwnerKeeps(readOk, ownerIsPlayers); }
inline int BuildTeardownPieceAct(int resolved, int ownerKeeps, int keyed, int recordMatch, int engineDestroys)
{
    if (resolved == 0) return kBuildTdDead;
    if (ownerKeeps != 0) return kBuildTdKeepOwner;
    if (keyed == 0) return kBuildTdKeepNoKey;
    if (recordMatch != 0) return kBuildTdKeepPlayer;
    return engineDestroys == 1 ? kBuildTdRemove : kBuildTdKeepEngine;
}
/* the purchase's room step (decomp_7acb20 :149-165) frees the user of every resolved BUILDING piece (the set node's hand type 0) before
   the teardown, with no owner test - a player's piece the keep rule keeps too; the friend's game frees the same users, so both games
   leave the same people in the same beds and cages. An unresolved entry (kBuildTdDead), an entry of another type and a second-set
   entry (kBuildTdKeepNoErase - the room step walks the first set only) are not touched */
inline int BuildTeardownFreesUsers(int act, int handType) { return (handType == 0 && act != kBuildTdDead && act != kBuildTdKeepNoErase) ? 1 : 0; }
/* THE BUYER'S OWN PURCHASE. Building::buyMeCallback (decomp_7acb20 :166) calls 0x54EA40(interior, 1) at one call site, after the
   house's owner write; that call is redirected so the buyer's game tears the house down by the friend's rule (the keep rule above),
   and both games keep the same pieces. The mod's teardown runs only when this game armed it for this purchase (armed: the buy
   detour, before the engine's purchase, with the owners recorded before its owner write), the call comes on the main thread, its
   interior is the armed house's and it asks the sale rule (saleRule: the call's second argument, 1 at this site); anything else
   gets the engine's own teardown. 1 = the mod's teardown runs. */
inline int BuildBuyerTeardownRuns(int armed, int onMain, int interiorMatches, int saleRule)
{
    return (armed != 0 && onMain != 0 && interiorMatches != 0 && saleRule != 0) ? 1 : 0;
}
/* the room step inside the mod's teardown: the friend's road runs it (no engine purchase runs there); on the buyer's road the
   engine's purchase has already run its own (decomp_7acb20 :149-165, every building piece of the set) before the call - not twice */
inline int BuildTeardownRoomStepHere(int buyerRoad) { return buyerRoad == 1 ? 0 : 1; }   /* road 2: the buyer's own restore (T-1335) - no engine purchase ran in this load: the room step runs */
/* the interior's second set (+0x128): 0x54EA40 erases every entry from it (0x574420) and destroys it, with no type test (decomp_54ea40
   :128-139). The same keep rule judges each entry (engineDestroys 1); one it would remove is removed only when the set's erase can be
   called (eraseRow) - a destroyed entry left in the set is a freed pointer there - else it is kept and counted (kBuildTdKeepNoErase) */
inline int BuildTeardownSet2Act(int act, int eraseRow) { return (act == kBuildTdRemove && eraseRow == 0) ? kBuildTdKeepNoErase : act; }
/* a teardown that could not judge the whole of a house that had pieces: the set held entries (setSize > 0) and the walk listed none,
   was cut at its cap or was torn by a zone change - counted, so a capped line cannot hide it */
inline int BuildTeardownBlind(long long setSize, int n, int truncated, int torn) { return (setSize > 0 && (n == 0 || truncated != 0 || torn != 0)) ? 1 : 0; }
/* WHOSE STAND-IN A COPY BELONGS TO: the slot of the player whose PLACE made it (or whose REMOVE names a piece with no row here). fromSlot:
   the sender's slot as recorded when the message arrived (>= 0 a slot; kBuildSenderSessionPeer = the old game-to-game link's peer before
   its slot was known, ownerroute.h kAddrOwnerSessionPeer; -1 none). linkPeerSlot: that link peer's slot now (-1 unknown). The answer is
   the slot whose stand-in owns the copy, or -1 = not known yet - the copy waits; it is never given another player's stand-in. */
const int kBuildSenderSessionPeer = -2;
inline int BuildSenderSlotNow(int fromSlot, int linkPeerSlot)
{
    if (fromSlot >= 0) return fromSlot;
    if (fromSlot == kBuildSenderSessionPeer && linkPeerSlot >= 0) return linkPeerSlot;
    return -1;
}
inline const char* BuildOwnerWord(int cls)
{
    switch (cls)
    {
    case kBuildLiveOwnNone: return "none";
    case kBuildLiveOwnMine: return "mine";
    case kBuildLiveOwnSender: return "peer";
    case kBuildLiveOwnSenderRecord: return "stand-in-record";
    case kBuildLiveOwnOtherStandIn: return "other-stand-in";
    case kBuildLiveOwnSenderList: return "copy-list";
    default: return "other";
    }
}

/* P87 fold 1 (H1): THE OWNER'S RE-SEND RULE for one tombstone. readBack: read from pp.build (the owner did not see the dismantle in this
   world - after a crash or an older save the piece may stand again); verified: the zone holding the key was walked in this world and
   the live lookup found no own piece there. ownHere: a live own row at the key (or the lookup found one); placePending: a PLACE for
   the key waits here (e.g. a hand-over to this game). */
const int kBuildTombSend = 0;   /* its REMOVE is owed again */
const int kBuildTombDrop = 1;   /* the tombstone ends (the owner's list wins) */
const int kBuildTombHold = 2;   /* kept in pp.build, not sent (yet) */
inline int BuildTombSendAct(int readBack, int verified, int ownHere, int placePending)
{
    if (ownHere != 0 || placePending != 0) return kBuildTombDrop;
    if (readBack != 0 && verified == 0) return kBuildTombHold;
    return kBuildTombSend;
}
/* P87 fold 1 (H1, H1b): verifying a read-back tombstone. tail: the row carried the fold-1 tail (else its furniture-ness is unknown -
   never verified). walked: the zone scan walked the key's zone (furniture: its host's) in this world. hostFound: furniture - its host
   resolved. look: 0 a complete search found nothing at the key, 1 one piece answered, 2 anything else. own / dismantling: that piece.
   1 = verified absent, 0 = not yet, -1 = an own piece stands at the key (the tombstone ends). */
inline int BuildTombVerifyVerdict(int tail, int furn, int walked, int hostFound, int look, int own, int dismantling)
{
    if (tail == 0 || walked == 0) return 0;
    if (furn != 0 && hostFound == 0) return 0;
    if (look == 0) return 1;
    if (look != 1) return 0;
    if (own == 0) return 1;
    return dismantling != 0 ? 0 : -1;
}
/* P87 fold 1 (H2): a REMOVE's nonce names the placement it ends; a row / pending PLACE / owed hand-over here of ANOTHER placement (both
   nonces known and different) is not touched. 1 = skip */
inline int BuildRemoveNonceSkip(unsigned int removeNonce, unsigned int hereNonce)
{
    return (removeNonce != 0 && hereNonce != 0 && removeNonce != hereNonce) ? 1 : 0;
}
/* P87 fold 1 (L1): a blueprint placed here in this world, never shared (its PLACE never went) and never worked on, leaves no copy
   anywhere - no tombstone. 1 = tombstone it */
inline int BuildTombNeeded(int sentOk, float progress, int placedHereThisWorld)
{
    return (sentOk == 0 && !(progress > 0.0f) && placedHereThisWorld != 0) ? 0 : 1;
}
/* P14 (mmo8b, mmo8-buildings-read.md 4b): an own piece found STANDING at a tombstone's key (the load / zone scan, the tombstone
   check's lookup). A READ-BACK tombstone (read from pp.build at this load: the dismantle was recorded after the save this zone came
   from, and its refund is already in the records) with the fold-1 tail: the piece is the save's copy of a piece the records retired -
   REMOVED here with no refund (kBuildTombKill), never registered, the tombstone kept. A live own row or a PLACE waiting at the key (a
   new placement), a tombstone of THIS world (the dismantle was seen here, so a piece standing is a new one) or a row written before
   P87 fold 1: the tombstone ends, as before (kBuildTombDrop). */
const int kBuildTombKill = 3;
inline int BuildTombSeenAct(int readBack, int tail, int ownRow, int placePending)
{
    if (ownRow != 0 || placePending != 0) return kBuildTombDrop;
    return (readBack != 0 && tail != 0) ? kBuildTombKill : kBuildTombDrop;
}
/* P14: one look at a queued removal (build.cpp BdTombKillDrain, the K2 safe point). zoneLoaded: the key's zone (furniture: its host's)
   is loaded NOW. look: 0 a complete search found nothing at the key, 1 one piece answered, 2 anything else. own: that piece is this
   game's faction's; listedCopy: the key is in this game's copy list (another game's copy); dismantling: its dismantled flag is set.
   tries: dismantle calls made; misses: looks that settled nothing. Wait (not counted), Miss (counted - GiveUp at maxTries), Call the
   engine's dismantle (mats zeroed first), Done (nothing stands: removed, or already gone), Cancel (not this game's own piece). */
const int kBuildTombKillWait = 0, kBuildTombKillMiss = 1, kBuildTombKillCall = 2, kBuildTombKillDone = 3, kBuildTombKillCancel = 4, kBuildTombKillGiveUp = 5;
/* P14 fold 1: keyExact - the FOUND piece's own key is the tombstone key (F2: the lookup's 0.1 u window can answer a rebuilt piece);
   rowNamed - a registry row (not removed) names the found piece's key; interior - the found piece has an interior (F4: excluded).
   Exclude: left standing (the tombstone ends, counted); Mismatch: not the retired piece - never touched. F3: our call took (dismantling
   after a call) - the engine frees it next frame, or a wall's deferred dismantle runs: Wait, never a miss. */
const int kBuildTombKillExclude = 6, kBuildTombKillMismatch = 7;
/* T-1372 attempt 2 (H-T1372-removal, F1045): a retired piece WITH AN INTERIOR is removed the game's own panel way - Building::confirmDismantle
   0x54F410 (answer 2: the interior's furniture through 0x54EA40(interior, 1), then the dismantle vt+0x248(FLT_MAX), then the mounted
   pieces) - for its first call (kBuildTombKillPanel); a retry after it is the bare dismantle (kBuildTombKillCall). panelAddr: the
   confirmDismantle address row reads non-zero - without it the piece is left standing as before (Exclude). innerReady: the interior's
   piece list was read whole this look (InteriorPieceIds: listed, not cut, not torn, every entry read, the walk generation unmoved) -
   until then a counted miss (GiveUp at maxTries leaves it standing). Both default 0: the fold-1 callers' answer (Exclude) is kept.
   Review fold (B1): innerForeign - a resolved entry of either interior set (+0xE8, +0x128) is not this game's own piece (another
   owner, a listed copy, another player's row, no key) - the panel's teardown never reads an owner (decomp_54ea40 :63-65, :128-139), so
   the piece is left standing as before (kBuildTombKillForeign: the tombstone ends, counted). (F2, re-check) furnPending - a furniture
   tombstone of this house still to settle (BuildTombFurnBlocks): a COUNTED miss (the zone walk may not have reached the furniture yet,
   so it may not be queued), so that piece is removed with its own refund first; at maxTries the give-up keeps the tombstone (N2). */
const int kBuildTombKillPanel = 8, kBuildTombKillForeign = 9;
/* T-1372 re-check (F2): does furniture tombstone t hold a house's panel step? furn: t is furniture; hostExact / hostNear: t's host key is
   the house's tombstone key, or within ItBoxResolve's window of it (BoxKeyNearKey - a furniture row's host key is built from another
   position read and can drift); queued: t's own removal is queued now; readBack / verified: t was read back at this load and is not yet
   verified absent (its piece may still stand, not yet reached by the zone walk); settled: t's removal was cancelled, given up or put off
   in this world (g_tombKillNo / g_tombKillLater). 1 = the panel waits for it. */
inline int BuildTombFurnBlocks(int furn, int hostExact, int hostNear, int queued, int readBack, int verified, int settled)
{
    if (furn == 0 || (hostExact == 0 && hostNear == 0)) return 0;
    if (queued != 0) return 1;
    if (settled != 0) return 0;
    return (readBack != 0 && verified == 0) ? 1 : 0;
}
inline int BuildTombKillStep(int zoneLoaded, int look, int own, int listedCopy, int dismantling, int tries, int misses, int maxTries,
                             int keyExact, int rowNamed, int interior, int panelAddr = 0, int innerReady = 0, int innerForeign = 0,
                             int furnPending = 0)
{
    if (zoneLoaded == 0) return kBuildTombKillWait;
    if (look == 0) return kBuildTombKillDone;
    if (look == 1 && (own == 0 || listedCopy != 0)) return kBuildTombKillCancel;
    if (look == 1 && dismantling != 0 && tries > 0) return kBuildTombKillWait;
    if (look != 1 || dismantling != 0) return (misses + 1 >= maxTries) ? kBuildTombKillGiveUp : kBuildTombKillMiss;
    if (keyExact == 0 || rowNamed != 0) return kBuildTombKillMismatch;
    if (interior != 0 && tries == 0)
    {
        if (panelAddr == 0) return kBuildTombKillExclude;
        if (furnPending != 0) return (misses + 1 >= maxTries) ? kBuildTombKillGiveUp : kBuildTombKillMiss;
        if (innerReady == 0) return (misses + 1 >= maxTries) ? kBuildTombKillGiveUp : kBuildTombKillMiss;
        if (innerForeign != 0) return kBuildTombKillForeign;
        return kBuildTombKillPanel;
    }
    return (tries >= maxTries) ? kBuildTombKillGiveUp : kBuildTombKillCall;
}

/* P14 fold 1 (manager decision 2026-09-30): THE ESCAPE LEDGER of a tombstone. A piece standing at reload proves its ground refund was
   never recorded (the piece and its refund live in the same zone file), so the P14 removal lets the engine's refund drop again - MINUS
   what provably landed in another record. Kept in the tombstone's pp.build '~t' row, in its STATE bytes (an empty STATE before; no
   reader before fold 1 reads them). refund: the items the engine's refund dropped (the landed ground key "G|<sid>@sx,sy@x,z,y" and the
   quantity; gen 1 = a later re-drop (the P14 removal's own) - its keys are matched, never owed). esc: what left the ground - our pickup
   {ref, qty, the squad record key, the seq that record must reach} or the other player's (kind 1: no record of ours to check).
   missed: refund items the ground road never keyed (> 64 per refund, not on the ground, no key); spill: items that landed in another
   zone than the piece's (that zone's file may hold them - inexact, counted). */
struct BuildLedgerRef { std::string sid, gkey; int qty; unsigned char gen; BuildLedgerRef() : qty(0), gen(0) {} };
struct BuildLedgerEsc { unsigned char ref, kind; int qty; unsigned long long seq; std::string rec; BuildLedgerEsc() : ref(0), kind(0), qty(0), seq(0) {} };
/* P14 fold 2 (D1/D3/D6): a material's line. unkeyed: first-drop items of it no refund entry could key (the entries or the row's bytes
   full) - owed all the same, their pickups matched by material and zone (an esc entry whose ref is kBuildLedgerMatRef + the line's
   index). blind: a pickup of it could not be recorded (the caps full) - its re-drop is REFUSED (a loss of what was not picked, never a
   duplicate of what was). */
struct BuildLedgerMat { std::string sid; int unkeyed; unsigned char blind; BuildLedgerMat() : unkeyed(0), blind(0) {} };
/* P14 fold 3 (N2): refuse - the whole ledger REFUSES every material's re-drop (a blind mark that did not fit, an encoding that failed):
   a loss of what was not picked, never a duplicate of what was */
struct BuildLedger { std::vector<BuildLedgerRef> refund; std::vector<BuildLedgerEsc> esc; std::vector<BuildLedgerMat> mat; int missed, spill; unsigned char refuse; BuildLedger() : missed(0), spill(0), refuse(0) {} };
/* P14 fold 2 (D3): refund entries scaled from 16 to 64, bounded by the row's 4000 bytes - every add checks the encoding still fits, so
   an encoding never fails for size; what does not fit is owed by material (BuildLedgerMat.unkeyed) */
const size_t kBuildLedgerMaxRef = 64, kBuildLedgerMaxEsc = 48, kBuildLedgerMaxRecs = 8, kBuildLedgerMaxBytes = 4000, kBuildLedgerMaxMat = 16;
/* P14 fold 5 (T732): kBuildLedgerEscHolder - a refund item our PUT handed to the area's holder, applied there: on the holder's
   ground (decision 255: the holder's ground is the truth, a stale save never re-owes it) */
const unsigned char kBuildLedgerEscOwn = 0, kBuildLedgerEscPeer = 1, kBuildLedgerEscHolder = 2;
const unsigned char kBuildLedgerMatRef = 0x80;
inline bool BuildLedgerEmpty(const BuildLedger& L) { return L.refund.empty() && L.esc.empty() && L.mat.empty() && L.missed == 0 && L.spill == 0 && L.refuse == 0; }
/* the base sid of a ground key "G|<sid>@..." (a hashed one "#xxxxxxxx" comes back as it is) */
inline bool BuildGroundKeySid(const std::string& gkey, std::string* sid)
{
    if (gkey.size() < 4 || gkey[0] != 'G' || gkey[1] != '|') return false;
    const size_t at = gkey.find('@', 2);
    if (at == std::string::npos || at == 2) return false;
    *sid = gkey.substr(2, at - 2);
    return true;
}
/* P14 fold 2 (D1): a ledger sid names a material slot's sid as it is, or hashed the way the ground key hashes a sid it cannot hold
   (groundkey.h GroundSidHash32: "#" + FNV-1a 32 in 8 lower-case hex) */
inline bool BuildLedgerSidIs(const std::string& led, const std::string& sid)
{
    if (led == sid) return true;
    if (led.size() != 9 || led[0] != '#' || sid.empty()) return false;
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < sid.size(); ++i) { h ^= (unsigned int)(unsigned char)sid[i]; h *= 16777619u; }
    const char* const hex = "0123456789abcdef";
    for (int i = 0; i < 8; ++i) if (led[1 + i] != hex[(h >> ((7 - i) * 4)) & 0xFu]) return false;
    return true;
}
inline int BuildLedgerFind(const BuildLedger& L, const std::string& gkey)
{
    for (size_t i = 0; i < L.refund.size(); ++i) if (L.refund[i].gkey == gkey) return (int)i;
    return -1;
}
inline void BuildLedPut8(std::string* o, unsigned int v) { o->push_back((char)(v & 0xFFu)); }
inline void BuildLedPut32(std::string* o, unsigned int v) { for (int i = 0; i < 4; ++i) o->push_back((char)((v >> (8 * i)) & 0xFFu)); }
inline void BuildLedPutStr(std::string* o, const std::string& s) { BuildLedPut8(o, (unsigned int)s.size()); o->append(s); }
/* "LG3" (P14 fold 3) | u32 missed | u32 spill | u8 flags (1 = refuse every re-drop) | u8 nRecs, nRecs x str | u8 nRef, nRef x (u8 gen, u32 qty, str gkey) | u8 nEsc, nEsc x (u8 ref,
   u8 kind, u32 qty, u32 seqLo, u32 seqHi, u8 rec index (0xFF none)) | u8 nMat, nMat x (str sid, u32 unkeyed, u8 blind); str = u8 length
   + bytes. "LG2" (P14 fold 2) is the same without the flags byte; "LG1" (fold 1) also without the material lines. false: too many rows (Raw: any size) */
inline bool BuildLedgerEncodeRaw(const BuildLedger& L, std::string* out)
{
    out->clear();
    if (L.refund.size() > kBuildLedgerMaxRef || L.esc.size() > kBuildLedgerMaxEsc || L.mat.size() > kBuildLedgerMaxMat) return false;
    std::vector<std::string> recs;
    for (size_t i = 0; i < L.esc.size(); ++i)
        if (!L.esc[i].rec.empty() && std::find(recs.begin(), recs.end(), L.esc[i].rec) == recs.end()) recs.push_back(L.esc[i].rec);
    if (recs.size() > kBuildLedgerMaxRecs) return false;
    out->append("LG3");
    BuildLedPut32(out, (unsigned int)(L.missed < 0 ? 0 : L.missed));
    BuildLedPut32(out, (unsigned int)(L.spill < 0 ? 0 : L.spill));
    BuildLedPut8(out, L.refuse != 0 ? 1u : 0u);   /* P14 fold 3 (N2): the fixed header - never short of room */
    BuildLedPut8(out, (unsigned int)recs.size());
    for (size_t i = 0; i < recs.size(); ++i) { if (recs[i].size() > 255) return false; BuildLedPutStr(out, recs[i]); }
    BuildLedPut8(out, (unsigned int)L.refund.size());
    for (size_t i = 0; i < L.refund.size(); ++i)
    {
        if (L.refund[i].gkey.size() > 255) return false;
        BuildLedPut8(out, L.refund[i].gen); BuildLedPut32(out, (unsigned int)L.refund[i].qty); BuildLedPutStr(out, L.refund[i].gkey);
    }
    BuildLedPut8(out, (unsigned int)L.esc.size());
    for (size_t i = 0; i < L.esc.size(); ++i)
    {
        const BuildLedgerEsc& e = L.esc[i];
        BuildLedPut8(out, e.ref); BuildLedPut8(out, e.kind); BuildLedPut32(out, (unsigned int)e.qty);
        BuildLedPut32(out, (unsigned int)(e.seq & 0xFFFFFFFFull)); BuildLedPut32(out, (unsigned int)(e.seq >> 32));
        size_t ri = 0xFF;
        for (size_t k = 0; k < recs.size(); ++k) if (recs[k] == e.rec) ri = k;
        BuildLedPut8(out, (unsigned int)ri);
    }
    BuildLedPut8(out, (unsigned int)L.mat.size());
    for (size_t i = 0; i < L.mat.size(); ++i)
    {
        if (L.mat[i].sid.size() > 255) return false;
        BuildLedPutStr(out, L.mat[i].sid); BuildLedPut32(out, (unsigned int)(L.mat[i].unkeyed < 0 ? 0 : L.mat[i].unkeyed));
        BuildLedPut8(out, L.mat[i].blind != 0 ? 1u : 0u);
    }
    return true;
}
/* false: too many rows or > 4000 bytes */
inline bool BuildLedgerEncode(const BuildLedger& L, std::string* out) { return BuildLedgerEncodeRaw(L, out) && out->size() <= kBuildLedgerMaxBytes; }
inline bool BuildLedgerFits(const BuildLedger& L) { std::string s; return BuildLedgerEncode(L, &s); }
/* a keyed refund entry or an escape entry leaves room for one material line (1 + 200 + 4 + 1 bytes), so a material can always be owed
   by material or marked blind once the keys are full */
const size_t kBuildLedgerMatReserve = 206;
inline bool BuildLedgerFitsKeyed(const BuildLedger& L) { std::string s; return BuildLedgerEncodeRaw(L, &s) && s.size() + kBuildLedgerMatReserve <= kBuildLedgerMaxBytes; }
inline int BuildLedgerMatFind(const BuildLedger& L, const std::string& sid)
{
    for (size_t i = 0; i < L.mat.size(); ++i) if (L.mat[i].sid == sid) return (int)i;
    return -1;
}
/* the material's line, made when missing; -1 = no room (16 lines, or the row's bytes) */
inline int BuildLedgerMatAt(BuildLedger* L, const std::string& sid)
{
    const int m = BuildLedgerMatFind(*L, sid);
    if (m >= 0) return m;
    if (L->mat.size() >= kBuildLedgerMaxMat || sid.empty() || sid.size() > 200) return -1;
    BuildLedgerMat x; x.sid = sid;
    L->mat.push_back(x);
    if (BuildLedgerFits(*L)) return (int)L->mat.size() - 1;
    L->mat.pop_back();
    return -1;
}
/* a refund item landed: merged into the entry of its key and gen (two items on one spot), else a new entry. 1 = keyed; 2 = P14 fold 2
   (D3): no room for its key (64 entries, or the row's bytes less one material line's room) - a first-drop item is owed by material (its pickups matched by material and
   zone); 0 = not in the ledger (missed: a re-drop, or no material line left) */
inline int BuildLedgerAddRefund(BuildLedger* L, const std::string& sid, const std::string& gkey, int qty, int gen)
{
    if (qty < 1) qty = 1;
    const unsigned char g = (unsigned char)(gen != 0 ? 1 : 0);
    for (size_t i = 0; i < L->refund.size(); ++i)
        if (L->refund[i].gkey == gkey && L->refund[i].gen == g) { L->refund[i].qty += qty; return 1; }
    if (L->refund.size() < kBuildLedgerMaxRef && gkey.size() <= 200 && sid.size() <= 200)
    {
        BuildLedgerRef r; r.sid = sid; r.gkey = gkey; r.qty = qty; r.gen = g;
        L->refund.push_back(r);
        if (BuildLedgerFitsKeyed(*L)) return 1;
        L->refund.pop_back();
    }
    if (g == 0)
    {
        const int m = BuildLedgerMatAt(L, sid);
        if (m >= 0) { L->mat[m].unkeyed += qty; return 2; }
    }
    L->missed += qty;
    return 0;
}
/* the escapes of one ref value (a refund index, or kBuildLedgerMatRef + a material index) */
inline int BuildLedgerEscOf(const BuildLedger& L, size_t ref)
{
    int s = 0;
    for (size_t i = 0; i < L.esc.size(); ++i) if ((size_t)L.esc[i].ref == ref) s += L.esc[i].qty;
    return s;
}
inline size_t BuildLedgerRecCount(const BuildLedger& L, const std::string& rec)
{
    std::vector<std::string> r;
    for (size_t i = 0; i < L.esc.size(); ++i)
        if (!L.esc[i].rec.empty() && std::find(r.begin(), r.end(), L.esc[i].rec) == r.end()) r.push_back(L.esc[i].rec);
    if (!rec.empty() && std::find(r.begin(), r.end(), rec) == r.end()) r.push_back(rec);
    return r.size();
}
/* one escape entry. 1 = an entry of its own; 2 = P14 fold 2 (D6): the entries or the bytes full - merged into an entry of the same ref,
   kind and record, carrying the EARLIER seq (P14 fold 3: it counts once the earlier write landed - inexact toward a LOSS of the later
   part, never a duplicate of the earlier one; counted by the caller); 0 = neither (a 9th squad record, or no entry to merge into) */
inline int BuildLedgerEscPut(BuildLedger* L, unsigned char refv, int take, unsigned char kind, const std::string& rec, unsigned long long seq)
{
    if (rec.size() <= 200 && BuildLedgerRecCount(*L, rec) <= kBuildLedgerMaxRecs && L->esc.size() < kBuildLedgerMaxEsc)
    {
        BuildLedgerEsc e; e.ref = refv; e.kind = kind; e.qty = take; e.seq = seq; e.rec = rec;
        L->esc.push_back(e);
        if (BuildLedgerFitsKeyed(*L)) return 1;
        L->esc.pop_back();
    }
    for (size_t i = 0; i < L->esc.size(); ++i)
        if (L->esc[i].ref == refv && L->esc[i].kind == kind && L->esc[i].rec == rec)
        {
            L->esc[i].qty += take;
            if (seq < L->esc[i].seq) L->esc[i].seq = seq;
            return 2;
        }
    return 0;
}
/* P14 fold 2 (D6): a pickup of the material could not be recorded - its re-drop is refused from now on. 1 = the material BLIND; 2 = P14
   fold 3 (N2): no line for it fits - the whole ledger REFUSES (the flag rides the fixed header) */
inline int BuildLedgerSetBlind(BuildLedger* L, const std::string& sid)
{
    const int m = BuildLedgerMatAt(L, sid);
    if (m < 0) { L->refuse = 1; return 2; }
    L->mat[m].blind = 1;
    return 1;
}
/* P14 fold 3 (N1): a first-drop item owed by MATERIAL (BuildLedgerAddRefund 2) that landed in another zone (spill) - its pickup is never
   charged (the by-material match takes only the piece's own zone), so the material is BLIND (no line: the whole ledger refuses).
   0 = nothing to do; else as BuildLedgerSetBlind */
inline int BuildLedgerSpillUnkeyed(BuildLedger* L, const std::string& sid, int kr, int spill)
{
    if (kr != 2 || spill == 0) return 0;
    return BuildLedgerSetBlind(L, sid);
}
/* P14 fold 4 (e5): 1 only when both sectors parsed and are equal - the zones SHOWN equal. A sector that does not parse counts as
   another zone (the by-material pickup match needs both, so that pickup is never charged: its material goes blind) */
inline int BuildZoneShownSame(int pieceOk, int psx, int psy, int groundOk, int gsx, int gsy)
{
    return (pieceOk != 0 && groundOk != 0 && psx == gsx && psy == gsy) ? 1 : 0;
}
/* P14 fold 4 (L2): a by-material pickup that more than one same-zone ledger has room for - which piece dropped it is not known, and
   charging one lets another re-drop in full (a duplicate): the material is BLIND in every candidate (each re-drop of it refused - a
   loss, never a duplicate). The count of candidates with no line for the mark (those ledgers REFUSE every re-drop) */
inline int BuildLedgerBlindAll(const std::vector<BuildLedger*>& cand, const std::string& sid)
{
    int ra = 0;
    for (size_t i = 0; i < cand.size(); ++i)
        if (cand[i] != 0 && BuildLedgerSetBlind(cand[i], sid) == 2) ++ra;
    return ra;
}
/* a pickup of qty from the ground key: spread over that key's entries with room left, so partial pickups add up to at most the refund's
   quantity. The amount recorded (0: not a refund item or its entries used up). *full: 0; 1 = a part could not be recorded - the material
   is BLIND (P14 fold 2, D6); 2 = merged into an entry (inexact, D6); 3 = not recorded and no line for the blind mark - the whole
   ledger REFUSES (P14 fold 3, N2) */
inline int BuildLedgerAddEsc(BuildLedger* L, const std::string& gkey, int qty, unsigned char kind, const std::string& rec, unsigned long long seq, int* full)
{
    int got = 0, merged = 0, blind = 0;
    if (full != 0) *full = 0;
    for (size_t i = 0; i < L->refund.size() && got < qty; ++i)
    {
        if (L->refund[i].gkey != gkey) continue;
        const int room = L->refund[i].qty - BuildLedgerEscOf(*L, i);
        if (room <= 0) continue;
        const int take = (qty - got < room) ? qty - got : room;
        const int p = BuildLedgerEscPut(L, (unsigned char)i, take, kind, rec, seq);
        if (p == 0) { blind = (BuildLedgerSetBlind(L, L->refund[i].sid) == 1) ? 1 : 3; break; }
        if (p == 2) merged = 1;
        got += take;
    }
    if (full != 0) *full = (blind != 0) ? blind : (merged != 0 ? 2 : 0);
    return got;
}
/* P14 fold 2 (D1/D3): what a material's unkeyed first drop still has room for (its unkeyed quantity less the pickups charged to it) */
inline int BuildLedgerMatRoom(const BuildLedger& L, const std::string& sid)
{
    const int m = BuildLedgerMatFind(L, sid);
    if (m < 0) return 0;
    const int r = L.mat[m].unkeyed - BuildLedgerEscOf(L, (size_t)kBuildLedgerMatRef + (size_t)m);
    return r > 0 ? r : 0;
}
/* P14 fold 2 (D1/D3): a pickup no refund entry keys, charged by MATERIAL to its unkeyed first drop (the caller matched the zone - an item
   of the same material there that was not the refund's is charged too: inexact toward a loss, bounded by the unkeyed quantity).
   The amount recorded; *full as BuildLedgerAddEsc */
inline int BuildLedgerAddEscMat(BuildLedger* L, const std::string& sid, int qty, unsigned char kind, const std::string& rec, unsigned long long seq, int* full)
{
    if (full != 0) *full = 0;
    const int room = BuildLedgerMatRoom(*L, sid);
    if (room <= 0 || qty <= 0) return 0;
    const int m = BuildLedgerMatFind(*L, sid);
    const int take = (qty < room) ? qty : room;
    const int p = BuildLedgerEscPut(L, (unsigned char)(kBuildLedgerMatRef + m), take, kind, rec, seq);
    if (p == 0) { L->mat[m].blind = 1; if (full != 0) *full = 1; return 0; }
    if (p == 2 && full != 0) *full = 2;
    return take;
}
/* at reload: our pickup counts only when the load's overlay APPLIED that squad record at or past the entry's seq (appliedSeq 0 = not
   applied); the other player's always counts (inexact: both games down within ~2 s of it) */
inline int BuildLedgerEscCounts(const BuildLedgerEsc& e, unsigned long long appliedSeq)
{
    if (e.kind == kBuildLedgerEscPeer || e.kind == kBuildLedgerEscHolder) return 1;   /* P14 fold 5: the holder's ground always counts */
    return (appliedSeq != 0 && e.seq != 0 && appliedSeq >= e.seq) ? 1 : 0;
}
/* P14 fold 2 (D2): at the first look after a load, our pickups that did NOT count leave the ledger - their items were re-dropped, and
   after the load the seq restarts from the highest on disk, so a later squad write would reach their seq and a second reload would
   subtract items that never reached the squad. ok[i] = BuildLedgerEscCounts of esc[i]. The number dropped */
inline int BuildLedgerPruneUncounted(BuildLedger* L, const std::vector<int>& ok)
{
    std::vector<BuildLedgerEsc> keep;
    int n = 0;
    for (size_t i = 0; i < L->esc.size(); ++i)
    {
        if (L->esc[i].kind == kBuildLedgerEscOwn && (i >= ok.size() || ok[i] == 0)) { ++n; continue; }
        keep.push_back(L->esc[i]);
    }
    L->esc.swap(keep);
    return n;
}
/* the first drop's quantity of a material: its gen 0 entries plus its unkeyed items (P14 fold 2, D3); a ledger sid may be the hashed form
   (D1). -1 = nothing names the material */
inline int BuildLedgerOwed(const BuildLedger& L, const std::string& sid)
{
    int s = 0, any = 0;
    for (size_t i = 0; i < L.refund.size(); ++i) if (L.refund[i].gen == 0 && BuildLedgerSidIs(L.refund[i].sid, sid)) { s += L.refund[i].qty; any = 1; }
    for (size_t i = 0; i < L.mat.size(); ++i) if (L.mat[i].unkeyed > 0 && BuildLedgerSidIs(L.mat[i].sid, sid)) { s += L.mat[i].unkeyed; any = 1; }
    return any != 0 ? s : -1;
}
inline int BuildLedgerBlindOf(const BuildLedger& L, const std::string& sid)
{
    for (size_t i = 0; i < L.mat.size(); ++i) if (L.mat[i].blind != 0 && BuildLedgerSidIs(L.mat[i].sid, sid)) return 1;
    return 0;
}
/* the counted escapes of a material (ok[i] = BuildLedgerEscCounts of esc[i]) - keyed and by material */
inline int BuildLedgerEscaped(const BuildLedger& L, const std::vector<int>& ok, const std::string& sid)
{
    int s = 0;
    for (size_t i = 0; i < L.esc.size() && i < ok.size(); ++i)
    {
        if (ok[i] == 0) continue;
        const size_t r = (size_t)L.esc[i].ref;
        const std::string* es = 0;
        if (r < (size_t)kBuildLedgerMatRef) { if (r < L.refund.size()) es = &L.refund[r].sid; }
        else if (r - (size_t)kBuildLedgerMatRef < L.mat.size()) es = &L.mat[r - (size_t)kBuildLedgerMatRef].sid;
        if (es != 0 && BuildLedgerSidIs(*es, sid)) s += L.esc[i].qty;
    }
    return s;
}
/* the delivered amount a material slot keeps before the engine's refund. The refund 0x29DBD0 drops (int)(delivered x a random share in
   [lo, 1.0]) items, at least 1 (build/decomp_29dbd0.txt) - so the rest owed (the first drop less the counted escapes) caps it: the
   re-drop never exceeds what was lost. P14 fold 2 (D1): owed unknown (-1: nothing names the material - a tombstone written before
   fold 1, a refund the ledger never saw): REFUSED - 0, a loss of that refund, never a duplicate of what was picked up unrecorded. */
inline float BuildLedgerKeep(float delivered, int owed, int escaped)
{
    if (!(delivered > 0.0f)) return delivered;
    if (owed < 0) return 0.0f;
    const int r = owed - escaped;
    if (r <= 0) return 0.0f;
    return ((float)r < delivered) ? (float)r : delivered;
}
/* P14 fold 2: a slot's kept amount - *refused when nothing names the material or it is blind (its pickups could not be accounted) */
inline float BuildLedgerKeepOf(const BuildLedger& L, const std::vector<int>& ok, const std::string& sid, float delivered, int* owed, int* escaped, int* refused)
{
    *owed = BuildLedgerOwed(L, sid);
    *escaped = BuildLedgerEscaped(L, ok, sid);
    *refused = (L.refuse != 0 || *owed < 0 || BuildLedgerBlindOf(L, sid) != 0) ? 1 : 0;   /* P14 fold 3 (N2): the whole-ledger refuse */
    if (!(delivered > 0.0f)) return delivered;
    return (*refused != 0) ? 0.0f : BuildLedgerKeep(delivered, *owed, *escaped);
}
/* P14 fold 2 (D4 / D7): what a P14 call does to a piece's mats first, and what a removal that ends with the piece standing (a faulted
   call, a give-up) does to them. A wall run's shared state is only read (0x29DBD0 never refunds it). Once a call's refund fired
   (refundsSeen > 0) a retry ZEROES them - the refund went, never twice - and a piece left standing keeps 0; before that a retry keeps
   the ledger's amount and a piece left standing keeps the amounts the removal set (P14 fold 3, D7: the ledger-reduced ones - its full
   delivered mats back would refund the first drop's picked-up items again at a later hand dismantle, a duplicate). */
const int kBuildTombMatsRead = 0, kBuildTombMatsLedger = 1, kBuildTombMatsZero = 2;
inline int BuildTombKillMats(int wall, long long refundsSeen)
{
    if (wall != 0) return kBuildTombMatsRead;
    return (refundsSeen > 0) ? kBuildTombMatsZero : kBuildTombMatsLedger;
}
inline int BuildTombKillRestore(int wall, long long refundsSeen)
{
    if (wall != 0) return kBuildTombMatsRead;
    return (refundsSeen > 0) ? kBuildTombMatsZero : kBuildTombMatsRead;   /* P14 fold 3 (D7): Read = leave the amounts the removal set */
}
/* P14 fold 4 (L1): a P14 call that must first set the mats (the ledger's amounts, or 0 once a refund fired) found them unreadable
   (nz < 0: the slots, or the tomb info missing) - the call would refund the FULL delivered mats while earlier picked-up items are
   in the squad (a duplicate), so it is never made: Retry at a later safe point; GiveUp (the piece stands, as a give-up does) once
   maxTries such looks went by. A wall run (Read) never refunds its shared state - called as before */
const int kBuildTombUnreadCall = 0, kBuildTombUnreadRetry = 1, kBuildTombUnreadGiveUp = 2;
inline int BuildTombMatsUnread(int mode, int nz, int unreadSoFar, int maxTries)
{
    if (nz >= 0 || mode == kBuildTombMatsRead) return kBuildTombUnreadCall;
    return (unreadSoFar + 1 >= maxTries) ? kBuildTombUnreadGiveUp : kBuildTombUnreadRetry;
}
struct BuildLedReader
{
    const std::string& s; size_t at; bool ok;
    explicit BuildLedReader(const std::string& in) : s(in), at(0), ok(true) {}
    unsigned int u8() { if (at + 1 > s.size()) { ok = false; return 0; } return (unsigned char)s[at++]; }
    unsigned int u32() { unsigned int v = 0; for (int i = 0; i < 4; ++i) v |= u8() << (8 * i); return v; }
    std::string str() { const unsigned int n = u8(); if (!ok || at + n > s.size()) { ok = false; return std::string(); } std::string r = s.substr(at, n); at += n; return r; }
private:
    BuildLedReader& operator=(const BuildLedReader&);
};
/* false: not a ledger (the empty STATE of a row before fold 1), or torn - *L is left empty. "LG1" (fold 1), "LG2" (P14 fold 2) and "LG3"
   (P14 fold 3: the flags byte - any non-zero value, an unknown one too, refuses every re-drop; a decoder before fold 3 reads no "LG3",
   so its tombstone has no ledger and refuses too) */
inline bool BuildLedgerDecode(const std::string& in, BuildLedger* L)
{
    *L = BuildLedger();
    if (in.size() < 3 || (in.compare(0, 3, "LG1") != 0 && in.compare(0, 3, "LG2") != 0 && in.compare(0, 3, "LG3") != 0)) return false;
    const bool v3 = (in[2] == '3'), v2 = (in[2] == '2' || v3);
    BuildLedReader r(in);
    r.at = 3;
    BuildLedger x;
    x.missed = (int)r.u32(); x.spill = (int)r.u32();
    if (v3) x.refuse = (unsigned char)(r.u8() != 0 ? 1 : 0);   /* P14 fold 3 (N2): an unknown flag value refuses, never reads as 0 */
    std::vector<std::string> recs;
    const unsigned int nr = r.u8();
    if (nr > kBuildLedgerMaxRecs) return false;
    for (unsigned int i = 0; i < nr && r.ok; ++i) recs.push_back(r.str());
    const unsigned int nf = r.u8();
    if (nf > kBuildLedgerMaxRef) return false;
    for (unsigned int i = 0; i < nf && r.ok; ++i)
    {
        BuildLedgerRef f; f.gen = (unsigned char)(r.u8() != 0 ? 1 : 0); f.qty = (int)r.u32(); f.gkey = r.str();
        if (!r.ok || f.qty < 0 || !BuildGroundKeySid(f.gkey, &f.sid)) return false;
        x.refund.push_back(f);
    }
    const unsigned int ne = r.u8();
    if (ne > kBuildLedgerMaxEsc) return false;
    for (unsigned int i = 0; i < ne && r.ok; ++i)
    {
        BuildLedgerEsc e; e.ref = (unsigned char)r.u8(); e.kind = (unsigned char)r.u8(); e.qty = (int)r.u32();
        const unsigned int lo = r.u32(), hi = r.u32();
        e.seq = ((unsigned long long)hi << 32) | (unsigned long long)lo;
        const unsigned int ri = r.u8();
        if (!r.ok || (e.ref < kBuildLedgerMatRef && (size_t)e.ref >= x.refund.size()) || (!v2 && e.ref >= kBuildLedgerMatRef) || e.qty < 0
            || e.kind > kBuildLedgerEscHolder || (ri != 0xFF && ri >= recs.size())) return false;
        if (ri != 0xFF) e.rec = recs[ri];
        x.esc.push_back(e);
    }
    if (v2)
    {
        const unsigned int nm = r.u8();
        if (nm > kBuildLedgerMaxMat) return false;
        for (unsigned int i = 0; i < nm && r.ok; ++i)
        {
            BuildLedgerMat m; m.sid = r.str(); m.unkeyed = (int)r.u32(); m.blind = (unsigned char)(r.u8() != 0 ? 1 : 0);
            if (!r.ok || m.sid.empty() || m.unkeyed < 0) return false;
            x.mat.push_back(m);
        }
        for (size_t i = 0; i < x.esc.size(); ++i)
            if (x.esc[i].ref >= kBuildLedgerMatRef && (size_t)(x.esc[i].ref - kBuildLedgerMatRef) >= x.mat.size()) return false;
    }
    if (!r.ok || r.at != in.size()) return false;
    *L = x;
    return true;
}

/* TEST LEVER ONLY (`buildtest into <host> <sid> <dx>,<dz>`): "<dx>,<dz>" read as two numbers, each within +/-kBuildIntoOffsetMax. 1 = read */
const double kBuildIntoOffsetMax = 5000.0;
inline int BuildIntoOffsetParse(const std::string& s, float* dx, float* dz)
{
    const size_t c = s.find(',');
    if (c == std::string::npos || c == 0 || c + 1 >= s.size() || s.find(',', c + 1) != std::string::npos) return 0;
    const std::string a = s.substr(0, c), b = s.substr(c + 1);
    char* e = 0;
    const double x = std::strtod(a.c_str(), &e);
    if (e == a.c_str() || *e != 0) return 0;
    const double z = std::strtod(b.c_str(), &e);
    if (e == b.c_str() || *e != 0) return 0;
    if (!(x >= -kBuildIntoOffsetMax && x <= kBuildIntoOffsetMax) || !(z >= -kBuildIntoOffsetMax && z <= kBuildIntoOffsetMax)) return 0;   /* NaN fails too */
    *dx = (float)x; *dz = (float)z;
    return 1;
}

/* TEST LEVER ONLY (`buildtest town <sid|key-substring>`, T-899): a registry row is named by the argument when its key contains it
   (1) or its sid is exactly it (2); 0 = not named. An empty argument names nothing. */
inline int BuildTownArgMatch(const std::string& arg, const std::string& key, const std::string& sid)
{
    if (arg.empty()) return 0;
    if (key.find(arg) != std::string::npos) return 1;
    if (sid == arg) return 2;
    return 0;
}

/* TEST LEVER ONLY (`buildtest upgrade|downgrade <key-substring>`, T-609 U): whether the lever calls the engine's upgrade / downgrade on
   a registered, loaded piece. Checked in this order; kBuildUpgradeGo = call it. The engine's own upgrade (1.0.65 0x2A49A0) does nothing
   when the 'upgrades to' record (vt+0x268; downgrade: vt+0x270) is 0. vt+0x268 (0x29BEE0) answers only when the record's 'upgrades to'
   list is not empty, the owner is a player faction and the target is in the research object's enabled-buildings set ([0x2133630]+0x38,
   +0x130, filled from finished research's 'enable buildings' - Research::setResearched 0x833680; read 2026-10-17). */
const int kBuildUpgradeGo = 0, kBuildUpgradeNotOwn = 1, kBuildUpgradeNoState = 2, kBuildUpgradeDismantled = 3, kBuildUpgradeRuin = 4,
          kBuildUpgradeNotComplete = 5, kBuildUpgradeNoRecord = 6, kBuildUpgradeWall = 7, kBuildUpgradeWallGroup = 8;
/* T-1294 (the upgrade case): a wall goes through only when the lever's line says `wall` (wallAsked) AND the wall leads no group
   (wallGroupCount, its +0x390 count, is 0 - a wall made at runtime starts ungrouped with a count of 0). The wall's upgrade override
   (1.0.65 0x2A5DE0, build/decomp_2a5de0.txt:13-45) upgrades each wall of that list first, then itself (0x2A49A0), then queues the wall's
   box on the deferred walking-map list (0x4D21F0) - with a count of 0 it changes this wall only, as a player's upgrade of a lone wall
   does. A count that does not read (-1) or is above 0 is refused. */
inline int BuildUpgradeLeverVerdict(int own, int isCopy, int stateRead, int complete, int dismantled, int ruin, int hasRecord, int isWall,
                                    int wallAsked = 0, int wallGroupCount = -1)
{
    if (own == 0 || isCopy != 0) return kBuildUpgradeNotOwn;
    if (isWall != 0 && wallAsked == 0) return kBuildUpgradeWall;   /* a wall's upgrade override also upgrades its linked walls, which may be other games' pieces */
    if (isWall != 0 && wallGroupCount != 0) return kBuildUpgradeWallGroup;
    if (stateRead == 0) return kBuildUpgradeNoState;
    if (dismantled != 0) return kBuildUpgradeDismantled;
    if (ruin != 0) return kBuildUpgradeRuin;
    if (complete == 0) return kBuildUpgradeNotComplete;
    if (hasRecord == 0) return kBuildUpgradeNoRecord;
    return kBuildUpgradeGo;
}
/* the refusal's reason, as the lever's line says it */
inline const char* BuildUpgradeLeverWhy(int verdict, int down)
{
    switch (verdict)
    {
    case kBuildUpgradeGo:          return "go";
    case kBuildUpgradeNotOwn:      return "not this game's own piece (a copy, or no own row)";
    case kBuildUpgradeNoState:     return "its construction state does not read";
    case kBuildUpgradeDismantled:  return "it is dismantled";
    case kBuildUpgradeRuin:        return "it is a ruin";
    case kBuildUpgradeNotComplete: return "it is not complete";
    case kBuildUpgradeWall:        return "it is a wall (its upgrade also changes the walls linked to it; `wall` at the end of the line lets a lone wall through)";
    case kBuildUpgradeWallGroup:   return "it is a wall that leads a group of walls, or its group count does not read (the upgrade would change the other walls too)";
    case kBuildUpgradeNoRecord:    return down != 0 ? "the game gave no 'downgrades to' target (no link in the record, or the owner is not a player faction)"
                                                    : "the game gave no 'upgrades to' target (no link in the record, the owner is not a player faction, or the research that enables the target is not done - team research finish <techSid>)";
    default:                       return "unknown verdict";
    }
}

/* T-609 U fix: what the main thread does with one upgrade / downgrade hook event. A piece's key holds its record id, so the engine's
   upgrade gives the SAME piece a new key: this game's own row moves to the new key as a REMOVE of the old key (the dismantle's road:
   the row marked removed, its tombstone, the REMOVE sent now or owed) and a new row at the new key whose PLACE + STATE are owed.
   A copy is never moved here - its owner's game re-keys it and tells this one. newRow: what the registry holds at the NEW key. */
const int kBuildRekeyNewNone = 0;      /* no row there, or a removed / dismantled one (replaced) */
const int kBuildRekeyNewCopy = 1;      /* another game's copy row */
const int kBuildRekeyNewOwn = 2;       /* a live own row */
const int kBuildRekeyNoKey = 0;        /* a key or the new record could not be read: nothing can be moved (dropped) */
const int kBuildRekeySame = 1;         /* the key did not change (no target record): nothing to do */
const int kBuildRekeyStale = 2;        /* queued in an earlier world: dropped */
const int kBuildRekeyNoRow = 3;        /* no row at the old key: not a piece the list tracks */
const int kBuildRekeyCopy = 4;         /* a copy, or a row this game does not own: its owner re-keys it */
const int kBuildRekeyGone = 5;         /* the row is already removed or dismantled */
const int kBuildRekeyOverCopy = 6;     /* the new key holds a copy row here: left as it is (dropped) */
const int kBuildRekeyRemoveOnly = 7;   /* the new key already holds a live own row: only the old key's REMOVE */
const int kBuildRekeyOwn = 8;          /* REMOVE the old key; a new row at the new key, its PLACE owed */
inline int BuildRekeyAct(int keysOk, int sameKey, int sameWorld, int rowExists, int own, int copyHere, int removed, int newRow)
{
    if (keysOk == 0) return kBuildRekeyNoKey;
    if (sameKey != 0) return kBuildRekeySame;
    if (sameWorld == 0) return kBuildRekeyStale;
    if (rowExists == 0) return kBuildRekeyNoRow;
    if (copyHere != 0 || own == 0) return kBuildRekeyCopy;
    if (removed != 0) return kBuildRekeyGone;
    if (newRow == kBuildRekeyNewCopy) return kBuildRekeyOverCopy;
    if (newRow == kBuildRekeyNewOwn) return kBuildRekeyRemoveOnly;
    return kBuildRekeyOwn;
}
/* 1 = the verdict leaves a changed key un-moved although it may be this game's own piece (the rekeyDropped counter) */
inline int BuildRekeyDropped(int act) { return (act == kBuildRekeyNoKey || act == kBuildRekeyStale || act == kBuildRekeyOverCopy) ? 1 : 0; }
/* the old key's REMOVE, as the rekey line says it */
const int kBuildRekeyRmSent = 0, kBuildRekeyRmOwed = 1, kBuildRekeyRmNotShared = 2, kBuildRekeyRmFailed = 3;
inline int BuildRekeyRemoveOutcome(int sent, int owed, int sentOk)
{
    if (sent != 0) return kBuildRekeyRmSent;
    if (owed != 0) return kBuildRekeyRmOwed;
    if (sentOk == 0) return kBuildRekeyRmNotShared;   /* the other game never got its PLACE: no copy there */
    return kBuildRekeyRmFailed;                       /* not encodable */
}
inline const char* BuildRekeyRemoveWord(int o)
{
    switch (o)
    {
    case kBuildRekeyRmSent:      return "removeSent";
    case kBuildRekeyRmOwed:      return "removeOwed";
    case kBuildRekeyRmNotShared: return "removeNotShared";
    case kBuildRekeyRmFailed:    return "removeFailed";
    default:                     return "remove?";
    }
}
/* the off-main queue: a fixed array holding n records; a push past cap (or with a bad count) is refused - the caller counts it dropped */
const int kBuildRekeyQueueCap = 256;   /* a long wall group upgraded off the main thread queues one pair per piece */
template <class Rec>
inline int BuildRekeyQueuePush(Rec* q, int* n, int cap, const Rec& r)
{
    if (q == 0 || n == 0 || cap <= 0 || *n < 0 || *n >= cap) return 0;
    q[*n] = r;
    ++*n;
    return 1;
}
/* T-903 (F1026): a furniture copy's search inside its host that came back truncated only because some handles on the host's interior
   list resolve to nothing. Such a handle may be this very piece not loaded yet, so by default the copy is held (never an empty twin).
   BuildDeadHandlesIgnore says when those handles count as NO PIECE and the search as complete-and-empty. The caller fills one
   BuildDeadHandleIn from a fresh read of the list; every BUILDING handle on it is exactly one of: good (resolved, its own ids equal the
   hand's, a key built, not at or near the wanted key), hit (resolved and keyed at or near the wanted key), dead (hand::asBuilding and
   hand::asObjectBase both answered null, neither faulted), other (anything else: a faulted lookup, unreadable or different ids, no key,
   or asObjectBase answering an object). */
struct BuildDeadHandleIn
{
    int listed, n, cut, torn;        /* the list's own size (-1 unreadable), entries read, the walk stopped early, a zone came down */
    int typed, good, hits, dead, other;
    int areaLoaded;                  /* the host's area reads fully loaded here */
    long long setAge;                /* safe points since this host's dead set (BuildDeadSetSig) was first seen unchanged; -1 = new or changed now */
    int regRow, copyListRow, removedMark;   /* this key: a registry row here, a copy-list ('~c') row, marked removed */
    int copyRowPlain;                /* T-1343: the copy-list row is a plain copy row - not marked removed here, not a hand-over this game made */
    int goodMayBeCopy;               /* T-1343 review: 'good' pieces on the host's list that may be this row's copy under another key (BuildGoodMayBeCopy) */
};
const long long kBuildDeadHandleWait = 30;   /* safe points the same dead set must persist across two searches */
/* 1 = count the dead handles as no piece (the copy may be made), only when ALL hold:
   (a) the list was read whole (listed >= 0, n == listed, cut 0, torn 0), every BUILDING handle is good or dead (no other) and none hit;
   (b) at least one dead handle (none dead = an ordinary search, not this rule's case);
   (c) the host's area reads fully loaded;
   (d) the same dead set was seen at least kBuildDeadHandleWait safe points before (setAge; a new or changed set is -1);
   (e) a fresh copy: no registry row, the key not marked removed, and no copy-list row other than a plain one (T-1343, below).
   0 = keep the hold.
   T-1343 (F1065) - a plain copy-list row no longer holds the copy. Three designs were compared, each by whether it decides from the
   real fact or a stand-in for it:
   (A) count a search cut short only by a stable dead set as complete for the row's empty-loads clearing (BuildCopyEvict) - a stand-in
       (a load count), and slow: the furniture stays missing for kBuildCopyEmptyLoads restarts;
   (B) void the row when the area was served from another game's area record (LOAD-OVERRIDE) - the load source is a real fact, but it
       answers a proxy question and goes stale: once this game saves that area (its copy still missing) the next restart loads its own
       file, the row reads trusted again and the hold is back; a quit without saving after the copy was made is not covered at all;
   (C) decide from the pieces actually there - PICKED. A copy-list row says "this game once made this copy"; it is this game's cache,
       not the world. What the world holds here is answered by (a)-(d): the area's saved pieces are all in (the engine's own loaded
       byte, (c)), every other piece on the host's list resolved and keyed with none at or near the key, and every handle that did not
       resolve answers null from BOTH engine lookups - no object exists for it - unchanged for kBuildDeadHandleWait safe points. A copy
       this game saved would be a live piece on that list (found, or a hit); a real copy present is still refused. As established
       games treat a stale client cache entry against the server's record: the world's record wins, the cache is refreshed from it
       (the made copy refreshes the row, BdCopyListNote). A row marked removed here (the reconcile removes a returning copy) or a
       hand-over this game made (PcrHandLoadPass owns it) is not plain and keeps the hold. */
inline int BuildDeadHandlesIgnore(const BuildDeadHandleIn& x)
{
    if (x.listed < 0 || x.n != x.listed || x.cut != 0 || x.torn != 0) return 0;                        /* (a) the list read whole */
    if (x.typed < 0 || x.good < 0 || x.hits != 0 || x.other != 0 || x.good + x.dead != x.typed) return 0;   /* (a) every handle good or dead */
    if (x.dead <= 0) return 0;                                                                         /* (b) */
    if (x.areaLoaded == 0) return 0;                                                                   /* (c) */
    if (x.setAge < kBuildDeadHandleWait) return 0;                                                     /* (d) (-1 fails too) */
    if (x.regRow != 0 || x.removedMark != 0) return 0;                                                 /* (e) */
    if (x.copyListRow != 0 && x.copyRowPlain == 0) return 0;                                           /* (e) T-1343: only a plain row is void */
    if (x.copyListRow != 0 && x.goodMayBeCopy != 0) return 0;   /* (e) T-1343 review: a saved copy can stand under another key (a key mismatch
                                                                   at its making, the town-version swap) and reads 'good', not a hit - a
                                                                   piece of the row's record anywhere in the house keeps the hold (-1 too) */
    return 1;
}
/* T-1343 review: one 'good' piece on the host's list, for BuildDeadHandleIn::goodMayBeCopy - 1 = it may be the row's own copy standing
   under another key: its record id equals the row's (sameSid 1) or did not read (-1), and its own key has no registry row and no
   copy-list row here (a piece this game already accounts for - another copy of the same type - is not it). The record id is per TYPE,
   so a house's own set-piece of the same type also counts (Inferred rare; the hold stays then, as before T-1343). */
inline int BuildGoodMayBeCopy(int sameSid, int ownRegRow, int ownCopyRow)
{
    if (sameSid == 0) return 0;
    if (ownRegRow != 0 || ownCopyRow != 0) return 0;
    return 1;
}
/* T-1343: the copy-list row's kind as BuildDeadHandleIn::copyRowPlain reads it - 1 = listed, not marked removed, not a hand-over */
inline int BuildCopyRowPlain(int listed, int removed, int handed)
{
    return (listed != 0 && removed == 0 && handed == 0) ? 1 : 0;
}
/* T-1240: which pending entry may use BuildDeadHandlesIgnore. A copy (restore 0): always (T-903). A restore keeps the hold - a dead
   handle can be this very saved piece, not loaded yet (never an empty twin) - except a piece another game wrote into this game's
   ownership and never made here, when ALL hold: made from its piece record (recMake 1); this load's own pp.build rows read
   (ownRowsState > 0; -1 = not yet, 0 = not read); no own pp.build row of the key read at this world load (ownRowKnown 0); not placed
   by this game (madeHere 0). 1 = the entry may use the rule; 0 = the hold stays. */
inline int BuildDeadHandlesEntryOk(int restore, int recMake, int ownRowsState, int ownRowKnown, int madeHere)
{
    if (restore == 0) return 1;
    if (recMake == 0) return 0;
    if (ownRowsState <= 0) return 0;
    if (ownRowKnown != 0) return 0;
    if (madeHere != 0) return 0;
    return 1;
}
/* T-1240 (T2039 LEG F): the own-rows answer BuildDeadHandlesEntryOk takes for a restore. pending 1 = this load's restore edge has
   not run (-1); rowsRead 1 = its own pp.build rows were read and handed (1); noRecord 1 = the whole restore was armed as "no-record" -
   the records store had no record yet, at a load's overlay or at a reset world's import: nothing of this game's own could be
   restored, so a handed restore is not one of its own rows (1); else 0 (own records off, unreadable, refused for identity, single,
   claim-busy). Only the dead-handle gate reads it: the make road's rows-read (piecerec::PieceOwnMakeMats) keeps PcaOwnRowsState. */
inline int BuildHandedRowsState(int pending, int rowsRead, int noRecord)
{
    if (pending != 0) return -1;
    if (rowsRead != 0) return 1;
    return noRecord != 0 ? 1 : 0;
}
/* the dead set's signature: each dead handle's (container, index, serial) as "c/i/s", sorted and joined by ';' - the order the list
   was read in does not change it; any handle added, gone or changed does */
inline std::string BuildDeadSetSig(const std::vector<unsigned int>& triples)
{
    std::vector<std::string> v;
    for (size_t i = 0; i + 2 < triples.size(); i += 3)
    {
        char b[48];
        std::sprintf(b, "%u/%u/%u", triples[i], triples[i + 1], triples[i + 2]);
        v.push_back(b);
    }
    std::sort(v.begin(), v.end());
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) { if (i != 0) s += ";"; s += v[i]; }
    return s;
}
/* one search's step of a host's remembered dead set: `*keptSig` / `*keptAt` (`*had` 0 = nothing remembered) are updated in place.
   Returns the set's age in safe points when the same set was remembered (now - keptAt, unsigned wrap allowed), else remembers this
   set at `now` and returns -1 (a changed set restarts the wait). An empty set is never remembered (-1, memory cleared). */
inline long long BuildDeadSetStep(int* had, std::string* keptSig, unsigned int* keptAt, const std::string& sig, unsigned int now)
{
    if (sig.empty()) { *had = 0; keptSig->clear(); *keptAt = 0; return -1; }
    if (*had != 0 && *keptSig == sig) return (long long)(unsigned int)(now - *keptAt);
    *had = 1; *keptSig = sig; *keptAt = now;
    return -1;
}

/* T-988 (owner decision 1099, 2026-10-25; fold 1): THE PIECE SELF-CHECK'S RULE - does a FINISHED player wall, gate or building have a
   physics body at all? Read from the piece by build.cpp (PcCheckPiece) with plain reads of the game's own records only; decided here
   from plain values.
   The physics: building +0x228 -> its physics list (SimplePhysXEntity count +0x20, array +0x28, as 0x5594A0 reads them); each
   entity's NxActor count +0x58 and array +0x60 (0x4CD3A0) - the reads probe P168 made in runs T1494-T1498 (an actor pointer changed
   across a reload in T1495). A body = a non-null actor pointer in that array; the actor itself is never read or called.
   Fold 1 (review 'T-988 SELF-CHECK REVIEW' HIGH): the check no longer calls into PhysXCore64. No concrete actor vtable is Confirmed,
   and the engine deletes physics objects on its physics thread while the check reads on the main thread: a dying actor carries an
   abstract base vtable whose slots are _purecall, an abort no guard catches. So the check can NO LONGER SEE an actor's shapes: an
   actor with no shape, and a finished piece whose every shape is still in the unfinished-piece groups 23-26 (0x55EC50; 05-findings
   'T-988 PHYSICS-GROUP READ'), both read as healthy here. A piece whose record's path mode is 0 (IGNORE) is never in the walk map by
   design and is not suspect. */
const int kPieceCheckFault = -1, kPieceCheckSkip = 0, kPieceCheckHealthy = 1, kPieceCheckNoBody = 2;
struct PieceCheckIn
{
    int stateOk, complete, dismantled, destroyed;   /* the piece's build state (stateOk 0 = unreadable) */
    int pathModeOk, pathMode;                       /* 1 the record's "path mode" was read, 0 the record has none, -1 unreadable,
                                                       kPcPathModeOverLimit (fold 2) its fields ran past kPcFieldWalkMax */
    int physOk;                                     /* 1 the physics list was read, 0 the piece has no physics list, -1 a read faulted */
    int entities, bodies;                           /* the list's entities; the non-null actors they list */
    PieceCheckIn() : stateOk(0), complete(0), dismantled(0), destroyed(0), pathModeOk(0), pathMode(0), physOk(0), entities(0), bodies(0) {}
};
/* kPieceCheckSkip not finished (or dismantled, or a ruin) - not checked; kPieceCheckFault a read failed or a count cannot be (no
   verdict); kPieceCheckHealthy; kPieceCheckNoBody no physics list, no entity, or no actor in any entity */
/* Fold 2 (re-check LOW): the record's "path mode" is found by walking its int fields (GameData +0x178, a hash map) with a count limit,
   so a corrupt map (a node chain that loops back) cannot hang the main thread. kPcFieldWalkMax = 4096: far above the few dozen int
   fields a building record carries, and 4096 short string compares cost microseconds even at kPcPiecesPerTick checks per tick. A walk
   that reaches the limit is kPcPathModeOverLimit, and the piece gets no verdict (kPieceCheckFault, counted in `faults`) */
const int kPcPathModeOverLimit = -2;
const size_t kPcFieldWalkMax = 4096;
/* 1 found (*v), 0 not among the fields, kPcPathModeOverLimit more than maxFields fields walked. `It` walks (name, int) pairs */
template <class It> int PieceCheckFindPathMode(It it, It end, size_t maxFields, int* v)
{
    size_t n = 0;
    for (; it != end; ++it)
    {
        if (++n > maxFields) return kPcPathModeOverLimit;
        if (it->first.size() == 9 && std::memcmp(it->first.c_str(), "path mode", 9) == 0) { *v = it->second; return 1; }
    }
    return 0;
}
inline int PieceCheckVerdict(const PieceCheckIn& x)
{
    if (x.stateOk == 0) return kPieceCheckFault;
    if (x.complete == 0 || x.dismantled != 0 || x.destroyed != 0) return kPieceCheckSkip;
    if (x.pathModeOk == kPcPathModeOverLimit) return kPieceCheckFault;   /* fold 2: a record whose fields could not all be walked */
    if (x.pathModeOk == 1 && x.pathMode == 0) return kPieceCheckHealthy;   /* IGNORE: never in the walk map */
    if (x.physOk < 0) return kPieceCheckFault;
    if (x.physOk == 0) return kPieceCheckNoBody;
    if (x.entities < 0 || x.bodies < 0) return kPieceCheckFault;   /* counts that cannot be */
    if (x.bodies == 0) return kPieceCheckNoBody;
    return kPieceCheckHealthy;
}
/* Fold 1 (review MED): the piece kinds the check judges. Read from the game's own type of the live object - the RTTI class of its
   vtable (build.cpp PcKindOf, as BdIsWall reads it) - and its layout-furniture link (Building +0x238 isFurnitureOf, kBFurnitureOf).
   The record's itemType (GameData +0x50) is BUILDING for every piece, so it cannot tell furniture apart. Exactly WallBuilding is a
   wall, DoorStuff / GatewayBuilding a gate, Building a building; anything else (UseableStuff and every class under it: storage,
   beds, crafting, turrets, lights; an unreadable class) and any layout furniture is another kind: counted apart, never a SUSPECT line */
const int kPcKindOther = 0, kPcKindWall = 1, kPcKindGate = 2, kPcKindBuilding = 3;
/* Fold 2 (re-check MED): the furniture PieceCheckKindOf takes is what the mod's own furniture tests name so - the live object's layout-
   furniture link (+0x238 kBFurnitureOf) OR its interior-object flag (+0xF8 kBInteriorObj, Building::isAnInteriorObject), as the
   setFaction hook's queue test reads them (build.cpp detour_setfaction: `BdPtrAt(b, kBFurnitureOf) != 0 || BdByteAtOr0(b,
   kBInteriorObj) != 0`), OR a registry row with a host, as the tombstone note reads it (build.cpp BdTombNote: `!r.hostKey.empty() ||
   r.hostForm != 0`). Furniture placed with only isIndoorsOf (no +0x238 link) is still furniture: never judged */
inline int PieceCheckFurniture(int furnitureLink, int interiorObject, const std::string& rowHostKey, int rowHostForm)
{
    return (furnitureLink != 0 || interiorObject != 0 || !rowHostKey.empty() || rowHostForm != 0) ? 1 : 0;
}
inline int PieceCheckKindOf(const char* cls, int layoutFurniture)
{
    if (layoutFurniture != 0 || cls == 0) return kPcKindOther;
    if (std::strcmp(cls, ".?AVWallBuilding@@") == 0) return kPcKindWall;
    if (std::strcmp(cls, ".?AVDoorStuff@@") == 0 || std::strcmp(cls, ".?AVGatewayBuilding@@") == 0) return kPcKindGate;
    if (std::strcmp(cls, ".?AVBuilding@@") == 0) return kPcKindBuilding;
    return kPcKindOther;
}
inline const char* PieceCheckKindWord(int k)
{
    switch (k)
    {
    case kPcKindWall: return "wall";
    case kPcKindGate: return "gate";
    case kPcKindBuilding: return "building";
    default: return "other";
    }
}
/* Fold 1 (review MED): a piece found with no body is checked again later - a wall's physics was first seen ~66 s after its area
   loaded (T-988 PHYSICS-GROUP READ E4) - and logs SUSPECT only when it still has none at its last re-check. `tries` = the re-checks
   already made. Done for any other verdict */
const int kPcNextDone = 0, kPcNextRecheck = 1, kPcNextSuspect = 2;
inline int PieceCheckNext(int verdict, int tries, int maxRechecks)
{
    if (verdict != kPieceCheckNoBody) return kPcNextDone;
    return (tries < maxRechecks) ? kPcNextRecheck : kPcNextSuspect;
}
/* Fold 1: the waiting checks - one per key, at most `cap`; TakeDue hands out at most `max` whose time came (the per-tick cursor), the
   oldest first. Times are a wrapping millisecond clock (GetTickCount) */
struct PieceCheckDue { std::string key; unsigned int due; int road; int tries; };
struct PieceCheckQueue
{
    std::vector<PieceCheckDue> q;
    std::set<std::string> keys;
    size_t cap;
    explicit PieceCheckQueue(size_t c) : cap(c) {}
    /* 1 queued, 0 already waiting (it keeps its first time and road) or no key, -1 the queue is full (not queued) */
    int Push(const std::string& key, unsigned int due, int road, int tries)
    {
        if (key.empty() || keys.count(key) != 0) return 0;
        if (q.size() >= cap) return -1;
        PieceCheckDue d;
        d.key = key; d.due = due; d.road = road; d.tries = tries;
        q.push_back(d);
        keys.insert(key);
        return 1;
    }
    int Waiting(const std::string& key) const { return keys.count(key) != 0 ? 1 : 0; }
    size_t TakeDue(unsigned int now, int max, std::vector<PieceCheckDue>* out)
    {
        out->clear();
        for (size_t i = 0; i < q.size() && (int)out->size() < max; )
        {
            if ((int)(now - q[i].due) < 0) { ++i; continue; }
            out->push_back(q[i]);
            keys.erase(q[i].key);
            q.erase(q.begin() + (std::ptrdiff_t)i);
        }
        return out->size();
    }
    void Clear() { q.clear(); keys.clear(); }
};
/* T-1294 design A: the walking-map steps the engine runs for its own building actions, owed by the mod's versions of them. The
   engine's step is its deferred walking-map list (*BuildPreviewListGlobal): 0x4D21F0 (list, building +0x27C box) - what the
   placement confirm 0x4DA6F0 does for a piece standing at once (build threshold 0) and the wall upgrade 0x2A5DE0 does - and
   0x4D2340 (list, building, 0) - what the purchase 0x7ACB20 does for a building whose interior changed: its 'Job added: Interior'
   (drained by 0x4E3690 -> 0x3ABDE0 -> 0x3C6E90). Both lists are drained once physics settles. A wall the mod finishes goes through
   its own vt+0x230 (0x5594A0) with kWallFinishAmount: finite and under 9999, so its threshold crossing flips its physics group and
   posts its own 'Job added: Partial' (FLT_MAX skips both). Which steps one mod event owes (kWmBox | kWmInterior):
     a piece made / restored / made for a kept row (an upgrade's replacement on another game is a copy made): its box when it
       stands solid now (complete or past its build threshold - below it the engine leaves it out of the walking map), its
       interior when it has one;
     finished, or raised past its threshold by a mod write: its box, and its interior when it has one;
     removed (asked before the engine's dismantle runs - the box is copied, so the piece may be gone when the list drains): its box.
   Furniture and anything unknown: nothing (the engine queues furniture only through its host's interior). */
const int kWmCopyMade = 1, kWmCopyRestored = 2, kWmKeptMade = 3, kWmFinished = 4, kWmThreshold = 5, kWmRemoved = 6, kWmBodyArrived = 7;
const int kWmBox = 1, kWmInterior = 2;
const float kWallFinishAmount = 999.0f;     /* 0x5594A0: the crossing is skipped for FLT_MAX, its job for amounts >= 9999 */
const float kWallJobAmountLimit = 9999.0f;
/* fold 1 (review M3 / L4): a WALL finished or raised past its threshold by the mod goes through its own threshold crossing
   (BdCompleteNowPod / BdWallCrossPod -> 0x5594A0), which flips its physics group and posts its own job at that moment - nothing more
   is queued (a box queued earlier could drain before the flip). A wall copy made counts as solid only when made complete (its group is
   fixed then, not excluded). kWmBodyArrived (fold 2, review M2): the engine gave a waiting piece its physics body (createPhysical, see
   BuildWalkMapBodyWait) - its box (and interior) again, a wall too (the earlier job ran without its body). */
inline int BuildWalkMapWhat(int event, int solid, int hasInterior, int furniture, int wall = 0)
{
    if (furniture != 0) return 0;
    const int in = (hasInterior != 0) ? kWmInterior : 0;
    if (wall != 0 && (event == kWmFinished || event == kWmThreshold)) return 0;
    switch (event)
    {
    case kWmBodyArrived: return kWmBox | in;
    case kWmCopyMade: case kWmCopyRestored: case kWmKeptMade: return ((solid != 0) ? kWmBox : 0) | in;
    case kWmFinished: case kWmThreshold: return kWmBox | in;
    case kWmRemoved: return kWmBox;
    default: return 0;
    }
}
/* T-1309 (.modding/05-findings.md F-T1309-GATEISLAND): "when it has one" above means the engine itself would give the building an
   interior walking map - the area load's test (1.0.65 0x3ABF00 and its regenerate 0x3AB700; 1.0.68 0x3AC040 / 0x3AB840, both
   byte-identical): vt+0x20 answers 0, the interior pointer +0x1F0 is set, vt+0x2C8 answers 0 (WallBuilding answers non-zero),
   vt+0x3D8 Building::isGate answers 0 (GatewayBuilding 0x57B400 answers itself) and the interior's +0x2C byte is 0. A gate or a
   wall has an interior pointer but no interior walking map in the engine; queuing one built an unconnected piece on the teammate's
   game (T2273: the gate's unlock stand point snapped onto it and no path existed). read 0 = a fault or an unreadable value: no. */
const size_t kEngIntVtKind = 0x20, kEngIntVtWallPart = 0x2C8, kEngIntVtGate = 0x3D8, kEngIntFlag = 0x2C;
inline int BuildEngineGivesInterior(int read, int kind, int interiorPtr, int wallPart, int gate, int interiorFlag)
{
    return (read != 0 && kind == 0 && interiorPtr != 0 && wallPart == 0 && gate == 0 && interiorFlag == 0) ? 1 : 0;
}
/* T-1426 (.modding/05-findings.md F1221 / F1245): furniture (a hosted piece) gets nothing from BuildWalkMapWhat - the engine queues it
   only through its HOST. The placement confirm's build step 0x4D6810 (decomp_4d6810:365-373) reads each piece's host hand (vt+0x1D8),
   and unless its type is none (0xB) puts that hand on the list's interior jobs (+0x58, the list 0x4D2340 appends a building's own hand
   to). So a furniture piece the mod makes, re-makes from a record, makes for a kept row, finishes or removes owes its host's interior
   walking map - when the engine's own test (BuildEngineGivesInterior) gives the host one. 1 queue the host's interior; 0 not owed (not
   furniture, or another event: a threshold crossing or a body's arrival - furniture never waits for a body); kWmHostNone the piece
   names no host building; kWmHostNoInterior the host gets no interior walking map. */
const int kWmHostNone = -1, kWmHostNoInterior = -2;
inline int BuildWalkMapHostInterior(int event, int furniture, int hostKnown, int hostInterior)
{
    if (furniture == 0) return 0;
    if (event != kWmCopyMade && event != kWmCopyRestored && event != kWmKeptMade && event != kWmFinished && event != kWmRemoved) return 0;
    if (hostKnown == 0) return kWmHostNone;
    if (hostInterior == 0) return kWmHostNoInterior;
    return 1;
}
/* the piece stands solid in the walking map: finished, or its progress at or past its build threshold (a share of needed) */
inline int BuildWalkMapSolid(int complete, float progress, float needed, float threshold)
{
    if (complete != 0) return 1;
    if (!(needed > 0.0f)) return 0;
    if (!(threshold > 0.0f)) return 1;   /* threshold 0: solid from placement (the confirm's own test) */
    return (progress >= needed * threshold) ? 1 : 0;
}
/* a wall counts as solid for a copy made only when complete (review M3); any other piece by BuildWalkMapSolid */
inline int BuildWalkMapSolidFor(int wall, int complete, float progress, float needed, float threshold)
{
    if (wall != 0) return (complete != 0) ? 1 : 0;
    return BuildWalkMapSolid(complete, progress, needed, threshold);
}
/* fold 1 (review H1): the confirm sets the list's state +0x38 = 1 ('build the confirmed pieces' - the drain runs 0x4D6810 only on 1)
   and the queue calls set 2 - a 1 found before the mod's queue is put back after it */
inline int BuildWalkMapStateAfter(int before, int after) { return (before == 1) ? 1 : after; }
/* fold 1 (review M1): a queue made in build mode (+0x28 != 0) leaves the state as it was; leaving build mode by cancel sets nothing.
   At the next tick out of build mode with such a queue waiting and the state 0, the mod sets 2 (what the queue call itself sets
   outside build mode) */
inline int BuildWalkMapDueNow(int heldPending, int buildMode, int state) { return (heldPending != 0 && buildMode == 0 && state == 0) ? 1 : 0; }
/* fold 1 (review H2): a write that raises a wall from below its threshold (needed x threshold) to at or above it goes through the
   wall's own vt+0x230 (0x5594A0) so its crossing runs: the progress is first written to just below the line (BuildWallCrossPre),
   then vt+0x230 with small finite amounts (BuildWallCrossStep, x4 each try; 0x558B10 scales them by the piece's rate +0x18C and the
   game's settings) until the line is passed; then the write lands its own value. Rate 0 or below: 0x558B10 completes the piece on
   any finite amount - no crossing call (0 step) */
const float kWallCrossEps = 0.002f;   /* a share of needed */
const int kWallCrossTries = 6;
inline int BuildWallCrossNeeded(int wall, float p0, float p1, float needed, float threshold)
{
    if (wall == 0 || !(needed > 0.0f) || !(threshold > 0.0f)) return 0;
    const float line = needed * threshold;
    return (p0 < line && p1 >= line) ? 1 : 0;
}
/* T-1294 test tool (T2225 premise): `bldstate <key> complete` - the trailing word asks the read to answer an error when the piece is not
   finished, so a leg check (lever-ok) stops the run. 1 = asked (*key = the argument without it), 0 = a plain read (*key = arg) */
inline int BuildStateWantsComplete(const std::string& arg, std::string* key)
{
    const std::string w = " complete";
    if (arg.size() > w.size() && arg.compare(arg.size() - w.size(), w.size(), w) == 0)
    {
        size_t e = arg.size() - w.size();
        while (e > 0 && arg[e - 1] == ' ') --e;
        *key = arg.substr(0, e);
        return key->empty() ? 0 : 1;
    }
    *key = arg;
    return 0;
}
inline float BuildWallCrossPre(float needed, float threshold) { const float v = needed * threshold - needed * kWallCrossEps; return v > 0.0f ? v : 0.0f; }
/* fold 3 (T2222: B's W2 copy reached 1.00/1.00 by the helper's cap write after the engine's step ran on a raised needed - from 0.00,
   past its line, with no crossing - so no 'Job added: Partial' on B). The progress a crossing starts from: a replay starts just below
   the line; a caller whose own write follows the engine's step on a raised needed (BdHelpAfter - the live progress there is the step's
   raw result, not a share of the real needed) passes the progress read before that step (from >= 0); otherwise the live progress.
   *writePre = 1: the progress is first written to just below the line (a replay, a `from`, or a live value below it) */
inline float BuildWallCrossFrom(int replay, float from, float live, float pre, int* writePre)
{
    if (replay != 0) { *writePre = 1; return pre; }
    if (from >= 0.0f) { *writePre = 1; return from; }
    *writePre = (live < pre) ? 1 : 0;
    return live;
}
inline float BuildWallCrossStep(float needed, float rate, int tryNo)
{
    if (!(rate > 0.0f) || !(needed > 0.0f) || tryNo < 0 || tryNo >= kWallCrossTries) return 0.0f;
    float a = needed * kWallCrossEps * 2.0f / rate;
    for (int i = 0; i < tryNo; ++i) a *= 4.0f;
    return a;
}
/* fold 2 (review M2, H2(b)): Building::createPhysical 0x55FF50 (vt+0x200; 1.0.68 0x5609E0) makes and fills a piece's physics list
   +0x228 - the engine step that gives it its body (0x5594A0 crosses a wall's line only with that list set). A piece the mod queues
   while its list is still empty (bodyList 0; 1 set, -1 unreadable) waits, keyed by the piece, and is queued again from that hook
   (kWmBodyArrived) - no timer. Removals, the arrival itself and furniture never wait */
const int kWmBodyWaitCap = 384;   /* T-1389: 256 for the walk map plus the piece check's own waits (kPcBodyWaitPcCap 128) */
inline int BuildWalkMapBodyWait(int event, int bodyList, int furniture)
{
    if (bodyList != 0 || furniture != 0) return 0;
    return (event == kWmCopyMade || event == kWmCopyRestored || event == kWmKeptMade || event == kWmFinished || event == kWmThreshold) ? 1 : 0;
}
/* T-1428 (.modding/05-findings.md F1226 / F1223). The building's box +0x27C (an Ogre::Aabb: centre x,y,z then half-size x,y,z) is the
   engine's 'nothing' box (half-size minus infinity, decomp_55c000:106-112) until its models are loaded: each model part that finishes
   loading merges its world box in (0x5580B0 -> 0x546980), and the whole-box recount 0x5489F0 does the same - both only once the
   physics holder +0x228 is made, and createPhysical writes no box. So a holder set does not mean a box filled in. The engine's
   job-adding step 0x3C68E0 (decomp_3c68e0:18-20) refuses a box whose half-size is not above zero on all three axes: a box queued
   before the models load cuts nothing. A NaN or minus infinity half-size is not real */
inline int BuildWalkMapBoxReal(const float* box)
{
    return (box != 0 && box[3] > 0.0f && box[4] > 0.0f && box[5] > 0.0f) ? 1 : 0;
}
/* the engine's own asks with a building's box (setDestroyed 0x5567F0 [1.0.68 0x557780]; the upgrade 0x2A49A0 / downgrade 0x2A4D70)
   grow its half-size +0x288 by the constant at 0x1684340 [1.0.68 0x1685340] before the job and put it back after: 5.0 in both builds
   (read from both exes, T-1428). The mod queues a grown COPY (the queue 0x4D21F0 copies the box) and leaves the building's own alone */
const float kWmBoxNudge = 5.0f;
inline void BuildWalkMapBoxNudged(const float* in, float* out)
{
    for (int i = 0; i < 3; ++i) out[i] = in[i];
    for (int i = 3; i < 6; ++i) out[i] = in[i] + kWmBoxNudge;
}
/* T-1428: a walking-map ask that owes the piece's box waits while the box is not real (BuildWalkMapBoxReal) - in the walk-map wait
   (kWmBodyWaitCap), re-checked each tick beside the T-1389 bodies watch and at the createPhysical arrival - and asks (kWmBodyArrived)
   the first time it is real. A removal never waits (its box is copied before the dismantle). Replaces the holder test
   (BuildWalkMapBodyWait) at the ask: the holder is made at creation, the box later. Fold 1: boxReal is BuildWalkMapBoxReady (the grown
   box real and every model part loaded), and the wait is in its own list (BdBoxWait), re-checked each tick */
inline int BuildWalkMapBoxWait(int event, int what, int boxReal)
{
    if (event == kWmRemoved || (what & kWmBox) == 0) return 0;
    return (boxReal != 0) ? 0 : 1;
}
/* what is queued now. T-1428 fold 1 (review 4): while the box waits NOTHING goes now - the interior (keyed on the building,
   decomp_4d2340) goes with the box at the re-ask, after the outdoor cut, in the engine's own order (its asks queue the box, then the
   interior); one interior ask per event, not two. A wait that never gets a real box still sends its interior at the time cap
   (BuildWalkMapBoxDue kWmBoxNoBox) */
inline int BuildWalkMapQueueNow(int what, int boxWait) { return (boxWait != 0) ? 0 : what; }
/* T-1428 fold 1 (review 3): the engine tests the GROWN box (its asks grow +0x288 by kWmBoxNudge before the job-adding test 0x3C68E0,
   decomp_3c68e0:18-20), so the mod tests the grown copy it queues: a box flat on one side (half-size 0 there) counts, as the engine's
   would; the 'nothing' box (minus infinity) and NaN stay not real */
inline int BuildWalkMapBoxGrownReal(const float* box)
{
    if (box == 0) return 0;
    float g[6];
    BuildWalkMapBoxNudged(box, g);
    return BuildWalkMapBoxReal(g);
}
/* T-1428 fold 1 (review 1): the building's count of model parts still loading, +0x230 (an int). The part maker 0x55D6C0 raises it
   for each model it asks to load (0x55D8F8; 1.0.68 0x55E388); the part-loaded step 0x5580B0 lowers it (0x5581D9; 1.0.68 0x558C69),
   merges that part's box into +0x27C, and at 0 sets +0x234 and calls vt+0x490; the constructors 0x55C000 / 0x55C310 and 0x561F10
   put it to 0. The box grows one part at a time, so the ask waits for the box real AND the count at 0 (every part in). The count
   can stay above 0 for good: 0x5580B0 lowers it only for a part it finds in the holder, so a load that never comes back, or comes back
   after the holder was rebuilt, leaves it raised. Give-up: after kWmBoxWaitMs the ask goes with the box it has (some cut beats none),
   or, with no real box, its interior alone - logged once */
const unsigned int kWmBoxWaitMs = 60000;
const int kWmBoxKeep = 0, kWmBoxAsk = 1, kWmBoxAskPartial = 2, kWmBoxNoBox = 3;
inline int BuildWalkMapBoxReady(int grownReal, int partsLoading) { return (grownReal != 0 && partsLoading <= 0) ? 1 : 0; }
inline int BuildWalkMapBoxDue(int grownReal, int partsLoading, unsigned int waitedMs)
{
    if (BuildWalkMapBoxReady(grownReal, partsLoading) != 0) return kWmBoxAsk;
    if (waitedMs < kWmBoxWaitMs) return kWmBoxKeep;
    return (grownReal != 0) ? kWmBoxAskPartial : kWmBoxNoBox;
}
/* T-1428 fold 1 (review 2): the box waits have their own list, growable - no cap, no drop (a joiner loading a large base may hold
   hundreds); the shared kWmBodyWaitCap list keeps only the waits the createPhysical hook must see (a wall's crossing, the piece check).
   A box wait for a piece whose wall crossing already waits is not added (the arrival asks it), and a crossing wait added later takes
   the piece's box wait over */
inline int BuildWalkMapBoxWaitKeep(int crossWaits) { return (crossWaits != 0) ? 0 : 1; }
/* a waiting ask gives up (logged once with its key): its area ended (area unload: the piece's own area, or any area for a wait with no
   key - its area is not known; world left: every wait), or the address now holds another object */
const int kWmGiveUpArea = 1, kWmGiveUpWorld = 2, kWmGiveUpOther = 3;
inline int BuildWalkMapWaitAreaEnds(int keyKnown, int keySector, const unsigned char* left, int cells)
{
    if (left == 0) return 1;
    if (keyKnown == 0 || keySector < 0 || keySector >= cells) return 1;
    return (left[keySector] != 0) ? 1 : 0;
}
inline const char* BuildWalkMapGiveUpWord(int why)
{
    switch (why)
    {
    case kWmGiveUpArea: return "its area ended";
    case kWmGiveUpWorld: return "the world was left";
    case kWmGiveUpOther: return "another object at that address";
    default: return "?";
    }
}
/* T-1428 attempt 2 (.modding/06-hypotheses.md H-T1428-nocut; F1339 / F1340): a piece the mod makes from its record into an area that
   is already loaded (the restore road, a re-make, a world-reset kept row) is never cut out of the area's OUTDOOR walking map - the
   engine cuts a building out only at its own placement confirm (0x4DA6F0) or when its own area load finds the area's buildings hash
   changed (0x3ADF30 -> Full). So after an area's restored pieces are made and their models loaded, the mod asks the engine's own
   whole-area rebuild ONCE for that area: 1.0.65 0x3AB700 (1.0.68 0x3AB840; rows ZoneWalkMapRebuild, NavMeshManagerPtr
   0x2133560 / 0x21345C0), called as the engine's 'Fixed broken save data' key (0x7875C0) and regenerate-zone (0x76B850) call it - on
   the main thread, with the ZoneManager's map entry for the area (0xA07C10) - Full for the area plus every building's inside job.
   0x3AB700 itself first asks 0x3BF360(walkMapJobs = navMgr+0x290, zoneMap) and does nothing when it answers 1: the job running now
   (+0xE8) is this area's with type (+0x58 & 7) 1, or a QUEUED job (list +0x88, next +0x60) is this area's with type not 1.
   T-1428 fold 2 (F1361 SHOULD 2): read from the decompile, build/t1428c/dc_3bf360.txt (1.0.65 0x3BF360; 1.0.68 0x3BF4A0 has the same
   bytes) - running job *(jobs+0xE8), job key at +0, the type dword +0x58 tested as (byte & 7) == 1, the queue *(jobs+0x88), next
   +0x60; no lock taken (only the adders 0x3C66E0 / 0x3C6E90 take +0x98, so the mod reads unlocked as the engine's own reader does).
   The job object is 0x3BFB50's 0x68 bytes (dc_3bfb50.txt; 1.0.68 0x3BFC90): key +0, box +0x30, +0x58 zeroed, next +0x60; the type
   is written by its adder: Full 0 (0x3C66E0), Partial 1 (0x3C68E0), Interior 3 (0x3C6E90).
   The mod reads the same facts first (fold 2, F1361 SHOULD 1): only a QUEUED FULL job (type 0) of the area covers the pieces (done);
   any other queued job of the area (an Interior, which does not redo the outdoor map) makes 0x3AB700 skip too, so the mod waits and
   asks again once it has left the queue; a running Partial may have read the area before the pieces were made - asked again once it
   has ended. Both waits share the kZoneRbRunWaitMs cap. */
const unsigned int kZoneRbQuietMs = 2000;    /* no new restored piece noted in the area for this long: its restores are made */
const unsigned int kZoneRbWaitMs = 60000;    /* pieces whose models still load at this age: the rebuild goes anyway (kWmBoxWaitMs) */
const unsigned int kZoneRbRunWaitMs = 30000; /* the engine's own running job (or a queued non-Full) for the area: asked again until this, then GAVE UP */
const int kZoneRbReadRetries = 50;           /* fold 2 (F1361 NIT): a fault reading the area or the job list is tried again next tick, this many times */
const int kZrbKeep = 0, kZrbAsk = 1, kZrbAskTimedOut = 2;
/* fold 2 (F1361 NIT): a noted piece is ready when its grown box is real, its model parts are in (BuildWalkMapBoxReady) AND its physics
   list holds at least one body - the area job carves only the physics objects (dc_3c4250:30-38) and F1315 saw parts loaded with 0
   bodies. bodies: PcPhysPod's count (-1 unreadable) */
inline int BuildZoneRebuildPieceReady(int grownReal, int partsLoading, int bodies)
{
    return (BuildWalkMapBoxReady(grownReal, partsLoading) != 0 && bodies > 0) ? 1 : 0;
}
inline int BuildZoneRebuildReadRetry(int faults) { return (faults < kZoneRbReadRetries) ? 1 : 0; }
/* when an area's rebuild is due: every noted piece's model parts loaded and its grown box real (BuildWalkMapBoxReady), and the area
   quiet; at kZoneRbWaitMs it goes with what it has. pieces 0 (every noted piece gone): nothing (kZrbKeep - the caller drops it) */
inline int BuildZoneRebuildDue(int pieces, int ready, unsigned int quietMs, unsigned int waitedMs)
{
    if (pieces <= 0) return kZrbKeep;
    if (quietMs < kZoneRbQuietMs) return kZrbKeep;
    if (ready >= pieces) return kZrbAsk;
    return (waitedMs >= kZoneRbWaitMs) ? kZrbAskTimedOut : kZrbKeep;
}
/* the engine's own skip (0x3BF360), read as it reads it. curZone / qZone: the job's area is this one (1), curType / qType: its +0x58 */
inline int BuildZoneRebuildSkipCur(int curZone, int curType) { return (curZone != 0 && (curType & 7) == 1) ? 1 : 0; }
inline int BuildZoneRebuildSkipQueued(int qZone, int qType) { return (qZone != 0 && (qType & 7) != 1) ? 1 : 0; }
/* fold 2 (F1361 SHOULD 1): a queued job of the area that is a Full (type 0, 0x3C66E0) - the only queued job that covers the pieces */
inline int BuildZoneRebuildQueuedFull(int qZone, int qType) { return (qZone != 0 && (qType & 7) == 0) ? 1 : 0; }
/* what the mod does with the area now: ask; the engine's queued FULL job covers it (done); the engine's running job, or a queued job of
   the area that is not a Full - again later (fold 2); or the area is not loaded (its map entry has no area: 0x3AB700 would read through a
   null) - given up. skipQueued: a queued job 0x3BF360 skips for (type not 1); queuedFull: one of them is a Full */
const int kZrbDoAsk = 0, kZrbDoneQueued = 1, kZrbAgainRunning = 2, kZrbNoArea = 3, kZrbNoRows = 4, kZrbAgainQueued = 5;
inline int BuildZoneRebuildAct(int rowsOk, int area, int skipRunning, int skipQueued, int queuedFull)
{
    if (rowsOk == 0) return kZrbNoRows;
    if (area == 0) return kZrbNoArea;
    if (queuedFull != 0) return kZrbDoneQueued;
    if (skipRunning != 0) return kZrbAgainRunning;
    if (skipQueued != 0) return kZrbAgainQueued;
    return kZrbDoAsk;
}
/* the events that note a piece for its area's rebuild: made from its record into a loaded area (restored, a re-make, a kept row); a
   teammate's single live copy is not one (its own box ask, BdWalkMapNote) and furniture never (its host's inside job is the rebuild's) */
inline int BuildZoneRebuildNotes(int restored, int remake, int kept, int furniture)
{
    if (furniture != 0) return 0;
    return (restored != 0 || remake != 0 || kept != 0) ? 1 : 0;
}
inline const char* BuildZoneRebuildActWord(int act)
{
    switch (act)
    {
    case kZrbDoAsk: return "asked";
    case kZrbDoneQueued: return "skipped: the engine's own Full job for this area is queued (0x3BF360) - it covers the pieces";
    case kZrbAgainQueued: return "waits: a queued engine job for this area is not a Full (0x3BF360 would skip) - asked again when it has left the queue";
    case kZrbAgainRunning: return "waits: the engine's own job for this area is running (0x3BF360) - asked again when it ends";
    case kZrbNoArea: return "GAVE UP: the area is not loaded here";
    case kZrbNoRows: return "NOT ASKED: ZoneWalkMapRebuild / NavMeshManagerPtr / ZoneManagerPtr is not in the address table";
    default: return "?";
    }
}
/* fold 2 (H2(b)): a wall whose crossing waited for its body gets it at the arrival when it is still unfinished, not destroyed and at
   or past its line (the mod's write landed there): written to just below the line and crossed with its own vt+0x230, then put back */
inline int BuildWallCrossReplay(int complete, int destroyed, float progress, float needed, float threshold)
{
    if (complete != 0 || destroyed != 0 || !(needed > 0.0f) || !(threshold > 0.0f)) return 0;
    return (progress >= needed * threshold) ? 1 : 0;
}
/* fold 2 (H2(c)): each crossing step's 0x5594A0 sets every group neighbour's own progress (+0x164, through its vt+0x238 0x559040) to
   this wall's ratio - a neighbour that stood further along is put back to the value read before the crossing */
const int kWallNbrCap = 32;
inline int BuildWallNbrPutBack(float before, float after) { return (after < before) ? 1 : 0; }
/* a mod write raised the piece past its threshold (or finished it): before not solid, after solid */
inline int BuildWalkMapCrossed(int solidBefore, int solidAfter) { return (solidBefore == 0 && solidAfter != 0) ? 1 : 0; }
inline const char* BuildWalkMapEventWord(int event)
{
    switch (event)
    {
    case kWmCopyMade: return "copyMade";
    case kWmCopyRestored: return "restored";
    case kWmKeptMade: return "keptMade";
    case kWmFinished: return "finished";
    case kWmThreshold: return "threshold";
    case kWmRemoved: return "removed";
    case kWmBodyArrived: return "bodyArrived";
    default: return "?";
    }
}
/* how the piece came to be finished on this game, as the SUSPECT line names it */
const int kPcRoadPlaced = 1, kPcRoadLeverPlace = 2, kPcRoadBuilt = 3, kPcRoadLeverProgress = 4, kPcRoadCopyMade = 5, kPcRoadCopyAdopted = 6,
          kPcRoadRestored = 7, kPcRoadOwnerState = 8, kPcRoadHelp = 9, kPcRoadAreaLoad = 10;
const int kPcRoadRecord = 11, kPcRoadBodyArrived = 12, kPcRoadHandover = 13, kPcRoadSave = 14, kPcRoadWorldReset = 15, kPcRoadProgressEvent = 16,
          kPcRoadLever = 17, kPcRoadAreaUnload = 18, kPcRoadWorldLeft = 19, kPcRoadLast = 19;   /* T-1389; 18, 19: the re-check's area-end events */
inline const char* PieceCheckRoadWord(int road)
{
    switch (road)
    {
    case kPcRoadPlaced: return "placed";
    case kPcRoadLeverPlace: return "leverPlace";
    case kPcRoadBuilt: return "built";
    case kPcRoadLeverProgress: return "leverProgress";
    case kPcRoadCopyMade: return "copyMade";
    case kPcRoadCopyAdopted: return "copyAdopted";
    case kPcRoadRestored: return "restored";
    case kPcRoadOwnerState: return "ownerState";
    case kPcRoadHelp: return "help";
    case kPcRoadAreaLoad: return "areaLoad";
    case kPcRoadRecord: return "record";
    case kPcRoadBodyArrived: return "bodyArrived";
    case kPcRoadHandover: return "handover";
    case kPcRoadSave: return "save";
    case kPcRoadWorldReset: return "worldReset";
    case kPcRoadProgressEvent: return "progressEvent";
    case kPcRoadLever: return "lever";
    case kPcRoadAreaUnload: return "areaUnload";
    case kPcRoadWorldLeft: return "worldLeft";
    default: return "?";
    }
}

/* ==== T-1389 (owner decision 1712, 2026-11-16): THE BUILDING CHECK BY BUILD STAGE - LOG ONLY ==========================================
   Owner: "we need the ability to differentiate between a building that is still in construction because in that case it shouldn't be
   complete, while also being able to make sure a building doesn't revert to a precompleted state as part of a bug."
   Every player wall, gate and building is judged at each event that touches it, finished or not, by its stage (PieceStageOf: the
   build state +0x160 complete, +0x162 taken apart, +0x1A1 destroyed; a wall in a group reads its leader's record). Its model parts,
   models and doors are counted against what its record makes - the same set at every stage (the engine makes the same parts at every
   stage and fades them by a shader value, decomp_558680:28-65; collision pieces at every stage, decomp_55ec50:485-581; Read). A ruin's
   models are its 'destroyed mesh' and are not counted. A finished piece's physics body is judged as T-988 judged it; an unfinished
   one's is not (Inferred: its physics group is the engine's below-threshold one). The came-back rule (PieceCameBack) names a finished
   building that shows unfinished again with none of the legitimate drops behind it. The roads (kPcRoad*) are above. */
/* the road in plain words, as the T-1389 lines name it ("at: <road>") */
inline const char* PieceCheckRoadPlain(int road)
{
    switch (road)
    {
    case kPcRoadPlaced: return "placement";
    case kPcRoadLeverPlace: return "placement by a test lever";
    case kPcRoadBuilt: return "finishing";
    case kPcRoadLeverProgress: return "a test lever's progress";
    case kPcRoadCopyMade: return "copy made";
    case kPcRoadCopyAdopted: return "copy taken on";
    case kPcRoadRestored: return "remade from the record";
    case kPcRoadOwnerState: return "the owner's STATE";
    case kPcRoadHelp: return "a helper's work";
    case kPcRoadAreaLoad: return "area load";
    case kPcRoadRecord: return "the shared record applied";
    case kPcRoadBodyArrived: return "physics arrived";
    case kPcRoadHandover: return "handover";
    case kPcRoadSave: return "save";
    case kPcRoadWorldReset: return "world-reset rebuild";
    case kPcRoadProgressEvent: return "a progress event that unfinished it";
    case kPcRoadLever: return "test lever";
    case kPcRoadAreaUnload: return "area unload";
    case kPcRoadWorldLeft: return "the world left";
    default: return "?";
    }
}
const int kPcStageUnread = 0, kPcStageFinished = 1, kPcStageBuilding = 2, kPcStageRuin = 3, kPcStageTakenApart = 4;
/* the stage the piece's own state reads: taken apart first (it is going), then a ruin (+0x1A1 - complete may still read 1), then
   finished / under construction */
inline int PieceStageOf(int stateOk, int complete, int dismantled, int destroyed)
{
    if (stateOk == 0) return kPcStageUnread;
    if (dismantled != 0) return kPcStageTakenApart;
    if (destroyed != 0) return kPcStageRuin;
    return (complete != 0) ? kPcStageFinished : kPcStageBuilding;
}
inline const char* PieceStageWord(int stage)
{
    switch (stage)
    {
    case kPcStageFinished: return "finished";
    case kPcStageBuilding: return "under construction";
    case kPcStageRuin: return "ruin";
    case kPcStageTakenApart: return "taken apart";
    default: return "unread";
    }
}
/* what was read off the piece and what its record makes. -1 = not read (never judged). holder: the model holder +0x228 (1 set, 0 none
   yet - the piece's physics and models are not made yet, F1046) */
struct PieceWholeIn
{
    int stage, holder;
    int parts, partsWant, models, modelsWant, doors, doorsWant;
    int node, material, interior, interiorWant;
    int bodiesJudged, bodies;
    int shown;   /* T-1389 fold (NOTE b): +0x19D - 1 shown, 0 hidden (a part's model exists only while the building is shown: models not judged), -1 unread */
    PieceWholeIn() : stage(kPcStageUnread), holder(-1), parts(-1), partsWant(-1), models(-1), modelsWant(-1), doors(-1), doorsWant(-1),
                     node(-1), material(-1), interior(-1), interiorWant(-1), bodiesJudged(0), bodies(-1), shown(-1) {}
};
/* T-36 (F1186, probe P297; Read 1.0.65 0x54C2A0, run by the model-loaded step 0x5580B0, which keeps its answer at part+0x20): the material
   set ONE model part of a building is drawn with - its own record's (part+0x28) 'material' list when that list is not empty (its first
   entry), else the building's material slot +0x248 when set, else the engine's default set kPieceDefaultMaterial. PieceWholeIn::material
   judges what the parts resolve to, never +0x248 alone: a teammate's copy whose +0x248 is empty while every part carries its own set looks
   the same to a player (T2497: 'own 11, from +0x248 0, DEFAULT 360 0' on both games) */
const char* const kPieceDefaultMaterial = "360-gamedata.base";
const int kPcMatUnread = -1, kPcMatOwn = 0, kPcMatSlot = 1, kPcMatDefault = 2;
/* one part as read: ownRefs - entries in its record's 'material' list (-1 the record or the list not read); ownSid - the first entry's
   string id (empty = not read) */
struct PieceMatPart
{
    int ownRefs;
    std::string ownSid;
    PieceMatPart() : ownRefs(-1) {}
};
/* where the engine takes one part's set from. slotSet: +0x248 set 1 / empty 0 / not read -1 */
inline int PieceMaterialSource(int ownRefs, int slotSet)
{
    if (ownRefs < 0) return kPcMatUnread;
    if (ownRefs > 0) return kPcMatOwn;
    if (slotSet < 0) return kPcMatUnread;
    return slotSet != 0 ? kPcMatSlot : kPcMatDefault;
}
/* one part: 1 drawn with a set other than the default, 0 drawn with the default (by the rule's last step, or a set that IS the default),
   -1 unread. slotSid: +0x248's string id (empty = not read) */
inline int PieceMaterialPartOk(const PieceMatPart& p, int slotSet, const std::string& slotSid)
{
    const int s = PieceMaterialSource(p.ownRefs, slotSet);
    if (s == kPcMatUnread) return -1;
    if (s == kPcMatDefault) return 0;
    const std::string& sid = (s == kPcMatOwn) ? p.ownSid : slotSid;
    if (sid.empty()) return -1;
    return sid == kPieceDefaultMaterial ? 0 : 1;
}
/* THE MATERIAL VERDICT (PieceWholeIn::material): 0 = some part would be drawn with the default set (the line's 'material' word); else -1
   (not judged) when any part is unread, the holder has more parts than were read (n < total) or no part was read; else 1. parts: the n
   parts read; total: the parts in the model holder */
inline int PieceMaterialVerdict(const PieceMatPart* parts, int n, int total, int slotSet, const std::string& slotSid)
{
    int unread = (n <= 0 || n < total || parts == 0) ? 1 : 0;
    for (int i = 0; parts != 0 && i < n; ++i)
    {
        const int v = PieceMaterialPartOk(parts[i], slotSet, slotSid);
        if (v == 0) return 0;
        if (v < 0) unread = 1;
    }
    return unread != 0 ? -1 : 1;
}
/* what is missing, comma-separated in this order (empty = whole for its stage): models and physics (none made), parts, models, doors,
   scene node, material, interior, physics body. A ruin's parts and models are its destroyed mesh - not counted; its doors are not
   judged either (Guess: a ruin may lose them). The interior is judged on a finished piece only. Unread values are never judged */
inline std::string PieceWholeMissing(const PieceWholeIn& x)
{
    if (x.holder == 0) return "models and physics (none made)";
    std::string w;
    char t[96];
    if (x.stage != kPcStageRuin)
    {
        if (x.parts >= 0 && x.partsWant >= 0 && x.parts < x.partsWant) { std::sprintf(t, "parts (%d of %d)", x.parts, x.partsWant); w += (w.empty() ? "" : ", "); w += t; }
        if (x.shown != 0 && x.models >= 0 && x.modelsWant >= 0 && x.models < x.modelsWant) { std::sprintf(t, "models (%d of %d)", x.models, x.modelsWant); w += (w.empty() ? "" : ", "); w += t; }
        if (x.doors >= 0 && x.doorsWant >= 0 && x.doors < x.doorsWant) { std::sprintf(t, "doors (%d of %d)", x.doors, x.doorsWant); w += (w.empty() ? "" : ", "); w += t; }
    }
    if (x.node == 0) { w += (w.empty() ? "" : ", "); w += "scene node"; }
    if (x.material == 0) { w += (w.empty() ? "" : ", "); w += "material"; }
    if (x.stage == kPcStageFinished && x.interiorWant == 1 && x.interior == 0) { w += (w.empty() ? "" : ", "); w += "interior"; }
    if (x.bodiesJudged != 0 && x.bodies == 0) { w += (w.empty() ? "" : ", "); w += "physics body"; }
    return w;
}
/* What this game has seen of one piece this session (per key, MAIN THREAD). finished: seen finished (and not since dropped by a
   legitimate road) - finishedRoad: the road it was first seen finished at (kPcRoad*, 0 unknown); ruin: seen a ruin since it was last
   seen finished; takenApart: seen taken apart (cleared by a later finished reading - a cancelled dismantle, T-1389 fold NOTE a); dropOk:
   a legitimate drop was seen (a ruin being repaired - 558b10:60-86 clears complete, lowers progress and clears destroyed - or taken
   apart) and it has not been seen finished since; said: the came-back line was written for this placement; placement: the placement
   number last seen */
struct PieceSeen
{
    int finished, ruin, takenApart, dropOk, said, finishedRoad;
    unsigned int placement;
    PieceSeen() : finished(0), ruin(0), takenApart(0), dropOk(0), said(0), finishedRoad(0), placement(0u) {}
};
/* WHICH TRUTH decided a came-back (T-1389 fold 3) - the line names it */
const int kPcTruthNone = 0, kPcTruthRecord = 1, kPcTruthSeen = 2, kPcTruthOwner = 3;
/* THE CAME-BACK RULE for THIS GAME'S OWN piece, judged on a FRESH live read BEFORE that read is noted (PieceSeenNote). Returns the truth
   that decided (kPcTruthRecord: the shared record says finished - recordFinished, complete and not a ruin; kPcTruthSeen: this game saw it
   finished this session) or kPcTruthNone. The live piece reads complete 0 - not a ruin now or since it was last finished (a repair), not
   taken apart, the same placement number (record's, last seen and live, where known). Damage alone lowers progress only (complete stays
   1) - never a came-back. A teammate's copy is judged by PieceCameBackCopy, never here */
inline int PieceCameBack(const PieceSeen& s, int recordFinished, unsigned int recordPlacement, int liveOk, int complete, int dismantled,
                         int destroyed, unsigned int livePlacement)
{
    if (liveOk == 0) return kPcTruthNone;
    if (complete != 0 || dismantled != 0 || destroyed != 0) return kPcTruthNone;
    if (s.ruin != 0 || s.takenApart != 0 || s.dropOk != 0) return kPcTruthNone;
    if (recordPlacement != 0u && livePlacement != 0u && recordPlacement != livePlacement) return kPcTruthNone;   /* a new placement at the key */
    if (recordFinished != 0) return kPcTruthRecord;
    const int seenSame = (s.placement == 0u || livePlacement == 0u || s.placement == livePlacement) ? 1 : 0;
    return (s.finished != 0 && seenSame != 0) ? kPcTruthSeen : kPcTruthNone;
}
/* T-1389 fold 1: a teammate's copy counts as 'no STATE yet' from each (re)make until an owner's STATE lands AFTER it was made (a re-made
   copy is made 0 of needed; its owner's last STATE is only queued then). isCopy: another player's piece on this game; ownerKnown: the
   row's ownerLast holds a STATE; awaitSinceMade: made or re-made here and no owner's STATE applied since */
inline int PieceCopyNoState(int isCopy, int ownerKnown, int awaitSinceMade)
{
    return (isCopy != 0 && (ownerKnown == 0 || awaitSinceMade != 0)) ? 1 : 0;
}
/* T-1389 fold 2: THE CAME-BACK RULE for a teammate's COPY - judged by its OWNER's last STATE (ownerLast), never by this game's memory
   (this game may have missed a ruin and its repair while away: the owner's STATE unfinishing it is legit). kPcTruthOwner only when that
   STATE says finished (complete, not a ruin) and the live copy reads complete 0, not a ruin, not taken apart, the same placement (the
   record's, where this event has it, and the row's). A copy with no STATE since its (re)make is never judged */
inline int PieceCameBackCopy(int copyNoState, int ownerComplete, int ownerDestroyed, unsigned int recordPlacement, int liveOk, int complete,
                             int dismantled, int destroyed, unsigned int livePlacement)
{
    if (copyNoState != 0 || liveOk == 0) return kPcTruthNone;
    if (complete != 0 || dismantled != 0 || destroyed != 0) return kPcTruthNone;
    if (ownerComplete == 0 || ownerDestroyed != 0) return kPcTruthNone;
    if (recordPlacement != 0u && livePlacement != 0u && recordPlacement != livePlacement) return kPcTruthNone;   /* a new placement at the key */
    return kPcTruthOwner;
}
/* a live read noted (after the came-back rule ran on it). road: the road it was read at (kPcRoad*) - kept as finishedRoad when it is
   first seen finished */
inline void PieceSeenNote(PieceSeen* s, int complete, int dismantled, int destroyed, unsigned int placement, int road = 0)
{
    if (placement != 0u && s->placement != 0u && placement != s->placement) *s = PieceSeen();   /* a new placement: nothing seen of it yet */
    if (placement != 0u) s->placement = placement;
    if (dismantled != 0) { s->takenApart = 1; s->dropOk = 1; return; }
    if (destroyed != 0) { s->ruin = 1; return; }
    if (complete != 0)
    {
        if (s->finished == 0) s->finishedRoad = road;
        s->finished = 1; s->ruin = 0; s->dropOk = 0; s->said = 0; s->takenApart = 0;   /* fold NOTE a: a cancelled dismantle no longer silences the rule */
        return;
    }
    if (s->ruin != 0) { s->dropOk = 1; s->ruin = 0; s->finished = 0; s->finishedRoad = 0; }   /* a ruin read unfinished and whole again: it is being repaired */
}
/* the record's 'finished': complete and not a ruin */
inline int PieceRecordFinished(int complete, int destroyed) { return (complete != 0 && destroyed == 0) ? 1 : 0; }
/* the line kinds */
const int kPcLineWhole = 1, kPcLineBuilding = 2, kPcLineNotWhole = 3, kPcLineRuin = 4, kPcLineCameBack = 5;
/* one count as the lines print it: '?' when not read */
inline std::string PieceCheckCount(int v) { if (v < 0) return "?"; char t[16]; std::sprintf(t, "%d", v); return std::string(t); }
/* T-1389 fold 3: which truth decided a came-back, in the line's words */
inline std::string PieceCameBackTruthText(int truth, int seenRoad)
{
    if (truth == kPcTruthRecord) return "the record says finished";
    if (truth == kPcTruthOwner) return "the owner's last state says finished";
    if (truth == kPcTruthSeen) return std::string("this game saw it finished at ") + (seenRoad > 0 ? PieceCheckRoadPlain(seenRoad) : "?");
    return "?";
}
/* T-1389 fold NOTE d: the old SUSPECT line's facts on a NOT whole line: owner (mine / slot N / slot ?), area and position */
inline std::string PieceCheckFactsText(int mine, int slot, int posOk, int ax, int ay, const float* pos)
{
    char t[200];
    char own[24];
    if (mine != 0) std::sprintf(own, "mine");
    else if (slot >= 0) std::sprintf(own, "slot %d", slot);
    else std::sprintf(own, "slot ?");
    if (posOk != 0 && pos != 0) std::sprintf(t, " owner=%s area=%d,%d pos=%.1f,%.1f,%.1f", own, ax, ay, pos[0], pos[1], pos[2]);
    else std::sprintf(t, " owner=%s area=? pos=?", own);
    return std::string(t);
}
/* THE LINE WORDS (after "[PIECECHECK] "): key, kind ("building 'Name'"), and the road in plain words. solid: 1 / 0 / -1 unread. A hidden
   piece (x.shown 0) says so: its models are not judged (fold NOTE b). truth / seenRoad: the came-back line's deciding truth (fold 3) */
inline std::string PieceCheckLineText(int line, const std::string& key, const std::string& kind, const PieceWholeIn& x, float progress,
                                      float needed, int solid, const std::string& missing, int road, int truth = kPcTruthRecord, int seenRoad = 0)
{
    std::string s = key + " " + kind + " ";
    char t[400];
    switch (line)
    {
    case kPcLineWhole:
        s += "whole - finished, parts " + PieceCheckCount(x.parts) + "/" + PieceCheckCount(x.partsWant) + ", models "
             + (x.shown == 0 ? std::string("not judged") : PieceCheckCount(x.models) + "/" + PieceCheckCount(x.modelsWant)) + ", doors "
             + PieceCheckCount(x.doors) + "/" + PieceCheckCount(x.doorsWant) + ", bodies "
             + (x.bodiesJudged != 0 ? PieceCheckCount(x.bodies) : std::string("not judged"));
        break;
    case kPcLineBuilding:
        std::sprintf(t, "under construction - progress %.2f of %.2f (solid in the walking map: %s), whole for its stage", progress, needed,
                     solid > 0 ? "yes" : (solid == 0 ? "no" : "?"));
        s += t;
        break;
    case kPcLineRuin:
        s += "ruin - its models are its destroyed mesh (not counted), whole for a ruin";
        break;
    case kPcLineNotWhole:
        s += "NOT whole - missing: " + missing + " (" + PieceStageWord(x.stage) + ")";
        break;
    case kPcLineCameBack:
        std::sprintf(t, ", this game shows progress %.2f of %.2f; not a ruin, not taken apart, same placement", progress, needed);
        s += "FINISHED BUILDING CAME BACK UNFINISHED - " + PieceCameBackTruthText(truth, seenRoad) + t;
        break;
    default:
        s += "?";
        break;
    }
    if (x.shown == 0) s += " - hidden (its models not judged)";
    return s + " (at: " + PieceCheckRoadPlain(road) + ")";
}
/* T-1389 fold 4: what a check does with a piece whose physics and models are not made yet (+0x228 empty, holder 0). kPcWaitJudge: judge it
   now (it has them, or it waited, its wait is over and it still has none: NOT whole); kPcWaitAdd: wait for its physics-arrived event;
   kPcWaitKeep: it waits already and its wait is pending - a second event (copy made then STATE; record + area load) keeps waiting;
   kPcWaitDrop: its physics-arrived event found none (counted, no line) */
const int kPcWaitJudge = 0, kPcWaitAdd = 1, kPcWaitKeep = 2, kPcWaitDrop = 3;
/* T-1389 (F1114, 2026-11-16): a finished piece whose holder is made but whose physics bodies are not there yet (bodiesPending,
   PieceBodiesPending) waits like a piece with no holder - the engine makes the bodies after createPhysical returns. Its holder-arrived
   event (createPhysical) that still finds no bodies waits on for them (kPcWaitAdd), never kPcWaitDrop; the same aged limit and the same
   'waited <n> s' words apply. bodiesPending 0 is the fold-4 rule unchanged */
inline int PieceWaitActionFor(int holder, int bodiesPending, int bodyArrivedRoad, int waitedThisObject, int waitPending)
{
    if (holder != 0 && bodiesPending == 0) return kPcWaitJudge;
    if (holder == 0 && bodyArrivedRoad != 0) return kPcWaitDrop;
    if (bodyArrivedRoad != 0) return kPcWaitAdd;   /* its holder came, its bodies not yet: it waits for them */
    if (waitedThisObject == 0) return kPcWaitAdd;
    if (waitPending != 0) return kPcWaitKeep;
    return kPcWaitJudge;
}
inline int PieceWaitAction(int holder, int bodyArrivedRoad, int waitedThisObject, int waitPending)
{
    return PieceWaitActionFor(holder, 0, bodyArrivedRoad, waitedThisObject, waitPending);
}
/* T-1389 (F1114): 1 = a finished piece whose holder is made and whose bodies were judged and found none - its bodies are not made yet */
inline int PieceBodiesPending(const PieceWholeIn& x)
{
    return (x.stage == kPcStageFinished && x.holder == 1 && x.bodiesJudged != 0 && x.bodies == 0) ? 1 : 0;
}
/* T-1389 (F1114): 1 = this NOT whole line was printed already - the same stage AND the same missing set (a stage change prints again) */
inline int PieceProblemSaid(int saidStage, const std::string& saidMissing, int stage, const std::string& missing)
{
    return (saidStage == stage && saidMissing == missing) ? 1 : 0;
}
/* T-1389 re-check (2026-11-16): a piece whose physics NEVER arrive must still be named. A wait is kept only while it is younger than
   kPcBodyWaitLimitMs, counted from the FIRST wait of this object (an evicted wait re-added keeps it: PieceWaitSince); an older wait is
   judged at the event that finds it (kPcWaitAged: NOT whole, 'waited <n> s for its physics'). The limit is the old SUSPECT point - the
   T-988 check re-checked a piece with no body kPcRecheckMs (90 s) later, twice, and only one still without a body at 180 s was named.
   No timer: the age is read at each event, and the events that end an area (area unload, the world left, a save) judge it too
   (PieceWaitEndJudge) so a piece with no later event is named once */
const int kPcWaitAged = 4;
const unsigned int kPcBodyWaitLimitMs = 180000u;
inline int PieceWaitAged(int action, int sinceKnown, unsigned int waitedMs)
{
    if ((action == kPcWaitKeep || action == kPcWaitAdd) && sinceKnown != 0 && waitedMs >= kPcBodyWaitLimitMs) return kPcWaitAged;
    return action;
}
/* the wait's start: the same object waiting again (its wait evicted at the cap, re-added at its next event) keeps its first start */
inline unsigned int PieceWaitSince(int sameObject, unsigned int firstSince, unsigned int now)
{
    return (sameObject != 0) ? firstSince : now;
}
/* an event that ends an area: 1 = name the still-waiting piece now (its wait is old enough and it was not named for it yet) */
inline int PieceWaitEndJudge(int waiting, unsigned int waitedMs, int namedAlready)
{
    return (waiting != 0 && namedAlready == 0 && waitedMs >= kPcBodyWaitLimitMs) ? 1 : 0;
}
inline std::string PieceWaitedText(unsigned int waitedMs)
{
    char t[64];
    std::sprintf(t, " - waited %u s for its physics", waitedMs / 1000u);
    return std::string(t);
}
/* T-1389 fold 5: the line caps - healthy, problem (NOT whole) and came-back lines each their own kPcLineCap */
const int kPcSayHealthy = 0, kPcSayProblem = 1, kPcSayCameBack = 2;
/* the line a judged piece gets (the came-back line is its own) */
inline int PieceCheckLineFor(int stage, const std::string& missing)
{
    if (!missing.empty()) return kPcLineNotWhole;
    if (stage == kPcStageFinished) return kPcLineWhole;
    if (stage == kPcStageRuin) return kPcLineRuin;
    return kPcLineBuilding;
}

/* ---- T-1427 (F1265): a re-towned building's floor parts move to its new town's floor store ----
   The engine keeps each building's floor parts (the per-floor model pieces its walk-in hides above the floor looked at) in the floor
   store of the town it was MADE in: Building+0x1F8 = town+0x250, set only by createPhysical 0x55FF50 (and the town merge 0x926190
   after 0x930510 moved the whole store). The walk-in 0x561AB0 looks in the store of the building's CURRENT town (0x92B0A0), so a
   building whose town changed after it was made has no record there, its floor pass is skipped and its roof stays drawn. */
const int kFsSame = 0, kFsMove = 1, kFsNoModels = 2, kFsNoStore = 3, kFsNoTown = 4;
/* a top-level building re-towned: hasModels = its model list (+0x228) is made; oldStore = its +0x1F8; newStore = the new town's
   +0x250 (0 = no town). No models yet: createPhysical will point it at the store of the town its words name then - nothing to move */
inline int FloorStoreStep(int hasModels, unsigned long long oldStore, unsigned long long newStore)
{
    if (newStore == 0) return kFsNoTown;
    if (hasModels == 0) return kFsNoModels;
    if (oldStore == 0) return kFsNoStore;
    return oldStore == newStore ? kFsSame : kFsMove;
}
/* a piece inside the moved building: its parts sit in its host's record, found through its OWN +0x1F8 at its teardown (0x92CFF0 asks
   the store it is given for the host's record) - so a piece in the host's old store moves with it (kFsInsideMove), one already in
   the new store stays and its parts are its host's (kFsInsideThere), one with no models or no store has nothing anywhere
   (kFsInsideNone), and one in a third store is not touched (kFsInsideForeign: a part of it in the host's record stops the move) */
const int kFsInsideNone = 0, kFsInsideMove = 1, kFsInsideThere = 2, kFsInsideForeign = 3;
inline int FloorStoreInsideStep(int hasModels, unsigned long long store, unsigned long long hostOld, unsigned long long newStore)
{
    if (hasModels == 0 || store == 0) return kFsInsideNone;
    if (store == hostOld) return kFsInsideMove;
    if (store == newStore) return kFsInsideThere;
    return kFsInsideForeign;
}
/* the moving group (the host and the inside pieces that move with it) is moved only when all of it was read, the reads were not cut
   short, the new store holds no record of any of them, every part in their records belongs to one of them (or to the host's
   interior, or to an inside piece already in the new store) - so no owner is left pointing at the old store while its parts sit in
   the new one - and none of them has a sign highlight in the old store's second list (0x92CFF0 retires those and the re-adding road
   0x9331D0 is not read) */
const int kFsGroupGo = 0, kFsGroupFault = 1, kFsGroupCapped = 2, kFsGroupNewHolds = 3, kFsGroupUnknownPart = 4, kFsGroupSigns = 5;
inline int FloorStoreGroupStep(int readOk, int capped, int newHolds, int unknownParts, int signs)
{
    if (readOk == 0) return kFsGroupFault;
    if (capped != 0) return kFsGroupCapped;
    if (newHolds != 0) return kFsGroupNewHolds;
    if (unknownParts != 0) return kFsGroupUnknownPart;
    if (signs != 0) return kFsGroupSigns;
    return kFsGroupGo;
}
inline const char* FloorStoreGroupWord(int step)
{
    switch (step)
    {
    case kFsGroupGo: return "moved";
    case kFsGroupFault: return "its reads FAULTED";
    case kFsGroupCapped: return "too many parts to read";
    case kFsGroupNewHolds: return "the new store already holds a record of it";
    case kFsGroupUnknownPart: return "a part in its records belongs to none of the moving group";
    case kFsGroupSigns: return "it has sign highlights (re-adding them is not read)";
    }
    return "?";
}

/* ---- T-1427 part 2 (owner request 2026-11-18, F1265 / F1270): THE TOWN A COPY IS CREATED IN ----
   Every road that makes a copy of a player's outdoor piece used to pass town 0 to createBuilding (0x57C1E0), so the engine chose: the
   town whose own area holds the spot, else the world default 'Nowhere' (57c1e0:373, 9279c0:18-58), and for an interior piece that got
   'Nowhere' the nearest NON-player town within 10000 (57c1e0:401-433) - the owner's shack copy came out 'Squin'. Now the copy is made
   in the town the builder's own rule gives, in this order:
     record - the town the piece's record names (its outpost id, standing here);
     named  - the outpost the PLACE names by its centre (townHand / townPos): the owner's town whose centre (here, or as the world
              server's outpost list gives it) is within sameRadius of it, the nearest; else ...
     area   - ... the owner's town whose own area holds that centre, nearest by centre;
     near   - the owner's town nearest the spot, kept within keepRange (the placement confirm's rule, decomp_4d6300:63-87);
     else Nowhere (*pick = -1) - never the engine's own fallback to an NPC town.
   Candidates are the owning player's towns here (this player's own, or the towns shown here for that player's outposts). */
const int kCtNowhere = 0, kCtRecord = 1, kCtNamed = 2, kCtNamedArea = 3, kCtNear = 4;
struct CopyTownCand
{
    float cx, cz;        /* the town's own centre here (x, z) */
    int hasList;         /* 1 = listX / listZ: its outpost's centre as the world server's list gives it (a shown town) */
    float listX, listZ;
    int holdsNamed;      /* 1 = the town's own area holds the named centre (the engine's inside test) */
    int recordNamed;     /* 1 = the piece's record names this town's outpost */
    CopyTownCand() : cx(0.0f), cz(0.0f), hasList(0), listX(0.0f), listZ(0.0f), holdsNamed(0), recordNamed(0) {}
};
inline float CopyTownD2(float ax, float az, float bx, float bz) { const float dx = ax - bx, dz = az - bz; return dx * dx + dz * dz; }
/* the rule that picked (kCt*); *pick = the candidate, -1 = Nowhere. namedXZ / spotXZ 0 = not given */
inline int CopyTownChoose(const CopyTownCand* c, int n, const float* namedXZ, const float* spotXZ, float sameRadius, float keepRange, int* pick)
{
    *pick = -1;
    for (int i = 0; i < n; ++i) if (c[i].recordNamed != 0) { *pick = i; return kCtRecord; }
    float bd = 0.0f;
    if (namedXZ != 0)
    {
        const float r2 = sameRadius * sameRadius;
        for (int i = 0; i < n; ++i)
        {
            float d = CopyTownD2(c[i].cx, c[i].cz, namedXZ[0], namedXZ[1]);
            if (c[i].hasList != 0) { const float dl = CopyTownD2(c[i].listX, c[i].listZ, namedXZ[0], namedXZ[1]); if (dl < d) d = dl; }
            if (d <= r2 && (*pick < 0 || d < bd)) { *pick = i; bd = d; }
        }
        if (*pick >= 0) return kCtNamed;
        for (int i = 0; i < n; ++i)
        {
            if (c[i].holdsNamed == 0) continue;
            const float d = CopyTownD2(c[i].cx, c[i].cz, namedXZ[0], namedXZ[1]);
            if (*pick < 0 || d < bd) { *pick = i; bd = d; }
        }
        if (*pick >= 0) return kCtNamedArea;
    }
    if (spotXZ != 0)
    {   /* T-1427 fold 1 (review LOW): a shown town is measured from its outpost's centre as the world server's list gives it - the
           builder's own town's centre, which its placement confirm measures (decomp_4d6300:63-87) - not the copy town's own centre */
        for (int i = 0; i < n; ++i)
        {
            const float d = (c[i].hasList != 0) ? CopyTownD2(c[i].listX, c[i].listZ, spotXZ[0], spotXZ[1]) : CopyTownD2(c[i].cx, c[i].cz, spotXZ[0], spotXZ[1]);
            if (*pick < 0 || d < bd) { *pick = i; bd = d; }
        }
        if (*pick >= 0 && bd <= keepRange * keepRange) return kCtNear;   /* the nearest, kept only within range (the confirm's order) */
        *pick = -1;
    }
    return kCtNowhere;
}
inline const char* CopyTownRuleWord(int rule)
{
    switch (rule)
    {
    case kCtNowhere: return "none of the owner's towns - the world default 'Nowhere'";
    case kCtRecord: return "the town its record names";
    case kCtNamed: return "the outpost the PLACE names (by its centre)";
    case kCtNamedArea: return "the outpost the PLACE names (its area holds the centre)";
    case kCtNear: return "the owner's town nearest the spot within 5000 (the placement confirm's rule)";
    }
    return "?";
}
/* ---- T-1427 fold 1 (owner request 2026-11-18: '... it can log the discrepancy if one exists and then switch it to match'): THE COPY
   AGAINST THE BUILDER'S TOWN. After createBuilding (CopyTownMadeStep): chosen - a town was given; nowSame - the engine's town is the
   given one; wrote - the write of the given town after the engine overrode it (1 written, -1 failed). kCmWriteFailed is not the end:
   the copy stays owed and the builder's town's arrival (its record) switches it */
const int kCmTook = 0, kCmWritten = 1, kCmWriteFailed = 2, kCmEnginePick = 3;
inline int CopyTownMadeStep(int chosen, int nowSame, int wrote)
{
    if (chosen == 0) return kCmEnginePick;
    if (nowSame != 0) return kCmTook;
    return wrote > 0 ? kCmWritten : kCmWriteFailed;
}
/* the builder's town arrives (its record, or the [SHOW] link step): same - the copy's creation town IS the builder's; ownerHere - this
   game owns the piece (its engine's link rules there, T-1047: compared, never switched); sw - what the caller's switch did (kSw*:
   none tried, done, the copy already stands in it, refused - the record link refused or skipped as failed before -, the write
   faulted, the piece stands inside a building - its host's town rules it). kCjOwed* keep the copy's entry: the next event that touches
   that building (its record's next look or the link step) tries again, each counted once per kind */
const int kSwNone = 0, kSwDone = 1, kSwAlready = 2, kSwRefused = 3, kSwFault = 4, kSwInside = 5;
const int kCjMatch = 0, kCjSwitched = 1, kCjOwedRefused = 2, kCjOwedFault = 3, kCjOwedInside = 4, kCjOwnNoSwitch = 5, kCjMax = 5;
inline int CopyTownJudgeStep(int same, int ownerHere, int sw)
{
    if (same != 0) return kCjMatch;
    if (ownerHere != 0) return kCjOwnNoSwitch;
    if (sw == kSwDone || sw == kSwAlready) return kCjSwitched;
    if (sw == kSwFault) return kCjOwedFault;
    if (sw == kSwInside) return kCjOwedInside;
    return kCjOwedRefused;
}
/* 1 = the copy's entry stays (it is still owed a switch); every other step ends it - compared */
inline int CopyTownJudgeKeeps(int step) { return (step == kCjOwedRefused || step == kCjOwedFault || step == kCjOwedInside) ? 1 : 0; }
inline const char* CopyTownJudgeWord(int step)
{
    static const char* const n[] = { "MATCH", "DIFFER - switched to the builder's town", "DIFFER - NOT switched yet: the record link refused (tried again at its next look)",
                                     "DIFFER - NOT switched yet: the link write FAULTED (tried again at its next look)",
                                     "DIFFER - NOT switched yet: it stands inside a building (its host's town rules it; tried again at its next look)",
                                     "DIFFER - not switched: this game owns the piece, its engine's link rules here (T-1047)" };
    return (step >= 0 && step <= kCjMax) ? n[step] : "?";
}
/* T-1427 fold 2 (re-review SHOULD 1): a world-town fact's town here - the ONE live world town whose stringID is `want`, out of EVERY
   world town this game has (sids: their ids, read once; no cap - a list cut short would miss a town and never compare the copy).
   Returns its index, or -1 (none, or two: never a guess); *hits = how many carry the id */
inline int WorldTownPick(const std::vector<std::string>& sids, const std::string& want, int* hits)
{
    int at = -1, n = 0;
    if (!want.empty())
        for (size_t i = 0; i < sids.size(); ++i)
            if (sids[i] == want) { if (n == 0) at = (int)i; ++n; }
    if (hits != 0) *hits = n;
    return n == 1 ? at : -1;
}

} // namespace coopbuild
