/* src/common/bedhandoff.h - THE OWNER ENTERS THE BED FROM THE CARRY, AS THE ENGINE DOES.
 *
 * Another game's rescuer carries this game's knocked-out character (its copy there) into a bed. That game's engine puts the copy
 * in the bed in one call (Task_PutInSomething's bed branch): the carrier lets go, the body is put down with no ragdoll, then
 * setBedMode, the move and stopAction. The owner is asked (BED IN) to do the same with its own character, which its carry mirror
 * shows carried here by the rescuer's copy. BedOwnStep says what the owner does with a waiting request:
 *   - carried by a copy of the asking game's character: into the bed straight from that carry (FromCarry);
 *   - carried by anyone else (a third game's copy, this game's own character): wait - the request's own limit refuses it -
 *     or, once a pick-up again was posted, refuse;
 *   - lying here after a fall began (the rescuer's "carrying nothing" was applied first and the body ragdolled): the carrier copy
 *     it was dropped from picks it up again (Recarry) and the bed follows from that carry; that copy carrying something else
 *     refuses;
 *   - lying still with no fall: into the bed at once (PutIn);
 *   - awake (not helpless) or already in a bed or cage: refused.
 * BedRecarryMayPickUp bounds the pick-ups again one request posts (each one the engine refused). CarryEndHoldsDrop: the carry
 * mirror's ragdoll drop of an own body ("carrying nothing" from the carrier's game) waits while a bed request from that game for
 * the body is pending - the bed is entered from the carry instead.
 *
 * Pure: no engine memory, no globals, no Windows; the offline suite drives it. C++03 (VS2010 v100).
 */
#pragma once

namespace bedhandoff {

const int kBedOwnWait              = 0;   /* carried by someone else: the request waits */
const int kBedOwnFromCarry         = 1;   /* the asking game's carrier copy holds it: into the bed straight from that carry */
const int kBedOwnRecarry           = 2;   /* put down here already (a fall began): that carrier copy picks it up again */
const int kBedOwnPutIn             = 3;   /* lying still: into the bed at once */
const int kBedOwnRefuseAwake       = 4;   /* not helpless here */
const int kBedOwnRefuseInSomething = 5;   /* already in a bed or cage here */
const int kBedOwnRefuseCarrierBusy = 6;   /* the carrier copy carries something else, or someone else carries it after the pick-up again */

const int kBedRecarryPickupsMax = 3;   /* pick-ups again one request posts before it is refused */

/* carriedByAskerCopy: the character carrying it here is a copy of the asking game's character. recarryPosted: a pick-up again
   was posted for this request. carrierCarriesBody / carrierCarriesOther: the carrier copy a pick-up again uses carries this
   body / something else. ragdollActive: a fall began (a ragdoll, or a ragdoll request waiting). */
inline int BedOwnStep(bool inSomething, bool beingCarried, bool carriedByAskerCopy, bool mayBeCarried, bool ragdollActive,
                      bool recarryPosted, bool carrierCarriesBody, bool carrierCarriesOther)
{
    if (inSomething) return kBedOwnRefuseInSomething;
    if (beingCarried)
    {
        const bool asker = recarryPosted ? carrierCarriesBody : carriedByAskerCopy;
        if (!asker) return recarryPosted ? kBedOwnRefuseCarrierBusy : kBedOwnWait;
        return mayBeCarried ? kBedOwnFromCarry : kBedOwnRefuseAwake;
    }
    if (!mayBeCarried) return kBedOwnRefuseAwake;
    if (!recarryPosted && !ragdollActive) return kBedOwnPutIn;
    return carrierCarriesOther ? kBedOwnRefuseCarrierBusy : kBedOwnRecarry;
}

inline bool BedRecarryMayPickUp(int pickupsPosted)
{
    return pickupsPosted < kBedRecarryPickupsMax;
}

inline bool CarryEndHoldsDrop(bool bodyMine, bool bedPending)
{
    return bodyMine && bedPending;
}

}   /* namespace bedhandoff */
