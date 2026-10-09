/* src/common/cageask.h - A GUARD'S CAGING OF ANOTHER GAME'S CHARACTER IS ALWAYS ANSWERED.
 *
 * A guard on the game running an area cages its COPY of a character another game drives; that game (the owner) is asked once
 * to cage its own character (MSG_PRISON IN), and the owner's STATE pose then holds the copy in the cage. A request the owner
 * never acted on left the copy caged here and free on the owner's screen. The guard's game keeps the ask open until the
 * owner's word shows the cage, the copy leaves the cage, or the owner refuses; it counts the owner's STATEs since the last
 * send (an event, never a clock), sends the same request again after kCageAskOwnerStates of them, and after kCageAskSendsMax
 * sends stops asking. Only the owner's refusal takes the copy out: an owner whose character sits in a cage it cannot name sends
 * no cage in its STATE and takes each resend as already caged, so a copy taken out on silence would be free here and caged there.
 *
 * Pure: no engine memory, no globals, no Windows; the offline suite drives it. C++03 (VS2010 v100).
 */
#pragma once

namespace cageask {

const int kCageAskOwnerStates = 3;   /* the owner's STATEs after a send that do not show the cage before it is sent again */
const int kCageAskSendsMax    = 3;   /* sends (the first one included) before the asking stops */

const int kCageAskSettled = 0;   /* the owner's word shows the cage: the ask is done */
const int kCageAskGone    = 1;   /* the copy is out of the cage here: nothing left to ask */
const int kCageAskRefused = 2;   /* the owner refused: the refusal takes the copy out */
const int kCageAskWait    = 3;   /* the owner has not yet sent enough STATEs to judge */
const int kCageAskResend  = 4;   /* send the same request again */
const int kCageAskGiveUp  = 5;   /* every send went unanswered: stop asking; the copy stays caged (only a refusal takes it out) */

inline int CageAskStep(bool copyCaged, bool ownerSaysCaged, bool refused, int ownerStatesSinceSend, int sends)
{
    if (refused) return kCageAskRefused;
    if (!copyCaged) return kCageAskGone;
    if (ownerSaysCaged) return kCageAskSettled;
    if (ownerStatesSinceSend < kCageAskOwnerStates) return kCageAskWait;
    if (sends < kCageAskSendsMax) return kCageAskResend;
    return kCageAskGiveUp;
}

/* The owner's side: a request arriving while one waits keeps the waiting one's first-arrival time when it is the same game asking
   about the same cage (a resend), so the owner's own give-up limit is not restarted by resends. */
inline bool PrisonInKeepsFirstAt(bool prevExists, bool samePeer, bool sameKey)
{
    return prevExists && samePeer && sameKey;
}

}   /* namespace cageask */
