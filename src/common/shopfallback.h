#pragma once
/* T-1381 fix attempt 1 (H-T1381-shopfallback; F1124, F1146, F1147): the shopping goal's random-building fallback, refused in another
   player's town. The 'Shopping' goal's target pick (1.0.65 0x998920 / 1.0.68 0x9997F0) asks findShop for each needed item, else -
   only when the town owner's player-interface pointer (+0x250) is 0 - asks the fallback 0x9FB2E0 / 0x9FC1B0 for a random building
   of that town. Only this game's player faction carries that pointer, so on each game the copy of the OTHER player's town (owned
   here by that player's stand-in) took the fallback and NPCs went for the player's house (T2481: 42 picks of the joiner's house on
   the host's game). The detour (policy.cpp detour_ShopFallback) answers 'no building' for a town whose owner is a stand-in, as the
   engine itself does for this game's own player's town (the pick then returns 0 - no target); every other town asks the engine.
   No engine reads here. */
namespace shopfb {

const int kAskEngine = 0;   /* call the engine's fallback as it is */
const int kRefuse = 1;      /* answer 'no building' without calling it (another player's town: a stand-in or a saved player faction not this game's) */

/* ownerFound: the town's owner faction was read and is not null; isStandIn: that faction is another player's here (PolicyOtherPlayerFaction) */
inline int Decide(int ownerFound, int isStandIn)
{
    return (ownerFound != 0 && isStandIn != 0) ? kRefuse : kAskEngine;
}

const long long kSayFirst = 10;    /* the first 10 refusals each write a line */
const long long kSayStep = 10;     /* then the 100th, 1000th, 10000th ... (review fold LOW: a town re-picked every tick stays a few lines) */

/* the refusal numbered n (1 = the first of this game) writes a line? */
inline int SayRefusal(long long n)
{
    if (n <= 0) return 0;
    if (n <= kSayFirst) return 1;
    for (long long m = kSayFirst * kSayStep; m > 0 && m <= n; m *= kSayStep) if (m == n) return 1;
    return 0;
}

}   // namespace shopfb
