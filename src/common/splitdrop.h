#pragma once
/* T-1417 (F1812 / F1814 / F1815, design D1): A HELD STACK DROPPED WHERE FEWER FIT.
   The engine's drop (placeItemFromMouse, build/decomp_714990.txt:442-463) asks how many of the carried item the target square holds;
   when that is fewer than the stack it splits the excess off into a NEW item (takeCertainAmountFrom, the fifth hook's clamp exit) and
   puts that new item on the cursor (:631-634). The mod used to send the pick's count (put back x4, take x4) while the engine had placed
   only part of it - the joiner got x4 from the host's answer PLUS the excess the engine kept on the cursor.
   The rule here: the placed count is the engine's own numbers (the carried count before the split minus the units it split off, read
   back off the carried object), never a prediction; the leftover is the units split off. Pure - shared with the offline suite. */
namespace splitdrop
{
enum { kOk = 0, kNoClamp = 1, kBadNumbers = 2, kNotDone = 3, kHandedAway = 4 };

/* q0: the carried object's count before the engine's split (the fifth hook's read); excess: the units the engine split off (its
   howmany); q1: the carried object's count read back after the split. kOk fills *placed (q0 - excess) and *leftover (excess). */
inline int Decide(int q0, int excess, int q1, int* placed, int* leftover)
{
    *placed = 0; *leftover = 0;
    if (excess <= 0) return kNoClamp;
    if (q0 <= 0 || excess >= q0) return kBadNumbers;
    if (q1 != q0 - excess) return kNotDone;
    *placed = q0 - excess; *leftover = excess;
    return kOk;
}
/* The count a TAKE (or a put-down) of the drop sends: the placed count when a split ran, else the carried count as before. */
inline int SendCount(int clampRan, int placed, int carried) { return (clampRan != 0 && placed > 0) ? placed : carried; }
/* The leftover's hold: units off the source stack (never the whole item at that square - after the put-back and the placed units'
   move the source square holds exactly the leftover's units on the writer, but a whole-item hold would lift whatever stands there). */
inline int LeftoverHoldWhole() { return 0; }
/* T-1417 fold 1: the leftover's held row keeps the pick's shape - units off a stack another game writes (the source half op 3) stay a
   split row off the original stack (that stack is still there); a whole item (op 1), or any leftover of this game's own stack (a give
   road: it stays ours), is a row of its own object. */
enum { kShapeWhole = 1, kShapeSplit = 3 };
inline int LeftoverShape(int mine, int srcOp) { return (mine == 0 && srcOp == 3) ? (int)kShapeSplit : (int)kShapeWhole; }
/* Totals check (the offline suite's): the writer's source after put-back whole, then the placed units moved, then the leftover held. */
inline int WriterSourceAfter(int sourceBefore, int carried, int placed, int leftover) { return sourceBefore + carried - placed - leftover; }
inline const char* ReasonName(int r)
{
    switch (r)
    {
    case kOk: return "ok";
    case kNoClamp: return "no split";
    case kBadNumbers: return "the engine's numbers do not describe a split (excess outside 0 < e < carried)";
    case kNotDone: return "the carried object's count read back is not carried minus excess";
    case kHandedAway: return "the engine handed the leftover away (a swap, decomp 607 / 619) - not on the cursor";
    default: return "?";
    }
}
const long long kSayCap = 40;   /* one line per clamped drop, the first 40 */
inline int SayNth(long long n) { return (n >= 1 && n <= kSayCap) ? 1 : 0; }
}
