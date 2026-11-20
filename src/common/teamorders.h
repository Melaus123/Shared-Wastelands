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
// T-1022: the list built for another player's character outside the team loses Talk only (TalkOffered, FilterForOtherPlayer below).
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
        if (t == kTalk || !OwnOffered(t, inCage)) { if (removed != 0) ++*removed; continue; }   /* Talk: TalkOffered */
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
// T-1022 (owner 1246 a, 2026-10-31): Talk (PLAYER_TALK_TO 12) is not offered on ANOTHER player's character outside this player's team
// (a copy in a player's stand-in faction, as the conversation start's speaker test reads it - speech.cpp TkPlayerSpeaker, an old save's
// stand-in record included), in a cage or not: that start refuses every conversation whose speaker is another player's character, so
// the order would only open a refused start (freeing a caged one is its own order, RELEASE_PRISONER 110, which stays). A teammate's
// character loses Talk too, caged or not (the same start refuses it: a teammate's character is another player's); a character of no
// player keeps Talk.
inline bool TalkOffered(bool otherPlayer, bool teammate, bool inCage)
{
    (void)inCage;
    return !otherPlayer && !teammate;
}
// The list built for a non-team other player's character, in place: every Talk entry goes, every other order stays in its order (the
// engine's own list: attack, knock-out, kidnap, loot, first aid, follow, bodyguard, release, shackles). Returns the new count; *removed
// (may be 0) gets how many Talk entries went. A list that cannot be read (n < 0) is left as it is.
inline int FilterForOtherPlayer(int* tasks, int n, int* removed)
{
    if (removed != 0) *removed = 0;
    if (tasks == 0 || n <= 0) return n < 0 ? n : 0;
    int w = 0;
    for (int r = 0; r < n; ++r)
    {
        if (tasks[r] == kTalk && !TalkOffered(true, false, false)) { if (removed != 0) ++*removed; continue; }
        tasks[w++] = tasks[r];
    }
    return w;
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
// ---- T-1167 piece 1 (owner 1633 / 1634 / 1635): THE PLAYER TRADE WINDOW's right-click entry, its label and the click ----
// LOOT_TARGET 26 is the engine's own order on a character that is down (decomp_7a6b76.txt:581, the word 'Loot'). On ANOTHER player's
// character (a teammate's or not) that is AWAKE and that isEnemy says is not an enemy, the menu offers it too, labelled 'Trade', and
// the click opens the mod's TRADE window instead of giving the order (policy.cpp T-1167 block; tradewin.cpp the window).
const int kLoot = 26;
// The list built for a character gets order 26: another player's character, awake, not an enemy (the engine's isEnemy answer).
inline bool TradeOffered(bool otherPlayer, bool awake, bool enemy) { return otherPlayer && awake && !enemy; }
inline bool HasLoot(const int* t, int n)
{
    if (t == 0 || n <= 0) return false;
    for (int i = 0; i < n; ++i) if (t[i] == kLoot) return true;
    return false;
}
// The fill hook adds 26 (through the engine's own list add) only when it is offered and the list does not hold it yet.
inline bool TradeFillAdds(bool offered, const int* t, int n) { return offered && n >= 0 && !HasLoot(t, n); }
// After the engine's list add appended 26: the first 26 in the list moved to the front, every other order kept in its order.
// Returns the index the 26 was at (0 = already first), -1 when the list holds none (nothing moved).
inline int TradeToFront(int* t, int n)
{
    if (t == 0 || n <= 0) return -1;
    int at = -1;
    for (int i = 0; i < n && at < 0; ++i) if (t[i] == kLoot) at = i;
    if (at <= 0) return at;
    for (int i = at; i > 0; --i) t[i] = t[i - 1];
    t[0] = kLoot;
    return at;
}
// The label (owner 1634): order 26 on another player's character reads 'Trade' while the character is awake (the trade window);
// knocked out it keeps the engine's own word ('Loot': a stand-in faction has no player interface, decomp_79a730.txt:249). Any
// other order, target or owner keeps the engine's word.
const int kLabelEngine = 0, kLabelTrade = 1;
inline int TradeLabel(int task, bool character, bool otherPlayer, bool awake, bool enemy)   /* T-1167 fold: an enemy keeps the engine's word */
{
    return (task == kLoot && character && otherPlayer && awake && !enemy) ? kLabelTrade : kLabelEngine;
}
inline const char* TradeWord() { return "Trade"; }
// The squared distance on the ground (x, z) is under the engine's trade range constant (0x70C6C0 compares the same way, strictly);
// a range that was not read (0 or less) is never in range.
inline bool InTradeRange(float ax, float az, float bx, float bz, float rangeSq)
{
    if (!(rangeSq > 0.0f)) return false;
    const float dx = bx - ax, dz = bz - az;
    return dx * dx + dz * dz < rangeSq;
}
// The order pre-check's decision (0x5D0EB0, hooked): order 26 given by this player's own character to another player's AWAKE
// character that is not an enemy -> the TRADE window (in range) or the refusal "Out of trading range"; either way the engine's
// order is not given. Everything else -> the engine's own pre-check, unchanged (a knocked-out character keeps the loot road).
const int kClickEngine = 0, kClickWindow = 1, kClickRefused = 2;
inline int TradeClick(int task, bool actorOwn, bool otherPlayer, bool awake, bool enemy, bool inRange)
{
    if (task != kLoot || !actorOwn || !otherPlayer || !awake || enemy) return kClickEngine;
    return inRange ? kClickWindow : kClickRefused;
}
inline const char* ClickWord(int d) { return d == kClickWindow ? "window" : d == kClickRefused ? "refused" : "engine"; }
// ---- T-1406 (owner 1757, mock-up 1758): 'Invite to faction' in the right-click menu on another player's character ----
// The entry carries a mod-chosen order number that no engine order uses: the order-word switch 0x79A730 ends at 0x122 and answers ""
// for any other number (decomp_79a730.txt:116-125, 179-184), the restricted-orders filter 0x7F1810 passes every number while that
// mode is off, and the per-entry test 0x7F5A40 answers 0 for it. It is the largest number in the list, so the menu (an ordered set)
// shows it last. The engine never acts on it: the click hook on newPlayerTask 0x7F9AB0 takes it before the task table 0x281F90.
const int kInviteOrder = 0x1406;   /* 5126 */
// Shown only when (1) the menu's own checks passed for the clicked character (another player's, awake, NEITHER side
// hostile by PeerHostile below, one of this player's own characters selected - policy.cpp InviteMenuOffer), (2) the PLAYERS tab's own rule would show INVITE TO FACTION
// for that player (teamscreen::ViewFor(..).right == kActInvite: online, in no faction of players, this player not a mere member),
// and (3) all three hooks (fill, word, click) are in - without the click hook the number would reach the engine's task table.
inline bool InviteOffered(bool tradeChecks, bool tabInvites, bool allHooks) { return tradeChecks && tabInvites && allHooks; }
// ---- T-1406 (T2524 leg 4, F1206; owner 1633 / 1758 a): 'hostile' for ANOTHER player's character, as the menu, the label and the
// click read it ----
// The engine's isEnemy answered 'not an enemy' with this player's standing -100 towards that player's faction (T2524 L4), so a
// player set HOSTILE on the PLAYERS tab was still offered Trade and Invite. For another player's character the answer is the stance
// the PLAYERS tab shows (playerstab.cpp PlayersTabStance: its own rows): Trade is hidden when EITHER side is hostile (a trade needs
// both sides, owner 1633), and so is Invite (F1214, the owner 2026-11-18: 'invite ... shouldn't be available for hostile') - the
// same answer as the tab's own invite rule, teamscreens.h InviteStanceAllows, which PlayersTabInvites reads after this check. A teammate is never
// hostile (its side is held at ally, as the isEnemy hook already answered). No row on the tab (stance not read): the engine's answer.
// NPCs never come here (policy.cpp TrFactsOf: another player's character only).
const int kHostileForTrade = 0, kHostileForInvite = 1;
inline bool PeerHostile(int kind, bool stanceRead, bool teammate, bool youHostile, bool themHostile, bool engineEnemy)
{
    if (teammate) return false;
    if (!stanceRead) return engineEnemy;
    (void)kind;   /* T-1406 (F1214): Trade and Invite now give the same either-side answer; the kind stays for the callers' lines */
    return youHostile || themHostile;
}
inline bool HasOrder(const int* t, int n, int task)
{
    if (t == 0 || n <= 0) return false;
    for (int i = 0; i < n; ++i) if (t[i] == task) return true;
    return false;
}
// The fill adds the number (through the engine's own list add) only when offered and the list does not hold it yet.
inline bool InviteFillAdds(bool offered, const int* t, int n) { return offered && n >= 0 && !HasOrder(t, n, kInviteOrder); }
// The word (approved text, exactly) for the entry's number; every other number keeps the engine's word.
inline bool InviteLabelled(int task) { return task == kInviteOrder; }
inline const char* InviteWord() { return "Invite to faction"; }
// The click: the entry's number is caught (the invitation sent, the menu closed as the engine closes it) and never passed on.
inline bool InviteCaught(int task) { return task == kInviteOrder; }
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
