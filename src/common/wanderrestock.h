/* T-1 B2 (owner decision 109, 2026-09-28; protocol 87) - WHO RESTOCKS A WANDERING TRADER'S PACKS.
 *
 * A wandering trader (a squad whose leader has no home building) restocks through ActivePlatoon::refreshInventory 0x4FEB50 ->
 * 0x959C40, which ROLLS RANDOM STOCK into its pack animals' packs, so two games restocking the same squad diverge. Since T-1 B1
 * one game runs each NPC squad whole - the game that runs its leader - so only that game restocks it:
 *
 *   squad unreadable                          -> run here (fail OPEN, as every read failure of the restock gate does - named)
 *
 * T-1 B2 fold (review F1/F3, 2026-09-28): the answer is keyed on the squad's PACK WEARERS, not on its formal leader (+0xA0) -
 * B1 runs a squad by its agreed acting leader, and after B1 one game runs every member, so the game running the wearers is the
 * game that runs the squad. The POPULATION is the wearers (members whose inventory holds a pack); a squad with no wearer yet
 * (the engine may add the packs in this very restock) is judged by all its members (WanderPopulation).
 *
 *   no member carries a uid (UNRUN)           -> the AREA rule town shops follow (owner 109, approved text: "A wandering trader
 *                                                that neither game controls yet: I recommend it restocks only once a game takes
 *                                                control of it, the same rule town shops already follow."): the game whose area
 *                                                holds the squad position restocks NOW - it is the game that will adopt the
 *                                                squad, and its spawn kit carries the stocked packs to the other game; the other
 *                                                game skips. The plugin asks the shops' own verdict (ItAreaVerdictAt), so the
 *                                                loading window answers as it does for shops (the world loads stocked).
 *                                                A squad position that will not read fails open (named).
 *   the population all run here               -> run here: this game is the writer
 *   the population all run by the other game  -> skip: the other game restocks and sends the packs whole (MSG_CLOTHING, BAGK rows)
 *   anything else - mine and the other's, or some with no uid while another member carries one (a hand-over or an adoption in
 *   flight)                                   -> skip and ask again soon (split wait)
 *
 * THE DEADLINE (Platoon+0x1F8; refreshInventory is its only writer, so a skip that writes nothing is re-asked every update
 * pass - P6y). Per answer:
 *   run / runUnrunArea / runUnreadable -> nothing: the engine's own restock re-arms it.
 *   skipNotWriter                      -> one full engine refresh period (ItRestockDefer): the writer restocks this squad and
 *                                         its packs arrive whole; asking sooner would only be refused again.
 *   skipUnrun / skipSplit              -> a SHORT step, 1/kWanderShortRetryDiv of that period: neither answer is a writer
 *                                         restocking on our behalf. skipUnrun: this game will be the copy once the area
 *                                         holder adopts the squad, so its deadline does not matter - and if the area passes
 *                                         to this game first, this game must stock the squad within a short step, not a
 *                                         whole period later. skipSplit: the split ends when the hand-over lands.
 *
 * Pure: no engine memory, no Windows, no globals; the plugin (items.cpp) and the offline suite compile the same code.
 * C++03 (VS2010 v100).
 */
#pragma once

namespace coopwander {

const int kWanderRun            = 0;   /* the population is all this game's: restock here */
const int kWanderSkipNotWriter  = 1;   /* the population is all the other game's: skip, it restocks (full-period deadline) */
const int kWanderSkipUnrun      = 2;   /* no member carries a uid and the other game holds the area: skip (short step) */
const int kWanderRunUnreadable  = 3;   /* the squad or its position would not read: run here (fail open, named) */
const int kWanderRunUnrunArea   = 4;   /* no member carries a uid and this game holds the area: restock here (owner 109, as shops) */
const int kWanderSkipSplit      = 5;   /* the population is split between the games / partly unrun: skip (short step) */

/* the area verdict at the squad position - asked only for an UNRUN squad */
const int kWanderAreaNotMine = 0, kWanderAreaMine = 1, kWanderAreaUnread = -1;

/* the short step of skipUnrun / skipSplit: 1/240 of the engine's refresh period (100 clock units at the 24000 default). The
   unit of the engine clock is not established; the choice is relative - about 240 asks per period instead of one per update
   pass (P6y), and never more than 1/240 of a period between the moment the answer changes and the restock. */
const int kWanderShortRetryDiv = 240;

/* The population: the wearers' run classes when the squad has any wearer, else every member's (wearers = 0 then, so the
   non-wearer counts are all the members). w* = wearers this game runs / the other game runs / no uid; m* = the other members. */
inline void WanderPopulation(int wMine, int wOther, int wNone, int mMine, int mOther, int mNone, int* pMine, int* pOther, int* pNone)
{
    const int wearers = wMine + wOther + wNone;
    *pMine  = (wearers > 0) ? wMine  : mMine;
    *pOther = (wearers > 0) ? wOther : mOther;
    *pNone  = (wearers > 0) ? wNone  : mNone;
}

/* squadRead: 1 = the squad read. membersWithUid: members carrying a uid (0 = UNRUN). popMine / popOther / popNone: the population
   this game runs / the other game runs / nobody runs (WanderPopulation). area: kWanderArea* at the squad position (read only
   when unrun; any value otherwise). */
inline int WanderRestockDecide(int squadRead, int membersWithUid, int popMine, int popOther, int popNone, int area)
{
    if (squadRead == 0) return kWanderRunUnreadable;
    if (membersWithUid == 0)
    {
        if (area == kWanderAreaMine) return kWanderRunUnrunArea;
        if (area == kWanderAreaNotMine) return kWanderSkipUnrun;
        return kWanderRunUnreadable;
    }
    if (popMine > 0 && popOther == 0 && popNone == 0) return kWanderRun;
    if (popOther > 0 && popMine == 0 && popNone == 0) return kWanderSkipNotWriter;
    return kWanderSkipSplit;   /* mixed, partly unrun, or (never expected) an empty population: nobody writes, ask again soon */
}

/* 1 = call the engine's restock on this game */
inline int WanderRestockRunsHere(int decision)
{
    return (decision == kWanderRun || decision == kWanderRunUnrunArea || decision == kWanderRunUnreadable) ? 1 : 0;
}

/* the deadline a skip writes: 1 = one full engine period (the writer restocks); 0 = not that */
inline int WanderRestockFullDefer(int decision) { return (decision == kWanderSkipNotWriter) ? 1 : 0; }
/* 1 = the short step (the answer can change soon and nobody writes meanwhile) */
inline int WanderRestockShortRetry(int decision) { return (decision == kWanderSkipUnrun || decision == kWanderSkipSplit) ? 1 : 0; }

inline const char* WanderRestockName(int decision)
{
    switch (decision)
    {
    case kWanderRun:           return "run";
    case kWanderSkipNotWriter: return "skipNotWriter";
    case kWanderSkipUnrun:     return "skipUnrun";
    case kWanderRunUnreadable: return "runUnreadable";
    case kWanderRunUnrunArea:  return "runUnrunArea";
    case kWanderSkipSplit:     return "skipSplit";
    default:                   return "?";
    }
}

}   /* namespace coopwander */
