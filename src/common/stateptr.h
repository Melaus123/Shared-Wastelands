/* src/common/stateptr.h - T-296 fix 2 (H055, T642 crash at kenshi+0x6A456): A PLATOON'S STATE RECORD (+0x40) LIVES
 * OUTSIDE ITS OWN CONTAINER.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or includes
 * a header, so the offline suite drives it directly. store.cpp CreateUnknownSquad / StateBeforeWake call it.
 *
 * WHY (Read, 1.0.65): Platoon+0x40 is the platoon-state record (loadStateData 0x7EC550:45). The platoon's own
 * container is emptied by the store's reload (LoadPod = clearAndFree + load) AND by the engine's own sleep
 * (0x4FE980:28 clearAndFree(active+0x10), then 0x677F50 writes through +0x40). The engine keeps every platoon's
 * state as a type-0x22 record in the GLOBAL state container (0x7EC550:116), which only a world load / teardown empties.
 * So +0x40 must never be one of the container's records when a clear can run.
 *
 * THE WAKE DECISION (before any reload and before the original wake):
 *   tracked - the store made this squad's 0x22 record (its ORIGIN, recorded at creation or at a move - not guessed later)
 *   ours    - +0x40 is still that record
 *   walkOk  - the container walk finished
 *   member  - +0x40 is one of the container's records (only meaningful when walkOk)
 *   -> Move   : inside the container - alive now, freed by the next clear: copy it to a fresh global 0x22 record
 *      Keep   : our global record (a failed walk cannot change where it lives)
 *      Refuse : unknown - the reload (which empties the container) is refused; nothing is moved
 *      Leave  : outside the container (a save-loaded squad's global record) - never touched
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_STATEPTR_H
#define COOP_COMMON_STATEPTR_H

namespace coopstate {

enum { kStLeave = 0, kStKeep = 1, kStMove = 2, kStRefuse = 3 };

inline int StateWakeDecide(int tracked, int ours, int walkOk, int member)
{
    if (walkOk && member) return kStMove;
    if (tracked && ours) return kStKeep;
    if (!walkOk) return kStRefuse;
    return kStLeave;
}

/* May the store's reload run? Only when +0x40 is known to be outside the container once the action is done. */
inline int StateReloadAllowed(int action, int moveOk)
{
    if (action == kStRefuse) return 0;
    if (action == kStMove && !moveOk) return 0;
    return 1;
}

/* CreateUnknownSquad builds the squad only on a complete 0x22 record outside its container. */
inline int StateCreateDecide(int have1E, int made22, int copyOk)
{
    return (have1E && made22 && copyOk) ? 1 : 0;
}

inline const char* StateActionWord(int a)
{
    switch (a)
    {
    case kStLeave:  return "leave";
    case kStKeep:   return "keep";
    case kStMove:   return "move";
    case kStRefuse: return "refuse";
    default:        return "?";
    }
}

}   /* namespace coopstate */

#endif
