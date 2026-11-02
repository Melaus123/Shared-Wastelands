/* stewardrules.h - WHAT A GAME MADE THE WORLD'S STEWARD DOES WITH THE WORLD'S RULES. Pure: no I/O, no globals.

   The world server's authority flag (WELCOME, AUTH) names the STEWARD (worldroles.h): it runs the technical duties and sets no
   rules - the world's options are its owner's and admins'. Before the duties could be handed over the steward was always the owner,
   so this game's own option values were the world's whenever it held the flag. KeepsWorldRules: a game given the flag while it is
   linked and in a running world is a steward taking the duties over (the owner away, "keep this world running") - it follows the
   world's rule record, whether that record reached it before the flag or arrives after, and never sends its own values as the
   world's (the world server ignores them, so the game would play by other options than everyone else). A game that launched this
   world's server itself (startedThisServer) is the owner coming back: it is the rule-setter again, but when it joined following the
   world's record or its HOST press joined the world running (KeepsRecordAsRuleSetter below) it keeps the record's values rather than putting its own back - only a change it
   makes from then on goes to the world. A game given the flag at its WELCOME (the first game of a new world) sets the rules with
   its own values as before.

   FollowedRecordGen / JoinedRunningWorld / KeepsRecordAsRuleSetter: "every game follows the record, the owner too". The rule-setter
   keeps the world's record (restores and sends nothing of its own: gameplay-tab values, host's Game options) for either of two
   reasons, and only while the record it knows holds at least one rule (a world whose rules were never set has nothing to keep):
   - it FOLLOWED the record on this link: a record holding rules reached it while it was NOT the rule-setter (its live values became
     the record's). A new link starts clean - the record is forgotten at the link's down edge, and the next link's WELCOME says
     again who sets the rules.
   - its HOST press JOINED the world while that world was already running (kept running, its owner away) instead of starting a
     world server, and the link is in that same world. This holds from the press itself, so it does not depend on the order in which
     the flag (WELCOME or AUTH) and the record (OPTIONS) arrive, and it holds across a link drop and re-link in that world: the owner
     coming back to its kept world - empty or occupied - plays by the world's rules. Another HOST or JOIN press ends it. */
#ifndef STEWARDRULES_H
#define STEWARDRULES_H

#include <string>
#include <vector>
#include <utility>
#include <map>

