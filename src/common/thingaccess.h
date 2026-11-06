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

/* T-1222 (owner 1444): A WORLD RESOURCE NODE (an ore rock, coopown::WorldNodeOf) IS NOBODY'S. Its output opens for every player on
   every game - whatever the base-access option, the team table or any owner mark an older save gave it - as single player opens a
   rock after its miner's claim. Taking from it is no theft: its owner 'No Faction' (record nofac) is marked "not real" (Faction
   +0x1D0), so the engine runs no stealing check (0x710B70), shows no stealing cursor (0x7FB8A0) and records no crime (0x851DB0).
   Every other building keeps the rules below; another player's machine is judged by the base-access option (BoxAccessAllowed).

   THE CLICK GATE (OwnedByAPlayerFaction 0x546340) and THE CURSOR: at an answered click site (the click gate's or the click
   handler's - never the door panel's, a save, the navmesh worker or any other caller) a world node is answered as this player's
   own. engineAnswer: the engine's own answer (non-zero is kept as it is). */
inline bool NodeClickOpens(int engineAnswer, bool answeredClickSite, int worldNode)
{
    return engineAnswer == 0 && answeredClickSite && worldNode == 1;
}

/* T-1222 (owner 1444): THE LEFT CLICK'S WINDOW. A left click selects the building, and the building's select virtual (vtable +0x130,
   1.0.65 0x54FA10) opens its own inventory window only when it is built (vtable +0x228) and its faction has a player interface
   (+0x250; decomp_54fa10 :34-39). An ore rock stays 'No Faction' (its miner's claim is skipped), so the engine opens nothing for
   anyone. policy.cpp's detour runs the engine's select first and then opens the window itself only when this says so:
   worldNode: the building is a world resource node (coopown::WorldNodeOf on the live piece; 1 = yes);
   built: the select virtual's own finished byte (1 = built; 0 not, -1 unread);
   engineGate: the select virtual's own gate read the same way (1 = the engine opened the window itself; 0 it did not; -1 unread).
   A gate that could not be read still opens: showing a window that is already open only shows it again (decomp_6e5ea0 :48-52). */
inline bool NodeLeftOpens(int worldNode, int built, int engineGate)
{
    return worldNode == 1 && built == 1 && engineGate != 1;
}

/* THE REQUEST GATE (policy.cpp PolicyJudge, which BoxAccessAllowed asks): the rule that decides, in its order. The first step needs
   no owner read: a member's request, then a world node, then `shared` (an option not heard yet is judged as `shared`, as before);
   kJudgeReadOwners = read the building's owner and this game's own player and go on with JudgeAfterReads. */
enum JudgeRule
{
    kJudgeReadOwners = 0, kJudgeMember = 1, kJudgeWorldNode = 2, kJudgeShared = 3, kJudgeNotPlayerOwned = 4, kJudgeOwner = 5,
    kJudgeOwnerOnly = 6, kJudgeLockedOwner = 7, kJudgeLockedRefused = 8, kJudgeLockedEngineLock = 9
};
inline int JudgeFirst(bool member, int worldNode, int basePolicy)
{
    if (member) return kJudgeMember;
    if (worldNode == 1) return kJudgeWorldNode;
    if (basePolicy == kPolicyShared || basePolicy == kPolicyUnknown) return kJudgeShared;
    return kJudgeReadOwners;
}
/* after both owner reads succeeded (a failed read refuses before this): ownerIsPlayerSide = the owner is this game's player or
   another player's stand-in; isOwner = the requester is the owner; peerIsPlayer = the stand-in reads as a player faction to the
   engine (then `locked` cannot rely on the engine's lock and refuses). basePolicy is `owner` or `locked` here. */
inline int JudgeAfterReads(int basePolicy, bool ownerIsPlayerSide, bool isOwner, bool peerIsPlayer)
{
    if (!ownerIsPlayerSide) return kJudgeNotPlayerOwned;
    if (basePolicy == kPolicyOwner) return isOwner ? kJudgeOwner : kJudgeOwnerOnly;
    if (isOwner) return kJudgeLockedOwner;
    return peerIsPlayer ? kJudgeLockedRefused : kJudgeLockedEngineLock;
}
inline bool JudgeAllows(int rule) { return rule != kJudgeReadOwners && rule != kJudgeOwnerOnly && rule != kJudgeLockedRefused; }

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

/* T-1222 (owner 1452): ORE TAKEN FROM A ROCK INTO THIS PLAYER'S OWN THINGS IS NO THEFT. A rock (a world resource node,
   coopown::WorldNodeOf) stays nobody's on every game: its miner's claim is skipped, so its window's record says 'not free' (+10 = 0:
   the owner's faction has no player interface, 0x70E6D0 via 0x6E4EA0) and the engine answers 'stealing' for a move between the rock's
   window and this player's own - which makes the drop's range test strict (60 units, not 1000) and refuses the drop (T1939). Single
   player never meets it: mining claims the rock for the player first. The answer becomes 'not stealing' when one window's owner is a
   world node and the other window's owner's faction is this game's player faction (kOwn: its PlayerInterface, Faction +0x250), in
   either order; whatever the team table holds or whether it was written yet. Every other pair keeps the answers above.
   engineAnswer: the engine's own answer (0 = not stealing, kept); nodeA / nodeB: each window's owner is a world node (1 = yes);
   whoA / whoB: each window's owner's faction (Who). */
