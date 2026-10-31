// clearnear.h - owner decision 1205 a (2026-10-31): the TEST-ONLY `clearnear` lever's pure parts (store.cpp ClearNearLever).
// `clearnear <x> <z> <r>` / `clearnear here <r>`: on the game that holds the area, every AWAKE outside squad with a member within
// r of (x, z) - or of this game's watched player character - is removed through the `platoonwipe` path: WANDERING groups only. Kept:
// player-side squads (a player faction or a stand-in), squads the run itself spawned, a town's own squads (the squad's home town is a
// real town: its guards, residents and shop keepers), and squads this game does not run (another game holds the area, or a member is
// another game's character). T1692: a nest's animals also carry their nest as home town - a nest home is not kept as a town. `here`
// also sweeps around every other player's character this game knows. The argument check, the home kind, the keep decision and the
// reply. PURE: no engine, no plugin types.
#pragma once
#include <sstream>
#include <string>
namespace swclearnear {
const float kMaxRadius = 2000.0f;   // a bigger circle is refused: the lever clears a run's spot, never a region
const float kMaxCoord = 1.0e7f;
// 1 = parsed: here = 1 for `here <r>` (x, z untouched), else x, z read. r in (0, kMaxRadius]. Nothing may follow.
inline int Parse(const std::string& args, int* here, float* x, float* z, float* r)
{
    std::istringstream is(args);
    std::string first;
    if (!(is >> first)) return 0;
    float px = 0.0f, pz = 0.0f, pr = 0.0f;
    int h = 0;
    if (first == "here") h = 1;
    else
    {
        std::istringstream fx(first);
        std::string tail;
        if (!(fx >> px) || (fx >> tail)) return 0;
        if (!(is >> pz)) return 0;
    }
    if (!(is >> pr)) return 0;
    std::string extra;
    if (is >> extra) return 0;
    if (!(pr > 0.0f && pr <= kMaxRadius)) return 0;   // also refuses NaN
    if (h == 0 && !(px > -kMaxCoord && px < kMaxCoord && pz > -kMaxCoord && pz < kMaxCoord)) return 0;
    *here = h; *r = pr;
    if (h == 0) { *x = px; *z = pz; }
    return 1;
}
// This game runs the area: its world-server seat is the area's holder. A game with no seat (no world server: a lone game) holds
// every area it has loaded; an unknown holder (-1) with a seat is not this game.
inline int HolderHere(int holderSlot, int mySlot)
{
    if (mySlot < 0) return 1;
    return (holderSlot >= 0 && holderSlot == mySlot) ? 1 : 0;
}
enum { kWipe = 0, kKeepPlayers = 1, kKeepRunSpawned = 2, kKeepNotHolder = 3, kKeepTown = 4 };
// The squad's home: none, a real town, or a base that is not a town (a nest).
enum { kHomeNone = 0, kHomeTown = 1, kHomeBase = 2 };
// T1692: the home's kind from its record. homePresent 0 = no home town. typeRead 0 = the record's type did not read: kept as a town
// (never a wipe on doubt). type = the home record's intFields "type": the engine builds a type-0 record as the small non-Town object
// (a nest - RootObjectFactory 0x580CE0) and any other type as a Town.
inline int HomeKind(int homePresent, int typeRead, int type)
{
    if (homePresent == 0) return kHomeNone;
    if (typeRead == 0) return kHomeTown;
    return type == 0 ? kHomeBase : kHomeTown;
}
// The keep decision for one awake squad with a member inside a circle, in this order: a player-side squad, a squad the run
// spawned, a town's squad (homeKind kHomeTown), a squad this game does not run (holderHere 0, or otherRun = a member is another
// game's character); else wiped - a wandering outside group, or a nest's, this game runs.
inline int Classify(int playerSide, int runSpawned, int homeKind, int holderHere, int otherRun)
{
    if (playerSide != 0) return kKeepPlayers;
    if (runSpawned != 0) return kKeepRunSpawned;
    if (homeKind == kHomeTown) return kKeepTown;
    if (holderHere == 0 || otherRun != 0) return kKeepNotHolder;
    return kWipe;
}
// `ok clearnear wiped=<n> base=<n> kept=<n> (players=<n> runSpawned=<n> notHolder=<n> town=<n>) faults=<n>`; base = the wiped
// squads whose home was a nest
inline std::string Reply(int wiped, int baseWiped, int players, int runSpawned, int notHolder, int town, int faults)
{
    std::ostringstream o;
    o << "ok clearnear wiped=" << wiped << " base=" << baseWiped << " kept=" << (players + runSpawned + notHolder + town) << " (players=" << players
      << " runSpawned=" << runSpawned << " notHolder=" << notHolder << " town=" << town << ") faults=" << faults;
    return o.str();
}
}
