#ifndef SW_COMMON_STALECOPY_H
#define SW_COMMON_STALECOPY_H
/* A COPY THAT NO LONGER STANDS WHERE ITS OWNER'S CHARACTER IS. Pure: no engine, no network - the offline suite tests every
   decision here.

   1. A SPAWN for another game's character this game already holds a live copy of. The SPAWN carries where the owner's character
      stands now. A copy within kStaleDist of that spot is the same character where it should be: the SPAWN is a repeat and is
      ignored (a replayed or duplicated SPAWN must not create twins). A copy farther away is stale - it was made long ago (a joiner
      that sat in the character editor keeps the copies made when it entered the world) and nothing moved it since. Such a copy
      is retired (not a death) unless its owner's fresh MOVE sample says it stands where its owner is (the SPAWN came late:
      StaleCopyFreshKept below), it is the watched player or a player-faction character (never retired here), or one of rule 2's
      own checks holds (StaleCopyKeep below) - then the SPAWN is ignored as a repeat; RepeatSpawnStep below gives that order.
      When the SPAWN's spot is loaded here the same SPAWN makes the copy there; when it is not, nothing
      is made (a copy made in a sleeping area would be put away again at once) and the uid is booked as a lost copy at that spot,
      so its owner is asked to send it again once that spot is loaded here (lostcopy.h). A load test that could not be asked
      leaves the copy as it is.

   2. The puppet drive: a copy more than the far-snap distance from its owner's target, at a progress-window edge where the far
      snap would place it, but the target lies in an area this game has not loaded. The copy cannot be placed there, and walking
      it there leaves this game's characters following a body that is not where its owner is. It is retired and booked lost at the
      owner's target in the same way - only when the owner's sample is fresh, the copy is not carried or in a cage, bed or building
      slot (or that could not be read), and the load test answered "not loaded" (not "could not ask"). */

namespace stalecopy {

const float kStaleDist = 250.0f;   /* world units: the puppet drive's far-snap distance (replicate.cpp kLostDist) */

enum { kSpawnIgnore = 0, kSpawnRebuild = 1, kSpawnRetire = 2 };

/* uidMine / twin / copyLive: 0 or 1. distXZ: the live copy's horizontal distance from the SPAWN's spot (NaN or negative = the
   copy's position could not be read). spotLoaded: 1 loaded here, 0 not loaded, -1 the question could not be asked. */
inline int RepeatSpawnDecide(int uidMine, int twin, int copyLive, float distXZ, int spotLoaded)
{
    if (uidMine != 0 || twin != 0 || copyLive == 0) return kSpawnIgnore;
    if (!(distXZ > kStaleDist)) return kSpawnIgnore;   /* close enough, or unreadable (NaN compares false) */
    if (spotLoaded == 1) return kSpawnRebuild;
    if (spotLoaded == 0) return kSpawnRetire;
    return kSpawnIgnore;
}

/* Why a stale copy (rule 1) is kept rather than retired: the checks rule 2 makes before its load test. moveDrives: 1 the copy
   stands or crawls, 0 knocked down. ragdolled, gaveUp (the puppet drive gave up placing it), attachedOrUnread (carried, in a cage,
   bed or building slot, or that read failed): 0 or 1. The first reason that holds is returned; kKeepNone = the copy may be
   retired. */
enum { kKeepNone = 0, kKeepDown = 1, kKeepRagdoll = 2, kKeepGaveUp = 3, kKeepAttached = 4 };

inline int StaleCopyKeep(int moveDrives, int ragdolled, int gaveUp, int attachedOrUnread)
{
    if (moveDrives == 0) return kKeepDown;
    if (ragdolled != 0) return kKeepRagdoll;
    if (gaveUp != 0) return kKeepGaveUp;
    if (attachedOrUnread != 0) return kKeepAttached;
    return kKeepNone;
}

inline const char* StaleCopyKeepName(int keep)
{
    switch (keep)
    {
    case kKeepNone:     return "nothing holds it";
    case kKeepDown:     return "knocked down";
    case kKeepRagdoll:  return "ragdolled";
    case kKeepGaveUp:   return "the puppet drive gave up placing it";
    case kKeepAttached: return "carried, in a cage, bed or building slot, or its state could not be read";
    default:            return "unknown reason";
    }
}

/* Rule 1's fresh check. A SPAWN rides the reliable channel and carries where the owner's character stood when it was sent, so a
   catch-up stream can hand it over late, while the MOVE stream keeps the copy where its owner is. 1 = the copy is kept and the
   SPAWN ignored: the copy stands farther than kStaleDist from the SPAWN's spot (distToSpawn), its puppet holds an owner sample
   younger than the far snap's stale limit (hasFreshSample: 0 or 1 - replicate.cpp kSnapMaxSampleAgeSec, rule 2's own 'fresh'
   test), and the copy stands within kStaleDist of that sample (distToSample; NaN or negative = unreadable, which never keeps). */
inline int StaleCopyFreshKept(int hasFreshSample, float distToSample, float distToSpawn)
{
    if (!(distToSpawn > kStaleDist)) return 0;   /* not stale by the SPAWN (or unreadable): not this check's case */
    if (hasFreshSample == 0) return 0;
    return (distToSample >= 0.0f && distToSample <= kStaleDist) ? 1 : 0;   /* NaN compares false */
}

/* Rule 1's order once RepeatSpawnDecide has answered (act): Ignore ends it; then the fresh check (freshKept: StaleCopyFreshKept);
   then a protected copy (isProtected: the watched player or a player-faction character - spawn.cpp WithdrawNotDestroy - is never
   retired here); then a keep reason (keep: StaleCopyKeep); only then is the copy retired as act says. spawn.cpp RepeatSpawnAct
   reads them in this order and asks a later one only when every earlier one passed, so the kept count holds only copies rule 1
   would otherwise retire. */
enum { kStepAct = 0, kStepIgnore = 1, kStepFreshKept = 2, kStepProtected = 3, kStepKept = 4 };

inline int RepeatSpawnStep(int act, int freshKept, int isProtected, int keep)
{
    if (act == kSpawnIgnore) return kStepIgnore;
    if (freshKept != 0) return kStepFreshKept;
    if (isProtected != 0) return kStepProtected;
    if (keep != kKeepNone) return kStepKept;
    return kStepAct;
}

/* 1 = retire the copy and book it lost at its owner's target. farPlaceable: every far-snap gate before the load test passed
   (catchup on, standing, not ragdolled, not given up, a window edge, no placement pending). targetLoaded: as spotLoaded above.
   sampleFresh: the owner's last sample is younger than the snap's stale limit. attachedOrUnread: carried, in a cage, bed or
   building slot, or that read failed. */
inline int FarUnloadedRetire(int farPlaceable, int targetLoaded, int sampleFresh, int attachedOrUnread)
{
    return (farPlaceable != 0 && targetLoaded == 0 && sampleFresh != 0 && attachedOrUnread == 0) ? 1 : 0;
}

}  // namespace stalecopy

#endif
