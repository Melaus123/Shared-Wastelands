/* src/common/owncarry.h - T-1265 (owner 1246 a, 512): MAY ANOTHER GAME'S CARRIER PICK UP THIS GAME'S OWN CHARACTER HERE?
 *
 * Another game's carrier (its copy here) picks up a character THIS game drives. The engine's own carry needs no helpless body: the
 * right-click menu offers carry (LIFT_PERSON_PLAYER_ORDER 225) on an awake character of one's own faction, and the mod's teammate
 * menu keeps that order on an awake teammate's character (owner 1246 a: "teammates pick up shall remain"; owner 512: teammates as if
 * one person owned both sets). So the body's own game takes the carry when:
 *   - the body is helpless here (lying, limp, unconscious or about to be) and in nothing - the arrest1 rule (OwnBodyMayBeCarried):
 *     any carrier;
 *   - the body is awake and standing here and in nothing, and the carrier is a PLAYER character of a slot that shares this game's
 *     team (the team table, never a stand-in alone): that player's carry of its teammate's awake character;
 *   - (fold 1, review LOW) the body is awake and standing here and in nothing, and the carrier is a copy (another game drives it) in
 *     THIS game's own faction - a member of this game's own squad another game runs (a save-born body adopted in place as another
 *     game's copy, spawn.cpp AdoptExistingTwin, e.g. a squad handed over): in single player any squad member may carry another.
 *     An NPC of any other faction never carries an awake own character.
 * Every other case is refused, with its reason: in a bed or cage, inSomething unreadable, awake and standing (the carrier is no
 * teammate's player character), or the lying-state unreadable. A body that is helpless and in a BED is taken out of it first
 * (spawn.cpp, before this decision) and is not decided here.
 *
 * downed: CopyDowned's answer - 1 helpless (lying 2..4, ragdoll, unconscious or a wake-up clock), 0 standing awake, -1 unreadable.
 * carrier: kCarrier* - what the carrier is (spawn.cpp CarrierKindOf).
 *
 * Pure: no engine memory, no globals, no Windows; the offline suite drives it. C++03 (VS2010 v100).
 */
#pragma once

namespace owncarry {

const int kOwnCarryAccept             = 0;   /* helpless and in nothing: any carrier (arrest1) */
const int kOwnCarryAcceptTeamAwake    = 1;   /* awake, standing, in nothing; the carrier is a teammate's player character */
const int kOwnCarryRefuseInSomething  = 2;   /* in a bed or cage here */
const int kOwnCarryRefuseInsUnread    = 3;   /* inSomething could not be read */
const int kOwnCarryRefuseAwake        = 4;   /* awake and standing; the carrier is neither a teammate's player character nor own-faction copy */
const int kOwnCarryRefuseStateUnread  = 5;   /* the lying-state could not be read */
const int kOwnCarryAcceptOwnFaction   = 6;   /* awake, standing, in nothing; the carrier is a copy in this game's own faction */

const int kCarrierOther          = 0;   /* an NPC, a non-teammate's player character, this game's own carrier, or unreadable */
const int kCarrierTeamPlayer     = 1;   /* a copy that is a player character of a teammate's slot */
const int kCarrierOwnFactionCopy = 2;   /* a copy (another game drives it) in this game's own faction */

inline int OwnCarryDecide(bool insRead, int inSomething, int downed, int carrier)
{
    if (!insRead) return kOwnCarryRefuseInsUnread;
    if (inSomething != 0) return kOwnCarryRefuseInSomething;
    if (downed == 1) return kOwnCarryAccept;
    if (downed != 0) return kOwnCarryRefuseStateUnread;
    if (carrier == kCarrierTeamPlayer) return kOwnCarryAcceptTeamAwake;
    if (carrier == kCarrierOwnFactionCopy) return kOwnCarryAcceptOwnFaction;
    return kOwnCarryRefuseAwake;
}

inline bool OwnCarryAccepted(int decision)
{
    return decision == kOwnCarryAccept || decision == kOwnCarryAcceptTeamAwake || decision == kOwnCarryAcceptOwnFaction;
}

}   /* namespace owncarry */
