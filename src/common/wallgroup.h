/* wallgroup.h - T-1408 fold 13 (F1418 / F1419, design B chosen 2026-11-19): a player's wall groups are the OWNER's, shared through the
 * piece record. Pure decisions only (no engine, no game state); offline tests in src/coop-test (t1408_fold13).
 *
 * THE CAUSE (F1418): each game formed wall groups itself - at placement and again at every area load (the engine's join 0x29AC10 walks an
 * in-memory tree of the area's buildings and joins the first neighbour that accepts, 0x29A850) - and nothing carried the result, so two
 * games grouped the same four walls differently (one group on one, two pairs on the other) and their build progress differed.
 *
 * THE DESIGN: the owner's game reads its group after the placement commit's own join and writes it into each wall's STATE as the GROUP
 * FACT (buildwire.h kBuildStateGroupBit): the lead's key and the wall's join index (0 = the lead, 1.. = the members in the lead's list
 * order). Every game - the owner's too - holds back the engine's own join for a wall whose record carries a fact (LoadJoinDecide), and
 * joins its pieces itself with the engine's 0x29A850 in the owner's order, on events (a copy made, a record arrival, an area's pieces
 * laid). A member waits until its lead's piece exists here. The engine never takes a wall out of a group, so a wall that is already in
 * another group (or holds members) cannot be moved: that is logged, never forced.
 */
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

namespace wallgroup {

/* ---- the engine's load-time join (the hook on 0x29AC10) ---- */
const int kLoadJoinSkip = 0, kLoadJoinRun = 1;
/* fold 13b (review): decided from the wall's OWN OWNER, never from the fact table (facts arrive by messages the world load does not wait
   for, and the engine has no leave routine). inCommit: the placement commit (0x4D6810) on this thread - the owner's own new placement, the
   owner decides; playerWall: a player owns the wall (an unreadable owner counts as a player's); joinToKnown: the engine's 0x29A850 has an
   address here - without it nothing could join a held wall, so nothing is held. */
inline int LoadJoinDecide(int inCommit, int playerWall, int joinToKnown)
{
    return (inCommit != 0 || playerWall == 0 || joinToKnown == 0) ? kLoadJoinRun : kLoadJoinSkip;
}

/* ---- one wall's join step from its fact ---- */
const int kJsNone = 0;         /* no fact, or the wall is its group's lead (nothing to join) */
const int kJsWaitLead = 1;     /* the lead's piece is not here yet: the member waits for the lead's event */
const int kJsDone = 2;         /* already in the lead's group */
const int kJsJoin = 3;         /* join it now: 0x29A850(lead, wall) */
const int kJsMemberBusy = 4;   /* the wall leads members of its own or is in another group - the engine never takes a wall out: logged */
const int kJsLeadElsewhere = 5;/* the lead's piece is itself in another group here - a join would land in that group: logged */
inline const char* JoinStepName(int s)
{
    static const char* const n[] = { "none", "waits for its lead", "already joined", "join", "in another group (not moved)", "lead in another group (not joined)" };
    return (s >= 0 && s <= 5) ? n[s] : "?";
}
/* idx: the wall's join index (0 = the lead); leadHere: the lead's piece exists here; inLeadGroup: the wall's record is the lead's (joined);
   wallHasLead: the wall is in some group as a member; wallMembers: how many members the wall itself leads; leadHasLead: the lead's piece
   is a member of another group here */
inline int JoinStep(int hasFact, unsigned idx, int leadHere, int inLeadGroup, int wallHasLead, int wallMembers, int leadHasLead)
{
    if (hasFact == 0 || idx == 0) return kJsNone;
    if (leadHere == 0) return kJsWaitLead;
    if (inLeadGroup != 0) return kJsDone;
    if (leadHasLead != 0) return kJsLeadElsewhere;
    if (wallHasLead != 0 || wallMembers > 0) return kJsMemberBusy;
    return kJsJoin;
}

/* ---- the owner's member order ---- */
/* the members waiting on one lead, joined in the owner's order: by join index, then key. DECIDED (manager, 2026-11-19): indices stay
   numbered per owner (each owner numbers its own walls by their place in the lead's list on its own engine); two owners' walls in one group
   may share an index, and the key breaks that tie the same way on every game, so every game joins in one order */
/* fold 13d: a missing lower-index member is waited for only while its copy can come here (its area loaded on this game) */
inline int OrderWaits(int areaLoadedHere) { return areaLoadedHere != 0 ? 1 : 0; }
inline void JoinOrder(std::vector<std::pair<unsigned, std::string> >* v)
{
    std::sort(v->begin(), v->end());
}
/* fold 13b: the owner's join index of a member = its place in the lead's list (+0x398 entries, 0-based) + 1 - the engine's own order,
   read again after every engine join of the owner's walls (the owner decides) */
inline unsigned IdxOfListPos(unsigned pos) { return pos + 1u; }
/* the record's fact names this key the lead (index 0) */
inline int RecNamesLead(const std::string& recLead, unsigned recIdx, const std::string& key)
{
    return (recIdx == 0 && !recLead.empty() && recLead == key) ? 1 : 0;
}

/* ---- the owner's progress on a copy (narrows buildwire.h BuildWallCopyAction's ownState input) ---- */
/* wallOwn: the copy keeps its own record (1), a group's (0), unreadable (-1); ownRec: the record is THIS player's own (its own road, unchanged);
   copyLeads: the copy's record is its own +0x160 (it is its group's lead here); recLeads: the record's fact names this key as the lead
   (index 0). 1 = the copy takes the owner's progress (once per record: the lead's record carries the group's value, written once into the
   lead copy's record that every member copy here reads); 0 = materials only (a member's record, or a group the owner's fact does not
   name). */
/* fold 13b: stampNewer - the record's group stamp is above the last one this lead copy wrote, so an older value never lands over a newer
   one (a hit) */
/* fold 13c: have = the lead copy wrote an owner's value before; a first value passes whatever its stamp (0 included), a later one only
   with a higher stamp */
inline int LeadStampNewer(int have, unsigned last, unsigned rec) { return (have == 0 || rec > last) ? 1 : 0; }
inline int CopyOwnState(int wallOwn, int ownRec, int copyLeads, int recLeads, int stampNewer)
{
    if (wallOwn > 0) return 1;
    return (ownRec == 0 && wallOwn == 0 && copyLeads != 0 && recLeads != 0 && stampNewer != 0) ? 1 : 0;
}

}   /* namespace wallgroup */
