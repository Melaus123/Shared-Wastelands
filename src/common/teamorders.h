// teamorders.h - T-546 step 7 (owner 512 / 512-a): a teammate's characters answer this player's orders as its own characters do, "as if
// one person owned both sets": the orders menu built on a teammate's character offers what the base game offers on one's own character.
// The engine decides which orders a target offers in three places (Steam 1.0.65 addresses; build/decomp_7a6b76.txt, decomp_7fa870.txt,
// decomp_79c040.txt, decomp_7963e0.txt):
//   PlayerInterface::isEnemy 0x79C040 answers "not an enemy" for the player's own faction before anything else; for any other faction it
//     can answer yes from fight memories as well as from the standing (a yes makes a right-click an attack);
//   PlayerInterface::characterSelected 0x7FA870 (right-click on a character): for a non-enemy of another faction, a sneaking selected
//     character gets the stealth knock-out; the player's own characters never reach that branch;
//   ContextMenu::showContextMenu 0x7A6020 builds the menu's list of orders for ONE character - the clicked character, or the person in a
//     clicked bed or cage (a click on a character in a bed or cage becomes a click on that furniture; the furniture's own orders then go
//     to the main menu and the person's to a nested menu, the fill 0x7A7440 called with sub = 1). It asks isEnemy about that character
//     at one call site on every branch but the cage's (the character's inSomething +0x2F8 == 2).
// What the plugin does (policy.cpp): isEnemy answers no for a teammate's character, the sneaking test answers no at the right-click's
// one call site for a teammate's character, and the list built for a teammate's character keeps only the orders below.
// PURE: no engine, no plugin types.
#pragma once
#include <cstdio>
#include <string>
namespace swteamord {
// The TaskTypes the menu builder offers on this player's OWN character, over all its branches (the engine's TaskType enum):
// FIRST_AID_ORDER 25, LOOT_TARGET 26, STAND_UP 28, STAY_CLOSE_TO_TARGET 31 (follow), BODYGUARD 45, FIRST_AID_ROBOT 60,
// PUT_DOWN_OBJECT 69, RELEASE_PRISONER 110 (a cage of one's own), CUT_SHACKLES 185, BRUTE_FORCE_SHACKLES 186, PICK_LOCK_ON_SHACKLES 201,
// GET_OUT_OF_CAGE_ESCAPE 207, LIFT_PERSON_PLAYER_ORDER 225 (carry: the carry-or-kidnap slot 0x7963E0 answers 225 for one's own
// faction), SPLINT_ORDER 249; and PLAYER_TALK_TO 12 only for a character in a cage (the cage branch asks the character's dialogue for
// any faction; the other branches offer talk only on another faction's character).
const int kOwnOffered[] = { 25, 26, 28, 31, 45, 60, 69, 110, 185, 186, 201, 207, 225, 249 };
const int kOwnOfferedCount = (int)(sizeof(kOwnOffered) / sizeof(kOwnOffered[0]));
const int kTalk = 12, kKidnap = 246, kCarry = 225;
inline bool OwnOffered(int task, bool inCage)
{
    if (task == kTalk) return inCage;
    for (int i = 0; i < kOwnOfferedCount; ++i) if (kOwnOffered[i] == task) return true;
    return false;
}
// The list built for a teammate's character, in place: an order the base game offers on one's own character stays where it is; kidnap
// 246 becomes carry 225 (the answer the slot gives for one's own faction), unless 225 is already in the list; every other order goes.
// Returns the new count; *removed and *replaced (either may be 0) get how many went and how many 246s became 225. A list that cannot
// be read (n < 0) is left as it is.
inline int FilterForTeammate(int* tasks, int n, bool inCage, int* removed, int* replaced)
{
    if (removed != 0) *removed = 0;
    if (replaced != 0) *replaced = 0;
    if (tasks == 0 || n <= 0) return n < 0 ? n : 0;
    bool carry = false;
    for (int r = 0; r < n; ++r) if (tasks[r] == kCarry) carry = true;
    int w = 0;
    for (int r = 0; r < n; ++r)
    {
        int t = tasks[r];
        if (t == kKidnap && !carry) { t = kCarry; carry = true; if (replaced != 0) ++*replaced; }
        if (!OwnOffered(t, inCage)) { if (removed != 0) ++*removed; continue; }
        tasks[w++] = t;
    }
    return w;
}
// Whether the menu's fill filters the list it is handed. `ordersMark`: during this build the engine asked isEnemy, at the menu builder's
// own call site, about a teammate's character - the character the orders are built for, so every later fill of the build is its list.
// Otherwise the character found before the build decides: `forTarget` = the clicked character is a teammate's and is in no furniture (its
// orders go to the main menu, sub 0), `forOccupant` = the person in the clicked furniture is a teammate's (its orders go to the nested
// menu, sub 1; the furniture's own orders, sub 0, are never filtered).
inline bool MenuFiltered(bool ordersMark, unsigned long long sub, bool forTarget, bool forOccupant)
{
    return ordersMark || (sub != 0 ? forOccupant : forTarget);
}
// A faction is a teammate's when one of the cells holds it (the cells: this game's teammates' factions, 0 = empty).
inline bool TeammateFaction(const void* f, void* const volatile* cells, int n)
{
    if (f == 0 || cells == 0) return false;
    for (int k = 0; k < n; ++k) if (cells[k] == f) return true;
    return false;
}
// The sneaking test's answer: forced to "not sneaking" only inside a right-click on a teammate's character, on the same thread, at the
// right-click's own call site; every other caller gets the engine's answer.
inline bool SneakForcedOff(bool clickOnTeammate, bool sameThread, unsigned long long retRva, unsigned long long siteRva)
{
    return clickOnTeammate && sameThread && siteRva != 0 && retRva == siteRva;
}
// The isEnemy call is the menu builder's own when the call returns to that site, inside a menu build, on the build's thread.
inline bool MenuEnemySite(bool inBuild, bool sameThread, unsigned long long retRva, unsigned long long siteRva)
{
    return inBuild && sameThread && siteRva != 0 && retRva == siteRva;
}
// "61,45,31" (the order numbers as listed), "none" for an empty list.
inline std::string ListText(const int* t, int n)
{
    if (t == 0 || n <= 0) return "none";
    std::string s;
    char b[16];
    for (int i = 0; i < n; ++i) { std::sprintf(b, "%d", t[i]); if (i) s += ","; s += b; }
    return s;
}
// The orders in a list that the base game does not offer on one's own character (in the list's order), "none" when there are none.
inline std::string NotOwnIn(const int* t, int n, bool inCage)
{
    if (t == 0 || n <= 0) return "none";
    std::string s;
    char b[16];
    for (int i = 0; i < n; ++i) if (!OwnOffered(t[i], inCage)) { std::sprintf(b, "%d", t[i]); if (!s.empty()) s += ","; s += b; }
    return s.empty() ? std::string("none") : s;
}
}
