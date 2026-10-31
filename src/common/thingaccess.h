#pragma once
// thingaccess.h - WHO OWNS THE THING, AS THIS GAME'S PLAYER SEES IT: own / teammate / friend / stranger (owner decisions 1187 a,
// 1188). Pure: the plugin (policy.cpp) and the offline suite compile this same header. Every site that must treat another
// player's box, downed character or building by the owner's action table asks Classify once per faction, on the main thread,
// and keeps the answer in a table the sites read.
//   own       this game's player faction.
//   teammate  a player who shares this game's player's team (the world server's team table).
//   friend    a player whose OWN side towards this game's player is the players tab's ALLY (TabAlly: the value that tab writes,
//             or the engine's ally flag; a stance that could not be read is no friend). Box and downed-character loot follow
//             the host's base-access option: allowed only under `shared`; under `owner`, `locked` and an option not heard yet a
//             friend is treated as a stranger.
//   stranger  any other player: under `shared` it may open another player's box (the click hooks, unchanged), and a take shows
//             as stealing (no crime between players); under `owner` / `locked` the game's own lock rules (owner 1192 a).
//   not a player  every other faction (the game's own rules, always).
// C++03 (VS2010 v100).

namespace swaccess {

enum Who { kNotPlayer = 0, kOwn = 1, kTeam = 2, kFriendAllowed = 3, kFriendRefused = 4, kStranger = 5 };
const int kWhoCount = 6;
/* the base-access option's values, as the plugin's policy.h numbers them (checked there at compile time) */
enum Policy { kPolicyUnknown = 0, kPolicyShared = 1, kPolicyOwner = 2, kPolicyLocked = 3 };

/* own: the faction is this game's player faction; otherPlayer: it is another player's faction on this game (a stand-in, or the
   coop-p<n> record a save carries); sameTeam: that player shares this game's player's team; allyTowardsMe: that player's side
   towards this game's player is ally; basePolicy: the host's base-access option. */
/* the players tab's ALLY (relations.cpp RelateSet writes this value through the engine's setter; the engine's own ally flag
   counts too); 1 = the stance was read. Ordinary standing drift a little under the value still counts. */
const float kTabAllyValue = 100.0f;
const float kTabAllySlack = 0.5f;
inline bool TabAlly(int read, float relation, bool allyFlag) { return read == 1 && (allyFlag || relation >= kTabAllyValue - kTabAllySlack); }

inline int Classify(bool own, bool otherPlayer, bool sameTeam, bool allyTowardsMe, int basePolicy)
{
    if (own) return kOwn;
    if (!otherPlayer) return kNotPlayer;
    if (sameTeam) return kTeam;
    if (allyTowardsMe) return basePolicy == kPolicyShared ? kFriendAllowed : kFriendRefused;
    return kStranger;
}

/* a box or a downed character of this owner is used as this game's player's own: no stealing chance, cursor or label */
inline bool LikeOwn(int who) { return who == kOwn || who == kTeam || who == kFriendAllowed; }

/* the order from "most own" to "least own"; kNotPlayer is not on it */
inline int Rank(int who)
{
    switch (who)
    {
    case kOwn: return 0;
    case kTeam: return 1;
    case kFriendAllowed: return 2;
    case kFriendRefused: return 3;
    case kStranger: return 4;
    default: return -1;
    }
}

/* an item move between two windows (a box or character on each side): the move is judged by the side that is least this player's
   own. A side that is not a player's at all leaves the move to the game's own rules (kNotPlayer). */
inline int PairClass(int a, int b)
{
    const int ra = Rank(a), rb = Rank(b);
    if (ra < 0 || rb < 0) return kNotPlayer;
    return ra >= rb ? a : b;
}

/* the site answer: the engine's "stealing" answer is replaced by "not stealing" only for a teammate's or an allowed friend's
   thing; this player's own things keep the engine's answer, and every other owner too */
inline bool ClearsStealing(int pairClass) { return pairClass == kTeam || pairClass == kFriendAllowed; }

/* the trade / loot window choice (ForgottenGUI::_showTradeWindow 0x790D00, last argument: 1 always the shop window, 2 never, any
   other value the engine's own choice; decomp_790d00.txt:56-90). For another player's thing (owner 1188, 1189 a):
     a box, or a character DOWN or carried (the engine's own test there, Character_inRagdoll 0x7D08A0, decomp_790d00.txt:61):
         never a shop window - a loot window (2); the owner test at 0x790DD8 gives a teammate's / allowed friend's the free flags
         (no stealing label), anyone else's shows stealing (no crime between players);
     an AWAKE character: a teammate's / allowed friend's opens as this player's own (2, free flags); anyone else's opens NO window
         (kNoWindow: the call is skipped - pickpocketing is the game's own road, not this one).
   Every other owner keeps the caller's choice. */
const int kTradeNever = 2;
const int kNoWindow = -1;
inline int WindowFor(int who, bool character, bool downOrCarried, int mode)
{
    if (who != kTeam && who != kFriendAllowed && who != kFriendRefused && who != kStranger) return mode;
    if (!character || downOrCarried) return kTradeNever;
    return LikeOwn(who) ? kTradeNever : kNoWindow;
}

/* the counter a judged move is added to: 0 team, 1 friend (allowed), 2 friend refused, 3 stranger, -1 none (own, not a player) */
inline int CounterOf(int who)
{
    switch (who)
    {
    case kTeam: return 0;
    case kFriendAllowed: return 1;
    case kFriendRefused: return 2;
    case kStranger: return 3;
    default: return -1;
    }
}
const int kCounters = 4;

/* the table the sites read: `n` factions with their classes; the class of `f`, kNotPlayer when it is not there */
inline int ClassIn(const void* const* facs, const int* who, int n, const void* f)
{
    if (f == 0) return kNotPlayer;
    for (int k = 0; k < n; ++k)
        if (facs[k] == f) return who[k];
    return kNotPlayer;
}

/* the factions of `facs` (n) whose things are used as this player's own (LikeOwn), into `out` (cap); the count written.
   Those past `cap` are counted in *dropped. */
inline int LikeOwnList(const void* const* facs, const int* who, int n, const void** out, int cap, int* dropped)
{
    int w = 0, d = 0;
    for (int k = 0; k < n; ++k)
    {
        if (facs[k] == 0 || !LikeOwn(who[k]) || who[k] == kOwn) continue;
        if (w < cap) out[w++] = facs[k]; else ++d;
    }
    if (dropped != 0) *dropped = d;
    return w;
}

}
