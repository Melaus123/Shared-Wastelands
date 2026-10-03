/* src/common/pathlead.h - D3 (read-pathend, T266 stop-start): THE PATH DRIVE'S LEAD / NEAR-END / TOO-CLOSE ARITHMETIC.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes
 * a header, so the offline suite drives it directly. replicate.cpp PathDriveStep calls these.
 *
 * WHY (T266 B, whole run: pathArrivedWhileOwnerMoving 16816 = 46% of new paths; 63% of paths started with the copy
 * < 1 u/s). The D2 near-end re-plan fired at 0.9 s x the OWNER's speed of path left, but the next path needs the 0.5 s
 * issue floor plus 0.08-0.51 s to land (0x144C90). On arrival the engine sets the agent's state 1 (0x147F90); the next
 * issue flips it to 0 (0x145CB0) and the copy has no route until the result lands - it stops, then starts. So:
 *   near end  = max(kNearEndMin, v_copy x kNearEndSec)   (in TIME on the copy's own measured speed; < 0.9 x the lead)
 *   lead      = clamp(max(v_owner, v_copy) x kLeadSec + kLeadPad, kLeadMin, kLeadMax)
 *               - every new destination lies at least v_copy x (kLeadSec - kNearEndSec) = 0.5 s beyond the last
 *   too close = the engine drops a destination < 2 u from the last (0x6607E0, 4.0 squared) or < 1 u from the copy; while
 *               the owner moves, such a destination is pushed FORWARD along the owner's heading by
 *               max(kExtendMin, v_copy x kExtendSec) instead of being refused.
 *   landed    = the agent's request status reads 1 after it has read anything else since the issue (4/5 = waiting).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_PATHLEAD_H
#define COOP_COMMON_PATHLEAD_H

namespace cooppath {

const float kLeadSec    = 1.6f;   /* the destination leads by this many seconds of max(owner, copy) speed ... */
const float kLeadPad    = 2.0f;   /* ... plus this many units ... */
const float kLeadMin    = 8.0f;   /* ... at least this ... */
const float kLeadMax    = 90.0f;  /* ... at most this (runners 45-85 u/s) */
const float kNearEndSec = 1.1f;   /* re-plan when the copy is within this many seconds of ITS OWN speed of its destination */
const float kNearEndMin = 3.0f;   /* ... or this many units, whichever is more */
const float kExtendSec  = 0.5f;   /* a too-close destination is pushed forward by this many seconds of the copy's speed ... */
const float kExtendMin  = 2.5f;   /* ... or this many units, whichever is more (above the engine's 2 u drop) */
const int   kStatusLanded = 1;    /* the path agent's request status once the result landed (4/5 = waiting) */
const float kCopySpeedMax = 120.0f;   /* fold LOW 1: a measured copy speed above this is clamped (a position jump that skipped
                                        PathForgetDest must not push an aim hundreds of units out) */
const double kMinIssueGapSec = 0.2;   /* fold M1: never two issues for one copy closer than this, even after a landing - a copy
                                        ahead of its aim keeps 'near end' true and would re-plan on every 0.08 s landing */

/* A measured speed as a usable non-negative number: negative (no sample), NaN or absurd -> 0. */
inline float UsableSpeed(float v) { return (v > 0.0f && v < 1.0e6f) ? v : 0.0f; }
/* fold LOW 1: the copy's own measured speed as fed to the lead, the near end and the push - usable, at most kCopySpeedMax. */
inline float CopySpeed(float v) { const float u = UsableSpeed(v); return u > kCopySpeedMax ? kCopySpeedMax : u; }

inline float LeadDist(float ownerSpeed, float copySpeed)
{
    float v = UsableSpeed(ownerSpeed);
    const float c = CopySpeed(copySpeed);
    if (c > v) v = c;
    float la = v * kLeadSec + kLeadPad;
    if (la < kLeadMin) la = kLeadMin;
    if (la > kLeadMax) la = kLeadMax;
    return la;
}

/* Always below 0.9 x the lead, so a fresh destination is never already 'near its end'. */
inline float NearEndDist(float copySpeed, float lead)
{
    float ne = CopySpeed(copySpeed) * kNearEndSec;
    if (ne < kNearEndMin) ne = kNearEndMin;
    if (ne > lead * 0.9f) ne = lead * 0.9f;
    return ne;
}

/* max(kExtendMin, copy speed x kExtendSec), never beyond the lead (fold LOW 1). */
inline float ExtendDist(float copySpeed, float lead)
{
    float e = CopySpeed(copySpeed) * kExtendSec;
    if (e < kExtendMin) e = kExtendMin;
    if (lead > 0.0f && e > lead) e = lead;
    return e;
}

