#pragma once
// T-966: A BUILDING LOADED FROM A SAVE IS NOT FURNISHED A SECOND TIME. The pure half of the residents re-run's plan hold, ONE
// header compiled into the plugin (towngen.cpp) and the offline suite.
//
// The re-run (towngen.cpp T392RerunBuilding) calls the engine's populateBuilding 0x57ED90 to add a building's residents. That
// call also switches the building's interior on (decomp_57ed90:77-78), and the switch-on (561750:14-15) loads the interior's
// furniture plan (Layout::loadLayout 0x550810, vt+0x58 of interior+0x30) under the interior's own plan name (interior+0x98).
// loadLayout creates every piece of the plan (54dcf0:307) when the name is non-empty and the plan's own list is empty
// (550810:65-67). A building made from a save never had its interior switched on (57c1e0:1179-1199, the "serialised interior"
// flag of the save record) and its saved pieces sit in the interior's piece set (+0xE8, size at +0x108), never in the plan list
// (count +0x88) - so the switch-on would create the whole plan again over the saved pieces (T1450, T1438: a second box at each
// box's spot). The save record's flag is not kept on the live building, so the piece set is the evidence used here.
//
// Decide: hold the plan load (blank the name around the engine call, restore it after) only when the switch-on WOULD load a plan
// AND the interior already lists pieces. A building the engine never furnished (no piece listed) takes its plan as before.
namespace layouthold {

const int kWhyUnread = 0;       // the building or its interior could not be read: the re-run goes as before
const int kWhyNoInterior = 1;   // no interior: the switch-on does nothing
const int kWhyOn = 2;           // the interior is switched on already: the switch-on loads nothing (561750:14)
const int kWhyNoName = 3;       // the plan name is empty: loadLayout stops at once (550810:65)
const int kWhyPlanListed = 4;   // the plan's own list holds pieces: an unchanged name is not loaded again (550810:66-67)
const int kWhyNotSaved = 5;     // no piece is listed under the building: one never furnished takes its plan
const int kWhyNameLong = 6;     // the name does not fit the copy kept to restore it: not held (counted)
const int kWhyHold = 7;         // held: the name is blanked around the engine call and put back after it
const int kWhyCount = 8;

const unsigned long long kNameCap = 256;   // the restore copy's buffer: the name's characters plus the end mark

// read: the reads succeeded; hasInterior: Building+0x1F0 set; switchedOn: interior+0x10 set; nameLen: interior+0x98's size;
// planCount: interior+0x88; pieces: the piece set's size, interior+0x108.
inline int Decide(int read, int hasInterior, int switchedOn, unsigned long long nameLen, int planCount, unsigned long long pieces)
{
    if (read == 0) return kWhyUnread;
    if (hasInterior == 0) return kWhyNoInterior;
    if (switchedOn != 0) return kWhyOn;
    if (nameLen == 0) return kWhyNoName;
    if (planCount != 0) return kWhyPlanListed;
    if (pieces == 0) return kWhyNotSaved;
    if (nameLen >= kNameCap) return kWhyNameLong;
    return kWhyHold;
}
inline bool Holds(int why) { return why == kWhyHold; }
inline const char* WhyName(int why)
{
    switch (why)
    {
    case kWhyUnread: return "unread";
    case kWhyNoInterior: return "no-interior";
    case kWhyOn: return "switched-on";
    case kWhyNoName: return "no-plan-name";
    case kWhyPlanListed: return "plan-listed";
    case kWhyNotSaved: return "no-saved-pieces";
    case kWhyNameLong: return "name-too-long";
    case kWhyHold: return "held";
    default: return "?";
    }
}

}   // namespace layouthold
