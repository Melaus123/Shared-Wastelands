#pragma once
// T-1090: A BUILDING WHOSE RESIDENT TEMPLATE MAKES NOBODY. The pure half, ONE header compiled into the plugin (towngen.cpp) and the
// offline suite.
//
// A building's residents come from its resident template (Building+0xF0, a squad template): populateBuilding 0x57ED90 makes a squad
// for every entry of the template's "housemates" list (decomp_57ed90:128-182), then one squad from the template itself through
// createRandomSquad 0x582F80. createRandomSquad returns at once, making nobody, when the template's "leader", "squad", "squad2",
// "animals", "animals2" and "choosefrom list" lists are all empty (decomp_582f80:146-291, 341-352). "slaves" and "prisoners" are
// read only after that return (decomp_582f80:1162, 1300); a template with only those lists still counts as able to make people
// here, so it is repaired as before. The "choosefrom list" makes "num random chars" people, rolled up to
// "num random chars max" when that is larger (the template's int map +0x178, decomp_582f80:504-522), so a choosefrom list with a
// count of 0..0 makes nobody. Some templates are meant to stay empty (rebirth.mod 44421 '_ruined empty building (DEFAULT)'):
// the town repair skips a building whose template makes nobody instead of asking for its residents again and again.
// A template that can roll people (44426 'hub squatters', choosefrom with 0..3) is repaired as before.

namespace tplnobody {

// the reference lists (template map +0x2B8) whose entries make people
const int kLeader = 0, kSquad = 1, kSquad2 = 2, kAnimals = 3, kAnimals2 = 4, kSlaves = 5, kPrisoners = 6, kHousemates = 7, kChooseFrom = 8;
const int kLists = 9;

inline int NameIs(const char* name, unsigned long long len, const char* want)
{
    unsigned long long n = 0;
    while (want[n] != 0) ++n;
    if (name == 0 || n != len) return 0;
    for (unsigned long long j = 0; j < n; ++j) if (name[j] != want[j]) return 0;
    return 1;
}

// the k* index of a reference list's name (`len` its length), -1 for a list that makes nobody
inline int ListIndex(const char* name, unsigned long long len)
{
    static const char* const k[kLists] = { "leader", "squad", "squad2", "animals", "animals2", "slaves", "prisoners", "housemates", "choosefrom list" };
    for (int i = 0; i < kLists; ++i) if (NameIs(name, len, k[i]) != 0) return i;
    return -1;
}

// the int-map key (template map +0x178): 1 = "num random chars" (the count), 2 = "num random chars max", 0 another
inline int RandomKey(const char* name, unsigned long long len)
{
    if (NameIs(name, len, "num random chars") != 0) return 1;
    if (NameIs(name, len, "num random chars max") != 0) return 2;
    return 0;
}

// 1 = the template can make at least one person, 0 = it makes nobody. counts[kLists]: the entries of each list; randMin / randMax:
// the two random counts (0 when absent - the engine's lookup gives 0 for a missing key). The roll is randMin, or a roll between
// randMin and randMax when randMax is larger: above 0 only when one of them is.
inline int CanMakePeople(const int* counts, int randMin, int randMax)
{
    for (int i = 0; i < kLists; ++i) if (i != kChooseFrom && counts[i] > 0) return 1;
    return (counts[kChooseFrom] > 0 && (randMin > 0 || randMax > 0)) ? 1 : 0;
}

// a road skips the building only on a template read that makes nobody: 1 skip; an unread template (-1) keeps today's behaviour
inline int Skips(int canMake) { return canMake == 0 ? 1 : 0; }

}   // namespace tplnobody
