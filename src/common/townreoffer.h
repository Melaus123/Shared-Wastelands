#pragma once
// T-392 (owner decisions 2026-10-01: 335 a, 336 a, 337 a): NO TOWN STAYS EMPTY. The pure half of the town gates' set-aside
// work and its second chance. ONE header compiled into the plugin (towngen.cpp) and the offline suite, so the two cannot hold
// different ideas of one decision.
//
// - DEFER: a bar crowd (Town::spawnTheBarFlies 0x9FBE50) or a building's residents (RootObjectFactory::populateBuilding
//   0x57ED90) are made ONCE, at the first load of a never-saved zone. A gate that cannot answer yet - no notebook link, no
//   fresh area map, or nobody holds the area - SETS THE WORK ASIDE instead of refusing it for the visit. Another game holding
//   the area is an answer (the engine's own end state follows); so is the engine freeing the world (kInventTeardown, -2).
// - RE-OFFER: from the engine's own per-town check-up (Town::periodicTick 0x92BE50, main thread). No timer and no
//   fallback (337 a): the second chance is that check-up finding this game the holder (generate) or another game the holder
//   (answered-other); anything else keeps waiting.
namespace townreoffer {

// DeferCause: why a gate sets work aside. `linked` = the notebook link; `may` = zones' MayInventFromView answer
// (1 invent, 0 not mine, -1 no fresh map, -2 teardown); `held` = AreaViewTS's held (1 = another game holds the area).
const int kDefNone = 0;         // answered: invent, another game holds it, or teardown - nothing is set aside
const int kDefNoMap = 1;        // no fresh area map
const int kDefNobody = 2;       // the map answered and nobody holds the area (and it is not this game's to presume)
const int kDefNoNotebook = 3;   // no notebook link yet
const int kDefTestLever = 4;   // T-581: the TEST-ONLY `owedtest aside on` - work this game would make now is set aside
const int kDefOwed = 5;        // T-581: work this game would make now is an owed row it does not hold (made under a claim, owedpop.h)
inline int DeferCause(int linked, int may, int held)
{
    if (linked == 0) return kDefNoNotebook;
    if (may == -1) return kDefNoMap;
    if (may == 0 && held != 1) return kDefNobody;
    return kDefNone;
}
inline const char* DeferCauseName(int c)
{
    if (c == kDefNoMap) return "no-map";
    if (c == kDefNobody) return "nobody";
    if (c == kDefNoNotebook) return "no-notebook";
    if (c == kDefTestLever) return "test-lever";
    if (c == kDefOwed) return "owed";
    return "none";
}

// Decide: what the town's check-up does with one set-aside item.
//   zoneLive        1 = the item's zone is in this game's active zone list
//   filled          1 = this game's own object already holds the result (bar: list mark 1 with count 0 - a later engine
//                     pass used it); the entry is simply forgotten
//   filledElsewhere 1 = the notebook records a fill by some game (bar: town_bars lastFilled >= 0) - never a second roll
//   notebookPending 1 = the notebook lists people for the town this game has not placed yet, from an area that holds the town
//                   (StoreTownPeoplePending answers townpending::kTownHeld - src/common/townpending.h)
//   may             T-392 fold (review LOW): the load-time gate's own answer for the item's sector (zones' MayInventFromView:
//                   1 invent, 0 not mine, -1 no fresh map, -2 teardown) - the same presumptions as at load
//   held            AreaViewTS's held (1 = another game holds the area)
const int kReWait = 0;       // keep waiting (no map, nobody, zone not live here, notebook people still to come)
const int kReGenerate = 1;   // this game holds the area: re-run through the normal gate
const int kReOther = 2;      // another game holds it, or it was filled elsewhere: settled the way the gate settles it today
const int kReFilled = 3;     // already used up on this game: forget the entry
inline int Decide(int zoneLive, int filled, int filledElsewhere, int notebookPending, int may, int held)
{
    if (filled != 0) return kReFilled;
    if (zoneLive == 0) return kReWait;
    if (filledElsewhere != 0) return kReOther;
    if (notebookPending != 0) return kReWait;
    if (may == 1) return kReGenerate;
    if (may == 0 && held == 1) return kReOther;
    return kReWait;
}

// T-438 (t438-townreoffer; a regression of T-392, found in T788): populateBuilding 0x57ED90 is not only the residents. Before it
// makes a squad it switches the building's interior on (0x561750 on Building+0x1F0 - the named furniture layout: bar, shop
// counters, storage) and sets the owner faction (vt+0xA0) - decomp_57ed90:77-79, 106-123. A building SET ASIDE still runs it at
// the game's own load event, with the squads inside refused; the re-offer runs it again to make the squads.
// PopulateMode: what the detour (atLoad 1) or the re-offer (atLoad 0) does with the residents gate's answer (1 run, 0 set aside).
const int kPopRun = 0;        // run the engine call; its squads go through the normal creation gate
const int kPopRunAside = 1;   // run it with the squads set aside: faction and furniture now, residents later
const int kPopSkip = 2;       // do not run it (a re-offer set aside again: the furniture was loaded at load)
inline int PopulateMode(int atLoad, int gate)
{
    if (gate != 0) return kPopRun;
    return atLoad != 0 ? kPopRunAside : kPopSkip;
}
// InBuildingSquad: one creation inside a populateBuilding call on THIS thread (`inBuilding` = the gate's building-sector context
// names this thread). With the set-aside flag raised the squad is refused as SET ASIDE - never counted as a not-holder refusal.
const int kSqNormal = 0;
const int kSqSetAside = 1;
inline int InBuildingSquad(int inBuilding, int aside)
{
    return (inBuilding != 0 && aside != 0) ? kSqSetAside : kSqNormal;
}
// ReofferRunsPopulate (t438-a-townreoffer, manager 2026-10-02): does a residents re-offer run populateBuilding again? Only to
// GENERATE. The set-aside run at load already switched the interior on and set the faction, so for kReOther (another game holds
// the area) a second run's only effects would be the town's resident budget added a second time (decomp_57ed90:197-199) and a
// refused group - the entry is erased instead. 1 = run it.
inline int ReofferRunsPopulate(int decide)
{
    return decide == kReGenerate ? 1 : 0;
}
// RerunPopUndo (t438-b-townreoffer, manager 2026-10-02): how much to take back off the town's resident budget (Town+0xE0 bucket
// type 1: +8 int, +0xC float - the regrow budget 0x929150 reads) after a GENERATE re-run of populateBuilding: EXACTLY the re-run's
// own add, so every game keeps the one add its load-time run made, as before T-392. dInt / dFloat = the two fields' changes
// measured across the re-run. The tail adds the same headcount to both (decomp_57ed90:198-199), so an equal positive pair is that
// add; anything else returns 0 and the fields are left alone (a mismatch: something else moved a field; or nothing was added).
// t438-r1 (review MED, manager 2026-10-02): the +0xC float is the town's LIVE budget (it refills toward the +8 cap and is clamped there,
// decomp_8f3d50:17-23), so it often carries a fraction and adding N can round - an exact-equality test skipped the undo exactly when
// it mattered. The pair counts as the tail's add when the int rose and the float rose by the same amount within a rounding step
// (kRerunPopTol); the caller then restores the float to its exact BEFORE value and the int to after-minus-the-add (nothing else runs
// between the two reads - same thread, one engine call).
const double kRerunPopTol = 0.01;
inline long long RerunPopUndo(long long dInt, double dFloat)
{
    if (dInt <= 0) return 0;
    const double gap = dFloat - (double)dInt;
    if (gap > kRerunPopTol || gap < -kRerunPopTol) return 0;
    return dInt;
}
// FurnitureName: the interior read after the engine call - -1 unread, 0 no interior, 1 switched on but the layout pointer
// (interior+0x10) not set, 2 the layout pointer set.
inline const char* FurnitureName(int s)
{
    if (s == 2) return "loaded";
    if (s == 1) return "asked";
    if (s == 0) return "no-interior";
    return "unread";
}

// AsideStateTemplate (T-515): createRandomUnloadedSquad 0x57EA30, for a squad made for a building, first takes the building-state
// step 0x57E760(building, template) - designation, public day, "building ruined", nest entries (decomp_57e760) - with the call's own
// template, else the building's resident template (Building+0xF0); with neither it makes nothing (decomp_57ea30:28-34; with a
// building its other refusal cannot fire). A squad the mod SETS ASIDE takes the same step at the same moment.
const int kStateNone = 0;       // no building or no template: no step
const int kStateArg = 1;        // the call's own template
const int kStateBuilding = 2;   // the building's resident template
inline int AsideStateTemplate(int hasBuilding, int hasArgTemplate, int hasBuildingTemplate)
{
    if (hasBuilding == 0) return kStateNone;
    if (hasArgTemplate != 0) return kStateArg;
    return hasBuildingTemplate != 0 ? kStateBuilding : kStateNone;
}
// RerunStateSkips (T-515): inside the re-offer's populateBuilding for a building (inRerunForBuilding), the step is skipped when it
// already ran as its squads were set aside (atLoad): the nest list appends, so a second step would add each entry twice.
inline int RerunStateSkips(int inRerunForBuilding, int atLoad)
{
    return (inRerunForBuilding != 0 && atLoad != 0) ? 1 : 0;
}
// RefusedSquadTakesState (T-515): a squad refused by another residents road (not the holder, notebook people pending, no
// notebook) inside a building's populateBuilding on this thread still takes the building-state step, as the unmodded game would
// have at that call - never while the engine frees the world (teardown), and never without a building.
inline int RefusedSquadTakesState(int inBuilding, int hasBuilding, int teardown)
{
    return (inBuilding != 0 && hasBuilding != 0 && teardown == 0) ? 1 : 0;
}

}   // namespace townreoffer