inline bool NodeTakeClearsStealing(int engineAnswer, int nodeA, int whoA, int nodeB, int whoB)
{
    if (engineAnswer == 0) return false;
    return (nodeA == 1 && whoB == kOwn) || (nodeB == 1 && whoA == kOwn);
}

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

/* T-1241 (decision 1487 a) TEST-ONLY READING: THE CURSOR A BOX SHOWS UNDER THE POINTER. PlayerInterface::buildingSelected (1.0.65
   0x7FBB40, 1.0.68 0x7FC6E0) is the hover itself: the frame's pick hands it the building under the pointer with the click byte
   (+0x2F3) clear, and then it only picks the cursor (the GUI's cursor setter 0x6E3A10) - it writes that cursor, so the lever does
   not call it; it makes the same reads in the same order and gives the number the hover would set. Read: decomp_7fbb40.txt :39-99
   (1.0.65); the 1.0.68 bytes 0x7FC729..0x7FCEF2 make the same calls at the same offsets (capstone, offline).
   - owned: OwnedByAPlayerFaction 0x546340 at the hover's own sites (ClickRet1 / 5 / 6, which the mod's hook answers - HoverOwned
     below). Neither owned nor the building's vtable +0x108: the hover gives up, no cursor set (the plain pointer).
   - the player interface's +0x268 byte set: no cursor set.
   - not finished (vtable +0x228's byte 0): 0xB. In use (vtable +0x308): 0x13. A seat (+0x1A0 set): the furniture road, not read here.
   - a box: function type (vtable +0x2F0) 0x12, or kind (vtable +0x358) 0xC / 9 / 7. No inventory (vtable +0x160), or an empty
     one (+0x18 == 0, decomp_4a093) that is not owned: no cursor set. Then the owner faction's +0x1D0 byte or owned: 2, the open
     cursor; else 0x18, the stealing cursor (a click on it shows 'Hold down the ALT key to steal items', decomp_7fbb40 :98-104).
   - anything else: the getter road - the building's own getMouseCursor (vtable +0x410), read for a UseableStuff by
     GetterRoadCursor below (T-1241, run T2045: a player-built chest is a UseableStuff, no box by these tests). */
struct HoverTerms
{
    int owned;         /* OwnedByAPlayerFaction's answer at the hover's sites */
    int selectable;    /* vtable +0x108 */
    int busy;          /* the player interface's +0x268 byte */
    int built;         /* vtable +0x228's byte */
    int inUse;         /* vtable +0x308 */
    int seat;          /* the building's +0x1A0 byte */
    int fnType;        /* vtable +0x2F0 */
    int kind;          /* vtable +0x358 */
    int haveInv;       /* vtable +0x160 answered an inventory */
    int invCount;      /* that inventory's +0x18 */
    int factionFree;   /* the owner faction's +0x1D0 byte */
};
const int kHoverNoCursor = -1, kHoverSeatRoad = -2, kHoverOwnGetter = -3;
const int kHoverOpen = 2, kHoverSteal = 0x18, kHoverUnbuilt = 0xB, kHoverInUse = 0x13;
/* T-1241 the getter road (GetterRoadCursor, below): UseableStuff::getMouseCursor's answers, and the two hover roads not read */
const int kHoverCharacterRoad = -4, kHoverCageRoad = -5;
const int kHoverOwnUse = 9, kHoverPickLock = 0xF, kHoverTypeEight = 0xD;
inline bool HoverIsBox(int fnType, int kind) { return fnType == 0x12 || kind == 0xC || kind == 9 || kind == 7; }
inline int BoxHoverCursor(const HoverTerms& t)
{
    if (t.owned == 0 && t.selectable == 0) return kHoverNoCursor;
    if (t.busy != 0) return kHoverNoCursor;
    if (t.built == 0) return kHoverUnbuilt;
    if (t.inUse != 0) return kHoverInUse;
    if (t.seat != 0) return kHoverSeatRoad;
    if (!HoverIsBox(t.fnType, t.kind)) return kHoverOwnGetter;
    if (t.haveInv == 0 || (t.owned == 0 && t.invCount == 0)) return kHoverNoCursor;
    return (t.factionFree != 0 || t.owned != 0) ? kHoverOpen : kHoverSteal;
}
inline const char* HoverCursorMeaning(int c)
{
    switch (c)
    {
    case kHoverOpen: return "open";
    case kHoverSteal: return "steal";
    case kHoverNoCursor: return "plain";
    case kHoverUnbuilt: return "unfinished";
    case kHoverInUse: return "in-use";
    case kHoverSeatRoad: return "seat-road-not-read";
    case kHoverOwnGetter: return "own-getter-not-read";
    case kHoverOwnUse: return "own-use";
    case kHoverPickLock: return "pick-lock";
    case kHoverTypeEight: return "type-eight";
    case 0: return "none-type-0x10";
    case kHoverCharacterRoad: return "character-road-not-read";
    case kHoverCageRoad: return "cage-road-not-read";
    default: return "other";
    }
}
/* the mod's answer at those sites: policy.cpp detour_OwnedByAPlayerFaction for a click-handler return address, step by step (an
   engine 'own' is kept; a world node opens; outside a team and outside `shared` the engine's answer; a teammate's building is
   ours; under `shared` another player's StorageBuilding is answered as ours, any other class keeps the engine's answer).
   memberOfTeam: this game's player is in a team and the owner shares it. */