namespace stewardrules {

inline bool KeepsWorldRules(bool relay, bool running, bool startedThisServer)
{
    return relay && running && !startedThisServer;
}

/* A world rule: the advanced options (gp.*) and the gameplay-tab settings (gt.*) - the only rows the world record holds besides the
   notebook's @refused reply. */
inline bool IsRuleKey(const std::string& key)
{
    return key.compare(0, 3, "gp.") == 0 || key.compare(0, 3, "gt.") == 0;
}

/* The link generation on which a record holding rules last reached this game while it was not the rule-setter (-1 = none yet).
   Called at every complete record arrival with the generation of the link it came on; a record with no rule marks nothing. */
inline int FollowedRecordGen(int was, int linkGen, bool ruleSetterAtArrival, bool recordHasRules)
{
    return (ruleSetterAtArrival || !recordHasRules) ? was : linkGen;
}

/* This game's HOST press joined the world joinedWorld while it ran (the world's folder name; empty = no such press), and the link
   is in world linkWorld (the folder whose record index this game read; empty = none yet). */
inline bool JoinedRunningWorld(const std::string& joinedWorld, const std::string& linkWorld)
{
    return !joinedWorld.empty() && joinedWorld == linkWorld;
}

/* The rule-setter keeps the world's record (restores and sends nothing of its own) when it followed that record on THIS link, or
   when its HOST press joined this running world (joinedRunning) - unless the record is known and holds no rule (recordWithoutRules):
   a world whose rules were never set has nothing to keep, and its rule-setter's own values become the world's. */
inline bool KeepsRecordAsRuleSetter(bool ruleSetter, int followedGen, int linkGen, bool joinedRunning, bool recordWithoutRules)
{
    if (!ruleSetter || recordWithoutRules) return false;
    return (followedGen >= 0 && followedGen == linkGen) || joinedRunning;
}

/* THE RULES FILE DECIDES. The host's own values - its live advanced options and gameplay-tab settings, and the GAME
   OPTIONS choices its HOST press carries - go into the world's record only at the world's FIRST HOSTING: a HOST press that starts the
   world server for a world folder holding neither rules.txt nor options.txt (no world server has ever stored rules for it). After
   that every HOST press keeps the record, whoever presses it and whether the world was running or not; a rule changes only through
   an admin's change made in the game or a hand edit of rules.txt. */
inline bool FirstHostingPress(bool hostPress, bool startsServer, bool rulesFileThere, bool optionsFileThere)
{
    return hostPress && startsServer && !rulesFileThere && !optionsFileThere;
}
/* The rule-setter keeps the world's record (puts none of its own values back, sends none) unless its HOST press is this world's first
   hosting, or the record is known and holds no rule (a world whose rules were never set: its rule-setter's values fill the rows the
   record lacks; its HOST screen's rows are sent as the owner's change). */
inline bool RuleSetterKeepsRecord(bool ruleSetter, bool firstHosting, bool recordWithoutRules)
{
    return ruleSetter && !firstHosting && !recordWithoutRules;
}
/* THE GAME OPTIONS ROWS A HOST PRESS SENDS (ui.cpp PanelOptHandOver). chosen: the rows the player changed on GAME OPTIONS;
   forThisWorld: they were chosen for the world this press hosts; handed: the rows (key and value) that already went with an earlier
   press or Done for that world. A world's first hosting: every chosen row, as the host's settings that fill the world's rules. A world
   that already has rules: the chosen rows not already handed at the same value, sent as the owner's change - the world's other rules
   stay as they are, and a row that already went is not sent again over a later rules.txt edit. Rows chosen for another world: none. */
enum { kPressSendsNone = 0, kPressSendsHostSettings = 1, kPressSendsOwnerChange = 2 };
typedef std::vector<std::pair<std::string, std::string> > OptionRows;
inline int HostPressRows(bool firstHosting, bool forThisWorld, const OptionRows& chosen, const OptionRows& handed, OptionRows* out)
{
    out->clear();
    if (!forThisWorld || chosen.empty()) return kPressSendsNone;
    if (firstHosting) { *out = chosen; return kPressSendsHostSettings; }
    for (size_t k = 0; k < chosen.size(); ++k)
    {
        bool went = false;
        for (size_t h = 0; h < handed.size() && !went; ++h) went = handed[h].first == chosen[k].first && handed[h].second == chosen[k].second;
        if (!went) out->push_back(chosen[k]);
    }
    return out->empty() ? kPressSendsNone : kPressSendsOwnerChange;
}
/* THE ROWS THAT WENT: a HOST-screen row counts as gone (HostPressRows' `handed`) only once the world has STORED it - the world's record
   (its OPTIONS map) holds that key at that value. TakeStored moves such rows from `awaiting` to `stored`; a press that never reached the
   world leaves its rows awaiting, so the next press sends them again. MarkStored lays stored rows into `handed` (a key already there takes
   the stored value). */
inline void MarkStored(OptionRows* handed, const OptionRows& stored)
{
    for (size_t s = 0; s < stored.size(); ++s)
    {
        bool have = false;
        for (size_t h = 0; h < handed->size() && !have; ++h)
            if ((*handed)[h].first == stored[s].first) { (*handed)[h].second = stored[s].second; have = true; }
        if (!have) handed->push_back(stored[s]);
    }
}
/* THE WORLD'S VALUES the stored check reads: every key and value of one OPTIONS map from the world - the multiplayer tab's rows as well
   as the gp.* / gt.* ones, since a HOST press can carry any of them. A refusal reply's `@refused` is not a value of the world. */
inline void NoteWorldValue(std::map<std::string, std::string>* values, const std::string& key, const std::string& value)
{
    if (key != "@refused") (*values)[key] = value;
}
inline void TakeStored(OptionRows* awaiting, const std::map<std::string, std::string>& record, OptionRows* stored)
{
    for (size_t k = awaiting->size(); k-- > 0;)
    {
        std::map<std::string, std::string>::const_iterator it = record.find((*awaiting)[k].first);
        if (it == record.end() || it->second != (*awaiting)[k].second) continue;
        MarkStored(stored, OptionRows(1, (*awaiting)[k]));
        awaiting->erase(awaiting->begin() + k);
    }
}

}

#endif
