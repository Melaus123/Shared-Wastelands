/* src/common/hovground.h - task 3 (0.4.0 contributor brief): WHICH GROUND ITEM A HAND-OVER LINE MAY REMOVE.
 *
 * A hand-over line from the ground (`g:<name>`) stores the item's name at the take. After a restart the holder's game looks for it
 * on its ground and removes it. It used to accept any stack of the same type within 0.5 units that matched record, count, quality
 * and level, so another player's identical stack lying close by could be deleted. Now a candidate is accepted only when it IS the
 * item: found by an exact lookup (never the 0.5-unit fallback), its id (GrIdOf: its shared name, else its current key) equal to
 * the stored name, and its fields matching. No such item means "not there" - at worst a duplicate stays, never a wrong deletion.
 * Pure: compiled by the game and by the offline suite. */
#pragma once
#include <string>

namespace hovrec
{
const int kGroundOk = 0, kGroundNoName = 1, kGroundFuzzy = 2, kGroundOtherId = 3, kGroundFields = 4;

/* stored: the line's name (`where` without "g:"); candId: the candidate's GrIdOf; fuzzy: found by the near fallback;
   fieldsMatch: HovItemIs said it is the same stack. Returns kGroundOk to accept, else why it was refused. */
inline int GroundCandidateVerdict(const std::string& stored, const std::string& candId, int fuzzy, int fieldsMatch)
{
    if (stored.empty()) return kGroundNoName;
    if (fuzzy != 0) return kGroundFuzzy;
    if (candId != stored) return kGroundOtherId;
    if (fieldsMatch == 0) return kGroundFields;
    return kGroundOk;
}
inline const char* GroundVerdictWord(int v)
{
    return v == kGroundOk ? "ok" : v == kGroundNoName ? "no stored name" : v == kGroundFuzzy ? "found only near, not by its name"
         : v == kGroundOtherId ? "another item's id" : "its fields differ";
}
}
