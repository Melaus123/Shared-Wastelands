#pragma once
/* T-948 b (run T1505, Confirmed): another game's characters (copies) arrive on this game in ONE squad, as their own game has them;
   two engine roads then split them into new squads numbered as this game's own. The pure decisions of the two hooks
   (store.cpp detour_sqSplit / detour_sqMove); the offline suite tests them.
     SplitDecide - ActivePlatoon::_putTheSpecialCharactersInNewSquads 0x79B620: skipped only when the squad has members, every
       one of them was read and every one is a copy; a squad with any own or unread member, an unsure answer, or no members
       runs the engine's original.
     MoveDecide - Character::moveToNewSquad 0x5CDA50: a copy is refused unless the mod's own deliberate move is running on
       this thread (the pass depth); an own character, a character the mod does not know, or an unsure answer runs the
       original. copyTest: 1 copy, 0 not a copy, -1 unsure (treated as this game's own). */
namespace sqsplit {

const int kSplitRun = 0, kSplitSkip = 1;
const int kMoveRun = 0, kMoveRefuse = 1, kMovePass = 2;

/* members: the count read (-1 = unreadable); copies: members whose copy test answered 1; unsure: members whose test answered -1 */
inline int SplitDecide(int members, int copies, int unsure)
{
    return (members > 0 && unsure == 0 && copies == members) ? kSplitSkip : kSplitRun;
}

inline int MoveDecide(int copyTest, int passDepth)
{
    if (copyTest != 1) return kMoveRun;
    return passDepth > 0 ? kMovePass : kMoveRefuse;
}

}   /* namespace sqsplit */
