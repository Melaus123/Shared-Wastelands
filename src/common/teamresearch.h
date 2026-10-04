#pragma once
// T-546 step 6 (t546d): SHARED RESEARCH - a game's decisions. Pure: the plugin (src/coop-plugin/team.cpp, store.cpp) and the
// offline suite (src/coop-test) compile this header. The wire and the world server's union are in teamwire.h (RESEARCH, Q lines).
//
// WHAT A GAME DOES (owner rule 2026-09-26: research is SHARED on joining; on leaving or removal each KEEPS what they have):
//   - once per world load and link while its player is in a team (and when the player enters one), after the load's own
//     research restore, it sends its whole finished list; the world server answers with the team's whole research;
//   - the whole research is applied ONCE, quietly: one Research::load of a record that is this game's own record (its queue and
//     every other field unchanged) with every tech it lacks appended as finished (ResearchMergeRecord);
//   - a tech its own player finishes afterwards (the setResearched hook) goes up as its own (ResearchOwnToSend - the echo rule);
//   - a tech a teammate finished arrives with the teammate's slot and is applied only when this game lacks it (ResearchNewToMe),
//     by the road its queue allows at that moment (ResearchApplyRoute): the engine's own setResearched (without the hook seeing
//     it as this player's own) when the tech is not queued here or is the queue's front, else one Research::load;
//   - every record Research::load gets has each finished tech taken out of the queue (ResearchMergeRecord);
//   - a list this game sends stays this game's until the world server answers it: a refusal (a NOTICE research-whole /
//     research-own) or no answer sends it again after a growing wait (ResearchResendWaitMs; ResearchDownKind reads an answer);
//   - leaving or removal takes nothing away (no code: nothing is ever removed).
// The message lines are the approved page's (build/pages/player-factions.html B7.8): ResearchSharedLine, ResearchCompleteLine.
#include "ownrec.h"
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace swteam {

/* the techs of `team` this game lacks: not in `mine`, and known to this game's data (`resolvable` - a stringID this game cannot
   find is never loaded nor counted); sorted, unique */
inline std::vector<std::string> ResearchNewToMe(const std::vector<std::string>& team, const std::vector<std::string>& mine, const std::set<std::string>& resolvable)
{
    const std::set<std::string> have(mine.begin(), mine.end());
    std::set<std::string> out;
    for (size_t i = 0; i < team.size(); ++i)
        if (!team[i].empty() && have.count(team[i]) == 0 && resolvable.count(team[i]) != 0) out.insert(team[i]);
    return std::vector<std::string>(out.begin(), out.end());
}

/* true when `key` is `prefix` followed by digits only ("current0" for "current", "current prog0" for "current prog") */
inline bool ResearchIndexKey(const std::string& key, const char* prefix)
{
    const size_t n = std::strlen(prefix);
    if (key.size() <= n || key.compare(0, n, prefix) != 0) return false;
    for (size_t i = n; i < key.size(); ++i) if (key[i] < '0' || key[i] > '9') return false;
    return true;
}
/* one entry of the research queue as Research::save writes it: "current<i>" (the tech) and "current prog<i>" (its progress) */
struct QueuedTech { std::string sid; float prog; QueuedTech() : prog(0.0f) {} };
/* the queue a Research::save record holds, front first: current0 .. current<num currents - 1>, each with its "current prog<i>"
   (0 when absent); an index with no stringID is skipped (Research::load finds no tech for it either) */
inline std::vector<QueuedTech> ResearchQueueOf(const coopown::ResearchRec& r)
{
    std::vector<QueuedTech> out;
    float nc = 0.0f;
    std::map<std::string, float> f;
    for (size_t i = 0; i < r.f.size(); ++i) { f[r.f[i].first] = r.f[i].second; if (r.f[i].first == "num currents") nc = r.f[i].second; }
    if (!(nc >= 1.0f)) return out;
    const int n = nc > 100000.0f ? 100000 : (int)nc;
    std::map<std::string, std::string> s;
    for (size_t i = 0; i < r.s.size(); ++i) s[r.s[i].first] = r.s[i].second;
    for (int i = 0; i < n; ++i)
    {
        char key[48]; std::sprintf(key, "current%d", i);
        std::map<std::string, std::string>::const_iterator it = s.find(key);
        if (it == s.end() || it->second.empty()) continue;
        QueuedTech q; q.sid = it->second;
        std::sprintf(key, "current prog%d", i);
        std::map<std::string, float>::const_iterator ft = f.find(key);
        if (ft != f.end()) q.prog = ft->second;
        out.push_back(q);
    }
    return out;
}

/* THE RECORD Research::load gets: `own` (this game's record as Research::save wrote it) with `add` appended as finished<n>,
   finished<n+1>, ... after its "num finished" (n), and "num finished" raised by their number; then every tech the record now
   holds as finished is taken out of the queue (Research::load rebuilds the queue from "current<i>" without checking): the other
   entries keep their order and their "current prog<i>", renumbered from 0, and "num currents" is their number. Every other
   field is kept unchanged; the queue is rewritten only when an entry leaves it. A tech already in `own`'s finished list is not
   added twice. *added = the techs appended, *dropped = the queue entries taken out (both 0 = `own` unchanged). */
inline void ResearchMergeRecord(const coopown::ResearchRec& own, const std::vector<std::string>& add, coopown::ResearchRec* out, int* added, int* dropped)
{
    *out = own; *added = 0; *dropped = 0;
    const std::vector<std::string> mine = coopown::ResearchFinishedSids(own);
    const std::set<std::string> have(mine.begin(), mine.end());
    float nf = 0.0f; int nfAt = -1;
    for (size_t i = 0; i < out->f.size(); ++i) if (out->f[i].first == "num finished") { nf = out->f[i].second; nfAt = (int)i; }
    int n = nf >= 1.0f ? (nf > 100000.0f ? 100000 : (int)nf) : 0;
    std::map<std::string, size_t> sAt;
    for (size_t i = 0; i < out->s.size(); ++i) sAt[out->s[i].first] = i;
    std::set<std::string> put;
    for (size_t k = 0; k < add.size(); ++k)
    {
        if (add[k].empty() || have.count(add[k]) != 0 || put.count(add[k]) != 0) continue;
        char key[32]; std::sprintf(key, "finished%d", n);
        std::map<std::string, size_t>::const_iterator it = sAt.find(key);
        if (it != sAt.end()) out->s[it->second].second = add[k];   /* a stale entry past "num finished" is overwritten */
        else { sAt[key] = out->s.size(); out->s.push_back(std::make_pair(std::string(key), add[k])); }
        put.insert(add[k]); ++n; ++*added;
    }
    if (*added > 0)
    {
        if (nfAt >= 0) out->f[(size_t)nfAt].second = (float)n;
        else out->f.push_back(std::make_pair(std::string("num finished"), (float)n));
    }
    const std::vector<std::string> fin = coopown::ResearchFinishedSids(*out);
    const std::set<std::string> done(fin.begin(), fin.end());
    const std::vector<QueuedTech> q = ResearchQueueOf(own);
    std::vector<QueuedTech> keep;
    for (size_t i = 0; i < q.size(); ++i) { if (done.count(q[i].sid) != 0) ++*dropped; else keep.push_back(q[i]); }
    if (*dropped > 0)
    {
        std::vector<std::pair<std::string, float> > f2;
        std::vector<std::pair<std::string, std::string> > s2;
        for (size_t i = 0; i < out->f.size(); ++i)
            if (out->f[i].first != "num currents" && !ResearchIndexKey(out->f[i].first, "current prog")) f2.push_back(out->f[i]);
        for (size_t i = 0; i < out->s.size(); ++i)
            if (!ResearchIndexKey(out->s[i].first, "current")) s2.push_back(out->s[i]);
        for (size_t k = 0; k < keep.size(); ++k)
        {
            char key[48]; std::sprintf(key, "current%d", (int)k);
            s2.push_back(std::make_pair(std::string(key), keep[k].sid));
            std::sprintf(key, "current prog%d", (int)k);
            f2.push_back(std::make_pair(std::string(key), keep[k].prog));
        }
        f2.push_back(std::make_pair(std::string("num currents"), (float)keep.size()));
        out->f.swap(f2); out->s.swap(s2);
    }
    if (*added > 0 || *dropped > 0) coopown::SortResearch(out);
}

/* HOW A TEAMMATE'S TECH IS APPLIED HERE, decided from this game's queue as it stands at that moment (front first). The engine's
   setResearched (decomp_833680) walks the queue and on ANY match pops the queue's FRONT, not the matching entry - the game itself
   calls it only for the front tech. So setResearched is the road when the tech is not queued here, or is the front entry and
   queued once; queued anywhere else, the tech goes in through Research::load (ResearchMergeRecord: this game's own record with
   the tech finished and taken out of the queue, every other entry's progress kept). */
enum { kResViaSetResearched = 0, kResViaLoad = 1 };
inline int ResearchApplyRoute(const std::vector<QueuedTech>& queue, const std::string& tech)
{
    int seen = 0; bool front = false;
    for (size_t i = 0; i < queue.size(); ++i) if (queue[i].sid == tech) { ++seen; if (i == 0) front = true; }
    return (seen == 0 || (seen == 1 && front)) ? kResViaSetResearched : kResViaLoad;
}

/* the wait before a list the world server has not accepted is sent again: 2 s, doubled at each resend, at most 60 s */
inline unsigned long ResearchResendWaitMs(int resends)
{
    unsigned long w = 2000;
    for (int i = 0; i < resends && w < 60000; ++i) w *= 2;
    return w > 60000 ? 60000 : w;
}

/* what a RESEARCH message from the world server is to this game (`mySlot`, -1 = none); bySlot names the game a reply answers:
   the answer to this game's whole list (whole, bySlot = me), the team's whole research grown by another member's list (whole,
   another bySlot or none), the answer to this game's own finishes (not whole, bySlot = me: the techs it accepted), or the techs a
   teammate finished (not whole, bySlot = that teammate) */
enum { kResDnTeamWhole = 0, kResDnWholeAnswer = 1, kResDnMateTech = 2, kResDnOwnAnswer = 3 };
inline int ResearchDownKind(bool whole, unsigned bySlot, int mySlot)
{
    const bool mine = mySlot >= 0 && bySlot == (unsigned)mySlot;
    if (whole) return mine ? kResDnWholeAnswer : kResDnTeamWhole;
    return mine ? kResDnOwnAnswer : kResDnMateTech;
}
/* `a` without the techs in `b`, order kept (what is still unanswered after an answer) */
inline std::vector<std::string> ResearchMinus(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    const std::set<std::string> gone(b.begin(), b.end());
    std::vector<std::string> out;
    for (size_t i = 0; i < a.size(); ++i) if (gone.count(a[i]) == 0) out.push_back(a[i]);
    return out;
}

/* THE ECHO RULE: of the techs this game's engine reported finished through the setResearched hook (`finishedHere`), only the
   ones this game's own player finished go up as its own - never one this game applied from a teammate (`applied`), nor one the
   team's research already holds (`held`, the last whole research or teammate's tech received); sorted, unique */
inline std::vector<std::string> ResearchOwnToSend(const std::vector<std::string>& finishedHere, const std::set<std::string>& applied, const std::set<std::string>& held)
{
    std::set<std::string> out;
    for (size_t i = 0; i < finishedHere.size(); ++i)
        if (!finishedHere[i].empty() && applied.count(finishedHere[i]) == 0 && held.count(finishedHere[i]) == 0) out.insert(finishedHere[i]);
    return std::vector<std::string>(out.begin(), out.end());
}

/* a research message from the world server is applied only to the team this game's player is in now (`myTeam`, 0 = none):
   a message for a team this player has left, or before the table says they joined, changes nothing */
inline bool ResearchForMyTeam(unsigned myTeam, unsigned msgTeam) { return myTeam != 0 && myTeam == msgTeam; }

/* the whole finished list goes up once per world load, link and team: due when this game is in a team and the list has not
   gone for that team on this link since the world was loaded (sentTeam 0 = not sent since the load) */
inline bool ResearchWholeDue(unsigned myTeam, unsigned sentTeam, int sentLink, int link)
{
    return myTeam != 0 && (sentTeam != myTeam || sentLink != link);
}

/* the approved lines (build/pages/player-factions.html, B7.8): "Shared research: 6 new technologies." and
   "Research complete: Iron Plates (Sam)." */
inline std::string ResearchSharedLine(int n)
{
    std::ostringstream o; o << "Shared research: " << n << (n == 1 ? " new technology." : " new technologies.");
    return o.str();
}
inline std::string ResearchCompleteLine(const std::string& tech, const std::string& member) { return "Research complete: " + tech + " (" + member + ")."; }

}   // namespace swteam