/* A destination (destX, destZ) within minSep of the last one (lastX, lastZ), the owner moving along the UNIT heading
 * (dirX, dirZ): push it forward by ExtendDist; if that is still within minSep of the last, the last one pushed forward
 * instead. 1 = *outX/*outZ hold the extended destination; 0 = refused (no heading, or it would land within minSep of
 * the last or within minFromCopy of the copy - the engine would drop it). */
inline int ExtendTooClose(float destX, float destZ, float lastX, float lastZ, float dirX, float dirZ,
                          float copyX, float copyZ, float copySpeed, float lead, float minSep, float minFromCopy,
                          float* outX, float* outZ)
{
    const float dn = dirX * dirX + dirZ * dirZ;
    if (!(dn > 0.81f && dn < 1.21f)) return 0;   /* not a unit heading (NaN fails too) */
    const float e = ExtendDist(copySpeed, lead);
    float x = destX + dirX * e, z = destZ + dirZ * e;
    float sx = x - lastX, sz = z - lastZ;
    if (!(sx * sx + sz * sz >= minSep * minSep))
    {
        x = lastX + dirX * e; z = lastZ + dirZ * e;
        sx = x - lastX; sz = z - lastZ;
        if (!(sx * sx + sz * sz >= minSep * minSep)) return 0;
    }
    const float cx = x - copyX, cz = z - copyZ;
    if (!(cx * cx + cz * cz >= minFromCopy * minFromCopy)) return 0;
    *outX = x; *outZ = z;
    return 1;
}

/* One read of the agent's request status for the current destination. status < 0 = the read faulted (or no agent):
 * returns -1. Any status other than kStatusLanded marks the request as seen pending; kStatusLanded after that = landed
 * (returns 1). A 1 never preceded by anything else since the issue (the previous path's, or an issue the engine dropped)
 * is not trusted: returns 0 and the caller keeps its timer floor. */
inline int LandedStep(int status, bool* seenPending)
{
    if (status < 0) return -1;
    if (status != kStatusLanded) { *seenPending = true; return 0; }
    return *seenPending ? 1 : 0;
}

/* fold M1: may this copy issue now? sinceIssue = seconds since its last issue. Never within kMinIssueGapSec; within
   floorSec only once the last path LANDED. (The owner-stopped exact-aim issue bypasses this in the caller, as before.) */
inline bool IssueGateBlocks(double sinceIssue, bool landed, double floorSec)
{
    if (sinceIssue < kMinIssueGapSec) return true;
    return !landed && sinceIssue < floorSec;
}

/* fold M2: at an issue - is an old LEAD path still being walked under it? A non-lead (exact-aim) issue replacing a live lead
   destination, or replacing an exact-aim one issued over a lead path that had not landed yet, inherits it; the caller
   clears it once the new path LANDED. A lead issue, or no live destination, clears it. */
inline bool OldLeadStillWalked(bool haveDest, bool destLead, bool leadWalking, bool landed, bool newIsLead)
{
    if (newIsLead || !haveDest) return false;
    return destLead || (leadWalking && !landed);
}

/* THE OWNER'S MOVE-ORDER GOAL ON THE WIRE (sender). The owner's INTENT carries its movement destination while its task is a
   Move order; the other game's copy uses that goal as the end of its path. A goal that moved (a second click) under an
   unchanged task would otherwise reach the other game only with the 5 s refresh. */
const float  kGoalResendDist   = 2.0f;    /* re-send INTENT when a Move order's goal moved more than this ... */
const double kGoalResendGapSec = 0.5;     /* ... and at least this long after the last INTENT for that character ... */
const float  kGoalJumpDist     = 10.0f;   /* ... or at once when it moved more than this (a new click, not drift) */
/* SENDER: re-send a character's INTENT although its task and subject did not change? Only for a Move order whose goal
 * (goalX, goalZ) moved more than kGoalResendDist from the one last sent (sentX, sentZ), and not within kGoalResendGapSec
 * of the last INTENT (sinceSent seconds ago) unless it jumped more than kGoalJumpDist. */
inline bool GoalResendDue(bool moveOrder, float goalX, float goalZ, float sentX, float sentZ, double sinceSent)
{
    if (!moveOrder) return false;
    const float mx = goalX - sentX, mz = goalZ - sentZ;
    const float d2 = mx * mx + mz * mz;
    return d2 > kGoalResendDist * kGoalResendDist && (sinceSent >= kGoalResendGapSec || d2 > kGoalJumpDist * kGoalJumpDist);
}

}   /* namespace cooppath */

#endif