inline int HoverOwned(int engineAnswer, int worldNode, bool shared, bool inTeam, bool memberOfTeam, bool standIn, bool storageClass)
{
    if (engineAnswer != 0) return 1;
    if (NodeClickOpens(engineAnswer, true, worldNode)) return 1;
    if (!inTeam && !shared) return 0;
    if (!memberOfTeam && (!standIn || !shared)) return 0;
    if (memberOfTeam) return 1;
    return storageClass ? 1 : 0;
}

/* T-1241 (run T2056, PARTIAL): THE CURSOR ASKS THE TAKE'S QUESTION. Under `shared` a friend's take from another player's General
   Storage Chest was no theft (site 1's ClearsStealing(PairClass) on the kit table's class) while the chest's cursor said 'steal'
   (0x18, T2056 log-B:22080): the cursor hook passed teammates only. UseableCursorMod's `member` is now this one answer: a teammate by
   the team table (the cursor's own test before, kept so a game whose kit table is not written yet answers as before), or an owner
   whose kit-table class (Classify) makes the take from this player's own side no theft - the take's own function, with this player's
   side as the other window. A friend under any other option (kFriendRefused), a stranger and a faction that is no player's: false. */
inline bool CursorLikeOwn(bool teammate, int ownerWho) { return teammate || ClearsStealing(PairClass(kOwn, ownerWho)); }

/* T-1241 (run T2045, VACUOUS): THE GETTER ROAD - the cursor of a building the hover does not treat as a box. A player-built General
   Storage Chest is a '.?AVUseableStuff@@' with function type 0xD and kind 2 (T2045 log-A:17917 / log-B:19405), so BoxHoverCursor
   answers kHoverOwnGetter for it. The hover then (decomp_7fbb40 :115-152) reads vtable +0x300 and, when that answers a thing whose
   +0x3A8 byte is set, takes a character road (it calls 0x7974F0 / 0x7FA870 - not read here); a function type 8 (a cage) may take
   cursor 9 from the selection; every other building gets the cursor its own getMouseCursor (vtable +0x410) returns. For a
   UseableStuff that slot is a jmp thunk (1.0.65 0x3BCBE) into UseableStuff::getMouseCursor (1.0.65 0x546C30, decomp_546c30), and
   THAT is where the owner is read: type 0x10 -> 0; the owner faction (vtable +0x58) with its player interface (+0x250) set -> 9,
   this player's own; no inventory (+0x160) -> 9 (type 8: 0xF locked / 0xD); locked (+0x408 / +0x400, 0x2AA550) -> 0xF; else 0x18,
   the loot cursor. The mod hooks that body (policy.cpp detour_UseableCursor) and remaps its answer; UseableCursorMod mirrors it:
   only 0xF / 0x18 / 0xD are touched; a world resource node -> 9; a teammate's object of any class -> 9 (0xD kept); a stranger's
   (another player's stand-in under `shared`) only when it is a StorageBuilding -> 9; everything else keeps the engine's answer. */
inline int UseableCursorMod(int engine, int worldNode, bool member, bool peerObject, bool storageClass)
{
    if (engine != kHoverPickLock && engine != kHoverSteal && engine != kHoverTypeEight) return engine;
    if (NodeClickOpens(0, engine != kHoverTypeEight, engine != kHoverTypeEight ? worldNode : 0)) return kHoverOwnUse;
    if (!member && !peerObject) return engine;
    if (!member && !storageClass) return engine;
    if (engine == kHoverTypeEight) return engine;
    return kHoverOwnUse;
}
/* character: vtable +0x300 answered a thing whose +0x3A8 byte is set; useableGetter: vtable +0x410 leads to UseableStuff::
   getMouseCursor; getter: that function's answer (the engine's, or the mod's remap of it). */
inline int GetterRoadCursor(int character, int fnType, int useableGetter, int getter)
{
    if (character != 0) return kHoverCharacterRoad;
    if (fnType == 8) return kHoverCageRoad;
    if (useableGetter == 0) return kHoverOwnGetter;
    return getter;
}

}
